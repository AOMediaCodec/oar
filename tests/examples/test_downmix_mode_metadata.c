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
 * @file test_downmix_mode_metadata.c
 * @brief Tests for ck_metadata_iamf_downmix_mode update handling (issue #50).
 *
 * Updating the down-mix mode is delegated to the renderer library selected
 * for the element. Only the iamf_downmix renderer library consumes
 * ck_metadata_iamf_downmix_mode; other libraries (EAR, OBR, ...) have no
 * consumer for it, so the advisory contract in oar.h applies on those
 * paths: accepted-and-ignored (NO-OP, ck_oar_ok). These tests pin the
 * NO-OP behavior, the update-time value validation by the consumer, and
 * the boundaries the contract must not cross.
 *
 * Test matrix:
 *   TC1: 5.1.2 -> stereo (EAR fallback, no down-mix stage): advisory update
 *        returns 0 (NO-OP), repeated updates keep returning 0, and the
 *        rendered output is bit-exact identical to not updating at all.
 *   TC2: 5.1.2 -> 5.1.2 (in == out, no down-mix stage): advisory update
 *        returns 0 and rendering still works.
 *   TC3: 5.1 -> stereo (down-mix renderer in chain): a valid mode update
 *        takes effect (the rendered output changes with the mode).
 *   TC4: 5.1 -> stereo: invalid mode values are rejected with
 *        ck_oar_error_inval; a valid update still succeeds afterwards.
 *   TC5: Unknown/reserved metadata types are still rejected with
 *        ck_oar_error_notsup.
 *   TC6: Object position metadata on a channel-based element is still
 *        rejected with ck_oar_error_notsup.
 *   TC7: Binaural (multi-element) path: advisory update returns 0.
 *        Skipped when the binaural renderer is not built in.
 *   TC8: 5.1.2 -> stereo (no consumer): invalid mode values are also
 *        accepted-and-ignored (return 0), pinning the documented NO-OP
 *        semantics.
 *   TC9: an advisory update for a non-existent element id returns
 *        ck_oar_error_inval, not a NO-OP: the translation covers "the
 *        type is not applicable here", never "the id is unknown".
 *   TC10: the ignore is logged once per element (rate-limited): the
 *        first ignored update warns on stderr and names the element, a
 *        repeat does not warn again, and a consumed update never warns.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "oar.h"
#include "oar_base.h"
#include "oar_metadata.h"
#include "test_framework.h"
#include "test_helpers.h"

#ifndef _WIN32
#include <unistd.h>
#endif

#define SAMPLES_PER_CHANNEL 256

typedef struct RenderSetup {
  oar_t *oar;
  uint32_t element_id;
  uint32_t input_channels;
  uint32_t output_channels;
} render_setup_t;

/* --- Local helpers ------------------------------------------------------- */

static oar_audio_element_config_t create_channel_element_config_with_downmix(
    oar_layout_t layout, int mode, int weight_index) {
  oar_audio_element_config_t cfg = create_channel_element_config(layout);
  cfg.parameters.flags = def_parameter_set_flag_iamf_downmix_info;
  cfg.parameters.downmix_info.mode = mode;
  cfg.parameters.downmix_info.weight_index = weight_index;
  return cfg;
}

static oar_metadata_t create_downmix_mode_metadata(int mode, int duration) {
  oar_metadata_t metadata;
  memset(&metadata, 0, sizeof(metadata));
  metadata.type = ck_metadata_iamf_downmix_mode;
  metadata.iamf_downmix_mode.mode = mode;
  metadata.duration = duration;
  return metadata;
}

/* Create oar + group + one channel element and a filled input block.
 * Each channel carries a sine of a different frequency so that down-mix
 * coefficient changes are observable in the rendered output. */
static int render_setup(oar_layout_t target, oar_layout_t element_layout,
                        int with_downmix_info, render_setup_t *setup,
                        oar_audio_block_t *input) {
  memset(setup, 0, sizeof(*setup));

  oar_config_t config =
      create_config(target, SAMPLES_PER_CHANNEL, TEST_SAMPLING_RATE);
  setup->oar = oar_create(&config);
  if (!setup->oar) return -1;

  int gid = oar_add_audio_group(setup->oar);
  if (gid < 0) {
    oar_destroy(setup->oar);
    return -1;
  }

  setup->element_id = 1;
  oar_audio_element_config_t element_cfg =
      with_downmix_info
          ? create_channel_element_config_with_downmix(element_layout, 0, -1)
          : create_channel_element_config(element_layout);
  if (oar_add_audio_element(setup->oar, gid, setup->element_id, &element_cfg) !=
      0) {
    oar_destroy(setup->oar);
    return -1;
  }

  setup->input_channels =
      oar_get_number_of_audio_element_channels(setup->oar, setup->element_id);
  setup->output_channels = oar_get_number_of_output_channels(setup->oar);
  if (alloc_audio_block(setup->input_channels, SAMPLES_PER_CHANNEL, input) !=
      0) {
    oar_destroy(setup->oar);
    return -1;
  }
  for (uint32_t ch = 0; ch < setup->input_channels; ++ch) {
    generate_sine_channel(input->data, SAMPLES_PER_CHANNEL,
                          setup->input_channels, ch, 200.0f + 100.0f * ch,
                          TEST_SAMPLING_RATE);
  }
  return 0;
}

/* Feed the input block and render into a freshly allocated output block.
 * The caller must free output->data. Returns 0 on success. */
static int feed_and_render(const render_setup_t *setup,
                           oar_audio_block_t *input,
                           oar_audio_block_t *output) {
  if (alloc_audio_block(setup->output_channels, SAMPLES_PER_CHANNEL, output) !=
      0)
    return -1;
  if (oar_update_audio_element_data(setup->oar, setup->element_id, input) != 0)
    return -1;
  return oar_render(setup->oar, output);
}

/* --- Test cases ---------------------------------------------------------- */

static int test_advisory_noop_512_to_stereo(void) {
  TEST_START("TC1: advisory NO-OP (5.1.2 -> stereo, EAR fallback)");

  render_setup_t setup_a, setup_b;
  oar_audio_block_t input_a, input_b, out_a, out_b;

  /* Two identical instances: B receives the advisory update, A does not.
   * Separate instances keep renderer-internal state (e.g. limiter priming)
   * identical, so the outputs must be bit-exact comparable. This relies on
   * rendering being deterministic across instances: no RNG or timestamped
   * state in the render path. */
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_512, 1, &setup_a,
                           &input_a) == 0,
              "render_setup A failed");
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_512, 1, &setup_b,
                           &input_b) == 0,
              "render_setup B failed");

  oar_metadata_t meta = create_downmix_mode_metadata(1, SAMPLES_PER_CHANNEL);
  int ret =
      oar_update_audio_element_metadata(setup_b.oar, setup_b.element_id, &meta);
  TEST_ASSERT_EQ(ret, 0, "advisory update should return 0 (NO-OP), not fail");

  /* Repeated advisory updates keep succeeding (rate-limited logging only). */
  ret =
      oar_update_audio_element_metadata(setup_b.oar, setup_b.element_id, &meta);
  TEST_ASSERT_EQ(ret, 0, "repeated advisory update should return 0");

  TEST_ASSERT(feed_and_render(&setup_a, &input_a, &out_a) == 0,
              "render A failed");
  TEST_ASSERT(feed_and_render(&setup_b, &input_b, &out_b) == 0,
              "render B failed");

  /* The ignored update must not change the rendered output. */
  size_t bytes =
      (size_t)out_a.channels * out_a.samples_per_channel * sizeof(float);
  TEST_ASSERT(memcmp(out_a.data, out_b.data, bytes) == 0,
              "ignored advisory update changed the output");

  free(input_a.data);
  free(input_b.data);
  free(out_a.data);
  free(out_b.data);
  oar_destroy(setup_a.oar);
  oar_destroy(setup_b.oar);
  return TEST_PASS;
}

static int test_advisory_noop_512_to_512(void) {
  TEST_START("TC2: advisory NO-OP (5.1.2 -> 5.1.2, in == out)");

  /* in == out: DMRenderer_open rejects it and EAR takes over, which is the
   * same forward path as TC1. Kept to pin the NO-OP behavior for the
   * second configuration the issue reports (demixing stream present while
   * no down-mix stage applies). */
  render_setup_t setup;
  oar_audio_block_t input, output;
  TEST_ASSERT(render_setup(ck_oar_layout_512, ck_oar_layout_512, 1, &setup,
                           &input) == 0,
              "render_setup failed");

  oar_metadata_t meta = create_downmix_mode_metadata(1, SAMPLES_PER_CHANNEL);
  int ret =
      oar_update_audio_element_metadata(setup.oar, setup.element_id, &meta);
  TEST_ASSERT_EQ(ret, 0, "advisory update should return 0 (NO-OP), not fail");

  TEST_ASSERT(feed_and_render(&setup, &input, &output) == 0, "render failed");
  TEST_ASSERT(is_output_non_silent(
                  output.data, output.channels * output.samples_per_channel),
              "render output is silent");

  free(input.data);
  free(output.data);
  oar_destroy(setup.oar);
  return TEST_PASS;
}

static int test_downmix_mode_effective(void) {
  TEST_START("TC3: valid down-mix mode takes effect (5.1 -> stereo)");

  render_setup_t setup_a, setup_b;
  oar_audio_block_t input_a, input_b, out_a, out_b;

  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_51, 1, &setup_a,
                           &input_a) == 0,
              "render_setup A failed");
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_51, 1, &setup_b,
                           &input_b) == 0,
              "render_setup B failed");

  /* Both instances start from mode 0 (config); B switches to mode 2. Their
   * down-mix coefficients (alpha/beta/gamma) differ in the current
   * _mix_factors_mat table, so the outputs must differ; the per-channel
   * frequency spread keeps the difference observable. */
  oar_metadata_t meta = create_downmix_mode_metadata(2, SAMPLES_PER_CHANNEL);
  int ret =
      oar_update_audio_element_metadata(setup_b.oar, setup_b.element_id, &meta);
  TEST_ASSERT_EQ(ret, 0, "valid down-mix mode update failed");

  TEST_ASSERT(feed_and_render(&setup_a, &input_a, &out_a) == 0,
              "render A failed");
  TEST_ASSERT(feed_and_render(&setup_b, &input_b, &out_b) == 0,
              "render B failed");

  size_t bytes =
      (size_t)out_a.channels * out_a.samples_per_channel * sizeof(float);
  TEST_ASSERT(memcmp(out_a.data, out_b.data, bytes) != 0,
              "mode change had no effect on the rendered output");

  free(input_a.data);
  free(input_b.data);
  free(out_a.data);
  free(out_b.data);
  oar_destroy(setup_a.oar);
  oar_destroy(setup_b.oar);
  return TEST_PASS;
}

static int test_downmix_mode_invalid_values(void) {
  TEST_START("TC4: invalid down-mix mode values rejected");

  render_setup_t setup;
  oar_audio_block_t input;
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_51, 1, &setup,
                           &input) == 0,
              "render_setup failed");

  const int invalid_modes[] = {-1, 3, 7, 99};
  for (size_t i = 0; i < sizeof(invalid_modes) / sizeof(invalid_modes[0]);
       ++i) {
    oar_metadata_t meta =
        create_downmix_mode_metadata(invalid_modes[i], SAMPLES_PER_CHANNEL);
    int ret =
        oar_update_audio_element_metadata(setup.oar, setup.element_id, &meta);
    TEST_ASSERT_EQ(ret, ck_oar_error_inval,
                   "invalid mode value must return ck_oar_error_inval");
  }

  /* A valid update still succeeds after the rejections. */
  oar_metadata_t meta = create_downmix_mode_metadata(1, SAMPLES_PER_CHANNEL);
  int ret =
      oar_update_audio_element_metadata(setup.oar, setup.element_id, &meta);
  TEST_ASSERT_EQ(ret, 0, "valid mode update failed after rejections");

  free(input.data);
  oar_destroy(setup.oar);
  return TEST_PASS;
}

static int test_unknown_metadata_type_rejected(void) {
  TEST_START("TC5: unknown metadata types still rejected");

  render_setup_t setup;
  oar_audio_block_t input;
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_512, 1, &setup,
                           &input) == 0,
              "render_setup failed");

  oar_metadata_t meta;
  memset(&meta, 0, sizeof(meta));
  meta.duration = SAMPLES_PER_CHANNEL;

  meta.type = ck_metadata_count; /* first reserved type value */
  int ret =
      oar_update_audio_element_metadata(setup.oar, setup.element_id, &meta);
  TEST_ASSERT_EQ(ret, ck_oar_error_notsup, "reserved type must fail");

  meta.type = (oar_metadata_type_t)99;
  ret = oar_update_audio_element_metadata(setup.oar, setup.element_id, &meta);
  TEST_ASSERT_EQ(ret, ck_oar_error_notsup, "unknown type must fail");

  free(input.data);
  oar_destroy(setup.oar);
  return TEST_PASS;
}

static int test_positions_on_channel_element_rejected(void) {
  TEST_START("TC6: object positions on a channel element still rejected");

  render_setup_t setup;
  oar_audio_block_t input;
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_512, 1, &setup,
                           &input) == 0,
              "render_setup failed");

  polar_t pos = {30.0f, 0.0f, 1.0f};
  oar_metadata_t *meta = create_object_metadata(&pos, 1, SAMPLES_PER_CHANNEL);
  TEST_ASSERT(meta != NULL, "create_object_metadata failed");
  int ret =
      oar_update_audio_element_metadata(setup.oar, setup.element_id, meta);
  free(meta);
  TEST_ASSERT_EQ(ret, ck_oar_error_notsup,
                 "positions on a channel-based element must fail");

  free(input.data);
  oar_destroy(setup.oar);
  return TEST_PASS;
}

static int test_advisory_noop_binaural(void) {
  TEST_START("TC7: advisory NO-OP on the binaural (multi-element) path");

  /* The only case covering the multi-element wrapper path
   * (audio_elements_renderer + OBR). When the binaural renderer is not
   * built in, this test skips and that path has no NO-OP coverage. */
  oar_config_t config = create_config(ck_oar_layout_binaural,
                                      SAMPLES_PER_CHANNEL, TEST_SAMPLING_RATE);
  oar_t *oar = oar_create(&config);
  TEST_ASSERT(oar != NULL, "oar_create failed");

  int gid = oar_add_audio_group(oar);
  if (gid < 0) {
    printf("SKIP: binaural renderer not available\n");
    oar_destroy(oar);
    return TEST_SKIP;
  }

  oar_audio_element_config_t element_cfg = create_object_element_config(1);
  int ret = oar_add_audio_element(oar, gid, 1, &element_cfg);
  if (ret != 0) {
    printf("SKIP: binaural renderer not available (ret=%d)\n", ret);
    oar_destroy(oar);
    return TEST_SKIP;
  }

  oar_metadata_t meta = create_downmix_mode_metadata(1, SAMPLES_PER_CHANNEL);
  ret = oar_update_audio_element_metadata(oar, 1, &meta);
  oar_destroy(oar);
  TEST_ASSERT_EQ(ret, 0, "advisory update on the binaural path must return 0");
  return TEST_PASS;
}

static int test_invalid_mode_no_consumer_accepted(void) {
  TEST_START("TC8: invalid mode values NO-OP accepted without a consumer");

  render_setup_t setup;
  oar_audio_block_t input;
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_512, 1, &setup,
                           &input) == 0,
              "render_setup failed");

  /* No down-mix stage on this path (EAR fallback): the advisory update is
   * accepted-and-ignored, so invalid mode values return 0 just like valid
   * ones -- the value is never consumed. Mirrors TC4 (consumer present). */
  const int invalid_modes[] = {-1, 3, 7, 99};
  for (size_t i = 0; i < sizeof(invalid_modes) / sizeof(invalid_modes[0]);
       ++i) {
    oar_metadata_t meta =
        create_downmix_mode_metadata(invalid_modes[i], SAMPLES_PER_CHANNEL);
    int ret =
        oar_update_audio_element_metadata(setup.oar, setup.element_id, &meta);
    TEST_ASSERT_EQ(ret, 0,
                   "invalid mode without a consumer must return 0 (NO-OP)");
  }

  free(input.data);
  oar_destroy(setup.oar);
  return TEST_PASS;
}

#ifndef _WIN32
/* Runs an update with stderr redirected into a temporary file and returns
 * the captured text (NUL-terminated) in cap->text. warn() writes to
 * stderr, so a rate-limited warning shows in the capture of the update
 * that emitted it and is absent from later captures. */
typedef struct StderrCapture {
  char text[8192];
} stderr_capture_t;

static int update_capturing_stderr(oar_t *oar, uint32_t id,
                                   const oar_metadata_t *metadata,
                                   stderr_capture_t *cap) {
  cap->text[0] = '\0';
  fflush(stderr);
  int saved_fd = dup(fileno(stderr));
  FILE *tmp = tmpfile();
  if (saved_fd < 0 || !tmp || dup2(fileno(tmp), fileno(stderr)) < 0) {
    /* Release whatever was acquired; a failed dup2 leaves stderr unchanged,
     * so closing tmp here cannot detach stderr. */
    if (tmp) fclose(tmp);
    if (saved_fd >= 0) close(saved_fd);
    return ck_oar_error_inval;
  }

  int ret = oar_update_audio_element_metadata(oar, id, metadata);

  fflush(stderr);
  if (dup2(saved_fd, fileno(stderr)) >= 0) {
    long size = ftell(tmp);
    rewind(tmp);
    if (size > (long)sizeof(cap->text) - 1) size = sizeof(cap->text) - 1;
    if (size > 0) size = (long)fread(cap->text, 1, (size_t)size, tmp);
    if (size < 0) size = 0; /* ftell failed: report an empty capture. */
    cap->text[size] = '\0';
  }
  /* saved_fd is redundant once the restore succeeds, and the only remaining
   * copy if it fails: close it on both paths. */
  close(saved_fd);
  fclose(tmp);
  return ret;
}
#endif

static int test_missing_element_keeps_inval(void) {
  TEST_START("TC9: advisory update for a missing element returns inval");

  render_setup_t setup;
  oar_audio_block_t input;
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_512, 1, &setup,
                           &input) == 0,
              "render_setup failed");

  /* The NO-OP translates "type not applicable", never "id not found". */
  oar_metadata_t meta = create_downmix_mode_metadata(1, SAMPLES_PER_CHANNEL);
  int ret = oar_update_audio_element_metadata(setup.oar, 999, &meta);
  TEST_ASSERT_EQ(ret, ck_oar_error_inval,
                 "missing element must keep reporting ck_oar_error_inval");

  free(input.data);
  oar_destroy(setup.oar);
  return TEST_PASS;
}

static int test_ignored_advisory_warns_once(void) {
  TEST_START("TC10: ignored advisory warns once, consumed never");
#ifdef _WIN32
  printf("SKIP: stderr capture requires POSIX\n");
  return TEST_SKIP;
#else
  render_setup_t noop_setup, consumer_setup;
  oar_audio_block_t noop_input, consumer_input;
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_512, 1,
                           &noop_setup, &noop_input) == 0,
              "render_setup (no consumer) failed");
  TEST_ASSERT(render_setup(ck_oar_layout_stereo, ck_oar_layout_51, 1,
                           &consumer_setup, &consumer_input) == 0,
              "render_setup (consumer) failed");

  stderr_capture_t cap;
  oar_metadata_t meta = create_downmix_mode_metadata(1, SAMPLES_PER_CHANNEL);

  /* The first ignored update warns and names the element. */
  int ret = update_capturing_stderr(noop_setup.oar, noop_setup.element_id,
                                    &meta, &cap);
  TEST_ASSERT_EQ(ret, 0, "advisory update should return 0");
  TEST_ASSERT(strstr(cap.text, "not applicable") != NULL,
              "first ignored advisory update did not warn");
  TEST_ASSERT(strstr(cap.text, "Element 1") != NULL,
              "the warning does not name the element");

  /* The repeat is rate-limited: no second warning for the same element. */
  ret = update_capturing_stderr(noop_setup.oar, noop_setup.element_id, &meta,
                                &cap);
  TEST_ASSERT_EQ(ret, 0, "repeated advisory update should return 0");
  TEST_ASSERT(strstr(cap.text, "not applicable") == NULL,
              "repeated ignored advisory update warned again");

  /* A consumed update on the down-mix path never warns. */
  ret = update_capturing_stderr(consumer_setup.oar, consumer_setup.element_id,
                                &meta, &cap);
  TEST_ASSERT_EQ(ret, 0, "valid down-mix mode update failed");
  TEST_ASSERT(strstr(cap.text, "not applicable") == NULL,
              "consumed down-mix mode update warned as ignored");

  free(noop_input.data);
  free(consumer_input.data);
  oar_destroy(noop_setup.oar);
  oar_destroy(consumer_setup.oar);
  return TEST_PASS;
#endif
}

/* --- Test table ---------------------------------------------------------- */

static test_entry_t g_tests[] = {
    TEST_ENTRY("TC1", "advisory NO-OP (5.1.2->stereo, EAR fallback)",
               test_advisory_noop_512_to_stereo),
    TEST_ENTRY("TC2", "advisory NO-OP (5.1.2->5.1.2, in == out)",
               test_advisory_noop_512_to_512),
    TEST_ENTRY("TC3", "valid down-mix mode takes effect (5.1->stereo)",
               test_downmix_mode_effective),
    TEST_ENTRY("TC4", "invalid down-mix mode values rejected",
               test_downmix_mode_invalid_values),
    TEST_ENTRY("TC5", "unknown metadata types still rejected",
               test_unknown_metadata_type_rejected),
    TEST_ENTRY("TC6", "object positions on channel element rejected",
               test_positions_on_channel_element_rejected),
    TEST_ENTRY("TC7", "advisory NO-OP on binaural path",
               test_advisory_noop_binaural),
    TEST_ENTRY("TC8", "invalid mode NO-OP accepted without a consumer",
               test_invalid_mode_no_consumer_accepted),
    TEST_ENTRY("TC9", "missing element keeps ck_oar_error_inval",
               test_missing_element_keeps_inval),
    TEST_ENTRY("TC10", "ignored advisory warns once, consumed never",
               test_ignored_advisory_warns_once),
};

int main(int argc, char *argv[]) {
  return run_all_tests(g_tests, NUM_TESTS(g_tests), argc, argv);
}
