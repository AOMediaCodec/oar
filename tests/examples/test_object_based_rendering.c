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
 * @file test_object_based_rendering.c
 * @brief Test object-based audio element rendering with various configurations.
 *
 * Test matrix:
 *   TC1: Single object at 30° azimuth, 440 Hz — render and verify non-silent.
 *   TC2: Two objects in one element (dual-mono) at ±45° — render and verify.
 *   TC3: Two single-object elements in separate groups — render and verify.
 *   TC4: Animated object positions (−45° → +45°) — render and verify.
 *   TC5: Object with gain metadata (−6 dB) — render and verify.
 *   TC6: Invalid parameters (NULL metadata, non-existent element) — expect
 *        errors.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "animation.h"
#include "oar.h"
#include "oar_base.h"
#include "oar_metadata.h"
#include "test_framework.h"
#include "test_helpers.h"

/* --- Local helpers ------------------------------------------------------ */

/* Create animated object metadata (heap-allocated, caller must free). */
static oar_metadata_t *create_animated_object_metadata(
    const polar_t *start_positions, const polar_t *end_positions,
    uint32_t num_objects, uint32_t samples_per_channel) {
  if (num_objects == 0 || num_objects > def_max_number_of_objects ||
      !start_positions || !end_positions) {
    return NULL;
  }

  oar_metadata_t *metadata = (oar_metadata_t *)malloc(sizeof(oar_metadata_t));
  if (!metadata) return NULL;

  memset(metadata, 0, sizeof(*metadata));
  metadata->type = ck_metadata_object_positions;
  metadata->duration = (int)samples_per_channel;
  metadata->object_positions.param_type = ck_param_animated;
  metadata->object_positions.position_type = ck_polar;
  metadata->object_positions.num_objects = num_objects;

  for (uint32_t i = 0; i < num_objects; ++i) {
    metadata->object_positions.animated_polar_positions[i].animation_type =
        ck_animation_type_linear;
    metadata->object_positions.animated_polar_positions[i].azimuth =
        def_animated_data_linear_instance(animated_data_float32_t,
                                          start_positions[i].azimuth,
                                          end_positions[i].azimuth);
    metadata->object_positions.animated_polar_positions[i].elevation =
        def_animated_data_linear_instance(animated_data_float32_t,
                                          start_positions[i].elevation,
                                          end_positions[i].elevation);
    metadata->object_positions.animated_polar_positions[i].distance =
        def_animated_data_linear_instance(animated_data_float32_t,
                                          start_positions[i].distance,
                                          end_positions[i].distance);
  }

  return metadata;
}

/* Create gain metadata (heap-allocated, caller must free).
 * gain_value should be specified in decibels (dB). */
static oar_metadata_t *create_gain_metadata(uint32_t gain_id,
                                            float gain_value_db,
                                            uint32_t samples_per_channel) {
  oar_metadata_t *metadata = (oar_metadata_t *)malloc(sizeof(oar_metadata_t));
  if (!metadata) return NULL;

  memset(metadata, 0, sizeof(*metadata));
  metadata->type = ck_metadata_gain;
  metadata->duration = (int)samples_per_channel;
  metadata->gain.id = gain_id;
  metadata->gain.param_type = ck_param_constant;
  metadata->gain.constant_gain = gain_value_db;

  return metadata;
}

/* Add an object element, feed sine data, and attach position metadata.
 * Returns 0 on success, -1 on failure. */
static int add_object_element_with_data(oar_t *oar, uint32_t element_id,
                                        int num_objects,
                                        const polar_t *positions,
                                        const float *frequencies) {
  int gid = oar_add_audio_group(oar);
  if (gid < 0) return -1;

  oar_audio_element_config_t element_cfg =
      create_object_element_config(num_objects);
  int ret = oar_add_audio_element(oar, gid, element_id, &element_cfg);
  if (ret != 0) return -1;

  uint32_t input_channels =
      oar_get_number_of_audio_element_channels(oar, element_id);
  uint32_t samples_per_channel = oar_get_samples_per_channel(oar);

  oar_audio_block_t input_data;
  if (alloc_audio_block(input_channels, samples_per_channel, &input_data) != 0)
    return -1;

  for (int i = 0; i < num_objects; ++i) {
    generate_sine(input_data.data + (uint32_t)i * samples_per_channel,
                  samples_per_channel, frequencies[i], 48000.0f);
  }

  ret = oar_update_audio_element_data(oar, element_id, &input_data);
  free(input_data.data);
  if (ret != 0) return -1;

  oar_metadata_t *metadata = create_object_metadata(
      positions, (uint32_t)num_objects, samples_per_channel);
  if (!metadata) return -1;

  ret = oar_update_audio_element_metadata(oar, element_id, metadata);
  free(metadata);
  return ret;
}

/* --- Test cases --------------------------------------------------------- */

/* TC1: Single object at 30° azimuth, 440 Hz */
static int test_single_object(void) {
  TEST_START("TC1: single object rendering");

  oar_config_t oar_cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&oar_cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  polar_t position = {30.0f, 10.0f, 1.0f};
  float frequency = 440.0f;
  TEST_ASSERT(
      add_object_element_with_data(oar, 1, 1, &position, &frequency) == 0,
      "failed to add object element with data");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  uint32_t spc = oar_get_samples_per_channel(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0,
              "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "render output is silent");
  return TEST_PASS;
}

/* TC2: Two objects in one element (dual-mono) at ±45° */
static int test_two_objects_one_element(void) {
  TEST_START("TC2: two objects in one element");

  oar_config_t oar_cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&oar_cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  polar_t positions[2] = {{-45.0f, 0.0f, 1.0f}, {45.0f, 0.0f, 1.0f}};
  float frequencies[2] = {660.0f, 880.0f};
  TEST_ASSERT(
      add_object_element_with_data(oar, 2, 2, positions, frequencies) == 0,
      "failed to add dual-object element with data");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  uint32_t spc = oar_get_samples_per_channel(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0,
              "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "render output is silent");
  return TEST_PASS;
}

/* TC3: Two single-object elements in separate groups */
static int test_two_objects_separate_elements(void) {
  TEST_START("TC3: two objects in separate elements");

  oar_config_t oar_cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&oar_cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  /* Element 1: 660 Hz at -45° */
  polar_t pos1 = {-45.0f, 0.0f, 1.0f};
  float freq1 = 660.0f;
  TEST_ASSERT(add_object_element_with_data(oar, 3, 1, &pos1, &freq1) == 0,
              "failed to add element 1");

  /* Element 2: 880 Hz at +45° */
  polar_t pos2 = {45.0f, 0.0f, 1.0f};
  float freq2 = 880.0f;
  TEST_ASSERT(add_object_element_with_data(oar, 4, 1, &pos2, &freq2) == 0,
              "failed to add element 2");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  uint32_t spc = oar_get_samples_per_channel(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0,
              "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "render output is silent");
  return TEST_PASS;
}

/* TC4: Animated object positions (−45° → +45°) */
static int test_animated_object(void) {
  TEST_START("TC4: animated object positions");

  oar_config_t oar_cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&oar_cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t element_cfg = create_object_element_config(1);
  int ret = oar_add_audio_element(oar, gid, 5, &element_cfg);
  TEST_ASSERT(ret == 0, "oar_add_audio_element failed");

  uint32_t spc = oar_get_samples_per_channel(oar);

  /* Feed sine data */
  oar_audio_block_t input_data;
  TEST_ASSERT(alloc_audio_block(1, spc, &input_data) == 0,
              "alloc_audio_block failed");
  generate_sine(input_data.data, spc, 440.0f, 48000.0f);
  ret = oar_update_audio_element_data(oar, 5, &input_data);
  free(input_data.data);
  TEST_ASSERT(ret == 0, "oar_update_audio_element_data failed");

  /* Attach animated metadata */
  polar_t start_pos = {-45.0f, 0.0f, 1.0f};
  polar_t end_pos = {45.0f, 0.0f, 1.0f};
  oar_metadata_t *metadata =
      create_animated_object_metadata(&start_pos, &end_pos, 1, spc);
  TEST_ASSERT(metadata != NULL, "create_animated_object_metadata failed");
  ret = oar_update_audio_element_metadata(oar, 5, metadata);
  free(metadata);
  TEST_ASSERT(ret == 0, "oar_update_audio_element_metadata failed");

  /* Render and verify */
  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0,
              "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "render output is silent");
  return TEST_PASS;
}

/* TC5: Object with gain metadata (−6 dB) */
static int test_object_with_gain(void) {
  TEST_START("TC5: object with gain");

  oar_config_t oar_cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&oar_cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t element_cfg = create_object_element_config(1);
  int ret = oar_add_audio_element(oar, gid, 6, &element_cfg);
  TEST_ASSERT(ret == 0, "oar_add_audio_element failed");

  uint32_t spc = oar_get_samples_per_channel(oar);

  /* Feed sine data */
  oar_audio_block_t input_data;
  TEST_ASSERT(alloc_audio_block(1, spc, &input_data) == 0,
              "alloc_audio_block failed");
  generate_sine(input_data.data, spc, 440.0f, 48000.0f);
  ret = oar_update_audio_element_data(oar, 6, &input_data);
  free(input_data.data);
  TEST_ASSERT(ret == 0, "oar_update_audio_element_data failed");

  /* Set position metadata */
  polar_t position = {0.0f, 0.0f, 1.0f};
  oar_metadata_t *pos_metadata = create_object_metadata(&position, 1, spc);
  TEST_ASSERT(pos_metadata != NULL, "create_object_metadata failed");
  ret = oar_update_audio_element_metadata(oar, 6, pos_metadata);
  free(pos_metadata);
  TEST_ASSERT(ret == 0, "position metadata update failed");

  /* Apply gain (−6.0 dB) */
  oar_metadata_t *gain_metadata = create_gain_metadata(1, -6.0f, spc);
  TEST_ASSERT(gain_metadata != NULL, "create_gain_metadata failed");
  ret = oar_update_audio_element_metadata(oar, 6, gain_metadata);
  free(gain_metadata);
  TEST_ASSERT(ret == 0, "gain metadata update failed");

  /* Render and verify */
  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0,
              "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "render output is silent");
  return TEST_PASS;
}

/* TC6: Invalid parameters — NULL metadata and non-existent element ID */
static int test_invalid_parameters(void) {
  TEST_START("TC6: invalid parameter handling");

  oar_config_t oar_cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&oar_cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  /* NULL metadata should be rejected */
  int ret = oar_update_audio_element_metadata(oar, 99, NULL);
  TEST_ASSERT(ret != 0, "NULL metadata was accepted");

  /* Non-existent element ID should be rejected */
  oar_metadata_t dummy_metadata;
  memset(&dummy_metadata, 0, sizeof(dummy_metadata));
  dummy_metadata.type = ck_metadata_object_positions;
  dummy_metadata.duration = 256;
  ret = oar_update_audio_element_metadata(oar, 999, &dummy_metadata);
  TEST_ASSERT(ret != 0, "non-existent element ID was accepted");

  oar_destroy(oar);
  return TEST_PASS;
}

/* --- Test table --------------------------------------------------------- */

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "single object", test_single_object),
    TEST_ENTRY("TC2", "two objects in one element",
               test_two_objects_one_element),
    TEST_ENTRY("TC3", "two objects in separate elements",
               test_two_objects_separate_elements),
    TEST_ENTRY("TC4", "animated object positions", test_animated_object),
    TEST_ENTRY("TC5", "object with gain", test_object_with_gain),
    TEST_ENTRY("TC6", "invalid parameters", test_invalid_parameters),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
