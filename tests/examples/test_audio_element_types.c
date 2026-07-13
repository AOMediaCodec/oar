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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include the OAR header
#include "oar.h"

// Helper function to create a basic OAR config
oar_config_t create_default_oar_config() {
  oar_config_t config;
  config.target_layout = ck_oar_layout_stereo;
  config.samples_per_channel = 1024;
  config.sampling_rate = 48000;
  return config;
}

// Test case for ck_channel_based audio element type
void test_channel_based_element() {
  printf("Testing ck_channel_based audio element...\n");

  oar_config_t config = create_default_oar_config();
  oar_t *oar = oar_create(&config);
  if (oar == NULL) {
    fprintf(stderr, "Error: Failed to create OAR object\n");
    return;
  }

  // Add an audio group first
  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr, "Error: Failed to add audio group, error code: %d\n",
            group_id);
    oar_destroy(oar);
    return;
  }
  printf("Audio group (ID: %d) added successfully.\n", group_id);

  oar_audio_element_config_t element_config;
  element_config.type = ck_channel_based;
  element_config.cbc.layout = ck_oar_layout_stereo;
  memset(&element_config.parameters, 0, sizeof(parameter_set_t));

  int ret = oar_add_audio_element(oar, group_id, 1, &element_config);
  printf("oar_add_audio_element returned: %d\n", ret);
  if (ret != 0) {
    fprintf(stderr,
            "Error: Failed to add audio element, expected 0 but got %d\n", ret);
    oar_destroy(oar);
    return;
  }

  uint32_t num_channels = oar_get_number_of_audio_element_channels(oar, 1);
  if (num_channels != 2) {  // Stereo layout has 2 channels
    fprintf(stderr, "Error: Expected 2 channels but got %u\n", num_channels);
    oar_destroy(oar);
    return;
  }

  oar_destroy(oar);
  printf("ck_channel_based test passed.\n\n");
}

// Test case for ck_scene_based audio element type
void test_scene_based_element() {
  printf("Testing ck_scene_based audio element...\n");

  oar_config_t config = create_default_oar_config();
  oar_t *oar = oar_create(&config);
  if (oar == NULL) {
    fprintf(stderr, "Error: Failed to create OAR object\n");
    return;
  }

  // Add an audio group first
  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr, "Error: Failed to add audio group, error code: %d\n",
            group_id);
    oar_destroy(oar);
    return;
  }
  printf("Audio group (ID: %d) added successfully.\n", group_id);

  oar_audio_element_config_t element_config;
  element_config.type = ck_scene_based;
  element_config.sbc.order = ck_oar_1oa;
  memset(&element_config.parameters, 0, sizeof(parameter_set_t));

  int ret = oar_add_audio_element(oar, group_id, 2, &element_config);
  if (ret != 0) {
    fprintf(stderr,
            "Error: Failed to add audio element, expected 0 but got %d\n", ret);
    oar_destroy(oar);
    return;
  }

  // For 1st Order Ambisonics (1oa), we expect 4 channels (W, X, Y, Z)
  uint32_t num_channels = oar_get_number_of_audio_element_channels(oar, 2);
  if (num_channels != 4) {
    fprintf(stderr, "Error: Expected 4 channels but got %u\n", num_channels);
    oar_destroy(oar);
    return;
  }

  oar_destroy(oar);
  printf("ck_scene_based test passed.\n\n");
}

// Test case for ck_object_based audio element type
void test_object_based_element() {
  printf("Testing ck_object_based audio element...\n");

  oar_config_t config = create_default_oar_config();
  oar_t *oar = oar_create(&config);
  if (oar == NULL) {
    fprintf(stderr, "Error: Failed to create OAR object\n");
    return;
  }

  // Add an audio group first
  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr, "Error: Failed to add audio group, error code: %d\n",
            group_id);
    oar_destroy(oar);
    return;
  }
  printf("Audio group (ID: %d) added successfully.\n", group_id);

  oar_audio_element_config_t element_config;
  element_config.type = ck_object_based;
  element_config.obc.num_objects = 1;  // Mono
  memset(&element_config.parameters, 0, sizeof(parameter_set_t));

  int ret = oar_add_audio_element(oar, group_id, 3, &element_config);
  if (ret != 0) {
    fprintf(stderr,
            "Error: Failed to add audio element, expected 0 but got %d\n", ret);
    oar_destroy(oar);
    return;
  }

  // Object based is currently mono
  uint32_t num_channels = oar_get_number_of_audio_element_channels(oar, 3);
  if (num_channels != 1) {
    fprintf(stderr, "Error: Expected 1 channel but got %u\n", num_channels);
    oar_destroy(oar);
    return;
  }

  oar_destroy(oar);
  printf("ck_object_based test passed.\n\n");
}

// Test case for adding multiple elements of different types
void test_multiple_element_types() {
  printf("Testing multiple audio element types...\n");
  // This test will focus on channel and scene based.

  oar_config_t config = create_default_oar_config();
  oar_t *oar = oar_create(&config);
  if (oar == NULL) {
    fprintf(stderr, "Error: Failed to create OAR object\n");
    return;
  }

  // Add channel-based element
  oar_audio_element_config_t channel_config;
  channel_config.type = ck_channel_based;
  channel_config.cbc.layout = ck_oar_layout_mono;
  memset(&channel_config.parameters, 0, sizeof(parameter_set_t));

  int group_id1 = oar_add_audio_group(oar);
  if (group_id1 < 0) {
    fprintf(stderr, "Error: Failed to add audio group 1, error code: %d\n",
            group_id1);
    oar_destroy(oar);
    return;
  }
  printf("Audio group (ID: %d) added for channel-based element.\n", group_id1);

  int ret = oar_add_audio_element(oar, group_id1, 1, &channel_config);
  if (ret != 0) {
    fprintf(
        stderr,
        "Error: Failed to add channel audio element, expected 0 but got %d\n",
        ret);
    oar_destroy(oar);
    return;
  }
  if (oar_get_number_of_audio_element_channels(oar, 1) != 1) {
    fprintf(stderr,
            "Error: Expected 1 channel for channel-based element but got %u\n",
            oar_get_number_of_audio_element_channels(oar, 1));
    oar_destroy(oar);
    return;
  }

  // Add scene-based element
  oar_audio_element_config_t scene_config;
  scene_config.type = ck_scene_based;
  scene_config.sbc.order = ck_oar_zoa;  // Zero Order Ambisonics (mono)
  memset(&scene_config.parameters, 0, sizeof(parameter_set_t));

  int group_id2 = oar_add_audio_group(oar);
  if (group_id2 < 0) {
    fprintf(stderr, "Error: Failed to add audio group 2, error code: %d\n",
            group_id2);
    oar_destroy(oar);
    return;
  }
  printf("Audio group (ID: %d) added for scene-based element.\n", group_id2);

  ret = oar_add_audio_element(oar, group_id2, 2, &scene_config);
  if (ret != 0) {
    fprintf(stderr,
            "Error: Failed to add scene audio element, expected 0 but got %d\n",
            ret);
    oar_destroy(oar);
    return;
  }
  if (oar_get_number_of_audio_element_channels(oar, 2) !=
      1) {  // ZOA is 1 channel
    fprintf(stderr,
            "Error: Expected 1 channel for scene-based element but got %u\n",
            oar_get_number_of_audio_element_channels(oar, 2));
    oar_destroy(oar);
    return;
  }

  uint32_t num_elements = oar_get_number_of_audio_elements(oar);
  if (num_elements != 2) {
    fprintf(stderr, "Error: Expected 2 elements but got %u\n", num_elements);
    oar_destroy(oar);
    return;
  }

  oar_destroy(oar);
  printf("Multiple element types test passed.\n\n");
}

int main() {
  printf("Running audio_element_type_t tests...\n\n");

  test_channel_based_element();
  test_scene_based_element();
  test_object_based_element();
  test_multiple_element_types();

  printf("All audio_element_type_t tests completed successfully.\n");
  return 0;
}
