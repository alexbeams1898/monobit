# Claude Code Instructions

## Pre-Push Verification

Before creating a PR or pushing fixes for a failing pipeline, ALWAYS run ALL pipeline stages locally to verify they pass:

1. **Format check** (clang-format):
   ```bash
   find engine game -name '*.cpp' -o -name '*.h' | xargs /c/msys64/mingw64/bin/clang-format.exe --dry-run --Werror
   ```

2. **Build**:
   ```bash
   cmake --build build
   ```

3. **Lint** (clang-tidy -- run on changed .cpp files):
   ```bash
   # Example for specific files:
   /c/msys64/mingw64/bin/clang-tidy.exe -p build --warnings-as-errors='*' <file.cpp>
   ```

4. **Tests**:
   ```bash
   ctest --test-dir build --output-on-failure
   ```

Never push code that hasn't been verified against all pipeline stages. If clang-tidy is not available locally, at minimum run clang-format and build+test before pushing.
