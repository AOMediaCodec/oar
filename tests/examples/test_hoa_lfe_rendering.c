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

// Regression test for the 120 Hz LFE low-pass filter applied when rendering
// HOA to an LFE-bearing loudspeaker layout (requires OAR_ENABLE_HOA_LFE).
//
// The filter coefficients were previously computed for a hardcoded 48 kHz
// rate regardless of oar_config_t.sampling_rate, so at 96 kHz the effective
// cutoff landed at ~240 Hz and the LFE channel passed twice the intended
// bandwidth. The LFE level for a given tone must be invariant to the output
// sampling rate, and tones well above the cutoff must be attenuated.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "oar.h"

// Channel order of ck_oar_layout_51 is L R C LFE Ls Rs.
#define LFE_CHANNEL_INDEX 3
#define BLOCK_SIZE 256
// Enough blocks for the IIR filter to reach steady state at both rates.
#define NUM_BLOCKS 40

// Renders a sine of `tone_hz` on the W channel of a 1OA scene element to a
// 5.1 layout at `sampling_rate`, and returns the steady-state RMS of the LFE
// output channel (measured over the last quarter of the blocks). Returns a
// negative value on setup failure.
static double render_lfe_rms(uint32_t sampling_rate, double tone_hz) {
  oar_config_t config;
  config.target_layout = ck_oar_layout_51;
  config.samples_per_channel = BLOCK_SIZE;
  config.sampling_rate = sampling_rate;

  oar_t *oar = oar_create(&config);
  if (oar == NULL) {
    fprintf(stderr, "Failed to create OAR object at %u Hz.\n", sampling_rate);
    return -1.0;
  }

  oar_audio_element_config_t element_cfg;
  memset(&element_cfg, 0, sizeof(element_cfg));
  element_cfg.type = ck_scene_based;
  element_cfg.sbc.order = ck_oar_1oa;

  int group_id = oar_add_audio_group(oar);
  if (group_id < 0) {
    fprintf(stderr, "Failed to add audio group.\n");
    oar_destroy(oar);
    return -1.0;
  }
  const uint32_t element_id = 1;
  if (oar_add_audio_element(oar, group_id, element_id, &element_cfg) != 0) {
    fprintf(stderr, "Failed to add audio element.\n");
    oar_destroy(oar);
    return -1.0;
  }

  const uint32_t in_channels =
      oar_get_number_of_audio_element_channels(oar, element_id);
  const uint32_t out_channels = oar_get_number_of_output_channels(oar);

  float *input = (float *)calloc((size_t)in_channels * BLOCK_SIZE,
                                 sizeof(float));
  float *output = (float *)calloc((size_t)out_channels * BLOCK_SIZE,
                                  sizeof(float));
  if (input == NULL || output == NULL) {
    fprintf(stderr, "Allocation failure.\n");
    free(input);
    free(output);
    oar_destroy(oar);
    return -1.0;
  }

  oar_audio_block_t input_block = {
      .channels = in_channels, .samples_per_channel = BLOCK_SIZE,
      .data = input};
  oar_audio_block_t output_block = {
      .channels = out_channels, .samples_per_channel = BLOCK_SIZE,
      .data = output};

  double sum_squares = 0.0;
  size_t num_measured = 0;
  for (int block = 0; block < NUM_BLOCKS; ++block) {
    // Continuous-phase tone on W (channel 0); other channels stay silent.
    for (int i = 0; i < BLOCK_SIZE; ++i) {
      const double t =
          (double)(block * BLOCK_SIZE + i) / (double)sampling_rate;
      input[i] = (float)sin(2.0 * M_PI * tone_hz * t);
    }
    if (oar_update_audio_element_data(oar, element_id, &input_block) != 0 ||
        oar_render(oar, &output_block) != 0) {
      fprintf(stderr, "Render failure at %u Hz.\n", sampling_rate);
      free(input);
      free(output);
      oar_destroy(oar);
      return -1.0;
    }
    if (block >= (3 * NUM_BLOCKS) / 4) {  // Steady state only.
      const float *lfe = output + (size_t)LFE_CHANNEL_INDEX * BLOCK_SIZE;
      for (int i = 0; i < BLOCK_SIZE; ++i) {
        sum_squares += (double)lfe[i] * lfe[i];
        num_measured++;
      }
    }
  }

  free(input);
  free(output);
  oar_destroy(oar);
  return sqrt(sum_squares / (double)num_measured);
}

int main() {
  printf("Running HOA LFE low-pass sampling-rate test...\n\n");
  int failures = 0;

  // A tone near the 120 Hz cutoff, where a mis-scaled cutoff (240 Hz at a
  // 96 kHz output rate with the old hardcoded coefficients) changes the
  // level the most.
  const double kProbeToneHz = 200.0;
  const double rms_48k = render_lfe_rms(48000, kProbeToneHz);
  const double rms_96k = render_lfe_rms(96000, kProbeToneHz);
  const double rms_44k = render_lfe_rms(44100, kProbeToneHz);
  if (rms_48k <= 0.0 || rms_96k <= 0.0 || rms_44k <= 0.0) {
    fprintf(stderr, "FAIL: setup/render failed or LFE channel is silent.\n");
    return -1;
  }
  printf("LFE RMS for %.0f Hz tone: 44.1k=%.6f 48k=%.6f 96k=%.6f\n",
         kProbeToneHz, rms_44k, rms_48k, rms_96k);

  // The LFE level must be invariant to the output sampling rate. With the
  // hardcoded-48k bug, the 96 kHz ratio is ~2.4x.
  const double ratio_96k = rms_96k / rms_48k;
  const double ratio_44k = rms_44k / rms_48k;
  if (fabs(ratio_96k - 1.0) > 0.05 || fabs(ratio_44k - 1.0) > 0.05) {
    fprintf(stderr,
            "FAIL: LFE level depends on the sampling rate "
            "(96k/48k=%.3f, 44.1k/48k=%.3f, expected 1.0 +-0.05).\n",
            ratio_96k, ratio_44k);
    failures++;
  } else {
    printf("PASS: LFE level is invariant to the sampling rate "
           "(96k/48k=%.3f, 44.1k/48k=%.3f).\n",
           ratio_96k, ratio_44k);
  }

  // The filter must attenuate well above the 120 Hz cutoff at every rate:
  // measured, a 1 kHz tone sits ~29 dB below the 200 Hz tone. With the
  // hardcoded-48k bug, the 96 kHz attenuation degraded to ~25.5 dB.
  const double kHighToneHz = 1000.0;
  for (int i = 0; i < 3; ++i) {
    const uint32_t rates[3] = {44100, 48000, 96000};
    const double rms_low = render_lfe_rms(rates[i], kProbeToneHz);
    const double rms_high = render_lfe_rms(rates[i], kHighToneHz);
    if (rms_low <= 0.0 || rms_high <= 0.0) {
      fprintf(stderr, "FAIL: render failed at %u Hz.\n", rates[i]);
      return -1;
    }
    const double attenuation_db = 20.0 * log10(rms_high / rms_low);
    printf("1 kHz vs 200 Hz LFE level at %u Hz: %.1f dB\n", rates[i],
           attenuation_db);
    if (attenuation_db > -25.0) {
      fprintf(stderr,
              "FAIL: insufficient LFE low-pass attenuation at %u Hz "
              "(%.1f dB, expected < -25 dB).\n",
              rates[i], attenuation_db);
      failures++;
    }
  }

  if (failures > 0) {
    fprintf(stderr, "\nHOA LFE rendering test FAILED (%d).\n", failures);
    return -1;
  }
  printf("\nHOA LFE rendering test completed successfully.\n");
  return 0;
}
