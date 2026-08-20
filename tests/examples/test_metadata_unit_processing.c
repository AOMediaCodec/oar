/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

/**
 * @file test_metadata_unit_processing.c
 * @brief Test cases for oar_set_metadata_unit_to_process with binaural
 * rendering.
 *
 * Test matrix:
 *   TC1: binaural, add_group → set_metadata(32) → expect success
 *   TC2: binaural, add_group → set_metadata(512) → render → success
 *   TC3: stereo, add_group → set_metadata(32) → render → success
 *   TC4: binaural, set_metadata(32) → add_group → render → success + non-silent
 *   TC5: binaural, add_group → render (no set_metadata call) → success
 *   TC6: Invalid parameters → expect errors
 *   TC7: binaural, set_metadata(32) → set_metadata(64) → add_group → render
 *   TC8: binaural, non-divisor samples → expect success
 *   TC9: binaural, zero samples → expect error
 *   TC10: binaural, samples > frame → expect error
 *   TC11: stereo, non-divisor samples → expect success
 *   TC12: stereo, varying positions across sub-frames → verify position update
 */

#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#else
#include <process.h>
#include <windows.h>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "oar.h"

#define TEST_SAMPLING_RATE 48000
#define TEST_SAMPLES_PER_CHANNEL 512
#define TEST_SUB_FRAME_SAMPLES 32
#define TEST_SUB_FRAME_SAMPLES_ALT 64
#define TEST_ELEMENT_ID 1

static void generate_sine_wave(float *buffer, uint32_t samples, float frequency,
                               float sample_rate) {
  for (uint32_t i = 0; i < samples; ++i) {
    buffer[i] = (float)sin(2.0 * M_PI * frequency * ((float)i / sample_rate));
  }
}

static oar_metadata_t *create_position_metadata(polar_t position,
                                                uint32_t duration) {
  oar_metadata_t *metadata = (oar_metadata_t *)malloc(sizeof(oar_metadata_t));
  if (!metadata) return NULL;

  metadata->type = ck_metadata_object_positions;
  metadata->duration = (int)duration;
  metadata->object_positions.param_type = ck_param_constant;
  metadata->object_positions.position_type = ck_polar;
  metadata->object_positions.num_objects = 1;
  metadata->object_positions.polar_positions[0].azimuth = position.azimuth;
  metadata->object_positions.polar_positions[0].elevation = position.elevation;
  metadata->object_positions.polar_positions[0].distance = position.distance;

  return metadata;
}

static int is_output_non_silent(const float *data, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    if (fabs(data[i]) > 1e-9f) return 1;
  }
  return 0;
}

static int add_object_element_and_data(oar_t *oar, uint32_t *out_channels,
                                       float **out_input_data) {
  int gid = oar_add_audio_group(oar);
  if (gid < 0) return -1;

  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_object_based;
  elem_cfg.obc.num_objects = 1;
  memset(&elem_cfg.parameters, 0, sizeof(parameter_set_t));

  if (oar_add_audio_element(oar, gid, TEST_ELEMENT_ID, &elem_cfg) != 0)
    return -1;

  uint32_t input_channels =
      oar_get_number_of_audio_element_channels(oar, TEST_ELEMENT_ID);

  oar_audio_block_t input_data;
  input_data.channels = input_channels;
  input_data.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  input_data.data = (float *)malloc(input_channels * TEST_SAMPLES_PER_CHANNEL *
                                    sizeof(float));
  if (!input_data.data) return -1;

  generate_sine_wave(input_data.data, TEST_SAMPLES_PER_CHANNEL, 440.0f,
                     (float)TEST_SAMPLING_RATE);
  oar_update_audio_element_data(oar, TEST_ELEMENT_ID, &input_data);

  polar_t position = {30.0f, 0.0f, 1.0f};
  oar_metadata_t *pos_meta =
      create_position_metadata(position, TEST_SAMPLES_PER_CHANNEL);
  if (pos_meta) {
    oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, pos_meta);
    free(pos_meta);
  }

  *out_channels = oar_get_number_of_output_channels(oar);
  *out_input_data = input_data.data;
  return 0;
}

/* Like add_object_element_and_data but without adding default position
 * metadata. Used by TC12 which provides its own position metadata. */
static int add_object_element_without_position(oar_t *oar,
                                               uint32_t *out_channels,
                                               float **out_input_data) {
  int gid = oar_add_audio_group(oar);
  if (gid < 0) return -1;

  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_object_based;
  elem_cfg.obc.num_objects = 1;
  memset(&elem_cfg.parameters, 0, sizeof(parameter_set_t));

  if (oar_add_audio_element(oar, gid, TEST_ELEMENT_ID, &elem_cfg) != 0)
    return -1;

  uint32_t input_channels =
      oar_get_number_of_audio_element_channels(oar, TEST_ELEMENT_ID);

  oar_audio_block_t input_data;
  input_data.channels = input_channels;
  input_data.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  input_data.data = (float *)malloc(input_channels * TEST_SAMPLES_PER_CHANNEL *
                                    sizeof(float));
  if (!input_data.data) return -1;

  generate_sine_wave(input_data.data, TEST_SAMPLES_PER_CHANNEL, 440.0f,
                     (float)TEST_SAMPLING_RATE);
  oar_update_audio_element_data(oar, TEST_ELEMENT_ID, &input_data);

  *out_channels = oar_get_number_of_output_channels(oar);
  *out_input_data = input_data.data;
  return 0;
}

/* TC1: set_metadata(32) after add_group (binaural) — should succeed */

static int test_set_metadata_after_add_group_binaural() {
  printf("\n===== TC1: set_metadata(32) after add_group (binaural) =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) {
    fprintf(stderr, "FAIL: Failed to create OAR instance.\n");
    return -1;
  }

  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (add_object_element_and_data(oar, &out_channels, &input_data) != 0) {
    fprintf(stderr, "FAIL: Failed to setup binaural OAR.\n");
    oar_destroy(oar);
    return -1;
  }

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  if (ret != 0) {
    fprintf(stderr,
            "FAIL: set_metadata(32) failed after add_group (binaural). "
            "Expected success, got %d.\n",
            ret);
    free(input_data);
    oar_destroy(oar);
    return -1;
  }

  printf("PASS: set_metadata(32) succeeded after add_group (binaural).\n");

  free(input_data);
  oar_destroy(oar);
  return 0;
}

/* TC2: set_metadata(512) after add_group (equal, no-op) — should succeed */
static int test_set_metadata_equal_after_add_group() {
  printf(
      "\n===== TC2: set_metadata(512) after add_group (equal, no-op) =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (add_object_element_and_data(oar, &out_channels, &input_data) != 0) {
    oar_destroy(oar);
    return -1;
  }

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SAMPLES_PER_CHANNEL);
  if (ret != 0) {
    fprintf(stderr, "FAIL: set_metadata(512) returned %d, expected 0.\n", ret);
    free(input_data);
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)malloc(out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
  if (!output.data) {
    free(input_data);
    oar_destroy(oar);
    return -1;
  }
  memset(output.data, 0,
         out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));

  ret = oar_render(oar, &output);

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  if (ret != 0) {
    fprintf(stderr, "FAIL: oar_render returned %d\n", ret);
    return -1;
  }

  printf("PASS: set_metadata(512) + render succeeded.\n");
  return 0;
}

/* TC3: set_metadata(32) with stereo layout — should succeed */
static int test_set_metadata_stereo() {
  printf("\n===== TC3: set_metadata(32) with stereo layout =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_stereo;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (add_object_element_and_data(oar, &out_channels, &input_data) != 0) {
    oar_destroy(oar);
    return -1;
  }

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  if (ret != 0) {
    fprintf(stderr, "FAIL: set_metadata(32) on stereo returned %d\n", ret);
    free(input_data);
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)malloc(out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
  if (!output.data) {
    free(input_data);
    oar_destroy(oar);
    return -1;
  }
  memset(output.data, 0,
         out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));

  ret = oar_render(oar, &output);

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  if (ret != 0) {
    fprintf(stderr, "FAIL: oar_render on stereo returned %d\n", ret);
    return -1;
  }

  printf("PASS: stereo + set_metadata(32) + render succeeded.\n");
  return 0;
}

/* TC4: set_metadata(32) before add_group (binaural) — should succeed */
static int test_set_metadata_before_add_group_binaural() {
  printf("\n===== TC4: set_metadata(32) before add_group (binaural) =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  if (ret != 0) {
    fprintf(stderr, "FAIL: set_metadata(32) before add_group returned %d\n",
            ret);
    oar_destroy(oar);
    return -1;
  }

  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (add_object_element_and_data(oar, &out_channels, &input_data) != 0) {
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)malloc(out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
  if (!output.data) {
    free(input_data);
    oar_destroy(oar);
    return -1;
  }
  memset(output.data, 0,
         out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));

  ret = oar_render(oar, &output);

  if (ret != 0) {
    fprintf(stderr, "FAIL: oar_render returned %d\n", ret);
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  uint32_t total_samples = out_channels * TEST_SAMPLES_PER_CHANNEL;
  if (!is_output_non_silent(output.data, total_samples)) {
    fprintf(stderr,
            "FAIL: output is silent after sub-frame rendering. "
            "OBR may not have processed the 32-sample blocks.\n");
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  printf(
      "PASS: set_metadata(32) before add_group + render succeeded "
      "(output is non-silent).\n");

  free(input_data);
  free(output.data);
  oar_destroy(oar);
  return 0;
}

/* TC5: default (no set_metadata call, binaural) — backward compatibility */
static int test_default_no_set_metadata_binaural() {
  printf("\n===== TC5: default (no set_metadata call, binaural) =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (add_object_element_and_data(oar, &out_channels, &input_data) != 0) {
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)malloc(out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
  if (!output.data) {
    free(input_data);
    oar_destroy(oar);
    return -1;
  }
  memset(output.data, 0,
         out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));

  int ret = oar_render(oar, &output);

  if (ret != 0) {
    fprintf(stderr, "FAIL: oar_render returned %d (default path)\n", ret);
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  uint32_t total_samples = out_channels * TEST_SAMPLES_PER_CHANNEL;
  if (!is_output_non_silent(output.data, total_samples)) {
    fprintf(stderr, "FAIL: output is silent in default path.\n");
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  printf("PASS: default (no set_metadata) + render succeeded (non-silent).\n");

  free(input_data);
  free(output.data);
  oar_destroy(oar);
  return 0;
}

/* TC6: invalid parameters — NULL oar and unsupported metadata type */
static int test_invalid_parameters() {
  printf("\n===== TC6: invalid parameters =====\n");

  int ret = oar_set_metadata_unit_to_process(NULL, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  if (ret == 0) {
    fprintf(stderr, "FAIL: set_metadata(NULL) succeeded, expected error.\n");
    return -1;
  }
  printf("  NULL oar: returned %d (expected error) OK\n", ret);

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  ret = oar_set_metadata_unit_to_process(oar, ck_metadata_gain,
                                         TEST_SUB_FRAME_SAMPLES);
  if (ret == 0) {
    fprintf(
        stderr,
        "FAIL: set_metadata(ck_metadata_gain) succeeded, expected error.\n");
    oar_destroy(oar);
    return -1;
  }
  printf("  unsupported type: returned %d (expected error) OK\n", ret);

  oar_destroy(oar);
  printf("PASS: invalid parameters correctly rejected.\n");
  return 0;
}

/* TC7: multiple set_metadata before add_group — last value wins */
static int test_multiple_set_metadata_before_add_group() {
  printf("\n===== TC7: multiple set_metadata before add_group =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  if (ret != 0) {
    fprintf(stderr, "FAIL: first set_metadata(32) returned %d\n", ret);
    oar_destroy(oar);
    return -1;
  }

  ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                         TEST_SUB_FRAME_SAMPLES_ALT);
  if (ret != 0) {
    fprintf(stderr, "FAIL: second set_metadata(64) returned %d\n", ret);
    oar_destroy(oar);
    return -1;
  }

  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (add_object_element_and_data(oar, &out_channels, &input_data) != 0) {
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)malloc(out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
  if (!output.data) {
    free(input_data);
    oar_destroy(oar);
    return -1;
  }
  memset(output.data, 0,
         out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));

  ret = oar_render(oar, &output);

  if (ret != 0) {
    fprintf(stderr, "FAIL: oar_render returned %d\n", ret);
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  uint32_t total_samples = out_channels * TEST_SAMPLES_PER_CHANNEL;
  if (!is_output_non_silent(output.data, total_samples)) {
    fprintf(stderr, "FAIL: output is silent after multiple set_metadata.\n");
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  printf(
      "PASS: multiple set_metadata (last=64) + render succeeded "
      "(non-silent).\n");

  free(input_data);
  free(output.data);
  oar_destroy(oar);
  return 0;
}

/* TC8: binaural, non-divisor samples → expect success */
static int test_binaural_non_divisor_accepted() {
  printf("\n===== TC8: binaural non-divisor accepted =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int ret =
      oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions, 100);
  oar_destroy(oar);

  if (ret != 0) {
    fprintf(stderr, "FAIL: non-divisor 100 rejected for binaural (%d).\n", ret);
    return -1;
  }
  printf("PASS: non-divisor 100 accepted for binaural.\n");
  return 0;
}

/* TC9: zero samples → expect error */
static int test_zero_samples_rejected() {
  printf("\n===== TC9: zero samples rejected =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int ret =
      oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions, 0);
  oar_destroy(oar);

  if (ret == 0) {
    fprintf(stderr, "FAIL: zero samples accepted.\n");
    return -1;
  }
  printf("PASS: zero samples rejected (%d).\n", ret);
  return 0;
}

/* TC10: samples > frame → expect error */
static int test_exceeds_frame_rejected() {
  printf("\n===== TC10: samples > frame rejected =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SAMPLES_PER_CHANNEL * 2);
  oar_destroy(oar);

  if (ret == 0) {
    fprintf(stderr, "FAIL: samples > frame accepted.\n");
    return -1;
  }
  printf("PASS: samples > frame rejected (%d).\n", ret);
  return 0;
}

/* TC11: stereo, non-divisor samples → expect success */
static int test_stereo_non_divisor_accepted() {
  printf("\n===== TC11: stereo non-divisor accepted =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_stereo;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int ret =
      oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions, 100);
  oar_destroy(oar);

  if (ret != 0) {
    fprintf(stderr, "FAIL: non-divisor 100 rejected for stereo (%d).\n", ret);
    return -1;
  }
  printf("PASS: non-divisor 100 accepted for stereo.\n");
  return 0;
}

/* TC12: stereo, varying positions across sub-frames → verify position update */
static int test_stereo_varying_positions_sub_frame() {
  printf("\n===== TC12: stereo varying positions sub-frame =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_stereo;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  if (ret != 0) {
    oar_destroy(oar);
    return -1;
  }

  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (add_object_element_without_position(oar, &out_channels, &input_data) !=
      0) {
    oar_destroy(oar);
    return -1;
  }

  /* Provide two position metadata entries: first half left, second half right.
   * In stereo layout: ch0 = +30° (left speaker), ch1 = -30° (right speaker).
   * Use ±80° so positions are clearly on one side, producing asymmetric gain.
   * First half: +80° (far left) → ch0 dominant
   * Second half: -80° (far right) → ch1 dominant */
  polar_t pos_left = {80.0f, 0.0f, 1.0f};
  polar_t pos_right = {-80.0f, 0.0f, 1.0f};

  oar_metadata_t *meta_left =
      create_position_metadata(pos_left, TEST_SAMPLES_PER_CHANNEL / 2);
  oar_metadata_t *meta_right =
      create_position_metadata(pos_right, TEST_SAMPLES_PER_CHANNEL / 2);

  if (meta_left) {
    oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, meta_left);
    free(meta_left);
  }
  if (meta_right) {
    oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, meta_right);
    free(meta_right);
  }

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)calloc(out_channels * TEST_SAMPLES_PER_CHANNEL, sizeof(float));
  if (!output.data) {
    free(input_data);
    oar_destroy(oar);
    return -1;
  }

  ret = oar_render(oar, &output);

  if (ret != 0) {
    fprintf(stderr, "FAIL: oar_render returned %d\n", ret);
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  /* Verify: left channel first half energy > second half, right channel vice
   * versa */
  uint32_t half = TEST_SAMPLES_PER_CHANNEL / 2;
  float left_first_half = 0.0f, left_second_half = 0.0f;
  float right_first_half = 0.0f, right_second_half = 0.0f;

  for (uint32_t i = 0; i < half; i++) {
    left_first_half += fabsf(output.data[i]);
    right_first_half += fabsf(output.data[TEST_SAMPLES_PER_CHANNEL + i]);
  }
  for (uint32_t i = half; i < TEST_SAMPLES_PER_CHANNEL; i++) {
    left_second_half += fabsf(output.data[i]);
    right_second_half += fabsf(output.data[TEST_SAMPLES_PER_CHANNEL + i]);
  }

  if (left_first_half <= left_second_half ||
      right_second_half <= right_first_half) {
    fprintf(stderr,
            "FAIL: sub-frame position update not reflected in output.\n"
            "  Left ch:  first_half=%.4f, second_half=%.4f\n"
            "  Right ch: first_half=%.4f, second_half=%.4f\n",
            left_first_half, left_second_half, right_first_half,
            right_second_half);
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  printf("PASS: sub-frame position updates reflected in output.\n");

  free(input_data);
  free(output.data);
  oar_destroy(oar);
  return 0;
}

/* Test function table for child-process dispatch. */
typedef int (*test_fn_t)(void);
static test_fn_t g_test_table[] = {
    test_set_metadata_after_add_group_binaural,  /* 0  TC1 */
    test_set_metadata_equal_after_add_group,     /* 1  TC2 */
    test_set_metadata_stereo,                    /* 2  TC3 */
    test_set_metadata_before_add_group_binaural, /* 3  TC4 */
    test_default_no_set_metadata_binaural,       /* 4  TC5 */
    test_invalid_parameters,                     /* 5  TC6 */
    test_multiple_set_metadata_before_add_group, /* 6  TC7 */
    test_binaural_non_divisor_accepted,          /* 7  TC8 */
    test_zero_samples_rejected,                  /* 8  TC9 */
    test_exceeds_frame_rejected,                 /* 9  TC10 */
    test_stereo_non_divisor_accepted,            /* 10 TC11 */
    test_stereo_varying_positions_sub_frame,     /* 11 TC12 */
};
#define NUM_TESTS (int)(sizeof(g_test_table) / sizeof(g_test_table[0]))

static const char *g_test_names[] = {
    "TC1", "TC2", "TC3", "TC4",  "TC5",  "TC6",
    "TC7", "TC8", "TC9", "TC10", "TC11", "TC12",
};

/* Run a test function in a separate child process so that a crash
 * does not prevent subsequent tests from running.
 *
 * On Linux/macOS, fork() + waitpid() is used.
 * On Windows (MSVC), _spawnl(_P_WAIT, ...) re-invokes this executable
 * with "--child <index>" to run a single test in a child process.
 * Both paths provide identical crash isolation behaviour. */
static int run_test_in_child(int test_index) {
  const char *name = g_test_names[test_index];

#ifndef _WIN32
  test_fn_t test_fn = g_test_table[test_index];
  fflush(stdout);
  fflush(stderr);

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return -1;
  }

  if (pid == 0) {
    int ret = test_fn();
    fflush(stdout);
    fflush(stderr);
    _exit(ret);
  }

  int status = 0;
  waitpid(pid, &status, 0);

  if (WIFEXITED(status)) {
    int exit_code = WEXITSTATUS(status);
    if (exit_code == 0) {
      printf("[%s] PASSED (exit 0)\n", name);
      return 0;
    } else {
      printf("[%s] FAILED (exit %d)\n", name, exit_code);
      return -1;
    }
  } else if (WIFSIGNALED(status)) {
    int sig = WTERMSIG(status);
    printf("[%s] CRASHED (signal %d: %s)\n", name, sig,
           sig == SIGABRT ? "SIGABRT" : "other");
    return -1;
  }

  printf("[%s] UNKNOWN failure\n", name);
  return -1;
#else
  /* Windows: spawn a child process that runs a single test. */
  char index_str[16];
  char exe_path[1024];
  DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
  if (len == 0 || len >= sizeof(exe_path)) {
    fprintf(stderr, "[%s] FAILED: cannot get executable path\n", name);
    return -1;
  }

  snprintf(index_str, sizeof(index_str), "%d", test_index);

  int exit_code =
      _spawnl(_P_WAIT, exe_path, exe_path, "--child", index_str, NULL);

  if (exit_code == 0) {
    printf("[%s] PASSED (exit 0)\n", name);
    return 0;
  } else if (exit_code == -1) {
    printf("[%s] FAILED: spawn error\n", name);
    return -1;
  } else {
    /* On Windows, a crash typically yields exit code 3
     * (STATUS_ACCESS_VIOLATION) or similar non-zero codes. We treat any
     * non-zero exit as failure. */
    printf("[%s] FAILED/CRASHED (exit %d)\n", name, exit_code);
    return -1;
  }
#endif
}

int main(int argc, char *argv[]) {
  /* Child-process mode: run a single test by index and return its result.
   * Used by _spawnl on Windows to achieve crash isolation. */
  if (argc >= 3 && strcmp(argv[1], "--child") == 0) {
    int index = atoi(argv[2]);
    if (index < 0 || index >= NUM_TESTS) {
      return -1;
    }
    int ret = g_test_table[index]();
    fflush(stdout);
    fflush(stderr);
    return ret;
  }

  printf("========================================\n");
  printf("Metadata Unit Processing Tests\n");
  printf("  samples_per_channel: %d\n", TEST_SAMPLES_PER_CHANNEL);
  printf("  sub_frame_samples:   %d\n", TEST_SUB_FRAME_SAMPLES);
  printf("  sampling_rate:       %d\n", TEST_SAMPLING_RATE);
  printf("========================================\n");

  int result = 0;
  int tc_results[NUM_TESTS];

  for (int i = 0; i < NUM_TESTS; i++) {
    printf("\n--- Running %s ---\n", g_test_names[i]);
    tc_results[i] = run_test_in_child(i);
    result |= (tc_results[i] != 0) ? 1 : 0;
  }

  printf("\n========================================\n");
  printf("Test Summary:\n");
  printf("  TC1 (set_metadata after add_group):   %s\n",
         tc_results[0] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC2 (set_metadata equal, no-op):     %s\n",
         tc_results[1] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC3 (stereo + set_metadata):          %s\n",
         tc_results[2] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC4 (set_metadata before add_group):  %s\n",
         tc_results[3] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC5 (default, no set_metadata):       %s\n",
         tc_results[4] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC6 (invalid parameters):             %s\n",
         tc_results[5] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC7 (multiple set_metadata):          %s\n",
         tc_results[6] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC8 (binaural non-divisor):           %s\n",
         tc_results[7] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC9 (zero samples):                  %s\n",
         tc_results[8] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC10 (samples > frame):               %s\n",
         tc_results[9] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC11 (stereo non-divisor):            %s\n",
         tc_results[10] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("  TC12 (stereo varying positions):      %s\n",
         tc_results[11] == 0 ? "PASSED" : "FAILED/CRASHED");
  printf("========================================\n");

  if (result == 0) {
    printf("\nAll tests passed.\n");
  } else {
    printf("\nSome tests failed or crashed. Review output above.\n");
  }

  return result;
}
