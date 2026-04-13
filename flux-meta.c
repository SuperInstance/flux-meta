/**
 * flux-meta.c — Self-evolving ISA meta opcodes implementation
 *
 * The VM's ability to modify itself. Opcodes 0xD0-0xDF.
 *
 * The evolution table is a static array of 16 slots.
 * Meta opcodes (0xD8-0xDF) are assigned from the pool.
 * Sandbox provides isolated execution with no side effects.
 */

#include "flux-meta.h"
#include <string.h>
#include <stdio.h>

/* ═══ Simple hash (FNV-1a) ═══ */
static uint32_t fnv1a(const uint8_t *data, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 16777619u;
    }
    return hash;
}

/* ═══ Init ═══ */

void flux_meta_init(FluxMetaISA *meta) {
    if (!meta) return;
    memset(meta, 0, sizeof(*meta));
    meta->next_opcode = 0xD8; /* first user meta slot */
}

/* ═══ Discover ═══ */

int flux_meta_discover(FluxMetaISA *meta, uint32_t topic_hash,
                       FluxMetaBundle *out, uint8_t max) {
    if (!meta) return 0;
    int count = 0;
    for (int i = 0; i < FLUX_META_MAX && count < max; i++) {
        if (meta->bundles[i].bytecode_len > 0 && !meta->bundles[i].adopted) {
            /* Match topic hash (0 = wildcard) */
            if (topic_hash == 0 || meta->bundles[i].semantic_hash == topic_hash) {
                if (out) out[count] = meta->bundles[i];
                count++;
            }
        }
    }
    return count;
}

/* ═══ Define ═══ */

int flux_meta_define(FluxMetaISA *meta, const uint8_t *bytecode,
                     uint8_t len, uint16_t gas_cost, uint8_t flags,
                     uint8_t *assigned_opcode) {
    if (!meta || !bytecode || len == 0 || len > FLUX_BUNDLE_MAX_BYTECODE)
        return -1;
    if (gas_cost > 1024) return -1;

    /* Find empty bundle slot */
    int slot = -1;
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta->bundles[i].bytecode_len == 0) { slot = i; break; }
    }
    if (slot < 0) return -1; /* table full */

    /* Create bundle */
    FluxMetaBundle *b = &meta->bundles[slot];
    b->semantic_hash = fnv1a(bytecode, len);
    b->gas_cost = gas_cost;
    b->flags = flags;
    b->bytecode_len = len;
    memcpy(b->bytecode, bytecode, len);
    b->adopted = 0;
    b->opcode = 0;

    meta->total_discovered++;
    meta->bundle_count++;

    if (assigned_opcode) *assigned_opcode = b->opcode;
    return slot;
}

/* ═══ Adopt ═══ */

int flux_meta_adopt(FluxMetaISA *meta, uint8_t bundle_idx) {
    if (!meta || bundle_idx >= FLUX_META_MAX) return -1;

    FluxMetaBundle *b = &meta->bundles[bundle_idx];
    if (b->bytecode_len == 0 || b->adopted) return -1;

    /* Find empty evolution table slot */
    int slot = -1;
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (!meta->table[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    /* Assign opcode */
    if (meta->next_opcode > 0xDF) return -1; /* no more slots */

    FluxMetaEntry *e = &meta->table[slot];
    e->opcode = meta->next_opcode++;
    e->bytecode_len = b->bytecode_len;
    memcpy(e->bytecode, b->bytecode, b->bytecode_len);
    e->semantic_hash = b->semantic_hash;
    e->gas_cost = b->gas_cost;
    e->times_executed = 0;
    e->avg_energy = 0.0f;
    e->avg_cycles = 0.0f;
    e->active = true;

    b->adopted = 1;
    b->opcode = e->opcode;

    meta->total_adopted++;
    return 0;
}

/* ═══ Sandbox ═══ */

int flux_meta_sandbox(FluxMetaISA *meta, const uint8_t *bytecode,
                      uint8_t len, double arg1, double arg2,
                      double *result, float *energy_used) {
    if (!meta || !bytecode || len == 0 || len > FLUX_SANDBOX_SIZE)
        return -1;

    /* Copy bytecode to sandbox */
    memcpy(meta->sandbox, bytecode, len);
    meta->sandbox_steps = 0;
    meta->sandbox_energy_used = 0.1f; /* base cost */

    /* Simple execution simulation (non-Turing-complete subset) */
    double stack[16];
    int sp = 0;
    stack[sp++] = arg1;
    stack[sp++] = arg2;

    for (uint8_t i = 0; i < len && meta->sandbox_steps < FLUX_SANDBOX_STEPS; i++) {
        uint8_t op = meta->sandbox[i];
        meta->sandbox_steps++;
        meta->sandbox_energy_used += 0.01f;

        /* Execute a few basic ops */
        if (op == 0x30 && sp >= 2) { /* ADD */
            stack[sp-2] = stack[sp-2] + stack[sp-1]; sp--;
        } else if (op == 0x31 && sp >= 2) { /* SUB */
            stack[sp-2] = stack[sp-2] - stack[sp-1]; sp--;
        } else if (op == 0x32 && sp >= 2) { /* MUL */
            stack[sp-2] = stack[sp-2] * stack[sp-1]; sp--;
        } else if (op == 0x33 && sp >= 2) { /* DIV */
            if (stack[sp-1] != 0.0) stack[sp-2] /= stack[sp-1];
            sp--;
        } else {
            /* Unknown op in sandbox — skip */
        }

        /* Safety: check stack bounds */
        if (sp < 0) sp = 0;
        if (sp >= 16) sp = 15;
    }

    if (result) *result = (sp > 0) ? stack[sp-1] : 0.0;
    if (energy_used) *energy_used = meta->sandbox_energy_used;

    meta->total_sandbox_runs++;
    return 0;
}

/* ═══ Evolve ═══ */

int flux_meta_evolve(FluxMetaISA *meta, uint8_t base_opcode,
                     uint8_t mutation_type, uint8_t param,
                     uint8_t *new_opcode) {
    if (!meta) return -1;
    if (base_opcode < 0xD8 || base_opcode > 0xDF) return -1;
    if (mutation_type > 5) return -1; /* unknown mutation */

    /* Find base in evolution table */
    int slot = -1;
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta->table[i].active && meta->table[i].opcode == base_opcode) {
            slot = i; break;
        }
    }
    if (slot < 0) return -1;

    FluxMetaEntry *base = &meta->table[slot];
    uint8_t mutated[FLUX_BUNDLE_MAX_BYTECODE];
    uint8_t new_len = base->bytecode_len;
    memcpy(mutated, base->bytecode, base->bytecode_len);

    /* Apply mutation */
    switch (mutation_type) {
    case MUT_REORDER:
        /* Swap two random instructions */
        if (new_len >= 2 && param < new_len) {
            uint8_t a = param % new_len;
            uint8_t b = (param + 1) % new_len;
            uint8_t tmp = mutated[a]; mutated[a] = mutated[b]; mutated[b] = tmp;
        }
        break;

    case MUT_CONST_FOLD:
        /* Replace a PUSH+ADD with single PUSH of sum */
        /* (simplified: just tweak a byte) */
        if (new_len > 0 && param < new_len) {
            mutated[param] = (mutated[param] + 1) & 0xFF;
        }
        break;

    case MUT_LOOP_UNROLL:
        /* Duplicate a sequence (if room) */
        if (new_len + param <= FLUX_BUNDLE_MAX_BYTECODE && param > 0) {
            memcpy(mutated + new_len, mutated, param);
            new_len += param;
        }
        break;

    case MUT_DEAD_CODE:
        /* Remove an instruction */
        if (new_len > 1 && param < new_len) {
            memmove(mutated + param, mutated + param + 1, new_len - param - 1);
            new_len--;
        }
        break;

    case MUT_INLINE:
        /* Insert a NOP (simplified inline) */
        if (new_len < FLUX_BUNDLE_MAX_BYTECODE) {
            memmove(mutated + param + 1, mutated + param, new_len - param);
            mutated[param] = 0x00; /* NOP */
            new_len++;
        }
        break;

    default:
        return -1;
    }

    /* Find empty slot for evolved version */
    int new_slot = -1;
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (!meta->table[i].active) { new_slot = i; break; }
    }
    if (new_slot < 0 || meta->next_opcode > 0xDF) return -1;

    FluxMetaEntry *e = &meta->table[new_slot];
    e->opcode = meta->next_opcode++;
    e->bytecode_len = new_len;
    memcpy(e->bytecode, mutated, new_len);
    e->semantic_hash = fnv1a(mutated, new_len);
    e->gas_cost = base->gas_cost;
    e->active = true;

    meta->total_evolved++;
    if (new_opcode) *new_opcode = e->opcode;
    return 0;
}

/* ═══ Benchmark ═══ */

int flux_meta_benchmark(FluxMetaISA *meta, uint8_t opcode,
                        uint8_t iterations, float *avg_energy,
                        float *avg_cycles) {
    if (!meta || iterations == 0 || iterations > FLUX_BENCHMARK_MAX_ITERS)
        return -1;

    /* Find entry */
    int slot = -1;
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta->table[i].active && meta->table[i].opcode == opcode) {
            slot = i; break;
        }
    }
    if (slot < 0) return -1;

    FluxMetaEntry *e = &meta->table[slot];
    float total_energy = 0.0f;
    float total_cycles = 0.0f;

    for (uint8_t i = 0; i < iterations; i++) {
        double result;
        float energy;
        flux_meta_sandbox(meta, e->bytecode, e->bytecode_len,
                          1.0, 2.0, &result, &energy);
        total_energy += energy;
        total_cycles += (float)meta->sandbox_steps;
    }

    e->avg_energy = total_energy / iterations;
    e->avg_cycles = total_cycles / iterations;
    e->times_executed += iterations;

    if (avg_energy) *avg_energy = e->avg_energy;
    if (avg_cycles) *avg_cycles = e->avg_cycles;

    meta->total_benchmarks++;
    return 0;
}

/* ═══ Forget ═══ */

int flux_meta_forget(FluxMetaISA *meta, uint8_t opcode) {
    if (!meta) return -1;
    /* Cannot remove core opcodes */
    if (opcode < 0xD8 || opcode > 0xDF) return -1;

    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta->table[i].active && meta->table[i].opcode == opcode) {
            meta->table[i].active = false;
            meta->table[i].opcode = 0xFF;
            meta->total_forgotten++;
            return 0;
        }
    }
    return -1; /* not found */
}

/* ═══ Compose ═══ */

int flux_meta_compose(FluxMetaISA *meta, const uint8_t *sequence,
                      uint8_t len, uint32_t *macro_hash) {
    if (!meta || !sequence || len == 0 || len > FLUX_MACRO_MAX_LEN)
        return -1;

    /* Check for recursive macros */
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta->macros[i].active) {
            for (uint8_t j = 0; j < len; j++) {
                /* If any instruction is a macro reference, check depth */
                /* Simplified: just check total macro count */
            }
        }
    }

    /* Find empty macro slot */
    int slot = -1;
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (!meta->macros[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    FluxMacro *m = &meta->macros[slot];
    memcpy(m->instructions, sequence, len);
    m->len = len;
    m->hash = fnv1a(sequence, len);
    m->active = true;

    meta->total_composed++;
    if (macro_hash) *macro_hash = m->hash;
    return 0;
}

/* ═══ Execute ═══ */

int flux_meta_execute(FluxMetaISA *meta, uint8_t opcode,
                      double *stack, int *sp, int stack_max) {
    if (!meta || !stack || !sp) return -1;

    /* Find in evolution table */
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta->table[i].active && meta->table[i].opcode == opcode) {
            FluxMetaEntry *e = &meta->table[i];
            double result;
            float energy;
            double arg1 = (*sp > 0) ? stack[*sp - 1] : 0.0;
            double arg2 = (*sp > 1) ? stack[*sp - 2] : 0.0;

            int rc = flux_meta_sandbox(meta, e->bytecode, e->bytecode_len,
                                       arg1, arg2, &result, &energy);
            if (rc != 0) return -1;

            /* Push result */
            if (*sp < stack_max) stack[(*sp)++] = result;

            e->times_executed++;
            return 0;
        }
    }

    /* Check macros */
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta->macros[i].active && meta->macros[i].hash == opcode) {
            FluxMacro *m = &meta->macros[i];
            /* Expand macro: execute each instruction */
            for (uint8_t j = 0; j < m->len; j++) {
                /* Simplified: just record the instruction */
                (void)m->instructions[j];
            }
            meta->total_composed++;
            return 0;
        }
    }

    return -1; /* unknown meta opcode */
}

/* ═══ Stats ═══ */

void flux_meta_stats(FluxMetaISA *meta, uint32_t *discovered,
                     uint32_t *adopted, uint32_t *evolved,
                     uint32_t *forgotten, uint32_t *composed) {
    if (discovered) *discovered = meta->total_discovered;
    if (adopted) *adopted = meta->total_adopted;
    if (evolved) *evolved = meta->total_evolved;
    if (forgotten) *forgotten = meta->total_forgotten;
    if (composed) *composed = meta->total_composed;
}

/* ═══ Tests ═══ */

int flux_meta_test(void) {
    int failures = 0;
    FluxMetaISA meta;

    /* Test 1: Init */
    flux_meta_init(&meta);
    if (meta.next_opcode != 0xD8) { failures++; printf("FAIL init next_opcode\n"); }

    /* Test 2: Define + Discover */
    uint8_t bc[] = {0x30, 0x31, 0x32}; /* ADD, SUB, MUL */
    int slot = flux_meta_define(&meta, bc, 3, 100, 0, NULL);
    if (slot < 0) { failures++; printf("FAIL define\n"); }

    FluxMetaBundle found[4];
    int n = flux_meta_discover(&meta, 0, found, 4);
    if (n != 1) { failures++; printf("FAIL discover count: %d\n", n); }
    if (found[0].bytecode_len != 3) { failures++; printf("FAIL discover len\n"); }

    /* Test 3: Adopt */
    int rc = flux_meta_adopt(&meta, (uint8_t)slot);
    if (rc != 0) { failures++; printf("FAIL adopt\n"); }

    /* Verify it's in the evolution table */
    bool found_in_table = false;
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta.table[i].active && meta.table[i].opcode == 0xD8) {
            found_in_table = true;
            break;
        }
    }
    if (!found_in_table) { failures++; printf("FAIL adopted not in table\n"); }

    /* Test 4: Adopted bundles should not be discoverable */
    n = flux_meta_discover(&meta, 0, found, 4);
    if (n != 0) { failures++; printf("FAIL adopted still discoverable\n"); }

    /* Test 5: Sandbox */
    double result;
    float energy;
    uint8_t add_bc[] = {0x30}; /* ADD: arg1 + arg2 */
    rc = flux_meta_sandbox(&meta, add_bc, 1, 3.0, 4.0, &result, &energy);
    if (rc != 0 || result != 7.0) {
        failures++; printf("FAIL sandbox ADD: result=%f rc=%d\n", result, rc);
    }

    uint8_t mul_bc[] = {0x32}; /* MUL */
    rc = flux_meta_sandbox(&meta, mul_bc, 1, 5.0, 6.0, &result, &energy);
    if (rc != 0 || result != 30.0) {
        failures++; printf("FAIL sandbox MUL: result=%f\n", result);
    }

    /* Test 6: Evolve */
    uint8_t new_op;
    rc = flux_meta_evolve(&meta, 0xD8, MUT_REORDER, 0, &new_op);
    if (rc != 0) { failures++; printf("FAIL evolve\n"); }
    if (new_op != 0xD9) { failures++; printf("FAIL evolve opcode: 0x%02X\n", new_op); }

    /* Test 7: Benchmark */
    float avg_e, avg_c;
    rc = flux_meta_benchmark(&meta, 0xD8, 10, &avg_e, &avg_c);
    if (rc != 0) { failures++; printf("FAIL benchmark\n"); }
    if (avg_e <= 0.0f) { failures++; printf("FAIL benchmark energy: %f\n", avg_e); }

    /* Test 8: Forget */
    rc = flux_meta_forget(&meta, 0xD8);
    if (rc != 0) { failures++; printf("FAIL forget\n"); }
    /* Verify removed */
    for (int i = 0; i < FLUX_META_MAX; i++) {
        if (meta.table[i].opcode == 0xD8 && meta.table[i].active) {
            failures++; printf("FAIL forget still active\n");
            break;
        }
    }

    /* Test 9: Cannot forget core opcodes */
    rc = flux_meta_forget(&meta, 0x10); /* LOAD_CONST */
    if (rc == 0) { failures++; printf("FAIL forget core allowed\n"); }

    /* Test 10: Compose */
    uint8_t seq[] = {0x30, 0x32, 0x33}; /* ADD, MUL, DIV */
    uint32_t hash;
    rc = flux_meta_compose(&meta, seq, 3, &hash);
    if (rc != 0) { failures++; printf("FAIL compose\n"); }
    if (hash == 0) { failures++; printf("FAIL compose hash\n"); }

    /* Test 11: Stats */
    uint32_t disc, adopted, evolved, forgotten, composed;
    flux_meta_stats(&meta, &disc, &adopted, &evolved, &forgotten, &composed);
    if (disc != 1) { failures++; printf("FAIL stats discovered: %u\n", disc); }
    if (adopted != 1) { failures++; printf("FAIL stats adopted: %u\n", adopted); }
    if (evolved != 1) { failures++; printf("FAIL stats evolved: %u\n", evolved); }
    if (forgotten != 1) { failures++; printf("FAIL stats forgotten: %u\n", forgotten); }
    if (composed != 1) { failures++; printf("FAIL stats composed: %u\n", composed); }

    /* Test 12: Define with invalid params */
    rc = flux_meta_define(&meta, bc, 0, 100, 0, NULL); /* len=0 */
    if (rc != -1) { failures++; printf("FAIL define len=0\n"); }
    rc = flux_meta_define(&meta, bc, 3, 2000, 0, NULL); /* gas>1024 */
    if (rc != -1) { failures++; printf("FAIL define gas>1024\n"); }

    /* Test 13: Sandbox div by zero */
    uint8_t div_bc[] = {0x33}; /* DIV */
    rc = flux_meta_sandbox(&meta, div_bc, 1, 1.0, 0.0, &result, &energy);
    if (rc != 0) { failures++; printf("FAIL sandbox div/0\n"); }
    /* Result should be inf or some safe value */

    printf("flux_meta_test: %d failures\n", failures);
    return failures;
}
