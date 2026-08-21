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
 *   TC13: binaural, mirrored positions (±80°) → outputs differ + ILD flips
 *   TC14: binaural, position update between frames → takes effect next frame
 *   TC15: binaural, input block smaller than frame → rejected, no abort
 *   TC16: binaural, mismatched block sizes across elements → rejected
 *   TC17: binaural, two elements rendered simultaneously → independent
 * positions
 */

#include <errno.h>
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
#define TEST_ELEMENT_ID_2 2
/* Binaural azimuth for the interaural asymmetry checks. */
#define TEST_SIDE_AZIMUTH 80.0f
/* Minimum interaural energy ratio near/far ear at ±80°. */
#define TEST_ILD_RATIO 1.1f
/* Minimum RMS difference between two renders, relative to signal RMS,
 * for them to count as "different". */
#define TEST_MIN_RELATIVE_DIFF 0.05

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

/* Unified helper: adds an object-based element, fills all input channels with
 * a sine wave, optionally attaches a default position metadata, and submits
 * the data. Replaces the former add_object_element_and_data and
 * add_object_element_without_position. */
static int add_object_element(oar_t *oar, uint32_t *out_channels,
                              float **out_input_data, int add_position) {
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

  /* Fill all input channels (not just channel 0) */
  for (uint32_t ch = 0; ch < input_channels; ch++) {
    generate_sine_wave(input_data.data + ch * TEST_SAMPLES_PER_CHANNEL,
                       TEST_SAMPLES_PER_CHANNEL, 440.0f,
                       (float)TEST_SAMPLING_RATE);
  }

  if (oar_update_audio_element_data(oar, TEST_ELEMENT_ID, &input_data) != 0) {
    fprintf(stderr, "FAIL: oar_update_audio_element_data failed.\n");
    free(input_data.data);
    return -1;
  }

  if (add_position) {
    polar_t position = {30.0f, 0.0f, 1.0f};
    oar_metadata_t *pos_meta =
        create_position_metadata(position, TEST_SAMPLES_PER_CHANNEL);
    if (!pos_meta) {
      fprintf(stderr, "FAIL: create_position_metadata returned NULL.\n");
      free(input_data.data);
      return -1;
    }
    int ret = oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, pos_meta);
    free(pos_meta);
    if (ret != 0) {
      fprintf(stderr, "FAIL: oar_update_audio_element_metadata failed (%d).\n",
              ret);
      free(input_data.data);
      return -1;
    }
  }

  *out_channels = oar_get_number_of_output_channels(oar);
  *out_input_data = input_data.data;
  return 0;
}

/* Head-shadow ILD is weak below ~1.5 kHz, so a pure 440 Hz tone would make
 * the interaural asymmetry checks in TC13/TC14 fragile. Use a stimulus with
 * both low- and high-frequency content instead. */
static void generate_test_stimulus(float *buffer, uint32_t samples,
                                   float sample_rate) {
  for (uint32_t i = 0; i < samples; ++i) {
    float t = (float)i / sample_rate;
    buffer[i] = 0.45f * (float)sin(2.0 * M_PI * 440.0 * t) +
                0.45f * (float)sin(2.0 * M_PI * 3500.0 * t);
  }
}

static int add_object_element_to_group(oar_t *oar, int gid,
                                       uint32_t element_id) {
  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_object_based;
  elem_cfg.obc.num_objects = 1;
  memset(&elem_cfg.parameters, 0, sizeof(parameter_set_t));

  return oar_add_audio_element(oar, gid, element_id, &elem_cfg);
}

/* Like add_object_element_to_group but allows specifying num_objects (1-2). */
static int add_object_element_to_group_n(oar_t *oar, int gid,
                                         uint32_t element_id, int num_objects) {
  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_object_based;
  elem_cfg.obc.num_objects = num_objects;
  memset(&elem_cfg.parameters, 0, sizeof(parameter_set_t));

  return oar_add_audio_element(oar, gid, element_id, &elem_cfg);
}

/* Submit a stimulus block of the given size for one element, filling every
 * input channel. The renderer copies the block, so the buffer is freed here.
 * Returns the oar_update_audio_element_data result. */
static int submit_stimulus_block(oar_t *oar, uint32_t element_id,
                                 uint32_t samples) {
  uint32_t channels = oar_get_number_of_audio_element_channels(oar, element_id);
  if (channels == 0) return -1;

  oar_audio_block_t block;
  block.channels = channels;
  block.samples_per_channel = samples;
  block.data = (float *)malloc(channels * samples * sizeof(float));
  if (!block.data) return -1;

  for (uint32_t c = 0; c < channels; ++c) {
    generate_test_stimulus(block.data + c * samples, samples,
                           (float)TEST_SAMPLING_RATE);
  }

  int ret = oar_update_audio_element_data(oar, element_id, &block);
  free(block.data);
  return ret;
}

static int submit_position(oar_t *oar, uint32_t element_id, float azimuth,
                           uint32_t duration) {
  polar_t position = {azimuth, 0.0f, 1.0f};
  oar_metadata_t *meta = create_position_metadata(position, duration);
  if (!meta) return -1;
  int ret = oar_update_audio_element_metadata(oar, element_id, meta);
  free(meta);
  return ret;
}

/* Submit position metadata with two objects at different azimuths for a
 * num_objects=2 element. Both objects share the same duration. */
static int submit_position_dual(oar_t *oar, uint32_t element_id, float az1,
                                float az2, uint32_t duration) {
  oar_metadata_t meta;
  memset(&meta, 0, sizeof(meta));
  meta.type = ck_metadata_object_positions;
  meta.duration = (int)duration;
  meta.object_positions.param_type = ck_param_constant;
  meta.object_positions.position_type = ck_polar;
  meta.object_positions.num_objects = 2;
  meta.object_positions.polar_positions[0] = (polar_t){az1, 0.0f, 1.0f};
  meta.object_positions.polar_positions[1] = (polar_t){az2, 0.0f, 1.0f};
  return oar_update_audio_element_metadata(oar, element_id, &meta);
}

static double sum_abs(const float *data, uint32_t count) {
  double sum = 0.0;
  for (uint32_t i = 0; i < count; ++i) sum += fabs(data[i]);
  return sum;
}

static double rms_of(const float *data, uint32_t count) {
  double sum = 0.0;
  for (uint32_t i = 0; i < count; ++i) sum += (double)data[i] * data[i];
  return sqrt(sum / count);
}

static double rms_diff(const float *a, const float *b, uint32_t count) {
  double sum = 0.0;
  for (uint32_t i = 0; i < count; ++i) {
    double d = (double)a[i] - b[i];
    sum += d * d;
  }
  return sqrt(sum / count);
}

/* --- Setup/teardown helpers for TC1-TC7 to reduce config/output boilerplate.
 * Each TC runs in its own child process (crash isolation), so these helpers
 * do not introduce shared state — they simply eliminate the repeated
 * memset(config) / oar_create / output alloc pattern. --- */

/* Create a binaural OAR instance with a single object element (with default
 * position) and return it along with the output channel count and input data
 * pointer (caller frees input_data). Returns 0 on success. */
static int create_binaural_oar_with_element(oar_t **out_oar,
                                            uint32_t *out_channels,
                                            float **out_input_data) {
  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  uint32_t out_ch = 0;
  float *input_data = NULL;
  if (add_object_element(oar, &out_ch, &input_data, 1) != 0) {
    oar_destroy(oar);
    return -1;
  }

  *out_oar = oar;
  *out_channels = out_ch;
  *out_input_data = input_data;
  return 0;
}

/* Allocate a zeroed output block matching the given channel count. Returns 0
 * on success. */
static int alloc_output_block(uint32_t out_channels,
                              oar_audio_block_t *output) {
  memset(output, 0, sizeof(*output));
  output->channels = out_channels;
  output->samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output->data =
      (float *)calloc(out_channels * TEST_SAMPLES_PER_CHANNEL, sizeof(float));
  return output->data ? 0 : -1;
}

/* Render and verify output is non-silent. Returns 0 on success. Cleans up
 * and sets *out_oar to NULL on failure. */
static int render_and_check_non_silent(oar_t *oar, oar_audio_block_t *output,
                                       const char *label) {
  memset(output->data, 0,
         output->channels * output->samples_per_channel * sizeof(float));
  int ret = oar_render(oar, output);
  if (ret != 0) {
    fprintf(stderr, "FAIL: oar_render returned %d (%s)\n", ret, label);
    return -1;
  }
  uint32_t total = output->channels * output->samples_per_channel;
  if (!is_output_non_silent(output->data, total)) {
    fprintf(stderr, "FAIL: output is silent (%s)\n", label);
    return -1;
  }
  return 0;
}

/* Create a binaural renderer with a single object at the given azimuth,
 * render `frames` full frames (resubmitting stimulus and position before
 * each), and copy the last rendered frame into `out`
 * (2 * TEST_SAMPLES_PER_CHANNEL floats, planar). */
static int render_binaural_frames(float azimuth, int frames, float *out) {
  oar_config_t config;
  memset(&config, 0, sizeof(config));

  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int gid = oar_add_audio_group(oar);
  if (gid < 0 || add_object_element_to_group(oar, gid, TEST_ELEMENT_ID) != 0) {
    oar_destroy(oar);
    return -1;
  }

  uint32_t out_channels = oar_get_number_of_output_channels(oar);
  if (out_channels != 2) {
    fprintf(stderr, "FAIL: expected 2 binaural output channels, got %u.\n",
            out_channels);
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)calloc(out_channels * TEST_SAMPLES_PER_CHANNEL, sizeof(float));
  if (!output.data) {
    oar_destroy(oar);
    return -1;
  }

  int ret = -1;
  for (int f = 0; f < frames; ++f) {
    if (submit_stimulus_block(oar, TEST_ELEMENT_ID, TEST_SAMPLES_PER_CHANNEL) !=
        0)
      goto done;
    if (submit_position(oar, TEST_ELEMENT_ID, azimuth,
                        TEST_SAMPLES_PER_CHANNEL) != 0)
      goto done;
    memset(output.data, 0,
           out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
    if (oar_render(oar, &output) != 0) goto done;
  }

  memcpy(out, output.data,
         out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
  ret = 0;

done:
  free(output.data);
  oar_destroy(oar);
  return ret;
}

/* TC1: set_metadata(32) after add_group (binaural) — should succeed */
static int test_set_metadata_after_add_group_binaural() {
  printf("\n===== TC1: set_metadata(32) after add_group (binaural) =====\n");

  oar_t *oar = NULL;
  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (create_binaural_oar_with_element(&oar, &out_channels, &input_data) != 0) {
    fprintf(stderr, "FAIL: Failed to setup binaural OAR.\n");
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

  oar_t *oar = NULL;
  uint32_t out_channels = 0;
  float *input_data = NULL;
  if (create_binaural_oar_with_element(&oar, &out_channels, &input_data) != 0)
    return -1;

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SAMPLES_PER_CHANNEL);
  if (ret != 0) {
    fprintf(stderr, "FAIL: set_metadata(512) returned %d, expected 0.\n", ret);
    free(input_data);
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  if (alloc_output_block(out_channels, &output) != 0) {
    free(input_data);
    oar_destroy(oar);
    return -1;
  }

  int result = 0;
  if (render_and_check_non_silent(oar, &output, "TC2") != 0) result = -1;

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  if (result == 0)
    printf("PASS: set_metadata(512) + render succeeded (non-silent).\n");
  return result;
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
  if (add_object_element(oar, &out_channels, &input_data, 1) != 0) {
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

  if (ret != 0) {
    fprintf(stderr, "FAIL: oar_render on stereo returned %d\n", ret);
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  uint32_t total_samples = out_channels * TEST_SAMPLES_PER_CHANNEL;
  if (!is_output_non_silent(output.data, total_samples)) {
    fprintf(
        stderr,
        "FAIL: output is silent after stereo + set_metadata(32) + render.\n");
    free(input_data);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  printf("PASS: stereo + set_metadata(32) + render succeeded (non-silent).\n");
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
  if (add_object_element(oar, &out_channels, &input_data, 1) != 0) {
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
  if (add_object_element(oar, &out_channels, &input_data, 1) != 0) {
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
  if (add_object_element(oar, &out_channels, &input_data, 1) != 0) {
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
  if (add_object_element(oar, &out_channels, &input_data, 0) != 0) {
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

  if (!meta_left || !meta_right) {
    fprintf(stderr, "FAIL: create_position_metadata returned NULL.\n");
    free(meta_left);
    free(meta_right);
    free(input_data);
    oar_destroy(oar);
    return -1;
  }

  ret = oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, meta_left);
  free(meta_left);

  if (ret != 0) {
    fprintf(stderr,
            "FAIL: oar_update_audio_element_metadata (left) returned %d.\n",
            ret);
    free(meta_right);
    free(input_data);
    oar_destroy(oar);
    return -1;
  }

  ret = oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, meta_right);
  free(meta_right);
  if (ret != 0) {
    fprintf(stderr,
            "FAIL: oar_update_audio_element_metadata (right) returned %d.\n",
            ret);
    free(input_data);
    oar_destroy(oar);
    return -1;
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

/* TC13: binaural, mirrored positions (±80°) → outputs differ + ILD flips.
 *
 * Primary assertion: two renders of identical input at mirrored azimuths must
 * differ. If object positions never reach OBR, both render at the creation
 * default (front-center) and the outputs are bit-identical, regardless of the
 * HRIR set. Secondary assertion: the interaural energy asymmetry must follow
 * the azimuth sign (positive azimuth = left, as in TC12). ILD magnitude
 * accuracy is covered by OBR's own unit tests (GetBroadbandILD); only the
 * sign is asserted here. */
static int test_binaural_position_effect() {
  printf("\n===== TC13: binaural mirrored positions (±80°) =====\n");

  uint32_t total = 2 * TEST_SAMPLES_PER_CHANNEL;
  float *render_left = (float *)calloc(total, sizeof(float));
  float *render_right = (float *)calloc(total, sizeof(float));
  if (!render_left || !render_right) {
    free(render_left);
    free(render_right);
    return -1;
  }

  /* Render the second frame so the measurement is past the onset. */
  if (render_binaural_frames(TEST_SIDE_AZIMUTH, 2, render_left) != 0 ||
      render_binaural_frames(-TEST_SIDE_AZIMUTH, 2, render_right) != 0) {
    fprintf(stderr, "FAIL: binaural render failed.\n");
    free(render_left);
    free(render_right);
    return -1;
  }

  int result = 0;
  double sig = rms_of(render_left, total);
  double diff = rms_diff(render_left, render_right, total);

  if (sig < 1e-6) {
    fprintf(stderr, "FAIL: binaural output is silent (rms=%g).\n", sig);
    result = -1;
  } else if (diff < TEST_MIN_RELATIVE_DIFF * sig) {
    fprintf(stderr,
            "FAIL: renders at +80° and -80° are (near-)identical "
            "(diff rms=%g, signal rms=%g). Object positions are not being "
            "delivered to OBR.\n",
            diff, sig);
    result = -1;
  }

  double e_l_pos = sum_abs(render_left, TEST_SAMPLES_PER_CHANNEL);
  double e_r_pos =
      sum_abs(render_left + TEST_SAMPLES_PER_CHANNEL, TEST_SAMPLES_PER_CHANNEL);
  double e_l_neg = sum_abs(render_right, TEST_SAMPLES_PER_CHANNEL);
  double e_r_neg = sum_abs(render_right + TEST_SAMPLES_PER_CHANNEL,
                           TEST_SAMPLES_PER_CHANNEL);

  if (e_l_pos <= TEST_ILD_RATIO * e_r_pos ||
      e_r_neg <= TEST_ILD_RATIO * e_l_neg) {
    fprintf(stderr,
            "FAIL: interaural asymmetry does not follow azimuth sign.\n"
            "  +80°: E_L=%.4f, E_R=%.4f (expected E_L dominant)\n"
            "  -80°: E_L=%.4f, E_R=%.4f (expected E_R dominant)\n",
            e_l_pos, e_r_pos, e_l_neg, e_r_neg);
    result = -1;
  }

  if (result == 0) {
    printf(
        "PASS: mirrored positions render differently and ILD follows the "
        "azimuth sign.\n");
  }

  free(render_left);
  free(render_right);
  return result;
}

/* TC14: binaural, position update between frames takes effect.
 *
 * OBR's buffer_size_per_channel is fixed at creation, so binaural position
 * updates are frame-granular — but they must still be applied on the next
 * frame. Render at +80°, update to -80°, and assert the interaural balance
 * flips. The frame right after the update is skipped so any position
 * crossfade inside OBR does not blur the measurement. */
static int test_binaural_position_update_across_frames() {
  printf("\n===== TC14: binaural position update across frames =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int result = -1;
  float *frame1 = NULL;
  float *frame3 = NULL;

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.data = NULL;

  int gid = oar_add_audio_group(oar);
  if (gid < 0 || add_object_element_to_group(oar, gid, TEST_ELEMENT_ID) != 0) {
    fprintf(stderr, "FAIL: element setup failed.\n");
    goto done;
  }

  uint32_t out_channels = oar_get_number_of_output_channels(oar);
  if (out_channels != 2) {
    fprintf(stderr, "FAIL: expected 2 binaural output channels, got %u.\n",
            out_channels);
    goto done;
  }

  uint32_t total = out_channels * TEST_SAMPLES_PER_CHANNEL;
  frame1 = (float *)calloc(total, sizeof(float));
  frame3 = (float *)calloc(total, sizeof(float));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data = (float *)calloc(total, sizeof(float));
  if (!frame1 || !frame3 || !output.data) goto done;

  /* Frame 1 at +80°, frames 2 and 3 at -80°; measure frames 1 and 3. */
  float azimuths[3] = {TEST_SIDE_AZIMUTH, -TEST_SIDE_AZIMUTH,
                       -TEST_SIDE_AZIMUTH};
  for (int f = 0; f < 3; ++f) {
    if (submit_stimulus_block(oar, TEST_ELEMENT_ID, TEST_SAMPLES_PER_CHANNEL) !=
            0 ||
        submit_position(oar, TEST_ELEMENT_ID, azimuths[f],
                        TEST_SAMPLES_PER_CHANNEL) != 0) {
      fprintf(stderr, "FAIL: data/metadata update failed on frame %d.\n",
              f + 1);
      goto done;
    }
    memset(output.data, 0, total * sizeof(float));
    if (oar_render(oar, &output) != 0) {
      fprintf(stderr, "FAIL: oar_render failed on frame %d.\n", f + 1);
      goto done;
    }
    if (f == 0) memcpy(frame1, output.data, total * sizeof(float));
    if (f == 2) memcpy(frame3, output.data, total * sizeof(float));
  }

  double sig = rms_of(frame1, total);
  double diff = rms_diff(frame1, frame3, total);
  double e_l_f1 = sum_abs(frame1, TEST_SAMPLES_PER_CHANNEL);
  double e_r_f1 =
      sum_abs(frame1 + TEST_SAMPLES_PER_CHANNEL, TEST_SAMPLES_PER_CHANNEL);
  double e_l_f3 = sum_abs(frame3, TEST_SAMPLES_PER_CHANNEL);
  double e_r_f3 =
      sum_abs(frame3 + TEST_SAMPLES_PER_CHANNEL, TEST_SAMPLES_PER_CHANNEL);

  if (sig < 1e-6) {
    fprintf(stderr, "FAIL: binaural output is silent (rms=%g).\n", sig);
  } else if (diff < TEST_MIN_RELATIVE_DIFF * sig) {
    fprintf(stderr,
            "FAIL: output did not change after the position update "
            "(diff rms=%g, signal rms=%g).\n",
            diff, sig);
  } else if (e_l_f1 <= TEST_ILD_RATIO * e_r_f1 ||
             e_r_f3 <= TEST_ILD_RATIO * e_l_f3) {
    fprintf(stderr,
            "FAIL: interaural balance did not flip after the update.\n"
            "  frame1 (+80°): E_L=%.4f, E_R=%.4f\n"
            "  frame3 (-80°): E_L=%.4f, E_R=%.4f\n",
            e_l_f1, e_r_f1, e_l_f3, e_r_f3);
  } else {
    printf("PASS: position update applied on a subsequent frame.\n");
    result = 0;
  }

done:
  free(frame1);
  free(frame3);
  free(output.data);
  oar_destroy(oar);
  return result;
}

/* TC15: binaural, input block smaller than the configured frame must be
 * rejected by oar_update_audio_element_data — not fed to OBR, whose
 * ABSL_CHECK_EQ on the buffer size aborts the whole process. */
static int test_undersized_block_rejected() {
  printf("\n===== TC15: undersized input block rejected =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int gid = oar_add_audio_group(oar);
  if (gid < 0 || add_object_element_to_group(oar, gid, TEST_ELEMENT_ID) != 0) {
    oar_destroy(oar);
    return -1;
  }

  uint32_t out_channels = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)calloc(out_channels * TEST_SAMPLES_PER_CHANNEL, sizeof(float));
  if (!output.data) {
    oar_destroy(oar);
    return -1;
  }

  int ret =
      submit_stimulus_block(oar, TEST_ELEMENT_ID, TEST_SAMPLES_PER_CHANNEL / 2);
  if (ret == 0) {
    fprintf(stderr,
            "FAIL: %u-sample block accepted with samples_per_channel=%u. "
            "Rendering to demonstrate the failure mode (expect an abort in "
            "ObrImpl::Process)...\n",
            TEST_SAMPLES_PER_CHANNEL / 2, TEST_SAMPLES_PER_CHANNEL);
    fflush(stderr);
    oar_render(oar, &output);
    free(output.data);
    oar_destroy(oar);
    return -1;
  }
  printf("  undersized block: returned %d (expected error) OK\n", ret);

  /* The renderer must remain usable after the rejection. */
  if (submit_stimulus_block(oar, TEST_ELEMENT_ID, TEST_SAMPLES_PER_CHANNEL) !=
          0 ||
      oar_render(oar, &output) != 0) {
    fprintf(stderr, "FAIL: renderer unusable after rejected block.\n");
    free(output.data);
    oar_destroy(oar);
    return -1;
  }

  printf("PASS: undersized block rejected; renderer still renders.\n");
  free(output.data);
  oar_destroy(oar);
  return 0;
}

/* TC16: mismatched block sizes across two elements must be rejected. If both
 * submissions are accepted, add_data memcpys the second element's larger
 * block with the first block's stride/allocation — a heap buffer overflow
 * (deterministic under ASan; here detected via the return codes). */
static int test_mismatched_blocks_across_elements() {
  printf("\n===== TC16: mismatched blocks across elements rejected =====\n");

  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int gid = oar_add_audio_group(oar);
  if (gid < 0 || add_object_element_to_group(oar, gid, TEST_ELEMENT_ID) != 0 ||
      add_object_element_to_group(oar, gid, TEST_ELEMENT_ID_2) != 0) {
    oar_destroy(oar);
    return -1;
  }

  int ret_a =
      submit_stimulus_block(oar, TEST_ELEMENT_ID, TEST_SAMPLES_PER_CHANNEL / 2);
  int ret_b =
      submit_stimulus_block(oar, TEST_ELEMENT_ID_2, TEST_SAMPLES_PER_CHANNEL);

  if (ret_a == 0 && ret_b == 0) {
    fprintf(stderr,
            "FAIL: mismatched block sizes (%u then %u) both accepted — the "
            "second memcpy in add_data overruns the staging buffer sized for "
            "the first block.\n",
            TEST_SAMPLES_PER_CHANNEL / 2, TEST_SAMPLES_PER_CHANNEL);
    oar_destroy(oar);
    return -1;
  }

  printf("PASS: mismatched block sizes rejected (first: %d, second: %d).\n",
         ret_a, ret_b);
  oar_destroy(oar);
  return 0;
}

/* TC17 helper: create a binaural renderer with two object-based elements,
 * where element 1 has num_objects=1 and element 2 has num_objects=2. This
 * simultaneously exercises the multi-element multiplexing path
 * (channel_start_index interleaving, apply_frame_positions per-element) and
 * the multi-object path (polar_positions[1], dual-object metadata_update).
 * Submit data and positions for both, render two frames, and copy the last
 * frame into `out` (2 * TEST_SAMPLES_PER_CHANNEL floats, planar).
 *
 * az1: azimuth for element 1 (single object).
 * az2_obj1, az2_obj2: azimuths for element 2's two objects. */
static int render_multi_element_multi_object(float az1, float az2_obj1,
                                             float az2_obj2, float *out) {
  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  config.sampling_rate = TEST_SAMPLING_RATE;

  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int gid = oar_add_audio_group(oar);
  if (gid < 0 || add_object_element_to_group(oar, gid, TEST_ELEMENT_ID) != 0 ||
      add_object_element_to_group_n(oar, gid, TEST_ELEMENT_ID_2, 2) != 0) {
    oar_destroy(oar);
    return -1;
  }

  uint32_t out_channels = oar_get_number_of_output_channels(oar);
  if (out_channels != 2) {
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  memset(&output, 0, sizeof(output));
  output.channels = out_channels;
  output.samples_per_channel = TEST_SAMPLES_PER_CHANNEL;
  output.data =
      (float *)calloc(out_channels * TEST_SAMPLES_PER_CHANNEL, sizeof(float));
  if (!output.data) {
    oar_destroy(oar);
    return -1;
  }

  int ret = -1;
  /* Submit data and positions for both elements, then render. */
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (submit_stimulus_block(oar, TEST_ELEMENT_ID, TEST_SAMPLES_PER_CHANNEL) !=
        0)
      goto done;
    if (submit_stimulus_block(oar, TEST_ELEMENT_ID_2,
                              TEST_SAMPLES_PER_CHANNEL) != 0)
      goto done;
    if (submit_position(oar, TEST_ELEMENT_ID, az1, TEST_SAMPLES_PER_CHANNEL) !=
        0)
      goto done;
    if (submit_position_dual(oar, TEST_ELEMENT_ID_2, az2_obj1, az2_obj2,
                             TEST_SAMPLES_PER_CHANNEL) != 0)
      goto done;
    memset(output.data, 0,
           out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
    if (oar_render(oar, &output) != 0) goto done;
  }

  memcpy(out, output.data,
         out_channels * TEST_SAMPLES_PER_CHANNEL * sizeof(float));
  ret = 0;

done:
  free(output.data);
  oar_destroy(oar);
  return ret;
}

/* TC17: binaural, multi-element + multi-object rendering.
 *
 * audio_elements_renderer (plural) multiplexes multiple object-based elements
 * into a single OBR instance. This TC uses two elements: element 1 has
 * num_objects=1, element 2 has num_objects=2. This simultaneously covers:
 *   - Multi-element: channel_start_index interleaving, apply_frame_positions
 *     per-element traversal.
 *   - Multi-object: polar_positions[1] path, dual-object metadata_update
 *     via submit_position_dual.
 *
 * Verification:
 *   1. Render succeeds and output is non-silent.
 *   2. When all objects are on the same side (element 1 at +80°, element 2's
 *      two objects both at +80°), the ILD is stronger than when element 2's
 *      objects are split (one at +80°, one at -80°), confirming that both
 *      objects within the num_objects=2 element reach OBR independently.
 *   3. The two renders differ, proving the multi-object positions affect
 *      output. */
static int test_multi_element_rendering() {
  printf("\n===== TC17: multi-element + multi-object rendering =====\n");

  uint32_t total = 2 * TEST_SAMPLES_PER_CHANNEL;
  float *render_opposite = (float *)calloc(total, sizeof(float));
  float *render_same = (float *)calloc(total, sizeof(float));
  if (!render_opposite || !render_same) {
    free(render_opposite);
    free(render_same);
    return -1;
  }

  /* Render with split objects: element 1 at +80°, element 2's two objects
   * at +80° and -80°. Element 2's objects span both sides, weakening ILD. */
  if (render_multi_element_multi_object(TEST_SIDE_AZIMUTH, TEST_SIDE_AZIMUTH,
                                        -TEST_SIDE_AZIMUTH,
                                        render_opposite) != 0) {
    fprintf(stderr, "FAIL: multi-element render (split) failed.\n");
    free(render_opposite);
    free(render_same);
    return -1;
  }

  /* Render with all objects on same side: element 1 at +80°, element 2's
   * two objects both at +80°. All three objects on the left → strong ILD. */
  if (render_multi_element_multi_object(TEST_SIDE_AZIMUTH, TEST_SIDE_AZIMUTH,
                                        TEST_SIDE_AZIMUTH, render_same) != 0) {
    fprintf(stderr, "FAIL: multi-element render (same) failed.\n");
    free(render_opposite);
    free(render_same);
    return -1;
  }

  int result = 0;

  /* 1. Both renders must be non-silent. */
  double sig_opposite = rms_of(render_opposite, total);
  double sig_same = rms_of(render_same, total);
  if (sig_opposite < 1e-6) {
    fprintf(stderr, "FAIL: opposite-azimuth output is silent (rms=%g).\n",
            sig_opposite);
    result = -1;
  } else if (sig_same < 1e-6) {
    fprintf(stderr, "FAIL: same-azimuth output is silent (rms=%g).\n",
            sig_same);
    result = -1;
  }

  /* 2. When both elements are at +80°, the left ear should be strongly
   *    dominant (both objects on the left). When they are at opposite
   *    azimuths, the ILD should be weaker (one object each side).
   *    Verify: |E_L - E_R| / (E_L + E_R) is larger for same-azimuth. */
  double e_l_same = sum_abs(render_same, TEST_SAMPLES_PER_CHANNEL);
  double e_r_same =
      sum_abs(render_same + TEST_SAMPLES_PER_CHANNEL, TEST_SAMPLES_PER_CHANNEL);
  double e_l_opp = sum_abs(render_opposite, TEST_SAMPLES_PER_CHANNEL);
  double e_r_opp = sum_abs(render_opposite + TEST_SAMPLES_PER_CHANNEL,
                           TEST_SAMPLES_PER_CHANNEL);

  double ild_same = fabs(e_l_same - e_r_same) / (e_l_same + e_r_same + 1e-12);
  double ild_opp = fabs(e_l_opp - e_r_opp) / (e_l_opp + e_r_opp + 1e-12);

  if (ild_same <= ild_opp) {
    fprintf(stderr,
            "FAIL: ILD not stronger when both elements share azimuth.\n"
            "  same azimuth (+80,+80): ILD=%.4f (E_L=%.4f, E_R=%.4f)\n"
            "  opposite (+80,-80):     ILD=%.4f (E_L=%.4f, E_R=%.4f)\n"
            "  Expected same-azimuth ILD > opposite-azimuth ILD, confirming\n"
            "  both elements' positions reach OBR independently.\n",
            ild_same, e_l_same, e_r_same, ild_opp, e_l_opp, e_r_opp);
    result = -1;
  }

  /* 3. The two renders must differ (proves element 2's data affects output). */
  double diff = rms_diff(render_opposite, render_same, total);
  if (diff < TEST_MIN_RELATIVE_DIFF * sig_same) {
    fprintf(stderr,
            "FAIL: opposite-azimuth and same-azimuth renders are "
            "near-identical (diff rms=%g, signal rms=%g). Element 2's data "
            "or position may not be reaching OBR.\n",
            diff, sig_same);
    result = -1;
  }

  if (result == 0) {
    printf(
        "PASS: two elements rendered simultaneously with independent "
        "positions.\n");
  }

  free(render_opposite);
  free(render_same);
  return result;
}

/* Unified test table for child-process dispatch. Each entry combines the
 * short name, human-readable description, and test function in one place,
 * eliminating the need to keep separate arrays in sync. */
typedef int (*test_fn_t)(void);
typedef struct {
  const char *name;
  const char *description;
  test_fn_t fn;
} test_entry_t;

static test_entry_t g_tests[] = {
    {"TC1", "set_metadata after add_group",
     test_set_metadata_after_add_group_binaural},
    {"TC2", "set_metadata equal, no-op",
     test_set_metadata_equal_after_add_group},
    {"TC3", "stereo + set_metadata", test_set_metadata_stereo},
    {"TC4", "set_metadata before add_group",
     test_set_metadata_before_add_group_binaural},
    {"TC5", "default, no set_metadata", test_default_no_set_metadata_binaural},
    {"TC6", "invalid parameters", test_invalid_parameters},
    {"TC7", "multiple set_metadata",
     test_multiple_set_metadata_before_add_group},
    {"TC8", "binaural non-divisor", test_binaural_non_divisor_accepted},
    {"TC9", "zero samples", test_zero_samples_rejected},
    {"TC10", "samples > frame", test_exceeds_frame_rejected},
    {"TC11", "stereo non-divisor", test_stereo_non_divisor_accepted},
    {"TC12", "stereo varying positions",
     test_stereo_varying_positions_sub_frame},
    {"TC13", "binaural mirrored positions", test_binaural_position_effect},
    {"TC14", "binaural update across frames",
     test_binaural_position_update_across_frames},
    {"TC15", "undersized block rejected", test_undersized_block_rejected},
    {"TC16", "mismatched blocks rejected",
     test_mismatched_blocks_across_elements},
    {"TC17", "multi-element + multi-object rendering",
     test_multi_element_rendering},
};

#define NUM_TESTS (int)(sizeof(g_tests) / sizeof(g_tests[0]))

/* Run a test function in a separate child process so that a crash
 * does not prevent subsequent tests from running.
 *
 * On Linux/macOS, fork() + waitpid() is used.
 * On Windows (MSVC), _spawnl(_P_WAIT, ...) re-invokes this executable
 * with "--child <index>" to run a single test in a child process.
 * Both paths provide identical crash isolation behaviour. */
static int run_test_in_child(int test_index) {
  const char *name = g_tests[test_index].name;

#ifndef _WIN32
  test_fn_t test_fn = g_tests[test_index].fn;

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

  /* Quote the exe path for argv[0] to handle paths containing spaces.
   * _spawnl's cmdname (first exe_path) is used to locate the executable
   * and is not parsed as a command line, so it doesn't need quoting.
   * argv[0] (second exe_path) is written into the command line string and
   * needs quoting for non-MSVC CRTs that don't auto-quote argv[0]. */
  char quoted_path[1028];
  snprintf(quoted_path, sizeof(quoted_path), "\"%s\"", exe_path);

  errno = 0;
  int exit_code =
      _spawnl(_P_WAIT, exe_path, quoted_path, "--child", index_str, NULL);

  if (exit_code == -1) {
    /* The child process never exits with -1 (see main(): failures are mapped
     * to exit code 1), so -1 unambiguously means _spawnl itself failed to
     * launch the child.  errno provides additional diagnostic detail. */
    fprintf(stderr, "[%s] FAILED: spawn error (errno %d: %s)\n", name, errno,
            strerror(errno));
    return -1;
  } else if (exit_code == 0) {
    printf("[%s] PASSED (exit 0)\n", name);
    return 0;
  } else {
    /* Non-zero exit: test failure or crash.
     * On Windows, a crash typically yields exit code 3
     * (STATUS_ACCESS_VIOLATION) or similar non-zero codes. */
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
      return 2; /* sentinel: invalid index (distinct from test failure) */
    }
    int ret = g_tests[index].fn();

    fflush(stdout);
    fflush(stderr);
    /* Map -1 (test failure) to exit code 1 so that -1 from _spawnl uniquely
     * identifies a spawn failure rather than colliding with the child's own
     * failure return. */
    return (ret != 0) ? 1 : 0;
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
    printf("\n--- Running %s ---\n", g_tests[i].name);
    tc_results[i] = run_test_in_child(i);
    result |= (tc_results[i] != 0) ? 1 : 0;
  }

  printf("\n========================================\n");
  printf("Test Summary:\n");
  for (int i = 0; i < NUM_TESTS; i++) {
    printf("  %s (%s): %s\n", g_tests[i].name, g_tests[i].description,
           tc_results[i] == 0 ? "PASSED" : "FAILED/CRASHED");
  }
  printf("========================================\n");

  if (result == 0) {
    printf("\nAll tests passed.\n");
  } else {
    printf("\nSome tests failed or crashed. Review output above.\n");
  }

  return result;
}
