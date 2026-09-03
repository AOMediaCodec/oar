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
 * @file test_audio_element_types.c
 * @brief Test cases for audio element type configuration and channel counts.
 *
 * Test matrix:
 *   TC1: ck_channel_based element (stereo layout → 2 channels)
 *   TC2: ck_scene_based element (1OA → 4 channels)
 *   TC3: ck_object_based element (1 object → 1 channel)
 *   TC4: Multiple elements of different types in separate groups
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "oar_base.h"
#include "test_framework.h"
#include "test_helpers.h"

/* TC1: ck_channel_based audio element (stereo → 2 channels) */
static int test_channel_based_element(void) {
  TEST_START("TC1: channel-based element (stereo)");

  oar_config_t config = create_config(ck_oar_layout_stereo, 1024, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg =
      create_channel_element_config(ck_oar_layout_stereo);
  int ret = oar_add_audio_element(oar, gid, 1, &elem_cfg);
  TEST_ASSERT(ret == 0, "oar_add_audio_element failed");

  uint32_t num_channels = oar_get_number_of_audio_element_channels(oar, 1);
  TEST_ASSERT(num_channels == 2, "expected 2 channels for stereo layout");

  oar_destroy(oar);
  return TEST_PASS;
}

/* TC2: ck_scene_based audio element (1OA → 4 channels) */
static int test_scene_based_element(void) {
  TEST_START("TC2: scene-based element (1OA)");

  oar_config_t config = create_config(ck_oar_layout_stereo, 1024, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg = create_scene_element_config(ck_oar_1oa);
  int ret = oar_add_audio_element(oar, gid, 2, &elem_cfg);
  TEST_ASSERT(ret == 0, "oar_add_audio_element failed");

  uint32_t num_channels = oar_get_number_of_audio_element_channels(oar, 2);
  TEST_ASSERT(num_channels == 4, "expected 4 channels for 1OA");

  oar_destroy(oar);
  return TEST_PASS;
}

/* TC3: ck_object_based audio element (1 object → 1 channel) */
static int test_object_based_element(void) {
  TEST_START("TC3: object-based element (1 object)");

  oar_config_t config = create_config(ck_oar_layout_stereo, 1024, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  TEST_ASSERT(gid >= 0, "oar_add_audio_group failed");

  oar_audio_element_config_t elem_cfg = create_object_element_config(1);
  int ret = oar_add_audio_element(oar, gid, 3, &elem_cfg);
  TEST_ASSERT(ret == 0, "oar_add_audio_element failed");

  uint32_t num_channels = oar_get_number_of_audio_element_channels(oar, 3);
  TEST_ASSERT(num_channels == 1, "expected 1 channel for 1 object");

  oar_destroy(oar);
  return TEST_PASS;
}

/* TC4: Multiple elements of different types in separate groups */
static int test_multiple_element_types(void) {
  TEST_START("TC4: multiple element types");

  oar_config_t config = create_config(ck_oar_layout_stereo, 1024, 48000);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  /* Add channel-based element (mono) in group 1 */
  oar_audio_element_config_t channel_cfg =
      create_channel_element_config(ck_oar_layout_mono);
  int gid1 = oar_add_audio_group(oar);
  TEST_ASSERT(gid1 >= 0, "oar_add_audio_group 1 failed");

  int ret = oar_add_audio_element(oar, gid1, 1, &channel_cfg);
  TEST_ASSERT(ret == 0, "add channel element failed");

  TEST_ASSERT(oar_get_number_of_audio_element_channels(oar, 1) == 1,
              "expected 1 channel for mono");

  /* Add scene-based element (ZOA = 1 channel) in group 2 */
  oar_audio_element_config_t scene_cfg =
      create_scene_element_config(ck_oar_zoa);
  int gid2 = oar_add_audio_group(oar);
  TEST_ASSERT(gid2 >= 0, "oar_add_audio_group 2 failed");

  ret = oar_add_audio_element(oar, gid2, 2, &scene_cfg);
  TEST_ASSERT(ret == 0, "add scene element failed");

  TEST_ASSERT(oar_get_number_of_audio_element_channels(oar, 2) == 1,
              "expected 1 channel for ZOA");

  uint32_t num_elements = oar_get_number_of_audio_elements(oar);
  TEST_ASSERT(num_elements == 2, "expected 2 elements");

  oar_destroy(oar);
  return TEST_PASS;
}

/* --- Test table --------------------------------------------------------- */

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "channel-based element", test_channel_based_element),
    TEST_ENTRY("TC2", "scene-based element", test_scene_based_element),
    TEST_ENTRY("TC3", "object-based element", test_object_based_element),
    TEST_ENTRY("TC4", "multiple element types", test_multiple_element_types),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
