/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License was not distributed with this source
 * code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

/**
 * @file test_hoa_sound_system_h.c
 * @brief Regression test for the HOA to Sound System H (9+10+3) LFE remap.
 *
 * The remap in IAMF_element_renderer_render_H2M() compared the LFE output
 * slot indices (lfe1 = 3, lfe2 = 9) against the matrix column index instead
 * of the output slot being assigned. For Sound System H (22 matrix columns
 * + 2 LFE slots) the second LFE skip came one column late: column 8 was
 * moved into the LFE2 slot and then overwritten by LFE generation (signal
 * lost), while output slot 10 was never remapped and kept a duplicate of
 * column 10.
 *
 * Invariant under test: each of the 22 non-LFE output slots must be fed by
 * exactly one matrix column, i.e. no two non-LFE slots may carry identical
 * responses to one-hot HOA inputs (with the bug, slots 10 and 12 are
 * bit-identical).
 *
 * Test matrix:
 *   TC1: 1OA -> A293: non-LFE slot responses are non-silent and pairwise
 *        distinct.
 *   TC2: 3OA -> A293: same invariant with a different decoder matrix.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "test_framework.h"
#include "test_helpers.h"

/* Channel count of ck_oar_layout_a293 (Sound System H, 9+10+3). */
#define NUM_OUT_CHANNELS 24
#define MAX_HOA_CHANNELS 16 /* 3OA */
/* LFE slot indices of Sound System H; their content is generated, not
 * remapped, so they are excluded from the matrix-column checks. */
#define LFE1_SLOT_INDEX 3
#define LFE2_SLOT_INDEX 9
#define BLOCK_SIZE 64

static int is_lfe_slot(int slot) {
  return slot == LFE1_SLOT_INDEX || slot == LFE2_SLOT_INDEX;
}

/* Renders `num_hoa` one-hot constant signals through a scene element of the
 * given HOA order into Sound System H output, collecting each slot's
 * response to each HOA channel. The decode matrix is time-invariant, so
 * sample 0 of each render is representative. Returns 0 on success. */
static int collect_slot_responses(oar_hoa_t order, uint32_t num_hoa,
                                  float resp[][MAX_HOA_CHANNELS]) {
  oar_config_t config =
      create_config(ck_oar_layout_a293, BLOCK_SIZE, TEST_SAMPLING_RATE);
  oar_t *oar = oar_create(&config);
  if (oar == NULL) return -1;

  oar_audio_element_config_t element_cfg = create_scene_element_config(order);
  int group_id = oar_add_audio_group(oar);
  const uint32_t element_id = 1;
  if (group_id < 0 ||
      oar_add_audio_element(oar, group_id, element_id, &element_cfg) != 0) {
    oar_destroy(oar);
    return -1;
  }

  const uint32_t in_channels =
      oar_get_number_of_audio_element_channels(oar, element_id);
  const uint32_t out_channels = oar_get_number_of_output_channels(oar);
  if (in_channels != num_hoa || out_channels != NUM_OUT_CHANNELS) {
    oar_destroy(oar);
    return -1;
  }

  float *input =
      (float *)calloc((size_t)in_channels * BLOCK_SIZE, sizeof(float));
  float *output =
      (float *)calloc((size_t)out_channels * BLOCK_SIZE, sizeof(float));
  if (input == NULL || output == NULL) {
    free(input);
    free(output);
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t input_block = {.channels = in_channels,
                                   .samples_per_channel = BLOCK_SIZE,
                                   .data = input};
  oar_audio_block_t output_block = {.channels = out_channels,
                                    .samples_per_channel = BLOCK_SIZE,
                                    .data = output};

  int ret = 0;
  for (uint32_t h = 0; h < num_hoa && ret == 0; ++h) {
    memset(input, 0, (size_t)in_channels * BLOCK_SIZE * sizeof(float));
    for (int s = 0; s < BLOCK_SIZE; ++s) input[h * BLOCK_SIZE + s] = 1.0f;
    if (oar_update_audio_element_data(oar, element_id, &input_block) != 0 ||
        oar_render(oar, &output_block) != 0) {
      ret = -1;
      break;
    }
    for (int c = 0; c < NUM_OUT_CHANNELS; ++c) {
      resp[c][h] = output[c * BLOCK_SIZE];
    }
  }

  free(input);
  free(output);
  oar_destroy(oar);
  return ret;
}

static int check_slots_unique_and_non_silent(oar_hoa_t order,
                                             uint32_t num_hoa) {
  float resp[NUM_OUT_CHANNELS][MAX_HOA_CHANNELS];
  TEST_ASSERT(collect_slot_responses(order, num_hoa, resp) == 0,
              "failed to render HOA to Sound System H");

  for (int a = 0; a < NUM_OUT_CHANNELS; ++a) {
    if (is_lfe_slot(a)) continue;

    float energy = 0.0f;
    for (uint32_t h = 0; h < num_hoa; ++h) energy += fabsf(resp[a][h]);
    TEST_ASSERTF(energy > 0.0f, "non-LFE slot %d is silent", a);

    for (int b = a + 1; b < NUM_OUT_CHANNELS; ++b) {
      if (is_lfe_slot(b)) continue;
      float dist = 0.0f;
      for (uint32_t h = 0; h < num_hoa; ++h) {
        dist += fabsf(resp[a][h] - resp[b][h]);
      }
      /* With the remap bug, slots 10 and 12 are bit-identical. */
      TEST_ASSERTF(dist > 1e-5f,
                   "non-LFE slots %d and %d carry identical matrix columns "
                   "(dist=%g)",
                   a, b, (double)dist);
    }
  }
  return TEST_PASS;
}

/* TC1: 1OA -> A293: every non-LFE slot is fed by exactly one matrix column.
 */
static int test_sound_system_h_1oa(void) {
  TEST_START("TC1: 1OA -> Sound System H, slot responses unique");
  return check_slots_unique_and_non_silent(ck_oar_1oa, 4);
}

/* TC2: 3OA -> A293: same invariant with a different decoder matrix. */
static int test_sound_system_h_3oa(void) {
  TEST_START("TC2: 3OA -> Sound System H, slot responses unique");
  return check_slots_unique_and_non_silent(ck_oar_3oa, 16);
}

/* --- Test table --------------------------------------------------------- */

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "1OA -> Sound System H, slots unique",
               test_sound_system_h_1oa),
    TEST_ENTRY("TC2", "3OA -> Sound System H, slots unique",
               test_sound_system_h_3oa),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
