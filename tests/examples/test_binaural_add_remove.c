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
 * @file test_binaural_add_remove.c
 * @brief Test binaural rendering after an element is added or removed while
 *        rendering is under way.
 *
 * The binaural renderer stages every element's input in one block, which it
 * sizes when an element is added or removed. Adding or removing an element
 * also reinitializes OBR's processing, so the block rendered after the change
 * must equal the first block of a new oar_t that holds the same elements and
 * receives the same input.
 *
 * Element 1 is a stereo channel-based element and element 2 a one-object
 * element at 30 degrees azimuth. The limiter is disabled, so no state from
 * earlier blocks reaches the output.
 *
 * Test matrix:
 *   TC1: After two blocks of element 1, element 1's input is fed, element 2
 *        is added and fed, and the rendered block equals that of a new oar_t
 *        with both elements.
 *   TC2: After two blocks of both elements, element 2 is removed, and the
 *        block rendered from element 1 equals that of a new oar_t with
 *        element 1 only.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "test_framework.h"
#include "test_helpers.h"

#define BLOCK 256
#define STEREO_ELEMENT_ID 1
#define OBJECT_ELEMENT_ID 2

static const polar_t g_object_position = {30.0f, 0.0f, 1.0f};

/* Deterministic noise in [-0.25, 0.25) for element `id` and block `block`. */
static void fill_input(float *data, uint32_t count, uint32_t id, int block) {
  uint32_t state = 2654435761u * (id * 1000u + (uint32_t)block + 1u);
  for (uint32_t i = 0; i < count; i++) {
    state = state * 1664525u + 1013904223u;
    data[i] = 0.5f * ((float)(state >> 8) / 16777216.0f - 0.5f);
  }
}

/* Creates a binaural oar_t with one group; returns NULL with *notsup set when
 * the binaural renderer is not built. */
static oar_t *create_binaural_oar(int *notsup) {
  oar_config_t cfg =
      create_config(ck_oar_layout_binaural, BLOCK, TEST_SAMPLING_RATE);
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

/* Feeds element `id` its input for block `block` (and, for the object, its
 * position). */
static int feed(oar_t *oar, uint32_t id, int block) {
  float data[2 * BLOCK];
  const uint32_t channels = id == STEREO_ELEMENT_ID ? 2 : 1;
  fill_input(data, channels * BLOCK, id, block);
  oar_audio_block_t input = {data, channels, BLOCK};
  if (id == OBJECT_ELEMENT_ID &&
      set_object_position(oar, id, &g_object_position, BLOCK) != 0)
    return -1;
  return oar_update_audio_element_data(oar, id, &input) == ck_oar_ok ? 0 : -1;
}

static int render(oar_t *oar, float *out) {
  oar_audio_block_t output = {out, 2, BLOCK};
  return oar_render(oar, &output) == ck_oar_ok ? 0 : -1;
}

static int compare(const float *got, const float *expected) {
  for (int i = 0; i < 2 * BLOCK; i++) {
    if (got[i] != expected[i]) {
      fprintf(stderr, "sample %d: got %.9g, expected %.9g\n", i, got[i],
              expected[i]);
      return -1;
    }
  }
  return 0;
}

/* TC1: element added after rendering, with input fed before the add. */
static int test_element_added(void) {
  TEST_START("TC1: element added after two rendered blocks");

  float out[2 * BLOCK], expected[2 * BLOCK];
  int notsup = 0;

  oar_t *ref = create_binaural_oar(&notsup);
  if (notsup) return TEST_SKIP;
  TEST_ASSERT(ref != NULL, "failed to create reference oar_t");
  TEST_ASSERT(add_element(ref, STEREO_ELEMENT_ID) == 0, "add failed");
  TEST_ASSERT(add_element(ref, OBJECT_ELEMENT_ID) == 0, "add failed");
  TEST_ASSERT(feed(ref, STEREO_ELEMENT_ID, 2) == 0, "feed failed");
  TEST_ASSERT(feed(ref, OBJECT_ELEMENT_ID, 2) == 0, "feed failed");
  TEST_ASSERT(render(ref, expected) == 0, "render failed");
  oar_destroy(ref);
  TEST_ASSERT(is_output_non_silent(expected, 2 * BLOCK),
              "reference output is silent");

  oar_t *oar = create_binaural_oar(&notsup);
  TEST_ASSERT(oar != NULL, "failed to create oar_t");
  TEST_ASSERT(add_element(oar, STEREO_ELEMENT_ID) == 0, "add failed");
  for (int b = 0; b < 2; b++) {
    TEST_ASSERT(feed(oar, STEREO_ELEMENT_ID, b) == 0, "feed failed");
    TEST_ASSERT(render(oar, out) == 0, "render failed");
  }
  TEST_ASSERT(feed(oar, STEREO_ELEMENT_ID, 2) == 0, "feed failed");
  TEST_ASSERT(add_element(oar, OBJECT_ELEMENT_ID) == 0, "add failed");
  TEST_ASSERT(feed(oar, OBJECT_ELEMENT_ID, 2) == 0, "feed failed");
  TEST_ASSERT(render(oar, out) == 0, "render failed");
  oar_destroy(oar);

  TEST_ASSERT(compare(out, expected) == 0,
              "block after the add differs from a new oar_t");
  return TEST_PASS;
}

/* TC2: element removed after rendering. */
static int test_element_removed(void) {
  TEST_START("TC2: element removed after two rendered blocks");

  float out[2 * BLOCK], expected[2 * BLOCK];
  int notsup = 0;

  oar_t *ref = create_binaural_oar(&notsup);
  if (notsup) return TEST_SKIP;
  TEST_ASSERT(ref != NULL, "failed to create reference oar_t");
  TEST_ASSERT(add_element(ref, STEREO_ELEMENT_ID) == 0, "add failed");
  TEST_ASSERT(feed(ref, STEREO_ELEMENT_ID, 2) == 0, "feed failed");
  TEST_ASSERT(render(ref, expected) == 0, "render failed");
  oar_destroy(ref);
  TEST_ASSERT(is_output_non_silent(expected, 2 * BLOCK),
              "reference output is silent");

  oar_t *oar = create_binaural_oar(&notsup);
  TEST_ASSERT(oar != NULL, "failed to create oar_t");
  TEST_ASSERT(add_element(oar, STEREO_ELEMENT_ID) == 0, "add failed");
  TEST_ASSERT(add_element(oar, OBJECT_ELEMENT_ID) == 0, "add failed");
  for (int b = 0; b < 2; b++) {
    TEST_ASSERT(feed(oar, STEREO_ELEMENT_ID, b) == 0, "feed failed");
    TEST_ASSERT(feed(oar, OBJECT_ELEMENT_ID, b) == 0, "feed failed");
    TEST_ASSERT(render(oar, out) == 0, "render failed");
  }
  TEST_ASSERT(oar_remove_audio_element(oar, OBJECT_ELEMENT_ID) == ck_oar_ok,
              "remove failed");
  TEST_ASSERT(feed(oar, STEREO_ELEMENT_ID, 2) == 0, "feed failed");
  TEST_ASSERT(render(oar, out) == 0, "render failed");
  oar_destroy(oar);

  TEST_ASSERT(compare(out, expected) == 0,
              "block after the removal differs from a new oar_t");
  return TEST_PASS;
}

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "element added after two rendered blocks",
               test_element_added),
    TEST_ENTRY("TC2", "element removed after two rendered blocks",
               test_element_removed),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
