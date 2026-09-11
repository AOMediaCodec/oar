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
 * @file test_peak_limiter_oar_api.c
 * @brief OAR API tests for peak limiter (TC1 ~ TC14)
 *
 * End-to-end tests through oar_create/enable_limiter/render/flush/
 * set_limiter_threshold/get_limiter_delay API.
 *
 * Test matrix:
 *   TC1:  delay_size < samples_per_channel (emit-priming)
 *   TC2:  delay_size > samples_per_channel (emit-priming)
 *   TC3:  limiter enable/disable contrast with full limit check
 *   TC4:  set_limiter_threshold level verification (two thresholds)
 *   TC5:  set_threshold with limiter disabled
 *   TC6:  threshold out-of-range clamp
 *   TC7:  get_limiter_delay verification (multi-frequency)
 *   TC8:  flush with limiter disabled
 *   TC9:  repeated flush
 *   TC10: flush NULL parameter error handling
 *   TC11: NULL API calls (L1 delegation)
 *   TC12: block reuse without field restore
 *   TC13: planar stride integrity
 *   TC14: fixed-block render loop with sample conservation
 */

#include <math.h>
#include <stdlib.h>

#include "test_framework.h"
#include "test_helpers.h"

/* VBAP gain for center-panned source (azimuth=0) in stereo layout.
 * Each speaker at ±30°, source at 0° → per-channel gain ≈ 0.707.
 * This is a conservative estimate; actual gain may differ. */
#define RENDER_GAIN_CENTER_STEREO 0.707f

/* --- Local helpers ------------------------------------------------------ */

/** Create an OAR instance with a single object element and sine data at
 *  the specified amplitude. */
static oar_t *create_oar_with_object(uint32_t spc, uint32_t sr, float freq,
                                     float amplitude) {
  oar_config_t cfg = create_config(ck_oar_layout_stereo, spc, sr);
  oar_t *oar = oar_create(&cfg);
  if (!oar) return NULL;

  int gid = oar_add_audio_group(oar);
  if (gid < 0) {
    oar_destroy(oar);
    return NULL;
  }

  oar_audio_element_config_t ecfg = create_object_element_config(1);
  int ret = oar_add_audio_element(oar, gid, 1, &ecfg);
  if (ret != 0) {
    oar_destroy(oar);
    return NULL;
  }

  uint32_t in_ch = oar_get_number_of_audio_element_channels(oar, 1);
  oar_audio_block_t input;
  if (alloc_audio_block(in_ch, spc, &input) != 0) {
    oar_destroy(oar);
    return NULL;
  }

  generate_sine_ampl(input.data, spc, freq, (float)sr, amplitude);

  ret = oar_update_audio_element_data(oar, 1, &input);
  if (ret != 0) {
    free(input.data);
    oar_destroy(oar);
    return NULL;
  }
  free(input.data);

  polar_t pos = {0.0f, 0.0f, 1.0f};
  oar_metadata_t *meta = create_object_metadata(&pos, 1, spc);
  if (!meta) {
    oar_destroy(oar);
    return NULL;
  }

  ret = oar_update_audio_element_metadata(oar, 1, meta);
  free(meta);
  if (ret != 0) {
    oar_destroy(oar);
    return NULL;
  }

  return oar;
}

/** Re-feed audio data and metadata for subsequent render.
 * Required because has_data flag is set to 0 after each oar_render(). */
static int update_object_data(oar_t *oar, uint32_t spc, uint32_t sr, float freq,
                              float amplitude) {
  uint32_t in_ch = oar_get_number_of_audio_element_channels(oar, 1);
  oar_audio_block_t input;
  if (alloc_audio_block(in_ch, spc, &input) != 0) return -1;
  generate_sine_ampl(input.data, spc, freq, (float)sr, amplitude);
  int ret = oar_update_audio_element_data(oar, 1, &input);
  free(input.data);
  if (ret != ck_oar_ok) return -1;

  polar_t pos = {0.0f, 0.0f, 1.0f};
  oar_metadata_t *meta = create_object_metadata(&pos, 1, spc);
  if (!meta) return -1;
  ret = oar_update_audio_element_metadata(oar, 1, meta);
  free(meta);
  return (ret == ck_oar_ok) ? 0 : -1;
}

/** Render two frames to reach steady state (emit-priming: the first frame
 * contains delay_size priming zeros). Requires data pre-fed by
 * create_oar_with_object(); re-feeds data between the two renders.
 * @return 0 on success, -1 on failure. */
static int render_to_steady_state(oar_t *oar, oar_audio_block_t *output,
                                  uint32_t spc, uint32_t sr, float freq,
                                  float amplitude) {
  int ret = oar_render(oar, output);
  if (ret != ck_oar_ok) return -1;
  if (update_object_data(oar, spc, sr, freq, amplitude) != 0) return -1;
  ret = oar_render(oar, output);
  return (ret == ck_oar_ok) ? 0 : -1;
}

/** Find the maximum absolute value across all channels in an audio block. */
static float find_output_peak(const oar_audio_block_t *out) {
  uint32_t total = out->channels * out->samples_per_channel;
  float peak = 0.0f;
  for (uint32_t i = 0; i < total; ++i) {
    float v = fabsf(out->data[i]);
    if (v > peak) peak = v;
  }
  return peak;
}

/* --- Threshold-level helper --------------------------------------------- */

/** Create an OAR instance with a sine input, enable limiter at the given
 *  threshold, render to steady state, and return the output peak.
 *
 *  Encapsulates the create → enable → set_threshold → render pattern so that
 *  tests comparing multiple threshold levels can do so concisely.
 *
 *  @return Output peak (linear amplitude), or -1.0f on failure. */
static float render_peak_at_threshold(uint32_t spc, uint32_t sr, float freq,
                                      float amplitude, float threshold_db) {
  oar_t *oar = create_oar_with_object(spc, sr, freq, amplitude);
  if (!oar) return -1.0f;

  if (oar_enable_limiter(oar, 1) != ck_oar_ok) {
    oar_destroy(oar);
    return -1.0f;
  }

  if (oar_set_limiter_threshold(oar, threshold_db) != ck_oar_ok) {
    oar_destroy(oar);
    return -1.0f;
  }

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  if (alloc_audio_block(out_ch, spc, &output) != 0) {
    oar_destroy(oar);
    return -1.0f;
  }

  int ok = render_to_steady_state(oar, &output, spc, sr, freq, amplitude);
  oar_destroy(oar);

  float peak = (ok == 0) ? find_output_peak(&output) : -1.0f;
  free(output.data);
  return peak;
}

/* --- Test cases --------------------------------------------------------- */

/* --- TC1 local helpers -------------------------------------------------- */

/** Verify emit-priming pattern in a rendered block: first delay_size samples
 *  per channel are priming zeros, remaining (spc - delay) samples are
 *  non-zero audio.
 *  @return 1 if pattern matches, 0 otherwise. */
static int check_emit_priming(const oar_audio_block_t *out, uint32_t delay) {
  uint32_t spc = out->samples_per_channel;
  for (uint32_t ch = 0; ch < out->channels; ++ch) {
    float *ch_data = out->data + ch * spc;
    if (!is_all_zero(ch_data, delay)) return 0;
    if (!is_output_non_silent(ch_data + delay, spc - delay)) return 0;
  }
  return 1;
}

/** Verify output is non-silent and all samples are finite (not NaN/Inf).
 *  Used for flush output validation where limit checking is not required.
 *  @return 1 if valid, 0 otherwise. */
static int check_output_nonzero_finite(const oar_audio_block_t *out) {
  uint32_t total = out->channels * out->samples_per_channel;
  if (is_all_zero(out->data, total)) return 0;
  for (uint32_t i = 0; i < total; ++i) {
    if (!isfinite(out->data[i])) return 0;
  }
  return 1;
}

/* TC1: Emit-priming — delay_size < samples_per_channel
 *
 * Sub-threshold input (amplitude 1.0); verifies priming, not limiting.
 * - Render 1: full spc; first delay_size samples are priming zeros,
 *   remaining samples are non-zero audio.
 * - Render 2: full spc (steady state).
 * - Flush: returns delay_size samples, non-zero and finite.
 */
static int test_b1_delay_less_than_frame(void) {
  TEST_START("TC1: delay < frame");
  const uint32_t spc = 960;
  oar_t *oar = create_oar_with_object(spc, TEST_SAMPLING_RATE, 440.0f, 1.0f);
  TEST_ASSERT(oar != NULL, "create oar failed");

  TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok, "enable_limiter failed");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  int delay = oar_get_limiter_delay(oar);
  TEST_ASSERT(delay > 0, "limiter delay should be positive");
  TEST_ASSERT((uint32_t)delay < spc,
              "delay should be less than spc for this TC");
  /* Allocate buffer large enough for both render (spc) and flush (delay) */
  uint32_t buf_spc = (spc > (uint32_t)delay) ? spc : (uint32_t)delay;
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, buf_spc, &output) == 0, "alloc failed");

  /* First render: full spc (emit-priming, first delay samples are zeros) */
  int ret = oar_render(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "render failed");
  TEST_ASSERT(output.samples_per_channel == spc,
              "first render should be full spc (emit-priming)");

  TEST_ASSERT(check_emit_priming(&output, (uint32_t)delay),
              "emit-priming pattern mismatch: expected delay zeros then audio");

  /* Re-feed data (has_data set to 0 after render) */
  TEST_ASSERT(
      update_object_data(oar, spc, TEST_SAMPLING_RATE, 440.0f, 1.0f) == 0,
      "update data failed");
  ret = oar_render(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "render failed");
  TEST_ASSERT(output.samples_per_channel == spc,
              "second render should be full spc");

  /* Flush: returns delay_size samples */
  ret = oar_flush(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "flush failed");
  TEST_ASSERT(output.samples_per_channel == (uint32_t)delay,
              "flush should return delay_size");

  /* Flush output verification: non-zero, non-noise (no limit check needed) */
  TEST_ASSERT(check_output_nonzero_finite(&output),
              "flush output should be non-zero and finite");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC2: Emit-priming — delay_size > samples_per_channel
 *
 * Sub-threshold input; verifies priming, not limiting.
 * With spc=120 and delay=240 (48 kHz), priming spans 2 full frames:
 * - Render 1: full spc, all priming zeros.
 * - Render 2: full spc, still all priming zeros.
 * - Render 3: full spc, steady state with non-zero audio.
 * - Flush: returns delay_size samples, non-zero and finite.
 */
static int test_b2_delay_greater_than_frame(void) {
  TEST_START("TC2: delay > frame");
  const uint32_t spc = 120;
  oar_t *oar = create_oar_with_object(spc, TEST_SAMPLING_RATE, 440.0f, 1.0f);
  TEST_ASSERT(oar != NULL, "create oar failed");

  TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok, "enable_limiter failed");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  int delay = oar_get_limiter_delay(oar);
  TEST_ASSERT(delay > 0, "limiter delay should be positive");
  TEST_ASSERT((uint32_t)delay > spc,
              "delay should be greater than spc for this TC");
  /* Allocate buffer large enough for flush (delay > spc) */
  uint32_t buf_spc = (spc > (uint32_t)delay) ? spc : (uint32_t)delay;
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, buf_spc, &output) == 0, "alloc failed");

  /* Render 1: full spc, all priming zeros (delay > spc) */
  int ret = oar_render(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "render 1 failed");
  TEST_ASSERT(output.samples_per_channel == spc,
              "render 1 should be full spc (emit-priming)");
  TEST_ASSERT(is_all_zero(output.data, out_ch * output.samples_per_channel),
              "render 1 should be all priming zeros (delay > spc)");

  /* Render 2: full spc, still all priming zeros */
  TEST_ASSERT(
      update_object_data(oar, spc, TEST_SAMPLING_RATE, 440.0f, 1.0f) == 0,
      "update data 2 failed");
  ret = oar_render(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "render 2 failed");
  TEST_ASSERT(output.samples_per_channel == spc, "render 2 should be full spc");
  TEST_ASSERT(is_all_zero(output.data, out_ch * output.samples_per_channel),
              "render 2 should be all priming zeros");

  /* Render 3: full spc, steady state with non-zero audio */
  TEST_ASSERT(
      update_object_data(oar, spc, TEST_SAMPLING_RATE, 440.0f, 1.0f) == 0,
      "update data 3 failed");
  ret = oar_render(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "render 3 failed");
  TEST_ASSERT(output.samples_per_channel == spc, "render 3 should be full spc");
  /* Steady state: output must be non-silent */
  {
    uint32_t total = output.channels * output.samples_per_channel;
    TEST_ASSERT(is_output_non_silent(output.data, total),
                "render 3 should be non-silent (steady state reached)");
  }

  /* Flush: returns delay_size samples */
  ret = oar_flush(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "flush failed");
  TEST_ASSERT(output.samples_per_channel == (uint32_t)delay,
              "flush should return delay_size");
  /* Flush output: non-zero and finite (drains remaining delayed audio) */
  TEST_ASSERT(check_output_nonzero_finite(&output),
              "flush output should be non-zero and finite");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC3: Limiter enable/disable contrast with full limit check (render + flush)
 *
 * Two independent instances with identical input 6 dB above threshold:
 * - A (limiter disabled): render output exceeds threshold (baseline).
 * - B (limiter enabled): render and flush output are limited to threshold.
 * Separate instances avoid state carry-over between phases. */
static int test_b3_limiter_contrast(void) {
  TEST_START("TC3: limiter enable/disable contrast");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  const float threshold_db = -1.0f;
  float linear_threshold = powf(10.0f, threshold_db / 20.0f);

  /* Compute amplitude so rendered output exceeds threshold by 6 dB */
  float ampl =
      amplitude_over_threshold(threshold_db, RENDER_GAIN_CENTER_STEREO, 6.0f);

  /* --- Instance A: limiter disabled — output should exceed threshold --- */
  oar_t *oar_a = create_oar_with_object(spc, sr, 440.0f, ampl);
  TEST_ASSERT(oar_a != NULL, "create oar_a failed");

  uint32_t out_ch = oar_get_number_of_output_channels(oar_a);
  oar_audio_block_t output_a;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output_a) == 0, "alloc a failed");

  /* Render to steady state (no limiter, no priming — two renders to
   * consume metadata). */
  int ret = oar_render(oar_a, &output_a);
  TEST_ASSERT(ret == ck_oar_ok, "render A1 failed");
  TEST_ASSERT(output_a.samples_per_channel == spc,
              "output should equal spc when limiter disabled");

  TEST_ASSERT(update_object_data(oar_a, spc, sr, 440.0f, ampl) == 0,
              "update data A failed");
  ret = oar_render(oar_a, &output_a);
  TEST_ASSERT(ret == ck_oar_ok, "render A2 failed");

  /* Disabled limiter: output peak must exceed threshold */
  TEST_ASSERT(find_output_peak(&output_a) > linear_threshold,
              "disabled limiter output should exceed threshold");

  free(output_a.data);
  oar_destroy(oar_a);

  /* --- Instance B: limiter enabled — render + flush output limited --- */
  oar_t *oar_b = create_oar_with_object(spc, sr, 440.0f, ampl);
  TEST_ASSERT(oar_b != NULL, "create oar_b failed");

  TEST_ASSERT(oar_enable_limiter(oar_b, 1) == ck_oar_ok,
              "enable_limiter failed");

  oar_audio_block_t output_b;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output_b) == 0, "alloc b failed");

  /* Render to steady state (emit-priming: first frame has priming zeros) */
  TEST_ASSERT(update_object_data(oar_b, spc, sr, 440.0f, ampl) == 0,
              "update data B1 failed");
  ret = oar_render(oar_b, &output_b);
  TEST_ASSERT(ret == ck_oar_ok, "render B1 (priming) failed");
  TEST_ASSERT(output_b.samples_per_channel == spc,
              "enabled limiter should return full spc (emit-priming)");

  TEST_ASSERT(update_object_data(oar_b, spc, sr, 440.0f, ampl) == 0,
              "update data B2 failed");
  ret = oar_render(oar_b, &output_b);
  TEST_ASSERT(ret == ck_oar_ok, "render B2 (steady) failed");

  /* Enabled limiter: render output must be limited to threshold */
  TEST_ASSERT(check_output_limited(&output_b, linear_threshold),
              "enabled limiter render output should be limited to threshold");

  /* Enabled limiter: flush output must also be limited to threshold */
  ret = oar_flush(oar_b, &output_b);
  TEST_ASSERT(ret == ck_oar_ok, "flush failed");
  TEST_ASSERT(check_output_limited(&output_b, linear_threshold),
              "enabled limiter flush output should be limited to threshold");

  free(output_b.data);
  oar_destroy(oar_b);
  return TEST_PASS;
}

/* TC4: oar_set_limiter_threshold() — threshold level verification
 *
 * Two instances with identical input (rendered peak well above -6 dB):
 * - A: threshold -20 dB → output peak stays below -20 dB.
 * - B: threshold -6 dB → output peak stays below -6 dB.
 * - Cross-check: B's peak exceeds -20 dB, proving the threshold setting
 *   (not coincidence) controls the output level.
 */
static int test_b4_set_threshold_level(void) {
  TEST_START("TC4: set_limiter_threshold level verification");
  const uint32_t spc = 960;
  const float freq = 440.0f;

  /* Input amplitude: rendered peak (ampl × render_gain) well above -6 dB
   * to ensure both thresholds are exercised. */
  const float amplitude = 2.0f;

  const float threshold_a_db = -20.0f;
  const float threshold_b_db = -6.0f;
  float linear_a = powf(10.0f, threshold_a_db / 20.0f);
  float linear_b = powf(10.0f, threshold_b_db / 20.0f);

  /* Instance A: threshold = -20 dB → output peak must stay below -20 dB */
  float peak_a = render_peak_at_threshold(spc, TEST_SAMPLING_RATE, freq,
                                          amplitude, threshold_a_db);
  TEST_ASSERT(peak_a >= 0.0f, "render A failed");
  TEST_ASSERT(peak_a <= linear_a + 1e-6f, "output should be limited to -20 dB");

  /* Instance B: threshold = -6 dB → output peak must stay below -6 dB */
  float peak_b = render_peak_at_threshold(spc, TEST_SAMPLING_RATE, freq,
                                          amplitude, threshold_b_db);
  TEST_ASSERT(peak_b >= 0.0f, "render B failed");
  TEST_ASSERT(peak_b <= linear_b + 1e-6f, "output should be limited to -6 dB");

  /* Cross-check: -6 dB threshold allows higher peaks than -20 dB */
  TEST_ASSERT(peak_b > linear_a,
              "output at -6 dB threshold should exceed -20 dB level");

  return TEST_PASS;
}

/* TC5: oar_set_limiter_threshold() with limiter disabled
 *
 * Configure-before-enable pattern:
 * 1. set_threshold with limiter disabled returns ck_oar_ok.
 * 2. After enabling, over-threshold input (amplitude 2.0, rendered peak
 *    ~1.414) is limited to the pre-configured -3 dB threshold.
 */
static int test_b5_set_threshold_limiter_disabled(void) {
  TEST_START("TC5: set_threshold with limiter disabled");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  const float threshold_db = -3.0f;
  const float amplitude = 2.0f; /* render peak ~1.414 > -3 dB (0.7079) */

  /* Phase 1: set_threshold on a limiter-disabled instance. */
  oar_t *oar = create_oar_with_object(spc, sr, 440.0f, amplitude);
  TEST_ASSERT(oar != NULL, "create oar failed");
  TEST_ASSERT(oar_set_limiter_threshold(oar, threshold_db) == ck_oar_ok,
              "set_threshold should return ok");

  /* Phase 2: enable limiter and verify pre-configured threshold applies. */
  TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok, "enable_limiter failed");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0, "alloc failed");
  TEST_ASSERT(
      render_to_steady_state(oar, &output, spc, sr, 440.0f, amplitude) == 0,
      "render to steady state failed");

  float linear_threshold = powf(10.0f, threshold_db / 20.0f);
  TEST_ASSERT(check_output_limited(&output, linear_threshold),
              "output should be limited to pre-configured threshold");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC6: oar_set_limiter_threshold() — out-of-range clamp
 *
 * Out-of-range thresholds are clamped to [-60, 0] dB. Over-threshold input
 * (amplitude 2.0, rendered peak ~1.414) ensures the limiter engages:
 * 1. Set -80 dB → clamped to -60 dB → output peak stays below -60 dB.
 * 2. Set +5 dB → clamped to 0 dB → output peak stays below 0 dB.
 * Separate instances avoid one set_threshold call overwriting the other.
 */
static int test_b6_threshold_out_of_range(void) {
  TEST_START("TC6: threshold out-of-range clamp");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  const float amplitude = 2.0f; /* render peak ~1.414, above both clamps */

  /* --- Below minimum: -80 dB → clamped to -60 dB --- */
  {
    float peak_lo =
        render_peak_at_threshold(spc, sr, 440.0f, amplitude, -80.0f);
    float linear_lo = powf(10.0f, -60.0f / 20.0f); /* clamped value */
    TEST_ASSERT(peak_lo >= 0.0f, "render failed (below-min clamp)");
    TEST_ASSERT(peak_lo <= linear_lo + 1e-6f,
                "output should be limited to -60 dB (clamped from -80)");
  }

  /* --- Above maximum: +5 dB → clamped to 0 dB --- */
  {
    float peak_hi = render_peak_at_threshold(spc, sr, 440.0f, amplitude, 5.0f);
    float linear_hi = 1.0f; /* 0 dB clamped value */
    TEST_ASSERT(peak_hi >= 0.0f, "render failed (above-max clamp)");
    TEST_ASSERT(peak_hi <= linear_hi + 1e-6f,
                "output should be limited to 0 dB (clamped from +5)");
  }

  return TEST_PASS;
}

/* TC7: oar_get_limiter_delay() verification (multi-frequency)
 *
 * Delay contract: 5 ms look-ahead at 48 kHz = 240 samples, independent of
 * input frequency. For each test frequency: create an instance, enable the
 * limiter, verify delay == 240. Also verifies get_limiter_delay(NULL) == 0.
 */
static int test_b7_get_limiter_delay_multi_freq(void) {
  TEST_START("TC7: get_limiter_delay (multi-frequency)");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  const int expected_delay = 240; /* 5 ms look-ahead at 48 kHz */

  /* Frequencies spanning low to high audible range */
  static const float test_freqs[] = {100.0f, 440.0f, 1000.0f, 5000.0f,
                                     15000.0f};
  const int num_freqs = (int)(sizeof(test_freqs) / sizeof(test_freqs[0]));

  for (int fi = 0; fi < num_freqs; ++fi) {
    oar_t *oar = create_oar_with_object(spc, sr, test_freqs[fi], 1.0f);
    TEST_ASSERT(oar != NULL, "create oar failed");

    TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok,
                "enable_limiter failed");

    /* Delay must be constant regardless of input frequency */
    TEST_ASSERT(oar_get_limiter_delay(oar) == expected_delay,
                "delay should be 240 for 48kHz (freq-independent)");

    oar_destroy(oar);
  }

  /* NULL-safety: oar_get_limiter_delay(NULL) must return 0 */
  TEST_ASSERT(oar_get_limiter_delay(NULL) == 0, "NULL should return 0");

  return TEST_PASS;
}

/* TC8: oar_flush() with limiter disabled */
static int test_b8_flush_disabled(void) {
  TEST_START("TC8: flush with disabled limiter");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  oar_t *oar = create_oar_with_object(spc, sr, 440.0f, 1.0f);
  TEST_ASSERT(oar != NULL, "create oar failed");

  /* Limiter disabled by default */
  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0, "alloc failed");

  int ret = oar_render(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "render failed");

  ret = oar_flush(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "flush should return ok");
  TEST_ASSERT(output.samples_per_channel == 0,
              "flush with disabled limiter should return 0");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC9: Repeated oar_flush()
 * First flush drains the delay buffer and resets limiter state. Second
 * flush still returns delay_size samples (emit-priming), all zeros because
 * the delay buffer was cleared by the reset. */
static int test_b9_repeated_flush(void) {
  TEST_START("TC9: repeated flush");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  const float threshold_db = -1.0f;
  float ampl =
      amplitude_over_threshold(threshold_db, RENDER_GAIN_CENTER_STEREO, 6.0f);
  oar_t *oar = create_oar_with_object(spc, sr, 440.0f, ampl);
  TEST_ASSERT(oar != NULL, "create oar failed");

  TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok, "enable_limiter failed");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  int delay = oar_get_limiter_delay(oar);
  /* Allocate buffer large enough for flush (delay_size) */
  uint32_t buf_spc = (spc > (uint32_t)delay) ? spc : (uint32_t)delay;
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, buf_spc, &output) == 0, "alloc failed");

  /* Render to steady state (emit-priming: full spc each frame) */
  TEST_ASSERT(render_to_steady_state(oar, &output, spc, sr, 440.0f, ampl) == 0,
              "render to steady state failed");

  /* First flush: returns delay_size samples */
  int ret = oar_flush(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "flush 1 failed");
  TEST_ASSERT(output.samples_per_channel == (uint32_t)delay,
              "flush 1 should return delay_size");

  /* Second flush: returns delay_size samples, all zeros (delay buffer was
   * cleared by the reset after flush 1) */
  ret = oar_flush(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "flush 2 failed");
  TEST_ASSERT(output.samples_per_channel == (uint32_t)delay,
              "flush 2 should return delay_size (emit-priming, all zeros)");
  uint32_t total = output.channels * output.samples_per_channel;
  TEST_ASSERT(is_all_zero(output.data, total),
              "flush 2 output should be all zeros");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC10: oar_flush() NULL parameter error handling */
static int test_b10_flush_null_params(void) {
  TEST_START("TC10: flush NULL params");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  oar_t *oar = create_oar_with_object(spc, sr, 440.0f, 1.0f);
  TEST_ASSERT(oar != NULL, "create oar failed");

  TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok, "enable_limiter failed");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0, "alloc failed");

  /* oar_flush(NULL, output) */
  TEST_ASSERT(oar_flush(NULL, &output) == ck_oar_error_inval,
              "flush(NULL) should return inval");

  /* oar_flush(oar, NULL) */
  TEST_ASSERT(oar_flush(oar, NULL) == ck_oar_error_inval,
              "flush(oar, NULL) should return inval");

  /* oar_flush(oar, output_with_null_data) */
  oar_audio_block_t null_data_output;
  memset(&null_data_output, 0, sizeof(null_data_output));
  null_data_output.channels = out_ch;
  null_data_output.samples_per_channel = spc;
  null_data_output.data = NULL;
  TEST_ASSERT(oar_flush(oar, &null_data_output) == ck_oar_error_inval,
              "flush with null data should return inval");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC11: NULL API calls (L1 delegation)
 *
 * oar_* limiter APIs must return safe values for NULL arguments. Covers
 * enable_limiter, set_limiter_threshold, get_limiter_delay; flush NULL
 * params are covered by TC10. */
static int test_b11_null_api_calls(void) {
  TEST_START("TC11: NULL API calls");
  /* oar_enable_limiter(NULL) → inval (NULL check before limiter check) */
  TEST_ASSERT(oar_enable_limiter(NULL, 1) == ck_oar_error_inval,
              "enable_limiter(NULL) should return inval");
  /* oar_set_limiter_threshold(NULL) → inval (NULL check before limiter check)
   */
  TEST_ASSERT(oar_set_limiter_threshold(NULL, -3.0f) == ck_oar_error_inval,
              "set_limiter_threshold(NULL) should return inval");
  /* oar_get_limiter_delay(NULL) → 0 */
  TEST_ASSERT(oar_get_limiter_delay(NULL) == 0,
              "get_limiter_delay(NULL) should return 0");
  return TEST_PASS;
}

/* TC12: Block reuse without field restore
 *
 * With emit-priming the output length never changes, so a caller reusing
 * its oar_audio_block_t across renders without restoring any field must
 * keep working. Renders 5 consecutive frames without reset_output(). */
static int test_b12_block_reuse_no_restore(void) {
  TEST_START("TC12: block reuse without field restore");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  const int num_frames = 5;
  oar_t *oar = create_oar_with_object(spc, sr, 440.0f, 1.0f);
  TEST_ASSERT(oar != NULL, "create oar failed");

  TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok, "enable_limiter failed");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, spc, &output) == 0, "alloc failed");

  /* Render consecutive frames reusing the same block, with no field restore
   * (the device-callback pattern). Every render must succeed. */
  for (int frame = 0; frame < num_frames; frame++) {
    int ret = oar_render(oar, &output);
    TEST_ASSERT(ret == ck_oar_ok, "render should succeed on every frame");
    TEST_ASSERT(output.samples_per_channel == spc,
                "reused block should stay at full spc");

    if (frame < num_frames - 1) {
      TEST_ASSERT(update_object_data(oar, spc, sr, 440.0f, 1.0f) == 0,
                  "update data failed");
    }
  }

  /* Steady-state output must be non-silent and limited */
  uint32_t total = output.channels * output.samples_per_channel;
  TEST_ASSERT(!is_all_zero(output.data, total),
              "steady-state output should be non-silent");
  float linear_threshold = powf(10.0f, -1.0f / 20.0f);
  TEST_ASSERT(check_output_limited(&output, linear_threshold),
              "output exceeds threshold");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC13: Planar stride integrity
 *
 * Detection strategy: a center-panned object (azimuth 0) renders identically
 * to L and R, and the limiter applies the same gain to all channels, so at
 * steady state ch0[i] == ch1[i]. Any plane-stride corruption breaks this
 * symmetry. Verified on both render and flush output. */
static int test_b13_planar_stride_integrity(void) {
  TEST_START("TC13: planar stride integrity");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 960;
  oar_t *oar = create_oar_with_object(spc, sr, 440.0f, 1.0f);
  TEST_ASSERT(oar != NULL, "create oar failed");

  TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok, "enable_limiter failed");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  TEST_ASSERT(out_ch == 2, "stereo output expected");
  int delay = oar_get_limiter_delay(oar);
  uint32_t buf_spc = (spc > (uint32_t)delay) ? spc : (uint32_t)delay;
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, buf_spc, &output) == 0, "alloc failed");

  /* Render to steady state */
  TEST_ASSERT(render_to_steady_state(oar, &output, spc, sr, 440.0f, 1.0f) == 0,
              "render to steady state failed");

  /* Steady state: L and R must match sample-for-sample (center-panned
   * source, shared limiter gain). Stride corruption would mix planes. */
  for (uint32_t i = 0; i < spc; ++i) {
    float l = output.data[i];
    float r = output.data[spc + i];
    TEST_ASSERT(fabsf(l - r) < 1e-4f,
                "L/R mismatch at steady state: stride corruption");
  }

  /* Flush output must also keep planes separate (flush writes planes at
   * the returned-sample stride) */
  int ret = oar_flush(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "flush failed");
  uint32_t flushed = output.samples_per_channel;
  TEST_ASSERT(flushed > 0, "flush should return samples");
  for (uint32_t i = 0; i < flushed; ++i) {
    float l = output.data[i];
    float r = output.data[flushed + i];
    TEST_ASSERT(fabsf(l - r) < 1e-4f,
                "L/R mismatch in flush output: stride corruption");
  }

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* TC14: Fixed-block render loop with sample conservation
 *
 * Emit-priming contract: every render returns exactly spc samples and the
 * final flush returns delay_size samples, so N frames of 512-sample blocks
 * yield N*spc + delay_size samples in total, none lost or duplicated. */
static int test_b14_fixed_block_loop(void) {
  TEST_START("TC14: fixed-block loop sample conservation");
  const uint32_t sr = TEST_SAMPLING_RATE, spc = 512;
  const int num_frames = 8;
  oar_t *oar = create_oar_with_object(spc, sr, 440.0f, 1.0f);
  TEST_ASSERT(oar != NULL, "create oar failed");

  TEST_ASSERT(oar_enable_limiter(oar, 1) == ck_oar_ok, "enable_limiter failed");

  int delay = oar_get_limiter_delay(oar);
  TEST_ASSERT(delay > 0, "limiter delay expected");

  uint32_t out_ch = oar_get_number_of_output_channels(oar);
  uint32_t buf_spc = (spc > (uint32_t)delay) ? spc : (uint32_t)delay;
  oar_audio_block_t output;
  TEST_ASSERT(alloc_audio_block(out_ch, buf_spc, &output) == 0, "alloc failed");

  float linear_threshold = powf(10.0f, -1.0f / 20.0f);
  uint32_t total_samples = 0;

  /* Fixed-block loop: reused block, no field restore needed */
  for (int frame = 0; frame < num_frames; frame++) {
    int ret = oar_render(oar, &output);
    TEST_ASSERT(ret == ck_oar_ok, "render failed");
    TEST_ASSERT(output.samples_per_channel == spc,
                "each render must return full spc");
    TEST_ASSERT(check_output_limited(&output, linear_threshold),
                "render output exceeds threshold");
    total_samples += output.samples_per_channel;

    TEST_ASSERT(update_object_data(oar, spc, sr, 440.0f, 1.0f) == 0,
                "update data failed");
  }

  /* Final flush drains the delay buffer */
  int ret = oar_flush(oar, &output);
  TEST_ASSERT(ret == ck_oar_ok, "flush failed");
  TEST_ASSERT(output.samples_per_channel == (uint32_t)delay,
              "flush must return delay_size samples");
  TEST_ASSERT(check_output_limited(&output, linear_threshold),
              "flush output exceeds threshold");
  total_samples += output.samples_per_channel;

  /* Sample conservation: N*spc + delay_size */
  TEST_ASSERT(total_samples == num_frames * spc + (uint32_t)delay,
              "total sample count must be N*spc + delay_size");

  free(output.data);
  oar_destroy(oar);
  return TEST_PASS;
}

/* --- Test table --------------------------------------------------------- */

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "delay < frame", test_b1_delay_less_than_frame),

    TEST_ENTRY("TC2", "delay > frame", test_b2_delay_greater_than_frame),
    TEST_ENTRY("TC3", "limiter enable/disable contrast",
               test_b3_limiter_contrast),
    TEST_ENTRY("TC4", "set_limiter_threshold level verification",
               test_b4_set_threshold_level),
    TEST_ENTRY("TC5", "set_threshold with limiter disabled",
               test_b5_set_threshold_limiter_disabled),
    TEST_ENTRY("TC6", "threshold out-of-range clamp",
               test_b6_threshold_out_of_range),
    TEST_ENTRY("TC7", "get_limiter_delay (multi-frequency)",
               test_b7_get_limiter_delay_multi_freq),
    TEST_ENTRY("TC8", "flush with disabled limiter", test_b8_flush_disabled),
    TEST_ENTRY("TC9", "repeated flush", test_b9_repeated_flush),
    TEST_ENTRY("TC10", "flush NULL params", test_b10_flush_null_params),
    TEST_ENTRY("TC11", "NULL API calls", test_b11_null_api_calls),
    TEST_ENTRY("TC12", "block reuse without field restore",
               test_b12_block_reuse_no_restore),
    TEST_ENTRY("TC13", "planar stride integrity",
               test_b13_planar_stride_integrity),
    TEST_ENTRY("TC14", "fixed-block loop sample conservation",
               test_b14_fixed_block_loop),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
