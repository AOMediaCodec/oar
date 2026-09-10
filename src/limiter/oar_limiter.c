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

#include "oar_limiter.h"

#include <stdlib.h>
#include <string.h>

#include "audio_effect_peak_limiter.h"
#include "clog.h"
#include "definitions.h"

struct OarLimiter {
  /* --- Configuration (fixed after create) --- */
  int num_channels;
  int samples_per_channel;
  int delay_size;

  /* --- State --- */
  int enabled;

  /* --- Internal resources --- */
  audio_effect_peak_limiter_t *peak_limiter;
  float *out_buf;
};

oar_limiter_t *oar_limiter_create(const oar_limiter_config_t *config) {
  if (!config || config->sample_rate <= 0 || config->num_channels <= 0 ||
      config->samples_per_channel <= 0)
    return NULL;

  oar_limiter_t *lim = def_mallocz(oar_limiter_t, 1);
  if (!lim) return NULL;

  lim->num_channels = config->num_channels;
  lim->samples_per_channel = config->samples_per_channel;
  lim->delay_size = (int)(config->look_ahead_sec * config->sample_rate);

  /* Create underlying peak limiter */
  lim->peak_limiter = audio_effect_peak_limiter_create(
      config->threshold_db, config->sample_rate, config->num_channels,
      config->attack_sec, config->release_sec, lim->delay_size);
  if (!lim->peak_limiter) {
    warning("Failed to create peak limiter");
    def_free(lim);
    return NULL;
  }

  lim->delay_size = audio_effect_peak_limiter_get_delay(lim->peak_limiter);

  /* Allocate intermediate buffer: must hold both render and flush output */
  int out_buf_capacity = def_max(config->samples_per_channel, lim->delay_size);
  lim->out_buf = def_mallocz(float, (config->num_channels * out_buf_capacity));
  if (!lim->out_buf) {
    warning("Failed to allocate limiter output buffer");
    audio_effect_peak_limiter_destroy(lim->peak_limiter);
    def_free(lim);
    return NULL;
  }

  return lim;
}

void oar_limiter_destroy(oar_limiter_t *lim) {
  if (!lim) return;
  if (lim->peak_limiter) audio_effect_peak_limiter_destroy(lim->peak_limiter);
  def_free(lim->out_buf);
  def_free(lim);
}

int oar_limiter_enable(oar_limiter_t *lim, int enable) {
  if (!lim) return ck_oar_error_inval;

  /* Reset state when re-enabling to discard stale delay buffer data */
  if (enable && !lim->enabled && lim->peak_limiter) {
    audio_effect_peak_limiter_flush(lim->peak_limiter, NULL);
  }

  lim->enabled = enable ? 1 : 0;
  return ck_oar_ok;
}

int oar_limiter_process(oar_limiter_t *lim, oar_audio_block_t *output) {
  if (!lim || !output || !output->data) return ck_oar_error_inval;

  /* Passthrough when disabled or no peak limiter */
  if (!lim->enabled || !lim->peak_limiter) return ck_oar_ok;

  int samples = (int)output->samples_per_channel;

  /* Process through peak limiter: output->data -> out_buf */
  int returned = audio_effect_peak_limiter_process_block(
      lim->peak_limiter, output->data, lim->out_buf, samples);

  if (returned < 0) {
    warning("Limiter process error: %d", returned);
    output->samples_per_channel = 0;
    return ck_oar_error_inval;
  } else if (returned > 0) {
    for (int c = 0; c < lim->num_channels; c++) {
      memcpy(&output->data[c * returned], &lim->out_buf[c * returned],
             returned * sizeof(float));
    }
  }

  output->samples_per_channel = (uint32_t)returned;
  return ck_oar_ok;
}

int oar_limiter_flush(oar_limiter_t *lim, oar_audio_block_t *output) {
  if (!lim || !output || !output->data) return ck_oar_error_inval;

  /* If disabled or no delay buffer, return 0 samples */
  if (!lim->enabled || !lim->peak_limiter || lim->delay_size <= 0) {
    output->samples_per_channel = 0;
    return ck_oar_ok;
  }

  /* Flush: drains delay buffer into out_buf, then resets state. */
  int returned =
      audio_effect_peak_limiter_flush(lim->peak_limiter, lim->out_buf);

  if (returned < 0) {
    output->samples_per_channel = 0;
    return ck_oar_error_inval;
  } else if (returned > 0) {
    for (int c = 0; c < lim->num_channels; c++) {
      memcpy(&output->data[c * returned], &lim->out_buf[c * returned],
             returned * sizeof(float));
    }
  }

  output->samples_per_channel = (uint32_t)returned;
  return ck_oar_ok;
}

int oar_limiter_set_threshold(oar_limiter_t *lim, float threshold_db) {
  if (!lim) return ck_oar_error_inval;
  audio_effect_peak_limiter_set_threshold(lim->peak_limiter, threshold_db);
  return ck_oar_ok;
}

int oar_limiter_get_delay(const oar_limiter_t *lim) {
  return lim ? lim->delay_size : 0;
}
