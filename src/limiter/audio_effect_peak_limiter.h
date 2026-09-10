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

/*
AOM-IAMF Standard Deliverable Status:
This software module is out of scope and not part of the IAMF Final Deliverable.
*/

/**
 * @file audio_effect_peak_limiter.h
 * @brief Peak Limiter APIs.
 * @version 1.0.0
 * @date Created 03/03/2023
 **/

#ifndef __AUDIO_PEAK_LIMITER_H__
#define __AUDIO_PEAK_LIMITER_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque pointer: the internal structure is defined in the .c file to
 * prevent external code from directly accessing or modifying limiter state. */
typedef struct AudioEffectPeakLimiter audio_effect_peak_limiter_t;

/**
 * @brief     Create and initialize a peak limiter.
 * @param     [in] threshold_db : peak threshold in dB
 * @param     [in] sample_rate : sample rate of audio signal
 * @param     [in] num_channels : number of channels in frame
 * @param     [in] atk_sec : attack duration in seconds
 * @param     [in] rel_sec : release duration in seconds
 * @param     [in] delay_size : number of samples in delay buffer
 * @return    a peak limiter handle, or NULL on failure
 */
audio_effect_peak_limiter_t* audio_effect_peak_limiter_create(
    float threshold_db, int sample_rate, int num_channels, float atk_sec,
    float rel_sec, int delay_size);

/**
 * @brief     Process the pcm signal with predefined peak limiter.
 * @param     [in] ths : the peak limiter handle
 * @param     [in] inblock : the input pcm signal
 * @param     [out] outblock : the output pcm signal
 * @param     [in] frame_size : the frame size of one process block
 * @return    the number of processed samples
 */
int audio_effect_peak_limiter_process_block(audio_effect_peak_limiter_t* ths,
                                            const float* inblock,
                                            float* outblock, int frame_size);

/**
 * @brief     Flush remaining samples from the limiter's delay buffer
 *            and reset internal state to post-creation initial state.
 *            Used during flush to output delayed audio data.
 *            After this call, the limiter state is reset to initial
 *            state; subsequent process_block calls will need to
 *            re-buffer data.
 * @param     [in] ths : the peak limiter handle
 * @param     [out] outblock : the output buffer for flushed samples
 * @return    the number of flushed samples
 */
int audio_effect_peak_limiter_flush(audio_effect_peak_limiter_t* ths,
                                    float* outblock);

/**
 * @brief     Update the peak threshold without re-creating the limiter.
 *            This is an O(1) operation that only updates the internal
 *            linear_threshold field. Delay buffers, gain envelope state,
 *            and peak cache are preserved — audio continues seamlessly.
 * @param     [in] ths : the peak limiter handle
 * @param     [in] threshold_db : new peak threshold in dB [-60, 0]
 */
void audio_effect_peak_limiter_set_threshold(audio_effect_peak_limiter_t* ths,
                                             float threshold_db);

/**
 * @brief     Get the look-ahead delay length of the limiter.
 * @param     [in] ths : the peak limiter handle
 * @return    the number of delay samples (0 = no-delay mode or invalid handle)
 */
int audio_effect_peak_limiter_get_delay(audio_effect_peak_limiter_t* ths);

/**
 * @brief     Destroy the peak limiter.
 * @param     [in] ths : the peak limiter handle
 */
void audio_effect_peak_limiter_destroy(audio_effect_peak_limiter_t* ths);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_PEAK_LIMITER_H__ */
