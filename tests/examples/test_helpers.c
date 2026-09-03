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

#include "test_helpers.h"

#include <math.h>
#include <stdlib.h>

/* --- Config helpers ----------------------------------------------------- */

oar_config_t create_config(oar_layout_t layout, uint32_t samples_per_channel,
                           uint32_t sampling_rate) {
  oar_config_t config;
  memset(&config, 0, sizeof(config));
  config.target_layout = layout;
  config.samples_per_channel = samples_per_channel;
  config.sampling_rate = sampling_rate;
  return config;
}

/* --- Signal generation -------------------------------------------------- */

void generate_sine(float *buffer, uint32_t samples, float freq, float rate) {
  for (uint32_t i = 0; i < samples; ++i) {
    buffer[i] = (float)sin(2.0 * M_PI * freq * ((float)i / rate));
  }
}

void generate_sine_channel(float *buffer, uint32_t samples, uint32_t channels,
                           uint32_t ch_idx, float freq, float rate) {
  (void)channels; /* channels unused; ch_idx * samples is the planar offset */
  for (uint32_t i = 0; i < samples; ++i) {
    buffer[ch_idx * samples + i] =
        (float)sin(2.0 * M_PI * freq * ((float)i / rate));
  }
}

void generate_dual_tone_stimulus(float *buffer, uint32_t samples,
                                 float sample_rate, float freq_low,
                                 float freq_high) {
  for (uint32_t i = 0; i < samples; ++i) {
    float t = (float)i / sample_rate;
    buffer[i] = 0.45f * (float)sin(2.0 * M_PI * freq_low * t) +
                0.45f * (float)sin(2.0 * M_PI * freq_high * t);
  }
}

/* --- Element config helpers --------------------------------------------- */

oar_audio_element_config_t create_object_element_config(int num_objects) {
  oar_audio_element_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.type = ck_object_based;
  cfg.obc.num_objects = num_objects;
  return cfg;
}

oar_audio_element_config_t create_channel_element_config(oar_layout_t layout) {
  oar_audio_element_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.type = ck_channel_based;
  cfg.cbc.layout = layout;
  return cfg;
}

oar_audio_element_config_t create_scene_element_config(oar_hoa_t order) {
  oar_audio_element_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));
  cfg.type = ck_scene_based;
  cfg.sbc.order = order;
  return cfg;
}

/* --- Metadata helpers --------------------------------------------------- */

oar_metadata_t *create_object_metadata(const polar_t *positions,
                                       uint32_t num_objects,
                                       uint32_t duration) {
  if (num_objects == 0 || num_objects > def_max_number_of_objects ||
      !positions) {
    return NULL;
  }

  oar_metadata_t *metadata = (oar_metadata_t *)malloc(sizeof(oar_metadata_t));
  if (!metadata) return NULL;

  memset(metadata, 0, sizeof(*metadata));
  metadata->type = ck_metadata_object_positions;
  metadata->duration = (int)duration;
  metadata->object_positions.param_type = ck_param_constant;
  metadata->object_positions.position_type = ck_polar;
  metadata->object_positions.num_objects = num_objects;

  for (uint32_t i = 0; i < num_objects; ++i) {
    metadata->object_positions.polar_positions[i].azimuth =
        positions[i].azimuth;
    metadata->object_positions.polar_positions[i].elevation =
        positions[i].elevation;
    metadata->object_positions.polar_positions[i].distance =
        positions[i].distance;
  }

  return metadata;
}

/* --- Output helpers ------------------------------------------------------ */

int is_output_non_silent(const float *data, uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    if (fabsf(data[i]) > 1e-9f) return 1;
  }
  return 0;
}

int alloc_audio_block(uint32_t channels, uint32_t samples_per_channel,
                      oar_audio_block_t *output) {
  memset(output, 0, sizeof(*output));
  output->channels = channels;
  output->samples_per_channel = samples_per_channel;
  output->data = (float *)calloc(channels * samples_per_channel, sizeof(float));
  return output->data ? 0 : -1;
}

int render_and_check_non_silent(oar_t *oar, oar_audio_block_t *output) {
  memset(output->data, 0,
         output->channels * output->samples_per_channel * sizeof(float));
  if (oar_render(oar, output) != 0) return -1;
  uint32_t total = output->channels * output->samples_per_channel;
  if (!is_output_non_silent(output->data, total)) return -1;
  return 0;
}
