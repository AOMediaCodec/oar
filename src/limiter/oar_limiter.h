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

#ifndef __OAR_LIMITER_H__
#define __OAR_LIMITER_H__

#include <stdint.h>

#include "oar_base.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque pointer: the internal structure is defined in the .c file to
 * prevent external code from directly accessing or modifying limiter state. */
typedef struct OarLimiter oar_limiter_t;

typedef struct oar_limiter_config {
  int sample_rate;
  int num_channels;
  int samples_per_channel;
  float threshold_db;
  float attack_sec;
  float release_sec;
  float look_ahead_sec;
} oar_limiter_config_t;

/**
 * @brief     Create and initialize an OAR limiter instance.
 *            Internally creates audio_effect_peak_limiter_t and allocates
 *            the intermediate output buffer.
 * @param     [in] config : Limiter configuration
 * @return    Handle on success, NULL on failure
 */
oar_limiter_t *oar_limiter_create(const oar_limiter_config_t *config);

/**
 * @brief     Destroy the OAR limiter instance and free all resources.
 *            NULL-safe: passing NULL is a no-op.
 * @param     [in] limiter : OAR limiter handle
 */
void oar_limiter_destroy(oar_limiter_t *limiter);

/**
 * @brief     Enable or disable the limiter.
 *            When re-enabling (disabled -> enabled), internal state is reset
 *            via flush(NULL) to discard stale delay buffer data.
 * @param     [in] limiter : OAR limiter handle
 * @param     [in] enable  : 1 to enable, 0 to disable
 * @return    ck_oar_ok on success, ck_oar_error_inval on invalid parameters
 */
int oar_limiter_enable(oar_limiter_t *limiter, int enable);

/**
 * @brief     Process audio through the limiter.
 *            When enabled: applies peak limiting (emit-priming mode,
 *            output length is always equal to input samples_per_channel).
 *            When disabled or peak_limiter is NULL: passthrough (no
 *            modification to output).
 * @param     [in]     limiter : OAR limiter handle
 * @param     [in,out] output  : Audio block (planar float). Output length
 *                               is always equal to input samples_per_channel.
 *                               First delay_size samples are zero-padded
 *                               (priming) when limiter is freshly enabled.
 * @return    ck_oar_ok on success, ck_oar_error_inval on invalid parameters
 */
int oar_limiter_process(oar_limiter_t *limiter, oar_audio_block_t *output);

/**
 * @brief     Flush remaining samples from the limiter's delay buffer.
 *            After flush, limiter state is reset to initial state.
 *            Subsequent oar_limiter_process() calls work normally.
 * @param     [in]     limiter : OAR limiter handle
 * @param     [in,out] output  : Audio block to receive flushed samples.
 *                               samples_per_channel is set to flushed count.
 *                               output->data must be allocated for at least
 *                               max(samples_per_channel, delay_size) *
 *                               channels floats.
 * @return    ck_oar_ok on success, ck_oar_error_inval on invalid parameters.
 *            On success, output->samples_per_channel is set to the number of
 *            flushed samples (may be 0 if limiter is disabled or no delay).
 */
int oar_limiter_flush(oar_limiter_t *limiter, oar_audio_block_t *output);

/**
 * @brief     Set the limiter threshold dynamically (O(1), no state reset).
 *            The threshold_db value is clamped to [-60, 0] by the underlying
 *            implementation. Out-of-range values are silently clamped.
 * @param     [in] limiter      : OAR limiter handle
 * @param     [in] threshold_db : New threshold in dB [-60, 0]
 * @return    ck_oar_ok on success, ck_oar_error_inval on invalid parameters
 */
int oar_limiter_set_threshold(oar_limiter_t *limiter, float threshold_db);

/**
 * @brief     Get the limiter look-ahead delay in samples.
 * @param     [in] limiter : OAR limiter handle
 * @return    Delay in samples, 0 if invalid
 */
int oar_limiter_get_delay(const oar_limiter_t *limiter);

/*
 * @note This module is NOT thread-safe. The caller must ensure that
 *       the same handle is not accessed concurrently from multiple threads.
 */

#ifdef __cplusplus
}
#endif

#endif /* __OAR_LIMITER_H__ */
