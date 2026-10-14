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
 * @file test_metadata_reuse.c
 * @brief Test the reuse of queued gain and position metadata.
 *
 * Each metadata queue (an element's gains and object positions, a group's
 * output gain) keeps the copies that have elapsed and reuses them for the
 * next push, after reserving four of them when it is created. A push beyond
 * the spares copies into a new allocation, and a copy that owns a gain array
 * (ck_param_multiple) is freed rather than kept.
 *
 * TC1 to TC3 render the same input through two oar_t instances that receive
 * the same metadata at different times, and require bit-identical output.
 * TC4 and TC5 render blocks after a single push and check what OAR writes to
 * stderr. One stereo channel-based element and one one-object element render
 * to stereo with the limiter disabled.
 *
 * Test matrix:
 *   TC1: Element gains, group gains and object positions pushed eight blocks
 *        ahead, beyond the four spares, render as when each is pushed just
 *        before its block.
 *   TC2: Per-sample gain arrays (ck_param_multiple) alternating with animated
 *        gains, pushed three blocks ahead, render as when pushed just in time.
 *   TC3: A zero-duration gain and position pushed before the first block
 *        leave the output unchanged.
 *   TC4: Blocks rendered after only a zero-duration gain and position render
 *        as without them, and log no metadata warning: a zero-duration block
 *        covers no samples, like an empty queue.
 *   TC5: A block rendered after gains and a position that cover half of it
 *        logs the metadata warning.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "test_framework.h"
#include "test_helpers.h"

#ifdef _WIN32
#include <io.h>
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#define close _close
#else
#include <unistd.h>
#endif

#define BLOCK 256
#define BLOCKS 8
#define CHANNEL_ELEMENT_ID 1
#define OBJECT_ELEMENT_ID 2

/* Deterministic noise in [-0.5, 0.5) for element `id` and block `block`. */
static void fill_input(float *data, uint32_t count, uint32_t id, int block) {
  uint32_t state = 2654435761u * (id * 1000u + (uint32_t)block + 1u);
  for (uint32_t i = 0; i < count; i++) {
    state = state * 1664525u + 1013904223u;
    data[i] = (float)(state >> 8) / 16777216.0f - 0.5f;
  }
}

static oar_t *create_oar(void) {
  oar_config_t cfg =
      create_config(ck_oar_layout_stereo, BLOCK, TEST_SAMPLING_RATE);
  oar_t *oar = oar_create(&cfg);
  if (!oar) return NULL;
  oar_audio_element_config_t channel =
      create_channel_element_config(ck_oar_layout_stereo);
  oar_audio_element_config_t object = create_object_element_config(1);
  const int gid = oar_add_audio_group(oar);
  if (oar_enable_limiter(oar, 0) != ck_oar_ok || gid < 0 ||
      oar_add_audio_element(oar, (uint32_t)gid, CHANNEL_ELEMENT_ID, &channel) !=
          ck_oar_ok ||
      oar_add_audio_element(oar, (uint32_t)gid, OBJECT_ELEMENT_ID, &object) !=
          ck_oar_ok) {
    oar_destroy(oar);
    return NULL;
  }
  return oar;
}

/* Gain metadata for block `block`: a step in dB per block, or, with
 * `per_sample`, a per-sample ramp held in `gains` (BLOCK values). */
static oar_metadata_t gain_metadata(uint32_t id, int block, int per_sample,
                                    float *gains, uint32_t duration) {
  oar_metadata_t m;
  memset(&m, 0, sizeof(m));
  m.type = ck_metadata_gain;
  m.duration = duration;
  m.gain.id = id;
  if (per_sample) {
    for (uint32_t i = 0; i < BLOCK; i++)
      gains[i] = -0.5f * (float)block - 0.01f * (float)i;
    m.gain.param_type = ck_param_multiple;
    m.gain.gain_array = gains;
  } else {
    m.gain.param_type = ck_param_animated;
    m.gain.animated_gains.animation_type = ck_animation_type_linear;
    m.gain.animated_gains.data.start = -1.0f * (float)block;
    m.gain.animated_gains.data.end = -1.0f * (float)(block + 1);
  }
  return m;
}

/* Pushes block `block`'s element gains, group gain and object position.
 * Blocks whose index is odd carry per-sample gains when `alternate` is set. */
static int push_metadata(oar_t *oar, int block, int alternate,
                         uint32_t duration) {
  float gains[BLOCK];
  const int per_sample = alternate && (block % 2) == 1;
  oar_metadata_t m;

  m = gain_metadata(CHANNEL_ELEMENT_ID, block, per_sample, gains, duration);
  if (oar_update_audio_element_metadata(oar, CHANNEL_ELEMENT_ID, &m) !=
      ck_oar_ok)
    return -1;
  m = gain_metadata(OBJECT_ELEMENT_ID, block, per_sample, gains, duration);
  if (oar_update_audio_element_metadata(oar, OBJECT_ELEMENT_ID, &m) !=
      ck_oar_ok)
    return -1;
  m = gain_metadata(0, block, 0, gains, duration);
  if (oar_update_metadata(oar, 0, &m) != ck_oar_ok) return -1;

  const polar_t position = {-60.0f + 15.0f * (float)block, 10.0f, 1.0f};
  return set_object_position(oar, OBJECT_ELEMENT_ID, &position, duration);
}

static int feed_and_render(oar_t *oar, int block, float *out) {
  float channel[2 * BLOCK], object[BLOCK];
  fill_input(channel, 2 * BLOCK, CHANNEL_ELEMENT_ID, block);
  fill_input(object, BLOCK, OBJECT_ELEMENT_ID, block);
  oar_audio_block_t channel_block = {channel, 2, BLOCK};
  oar_audio_block_t object_block = {object, 1, BLOCK};
  if (oar_update_audio_element_data(oar, CHANNEL_ELEMENT_ID, &channel_block) !=
          ck_oar_ok ||
      oar_update_audio_element_data(oar, OBJECT_ELEMENT_ID, &object_block) !=
          ck_oar_ok)
    return -1;
  oar_audio_block_t output = {out, 2, BLOCK};
  return oar_render(oar, &output) == ck_oar_ok ? 0 : -1;
}

/* Renders BLOCKS blocks with each block's metadata pushed `ahead` blocks
 * before it is rendered (0: just before), after an optional zero-duration
 * push, into out[BLOCKS][2 * BLOCK]. */
static int render_blocks(int ahead, int alternate, int prime, float *out) {
  oar_t *oar = create_oar();
  if (!oar) return -1;
  int ret = 0;
  if (prime && push_metadata(oar, 0, 0, 0) != 0) ret = -1;
  for (int b = 0; ret == 0 && b < ahead && b < BLOCKS; b++)
    ret = push_metadata(oar, b, alternate, BLOCK);
  for (int b = 0; ret == 0 && b < BLOCKS; b++) {
    if (b + ahead < BLOCKS)
      ret = push_metadata(oar, b + ahead, alternate, BLOCK);
    if (ret == 0) ret = feed_and_render(oar, b, out + b * 2 * BLOCK);
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

/* TC1: metadata pushed beyond the spares. */
static int test_pushed_ahead(void) {
  TEST_START("TC1: metadata pushed eight blocks ahead");
  TEST_ASSERT(render_blocks(0, 0, 0, g_expected) == 0, "reference failed");
  TEST_ASSERT(is_output_non_silent(g_expected, BLOCKS * 2 * BLOCK),
              "reference is silent");
  TEST_ASSERT(render_blocks(BLOCKS, 0, 0, g_got) == 0, "render failed");
  TEST_ASSERT(compare(g_got, g_expected) == 0,
              "output differs from the just-in-time pushes");
  return TEST_PASS;
}

/* TC2: per-sample gain arrays interleaved with reusable copies. */
static int test_gain_arrays(void) {
  TEST_START("TC2: per-sample gain arrays pushed ahead");
  TEST_ASSERT(render_blocks(0, 1, 0, g_expected) == 0, "reference failed");
  TEST_ASSERT(is_output_non_silent(g_expected, BLOCKS * 2 * BLOCK),
              "reference is silent");
  TEST_ASSERT(render_blocks(3, 1, 0, g_got) == 0, "render failed");
  TEST_ASSERT(compare(g_got, g_expected) == 0,
              "output differs from the just-in-time pushes");
  return TEST_PASS;
}

/* TC3: zero-duration metadata before the first block. */
static int test_zero_duration(void) {
  TEST_START("TC3: zero-duration metadata before the first block");
  TEST_ASSERT(render_blocks(0, 0, 0, g_expected) == 0, "reference failed");
  TEST_ASSERT(render_blocks(0, 0, 1, g_got) == 0, "render failed");
  TEST_ASSERT(compare(g_got, g_expected) == 0,
              "zero-duration metadata changed the output");
  return TEST_PASS;
}

/* Renders BLOCKS blocks into out[BLOCKS][2 * BLOCK]. With `prime`, block 0's
 * metadata is pushed once before them, covering `prime_duration` samples; no
 * other metadata is pushed. */
static int render_after_prime(int prime, uint32_t prime_duration, float *out) {
  oar_t *oar = create_oar();
  if (!oar) return -1;
  int ret = 0;
  if (prime && push_metadata(oar, 0, 0, prime_duration) != 0) ret = -1;
  for (int b = 0; ret == 0 && b < BLOCKS; b++)
    ret = feed_and_render(oar, b, out + b * 2 * BLOCK);
  oar_destroy(oar);
  return ret;
}

/* Runs render_after_prime() with stderr redirected to a temporary file and
 * stores what it wrote in `captured` (at most size - 1 bytes, terminated).
 * Returns -1 if the redirect or the render fails. */
static int render_capturing_stderr(int prime, uint32_t prime_duration,
                                   float *out, char *captured, size_t size) {
  int ret = -1;
  captured[0] = '\0';
  FILE *capture = tmpfile();
  if (!capture) return -1;
  fflush(stderr);
  const int saved = dup(fileno(stderr));
  if (saved >= 0) {
    if (dup2(fileno(capture), fileno(stderr)) >= 0) {
      ret = render_after_prime(prime, prime_duration, out);
      fflush(stderr);
      if (dup2(saved, fileno(stderr)) < 0) ret = -1;
    }
    close(saved);
  }
  rewind(capture);
  const size_t n = fread(captured, 1, size - 1, capture);
  captured[n] = '\0';
  fclose(capture);
  if (n > 0) fprintf(stderr, "%s", captured);
  return ret;
}

/* TC4: zero-duration metadata, with nothing pushed after it, changes no
 * output and logs no metadata warning. */
static int test_zero_duration_alone(void) {
  char captured[4096];
  TEST_START("TC4: zero-duration metadata with no later metadata");
  TEST_ASSERT(render_after_prime(0, 0, g_expected) == 0, "reference failed");
  TEST_ASSERT(is_output_non_silent(g_expected, BLOCKS * 2 * BLOCK),
              "reference output is silent");
  TEST_ASSERT(
      render_capturing_stderr(1, 0, g_got, captured, sizeof(captured)) == 0,
      "render or stderr capture failed");
  TEST_ASSERT(compare(g_got, g_expected) == 0,
              "zero-duration metadata changed the output");
  TEST_ASSERT(strstr(captured, "negative duration") == NULL,
              "a zero-duration block logged a metadata warning");
  return TEST_PASS;
}

/* TC5: metadata that covers half of the first block logs the warning, so
 * TC4 does not pass because the warning or its capture is missing. */
static int test_partial_cover_warns(void) {
  char captured[4096];
  TEST_START("TC5: metadata covering half a block");
  TEST_ASSERT(render_capturing_stderr(1, BLOCK / 2, g_got, captured,
                                      sizeof(captured)) == 0,
              "render or stderr capture failed");
  TEST_ASSERT(strstr(captured, "negative duration") != NULL,
              "metadata covering half a block logged no warning");
  return TEST_PASS;
}

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "metadata pushed eight blocks ahead", test_pushed_ahead),
    TEST_ENTRY("TC2", "per-sample gain arrays pushed ahead", test_gain_arrays),
    TEST_ENTRY("TC3", "zero-duration metadata before the first block",
               test_zero_duration),
    TEST_ENTRY("TC4", "zero-duration metadata with no later metadata",
               test_zero_duration_alone),
    TEST_ENTRY("TC5", "metadata covering half a block",
               test_partial_cover_warns),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
