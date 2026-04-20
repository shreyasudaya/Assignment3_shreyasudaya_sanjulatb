# Assignment 3

## Repository contents

- `unifiedpass.cpp`: LLVM pass plugin source
- `Makefile`: build and test automation
- `tests/`: C source files used to generate LLVM bitcode for tests
- `build/`: generated build artifacts and test outputs
- `.gitignore`: ignores generated build outputs
- `lli-compare.sh` : shell script to compare dynamic instructions

## Build

```bash
make
```

This compiles the plugin and produces `build/unifiedpass.so`.

## Clean

```bash
make clean
```

Removes the entire `build/` directory and all generated artifacts.

## Run all tests

```bash
make tests
```

The test pipeline does the following:

1. Compile `tests/<pass>/*.c` to LLVM bitcode (`*.bc`) at `-O0`
2. Run `mem2reg` and `loop-simplify` to produce SSA-form bitcode (`*-m2r.bc`)
3. Load the plugin and run the pass named after the test subdirectory
4. Disassemble both the pre- and post-optimisation bitcode into `*.ll`

Generated files include:

- `build/tests/<pass>/<test>-m2r.ll` — SSA IR before the pass
- `build/tests/<pass>/<test>-opt.ll` — IR after the pass

## Verify correctness with lli

After running `make tests`, use the provided `lli-compare.sh` script to execute
the original and optimised bitcode with `lli` and diff the outputs:

```bash
# Loop Invariant Code Motion
./lli-compare.sh ./build/tests/loop-invariant-code-motion/

# Aggressive LICM
./lli-compare.sh ./build/tests/aggressive-licm/

# Dead Code Elimination
./lli-compare.sh ./build/tests/dead-code-elimination/
```

The script runs each `*-m2r.bc` and its corresponding `*-opt.bc` through `lli`
and reports whether the outputs match. A passing test prints no diff.

## Pass names

The plugin registers the following pipeline names, matched automatically by
the Makefile from the test subdirectory name:

| Subdirectory | Pass |
|---|---|
| `tests/dominators/` | `dominators` |
| `tests/dead-code-elimination/` | `dead-code-elimination` |
| `tests/loop-invariant-code-motion/` | `loop-invariant-code-motion` |
| `tests/aggressive-licm/` | `aggressive-licm` |