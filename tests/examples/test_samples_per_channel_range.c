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
 * @file test_samples_per_channel_range.c
 * @brief Test the samples_per_channel range that oar_create() accepts, and
 *        rendering with small and large blocks.
 *
 * oar_create() accepts 1 to def_oar_max_samples_per_channel (2^24) samples
 * per channel for every target layout. The renderers process one block of
 * samples_per_channel samples per render.
 *
 * Element 1 is a stereo channel-based element and element 2 a one-object
 * element at 30 degrees azimuth. The limiter is disabled.
 *
 * Test matrix:
 *   TC1: oar_create() returns NULL for 0, 2^24 + 1 and UINT32_MAX samples per
 *        channel and an oar_t for 1 and 2^24, with stereo and binaural target
 *        layouts.
 *   TC2: 1920 samples rendered in blocks of 1, 8, 32, 48 and 120 samples
 *        match, within 1e-5, the same input rendered in one block of 1920
 *        samples, with binaural and 7.1.4 target layouts.
 *   TC3: One binaural block of 65536 samples renders with both elements.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "test_framework.h"
#include "test_helpers.h"

#define NUM_SAMPLES 1920
#define MAX_OUT_CHANNELS 12
#define LARGE_BLOCK 65536
#define TOLERANCE 1e-5f
#define STEREO_ELEMENT_ID 1
#define OBJECT_ELEMENT_ID 2

static const polar_t g_object_position = {30.0f, 0.0f, 1.0f};

/* Input channels: the stereo element's two, then the object's. */
static float g_input[3][NUM_SAMPLES];

/* Fills `data` with deterministic noise in [-0.25, 0.25). */
static void fill_noise(float *data, uint32_t count, uint32_t seed) {
  uint32_t state = 2654435761u * (seed + 1u);
  for (uint32_t i = 0; i < count; i++) {
    state = state * 1664525u + 1013904223u;
    data[i] = 0.5f * ((float)(state >> 8) / 16777216.0f - 0.5f);
  }
}

/* Creates an oar_t with one group; returns NULL with *notsup set when the
 * binaural renderer is not built. */
static oar_t *create_oar(oar_layout_t layout, uint32_t samples, int *notsup) {
  oar_config_t cfg = create_config(layout, samples, TEST_SAMPLING_RATE);
  oar_t *oar = oar_create(&cfg);
  *notsup = 0;
  if (!oar) return NULL;
  const int gid = oar_add_audio_group(oar);
  if (gid == ck_oar_error_notsup) *notsup = 1;
  if (gid != 0 || oar_enable_limiter(oar, 0) != ck_oar_ok) {
    oar_destroy(oar);
    return NULL;
  }
  return oar;
}

static int add_element(oar_t *oar, uint32_t id) {
  oar_audio_element_config_t cfg =
      id == STEREO_ELEMENT_ID
          ? create_channel_element_config(ck_oar_layout_stereo)
          : create_object_element_config(1);
  return oar_add_audio_element(oar, 0, id, &cfg) == ck_oar_ok ? 0 : -1;
}

/* Feeds both elements one block of `block` samples (stereo planar, then the
 * object) and renders it into `out` (planar, channels x block). */
static int render_block(oar_t *oar, uint32_t block, uint32_t channels,
                        float *stereo, float *object, float *out) {
  oar_audio_block_t stereo_input = {stereo, 2, block};
  oar_audio_block_t object_input = {object, 1, block};
  oar_audio_block_t output = {out, channels, block};
  if (oar_update_audio_element_data(oar, STEREO_ELEMENT_ID, &stereo_input) !=
          ck_oar_ok ||
      set_object_position(oar, OBJECT_ELEMENT_ID, &g_object_position, block) !=
          0 ||
      oar_update_audio_element_data(oar, OBJECT_ELEMENT_ID, &object_input) !=
          ck_oar_ok ||
      oar_render(oar, &output) != ck_oar_ok)
    return -1;
  return 0;
}

/* Renders g_input in blocks of `block` samples, which must divide
 * NUM_SAMPLES, into `out` (planar, channels x NUM_SAMPLES). */
static int render_all(oar_t *oar, uint32_t block, uint32_t channels,
                      float *out) {
  static float stereo[2 * NUM_SAMPLES];
  static float rendered[MAX_OUT_CHANNELS * NUM_SAMPLES];
  for (uint32_t start = 0; start < NUM_SAMPLES; start += block) {
    memcpy(stereo, &g_input[0][start], block * sizeof(float));
    memcpy(stereo + block, &g_input[1][start], block * sizeof(float));
    if (render_block(oar, block, channels, stereo, &g_input[2][start],
                     rendered) != 0)
      return -1;
    for (uint32_t ch = 0; ch < channels; ch++)
      memcpy(out + ch * NUM_SAMPLES + start, rendered + ch * block,
             block * sizeof(float));
  }
  return 0;
}

static int compare(const float *got, const float *expected, uint32_t count,
                   uint32_t block) {
  float max_difference = 0.0f;
  uint32_t worst = 0;
  for (uint32_t i = 0; i < count; i++) {
    const float difference = fabsf(got[i] - expected[i]);
    if (difference > max_difference) {
      max_difference = difference;
      worst = i;
    }
  }
  if (max_difference > TOLERANCE) {
    fprintf(stderr, "blocks of %u: sample %u: got %.9g, expected %.9g\n", block,
            worst, got[worst], expected[worst]);
    return -1;
  }
  return 0;
}

/* TC1: oar_create() accepts 1 to 2^24 samples per channel. */
static int test_create_range(void) {
  TEST_START("TC1: oar_create() accepts 1 to 2^24 samples per channel");

  const oar_layout_t layouts[] = {ck_oar_layout_stereo, ck_oar_layout_binaural};
  const uint32_t rejected[] = {0, def_oar_max_samples_per_channel + 1,
                               UINT32_MAX};
  const uint32_t accepted[] = {1, def_oar_max_samples_per_channel};

  for (size_t l = 0; l < sizeof(layouts) / sizeof(layouts[0]); l++) {
    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); i++) {
      oar_config_t cfg =
          create_config(layouts[l], rejected[i], TEST_SAMPLING_RATE);
      oar_t *oar = oar_create(&cfg);
      if (oar) oar_destroy(oar);
      TEST_ASSERT(oar == NULL, "oar_create accepted an out-of-range size");
    }
    for (size_t i = 0; i < sizeof(accepted) / sizeof(accepted[0]); i++) {
      oar_config_t cfg =
          create_config(layouts[l], accepted[i], TEST_SAMPLING_RATE);
      oar_t *oar = oar_create(&cfg);
      TEST_ASSERT(oar != NULL, "oar_create rejected an in-range size");
      oar_destroy(oar);
    }
  }
  return TEST_PASS;
}

/* TC2: blocks of 1, 8, 32, 48 and 120 samples against one block of 1920. */
static int test_small_blocks(void) {
  TEST_START("TC2: blocks of 1 to 120 samples match one 1920-sample block");

  static float out[MAX_OUT_CHANNELS * NUM_SAMPLES];
  static float expected[MAX_OUT_CHANNELS * NUM_SAMPLES];
  const oar_layout_t layouts[] = {ck_oar_layout_binaural, ck_oar_layout_714};
  const uint32_t layout_channels[] = {2, 12};
  const uint32_t blocks[] = {1, 8, 32, 48, 120};
  int notsup = 0;

  for (uint32_t ch = 0; ch < 3; ch++) fill_noise(g_input[ch], NUM_SAMPLES, ch);
  for (size_t l = 0; l < sizeof(layouts) / sizeof(layouts[0]); l++) {
    const uint32_t channels = layout_channels[l];
    oar_t *ref = create_oar(layouts[l], NUM_SAMPLES, &notsup);
    if (notsup) continue;
    TEST_ASSERT(ref != NULL, "failed to create reference oar_t");
    TEST_ASSERT(oar_get_number_of_output_channels(ref) == channels,
                "unexpected output channel count");
    TEST_ASSERT(add_element(ref, STEREO_ELEMENT_ID) == 0, "add failed");
    TEST_ASSERT(add_element(ref, OBJECT_ELEMENT_ID) == 0, "add failed");
    TEST_ASSERT(render_all(ref, NUM_SAMPLES, channels, expected) == 0,
                "render failed");
    oar_destroy(ref);
    TEST_ASSERT(is_output_non_silent(expected, channels * NUM_SAMPLES),
                "reference output is silent");

    for (size_t b = 0; b < sizeof(blocks) / sizeof(blocks[0]); b++) {
      oar_t *oar = create_oar(layouts[l], blocks[b], &notsup);
      TEST_ASSERT(oar != NULL, "failed to create oar_t");
      TEST_ASSERT(add_element(oar, STEREO_ELEMENT_ID) == 0, "add failed");
      TEST_ASSERT(add_element(oar, OBJECT_ELEMENT_ID) == 0, "add failed");
      TEST_ASSERT(render_all(oar, blocks[b], channels, out) == 0,
                  "render failed");
      oar_destroy(oar);
      TEST_ASSERT(
          compare(out, expected, channels * NUM_SAMPLES, blocks[b]) == 0,
          "output differs from one 1920-sample block");
    }
  }
  return TEST_PASS;
}

/* TC3: a binaural block of 65536 samples renders. */
static int test_large_block(void) {
  TEST_START("TC3: one binaural block of 65536 samples renders");

  static float stereo[2 * LARGE_BLOCK], object[LARGE_BLOCK],
      out[2 * LARGE_BLOCK];
  int notsup = 0;

  fill_noise(stereo, 2 * LARGE_BLOCK, 0);
  fill_noise(object, LARGE_BLOCK, 2);
  oar_t *oar = create_oar(ck_oar_layout_binaural, LARGE_BLOCK, &notsup);
  if (notsup) return TEST_SKIP;
  TEST_ASSERT(oar != NULL, "failed to create oar_t");
  TEST_ASSERT(add_element(oar, STEREO_ELEMENT_ID) == 0, "add failed");
  TEST_ASSERT(add_element(oar, OBJECT_ELEMENT_ID) == 0, "add failed");
  TEST_ASSERT(render_block(oar, LARGE_BLOCK, 2, stereo, object, out) == 0,
              "render failed");
  oar_destroy(oar);
  TEST_ASSERT(is_output_non_silent(out, 2 * LARGE_BLOCK), "output is silent");
  return TEST_PASS;
}

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "oar_create() accepts 1 to 2^24 samples per channel",
               test_create_range),
    TEST_ENTRY("TC2", "blocks of 1 to 120 samples match one 1920-sample block",
               test_small_blocks),
    TEST_ENTRY("TC3", "one binaural block of 65536 samples renders",
               test_large_block),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
