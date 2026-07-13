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


#include <math.h>  // For sine wave generation
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Include the OAR header
#include "oar.h"

// Helper function to create a basic OAR config
oar_config_t create_default_oar_config() {
  oar_config_t config;
  config.target_layout = ck_oar_layout_stereo;  // Render to stereo
  config.samples_per_channel = 256;             // Smaller block for testing
  config.sampling_rate = 48000;
  return config;
}

// Helper function to generate Ambisonics data (e.g., 1st Order)
// For 1OA, channels are W, Y, Z, X (ACN channel ordering)
void generate_ambisonics_data(float* buffer, uint32_t samples, uint32_t order,
                              float sample_rate) {
  uint32_t num_channels = (order + 1) * (order + 1);
  float freq_w = 220.0;  // Frequency for W channel (omnidirectional)
  float freq_y = 330.0;  // Frequency for Y channel (front-back)
  float freq_z = 440.0;  // Frequency for Z channel (up-down)
  float freq_x = 550.0;  // Frequency for X channel (left-right)

  for (uint32_t i = 0; i < samples; ++i) {
    float t = (float)i / sample_rate;
    if (num_channels > 0)
      buffer[0 * samples + i] = (float)sin(2.0 * M_PI * freq_w * t);  // W
    if (num_channels > 1)
      buffer[1 * samples + i] = (float)sin(2.0 * M_PI * freq_y * t);  // Y
    if (num_channels > 2)
      buffer[2 * samples + i] = (float)sin(2.0 * M_PI * freq_z * t);  // Z
    if (num_channels > 3)
      buffer[3 * samples + i] = (float)sin(2.0 * M_PI * freq_x * t);  // X
    // Higher orders would require more complex generation following Ambisonics
    // principles
  }
}

int main() {
  printf("Running scene-based audio element rendering test...\n\n");

  oar_config_t oar_cfg = create_default_oar_config();
  oar_t* oar = oar_create(&oar_cfg);
  if (oar == NULL) {
    fprintf(stderr, "Failed to create OAR object.\n");
    return -1;
  }
  printf("OAR object created successfully.\n");

  // Configure a scene-based audio element (e.g., 1st Order Ambisonics)
  oar_audio_element_config_t element_cfg;
  element_cfg.type = ck_scene_based;
  element_cfg.sbc.order = ck_oar_1oa;  // Use 1st Order Ambisonics

  memset(&element_cfg.parameters, 0, sizeof(parameter_set_t));

  // Add an audio group first
  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr, "Failed to add audio group. Error code: %d\n", group_id);
    oar_destroy(oar);
    return -1;
  }
  printf("Audio group (ID: %d) added successfully.\n", group_id);

  uint32_t element_id = 1;
  int ret = oar_add_audio_element(oar, group_id, element_id, &element_cfg);
  if (ret != 0) {
    fprintf(stderr, "Failed to add audio element. Error code: %d\n", ret);
    oar_destroy(oar);
    return -1;
  }
  printf(
      "Scene-based audio element (ID: %d, Order: 1OA) added to group %d "
      "successfully.\n",
      element_id, group_id);

  uint32_t input_channels =
      oar_get_number_of_audio_element_channels(oar, element_id);
  uint32_t output_channels = oar_get_number_of_output_channels(oar);
  uint32_t samples_per_channel = oar_get_samples_per_channel(oar);

  printf(
      "Input channels (HOA): %u, Output channels: %u, Samples per channel: "
      "%u\n",
      input_channels, output_channels, samples_per_channel);
  if (input_channels != 4) {  // 1OA should have 4 channels
    fprintf(stderr, "Error: Expected 4 input channels for 1OA, got %u.\n",
            input_channels);
    oar_destroy(oar);
    return -1;
  }

  // Prepare input audio data (planar format)
  oar_audio_block_t input_data;
  input_data.channels = input_channels;
  input_data.samples_per_channel = samples_per_channel;
  input_data.data =
      (float*)malloc(input_channels * samples_per_channel * sizeof(float));
  if (input_data.data == NULL) {
    fprintf(stderr, "Failed to allocate memory for input audio data.\n");
    oar_destroy(oar);
    return -1;
  }

  // Generate 1st Order Ambisonics data
  generate_ambisonics_data(input_data.data, samples_per_channel,
                           element_cfg.sbc.order, (float)oar_cfg.sampling_rate);
  printf("Generated %u samples of 1st Order Ambisonics data.\n",
         samples_per_channel);

  // Update the audio element with the generated data
  ret = oar_update_audio_element_data(oar, element_id, &input_data);
  if (ret != 0) {
    fprintf(stderr, "Failed to update audio element data. Error code: %d\n",
            ret);
    free(input_data.data);
    oar_destroy(oar);
    return -1;
  }
  printf("Audio element data updated successfully.\n");

  // Prepare output audio data (planar format)
  oar_audio_block_t output_data;
  output_data.channels = output_channels;  // Should be stereo as per oar_config
  output_data.samples_per_channel = samples_per_channel;
  output_data.data =
      (float*)malloc(output_channels * samples_per_channel * sizeof(float));
  if (output_data.data == NULL) {
    fprintf(stderr, "Failed to allocate memory for output audio data.\n");
    free(input_data.data);
    oar_destroy(oar);
    return -1;
  }
  // Initialize output to silence
  memset(output_data.data, 0,
         output_channels * samples_per_channel * sizeof(float));

  // Perform rendering
  printf("Rendering audio...\n");
  ret = oar_render(oar, &output_data);
  if (ret != 0) {
    fprintf(stderr, "Failed to render audio. Error code: %d\n", ret);
    free(input_data.data);
    free(output_data.data);
    oar_destroy(oar);
    return -1;
  }
  printf("Audio rendered successfully to %u channels.\n", output_data.channels);

  // Here, one would typically save the output_data to a file or play it.
  // For this test, we'll just verify that the output is not all silence.
  int is_silent = 1;
  for (uint32_t i = 0; i < output_channels * samples_per_channel; ++i) {
    if (fabs(output_data.data[i]) > 1e-9) {  // Check if not close to zero
      is_silent = 0;
      break;
    }
  }

  if (is_silent) {
    printf("Warning: Rendered output is silent.\n");
  } else {
    printf("Rendered output contains audio.\n");
  }

  // Print a few samples from the first output channel for inspection
  printf("First 5 samples of output channel 0:\n");
  for (int i = 0; i < 5; ++i) {
    printf("  %f\n", output_data.data[i]);
  }

  // Cleanup
  free(input_data.data);
  free(output_data.data);
  oar_destroy(oar);

  printf("\nScene-based rendering test completed.\n");
  return 0;
}
