/*
 * Copyright (c) 2025, Alliance for Open Media. All rights reserved.
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the LICENSE file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

/*
 * Test: oar_remove_audio_element()
 *
 * Scenarios:
 *   1. Non-binaural: Add then remove a single element, verify count=0
 *   2. Non-binaural: Add multiple elements, remove one, verify remaining
 *      renders
 *   3. Non-binaural: Add, remove, re-add element
 *   4. Binaural: Attempt non-LIFO removal (should fail, elements remain active)
 *   5. Binaural: LIFO remove one, then remove all, verify renderer retained and
 *      re-add works with rendering
 *   6. Binaural: Head rotation state preserved after remove-all + re-add
 *   7. Binaural: Without head tracking, remove-all + re-add produces identical
 *      output (baseline for TC6)
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "oar_base.h"
#include "test_framework.h"
#include "test_helpers.h"

/* Feed sine wave to element, render, and verify output is non-silent.
 * Returns 0 on success (non-silent), -1 on failure. */
static int render_and_verify_non_silent(oar_t *oar, uint32_t element_id,
                                        uint32_t element_channels,
                                        const oar_config_t *cfg) {
  uint32_t samples = oar_get_samples_per_channel(oar);
  uint32_t output_ch = oar_get_number_of_output_channels(oar);

  oar_audio_block_t input;
  if (alloc_audio_block(element_channels, samples, &input) != 0) return -1;

  generate_sine_channel(input.data, samples, element_channels, 0, 440.0f,
                        (float)cfg->sampling_rate);

  int ret = oar_update_audio_element_data(oar, element_id, &input);
  free(input.data);
  if (ret != 0) return -1;

  oar_audio_block_t output;
  if (alloc_audio_block(output_ch, samples, &output) != 0) return -1;

  ret = oar_render(oar, &output);
  if (ret != 0) {
    free(output.data);
    return -1;
  }

  int silent = !is_output_non_silent(output.data, output_ch * samples);
  free(output.data);
  return silent ? -1 : 0;
}

/* --- Head rotation test helpers ----------------------------------------- */

/* Feed stimulus to element, render, and capture output.
 * Returns 0 on success, -1 on failure. Caller must free out->data. */
static int render_and_capture(oar_t *oar, uint32_t element_id,
                              uint32_t element_channels,
                              const oar_config_t *cfg, oar_audio_block_t *out) {
  uint32_t samples = oar_get_samples_per_channel(oar);
  uint32_t output_ch = oar_get_number_of_output_channels(oar);

  oar_audio_block_t input;
  if (alloc_audio_block(element_channels, samples, &input) != 0) return -1;

  for (uint32_t c = 0; c < element_channels; c++)
    generate_dual_tone_stimulus(input.data + c * samples, samples,
                                (float)cfg->sampling_rate, 440.0f, 3500.0f);

  int ret = oar_update_audio_element_data(oar, element_id, &input);
  free(input.data);
  if (ret != 0) return -1;

  if (alloc_audio_block(output_ch, samples, out) != 0) return -1;

  ret = oar_render(oar, out);
  if (ret != 0) {
    free(out->data);
    return -1;
  }

  return 0;
}

/* Sum of absolute differences between two float buffers. */
static double sum_abs_diff(const float *a, const float *b, uint32_t count) {
  double sum = 0.0;
  for (uint32_t i = 0; i < count; ++i) sum += fabs((double)a[i] - (double)b[i]);
  return sum;
}

/* Scenario 1: Non-binaural — Add then remove a single element */
static int test_remove_single_element(void) {
  TEST_START("test_remove_single_element");

  oar_config_t cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg =
      create_channel_element_config(ck_oar_layout_mono);

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

  oar_config_t cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg =
      create_channel_element_config(ck_oar_layout_mono);

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

  oar_config_t cfg = create_config(ck_oar_layout_stereo, 256, 48000);
  oar_t *oar = oar_create(&cfg);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg =
      create_channel_element_config(ck_oar_layout_mono);

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

  oar_config_t cfg = create_config(ck_oar_layout_binaural, 256, 48000);
  oar_t *oar = oar_create(&cfg);
  if (!oar) {
    printf("SKIP: binaural not supported\n");
    return TEST_PASS;
  }

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg =
      create_channel_element_config(ck_oar_layout_mono);

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

  oar_config_t cfg = create_config(ck_oar_layout_binaural, 256, 48000);
  oar_t *oar = oar_create(&cfg);
  if (!oar) {
    printf("SKIP: binaural not supported\n");
    return TEST_PASS;
  }

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg =
      create_channel_element_config(ck_oar_layout_mono);

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
   * its lifecycle is tied to the audio group. After removing all elements,
   * the element count should be 0 (the renderer itself is retained but has
   * no elements). */
  ret = oar_remove_audio_element(oar, 1);
  TEST_ASSERT(ret == 0, "remove element 1 failed");

  uint32_t count = oar_get_number_of_audio_elements(oar);
  TEST_ASSERT(count == 0,
              "element count should be 0 after removing all elements");

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

/* Scenario 6 & 7: Binaural — remove-all + re-add determinism.
 *
 * Shared implementation for TC6 (with head tracking) and TC7 (without).
 *
 * When head_tracking is enabled, verifies that head rotation state is
 * preserved across remove-all + re-add.
 * When head_tracking is disabled, verifies that re-add produces
 * identical output (baseline for the head rotation test). */
static int test_binaural_readd_determinism(int head_tracking) {
  oar_config_t cfg = create_config(ck_oar_layout_binaural, 256, 48000);
  oar_t *oar = oar_create(&cfg);
  if (!oar) {
    printf("SKIP: binaural not supported\n");
    return TEST_PASS;
  }

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  if (head_tracking) {
    int ret = oar_enable_head_tracking(oar, 1);
    TEST_ASSERT(ret == 0, "oar_enable_head_tracking failed");

    /* Set 90° yaw rotation: quaternion (cos(45°), 0, sin(45°), 0) */
    oar_metadata_t rot_meta;
    memset(&rot_meta, 0, sizeof(rot_meta));
    rot_meta.type = ck_metadata_head_rotation;
    rot_meta.head_rotation.w = (float)cos(M_PI / 4.0);
    rot_meta.head_rotation.x = 0.0f;
    rot_meta.head_rotation.y = (float)sin(M_PI / 4.0);
    rot_meta.head_rotation.z = 0.0f;
    rot_meta.duration = 0;

    ret = oar_update_metadata(oar, gid, &rot_meta);
    TEST_ASSERT(ret == 0, "oar_update_metadata for head rotation failed");
  }

  /* Add object element at 45° azimuth (non-center position for meaningful
   * binaural rendering; rotation effect only applies when head_tracking=1) */
  oar_audio_element_config_t elem_cfg = create_object_element_config(1);
  int ret = oar_add_audio_element(oar, gid, 1, &elem_cfg);
  TEST_ASSERT(ret == 0, "add element failed");

  polar_t pos = {45.0f, 0.0f, 1.0f};
  oar_metadata_t *pos_meta = create_object_metadata(&pos, 1, 256);
  TEST_ASSERT(pos_meta != NULL, "create_object_metadata failed");
  ret = oar_update_audio_element_metadata(oar, 1, pos_meta);
  free(pos_meta);
  TEST_ASSERT(ret == 0, "set object position failed");

  /* Render — output A */
  uint32_t ch = oar_get_number_of_audio_element_channels(oar, 1);
  TEST_ASSERT(ch > 0, "element should have channels");

  oar_audio_block_t out_a;
  TEST_ASSERT(render_and_capture(oar, 1, ch, &cfg, &out_a) == 0,
              "render A failed");

  /* Remove all elements — OBR handle will be recreated on next add */
  ret = oar_remove_audio_element(oar, 1);
  TEST_ASSERT(ret == 0, "remove element failed");

  TEST_ASSERT(oar_get_number_of_audio_elements(oar) == 0,
              "element count should be 0");

  /* Re-add element and set same position */
  ret = oar_add_audio_element(oar, gid, 2, &elem_cfg);
  TEST_ASSERT(ret == 0, "re-add element failed");

  pos_meta = create_object_metadata(&pos, 1, 256);
  TEST_ASSERT(pos_meta != NULL, "create_object_metadata failed (re-add)");
  ret = oar_update_audio_element_metadata(oar, 2, pos_meta);
  free(pos_meta);
  TEST_ASSERT(ret == 0, "set object position failed (re-add)");

  /* Render — output B */
  ch = oar_get_number_of_audio_element_channels(oar, 2);
  TEST_ASSERT(ch > 0, "re-added element should have channels");

  oar_audio_block_t out_b;
  TEST_ASSERT(render_and_capture(oar, 2, ch, &cfg, &out_b) == 0,
              "render B failed");

  /* Compare outputs A and B */
  uint32_t total = out_a.channels * out_a.samples_per_channel;
  double diff = sum_abs_diff(out_a.data, out_b.data, total);

  double signal_level = 0.0;
  if (head_tracking) {
    for (uint32_t i = 0; i < total; ++i)
      signal_level += fabs((double)out_a.data[i]);
  }

  free(out_a.data);
  free(out_b.data);

  if (head_tracking) {
    printf("  diff=%.6f, signal=%.6f, ratio=%.6f\n", diff, signal_level,
           signal_level > 0 ? diff / signal_level : 0.0);
    TEST_ASSERT(signal_level > 0, "signal level should be non-zero");
    TEST_ASSERT(diff / signal_level < 0.01,
                "head rotation state lost: outputs differ after re-add");
  } else {
    printf("  diff=%.6f\n", diff);
    TEST_ASSERT(diff < 1e-4,
                "outputs should be identical without head tracking");
  }

  oar_destroy(oar);
  return TEST_PASS;
}

static int test_binaural_head_rotation_preserved_on_readd(void) {
  TEST_START("test_binaural_head_rotation_preserved_on_readd");
  return test_binaural_readd_determinism(1);
}

static int test_binaural_no_tracking_readd_identical(void) {
  TEST_START("test_binaural_no_tracking_readd_identical");
  return test_binaural_readd_determinism(0);
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
    TEST_ENTRY("TC6", "binaural head rotation preserved on re-add",
               test_binaural_head_rotation_preserved_on_readd),
    TEST_ENTRY("TC7", "binaural no tracking re-add identical",
               test_binaural_no_tracking_readd_identical),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
