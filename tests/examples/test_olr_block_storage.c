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
 * @file test_olr_block_storage.c
 * @brief Test OLR's preallocated metadata ring and processing block storage.
 *
 * Each OLR track keeps the metadata blocks it has not interpreted yet in a
 * ring of eight that doubles when a caller queues more, and builds its
 * processing block in storage with an interpolation table sized for the
 * longest block expected. oar_render() hands OLR one metadata block per
 * render, so these tests call OLR directly to queue more and to process
 * blocks longer than the storage was sized for.
 *
 * Test matrix:
 *   TC1: Twenty metadata blocks queued before the first render (the ring
 *        grows from 8 to 32) render bit-identically to one block queued
 *        before each render.
 *   TC2: An interpolated-gains block built in storage processes
 *        bit-identically to one from interp_gains_create(), for blocks
 *        shorter and longer than the storage's table, in that order and
 *        back.
 *   TC3: A fixed-gains block built in storage processes bit-identically to
 *        one from fixed_gains_create().
 *   TC4: Destroying a renderer while a track's processing block is still
 *        queued (its metadata block runs past the last render) frees the
 *        block before its storage. Under ASan the previous order reported a
 *        heap-use-after-free.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/renderer/olr/object_audio_renderer/block_processor/processing_block.h"
#include "src/renderer/olr/object_audio_renderer/object_based_audio_renderer.h"
#include "test_framework.h"

#define BLOCK 256
#define BLOCKS 20
#define STEREO 2
#define NUM_GAINS 3

/* Deterministic noise in [-0.5, 0.5) for block `block`. */
static void fill_input(float *data, uint32_t count, int block) {
  uint32_t state = 2654435761u * ((uint32_t)block + 1u);
  for (uint32_t i = 0; i < count; i++) {
    state = state * 1664525u + 1013904223u;
    data[i] = (float)(state >> 8) / 16777216.0f - 0.5f;
  }
}

static metadata_block_t position(int block) {
  metadata_block_t m;
  memset(&m, 0, sizeof(m));
  m.track_id = 0;
  m.start = (uint64_t)block * BLOCK;
  m.duration = BLOCK;
  m.azimuth = -60.0f + 6.0f * (float)block;
  m.elevation = 0.0f;
  m.distance = 1.0f;
  return m;
}

/* Renders BLOCKS blocks of one track to stereo, queueing every metadata
 * block before the first render when `ahead` is set. */
static int render_track(int ahead, float *out) {
  float in[BLOCK];
  object_audio_renderer_t *olr = object_audio_renderer_create(
      ck_olr_layout_200, ck_gain_calculator_custom, 48000, BLOCK);
  if (!olr) return -1;
  int ret = object_audio_renderer_add_tracks(olr, 1);
  for (int b = 0; ret == 0 && ahead && b < BLOCKS; b++) {
    metadata_block_t m = position(b);
    ret = object_audio_renderer_add_metadatas(olr, &m, 1);
  }
  for (int b = 0; ret == 0 && b < BLOCKS; b++) {
    if (!ahead) {
      metadata_block_t m = position(b);
      ret = object_audio_renderer_add_metadatas(olr, &m, 1);
    }
    fill_input(in, BLOCK, b);
    float *o = out + b * STEREO * BLOCK;
    memset(o, 0, sizeof(float) * STEREO * BLOCK);
    if (ret == 0)
      ret = object_audio_renderer_render(olr, in, BLOCK, 1, (uint64_t)b * BLOCK,
                                         o);
  }
  object_audio_renderer_destroy(olr);
  return ret;
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

static float g_expected[BLOCKS * STEREO * BLOCK];
static float g_got[BLOCKS * STEREO * BLOCK];

/* TC1: the metadata ring grows past its first eight blocks. */
static int test_ring_growth(void) {
  TEST_START("TC1: twenty metadata blocks queued ahead");
  TEST_ASSERT(render_track(0, g_expected) == 0, "reference failed");
  TEST_ASSERT(is_non_silent(g_expected, BLOCKS * STEREO * BLOCK),
              "reference is silent");
  TEST_ASSERT(render_track(1, g_got) == 0, "render failed");
  TEST_ASSERT(compare(g_got, g_expected, BLOCKS * STEREO * BLOCK) == 0,
              "queued blocks render differently");
  return TEST_PASS;
}

/* Processes `block` over [start, start + len) into out (zeroed first). */
static void process(processing_block_t *block, uint64_t start, int len,
                    const float *in, float *out) {
  memset(out, 0, sizeof(float) * NUM_GAINS * len);
  block->process(block, start, (float *)in, len, 1, out);
}

/* TC2: storage-backed interpolated gains against allocated ones. */
static int test_interp_storage(void) {
  TEST_START("TC2: interpolated gains in storage");

  auto_float_t gains_start[NUM_GAINS] = {1.0f, 0.25f, -0.5f};
  auto_float_t gains_end[NUM_GAINS] = {0.0f, 0.75f, 0.5f};
  const int lengths[] = {96, 128, 480, 64, 480};
  float in[480], expected[NUM_GAINS * 480], got[NUM_GAINS * 480];

  processing_block_storage_t *storage = processing_block_storage_create(128);
  TEST_ASSERT(storage != NULL, "processing_block_storage_create failed");

  uint64_t start = 1000;
  for (size_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
    const int len = lengths[i];
    fill_input(in, (uint32_t)len, (int)i);

    processing_block_t *ref = interp_gains_create(
        start, start + (uint64_t)len, gains_start, gains_end, NUM_GAINS);
    TEST_ASSERT(ref != NULL, "interp_gains_create failed");
    process(ref, start, len, in, expected);
    ref->destroy(ref);

    processing_block_t *block =
        interp_gains_init(storage, start, start + (uint64_t)len, gains_start,
                          gains_end, NUM_GAINS);
    TEST_ASSERT(block != NULL, "interp_gains_init failed");
    process(block, start, len, in, got);
    block->destroy(block);

    if (compare(got, expected, NUM_GAINS * len) != 0) {
      processing_block_storage_destroy(storage);
      fprintf(stderr, "block of %d samples\n", len);
      TEST_ASSERT(0, "storage block differs from the allocated block");
    }
    start += (uint64_t)len;
  }

  processing_block_storage_destroy(storage);
  return TEST_PASS;
}

/* TC3: storage-backed fixed gains against allocated ones. */
static int test_fixed_storage(void) {
  TEST_START("TC3: fixed gains in storage");

  auto_float_t gains[NUM_GAINS] = {0.5f, -0.25f, 1.0f};
  float in[BLOCK], expected[NUM_GAINS * BLOCK], got[NUM_GAINS * BLOCK];
  fill_input(in, BLOCK, 7);

  processing_block_t *ref = fixed_gains_create(0, BLOCK, gains, NUM_GAINS);
  TEST_ASSERT(ref != NULL, "fixed_gains_create failed");
  process(ref, 0, BLOCK, in, expected);
  ref->destroy(ref);

  processing_block_storage_t *storage = processing_block_storage_create(BLOCK);
  TEST_ASSERT(storage != NULL, "processing_block_storage_create failed");
  processing_block_t *block =
      fixed_gains_init(storage, 0, BLOCK, gains, NUM_GAINS);
  process(block, 0, BLOCK, in, got);
  block->destroy(block);
  processing_block_storage_destroy(storage);

  TEST_ASSERT(compare(got, expected, NUM_GAINS * BLOCK) == 0,
              "storage block differs from the allocated block");
  return TEST_PASS;
}

/* TC4: destroy with a processing block still queued. */
static int test_destroy_mid_block(void) {
  TEST_START("TC4: destroy with a processing block queued");

  float in[BLOCK], out[STEREO * BLOCK];
  object_audio_renderer_t *olr = object_audio_renderer_create(
      ck_olr_layout_200, ck_gain_calculator_custom, 48000, 2 * BLOCK);
  TEST_ASSERT(olr != NULL, "object_audio_renderer_create failed");
  TEST_ASSERT(object_audio_renderer_add_tracks(olr, 1) == 0,
              "object_audio_renderer_add_tracks failed");

  /* One metadata block of two render blocks, then one render block. */
  metadata_block_t m = position(0);
  m.duration = 2 * BLOCK;
  TEST_ASSERT(object_audio_renderer_add_metadatas(olr, &m, 1) == 0,
              "object_audio_renderer_add_metadatas failed");
  fill_input(in, BLOCK, 0);
  memset(out, 0, sizeof(out));
  TEST_ASSERT(object_audio_renderer_render(olr, in, BLOCK, 1, 0, out) == 0,
              "object_audio_renderer_render failed");
  TEST_ASSERT(is_non_silent(out, STEREO * BLOCK), "output is silent");

  object_audio_renderer_destroy(olr);
  return TEST_PASS;
}

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "twenty metadata blocks queued ahead", test_ring_growth),
    TEST_ENTRY("TC2", "interpolated gains in storage", test_interp_storage),
    TEST_ENTRY("TC3", "fixed gains in storage", test_fixed_storage),
    TEST_ENTRY("TC4", "destroy with a processing block queued",
               test_destroy_mid_block),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
