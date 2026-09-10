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
 * @file audio_effect_peak_limiter.c
 * @brief Peak limiter.
 * @version 1.0.0
 * @date Created 03/03/2023
 **/

#include "audio_effect_peak_limiter.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "clog.h"
#include "definitions.h"
#include "oar_base.h"

/* --- Parameter range macros (def_ prefix) --- */

/* threshold_db range: [-60dB, 0dB] */
#define def_threshold_db_min (-60.0f)
#define def_threshold_db_max (0.0f)

/* release range: [10ms, 1000ms] */
#define def_rel_sec_min (0.010f) /* 10ms */
#define def_rel_sec_max (1.000f) /* 1000ms */

/* attack range upper limit: 20ms */
#define def_atk_sec_max (0.020f) /* 20ms */

/* delay_size range: 0 (no-delay mode) or [0.1ms, 20ms] x sample_rate */
#define def_delay_sec_min (0.0001f) /* 0.1ms */
#define def_delay_sec_max (0.020f)  /* 20ms */

struct AudioEffectPeakLimiter {
  /* --- Configuration parameters (fixed after create) --- */
  float linear_threshold; /* Linear-domain threshold */
  float attack_sec;       /* Attack time in seconds */
  float release_sec;      /* Release time in seconds */
  float inc_tc;           /* Per-sample time increment (1/sample_rate) */
  int num_channels;       /* Number of channels */
  int delay_size;         /* Delay length in samples (0 = no-delay mode) */

  /* --- Gain envelope state machine --- */
  float current_gain;      /* Currently applied gain */
  float target_start_gain; /* Gain transition start value */
  float target_end_gain;   /* Gain transition end value */
  float current_tc;        /* Time counter (-1 = idle) */

  /* --- Delay buffers --- */
  float* delay_data; /* Delay buffer, contiguous (num_channels x delay_size) */
  float* peak_data;  /* Peak buffer (delay_size) */
  int entry_index;   /* Circular buffer write index */
  int peak_pos;      /* Cached peak position (-1 = invalid) */

  /* --- First-frame padding handling --- */
  int pad_size; /* Remaining padding samples (0 = no padding / completed) */
};

/**
 * @brief Wrap an index into a circular buffer range [0, buffer_size).
 * Handles negative indices correctly (unlike C's % operator).
 */
static inline int wrap_index(int index, int buffer_size) {
  int r = index % buffer_size;
  return (r < 0) ? r + buffer_size : r;
}

/**
 * @brief Curve acceleration function.
 * Quadratic curve: 1.0 - (x - 1.0)^2, clamped to [0, 1].
 */
static inline float curve_accel(float x) {
  if (x >= 1.0f) return 1.0f;
  if (x <= 0.0f) return 0.0f;
  float d = x - 1.0f;
  return 1.0f - d * d;
}

/**
 * @brief Hard-limit a sample to [-threshold, +threshold].
 * Pure function: no side effects, suitable for inlining.
 */
static inline float hard_limit(float sample, float threshold) {
  if (fabsf(sample) > threshold) {
    return (sample > 0.0f) ? threshold : -threshold;
  }
  return sample;
}

/**
 * @brief Clamp a float value to [min, max] with warning on out-of-range.
 * @param val  Value to clamp
 * @param min  Minimum allowed value
 * @param max  Maximum allowed value
 * @param name Parameter name for warning message
 * @return     Clamped value
 */
static inline float clampf(float val, float min, float max, const char* name) {
  if (val < min) {
    warning("%s %.4f < min %.4f, clamped", name, val, min);
    return min;
  }
  if (val > max) {
    warning("%s %.4f > max %.4f, clamped", name, val, max);
    return max;
  }
  return val;
}

/**
 * @brief Find the current peak from the peak_data circular buffer.
 *
 * Uses peak_pos cache to avoid re-scanning when the cached peak is still
 * valid. When peak_pos < 0 (cache invalid), scans the entire delay window
 * to find the maximum value.
 *
 * @param ths  Limiter handle
 * @param idx  Current wrapped delay buffer index
 * @return     Current peak value (0.0f if delay_size == 0)
 */
static inline float find_peak(audio_effect_peak_limiter_t* ths, int idx) {
  float peak = 0.0f;

  if (ths->delay_size <= 0) return 0.0f;

  if (ths->peak_pos < 0) {
    for (int i = 0; i < ths->delay_size; i++) {
      int wrapped = wrap_index(i + idx, ths->delay_size);
      float val = ths->peak_data[wrapped];
      if (val > peak) {
        peak = val;
        ths->peak_pos = wrapped;
      }
    }
    if (ths->peak_pos < 0) ths->peak_pos = wrap_index(idx, ths->delay_size);
  } else {
    peak = ths->peak_data[ths->peak_pos];
  }

  return peak;
}

/**
 * @brief Update peak_data buffer and peak_pos cache for the current index.
 *
 * If the current position was the cached peak, invalidate the cache.
 * Otherwise, if the new peak_max is larger than the cached peak, update
 * the cache to the current position.
 *
 * @param ths      Limiter handle
 * @param idx      Current wrapped delay buffer index
 * @param peak_max Maximum |input_sample| across all channels
 */
static inline void update_peak_data(audio_effect_peak_limiter_t* ths, int idx,
                                    float peak_max) {
  if (ths->delay_size <= 0) return;

  int wrapped_idx = wrap_index(idx, ths->delay_size);

  if (ths->peak_pos == wrapped_idx)
    ths->peak_pos = -1;
  else if (ths->peak_pos < 0 || ths->peak_data[ths->peak_pos] < peak_max)
    ths->peak_pos = wrapped_idx;

  ths->peak_data[wrapped_idx] = peak_max;
}

/**
 * @brief Update gain envelope using attack/release curves.
 * Implements the gain smoothing state machine.
 *
 * State machine phases (current_tc is the time counter):
 *   - Idle:      current_tc == -1, gain = 1.0 (no active transition)
 *   - Attack:    0 <= current_tc < attack_sec, gain decreases from
 *                target_start_gain toward target_end_gain
 *   - Release:   attack_sec <= current_tc < attack_sec + release_sec,
 *                gain increases from target_end_gain toward 1.0
 *   - Done:      current_tc >= attack_sec + release_sec, gain = 1.0
 *
 * @return Current gain after envelope update
 */
static inline float update_gain_envelope(audio_effect_peak_limiter_t* ths) {
  /* Attack phase: gain ramps down from target_start_gain to target_end_gain */
  if (ths->current_tc != -1 && ths->current_tc < ths->attack_sec) {
    ths->current_tc += ths->inc_tc;
    float acc_ratio = curve_accel(ths->current_tc / ths->attack_sec);
    ths->current_gain =
        ths->target_start_gain -
        acc_ratio * (ths->target_start_gain - ths->target_end_gain);
  } else if (ths->current_tc != -1 &&
             ths->current_tc < ths->release_sec + ths->attack_sec) {
    ths->current_tc += ths->inc_tc;
    float acc_ratio =
        curve_accel((ths->current_tc - ths->attack_sec) / ths->release_sec);
    ths->current_gain =
        ths->target_end_gain + acc_ratio * (1.0f - ths->target_end_gain);
  } else {
    ths->current_gain = 1.0f;
  }
  return ths->current_gain;
}

/**
 * @brief Detect if peak exceeds threshold and start new gain transition.
 *
 * If peak * current_gain exceeds the threshold, a new gain transition is
 * started. If a transition is already in progress, it is overridden:
 * target_start_gain is set to current_gain to preserve the current state,
 * and the time counter is reset to 0 (re-enters attack phase).
 *
 * @return 1 if new transition started, 0 otherwise
 */
static inline int detect_peak(audio_effect_peak_limiter_t* ths, float peak) {
  if (peak * ths->current_gain > ths->linear_threshold) {
    ths->target_start_gain = ths->current_gain;
    ths->target_end_gain = ths->linear_threshold / peak;
    ths->current_tc = 0.0f;
    return 1;
  }
  return 0;
}

/**
 * @brief Process all channels for one sample point.
 *
 * Reads delayed samples from delay buffer, applies gain, hard-limits,
 * writes new input to delay buffer, and computes peak_max.
 *
 * @param ths        Limiter handle
 * @param inblock    Input samples (NULL for flush mode -> input_sample = 0.0f)
 * @param outblock   Output buffer
 * @param k          Sample index within current frame
 * @param idx        Wrapped delay buffer index
 * @param frame_size Frame size (stride for planar layout)
 * @param gain       Current gain to apply
 * @return           Maximum |input_sample| across all channels (peak_max)
 */
static float process_channels(audio_effect_peak_limiter_t* ths,
                              const float* inblock, float* outblock, int k,
                              int idx, int frame_size, float gain) {
  float peak_max = 0.0f;

  for (int ch = 0; ch < ths->num_channels; ch++) {
    int pos = ch * frame_size;
    float input_sample = inblock ? inblock[pos + k] : 0.0f;
    float out;

    if (ths->delay_size > 0) {
      /* Delay mode: read from delay buffer, write new input */
      int delay_offset = ch * ths->delay_size + idx;
      out = ths->delay_data[delay_offset] * gain;
      ths->delay_data[delay_offset] = input_sample;
    } else {
      /* No-delay mode: process input directly */
      out = input_sample * gain;
    }

    out = hard_limit(out, ths->linear_threshold);
    outblock[pos + k] = out;

    float abs_val = fabsf(input_sample);
    if (abs_val > peak_max) peak_max = abs_val;
  }

  return peak_max;
}

/**
 * @brief Compact planar output data to remove look-ahead delay padding.
 * Uses pad_size to track remaining padding (0 = no padding / completed).
 * @return Number of valid output samples (frame_size - pad_size)
 */
static int compact_output(audio_effect_peak_limiter_t* ths, float* outblock,
                          int frame_size) {
  /* Case 1: Entire frame is padding */
  if (ths->pad_size >= frame_size) {
    ths->pad_size -= frame_size;
    return 0;
  }

  /* Case 2: Partial padding — remove and mark done */
  if (ths->pad_size > 0) {
    int valid = frame_size - ths->pad_size;
    for (int c = 0; c < ths->num_channels; c++) {
      memmove(&outblock[c * valid], &outblock[c * frame_size + ths->pad_size],
              valid * sizeof(float));
    }
    ths->pad_size = 0;
    return valid;
  }

  /* Case 3: No padding (pad_size == 0) — nothing to do */
  return frame_size;
}

/**
 * @brief Reset limiter state to post-creation initial state.
 * Called after flush completes.
 */
static void reset_state(audio_effect_peak_limiter_t* ths) {
  ths->entry_index = 0;
  ths->peak_pos = -1;
  ths->current_gain = 1.0f;
  ths->target_start_gain = -1.0f;
  ths->target_end_gain = -1.0f;
  ths->current_tc = -1.0f;
  ths->pad_size = ths->delay_size;
  if (ths->delay_size > 0) {
    memset(ths->delay_data, 0,
           ths->num_channels * ths->delay_size * sizeof(float));
    memset(ths->peak_data, 0, ths->delay_size * sizeof(float));
  }
}

/**
 * @brief Core processing loop shared by process_block and flush.
 * @param ths        Limiter handle
 * @param inblock    Input samples (NULL for flush mode -- flush semantics
 *                   are determined entirely by this NULL check, no flag needed)
 * @param outblock   Output buffer
 * @param frame_size Number of samples to process
 * @return           Number of valid output samples (after padding)
 */
static int process_core(audio_effect_peak_limiter_t* ths, const float* inblock,
                        float* outblock, int frame_size) {
  for (int k = 0; k < frame_size; k++) {
    int idx = (ths->delay_size > 0)
                  ? wrap_index(k + ths->entry_index, ths->delay_size)
                  : 0;

    float peak = find_peak(ths, idx);
    float gain = update_gain_envelope(ths);
    detect_peak(ths, peak);

    float peak_max =
        process_channels(ths, inblock, outblock, k, idx, frame_size, gain);
    update_peak_data(ths, idx, peak_max);
  }

  if (ths->delay_size > 0) {
    ths->entry_index =
        wrap_index(ths->entry_index + frame_size, ths->delay_size);
  }

  return compact_output(ths, outblock, frame_size);
}

audio_effect_peak_limiter_t* audio_effect_peak_limiter_create(
    float threshold_db, int sample_rate, int num_channels, float atk_sec,
    float rel_sec, int delay_size) {
  if (num_channels <= 0 || sample_rate <= 0 || atk_sec < 0.0f ||
      rel_sec <= 0.0f)
    return NULL;

  threshold_db = clampf(threshold_db, def_threshold_db_min,
                        def_threshold_db_max, "threshold_db");
  rel_sec = clampf(rel_sec, def_rel_sec_min, def_rel_sec_max, "rel_sec");
  atk_sec = clampf(atk_sec, 0.0f, def_atk_sec_max, "atk_sec");

  if (delay_size > 0) {
    int min_delay = (int)(def_delay_sec_min * sample_rate);
    int max_delay = (int)(def_delay_sec_max * sample_rate);
    if (delay_size < min_delay) {
      warning("delay_size %d < min %d (0.1ms), clamped", delay_size, min_delay);
      delay_size = min_delay;
    }
    if (delay_size > max_delay) {
      warning("delay_size %d > max %d (20ms), clamped", delay_size, max_delay);
      delay_size = max_delay;
    }
  } else if (delay_size < 0) {
    warning("delay_size %d < 0, set to 0 (no-delay mode)", delay_size);
    delay_size = 0;
  }

  /* Constraint: attack <= look-ahead (when look-ahead > 0) */
  if (delay_size > 0) {
    float look_ahead_sec = (float)delay_size / sample_rate;
    if (atk_sec > look_ahead_sec) {
      warning(
          "attack %.4fs > look-ahead %.4fs, hard-limit will trigger frequently",
          atk_sec, look_ahead_sec);
    }
  }

  audio_effect_peak_limiter_t* ths =
      def_mallocz(audio_effect_peak_limiter_t, 1);
  if (!ths) return NULL;

  /* Set configuration parameters (state init delegated to reset_state) */
  /* threshold setting delegated to set_threshold (reuses clampf + powf) */
  audio_effect_peak_limiter_set_threshold(ths, threshold_db);
  ths->attack_sec = atk_sec;

  ths->release_sec = rel_sec;
  ths->inc_tc = 1.0f / (float)sample_rate;
  ths->num_channels = num_channels;
  ths->delay_size = delay_size;

  if (delay_size > 0) {
    ths->delay_data = def_mallocz(float, num_channels * ths->delay_size);
    if (!ths->delay_data) {
      warning("Failed to allocate delay_data");
      audio_effect_peak_limiter_destroy(ths);
      return NULL;
    }

    ths->peak_data = def_mallocz(float, ths->delay_size);
    if (!ths->peak_data) {
      warning("Failed to allocate peak_data");
      audio_effect_peak_limiter_destroy(ths);
      return NULL;
    }
  }
  /* delay_size == 0: no-delay mode, no buffers allocated */

  /* Initialize all state machine fields, pad_size, and zero buffers */
  reset_state(ths);
  return ths;
}

int audio_effect_peak_limiter_process_block(audio_effect_peak_limiter_t* ths,
                                            const float* inblock,
                                            float* outblock, int frame_size) {
  if (!ths || !inblock || !outblock || frame_size <= 0)
    return ck_oar_error_inval;
  return process_core(ths, inblock, outblock, frame_size);
}

int audio_effect_peak_limiter_flush(audio_effect_peak_limiter_t* ths,
                                    float* outblock) {
  if (!ths) return ck_oar_error_inval;
  if (ths->delay_size <= 0) return 0;

  int ret = outblock ? process_core(ths, 0, outblock, ths->delay_size)
                     : ths->delay_size - ths->pad_size;

  reset_state(ths);
  return ret;
}

void audio_effect_peak_limiter_set_threshold(audio_effect_peak_limiter_t* ths,
                                             float threshold_db) {
  if (!ths) return;
  threshold_db = clampf(threshold_db, def_threshold_db_min,
                        def_threshold_db_max, "threshold_db");
  ths->linear_threshold = powf(10.0f, threshold_db / 20.0f);
}

int audio_effect_peak_limiter_get_delay(audio_effect_peak_limiter_t* ths) {
  return ths ? ths->delay_size : 0;
}

void audio_effect_peak_limiter_destroy(audio_effect_peak_limiter_t* ths) {
  if (!ths) return;

  def_free(ths->delay_data);
  def_free(ths->peak_data);
  def_free(ths);
}
