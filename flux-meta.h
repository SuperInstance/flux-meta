/**
 * flux-meta.h — Self-evolving ISA meta opcodes (0xD0-0xDF)
 *
 * Design by DeepSeek-Reasoner (Task 2).
 * Implementation by JetsonClaw1.
 *
 * The meta extension lets the VM discover, define, sandbox, adopt,
 * evolve, benchmark, forget, and compose new opcodes at runtime.
 * This is how the ISA grows without human intervention.
 *
 * Safety model:
 *   - All new opcodes execute in sandboxed scratchpad (256 bytes)
 *   - Max 256 steps per sandbox execution
 *   - Energy capped at caller's remaining budget
 *   - Semantic hash prevents malicious injection
 *   - Cannot modify core opcodes (0x00-0xCF)
 *
 * Zero malloc. Static allocation. C11. No deps beyond -lm.
 */

#ifndef FLUX_META_H
#define FLUX_META_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ═══ Constants ═══ */
#define FLUX_META_BASE       0xD0
#define FLUX_META_MAX        16      /* max evolution table entries */
#define FLUX_SANDBOX_SIZE    256     /* sandbox scratchpad bytes */
#define FLUX_SANDBOX_STEPS   256     /* max execution steps in sandbox */
#define FLUX_MACRO_MAX_LEN   16      /* max instructions in macro */
#define FLUX_BUNDLE_MAX_BYTECODE 64  /* max bytecode in capability bundle */
#define FLUX_BENCHMARK_MAX_ITERS 100 /* max benchmark iterations */

/* ═══ Meta Opcodes ═══ */
#define OP_DISCOVER   0xD0  /* Poll network for new capabilities */
#define OP_DEFINE     0xD1  /* Define new opcode from bytecode */
#define OP_ADOPT      0xD2  /* Install validated opcode */
#define OP_SANDBOX    0xD3  /* Execute bytecode in isolation */
#define OP_EVOLVE     0xD4  /* Mutate existing opcode */
#define OP_BENCHMARK  0xD5  /* Profile opcode performance */
#define OP_FORGET     0xD6  /* Remove opcode from local ISA */
#define OP_COMPOSE    0xD7  /* Create macro-opcode from sequence */

/* ═══ Mutation Types (for OP_EVOLVE) ═══ */
#define MUT_REORDER       0  /* Reorder sub-operations */
#define MUT_CONST_FOLD    1  /* Constant folding */
#define MUT_LOOP_UNROLL   2  /* Loop unrolling */
#define MUT_DEAD_CODE     3  /* Remove dead code paths */
#define MUT_INLINE        4  /* Inline macro expansion */

/* ═══ Capability Bundle ═══ */
typedef struct {
    uint32_t semantic_hash;           /* SHA3-256 truncated to 32 bits */
    uint16_t gas_cost;                /* estimated energy cost */
    uint8_t  flags;                   /* privilege flags */
    uint8_t  bytecode_len;            /* length of bytecode */
    uint8_t  bytecode[FLUX_BUNDLE_MAX_BYTECODE]; /* the new opcode's implementation */
    uint8_t  adopted;                 /* has this been installed? */
    uint8_t  opcode;                  /* assigned opcode (0xD8-0xDF) */
} FluxMetaBundle;

/* ═══ Evolution Table Entry ═══ */
typedef struct {
    uint8_t  opcode;           /* 0xD8-0xDF or 0xFF = unused */
    uint8_t  bytecode_len;
    uint8_t  bytecode[FLUX_BUNDLE_MAX_BYTECODE];
    uint32_t semantic_hash;
    uint16_t gas_cost;
    uint32_t times_executed;
    float    avg_energy;
    float    avg_cycles;
    bool     active;
} FluxMetaEntry;

/* ═══ Macro Opcode ═══ */
typedef struct {
    uint8_t  instructions[FLUX_MACRO_MAX_LEN]; /* opcode sequence */
    uint8_t  len;
    uint32_t hash;
    bool     active;
} FluxMacro;

/* ═══ Meta ISA State ═══ */
typedef struct {
    FluxMetaEntry  table[FLUX_META_MAX];
    FluxMacro      macros[FLUX_META_MAX];
    uint8_t        next_opcode;       /* next available meta slot */
    uint8_t        bundle_count;      /* discovered but not adopted */
    FluxMetaBundle bundles[FLUX_META_MAX];
    
    /* Sandbox state */
    uint8_t  sandbox[FLUX_SANDBOX_SIZE];
    uint32_t sandbox_steps;
    float    sandbox_energy_used;
    
    /* Statistics */
    uint32_t total_discovered;
    uint32_t total_adopted;
    uint32_t total_evolved;
    uint32_t total_forgotten;
    uint32_t total_composed;
    uint32_t total_sandbox_runs;
    uint32_t total_benchmarks;
} FluxMetaISA;

/* ═══ API ═══ */

/** Initialize meta ISA */
void flux_meta_init(FluxMetaISA *meta);

/** Discover: check if new capabilities are available (stub for A2A) */
int flux_meta_discover(FluxMetaISA *meta, uint32_t topic_hash,
                       FluxMetaBundle *out, uint8_t max);

/** Define: create a new opcode definition from bytecode */
int flux_meta_define(FluxMetaISA *meta, const uint8_t *bytecode,
                     uint8_t len, uint16_t gas_cost, uint8_t flags,
                     uint8_t *assigned_opcode);

/** Adopt: install a validated bundle into the evolution table */
int flux_meta_adopt(FluxMetaISA *meta, uint8_t bundle_idx);

/** Sandbox: execute bytecode in isolated scratchpad */
int flux_meta_sandbox(FluxMetaISA *meta, const uint8_t *bytecode,
                      uint8_t len, double arg1, double arg2,
                      double *result, float *energy_used);

/** Evolve: mutate an existing meta opcode */
int flux_meta_evolve(FluxMetaISA *meta, uint8_t base_opcode,
                     uint8_t mutation_type, uint8_t param,
                     uint8_t *new_opcode);

/** Benchmark: profile a meta opcode */
int flux_meta_benchmark(FluxMetaISA *meta, uint8_t opcode,
                        uint8_t iterations, float *avg_energy,
                        float *avg_cycles);

/** Forget: remove a meta opcode from the table */
int flux_meta_forget(FluxMetaISA *meta, uint8_t opcode);

/** Compose: create macro from instruction sequence */
int flux_meta_compose(FluxMetaISA *meta, const uint8_t *sequence,
                      uint8_t len, uint32_t *macro_hash);

/** Execute: run a meta opcode if it exists in the table */
int flux_meta_execute(FluxMetaISA *meta, uint8_t opcode,
                      double *stack, int *sp, int stack_max);

/** Stats */
void flux_meta_stats(FluxMetaISA *meta, uint32_t *discovered,
                     uint32_t *adopted, uint32_t *evolved,
                     uint32_t *forgotten, uint32_t *composed);

/** Test all meta opcodes */
int flux_meta_test(void);

#endif
