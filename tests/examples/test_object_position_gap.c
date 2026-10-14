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
 * @file test_object_position_gap.c
 * @brief Test an object element after renders without position metadata.
 *
 * OLR counts the samples it has rendered and where each object's next
 * position block starts. A render without a position advances only the
 * first; a position block starting before the next sample to render was
 * dropped as an underrun, and so was every later one, so the object stayed
 * silent. A host renders without positions when it drains a renderer's tail
 * while paused, with silent input and no metadata.
 *
 * One one-object element renders to stereo with the limiter disabled.
 *
 * Test matrix:
 *   TC1: After 4 blocks with positions and 1 to 3 blocks without, the next
 *        block with a position is not silent and equals the first block of a
 *        new oar_t given the same position and input.
 *   TC2: The same with positions pushed for the 4 blocks only, and the
 *        renders without positions fed silent input.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "oar.h"
#include "test_framework.h"
#include "test_helpers.h"

#define BLOCK 256
#define ELEMENT_ID 1

static const polar_t g_position = {30.0f, 0.0f, 1.0f};

/* Deterministic noise in [-0.5, 0.5) for block `block`; silent for
 * amplitude 0. */
static void fill_input(float *data, int block, float amplitude) {
  uint32_t state = 2654435761u * ((uint32_t)block + 1u);
  for (int i = 0; i < BLOCK; i++) {
    state = state * 1664525u + 1013904223u;
    data[i] = amplitude * ((float)(state >> 8) / 16777216.0f - 0.5f);
  }
}

static oar_t *create_oar(void) {
  oar_config_t cfg =
      create_config(ck_oar_layout_stereo, BLOCK, TEST_SAMPLING_RATE);
  oar_t *oar = oar_create(&cfg);
  if (!oar) return NULL;
  oar_audio_element_config_t element = create_object_element_config(1);
  const int gid = oar_add_audio_group(oar);
  if (oar_enable_limiter(oar, 0) != ck_oar_ok || gid < 0 ||
      oar_add_audio_element(oar, (uint32_t)gid, ELEMENT_ID, &element) !=
          ck_oar_ok) {
    oar_destroy(oar);
    return NULL;
  }
  return oar;
}

/* Renders one block of `block`'s input, with a position when `position` is
 * set. */
static int render(oar_t *oar, int block, float amplitude, int position,
                  float *out) {
  float in[BLOCK];
  fill_input(in, block, amplitude);
  oar_audio_block_t input = {in, 1, BLOCK};
  if (oar_update_audio_element_data(oar, ELEMENT_ID, &input) != ck_oar_ok)
    return -1;
  if (position && set_object_position(oar, ELEMENT_ID, &g_position, BLOCK))
    return -1;
  oar_audio_block_t output = {out, 2, BLOCK};
  memset(out, 0, sizeof(float) * 2 * BLOCK);
  return oar_render(oar, &output) == ck_oar_ok ? 0 : -1;
}

/* Renders 4 blocks with positions, `gap` blocks without (fed `gap_amplitude`
 * input), then block 100 with a position into out. */
static int render_after_gap(int gap, float gap_amplitude, float *out) {
  float scratch[2 * BLOCK];
  oar_t *oar = create_oar();
  if (!oar) return -1;
  int ret = 0;
  for (int b = 0; ret == 0 && b < 4; b++)
    ret = render(oar, b, 1.0f, 1, scratch);
  for (int b = 0; ret == 0 && b < gap; b++)
    ret = render(oar, 10 + b, gap_amplitude, 0, scratch);
  if (ret == 0) ret = render(oar, 100, 1.0f, 1, out);
  oar_destroy(oar);
  return ret;
}

static int expect_like_new_instance(float gap_amplitude) {
  float expected[2 * BLOCK], got[2 * BLOCK];
  oar_t *oar = create_oar();
  if (!oar) return -1;
  int ret = render(oar, 100, 1.0f, 1, expected);
  oar_destroy(oar);
  if (ret != 0 || !is_output_non_silent(expected, 2 * BLOCK)) return -1;

  for (int gap = 1; gap <= 3; gap++) {
    if (render_after_gap(gap, gap_amplitude, got) != 0) return -1;
    if (!is_output_non_silent(got, 2 * BLOCK)) {
      fprintf(stderr, "silent after a gap of %d blocks\n", gap);
      return -1;
    }
    for (int i = 0; i < 2 * BLOCK; i++) {
      if (got[i] != expected[i]) {
        fprintf(stderr, "gap %d, sample %d: got %.9g, expected %.9g\n", gap, i,
                got[i], expected[i]);
        return -1;
      }
    }
  }
  return 0;
}

/* TC1: renders without positions, fed program input. */
static int test_gap_with_input(void) {
  TEST_START("TC1: renders without positions, with input");
  TEST_ASSERT(expect_like_new_instance(1.0f) == 0,
              "block after the gap differs from a new instance");
  return TEST_PASS;
}

/* TC2: renders without positions, fed silence, as a paused tail drain. */
static int test_gap_with_silence(void) {
  TEST_START("TC2: renders without positions, with silent input");
  TEST_ASSERT(expect_like_new_instance(0.0f) == 0,
              "block after the gap differs from a new instance");
  return TEST_PASS;
}

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "renders without positions, with input",
               test_gap_with_input),
    TEST_ENTRY("TC2", "renders without positions, with silent input",
               test_gap_with_silence),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
