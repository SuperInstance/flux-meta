# flux-meta

Self-evolving ISA extension for FLUX VM. Opcodes 0xD0-0xDF.

Designed by DeepSeek-Reasoner (Task 2). Implemented by JetsonClaw1.

## Meta Opcodes

| Opcode | Name | Description |
|--------|------|-------------|
| 0xD0 | DISCOVER | Poll network for new capabilities |
| 0xD1 | DEFINE | Define new opcode from bytecode |
| 0xD2 | ADOPT | Install validated opcode |
| 0xD3 | SANDBOX | Execute in isolated environment |
| 0xD4 | EVOLVE | Mutate existing opcode |
| 0xD5 | BENCHMARK | Profile performance |
| 0xD6 | FORGET | Remove opcode |
| 0xD7 | COMPOSE | Create macro-opcode |

## Safety

- All new opcodes execute in sandboxed scratchpad (256 bytes)
- Max 256 steps per sandbox execution
- Energy capped at caller budget
- Cannot modify core opcodes (0x00-0xCF)
- Semantic hash prevents malicious injection

## Build

```bash
gcc -std=c99 -O2 -o test_flux_meta test_flux_meta.c flux-meta.c -lm
./test_flux_meta
```

## Tests

13/13 passing.

## Zero Dependencies

C11. No malloc. Static allocation. -lm only.

---

Part of the [Cocapn Fleet](https://github.com/Lucineer). See also: [flux-runtime-c](https://github.com/Lucineer/flux-runtime-c), [deepseek-reasoner-vessel](https://github.com/Lucineer/deepseek-reasoner-vessel), [mycorrhizal-relay](https://github.com/Lucineer/mycorrhizal-relay).