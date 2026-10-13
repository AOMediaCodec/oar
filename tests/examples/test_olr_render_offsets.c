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
 * @file test_olr_render_offsets.c
 * @brief Test OLR's position blocks against render calls of other lengths
 *        and at large sample positions.
 *
 * oar_render() hands OLR one position block per render call, of the call's
 * length and at the call's position, so these tests call the object audio
 * renderer directly to reach the other cases.
 *
 * One track renders to stereo.
 *
 * Test matrix:
 *   TC1: Position blocks of 128 samples rendered in calls of 256 samples
 *        give the output of calls of 128 samples. After a block that ended
 *        inside a call, the next block was checked against (uint64_t)-1 and
 *        dropped as an underrun.
 *   TC2: Position blocks rendered at sample positions around 2^31 and 2^32
 *        give the output of the same blocks at position 0, for blocks of the
 *        call's length and of twice the call's length. A processing block
 *        computed its overlap with a render call in int, which overflowed
 *        there.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "object_based_audio_renderer.h"
#include "test_framework.h"

#define BLOCK 256
#define BLOCKS 20
#define STEREO 2

/* Deterministic noise in [-0.5, 0.5) for block `block`. */
static void fill_input(float *data, uint32_t count, int block) {
  uint32_t state = 2654435761u * ((uint32_t)block + 1u);
  for (uint32_t i = 0; i < count; i++) {
    state = state * 1664525u + 1013904223u;
    data[i] = (float)(state >> 8) / 16777216.0f - 0.5f;
  }
}

/* Position block `index` of `duration` samples starting at `start`. */
static metadata_block_t position(int index, uint64_t start, uint64_t duration) {
  metadata_block_t m;
  memset(&m, 0, sizeof(m));
  m.track_id = 0;
  m.start = start;
  m.duration = duration;
  m.azimuth = -60.0f + 6.0f * (float)index;
  m.elevation = 0.0f;
  m.distance = 1.0f;
  return m;
}

static int compare(const float *got, const float *expected, int count) {
  for (int i = 0; i < count; i++) {
    if (got[i] != expected[i]) {
      fprintf(stderr, "sample %d: got %.9g, expected %.9g\n", i, got[i],
              expected[i]);
      return -1;
    }
  }
  return 0;
}

static int is_non_silent(const float *data, int count) {
  for (int i = 0; i < count; i++)
    if (data[i] != 0.0f) return 1;
  return 0;
}

/* Renders the `count` samples of `in` starting at sample `base`, in calls of
 * `call` samples, into out (planar, `count` samples per channel). */
static int render_calls(object_audio_renderer_t *olr, const float *in,
                        int count, int call, uint64_t base, float *out) {
  for (int c = 0; c < count; c += call) {
    float o[STEREO * 2 * BLOCK];
    memset(o, 0, sizeof(o));
    if (object_audio_renderer_render(olr, (float *)in + c, call, 1,
                                     base + (uint64_t)c, o) != 0)
      return -1;
    for (int ch = 0; ch < STEREO; ch++)
      memcpy(out + ch * count + c, o + ch * call, sizeof(float) * call);
  }
  return 0;
}

static float g_expected[BLOCKS * STEREO * BLOCK];
static float g_got[BLOCKS * STEREO * BLOCK];

/* Renders BLOCKS blocks of input with 128-sample position blocks, in calls
 * of `call` samples, into out. */
static int render_short_blocks(int call, float *out) {
  const int half = BLOCK / 2;
  float in[BLOCK];
  object_audio_renderer_t *olr = object_audio_renderer_create(
      ck_olr_layout_200, ck_gain_calculator_custom, 48000);
  if (!olr) return -1;
  int ret = 0;
  for (int b = 0; ret == 0 && b < 2 * BLOCKS; b++) {
    metadata_block_t m = position(b, (uint64_t)b * half, half);
    ret = object_audio_renderer_add_metadatas(olr, &m, 1);
  }
  for (int b = 0; ret == 0 && b < BLOCKS; b++) {
    fill_input(in, BLOCK, b);
    ret = render_calls(olr, in, BLOCK, call, (uint64_t)b * BLOCK,
                       out + b * STEREO * BLOCK);
  }
  object_audio_renderer_destroy(olr);
  return ret;
}

/* TC1: position blocks shorter than the render call. */
static int test_blocks_inside_a_call(void) {
  TEST_START("TC1: position blocks shorter than a render call");
  TEST_ASSERT(render_short_blocks(BLOCK / 2, g_expected) == 0,
              "reference failed");
  TEST_ASSERT(is_non_silent(g_expected, BLOCKS * STEREO * BLOCK),
              "reference is silent");
  TEST_ASSERT(render_short_blocks(BLOCK, g_got) == 0, "render failed");
  TEST_ASSERT(compare(g_got, g_expected, BLOCKS * STEREO * BLOCK) == 0,
              "calls of two blocks render differently");
  return TEST_PASS;
}

/* Renders two position blocks of `block` samples starting at `base`, in
 * calls of `call` samples, into out (2 * block samples per channel). block
 * is at most 2 * BLOCK and call at most block. */
static int render_at(uint64_t base, int block, int call, float *out) {
  float in[4 * BLOCK];
  object_audio_renderer_t *olr = object_audio_renderer_create(
      ck_olr_layout_200, ck_gain_calculator_custom, 48000);
  if (!olr) return -1;
  int ret = 0;
  for (int b = 0; ret == 0 && b < 2; b++) {
    metadata_block_t m =
        position(b, base + (uint64_t)(b * block), (uint64_t)block);
    ret = object_audio_renderer_add_metadatas(olr, &m, 1);
  }
  fill_input(in, 2 * block, 3);
  if (ret == 0) ret = render_calls(olr, in, 2 * block, call, base, out);
  object_audio_renderer_destroy(olr);
  return ret;
}

/* TC2: sample positions beyond the range of int. */
static int test_large_sample_positions(void) {
  TEST_START("TC2: sample positions around 2^31 and 2^32");
  const uint64_t bases[] = {(1ull << 31) - 256, 1ull << 31, (1ull << 32) - 256,
                            1ull << 32};
  const int layouts[][2] = {{BLOCK, BLOCK}, {2 * BLOCK, BLOCK}};
  for (size_t l = 0; l < sizeof(layouts) / sizeof(layouts[0]); l++) {
    const int block = layouts[l][0], call = layouts[l][1];
    TEST_ASSERT(render_at(0, block, call, g_expected) == 0, "reference failed");
    TEST_ASSERT(is_non_silent(g_expected, STEREO * 2 * block),
                "reference is silent");
    for (size_t b = 0; b < sizeof(bases) / sizeof(bases[0]); b++) {
      TEST_ASSERT(render_at(bases[b], block, call, g_got) == 0,
                  "render failed");
      if (compare(g_got, g_expected, STEREO * 2 * block) != 0) {
        fprintf(stderr, "base %llu, block %d, call %d\n",
                (unsigned long long)bases[b], block, call);
        TEST_ASSERT(0, "output differs from the same blocks at position 0");
      }
    }
  }
  return TEST_PASS;
}

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "position blocks shorter than a render call",
               test_blocks_inside_a_call),
    TEST_ENTRY("TC2", "sample positions around 2^31 and 2^32",
               test_large_sample_positions),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
