/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

/**
 * @file benchmark_samples_per_channel.c
 * @brief Measure CPU time and memory of one oar_t against samples_per_channel.
 *
 * For each scenario and samples_per_channel, the benchmark creates an oar_t at
 * 48 kHz with one group holding one element, renders one block, and records
 * the growth of the process's memory footprint from before oar_create() to
 * after that block. It then renders further blocks until at least
 * MIN_CPU_SECONDS of CPU time and two blocks have passed, and reports the CPU
 * time as a percentage of the rendered audio's duration: 100% is one core
 * busy in real time. The limiter is enabled, as by default.
 *
 * Except on Windows, each measurement runs in a child process, so memory
 * freed by one measurement cannot be reused by the next. The footprint is
 * phys_footprint on macOS and RssAnon on Linux; on other systems the memory
 * table shows "-".
 *
 * Usage: benchmark_samples_per_channel [max_samples_per_channel]
 * The default maximum is 1048576. The tables are printed in Markdown; the
 * ones in doc/samples_per_channel.md come from this program.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "oar.h"
#include "test_helpers.h"

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#endif

#define SAMPLING_RATE 48000
#define MIN_CPU_SECONDS 0.5
#define DEFAULT_MAX_SAMPLES 1048576u
#define ELEMENT_ID 1

typedef struct {
  const char *name;
  oar_layout_t layout;
  audio_element_type_t type;
  oar_binaural_filter_profile_t profile;
} scenario_t;

typedef struct {
  int ok;
  double cpu_percent;
  double memory_mb;
} result_t;

static const scenario_t g_scenarios[] = {
    {"Binaural 3OA direct", ck_oar_layout_binaural, ck_scene_based, ck_direct},
    {"Binaural 3OA ambient", ck_oar_layout_binaural, ck_scene_based,
     ck_ambient},
    {"Binaural 3OA reverberant", ck_oar_layout_binaural, ck_scene_based,
     ck_reverberant},
    {"Binaural object ambient", ck_oar_layout_binaural, ck_object_based,
     ck_ambient},
    {"7.1.4 3OA", ck_oar_layout_714, ck_scene_based, ck_ambient},
    {"7.1.4 object", ck_oar_layout_714, ck_object_based, ck_ambient},
};
#define NUM_SCENARIOS (sizeof(g_scenarios) / sizeof(g_scenarios[0]))

static const polar_t g_object_position = {30.0f, 10.0f, 1.0f};

/* Returns the process's memory footprint in bytes, or -1 where unknown. */
static double footprint_bytes(void) {
#if defined(__APPLE__)
  task_vm_info_data_t info;
  mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
  if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) !=
      KERN_SUCCESS)
    return -1;
  return (double)info.phys_footprint;
#elif defined(__linux__)
  char line[256];
  double kb = -1;
  FILE *f = fopen("/proc/self/status", "r");
  if (!f) return -1;
  while (fgets(line, sizeof(line), f))
    if (sscanf(line, "RssAnon: %lf", &kb) == 1) break;
  fclose(f);
  return kb < 0 ? -1 : kb * 1024.0;
#else
  return -1;
#endif
}

static oar_t *create_oar(const scenario_t *s, uint32_t samples) {
  oar_config_t cfg = create_config(s->layout, samples, SAMPLING_RATE);
  oar_t *oar = oar_create(&cfg);
  if (!oar) return NULL;
  oar_audio_element_config_t element =
      s->type == ck_scene_based ? create_scene_element_config(ck_oar_3oa)
                                : create_object_element_config(1);
  element.parameters.flags |=
      def_parameter_set_flag_iamf_element_rendering_config;
  element.parameters.element_rendering_config.headphones_rendering_mode =
      ck_world_locked;
  element.parameters.element_rendering_config.binaural_filter_profile =
      s->profile;
  if (oar_add_audio_group(oar) != 0 ||
      oar_add_audio_element(oar, 0, ELEMENT_ID, &element) != ck_oar_ok) {
    oar_destroy(oar);
    return NULL;
  }
  return oar;
}

static int render_block(oar_t *oar, const scenario_t *s, uint32_t samples,
                        oar_audio_block_t *input, oar_audio_block_t *output) {
  if (s->type == ck_object_based &&
      set_object_position(oar, ELEMENT_ID, &g_object_position, samples) != 0)
    return -1;
  if (oar_update_audio_element_data(oar, ELEMENT_ID, input) != ck_oar_ok)
    return -1;
  return oar_render(oar, output) == ck_oar_ok ? 0 : -1;
}

/* Measures one scenario with input and output buffers that are already
 * written, so their pages do not count towards the footprint growth. */
static result_t measure_with_buffers(const scenario_t *s, uint32_t samples,
                                     oar_audio_block_t *input,
                                     oar_audio_block_t *output) {
  result_t result = {0, 0.0, -1.0};
  const double before = footprint_bytes();
  oar_t *oar = create_oar(s, samples);
  if (!oar) return result;
  if (render_block(oar, s, samples, input, output) != 0) {
    oar_destroy(oar);
    return result;
  }
  const double after = footprint_bytes();
  if (before >= 0 && after >= 0)
    result.memory_mb = (after - before) / (1024.0 * 1024.0);

  uint64_t blocks = 0;
  double cpu = 0.0;
  const clock_t start = clock();
  while (cpu < MIN_CPU_SECONDS || blocks < 2) {
    if (render_block(oar, s, samples, input, output) != 0) break;
    blocks++;
    cpu = (double)(clock() - start) / CLOCKS_PER_SEC;
  }
  oar_destroy(oar);
  if (blocks >= 2) {
    const double audio = (double)blocks * samples / SAMPLING_RATE;
    result.cpu_percent = 100.0 * cpu / audio;
    result.ok = 1;
  }
  return result;
}

static result_t measure(const scenario_t *s, uint32_t samples) {
  result_t result = {0, 0.0, -1.0};
  const uint32_t in_channels = s->type == ck_scene_based ? 16 : 1;
  const uint32_t out_channels = s->layout == ck_oar_layout_binaural ? 2 : 12;
  const size_t in_count = (size_t)in_channels * samples;
  const size_t out_count = (size_t)out_channels * samples;
  float *in = malloc(sizeof(float) * in_count);
  float *out = malloc(sizeof(float) * out_count);
  if (in && out) {
    /* Deterministic noise in [-0.1, 0.1). Both buffers are written here so
     * that their pages are resident before the first footprint reading. */
    uint32_t state = 1u;
    for (size_t i = 0; i < in_count; i++) {
      state = state * 1664525u + 1013904223u;
      in[i] = 0.2f * ((float)(state >> 8) / 16777216.0f - 0.5f);
    }
    memset(out, 0, sizeof(float) * out_count);
    oar_audio_block_t input = {in, in_channels, samples};
    oar_audio_block_t output = {out, out_channels, samples};
    result = measure_with_buffers(s, samples, &input, &output);
  }
  free(in);
  free(out);
  return result;
}

/* Runs measure() in a child process on POSIX, in this process elsewhere. */
static result_t measure_isolated(const scenario_t *s, uint32_t samples) {
#ifndef _WIN32
  result_t result = {0, 0.0, -1.0};
  int fds[2];
  if (pipe(fds) != 0) return result;
  fflush(stdout);
  const pid_t pid = fork();
  if (pid == 0) {
    close(fds[0]);
    result = measure(s, samples);
    const ssize_t written = write(fds[1], &result, sizeof(result));
    _exit(written == (ssize_t)sizeof(result) ? 0 : 1);
  }
  close(fds[1]);
  if (pid > 0) {
    if (read(fds[0], &result, sizeof(result)) != (ssize_t)sizeof(result))
      result.ok = 0;
    waitpid(pid, NULL, 0);
  }
  close(fds[0]);
  return result;
#else
  return measure(s, samples);
#endif
}

/* Prints `value` as a table cell with three significant digits and no
 * exponent. */
static void print_cell(double value) {
  int decimals = 0;
  if (value > 0) decimals = 2 - (int)floor(log10(value));
  if (decimals < 0) decimals = 0;
  printf(" %.*f |", decimals, value);
}

static void print_header(void) {
  printf("| samples_per_channel | ms at 48 kHz |");
  for (size_t i = 0; i < NUM_SCENARIOS; i++)
    printf(" %s |", g_scenarios[i].name);
  printf("\n|---:|---:|");
  for (size_t i = 0; i < NUM_SCENARIOS; i++) printf("---:|");
  printf("\n");
}

int main(int argc, char *argv[]) {
  uint32_t max_samples = DEFAULT_MAX_SAMPLES;
  if (argc > 1) {
    const unsigned long value = strtoul(argv[1], NULL, 10);
    if (value == 0 || value > def_oar_max_samples_per_channel) {
      fprintf(stderr, "max_samples_per_channel must be 1 to %d\n",
              def_oar_max_samples_per_channel);
      return 1;
    }
    max_samples = (uint32_t)value;
  }

  uint32_t sizes[32];
  size_t num_sizes = 0;
  static const uint32_t small[] = {1, 8, 32, 64, 128, 256, 512, 960, 1024};
  for (size_t i = 0; i < sizeof(small) / sizeof(small[0]); i++)
    if (small[i] <= max_samples) sizes[num_sizes++] = small[i];
  for (uint32_t n = 4096; n <= max_samples && num_sizes < 32; n *= 4)
    sizes[num_sizes++] = n;

  static result_t results[32][NUM_SCENARIOS];
  for (size_t i = 0; i < num_sizes; i++) {
    for (size_t j = 0; j < NUM_SCENARIOS; j++) {
      results[i][j] = measure_isolated(&g_scenarios[j], sizes[i]);
      fprintf(stderr, "%u samples, %s: %s\n", sizes[i], g_scenarios[j].name,
              results[i][j].ok ? "done" : "failed");
    }
  }

  printf("CPU time, %% of one core in real time:\n\n");
  print_header();
  for (size_t i = 0; i < num_sizes; i++) {
    printf("| %u |", sizes[i]);
    print_cell(1000.0 * sizes[i] / SAMPLING_RATE);
    for (size_t j = 0; j < NUM_SCENARIOS; j++) {
      if (results[i][j].ok)
        print_cell(results[i][j].cpu_percent);
      else
        printf(" failed |");
    }
    printf("\n");
  }

  printf("\nMemory footprint growth, MiB:\n\n");
  print_header();
  for (size_t i = 0; i < num_sizes; i++) {
    printf("| %u |", sizes[i]);
    print_cell(1000.0 * sizes[i] / SAMPLING_RATE);
    for (size_t j = 0; j < NUM_SCENARIOS; j++) {
      if (results[i][j].ok && results[i][j].memory_mb >= 0)
        print_cell(results[i][j].memory_mb);
      else
        printf(" - |");
    }
    printf("\n");
  }
  return 0;
}
