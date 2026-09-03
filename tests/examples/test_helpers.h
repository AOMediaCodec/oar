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
 * @file test_helpers.h
 * @brief Shared test utility functions for OAR test suites.
 *
 * Provides signal generation, silence detection, config/element creation,
 * and output block helpers that are common across multiple test files.
 *
 * Depends on oar.h (and transitively oar_base.h, oar_metadata.h).
 */

#ifndef OAR_TEST_HELPERS_H
#define OAR_TEST_HELPERS_H

#include <stdint.h>
#include <string.h>

#include "oar.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --- Config helpers ----------------------------------------------------- */

/** Create an oar_config_t with the given target layout, samples_per_channel,
 *  and sampling_rate. */
oar_config_t create_config(oar_layout_t layout, uint32_t samples_per_channel,
                           uint32_t sampling_rate);

/* --- Signal generation -------------------------------------------------- */

/** Write a sine wave into buffer[0..samples-1] (single channel). */
void generate_sine(float *buffer, uint32_t samples, float freq, float rate);

/** Write a sine wave into one channel of a planar multi-channel buffer.
 *  Writes to buffer[ch_idx * samples .. (ch_idx + 1) * samples - 1]. */
void generate_sine_channel(float *buffer, uint32_t samples, uint32_t channels,
                           uint32_t ch_idx, float freq, float rate);

/** Generate a composite dual-tone test stimulus into a single-channel buffer.
 *  The broad-band content ensures frequency-dependent effects (e.g.
 *  head-shadow ILD) are exercised. */
void generate_dual_tone_stimulus(float *buffer, uint32_t samples,
                                 float sample_rate, float freq_low,
                                 float freq_high);

/* --- Element config helpers --------------------------------------------- */

/** Create a zeroed object-based element config with num_objects objects. */
oar_audio_element_config_t create_object_element_config(int num_objects);

/** Create a zeroed channel-based element config with the given layout. */
oar_audio_element_config_t create_channel_element_config(oar_layout_t layout);

/** Create a zeroed scene-based element config with the given ambisonic order.
 */
oar_audio_element_config_t create_scene_element_config(oar_hoa_t order);

/* --- Metadata helpers --------------------------------------------------- */

/** Create object position metadata (heap-allocated, caller must free).
 *  @param positions   Array of polar positions.
 *  @param num_objects Number of objects (1..def_max_number_of_objects).
 *  @param duration    Metadata duration in samples.
 *  @return NULL on failure. */
oar_metadata_t *create_object_metadata(const polar_t *positions,
                                       uint32_t num_objects, uint32_t duration);

/* --- Output helpers ------------------------------------------------------ */

/** Check whether data contains any non-zero samples (threshold 1e-9f).
 *  @return 1 if non-silent, 0 if silent. */
int is_output_non_silent(const float *data, uint32_t count);

/** Allocate a zero-initialised audio block (channels, samples_per_channel,
 *  and data buffer all set). Caller must free output->data.
 *  @return 0 on success, -1 on allocation failure. */
int alloc_audio_block(uint32_t channels, uint32_t samples_per_channel,
                      oar_audio_block_t *output);

/** Zero the output buffer, render, and verify the output is non-silent.
 *  @return 0 on success, -1 on render failure or silent output. */
int render_and_check_non_silent(oar_t *oar, oar_audio_block_t *output);

#endif /* OAR_TEST_HELPERS_H */
