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
 * @file test_render_scratch.c
 * @brief Test the oar_render() scratch blocks kept in the oar_t.
 *
 * oar_render() mixes each group into a group block and each renderer's output
 * through a renderer output block. The oar_t keeps these blocks between
 * calls, so each call must start from zeroed blocks, and a group added after
 * rendering has started must get its own block.
 *
 * Group 0 holds a stereo channel-based element and group 1 a one-object
 * element at 30 degrees azimuth. Both render to stereo with the limiter
 * disabled, so the output is the sum of the two groups.
 *
 * Test matrix:
 *   TC1: A block of silent input rendered after a block of noise gives an
 *        output of exact zeros.
 *   TC2: When group 1 is added after two blocks of group 0, the third block
 *        equals the sum of group 0's third block and group 1's first block,
 *        each rendered alone in its own oar_t.
 *   TC3: oar_create() returns NULL for a configuration with zero samples per
 *        channel.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "test_framework.h"
#include "test_helpers.h"

#define BLOCK 256
#define CHANNEL_ELEMENT_ID 1
#define OBJECT_ELEMENT_ID 2

static const polar_t g_object_position = {30.0f, 0.0f, 1.0f};

/* Deterministic noise in [-0.5, 0.5) for element `id` and block `block`;
 * amplitude 0 gives silence. */
static void fill_input(float *data, uint32_t count, uint32_t id, int block,
                       float amplitude) {
  uint32_t state = 2654435761u * (id * 1000u + (uint32_t)block + 1u);
  for (uint32_t i = 0; i < count; i++) {
    state = state * 1664525u + 1013904223u;
    data[i] = amplitude * ((float)(state >> 8) / 16777216.0f - 0.5f);
  }
}

static oar_t *create_stereo_oar(void) {
  oar_config_t cfg =
      create_config(ck_oar_layout_stereo, BLOCK, TEST_SAMPLING_RATE);
  oar_t *oar = oar_create(&cfg);
  if (oar && oar_enable_limiter(oar, 0) != ck_oar_ok) {
    oar_destroy(oar);
    return NULL;
  }
  return oar;
}

/* Adds a group holding the stereo channel-based element (group 0 content). */
static int add_channel_group(oar_t *oar) {
  int gid = oar_add_audio_group(oar);
  if (gid < 0) return -1;
  oar_audio_element_config_t cfg =
      create_channel_element_config(ck_oar_layout_stereo);
  return oar_add_audio_element(oar, (uint32_t)gid, CHANNEL_ELEMENT_ID, &cfg);
}

/* Adds a group holding the one-object element (group 1 content). */
static int add_object_group(oar_t *oar) {
  int gid = oar_add_audio_group(oar);
  if (gid < 0) return -1;
  oar_audio_element_config_t cfg = create_object_element_config(1);
  return oar_add_audio_element(oar, (uint32_t)gid, OBJECT_ELEMENT_ID, &cfg);
}

/* Feeds one block of input to element `id` (and the object position, for the
 * object element). */
static int feed(oar_t *oar, uint32_t id, int block, float amplitude) {
  float data[2 * BLOCK];
  const uint32_t channels = oar_get_number_of_audio_element_channels(oar, id);
  if (channels == 0 || channels > 2) return -1;
  fill_input(data, channels * BLOCK, id, block, amplitude);
  oar_audio_block_t input = {data, channels, BLOCK};
  if (oar_update_audio_element_data(oar, id, &input) != ck_oar_ok) return -1;
  if (id == OBJECT_ELEMENT_ID)
    return set_object_position(oar, id, &g_object_position, BLOCK);
  return 0;
}

static int render(oar_t *oar, float *out) {
  oar_audio_block_t output = {out, 2, BLOCK};
  return oar_render(oar, &output);
}

/* TC1: silent input after a block of noise renders exact zeros. */
static int test_silence_after_noise(void) {
  TEST_START("TC1: silent block after a block of noise");

  float out[2 * BLOCK];
  oar_t *oar = create_stereo_oar();
  TEST_ASSERT(oar != NULL, "oar_create failed");
  TEST_ASSERT(add_channel_group(oar) == 0, "failed to add channel group");
  TEST_ASSERT(add_object_group(oar) == 0, "failed to add object group");

  TEST_ASSERT(feed(oar, CHANNEL_ELEMENT_ID, 0, 1.0f) == 0, "feed failed");
  TEST_ASSERT(feed(oar, OBJECT_ELEMENT_ID, 0, 1.0f) == 0, "feed failed");
  TEST_ASSERT(render(oar, out) == ck_oar_ok, "render failed");
  TEST_ASSERT(is_output_non_silent(out, 2 * BLOCK), "noise block is silent");

  TEST_ASSERT(feed(oar, CHANNEL_ELEMENT_ID, 1, 0.0f) == 0, "feed failed");
  TEST_ASSERT(feed(oar, OBJECT_ELEMENT_ID, 1, 0.0f) == 0, "feed failed");
  TEST_ASSERT(render(oar, out) == ck_oar_ok, "render failed");
  oar_destroy(oar);

  for (int i = 0; i < 2 * BLOCK; i++) {
    if (out[i] != 0.0f) {
      fprintf(stderr, "sample %d is %g after silent input\n", i, out[i]);
      TEST_ASSERT(0, "silent block carries output from the previous block");
    }
  }
  return TEST_PASS;
}

/* TC2: a group added between renders mixes in exactly. */
static int test_group_added_between_renders(void) {
  TEST_START("TC2: group added between renders");

  float out[2 * BLOCK], ref_channel[2 * BLOCK], ref_object[2 * BLOCK];

  /* Group 0's third block, rendered alone. */
  oar_t *ref = create_stereo_oar();
  TEST_ASSERT(ref != NULL, "oar_create failed");
  TEST_ASSERT(add_channel_group(ref) == 0, "failed to add channel group");
  for (int b = 0; b < 3; b++) {
    TEST_ASSERT(feed(ref, CHANNEL_ELEMENT_ID, b, 1.0f) == 0, "feed failed");
    TEST_ASSERT(render(ref, ref_channel) == ck_oar_ok, "render failed");
  }
  oar_destroy(ref);

  /* Group 1's first block, rendered alone. */
  ref = create_stereo_oar();
  TEST_ASSERT(ref != NULL, "oar_create failed");
  TEST_ASSERT(add_object_group(ref) == 0, "failed to add object group");
  TEST_ASSERT(feed(ref, OBJECT_ELEMENT_ID, 2, 1.0f) == 0, "feed failed");
  TEST_ASSERT(render(ref, ref_object) == ck_oar_ok, "render failed");
  oar_destroy(ref);

  /* Group 0 for two blocks, then group 1 joins for the third. */
  oar_t *oar = create_stereo_oar();
  TEST_ASSERT(oar != NULL, "oar_create failed");
  TEST_ASSERT(add_channel_group(oar) == 0, "failed to add channel group");
  for (int b = 0; b < 2; b++) {
    TEST_ASSERT(feed(oar, CHANNEL_ELEMENT_ID, b, 1.0f) == 0, "feed failed");
    TEST_ASSERT(render(oar, out) == ck_oar_ok, "render failed");
  }
  TEST_ASSERT(add_object_group(oar) == 0, "failed to add object group");
  TEST_ASSERT(feed(oar, CHANNEL_ELEMENT_ID, 2, 1.0f) == 0, "feed failed");
  TEST_ASSERT(feed(oar, OBJECT_ELEMENT_ID, 2, 1.0f) == 0, "feed failed");
  TEST_ASSERT(render(oar, out) == ck_oar_ok, "render failed");
  oar_destroy(oar);

  TEST_ASSERT(is_output_non_silent(ref_object, 2 * BLOCK),
              "object group renders silence");
  for (int i = 0; i < 2 * BLOCK; i++) {
    if (out[i] != ref_channel[i] + ref_object[i]) {
      fprintf(stderr, "sample %d: got %.9g, expected %.9g + %.9g\n", i, out[i],
              ref_channel[i], ref_object[i]);
      TEST_ASSERT(0, "mixed output differs from the sum of the groups");
    }
  }
  return TEST_PASS;
}

/* TC3: a zero-length configuration is rejected when the oar_t is created. */
static int test_zero_length_config(void) {
  TEST_START("TC3: zero samples per channel");

  oar_config_t cfg = create_config(ck_oar_layout_stereo, 0, TEST_SAMPLING_RATE);
  TEST_ASSERT(oar_create(&cfg) == NULL,
              "oar_create accepted zero samples per channel");
  return TEST_PASS;
}

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "silent block after a block of noise",
               test_silence_after_noise),
    TEST_ENTRY("TC2", "group added between renders",
               test_group_added_between_renders),
    TEST_ENTRY("TC3", "zero samples per channel", test_zero_length_config),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
