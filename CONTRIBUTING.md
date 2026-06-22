# Contributing to LibHttp2

Thank you for considering contributing to LibHttp2. This document explains how to build, test, and submit changes.

## Building

See the [README](README.md) for full build instructions. In short:

```bash
cd LibHttp2
mkdir build && cd build
cmake ..
cmake --build . --config RelWithDebInfo --parallel 4
```

CMake options:

| Option | Default | Description |
|--------|---------|-------------|
| `LIBHTTP2_BUILD_TESTS` | `ON` | Build test executables |
| `BUILD_SHARED_LIBS` | `OFF` | Build as shared library instead of static |
| `LIBHTTP2_ENABLE_ASAN` | `OFF` | Enable AddressSanitizer |
| `LIBHTTP2_ENABLE_UBSAN` | `OFF` | Enable UndefinedBehaviorSanitizer |
| `LIBHTTP2_ENABLE_TSAN` | `OFF` | Enable ThreadSanitizer |

## Running Tests

After building:

```bash
cd LibHttp2/build
ctest -C RelWithDebInfo --output-on-failure
```

To run a single test executable directly:

```bash
./huffman_test
./parse_test
```

**All tests must pass before submitting a pull request.**

## Code Style

This project uses clang-format with an LLVM-based configuration. The key rules:

- **Indent:** 4 spaces, no tabs
- **Column limit:** 100 characters
- **Braces:** Attach style (opening brace on the same line)
- **Pointer alignment:** Right-aligned (`int *p`, not `int* p`)
- **Standard:** C++11

Format your code before committing:

```bash
clang-format -i src/path/to/file.cc
```

The `.clang-format` file at the project root contains the full configuration. Use it as-is; do not override settings locally.

## Submitting Issues

When filing a bug report, include:

1. A clear description of the problem.
2. Steps to reproduce (minimal test case if possible).
3. Expected behavior vs. actual behavior.
4. Platform and compiler version (e.g., Ubuntu 22.04 / GCC 12, Windows 10 / MSVC 2022).

For feature requests, describe the use case and why existing functionality does not cover it.

## Submitting Pull Requests

1. **Fork the repository** and create a feature branch from `main`.
2. **Make your changes.** Keep commits focused -- one logical change per commit.
3. **Follow the code style.** Run clang-format on all modified files.
4. **Add or update tests** if your change affects behavior.
5. **Ensure all tests pass:**
   ```bash
   cd build && cmake --build . --config RelWithDebInfo --parallel 4 && ctest -C RelWithDebInfo --output-on-failure
   ```
6. **Write a clear commit message** using the convention below.
7. **Open a pull request** against `main`. Describe what the PR does and why.

## Commit Convention

Format: `type: message`

| Type | Purpose |
|------|---------|
| `feature` | New feature |
| `fix` | Bug fix |
| `refactor` | Code restructuring (no behavior change) |
| `perf` | Performance improvement |
| `docs` | Documentation |
| `test` | Tests |
| `chore` | Build, dependencies, tooling |
| `style` | Formatting (no logic change) |

Examples:
```
feature: add CONTINUATION frame support for large headers
fix: prevent integer overflow in frame length validation
refactor: extract HPACK encode/decode into separate files
```

## Testing Requirements

- All existing tests must pass.
- New features should include corresponding test cases.
- Bug fixes should include a test that reproduces the issue and verifies the fix.
- Tests use a lightweight assertion framework defined in `src/utils/testutil.h`. Each test file compiles to a separate executable via the `libhttp2_test()` CMake function.

## License

By contributing, you agree that your contributions will be licensed under the Apache License 2.0.
