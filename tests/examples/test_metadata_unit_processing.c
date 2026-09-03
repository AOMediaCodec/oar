/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the LICENSE file, you can obtain it at
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
 *         positions
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "test_framework.h"
#include "test_helpers.h"

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

/* --- Metadata helpers --------------------------------------------------- */

static int submit_position(oar_t *oar, uint32_t element_id, float azimuth,
                           uint32_t duration) {
  polar_t position = {azimuth, 0.0f, 1.0f};
  oar_metadata_t *meta = create_object_metadata(&position, 1, duration);
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

/* --- Element setup helpers ----------------------------------------------- */

static int add_object_element_to_group(oar_t *oar, int gid, uint32_t element_id,
                                       int num_objects) {
  oar_audio_element_config_t elem_cfg =
      create_object_element_config(num_objects);
  return oar_add_audio_element(oar, gid, element_id, &elem_cfg);
}

/* Adds an object-based element (num_objects=1), fills all input channels with
 * a sine wave, optionally attaches a default position metadata, and submits
 * the data. Returns 0 on success; caller frees *out_input_data. */
static int add_object_element(oar_t *oar, uint32_t *out_channels,
                              float **out_input_data, int add_position) {
  int gid = oar_add_audio_group(oar);
  if (gid < 0) return -1;

  if (add_object_element_to_group(oar, gid, TEST_ELEMENT_ID, 1) != 0) return -1;

  uint32_t input_channels =
      oar_get_number_of_audio_element_channels(oar, TEST_ELEMENT_ID);

  oar_audio_block_t input_data;
  if (alloc_audio_block(input_channels, TEST_SAMPLES_PER_CHANNEL,
                        &input_data) != 0)
    return -1;

  for (uint32_t ch = 0; ch < input_channels; ch++) {
    generate_sine(input_data.data + ch * TEST_SAMPLES_PER_CHANNEL,
                  TEST_SAMPLES_PER_CHANNEL, 440.0f, (float)TEST_SAMPLING_RATE);
  }

  if (oar_update_audio_element_data(oar, TEST_ELEMENT_ID, &input_data) != 0) {
    free(input_data.data);
    return -1;
  }

  if (add_position) {
    polar_t position = {30.0f, 0.0f, 1.0f};
    oar_metadata_t *pos_meta =
        create_object_metadata(&position, 1, TEST_SAMPLES_PER_CHANNEL);
    if (!pos_meta) {
      free(input_data.data);
      return -1;
    }
    int ret = oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, pos_meta);
    free(pos_meta);
    if (ret != 0) {
      free(input_data.data);
      return -1;
    }
  }

  *out_channels = oar_get_number_of_output_channels(oar);
  *out_input_data = input_data.data;
  return 0;
}

/* Submit a stimulus block of the given size for one element, filling every
 * input channel. The renderer copies the block, so the buffer is freed here.
 * Returns the oar_update_audio_element_data result. */
static int submit_stimulus_block(oar_t *oar, uint32_t element_id,
                                 uint32_t samples) {
  uint32_t channels = oar_get_number_of_audio_element_channels(oar, element_id);
  if (channels == 0) return -1;

  oar_audio_block_t block;
  if (alloc_audio_block(channels, samples, &block) != 0) return -1;

  for (uint32_t c = 0; c < channels; ++c) {
    generate_dual_tone_stimulus(block.data + c * samples, samples,
                                (float)TEST_SAMPLING_RATE, 440.0f, 3500.0f);
  }

  int ret = oar_update_audio_element_data(oar, element_id, &block);
  free(block.data);
  return ret;
}

/* --- Measurement helpers ------------------------------------------------ */

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

/* --- Composite setup helpers -------------------------------------------- */

/* Create a binaural OAR instance with a single object element (with default
 * position) and return it along with the output channel count and input data
 * pointer (caller frees input_data). Returns 0 on success. */
static int create_binaural_oar_with_element(oar_t **out_oar,
                                            uint32_t *out_channels,
                                            float **out_input_data) {
  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
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

/* Create a binaural renderer with a single object at the given azimuth,
 * render `frames` full frames (resubmitting stimulus and position before
 * each), and copy the last rendered frame into `out`
 * (2 * TEST_SAMPLES_PER_CHANNEL floats, planar). */
static int render_binaural_frames(float azimuth, int frames, float *out) {
  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int gid = oar_add_audio_group(oar);
  if (gid < 0 ||
      add_object_element_to_group(oar, gid, TEST_ELEMENT_ID, 1) != 0) {
    oar_destroy(oar);
    return -1;
  }

  uint32_t out_channels = oar_get_number_of_output_channels(oar);
  if (out_channels != 2) {
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  if (alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) != 0) {
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

/* Create a binaural renderer with two object-based elements, where element 1
 * has num_objects=1 and element 2 has num_objects=2. Submit data and positions
 * for both, render two frames, and copy the last frame into `out`
 * (2 * TEST_SAMPLES_PER_CHANNEL floats, planar). */
static int render_multi_element_multi_object(float az1, float az2_obj1,
                                             float az2_obj2, float *out) {
  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  if (!oar) return -1;

  int gid = oar_add_audio_group(oar);
  if (gid < 0 ||
      add_object_element_to_group(oar, gid, TEST_ELEMENT_ID, 1) != 0 ||
      add_object_element_to_group(oar, gid, TEST_ELEMENT_ID_2, 2) != 0) {
    oar_destroy(oar);
    return -1;
  }

  uint32_t out_channels = oar_get_number_of_output_channels(oar);
  if (out_channels != 2) {
    oar_destroy(oar);
    return -1;
  }

  oar_audio_block_t output;
  if (alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) != 0) {
    oar_destroy(oar);
    return -1;
  }

  int ret = -1;
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

/* --- Test cases --------------------------------------------------------- */

/* TC1: set_metadata(32) after add_group (binaural) — should succeed */
static int test_set_metadata_after_add_group_binaural(void) {
  TEST_START("TC1: set_metadata(32) after add_group (binaural)");

  oar_t *oar = NULL;
  uint32_t out_channels = 0;
  float *input_data = NULL;
  TEST_ASSERT(
      create_binaural_oar_with_element(&oar, &out_channels, &input_data) == 0,
      "Failed to setup binaural OAR");

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  TEST_ASSERT(ret == 0, "set_metadata(32) failed after add_group (binaural)");

  free(input_data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC2: set_metadata(512) after add_group (equal, no-op) — should succeed */
static int test_set_metadata_equal_after_add_group(void) {
  TEST_START("TC2: set_metadata(512) after add_group (equal, no-op)");

  oar_t *oar = NULL;
  uint32_t out_channels = 0;
  float *input_data = NULL;
  TEST_ASSERT(
      create_binaural_oar_with_element(&oar, &out_channels, &input_data) == 0,
      "Failed to setup binaural OAR");

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SAMPLES_PER_CHANNEL);
  TEST_ASSERT(ret == 0, "set_metadata(512) returned error");

  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) == 0,
      "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "render output is silent");
  return TEST_PASS;
}

/* TC3: set_metadata(32) with stereo layout — should succeed */
static int test_set_metadata_stereo(void) {
  TEST_START("TC3: set_metadata(32) with stereo layout");

  oar_config_t config =
      create_config(ck_oar_layout_stereo, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  uint32_t out_channels = 0;
  float *input_data = NULL;
  TEST_ASSERT(add_object_element(oar, &out_channels, &input_data, 1) == 0,
              "add_object_element failed");

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  TEST_ASSERT(ret == 0, "set_metadata(32) on stereo returned error");

  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) == 0,
      "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "render output is silent");
  return TEST_PASS;
}

/* TC4: set_metadata(32) before add_group (binaural) — should succeed */
static int test_set_metadata_before_add_group_binaural(void) {
  TEST_START("TC4: set_metadata(32) before add_group (binaural)");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  TEST_ASSERT(ret == 0, "set_metadata(32) before add_group returned error");

  uint32_t out_channels = 0;
  float *input_data = NULL;
  TEST_ASSERT(add_object_element(oar, &out_channels, &input_data, 1) == 0,
              "add_object_element failed");

  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) == 0,
      "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "output is silent after sub-frame rendering");
  return TEST_PASS;
}

/* TC5: default (no set_metadata call, binaural) — backward compatibility */
static int test_default_no_set_metadata_binaural(void) {
  TEST_START("TC5: default (no set_metadata call, binaural)");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  uint32_t out_channels = 0;
  float *input_data = NULL;
  TEST_ASSERT(add_object_element(oar, &out_channels, &input_data, 1) == 0,
              "add_object_element failed");

  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) == 0,
      "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "output is silent in default path");
  return TEST_PASS;
}

/* TC6: invalid parameters — NULL oar and unsupported metadata type */
static int test_invalid_parameters(void) {
  TEST_START("TC6: invalid parameters");

  int ret = oar_set_metadata_unit_to_process(NULL, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  TEST_ASSERT(ret != 0, "set_metadata(NULL) succeeded, expected error");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  ret = oar_set_metadata_unit_to_process(oar, ck_metadata_gain,
                                         TEST_SUB_FRAME_SAMPLES);
  TEST_ASSERT(ret != 0,
              "set_metadata(ck_metadata_gain) succeeded, expected error");

  oar_destroy(oar);
  return TEST_PASS;
}

/* TC7: multiple set_metadata before add_group — last value wins */
static int test_multiple_set_metadata_before_add_group(void) {
  TEST_START("TC7: multiple set_metadata before add_group");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  TEST_ASSERT(ret == 0, "first set_metadata(32) returned error");

  ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                         TEST_SUB_FRAME_SAMPLES_ALT);
  TEST_ASSERT(ret == 0, "second set_metadata(64) returned error");

  uint32_t out_channels = 0;
  float *input_data = NULL;
  TEST_ASSERT(add_object_element(oar, &out_channels, &input_data, 1) == 0,
              "add_object_element failed");

  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) == 0,
      "alloc_audio_block failed");

  int result = render_and_check_non_silent(oar, &output);

  free(input_data);
  free(output.data);
  oar_destroy(oar);

  TEST_ASSERT(result == 0, "output is silent after multiple set_metadata");
  return TEST_PASS;
}

/* TC8: binaural, non-divisor samples → expect success */
static int test_binaural_non_divisor_accepted(void) {
  TEST_START("TC8: binaural non-divisor accepted");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int ret =
      oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions, 100);
  oar_destroy(oar);

  TEST_ASSERT(ret == 0, "non-divisor 100 rejected for binaural");
  return TEST_PASS;
}

/* TC9: zero samples → expect error */
static int test_zero_samples_rejected(void) {
  TEST_START("TC9: zero samples rejected");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int ret =
      oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions, 0);
  oar_destroy(oar);

  TEST_ASSERT(ret != 0, "zero samples accepted");
  return TEST_PASS;
}

/* TC10: samples > frame → expect error */
static int test_exceeds_frame_rejected(void) {
  TEST_START("TC10: samples > frame rejected");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SAMPLES_PER_CHANNEL * 2);
  oar_destroy(oar);

  TEST_ASSERT(ret != 0, "samples > frame accepted");
  return TEST_PASS;
}

/* TC11: stereo, non-divisor samples → expect success */
static int test_stereo_non_divisor_accepted(void) {
  TEST_START("TC11: stereo non-divisor accepted");

  oar_config_t config =
      create_config(ck_oar_layout_stereo, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int ret =
      oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions, 100);
  oar_destroy(oar);

  TEST_ASSERT(ret == 0, "non-divisor 100 rejected for stereo");
  return TEST_PASS;
}

/* TC12: stereo, varying positions across sub-frames → verify position update */
static int test_stereo_varying_positions_sub_frame(void) {
  TEST_START("TC12: stereo varying positions sub-frame");

  oar_config_t config =
      create_config(ck_oar_layout_stereo, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int ret = oar_set_metadata_unit_to_process(oar, ck_metadata_object_positions,
                                             TEST_SUB_FRAME_SAMPLES);
  TEST_ASSERT(ret == 0, "set_metadata(32) failed");

  uint32_t out_channels = 0;
  float *input_data = NULL;
  TEST_ASSERT(add_object_element(oar, &out_channels, &input_data, 0) == 0,
              "add_object_element failed");

  /* Provide two position metadata entries: first half left, second half right.
   * First half: +80° (far left) → ch0 dominant
   * Second half: -80° (far right) → ch1 dominant */
  polar_t pos_left = {80.0f, 0.0f, 1.0f};
  polar_t pos_right = {-80.0f, 0.0f, 1.0f};
  oar_metadata_t *meta_left =
      create_object_metadata(&pos_left, 1, TEST_SAMPLES_PER_CHANNEL / 2);
  oar_metadata_t *meta_right =
      create_object_metadata(&pos_right, 1, TEST_SAMPLES_PER_CHANNEL / 2);
  TEST_ASSERT(meta_left != NULL && meta_right != NULL,
              "create_object_metadata returned NULL");

  ret = oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, meta_left);
  free(meta_left);
  TEST_ASSERT(ret == 0, "oar_update_audio_element_metadata (left) failed");

  ret = oar_update_audio_element_metadata(oar, TEST_ELEMENT_ID, meta_right);
  free(meta_right);
  TEST_ASSERT(ret == 0, "oar_update_audio_element_metadata (right) failed");

  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) == 0,
      "alloc_audio_block failed");

  ret = oar_render(oar, &output);
  TEST_ASSERT(ret == 0, "oar_render returned error");

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

  TEST_ASSERT(left_first_half > left_second_half,
              "sub-frame position update not reflected in left channel");
  TEST_ASSERT(right_second_half > right_first_half,
              "sub-frame position update not reflected in right channel");

  free(input_data);
  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC13: binaural, mirrored positions (±80°) → outputs differ + ILD flips.
 *
 * Primary assertion: two renders of identical input at mirrored azimuths must
 * differ. If object positions never reach OBR, both render at the creation
 * default (front-center) and the outputs are bit-identical, regardless of the
 * HRIR set. Secondary assertion: the interaural energy asymmetry must follow
 * the azimuth sign (positive azimuth = left, as in TC12). */
static int test_binaural_position_effect(void) {
  TEST_START("TC13: binaural mirrored positions (±80°)");

  uint32_t total = 2 * TEST_SAMPLES_PER_CHANNEL;
  float *render_left = (float *)calloc(total, sizeof(float));
  float *render_right = (float *)calloc(total, sizeof(float));
  TEST_ASSERT(render_left != NULL && render_right != NULL, "calloc failed");

  /* Render the second frame so the measurement is past the onset. */
  TEST_ASSERT(render_binaural_frames(TEST_SIDE_AZIMUTH, 2, render_left) == 0,
              "binaural render (+azimuth) failed");
  TEST_ASSERT(render_binaural_frames(-TEST_SIDE_AZIMUTH, 2, render_right) == 0,
              "binaural render (-azimuth) failed");

  double sig = rms_of(render_left, total);
  double diff = rms_diff(render_left, render_right, total);

  TEST_ASSERT(sig >= 1e-6, "binaural output is silent");
  TEST_ASSERT(diff >= TEST_MIN_RELATIVE_DIFF * sig,
              "renders at +80° and -80° are near-identical; positions not "
              "delivered to OBR");

  double e_l_pos = sum_abs(render_left, TEST_SAMPLES_PER_CHANNEL);
  double e_r_pos =
      sum_abs(render_left + TEST_SAMPLES_PER_CHANNEL, TEST_SAMPLES_PER_CHANNEL);
  double e_l_neg = sum_abs(render_right, TEST_SAMPLES_PER_CHANNEL);
  double e_r_neg = sum_abs(render_right + TEST_SAMPLES_PER_CHANNEL,
                           TEST_SAMPLES_PER_CHANNEL);

  TEST_ASSERT(e_l_pos > TEST_ILD_RATIO * e_r_pos,
              "interaural asymmetry does not follow azimuth sign (+80°)");
  TEST_ASSERT(e_r_neg > TEST_ILD_RATIO * e_l_neg,
              "interaural asymmetry does not follow azimuth sign (-80°)");

  free(render_left);
  free(render_right);
  return TEST_PASS;
}

/* TC14: binaural, position update between frames takes effect.
 *
 * OBR's buffer_size_per_channel is fixed at creation, so binaural position
 * updates are frame-granular — but they must still be applied on the next
 * frame. Render at +80°, update to -80°, and assert the interaural balance
 * flips. The frame right after the update is skipped so any position
 * crossfade inside OBR does not blur the measurement. */
static int test_binaural_position_update_across_frames(void) {
  TEST_START("TC14: binaural position update across frames");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");
  TEST_ASSERT(add_object_element_to_group(oar, gid, TEST_ELEMENT_ID, 1) == 0,
              "add_object_element_to_group failed");

  uint32_t out_channels = oar_get_number_of_output_channels(oar);
  TEST_ASSERT_EQ(out_channels, (uint32_t)2,
                 "expected 2 binaural output channels");

  uint32_t total = out_channels * TEST_SAMPLES_PER_CHANNEL;
  float *frame1 = (float *)calloc(total, sizeof(float));
  float *frame3 = (float *)calloc(total, sizeof(float));
  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) == 0,
      "alloc_audio_block failed");
  TEST_ASSERT(frame1 != NULL && frame3 != NULL, "calloc failed");

  /* Frame 1 at +80°, frames 2 and 3 at -80°; measure frames 1 and 3. */
  float azimuths[3] = {TEST_SIDE_AZIMUTH, -TEST_SIDE_AZIMUTH,
                       -TEST_SIDE_AZIMUTH};
  for (int f = 0; f < 3; ++f) {
    TEST_ASSERT(submit_stimulus_block(oar, TEST_ELEMENT_ID,
                                      TEST_SAMPLES_PER_CHANNEL) == 0,
                "data update failed");
    TEST_ASSERT(submit_position(oar, TEST_ELEMENT_ID, azimuths[f],
                                TEST_SAMPLES_PER_CHANNEL) == 0,
                "metadata update failed");
    memset(output.data, 0, total * sizeof(float));
    TEST_ASSERT(oar_render(oar, &output) == 0, "oar_render failed");
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

  TEST_ASSERT(sig >= 1e-6, "binaural output is silent");
  TEST_ASSERT(diff >= TEST_MIN_RELATIVE_DIFF * sig,
              "output did not change after the position update");
  TEST_ASSERT(
      e_l_f1 > TEST_ILD_RATIO * e_r_f1,
      "interaural balance did not flip: frame1 (+80°) E_L not dominant");
  TEST_ASSERT(
      e_r_f3 > TEST_ILD_RATIO * e_l_f3,
      "interaural balance did not flip: frame3 (-80°) E_R not dominant");

  free(frame1);
  free(frame3);
  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC15: binaural, input block smaller than the configured frame must be
 * rejected by oar_update_audio_element_data — not fed to OBR, whose
 * ABSL_CHECK_EQ on the buffer size aborts the whole process. */
static int test_undersized_block_rejected(void) {
  TEST_START("TC15: undersized input block rejected");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");
  TEST_ASSERT(add_object_element_to_group(oar, gid, TEST_ELEMENT_ID, 1) == 0,
              "add_object_element_to_group failed");

  uint32_t out_channels = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  TEST_ASSERT(
      alloc_audio_block(out_channels, TEST_SAMPLES_PER_CHANNEL, &output) == 0,
      "alloc_audio_block failed");

  int ret =
      submit_stimulus_block(oar, TEST_ELEMENT_ID, TEST_SAMPLES_PER_CHANNEL / 2);
  TEST_ASSERT(ret != 0, "undersized block was accepted");

  /* The renderer must remain usable after the rejection. */
  TEST_ASSERT(submit_stimulus_block(oar, TEST_ELEMENT_ID,
                                    TEST_SAMPLES_PER_CHANNEL) == 0,
              "full-size block rejected after undersized rejection");
  TEST_ASSERT(oar_render(oar, &output) == 0,
              "renderer unusable after rejected block");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC16: mismatched block sizes across two elements must be rejected. If both
 * submissions are accepted, add_data memcpys the second element's larger
 * block with the first block's stride/allocation — a heap buffer overflow. */
static int test_mismatched_blocks_across_elements(void) {
  TEST_START("TC16: mismatched blocks across elements rejected");

  oar_config_t config =
      create_config(ck_oar_layout_binaural, TEST_SAMPLES_PER_CHANNEL, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");
  TEST_ASSERT(add_object_element_to_group(oar, gid, TEST_ELEMENT_ID, 1) == 0,
              "add element 1 failed");
  TEST_ASSERT(add_object_element_to_group(oar, gid, TEST_ELEMENT_ID_2, 1) == 0,
              "add element 2 failed");

  int ret_a =
      submit_stimulus_block(oar, TEST_ELEMENT_ID, TEST_SAMPLES_PER_CHANNEL / 2);
  int ret_b =
      submit_stimulus_block(oar, TEST_ELEMENT_ID_2, TEST_SAMPLES_PER_CHANNEL);

  TEST_ASSERT(
      !(ret_a == 0 && ret_b == 0),
      "mismatched block sizes both accepted — heap buffer overflow risk");

  oar_destroy(oar);
  return TEST_PASS;
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
static int test_multi_element_rendering(void) {
  TEST_START("TC17: multi-element + multi-object rendering");

  uint32_t total = 2 * TEST_SAMPLES_PER_CHANNEL;
  float *render_opposite = (float *)calloc(total, sizeof(float));
  float *render_same = (float *)calloc(total, sizeof(float));
  TEST_ASSERT(render_opposite != NULL && render_same != NULL, "calloc failed");

  /* Render with split objects: element 1 at +80°, element 2's two objects
   * at +80° and -80°. Element 2's objects span both sides, weakening ILD. */
  TEST_ASSERT(render_multi_element_multi_object(
                  TEST_SIDE_AZIMUTH, TEST_SIDE_AZIMUTH, -TEST_SIDE_AZIMUTH,
                  render_opposite) == 0,
              "multi-element render (split) failed");

  /* Render with all objects on same side: element 1 at +80°, element 2's
   * two objects both at +80°. All three objects on the left → strong ILD. */
  TEST_ASSERT(
      render_multi_element_multi_object(TEST_SIDE_AZIMUTH, TEST_SIDE_AZIMUTH,
                                        TEST_SIDE_AZIMUTH, render_same) == 0,
      "multi-element render (same) failed");

  /* 1. Both renders must be non-silent. */
  double sig_opposite = rms_of(render_opposite, total);
  double sig_same = rms_of(render_same, total);
  TEST_ASSERT(sig_opposite >= 1e-6, "opposite-azimuth output is silent");
  TEST_ASSERT(sig_same >= 1e-6, "same-azimuth output is silent");

  /* 2. When both elements are at +80°, the left ear should be strongly
   *    dominant (both objects on the left). When they are at opposite
   *    azimuths, the ILD should be weaker (one object each side). */
  double e_l_same = sum_abs(render_same, TEST_SAMPLES_PER_CHANNEL);
  double e_r_same =
      sum_abs(render_same + TEST_SAMPLES_PER_CHANNEL, TEST_SAMPLES_PER_CHANNEL);
  double e_l_opp = sum_abs(render_opposite, TEST_SAMPLES_PER_CHANNEL);
  double e_r_opp = sum_abs(render_opposite + TEST_SAMPLES_PER_CHANNEL,
                           TEST_SAMPLES_PER_CHANNEL);

  double ild_same = fabs(e_l_same - e_r_same) / (e_l_same + e_r_same + 1e-12);
  double ild_opp = fabs(e_l_opp - e_r_opp) / (e_l_opp + e_r_opp + 1e-12);

  TEST_ASSERT(ild_same > ild_opp,
              "ILD not stronger when both elements share azimuth");

  /* 3. The two renders must differ (proves element 2's data affects output). */
  double diff = rms_diff(render_opposite, render_same, total);
  TEST_ASSERT(diff >= TEST_MIN_RELATIVE_DIFF * sig_same,
              "opposite and same-azimuth renders are near-identical");

  free(render_opposite);
  free(render_same);
  return TEST_PASS;
}

/* --- Test table --------------------------------------------------------- */

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "set_metadata after add_group",
               test_set_metadata_after_add_group_binaural),
    TEST_ENTRY("TC2", "set_metadata equal, no-op",
               test_set_metadata_equal_after_add_group),
    TEST_ENTRY("TC3", "stereo + set_metadata", test_set_metadata_stereo),
    TEST_ENTRY("TC4", "set_metadata before add_group",
               test_set_metadata_before_add_group_binaural),
    TEST_ENTRY("TC5", "default, no set_metadata",
               test_default_no_set_metadata_binaural),
    TEST_ENTRY("TC6", "invalid parameters", test_invalid_parameters),
    TEST_ENTRY("TC7", "multiple set_metadata",
               test_multiple_set_metadata_before_add_group),
    TEST_ENTRY("TC8", "binaural non-divisor",
               test_binaural_non_divisor_accepted),
    TEST_ENTRY("TC9", "zero samples", test_zero_samples_rejected),
    TEST_ENTRY("TC10", "samples > frame", test_exceeds_frame_rejected),
    TEST_ENTRY("TC11", "stereo non-divisor", test_stereo_non_divisor_accepted),
    TEST_ENTRY("TC12", "stereo varying positions",
               test_stereo_varying_positions_sub_frame),
    TEST_ENTRY("TC13", "binaural mirrored positions",
               test_binaural_position_effect),
    TEST_ENTRY("TC14", "binaural update across frames",
               test_binaural_position_update_across_frames),
    TEST_ENTRY("TC15", "undersized block rejected",
               test_undersized_block_rejected),
    TEST_ENTRY("TC16", "mismatched blocks rejected",
               test_mismatched_blocks_across_elements),
    TEST_ENTRY("TC17", "multi-element + multi-object rendering",
               test_multi_element_rendering),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
