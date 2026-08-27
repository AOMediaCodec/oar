/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved.
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
 * Test: oar_remove_audio_element()
 *
 * Scenarios:
 *   1. Non-binaural: Add then remove a single element, verify count=0
 *   2. Non-binaural: Add multiple elements, remove one, verify remaining
 * renders
 *   3. Non-binaural: Add, remove, re-add element
 *   4. Binaural: Attempt non-LIFO removal (should fail, elements remain active)
 *   5. Binaural: LIFO remove one, then remove all, verify renderer retained and
 *      re-add works with rendering
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "oar.h"
#include "oar_base.h"
#include "test_framework.h"

static oar_config_t create_stereo_config(void) {
  oar_config_t config;
  config.target_layout = ck_oar_layout_stereo;
  config.samples_per_channel = 256;
  config.sampling_rate = 48000;
  return config;
}

static oar_config_t create_binaural_config(void) {
  oar_config_t config;
  config.target_layout = ck_oar_layout_binaural;
  config.samples_per_channel = 256;
  config.sampling_rate = 48000;
  return config;
}

static void generate_sine(float *buffer, uint32_t samples, uint32_t channels,
                          uint32_t ch_idx, float freq, float sr) {
  for (uint32_t i = 0; i < samples; i++)
    buffer[ch_idx * samples + i] =
        (float)sin(2.0 * M_PI * freq * ((float)i / sr));
}

/* Feed sine wave to element, render, and verify output is non-silent.
 * Returns 0 on success (non-silent), -1 on failure. */
static int render_and_verify_non_silent(oar_t *oar, uint32_t element_id,
                                        uint32_t element_channels,
                                        const oar_config_t *cfg) {
  uint32_t samples = oar_get_samples_per_channel(oar);
  uint32_t output_ch = oar_get_number_of_output_channels(oar);

  oar_audio_block_t input;
  input.channels = element_channels;
  input.samples_per_channel = samples;
  input.data = (float *)calloc(element_channels * samples, sizeof(float));
  if (!input.data) return -1;

  generate_sine(input.data, samples, element_channels, 0, 440.0f,
                (float)cfg->sampling_rate);

  int ret = oar_update_audio_element_data(oar, element_id, &input);
  free(input.data);
  if (ret != 0) return -1;

  oar_audio_block_t output;
  output.channels = output_ch;
  output.samples_per_channel = samples;
  output.data = (float *)calloc(output_ch * samples, sizeof(float));
  if (!output.data) return -1;

  ret = oar_render(oar, &output);
  if (ret != 0) {
    free(output.data);
    return -1;
  }

  int is_silent = 1;
  for (uint32_t i = 0; i < output_ch * samples; i++) {
    if (fabsf(output.data[i]) > 1e-9f) {
      is_silent = 0;
      break;
    }
  }
  free(output.data);
  return is_silent ? -1 : 0;
}

/* Scenario 1: Non-binaural — Add then remove a single element */
static int test_remove_single_element(void) {
  TEST_START("test_remove_single_element");

  oar_config_t cfg = create_stereo_config();
  oar_t *oar = oar_create(&cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_channel_based;
  elem_cfg.cbc.layout = ck_oar_layout_mono;

  int ret = oar_add_audio_element(oar, gid, 1, &elem_cfg);
  TEST_ASSERT(ret == 0, "oar_add_audio_element failed");

  uint32_t count = oar_get_number_of_audio_elements(oar);
  TEST_ASSERT(count == 1, "element count should be 1");

  ret = oar_remove_audio_element(oar, 1);
  TEST_ASSERT(ret == 0, "oar_remove_audio_element failed");

  count = oar_get_number_of_audio_elements(oar);
  TEST_ASSERT(count == 0, "element count should be 0 after removal");

  uint32_t ch = oar_get_number_of_audio_element_channels(oar, 1);
  TEST_ASSERT(ch == 0, "removed element should have 0 channels");

  /* Verify removing non-existent element returns error */
  ret = oar_remove_audio_element(oar, 99);
  TEST_ASSERT(ret != 0, "should fail to remove non-existent element");

  oar_destroy(oar);
  return TEST_PASS;
}

/* Scenario 2: Non-binaural — Remove one of multiple, verify remaining renders
 */
static int test_remove_one_of_multiple_elements(void) {
  TEST_START("test_remove_one_of_multiple_elements");

  oar_config_t cfg = create_stereo_config();
  oar_t *oar = oar_create(&cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_channel_based;
  elem_cfg.cbc.layout = ck_oar_layout_mono;

  int ret = oar_add_audio_element(oar, gid, 1, &elem_cfg);
  TEST_ASSERT(ret == 0, "add element 1 failed");

  ret = oar_add_audio_element(oar, gid, 2, &elem_cfg);
  TEST_ASSERT(ret == 0, "add element 2 failed");

  uint32_t count = oar_get_number_of_audio_elements(oar);
  TEST_ASSERT(count == 2, "element count should be 2");

  ret = oar_remove_audio_element(oar, 1);
  TEST_ASSERT(ret == 0, "remove element 1 failed");

  count = oar_get_number_of_audio_elements(oar);
  TEST_ASSERT(count == 1, "element count should be 1 after removal");

  uint32_t ch = oar_get_number_of_audio_element_channels(oar, 2);
  TEST_ASSERT(ch > 0, "element 2 should still have channels");

  ch = oar_get_number_of_audio_element_channels(oar, 1);
  TEST_ASSERT(ch == 0, "element 1 should have 0 channels");

  /* Verify remaining element renders non-silent output */
  ch = oar_get_number_of_audio_element_channels(oar, 2);
  TEST_ASSERT(render_and_verify_non_silent(oar, 2, ch, &cfg) == 0,
              "output should not be silent");

  oar_destroy(oar);
  return TEST_PASS;
}

/* Scenario 3: Non-binaural — Add, remove, re-add element */
static int test_readd_after_remove(void) {
  TEST_START("test_readd_after_remove");

  oar_config_t cfg = create_stereo_config();
  oar_t *oar = oar_create(&cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_channel_based;
  elem_cfg.cbc.layout = ck_oar_layout_mono;

  int ret = oar_add_audio_element(oar, gid, 1, &elem_cfg);
  TEST_ASSERT(ret == 0, "add element failed");

  ret = oar_remove_audio_element(oar, 1);
  TEST_ASSERT(ret == 0, "remove element failed");

  ret = oar_add_audio_element(oar, gid, 1, &elem_cfg);
  TEST_ASSERT(ret == 0, "re-add element failed");

  uint32_t count = oar_get_number_of_audio_elements(oar);
  TEST_ASSERT(count == 1, "element count should be 1 after re-add");

  oar_destroy(oar);
  return TEST_PASS;
}

/* Scenario 4: Binaural — Attempt non-LIFO removal (should fail gracefully) */
static int test_binaural_remove_non_lifo(void) {
  TEST_START("test_binaural_remove_non_lifo");

  oar_config_t cfg = create_binaural_config();
  oar_t *oar = oar_create(&cfg);
  if (!oar) {
    printf("SKIP: binaural not supported\n");
    return TEST_PASS;
  }

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_channel_based;
  elem_cfg.cbc.layout = ck_oar_layout_mono;

  int ret = oar_add_audio_element(oar, gid, 1, &elem_cfg);
  TEST_ASSERT(ret == 0, "add element 1 failed");

  ret = oar_add_audio_element(oar, gid, 2, &elem_cfg);
  TEST_ASSERT(ret == 0, "add element 2 failed");

  /* Attempt to remove element 1 (non-LIFO) — OBR only supports LIFO.
   * The removal should fail and return an error, but both elements
   * should remain active and rendering should be unaffected. */
  ret = oar_remove_audio_element(oar, 1);
  TEST_ASSERT(ret != 0, "non-LIFO removal should fail");

  /* Both elements should still be accessible */
  uint32_t ch = oar_get_number_of_audio_element_channels(oar, 1);
  TEST_ASSERT(ch > 0, "element 1 should still have channels");
  ch = oar_get_number_of_audio_element_channels(oar, 2);
  TEST_ASSERT(ch > 0, "element 2 should still have channels");

  /* Feed data to both elements and render — should produce non-silent output */
  ch = oar_get_number_of_audio_element_channels(oar, 1);
  TEST_ASSERT(render_and_verify_non_silent(oar, 1, ch, &cfg) == 0,
              "output should not be silent after failed removal");

  oar_destroy(oar);
  return TEST_PASS;
}

/* Scenario 5: Binaural — LIFO remove one, then remove all, verify renderer
 * retained and re-add works with rendering.
 *
 * This test verifies the design principle that the binaural renderer's
 * lifecycle is tied to the audio group: it is created in oar_add_audio_group
 * and destroyed in oar_destroy, not when elements are removed. */
static int test_binaural_remove_all_and_readd(void) {
  TEST_START("test_binaural_remove_all_and_readd");

  oar_config_t cfg = create_binaural_config();
  oar_t *oar = oar_create(&cfg);
  if (!oar) {
    printf("SKIP: binaural not supported\n");
    return TEST_PASS;
  }

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg;
  memset(&elem_cfg, 0, sizeof(elem_cfg));
  elem_cfg.type = ck_channel_based;
  elem_cfg.cbc.layout = ck_oar_layout_mono;

  int ret = oar_add_audio_element(oar, gid, 1, &elem_cfg);
  TEST_ASSERT(ret == 0, "add element 1 failed");

  ret = oar_add_audio_element(oar, gid, 2, &elem_cfg);
  TEST_ASSERT(ret == 0, "add element 2 failed");

  /* Step 1: Remove element 2 (LIFO), verify element 1 still renders */
  ret = oar_remove_audio_element(oar, 2);
  TEST_ASSERT(ret == 0, "remove element 2 (LIFO) failed");

  uint32_t ch = oar_get_number_of_audio_element_channels(oar, 1);
  TEST_ASSERT(ch > 0,
              "element 1 should still have channels after LIFO removal");

  TEST_ASSERT(render_and_verify_non_silent(oar, 1, ch, &cfg) == 0,
              "element 1 should render after LIFO removal of element 2");

  /* Step 2: Remove element 1 (now the last), verify renderer is retained.
   * The binaural renderer is NOT destroyed when all elements are removed —
   * its lifecycle is tied to the audio group. oar_get_number_of_audio_elements
   * returns the renderer count (1 = the empty binaural renderer). */
  ret = oar_remove_audio_element(oar, 1);
  TEST_ASSERT(ret == 0, "remove element 1 failed");

  uint32_t count = oar_get_number_of_audio_elements(oar);
  TEST_ASSERT(count == 1,
              "binaural renderer should be retained after removing all "
              "elements (count=1)");

  ch = oar_get_number_of_audio_element_channels(oar, 1);
  TEST_ASSERT(ch == 0, "removed element should have 0 channels");

  /* Step 3: Re-add a new element — should find the existing renderer and
   * trigger re-open via add_element. Verify it has channels and renders. */
  ret = oar_add_audio_element(oar, gid, 3, &elem_cfg);
  TEST_ASSERT(ret == 0, "re-add element 3 after removing all failed");

  ch = oar_get_number_of_audio_element_channels(oar, 3);
  TEST_ASSERT(ch > 0, "re-added element 3 should have channels");

  TEST_ASSERT(render_and_verify_non_silent(oar, 3, ch, &cfg) == 0,
              "re-added element should render non-silent output");

  oar_destroy(oar);
  return TEST_PASS;
}

/* --- Test table --------------------------------------------------------- */

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "remove single element", test_remove_single_element),
    TEST_ENTRY("TC2", "remove one of multiple",
               test_remove_one_of_multiple_elements),
    TEST_ENTRY("TC3", "re-add after remove", test_readd_after_remove),
    TEST_ENTRY("TC4", "binaural non-LIFO removal",
               test_binaural_remove_non_lifo),
    TEST_ENTRY("TC5", "binaural remove all and re-add",
               test_binaural_remove_all_and_readd),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
