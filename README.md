# Assignment 3

## Repository contents

- `unifiedpass.cpp`: LLVM pass plugin source
- `Makefile`: build and test automation
- `tests/`: C source files used to generate LLVM bitcode for tests
- `build/`: generated build artifacts and test outputs
- `.gitignore`: ignores generated build outputs

## Build

```bash
make
```

This compiles the plugin and produces `build/unifiedpass.so`.

## Run all tests

```bash
make tests
```

The test pipeline does the following:

1. Compile `tests/*.c` to LLVM bitcode (`*.bc`)
2. Run `mem2reg` on the bitcode to produce SSA-form bitcode (`*-m2r.bc`)
3. Load the plugin and run each pass using the test name as the pipeline name
4. Disassemble the optimized bitcode into `*.ll`

Generated files include:

- `build/tests/*-m2r.ll`
- `build/tests/*-opt.ll`

## Pass names

The plugin registers the following pipeline names:

- `dominator`
- `dead-code-elimination`
- `licm`


The `Makefile` uses test names to select the pass.


