# OAR Test Framework

## Overview

The OAR test framework (`test_framework.h` / `test_framework.c`) provides a
lightweight, header-based test harness for the `tests/examples/` suite. It
standardises how test cases are declared, executed, and reported, and provides
**crash isolation** so that a single test failure (including SIGABRT or
segfault) does not prevent subsequent tests from running.

## Key Features

- **Assertion macros** — `TEST_ASSERT`, `TEST_ASSERT_EQ`, `TEST_ASSERT_NE`
- **Test table registration** — declare a `test_entry_t[]` array and call
  `run_all_tests()`
- **Crash isolation** — each test runs in a child process (POSIX `fork()` /
  Windows `_spawnl`)
- **Cross-platform** — works on Linux, macOS, and Windows (MSVC)
- **Summary report** — per-test PASSED/FAILED/CRASHED status table

## Quick Start

### 1. Write test functions

Each test function has the signature `int (void)` and returns `TEST_PASS` (0)
or `TEST_FAIL` (-1). Use the assertion macros for checks:

```c
#include "test_framework.h"

static int test_my_feature(void) {
    TEST_ASSERT(1 + 1 == 2, "math is broken");
    TEST_ASSERT_EQ(some_function(), 0, "some_function should return 0");
    return TEST_PASS;
}
```

### 2. Declare a test table

```c
static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "my feature works", test_my_feature),
    TEST_ENTRY("TC2", "another test", test_another),
};
```

### 3. Write main()

```c
int main(int argc, char *argv[]) {
    return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
```

### 4. Build

Add the test source file and `test_framework.c` to the build:

**CMake** (`CMakeLists.txt`):
```cmake
add_executable(test_my_feature test_my_feature.c test_framework.c)
target_link_libraries(test_my_feature PRIVATE oar)
set_property(TARGET test_my_feature PROPERTY C_STANDARD 99)
```

**Bazel** (`BUILD.bazel`):
```python
cc_test(
    name = "test_my_feature",
    srcs = [
        "test_my_feature.c",
        "test_framework.c",
    ],
    deps = ["//:oar"],
)
```

## API Reference

### Macros

| Macro | Description |
|-------|-------------|
| `TEST_ASSERT(cond, msg)` | Assert `cond` is true; on failure, print `msg` + line number and return `TEST_FAIL` |
| `TEST_ASSERT_EQ(actual, expected, msg)` | Assert `actual == expected` |
| `TEST_ASSERT_NE(actual, unexpected, msg)` | Assert `actual != unexpected` |
| `TEST_START(name)` | Print a visual separator with the test name (optional) |
| `TEST_ENTRY(name, desc, fn)` | Initialise a `test_entry_t` |
| `NUM_TESTS(array)` | Number of elements in a static test array |

### Types

| Type | Description |
|------|-------------|
| `test_fn_t` | `typedef int (*test_fn_t)(void)` — test function pointer |
| `test_entry_t` | `{ const char *name; const char *description; test_fn_t fn; }` |

### Functions

| Function | Description |
|----------|-------------|
| `run_all_tests(tests, num_tests, argc, argv)` | Run all tests with crash isolation; returns 0 if all passed |

## Crash Isolation

Each test runs in a separate child process:

- **POSIX**: `fork()` + `waitpid()` — the child runs the test and exits with
  the result; the parent interprets the exit status (including signals).
- **Windows**: `_spawnl(_P_WAIT, ...)` re-invokes the executable with
  `--child <index>` to run a single test.

This means a test that calls `abort()` or segfaults will be reported as
`CRASHED` but will not affect other tests.

## Adding a New Test File

1. Create `test_my_feature.c` in `tests/examples/`
2. `#include "test_framework.h"` and the OAR headers you need
3. Write test functions returning `TEST_PASS` / `TEST_FAIL`
4. Declare a `g_tests[]` array with `TEST_ENTRY` macros
5. Write `main()` calling `run_all_tests()`
6. Add the executable to `CMakeLists.txt` and `BUILD.bazel` (include
   `test_framework.c` in the sources)
