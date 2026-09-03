/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the LICENSE file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

/**
 * @file test_framework.h
 * @brief Common test framework for OAR test suites.
 *
 * Provides assertion macros, a test-entry registration pattern, and a crash-
 * isolated test runner. Each test file defines its own test_entry_t array and
 * calls run_all_tests() from main().
 *
 * Usage example:
 *
 *   #include "test_framework.h"
 *
 *   static int test_something(void) {
 *       TEST_ASSERT(1 + 1 == 2, "math is broken");
 *       return TEST_PASS;
 *   }
 *
 *   static test_entry_t g_tests[] = {
 *       TEST_ENTRY("TC1", "something works", test_something),
 *   };
 *
 *   int main(int argc, char *argv[]) {
 *       return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
 *   }
 */

#ifndef OAR_TEST_FRAMEWORK_H
#define OAR_TEST_FRAMEWORK_H

#include <stdio.h>

/* --- Result codes ------------------------------------------------------- */

#define TEST_PASS 0
#define TEST_FAIL (-1)

/* --- Test function type and entry --------------------------------------- */

/** Test function signature: returns TEST_PASS (0) or TEST_FAIL (-1). */
typedef int (*test_fn_t)(void);

/** A single test case in a test table. */
typedef struct {
  const char *name;        /**< Short identifier, e.g. "TC1" */
  const char *description; /**< Human-readable summary */
  test_fn_t fn;            /**< Test function */
} test_entry_t;

/** Number of elements in a statically-declared test array. */
#define NUM_TESTS(array) ((int)(sizeof(array) / sizeof((array)[0])))

/** Convenience macro for initialising a test_entry_t. */
#define TEST_ENTRY(n, d, f) \
  { (n), (d), (f) }

/* --- Assertion macros --------------------------------------------------- */

/**
 * Assert that @p cond is true. On failure, prints @p msg and line number to
 * stderr, then returns TEST_FAIL from the enclosing function.
 */
#define TEST_ASSERT(cond, msg)                                  \
  do {                                                          \
    if (!(cond)) {                                              \
      fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); \
      return TEST_FAIL;                                         \
    }                                                           \
  } while (0)

/** Assert actual == expected. */
#define TEST_ASSERT_EQ(actual, expected, msg) \
  TEST_ASSERT((actual) == (expected), msg)

/** Assert actual != unexpected. */
#define TEST_ASSERT_NE(actual, unexpected, msg) \
  TEST_ASSERT((actual) != (unexpected), msg)

/* --- Visual helpers (optional) ----------------------------------------- */

/** Print a visual separator before a test body. */
#define TEST_START(name) printf("\n--- %s ---\n", (name))

/* --- Runner ------------------------------------------------------------ */

/**
 * Run all tests in @p tests, each in a child process for crash isolation.
 *
 * On POSIX systems, fork() + waitpid() is used.
 * On Windows, _spawnl(_P_WAIT, ...) re-invokes the executable with
 * "--child <index>" to run a single test in a child process.
 *
 * When called with "--child <index>" (Windows child-process mode), runs only
 * the test at @p index and returns its result directly.
 *
 * @param tests     Array of test entries.
 * @param num_tests  Number of entries in @p tests.
 * @param argc       Argument count from main().
 * @param argv       Argument vector from main().
 * @return 0 if all tests passed, non-zero if any failed.
 */
int run_all_tests(test_entry_t *tests, int num_tests, int argc, char *argv[]);

#endif /* OAR_TEST_FRAMEWORK_H */
