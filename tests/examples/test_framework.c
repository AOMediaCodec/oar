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
 * @file test_framework.c
 * @brief Implementation of the common OAR test runner.
 *
 * Each test runs in a child process so that a crash (SIGABRT, segfault, etc.)
 * does not prevent subsequent tests from running.
 *
 * On POSIX: fork() + waitpid().
 * On Windows: _spawnl(_P_WAIT, ...) re-invokes the executable with
 * "--child <index>" to run a single test in a child process.
 */

#include "test_framework.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#include <windows.h>
#endif

/* --- Internal: run a single test in a child process -------------------- */

static int run_test_in_child(test_entry_t *tests, int num_tests,
                             int test_index) {
  const char *name = tests[test_index].name;

#ifndef _WIN32
  test_fn_t test_fn = tests[test_index].fn;

  fflush(stdout);
  fflush(stderr);

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return TEST_FAIL;
  }

  if (pid == 0) {
    /* Child: run the test and exit with its result. */
    int ret = test_fn();
    fflush(stdout);
    fflush(stderr);
    _exit(ret);
  }

  /* Parent: wait for the child and interpret the exit status. */
  int status = 0;
  int r;
  /**
   * Retry waitpid if interrupted by a signal (EINTR). The loop exits when:
   * - waitpid succeeds (r >= 0), or
   * - waitpid fails with an error other than EINTR (e.g. ECHILD, EINVAL).
   * This cannot deadlock: the child process will eventually terminate
   * (exit, crash, or SIGKILL), so waitpid will ultimately succeed.
   * */
  while ((r = waitpid(pid, &status, 0)) < 0 && errno == EINTR) {
  }
  if (r < 0) {
    perror("waitpid");
    return TEST_FAIL;
  }

  if (WIFEXITED(status)) {
    int exit_code = WEXITSTATUS(status);
    if (exit_code == 0) {
      printf("[%s] PASSED\n", name);
      return TEST_PASS;
    } else {
      printf("[%s] FAILED (exit %d)\n", name, exit_code);
      return TEST_FAIL;
    }
  } else if (WIFSIGNALED(status)) {
    int sig = WTERMSIG(status);
    printf("[%s] CRASHED (signal %d: %s)\n", name, sig,
           sig == SIGABRT ? "SIGABRT" : "other");
    return TEST_FAIL;
  }

  printf("[%s] UNKNOWN failure\n", name);
  return TEST_FAIL;
#else
  /* Windows: spawn a child process that runs a single test. */
  char index_str[16];
  char exe_path[1024];
  DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
  if (len == 0 || len >= sizeof(exe_path)) {
    fprintf(stderr, "[%s] FAILED: cannot get executable path\n", name);
    return TEST_FAIL;
  }

  snprintf(index_str, sizeof(index_str), "%d", test_index);

  /* Quote the exe path for argv[0] to handle paths containing spaces. */
  char quoted_path[1028];
  snprintf(quoted_path, sizeof(quoted_path), "\"%s\"", exe_path);

  errno = 0;
  int exit_code =
      _spawnl(_P_WAIT, exe_path, quoted_path, "--child", index_str, NULL);

  if (exit_code == -1) {
    /* -1 means _spawnl itself failed (the child never ran). */
    fprintf(stderr, "[%s] FAILED: spawn error (errno %d: %s)\n", name, errno,
            strerror(errno));
    return TEST_FAIL;
  } else if (exit_code == 0) {
    printf("[%s] PASSED\n", name);
    return TEST_PASS;
  } else {
    printf("[%s] FAILED/CRASHED (exit %d)\n", name, exit_code);
    return TEST_FAIL;
  }
#endif
}

/* --- Public API --------------------------------------------------------- */

int run_all_tests(test_entry_t *tests, int num_tests, int argc, char *argv[]) {
  /* Child-process mode (Windows): run a single test by index. */
  if (argc >= 3 && strcmp(argv[1], "--child") == 0) {
    int index = atoi(argv[2]);
    if (index < 0 || index >= num_tests) {
      return 2; /* sentinel: invalid index */
    }
    int ret = tests[index].fn();
    fflush(stdout);
    fflush(stderr);
    /* Map -1 (test failure) to exit code 1 so that -1 from _spawnl uniquely
     * identifies a spawn failure rather than colliding with the child's own
     * failure return. */
    return (ret != 0) ? 1 : 0;
  }

  printf("========================================\n");
  printf("Running %d test(s)\n", num_tests);
  printf("========================================\n");

  int result = 0;
  int *tc_results = (int *)calloc(num_tests, sizeof(int));
  if (!tc_results) {
    fprintf(stderr, "Failed to allocate results array\n");
    return 1;
  }

  for (int i = 0; i < num_tests; i++) {
    printf("\n--- Running %s: %s ---\n", tests[i].name, tests[i].description);
    tc_results[i] = run_test_in_child(tests, num_tests, i);
    if (tc_results[i] != 0) result = 1;
  }

  printf("\n========================================\n");
  printf("Test Summary:\n");
  for (int i = 0; i < num_tests; i++) {
    printf("  %s (%s): %s\n", tests[i].name, tests[i].description,
           tc_results[i] == 0 ? "PASSED" : "FAILED/CRASHED");
  }
  printf("========================================\n");

  free(tc_results);

  if (result == 0) {
    printf("\nAll tests passed.\n");
  } else {
    printf("\nSome tests failed or crashed. Review output above.\n");
  }

  return result;
}
