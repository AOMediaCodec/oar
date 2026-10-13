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
 * @file test_binaural_mixed_lock.c
 * @brief Test binaural rendering of head-locked and world-locked elements
 *        together.
 *
 * OBR encodes the world-locked and the head-locked object and loudspeaker
 * channels of a processing group in two passes. Each pass copies its
 * channels to its encoder's first inputs, so each pass needs an encoder
 * whose sources are its own channels; with one encoder for both, a mixed
 * group encoded a channel with another channel's position.
 *
 * Element 1 is one object at +90 degrees and element 2 one object at -90
 * degrees, in one group, with the limiter disabled and head tracking off.
 * One of the two is silent.
 *
 * Test matrix:
 *   TC1: Element 1 head-locked and silent, element 2 world-locked: the
 *        output equals element 2 rendered alone.
 *   TC2: Element 1 head-locked, element 2 world-locked and silent: the
 *        output equals element 1 rendered alone, head-locked.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "oar.h"
#include "test_framework.h"
#include "test_helpers.h"

#define BLOCK 256
#define BLOCKS 8

static const polar_t g_left = {90.0f, 0.0f, 1.0f};
static const polar_t g_right = {-90.0f, 0.0f, 1.0f};

/* Deterministic noise in [-0.5, 0.5) for block `block`. */
static void fill_input(float *data, int block) {
  uint32_t state = 2654435761u * ((uint32_t)block + 1u);
  for (int i = 0; i < BLOCK; i++) {
    state = state * 1664525u + 1013904223u;
    data[i] = (float)(state >> 8) / 16777216.0f - 0.5f;
  }
}

typedef struct {
  uint32_t id;
  int head_locked;
  const polar_t *position;
  int silent;
} element_t;

/* Renders BLOCKS blocks of `n` one-object elements to binaural into out. */
static int render(const element_t *elements, int n, float *out) {
  oar_config_t cfg =
      create_config(ck_oar_layout_binaural, BLOCK, TEST_SAMPLING_RATE);
  oar_t *oar = oar_create(&cfg);
  if (!oar) return -1;
  const int gid = oar_add_audio_group(oar);
  int ret = (gid >= 0 && oar_enable_limiter(oar, 0) == ck_oar_ok) ? 0 : -1;
  for (int i = 0; ret == 0 && i < n; i++) {
    oar_audio_element_config_t cfg_e = create_object_element_config(1);
    cfg_e.parameters.flags =
        def_parameter_set_flag_iamf_element_rendering_config;
    cfg_e.parameters.element_rendering_config.headphones_rendering_mode =
        elements[i].head_locked ? ck_head_locked : ck_world_locked;
    if (oar_add_audio_element(oar, (uint32_t)gid, elements[i].id, &cfg_e) !=
        ck_oar_ok)
      ret = -1;
  }
  for (int b = 0; ret == 0 && b < BLOCKS; b++) {
    float in[BLOCK], silence[BLOCK];
    fill_input(in, b);
    memset(silence, 0, sizeof(silence));
    for (int i = 0; ret == 0 && i < n; i++) {
      oar_audio_block_t block = {elements[i].silent ? silence : in, 1, BLOCK};
      if (oar_update_audio_element_data(oar, elements[i].id, &block) !=
              ck_oar_ok ||
          set_object_position(oar, elements[i].id, elements[i].position,
                              BLOCK) != 0)
        ret = -1;
    }
    oar_audio_block_t output = {out + b * 2 * BLOCK, 2, BLOCK};
    if (ret == 0 && oar_render(oar, &output) != ck_oar_ok) ret = -1;
  }
  oar_destroy(oar);
  return ret;
}

static int compare(const float *got, const float *expected) {
  for (int i = 0; i < BLOCKS * 2 * BLOCK; i++) {
    if (got[i] != expected[i]) {
      fprintf(stderr, "block %d sample %d: got %.9g, expected %.9g\n",
              i / (2 * BLOCK), i % (2 * BLOCK), got[i], expected[i]);
      return -1;
    }
  }
  return 0;
}

static float g_expected[BLOCKS * 2 * BLOCK];
static float g_got[BLOCKS * 2 * BLOCK];

/* TC1: a silent head-locked element beside a world-locked one. */
static int test_world_locked_with_silent_head_locked(void) {
  TEST_START("TC1: world-locked object beside a silent head-locked one");
  const element_t alone[] = {{2, 0, &g_right, 0}};
  const element_t mixed[] = {{1, 1, &g_left, 1}, {2, 0, &g_right, 0}};
  TEST_ASSERT(render(alone, 1, g_expected) == 0, "reference failed");
  TEST_ASSERT(is_output_non_silent(g_expected, BLOCKS * 2 * BLOCK),
              "reference is silent");
  TEST_ASSERT(render(mixed, 2, g_got) == 0, "render failed");
  TEST_ASSERT(compare(g_got, g_expected) == 0,
              "the head-locked element moved the world-locked one");
  return TEST_PASS;
}

/* TC2: a silent world-locked element beside a head-locked one. */
static int test_head_locked_with_silent_world_locked(void) {
  TEST_START("TC2: head-locked object beside a silent world-locked one");
  const element_t alone[] = {{1, 1, &g_left, 0}};
  const element_t mixed[] = {{1, 1, &g_left, 0}, {2, 0, &g_right, 1}};
  TEST_ASSERT(render(alone, 1, g_expected) == 0, "reference failed");
  TEST_ASSERT(is_output_non_silent(g_expected, BLOCKS * 2 * BLOCK),
              "reference is silent");
  TEST_ASSERT(render(mixed, 2, g_got) == 0, "render failed");
  TEST_ASSERT(compare(g_got, g_expected) == 0,
              "the world-locked element moved the head-locked one");
  return TEST_PASS;
}

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "world-locked object beside a silent head-locked one",
               test_world_locked_with_silent_head_locked),
    TEST_ENTRY("TC2", "head-locked object beside a silent world-locked one",
               test_head_locked_with_silent_world_locked),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
