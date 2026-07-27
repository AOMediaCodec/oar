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

/*
 * Regression test for out-of-range object azimuths in the OLR.
 *
 * An azimuth outside (-180, 180] is an alias of a canonical angle (+270 is
 * -90). The renderer used to pass such angles through unfolded, and the
 * closest-speaker fallback compared angles linearly, so on a stereo layout a
 * source at +270 collapsed onto the LEFT speaker even though -90 is on the
 * right. This test renders a sine at aliased azimuth pairs and requires
 * bit-identical output, plus checks that lateral sources land on opposite
 * output channels.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "oar.h"

#define SAMPLES_PER_CHANNEL 256
#define SAMPLE_RATE 48000

// Renders one 440 Hz object at the given azimuth to a stereo output block.
// Returns 0 on success; the caller owns out->data.
static int render_object_at_azimuth(float azimuth, oar_audio_block_t* out) {
  oar_config_t cfg;
  cfg.target_layout = ck_oar_layout_stereo;
  cfg.samples_per_channel = SAMPLES_PER_CHANNEL;
  cfg.sampling_rate = SAMPLE_RATE;

  oar_t* oar = oar_create(&cfg);
  if (!oar) {
    fprintf(stderr, "oar_create failed\n");
    return -1;
  }

  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr, "oar_add_audio_group failed: %d\n", group_id);
    oar_destroy(oar);
    return -1;
  }

  oar_audio_element_config_t element_cfg;
  memset(&element_cfg, 0, sizeof(element_cfg));
  element_cfg.type = ck_object_based;
  element_cfg.obc.num_objects = 1;

  int ret = oar_add_audio_element(oar, group_id, 1, &element_cfg);
  if (ret != 0) {
    fprintf(stderr, "oar_add_audio_element failed: %d\n", ret);
    oar_destroy(oar);
    return -1;
  }

  float input[SAMPLES_PER_CHANNEL];
  for (int i = 0; i < SAMPLES_PER_CHANNEL; ++i) {
    input[i] = (float)sin(2.0 * M_PI * 440.0 * i / SAMPLE_RATE);
  }

  oar_audio_block_t input_block;
  input_block.channels = 1;
  input_block.samples_per_channel = SAMPLES_PER_CHANNEL;
  input_block.data = input;
  ret = oar_update_audio_element_data(oar, 1, &input_block);
  if (ret != 0) {
    fprintf(stderr, "oar_update_audio_element_data failed: %d\n", ret);
    oar_destroy(oar);
    return -1;
  }

  oar_metadata_t metadata;
  memset(&metadata, 0, sizeof(metadata));
  metadata.type = ck_metadata_object_positions;
  metadata.duration = SAMPLES_PER_CHANNEL;
  metadata.object_positions.param_type = ck_param_constant;
  metadata.object_positions.position_type = ck_polar;
  metadata.object_positions.num_objects = 1;
  metadata.object_positions.polar_positions[0].azimuth = azimuth;
  metadata.object_positions.polar_positions[0].elevation = 0.f;
  metadata.object_positions.polar_positions[0].distance = 1.f;
  ret = oar_update_audio_element_metadata(oar, 1, &metadata);
  if (ret != 0) {
    fprintf(stderr, "oar_update_audio_element_metadata failed: %d\n", ret);
    oar_destroy(oar);
    return -1;
  }

  out->channels = oar_get_number_of_output_channels(oar);
  out->samples_per_channel = oar_get_samples_per_channel(oar);
  out->data = (float*)calloc(out->channels * out->samples_per_channel,
                             sizeof(float));
  if (!out->data) {
    oar_destroy(oar);
    return -1;
  }

  ret = oar_render(oar, out);
  oar_destroy(oar);
  if (ret != 0) {
    fprintf(stderr, "oar_render failed: %d\n", ret);
    free(out->data);
    out->data = NULL;
    return -1;
  }
  return 0;
}

static double channel_energy(const oar_audio_block_t* block, uint32_t channel) {
  double energy = 0.0;
  const float* p = block->data + channel * block->samples_per_channel;
  for (uint32_t i = 0; i < block->samples_per_channel; ++i) {
    energy += (double)p[i] * p[i];
  }
  return energy;
}

static double max_abs_difference(const oar_audio_block_t* a,
                                 const oar_audio_block_t* b) {
  double max_diff = 0.0;
  uint32_t n = a->channels * a->samples_per_channel;
  for (uint32_t i = 0; i < n; ++i) {
    double diff = fabs((double)a->data[i] - b->data[i]);
    if (diff > max_diff) max_diff = diff;
  }
  return max_diff;
}

// Renders `azimuth` and its alias `azimuth + turns*360` and requires the two
// outputs to be identical. Returns 0 on pass.
static int check_alias_pair(float azimuth, int turns) {
  float alias = azimuth + 360.f * turns;
  oar_audio_block_t ref = {0}, aliased = {0};
  int failed = 0;

  if (render_object_at_azimuth(azimuth, &ref) != 0 ||
      render_object_at_azimuth(alias, &aliased) != 0) {
    free(ref.data);
    free(aliased.data);
    return 1;
  }

  if (channel_energy(&ref, 0) + channel_energy(&ref, 1) < 1e-6) {
    fprintf(stderr, "FAIL: azimuth %.1f rendered silence\n", azimuth);
    failed = 1;
  }

  double diff = max_abs_difference(&ref, &aliased);
  if (diff > 1e-6) {
    fprintf(stderr,
            "FAIL: azimuth %.1f and alias %.1f differ (max abs diff %g)\n",
            azimuth, alias, diff);
    failed = 1;
  } else {
    printf("PASS: azimuth %.1f == alias %.1f (max abs diff %g)\n", azimuth,
           alias, diff);
  }

  free(ref.data);
  free(aliased.data);
  return failed;
}

// Renders two laterally opposed azimuths and requires their dominant output
// channels to be opposite (each side collapses onto its closest speaker).
static int check_opposite_sides(float azimuth_a, float azimuth_b) {
  oar_audio_block_t a = {0}, b = {0};
  int failed = 0;

  if (render_object_at_azimuth(azimuth_a, &a) != 0 ||
      render_object_at_azimuth(azimuth_b, &b) != 0) {
    free(a.data);
    free(b.data);
    return 1;
  }

  int dominant_a = channel_energy(&a, 0) > channel_energy(&a, 1) ? 0 : 1;
  int dominant_b = channel_energy(&b, 0) > channel_energy(&b, 1) ? 0 : 1;
  if (dominant_a == dominant_b) {
    fprintf(stderr,
            "FAIL: azimuths %.1f and %.1f both favor output channel %d\n",
            azimuth_a, azimuth_b, dominant_a);
    failed = 1;
  } else {
    printf("PASS: azimuth %.1f favors channel %d, azimuth %.1f favors %d\n",
           azimuth_a, dominant_a, azimuth_b, dominant_b);
  }

  free(a.data);
  free(b.data);
  return failed;
}

int main(void) {
  int failures = 0;

  printf("Object azimuth wrapping test\n");

  // Aliased angles must render identically to their canonical forms.
  failures += check_alias_pair(-90.f, 1);   // +270 == -90
  failures += check_alias_pair(90.f, 1);    // +450 == +90
  failures += check_alias_pair(-170.f, 1);  // +190 == -170
  failures += check_alias_pair(30.f, -2);   // -690 == +30

  // Lateral sources must collapse onto opposite stereo speakers; before the
  // fix, +270 landed on the same (left) speaker as +90.
  failures += check_opposite_sides(90.f, -90.f);
  failures += check_opposite_sides(90.f, 270.f);

  if (failures) {
    fprintf(stderr, "\n%d azimuth wrapping check(s) FAILED\n", failures);
    return 1;
  }
  printf("\nAll azimuth wrapping checks passed.\n");
  return 0;
}
