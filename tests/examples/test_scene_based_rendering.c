/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
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
 * @file test_scene_based_rendering.c
 * @brief Test scene-based (1st Order Ambisonics) audio element rendering.
 *
 * Test matrix:
 *   TC1: Add a 1OA scene-based element, feed ambisonics data, render, and
 *        verify the output is non-silent.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "oar_base.h"
#include "test_framework.h"
#include "test_helpers.h"

/* Generate 1st Order Ambisonics data (W, Y, Z, X in ACN ordering). */
static void generate_ambisonics_data(float *buffer, uint32_t samples,
                                     float sample_rate) {
  float freq_w = 220.0f;
  float freq_y = 330.0f;
  float freq_z = 440.0f;
  float freq_x = 550.0f;

  for (uint32_t i = 0; i < samples; ++i) {
    float t = (float)i / sample_rate;
    buffer[0 * samples + i] = (float)sin(2.0 * M_PI * freq_w * t);
    buffer[1 * samples + i] = (float)sin(2.0 * M_PI * freq_y * t);
    buffer[2 * samples + i] = (float)sin(2.0 * M_PI * freq_z * t);
    buffer[3 * samples + i] = (float)sin(2.0 * M_PI * freq_x * t);
  }
}

static int test_scene_based_rendering(void) {
  TEST_START("TC1: scene-based rendering (1OA → stereo)");

  oar_config_t oar_cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&oar_cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t element_cfg =
      create_scene_element_config(ck_oar_1oa);
  uint32_t element_id = 1;
  int ret = oar_add_audio_element(oar, gid, element_id, &element_cfg);
  TEST_ASSERT(ret == 0, "oar_add_audio_element failed");

  uint32_t input_channels =
      oar_get_number_of_audio_element_channels(oar, element_id);
  uint32_t output_channels = oar_get_number_of_output_channels(oar);
  uint32_t samples_per_channel = oar_get_samples_per_channel(oar);

  TEST_ASSERT(input_channels == 4, "expected 4 input channels for 1OA");

  /* Prepare input audio data (planar format) */
  oar_audio_block_t input_data;
  TEST_ASSERT(
      alloc_audio_block(input_channels, samples_per_channel, &input_data) == 0,
      "alloc_audio_block failed for input data");

  generate_ambisonics_data(input_data.data, samples_per_channel,
                           (float)oar_cfg.sampling_rate);

  ret = oar_update_audio_element_data(oar, element_id, &input_data);
  free(input_data.data);
  TEST_ASSERT(ret == 0, "oar_update_audio_element_data failed");

  /* Render and verify non-silent output */
  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(output_channels, samples_per_channel, &output) == 0,
      "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);

  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "render output is silent");
  return TEST_PASS;
}

/* --- Test table --------------------------------------------------------- */

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "scene-based rendering", test_scene_based_rendering),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
