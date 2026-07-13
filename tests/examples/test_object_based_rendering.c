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

// Helper function to generate a simple sine wave for a given channel
void generate_sine_wave(float* buffer, uint32_t samples, float frequency,
                        float sample_rate) {
  for (uint32_t i = 0; i < samples; ++i) {
    buffer[i] = (float)sin(2.0 * M_PI * frequency * ((float)i / sample_rate));
  }
}

// Helper function to create object metadata for multiple objects
oar_metadata_t* create_object_metadata(const polar_t* positions,
                                       uint32_t num_objects,
                                       uint32_t samples_per_channel) {
  if (num_objects == 0 || num_objects > def_max_number_of_objects ||
      !positions) {
    return NULL;
  }

  oar_metadata_t* metadata = (oar_metadata_t*)malloc(sizeof(oar_metadata_t));
  if (!metadata) return NULL;

  metadata->type = ck_metadata_object_positions;
  metadata->duration = (int)samples_per_channel;  // Valid for the current frame

  metadata->object_positions.param_type = ck_param_constant;
  metadata->object_positions.position_type = ck_polar;
  metadata->object_positions.num_objects = num_objects;

  for (uint32_t i = 0; i < num_objects; ++i) {
    metadata->object_positions.polar_positions[i].azimuth =
        positions[i].azimuth;
    metadata->object_positions.polar_positions[i].elevation =
        positions[i].elevation;
    metadata->object_positions.polar_positions[i].distance =
        positions[i].distance;
  }

  return metadata;
}

// Helper function to create animated object metadata
oar_metadata_t* create_animated_object_metadata(const polar_t* start_positions,
                                                const polar_t* end_positions,
                                                uint32_t num_objects,
                                                uint32_t samples_per_channel) {
  if (num_objects == 0 || num_objects > def_max_number_of_objects ||
      !start_positions || !end_positions) {
    return NULL;
  }

  oar_metadata_t* metadata = (oar_metadata_t*)malloc(sizeof(oar_metadata_t));
  if (!metadata) return NULL;

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

// Helper function to create gain metadata
// Note: gain_value should be specified in decibels (dB)
oar_metadata_t* create_gain_metadata(uint32_t gain_id, float gain_value_db,
                                     uint32_t samples_per_channel) {
  oar_metadata_t* metadata = (oar_metadata_t*)malloc(sizeof(oar_metadata_t));
  if (!metadata) return NULL;

  metadata->type = ck_metadata_gain;
  metadata->duration = (int)samples_per_channel;
  metadata->gain.id = gain_id;
  metadata->gain.param_type = ck_param_constant;
  metadata->gain.constant_gain = gain_value_db;

  return metadata;
}

// Internal helper to add a single or dual object audio element, configure data
// and metadata. Returns 0 on success, -1 on failure.
static int _add_object_element(oar_t* oar, uint32_t element_id, int num_objects,
                               const char* element_description_tag,
                               uint32_t num_objects_to_configure,
                               const polar_t* positions,
                               const float* frequencies) {
  printf("\n--- Adding %s (ID: %d, num_objects: %d) ---\n",
         element_description_tag, element_id, num_objects);

  // Add an audio group first
  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr, "Failed to add audio group for %s. Error code: %d\n",
            element_description_tag, group_id);
    return -1;
  }
  printf("Audio group (ID: %d) added for %s.\n", group_id,
         element_description_tag);

  oar_audio_element_config_t element_cfg;
  element_cfg.type = ck_object_based;
  element_cfg.obc.num_objects = num_objects;
  memset(&element_cfg.parameters, 0, sizeof(parameter_set_t));

  int ret = oar_add_audio_element(oar, group_id, element_id, &element_cfg);
  if (ret != 0) {
    fprintf(stderr, "Failed to add %s audio element. Error code: %d\n",
            element_description_tag, ret);
    return -1;
  }
  printf("%s audio element (ID: %d) added successfully to group %d.\n",
         element_description_tag, element_id, group_id);

  uint32_t input_channels =
      oar_get_number_of_audio_element_channels(oar, element_id);
  uint32_t samples_per_channel = oar_get_samples_per_channel(oar);

  printf("Input channels for %s: %u, Samples per channel: %u\n",
         element_description_tag, input_channels, samples_per_channel);

  if (num_objects == 1) {
    if (input_channels != 1) {
      fprintf(stderr,
              "Error: Expected 1 input channel for 1 object, but got %u\n",
              input_channels);
      oar_remove_audio_element(oar, element_id);
      return -1;
    }
  } else if (num_objects == 2) {
    if (input_channels != 2) {
      fprintf(stderr,
              "Error: Expected 2 input channels for 2 objects, but got %u\n",
              input_channels);
      oar_remove_audio_element(oar, element_id);
      return -1;
    }
  } else {
    fprintf(stderr, "Error: Unsupported number of objects: %d\n", num_objects);
    oar_remove_audio_element(oar, element_id);
    return -1;
  }

  if (input_channels == 0 || num_objects_to_configure > input_channels) {
    fprintf(stderr, "Channel/object mismatch for %s. Aborting.\n",
            element_description_tag);
    oar_remove_audio_element(oar, element_id);
    return -1;
  }

  oar_audio_block_t input_data;
  input_data.channels = input_channels;
  input_data.samples_per_channel = samples_per_channel;
  input_data.data =
      (float*)malloc(input_channels * samples_per_channel * sizeof(float));
  if (input_data.data == NULL) {
    fprintf(stderr, "Failed to allocate memory for %s input data.\n",
            element_description_tag);
    oar_remove_audio_element(oar, element_id);
    return -1;
  }

  for (uint32_t i = 0; i < num_objects_to_configure; ++i) {
    generate_sine_wave(input_data.data + i * samples_per_channel,
                       samples_per_channel, frequencies[i], 48000.0);
    printf("Generated %u samples of %.0fHz sine wave for object %u of %s.\n",
           samples_per_channel, frequencies[i], i + 1, element_description_tag);
  }

  ret = oar_update_audio_element_data(oar, element_id, &input_data);
  if (ret != 0) {
    fprintf(stderr, "Failed to update %s data. Error code: %d\n",
            element_description_tag, ret);
    free(input_data.data);
    oar_remove_audio_element(oar, element_id);
    return -1;
  }

  oar_metadata_t* metadata = create_object_metadata(
      positions, num_objects_to_configure, samples_per_channel);
  if (metadata) {
    ret = oar_update_audio_element_metadata(oar, element_id, metadata);
    if (ret != 0) {
      fprintf(stderr, "Failed to update %s metadata. Error code: %d\n",
              element_description_tag, ret);
    } else {
      printf("%s metadata updated for %u object(s).\n", element_description_tag,
             num_objects_to_configure);
      for (uint32_t i = 0; i < num_objects_to_configure; ++i) {
        printf("  Object %u: Azimuth: %.1f, Elevation: %.1f, Distance: %.1f\n",
               i + 1, positions[i].azimuth, positions[i].elevation,
               positions[i].distance);
      }
    }
    free(metadata);
  } else {
    fprintf(stderr, "Failed to create metadata for %s.\n",
            element_description_tag);
    free(input_data.data);
    oar_remove_audio_element(oar, element_id);
    return -1;
  }

  free(input_data.data);
  return 0;
}

// Internal helper to render audio and perform basic validation.
// Returns 0 on success, -1 on failure. Output block must be freed by caller.
static int _render_audio(oar_t* oar, oar_audio_block_t* output_block,
                         const char* scenario_name_tag) {
  printf("\n--- Rendering audio for %s ---\n", scenario_name_tag);
  if (!oar || !output_block) {
    fprintf(stderr, "Invalid arguments to _render_audio.\n");
    return -1;
  }

  output_block->channels = oar_get_number_of_output_channels(oar);
  output_block->samples_per_channel = oar_get_samples_per_channel(oar);
  printf("Output channels: %u, Samples per channel: %u\n",
         output_block->channels, output_block->samples_per_channel);

  output_block->data =
      (float*)malloc(output_block->channels *
                     output_block->samples_per_channel * sizeof(float));
  if (output_block->data == NULL) {
    fprintf(stderr, "Failed to allocate memory for output audio data (%s).\n",
            scenario_name_tag);
    return -1;
  }
  memset(output_block->data, 0,
         output_block->channels * output_block->samples_per_channel *
             sizeof(float));

  int ret = oar_render(oar, output_block);
  if (ret != 0) {
    fprintf(stderr, "Failed to render audio for %s. Error code: %d\n",
            scenario_name_tag, ret);
    free(output_block->data);   // Free allocated data before returning error
    output_block->data = NULL;  // Mark as freed
    return -1;
  }
  printf("Audio for %s rendered successfully to %u channels.\n",
         scenario_name_tag, output_block->channels);

  int is_silent = 1;
  for (uint32_t i = 0;
       i < output_block->channels * output_block->samples_per_channel; ++i) {
    if (fabs(output_block->data[i]) > 1e-9) {
      is_silent = 0;
      break;
    }
  }
  if (is_silent) {
    printf("Warning: Rendered output for %s is silent.\n", scenario_name_tag);
  } else {
    printf("Rendered output for %s contains audio.\n", scenario_name_tag);
  }
  return 0;
}

// Internal helper to print first few samples of an audio block.
static void _print_audio_block_samples(const oar_audio_block_t* block,
                                       const char* block_name_tag) {
  printf("\n--- First 5 samples for %s ---\n", block_name_tag);
  if (!block || !block->data) {
    printf("Block is NULL or has no data.\n");
    return;
  }
  for (uint32_t ch = 0; ch < block->channels; ++ch) {
    printf("Channel %u:\n", ch);
    for (int i = 0; i < 5; ++i) {
      printf("  %f\n", block->data[ch * block->samples_per_channel + i]);
    }
  }
}

void run_single_object_test(oar_t* oar) {
  printf("\n--- Case 1: Rendering a single object ---\n");
  polar_t position = {30.0, 10.0, 1.0};
  float frequency = 440.0f;
  if (_add_object_element(oar, 1, 1, "Single Object", 1, &position,
                          &frequency) != 0) {
    fprintf(stderr, "Failed to set up single object test case.\n");
  }
}

void run_two_objects_test(oar_t* oar) {
  printf(
      "\n--- Case 2: Rendering a dual-mono object element (two objects) ---\n");
  polar_t positions[2] = {{-45.0, 0.0, 1.0}, {45.0, 0.0, 1.0}};
  float frequencies[2] = {660.0f, 880.0f};
  if (_add_object_element(oar, 2, 2, "Dual-Mono Object Element", 2, positions,
                          frequencies) != 0) {
    fprintf(stderr, "Failed to set up dual-mono object test case.\n");
  }
}

void run_two_single_objects_in_separate_elements_test(oar_t* oar) {
  printf(
      "\n--- Case 3: Rendering two single objects in separate elements ---\n");

  // --- Object 1 (660Hz, Azimuth -45.0) ---
  polar_t position_1 = {-45.0, 0.0, 1.0};
  float frequency_1 = 660.0f;
  if (_add_object_element(oar, 3, 1, "Single Object 1 (of two separate)", 1,
                          &position_1, &frequency_1) != 0) {
    fprintf(
        stderr,
        "Failed to set up first single object for separate elements test.\n");
    // No need to return here, the helper should clean up. If it failed, we just
    // try the next or let main fail.
  }

  // --- Object 2 (880Hz, Azimuth 45.0) ---
  polar_t position_2 = {45.0, 0.0, 1.0};
  float frequency_2 = 880.0f;
  if (_add_object_element(oar, 4, 1, "Single Object 2 (of two separate)", 1,
                          &position_2, &frequency_2) != 0) {
    fprintf(
        stderr,
        "Failed to set up second single object for separate elements test.\n");
  }
}

// Test edge case: animated object positions
void run_animated_object_test(oar_t* oar) {
  printf("\n--- Case 4: Rendering animated object positions ---\n");

  // Add an audio group first
  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr,
            "Failed to add audio group for animated test. Error code: %d\n",
            group_id);
    return;
  }

  oar_audio_element_config_t element_cfg;
  element_cfg.type = ck_object_based;
  element_cfg.obc.num_objects = 1;
  memset(&element_cfg.parameters, 0, sizeof(parameter_set_t));

  int ret = oar_add_audio_element(oar, group_id, 5, &element_cfg);
  if (ret != 0) {
    fprintf(stderr,
            "Failed to add animated object audio element. Error code: %d\n",
            ret);
    return;
  }

  uint32_t samples_per_channel = oar_get_samples_per_channel(oar);
  oar_audio_block_t input_data;
  input_data.channels = 1;
  input_data.samples_per_channel = samples_per_channel;
  input_data.data = (float*)malloc(samples_per_channel * sizeof(float));

  if (!input_data.data) {
    fprintf(stderr,
            "Failed to allocate memory for animated test input data.\n");
    return;
  }

  generate_sine_wave(input_data.data, samples_per_channel, 440.0f, 48000.0);

  ret = oar_update_audio_element_data(oar, 5, &input_data);
  if (ret != 0) {
    fprintf(stderr, "Failed to update animated test data. Error code: %d\n",
            ret);
    free(input_data.data);
    return;
  }

  // Create animated metadata: object moves from -45° to +45° azimuth
  polar_t start_pos = {-45.0, 0.0, 1.0};
  polar_t end_pos = {45.0, 0.0, 1.0};
  oar_metadata_t* metadata = create_animated_object_metadata(
      &start_pos, &end_pos, 1, samples_per_channel);

  if (metadata) {
    ret = oar_update_audio_element_metadata(oar, 5, metadata);
    if (ret != 0) {
      fprintf(stderr, "Failed to update animated metadata. Error code: %d\n",
              ret);
    } else {
      printf(
          "Animated metadata updated: object moves from azimuth %.1f° to "
          "%.1f°\n",
          start_pos.azimuth, end_pos.azimuth);
    }
    free(metadata);
  }

  free(input_data.data);
}

// Test edge case: object with gain
void run_object_with_gain_test(oar_t* oar) {
  printf("\n--- Case 5: Rendering object with gain ---\n");

  // Add an audio group first
  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr, "Failed to add audio group for gain test. Error code: %d\n",
            group_id);
    return;
  }

  oar_audio_element_config_t element_cfg;
  element_cfg.type = ck_object_based;
  element_cfg.obc.num_objects = 1;
  memset(&element_cfg.parameters, 0, sizeof(parameter_set_t));

  int ret = oar_add_audio_element(oar, group_id, 6, &element_cfg);
  if (ret != 0) {
    fprintf(stderr, "Failed to add gain test audio element. Error code: %d\n",
            ret);
    return;
  }

  uint32_t samples_per_channel = oar_get_samples_per_channel(oar);
  oar_audio_block_t input_data;
  input_data.channels = 1;
  input_data.samples_per_channel = samples_per_channel;
  input_data.data = (float*)malloc(samples_per_channel * sizeof(float));

  if (!input_data.data) {
    fprintf(stderr, "Failed to allocate memory for gain test input data.\n");
    return;
  }

  generate_sine_wave(input_data.data, samples_per_channel, 440.0f, 48000.0);

  ret = oar_update_audio_element_data(oar, 6, &input_data);
  if (ret != 0) {
    fprintf(stderr, "Failed to update gain test data. Error code: %d\n", ret);
    free(input_data.data);
    return;
  }

  // Set object position
  polar_t position = {0.0, 0.0, 1.0};
  oar_metadata_t* pos_metadata =
      create_object_metadata(&position, 1, samples_per_channel);

  if (pos_metadata) {
    ret = oar_update_audio_element_metadata(oar, 6, pos_metadata);
    if (ret != 0) {
      fprintf(
          stderr,
          "Failed to update position metadata for gain test. Error code: %d\n",
          ret);
    }
    free(pos_metadata);
  }

  // Apply gain (-6.0dB, which corresponds to linear gain of 0.5)
  oar_metadata_t* gain_metadata =
      create_gain_metadata(1, -6.0f, samples_per_channel);

  if (gain_metadata) {
    ret = oar_update_audio_element_metadata(oar, 6, gain_metadata);
    if (ret != 0) {
      fprintf(stderr, "Failed to update gain metadata. Error code: %d\n", ret);
    } else {
      printf("Gain metadata updated: gain = -6.0dB (linear gain ≈ 0.5)\n");
    }
    free(gain_metadata);
  }

  free(input_data.data);
}

// Test edge case: invalid parameters (should be handled gracefully)
void run_invalid_parameter_test(oar_t* oar) {
  printf("\n--- Case 6: Testing invalid parameter handling ---\n");

  // Test with NULL metadata (should be handled gracefully)
  int ret = oar_update_audio_element_metadata(oar, 99, NULL);
  if (ret != 0) {
    printf("Correctly rejected NULL metadata (error code: %d)\n", ret);
  } else {
    printf("WARNING: NULL metadata was accepted (potential bug)\n");
  }

  // Test with non-existent element ID
  oar_metadata_t dummy_metadata;
  dummy_metadata.type = ck_metadata_object_positions;
  dummy_metadata.duration = 256;
  ret = oar_update_audio_element_metadata(oar, 999, &dummy_metadata);
  if (ret != 0) {
    printf("Correctly rejected non-existent element ID (error code: %d)\n",
           ret);
  } else {
    printf("WARNING: Non-existent element ID was accepted (potential bug)\n");
  }
}

// Helper function to compare two audio blocks
void compare_audio_blocks(const oar_audio_block_t* block1,
                          const oar_audio_block_t* block2,
                          const char* block1_name, const char* block2_name) {
  printf("\n--- Comparing Audio Blocks: %s vs %s ---\n", block1_name,
         block2_name);
  if (block1->channels != block2->channels ||
      block1->samples_per_channel != block2->samples_per_channel) {
    printf("Error: Audio blocks have different dimensions.\n");
    printf("%s: channels=%u, samples_per_channel=%u\n", block1_name,
           block1->channels, block1->samples_per_channel);
    printf("%s: channels=%u, samples_per_channel=%u\n", block2_name,
           block2->channels, block2->samples_per_channel);
    return;
  }

  uint32_t total_samples = block1->channels * block1->samples_per_channel;
  double max_diff = 0.0;
  double sum_sq_diff = 0.0;
  double sample1_val, sample2_val, diff;

  for (uint32_t i = 0; i < total_samples; ++i) {
    sample1_val = block1->data[i];
    sample2_val = block2->data[i];
    diff = fabs(sample1_val - sample2_val);
    if (diff > max_diff) {
      max_diff = diff;
    }
    sum_sq_diff += diff * diff;
  }
  double mean_sq_error = sum_sq_diff / total_samples;

  printf("Total samples compared: %u\n", total_samples);
  printf("Maximum absolute difference: %f\n", max_diff);
  printf("Mean Squared Error (MSE): %f\n", mean_sq_error);

  if (max_diff < 1e-6) {  // A small threshold for floating point comparison
    printf("Result: Audio blocks are effectively identical.\n");
  } else {
    printf("Result: Audio blocks have significant differences.\n");
  }
}

int main() {
  printf("Running object-based audio element rendering test...\n");
  oar_config_t oar_cfg = create_default_oar_config();
  oar_audio_block_t output_data_single_object = {0};
  oar_audio_block_t output_data_case_a = {0};
  oar_audio_block_t output_data_case_b = {0};
  oar_audio_block_t output_data_animated = {0};
  oar_audio_block_t output_data_gain = {0};

  // --- Scenario 0: Single object ---
  printf("\n===== Scenario 0: Single Object =====\n");
  oar_t* oar_single = oar_create(&oar_cfg);
  if (oar_single == NULL) {
    fprintf(stderr, "Failed to create OAR object for Single Object test.\n");
    return -1;
  }
  printf("OAR object for Single Object test created successfully.\n");

  run_single_object_test(oar_single);

  if (_render_audio(oar_single, &output_data_single_object,
                    "Single Object Scenario") != 0) {
    fprintf(stderr,
            "Aborting due to rendering failure in Single Object test.\n");
    oar_destroy(oar_single);
    // output_data_single_object.data is freed by _render_audio on failure, or
    // NULL
    return -1;
  }
  oar_destroy(oar_single);  // Destroy OAR instance for Single Object test
  _print_audio_block_samples(&output_data_single_object,
                             "Single Object Output");

  // --- Scenario A: Two objects in a single element ---
  printf("\n===== Scenario A: Two objects in one element =====\n");
  oar_t* oar_case_a = oar_create(&oar_cfg);
  if (oar_case_a == NULL) {
    fprintf(stderr, "Failed to create OAR object for Scenario A.\n");
    return -1;
  }
  printf("OAR object for Scenario A created successfully.\n");

  run_two_objects_test(oar_case_a);

  if (_render_audio(oar_case_a, &output_data_case_a, "Scenario A") != 0) {
    fprintf(stderr, "Aborting due to rendering failure in Scenario A.\n");
    oar_destroy(oar_case_a);
    // output_data_case_a.data is freed by _render_audio on failure, or NULL
    return -1;
  }
  oar_destroy(oar_case_a);  // Destroy OAR instance for Scenario A
  _print_audio_block_samples(&output_data_case_a, "Scenario A Output");

  // --- Scenario B: Two objects in two separate elements ---
  printf("\n===== Scenario B: Two objects in two separate elements =====\n");
  oar_t* oar_case_b = oar_create(&oar_cfg);
  if (oar_case_b == NULL) {
    fprintf(stderr, "Failed to create OAR object for Scenario B.\n");
    free(output_data_case_a.data);  // Clean up Scenario A data
    return -1;
  }
  printf("OAR object for Scenario B created successfully.\n");

  run_two_single_objects_in_separate_elements_test(oar_case_b);

  if (_render_audio(oar_case_b, &output_data_case_b, "Scenario B") != 0) {
    fprintf(stderr, "Aborting due to rendering failure in Scenario B.\n");
    oar_destroy(oar_case_b);
    free(output_data_case_a.data);  // Clean up Scenario A data
    // output_data_case_b.data is freed by _render_audio on failure, or NULL
    return -1;
  }
  oar_destroy(oar_case_b);  // Destroy OAR instance for Scenario B
  _print_audio_block_samples(&output_data_case_b, "Scenario B Output");

  // --- Scenario C: Animated object positions ---
  printf("\n===== Scenario C: Animated object positions =====\n");
  oar_t* oar_animated = oar_create(&oar_cfg);
  if (oar_animated == NULL) {
    fprintf(stderr, "Failed to create OAR object for animated test.\n");
    free(output_data_case_a.data);
    free(output_data_case_b.data);
    return -1;
  }
  printf("OAR object for animated test created successfully.\n");

  run_animated_object_test(oar_animated);

  if (_render_audio(oar_animated, &output_data_animated, "Animated Scenario") !=
      0) {
    fprintf(stderr, "Aborting due to rendering failure in animated test.\n");
    oar_destroy(oar_animated);
    free(output_data_case_a.data);
    free(output_data_case_b.data);
    return -1;
  }
  oar_destroy(oar_animated);
  _print_audio_block_samples(&output_data_animated, "Animated Output");

  // --- Scenario D: Object with gain ---
  printf("\n===== Scenario D: Object with gain =====\n");
  oar_t* oar_gain = oar_create(&oar_cfg);
  if (oar_gain == NULL) {
    fprintf(stderr, "Failed to create OAR object for gain test.\n");
    free(output_data_case_a.data);
    free(output_data_case_b.data);
    free(output_data_animated.data);
    return -1;
  }
  printf("OAR object for gain test created successfully.\n");

  run_object_with_gain_test(oar_gain);

  if (_render_audio(oar_gain, &output_data_gain, "Gain Scenario") != 0) {
    fprintf(stderr, "Aborting due to rendering failure in gain test.\n");
    oar_destroy(oar_gain);
    free(output_data_case_a.data);
    free(output_data_case_b.data);
    free(output_data_animated.data);
    return -1;
  }
  oar_destroy(oar_gain);
  _print_audio_block_samples(&output_data_gain, "Gain Output");

  // --- Scenario E: Invalid parameter test ---
  printf("\n===== Scenario E: Invalid parameter test =====\n");
  oar_t* oar_invalid = oar_create(&oar_cfg);
  if (oar_invalid == NULL) {
    fprintf(stderr,
            "Failed to create OAR object for invalid parameter test.\n");
    free(output_data_case_a.data);
    free(output_data_case_b.data);
    free(output_data_animated.data);
    free(output_data_gain.data);
    return -1;
  }
  printf("OAR object for invalid parameter test created successfully.\n");

  run_invalid_parameter_test(oar_invalid);
  oar_destroy(oar_invalid);

  // --- Comparison ---
  compare_audio_blocks(&output_data_case_a, &output_data_case_b,
                       "Scenario A (Two objects in one element)",
                       "Scenario B (Two objects in two separate elements)");

  // --- Cleanup ---
  free(output_data_single_object.data);
  free(output_data_case_a.data);
  free(output_data_case_b.data);
  free(output_data_animated.data);
  free(output_data_gain.data);

  printf("\nObject-based rendering test completed.\n");
  return 0;
}
