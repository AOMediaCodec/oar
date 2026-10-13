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

#ifndef __RENDERER_H__
#define __RENDERER_H__

#include <stdint.h>

#include "gain_calculator/gain_calculator.h"
#include "object_based_audio_renderer.h"

typedef struct RendererImpl renderer_impl_t;

/* max_block_samples is the most samples a render call processes. */
renderer_impl_t *renderer_impl_create(layout_gain_calculator_t *gain_calculator,
                                      int sample_rate, int max_block_samples);
void renderer_impl_destroy(renderer_impl_t *impl);
/* Creates the processing channels of tracks 0 to num_tracks - 1, which
 * add_metadatas() otherwise creates on a track's first metadata block. */
int renderer_impl_add_tracks(renderer_impl_t *impl, int num_tracks);
int renderer_impl_add_metadatas(renderer_impl_t *impl,
                                metadata_block_t *metadata_blocks, int n);
int renderer_impl_render(renderer_impl_t *impl, float *input, int samples,
                         int channels, uint64_t offset, float *out);
#endif  // __RENDERER_H__
