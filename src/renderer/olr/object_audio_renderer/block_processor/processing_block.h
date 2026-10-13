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

#ifndef __PROCESSING_BLOCK_H__
#define __PROCESSING_BLOCK_H__

#include <stdint.h>

#include "oar_config.h"

typedef struct ProcessingBlock {
  uint64_t first_sample;
  uint64_t last_sample;

  uint64_t start_sample;
  uint64_t end_sample;

  int (*process)(struct ProcessingBlock *self, uint64_t start, float *in,
                 int num_samples, int channels, float *out);
  void (*destroy)(struct ProcessingBlock *self);
} processing_block_t;

processing_block_t *fixed_gains_create(uint64_t start_sample,
                                       uint64_t end_sample, auto_float_t *gain,
                                       int num);
processing_block_t *interp_gains_create(uint64_t start_sample,
                                        uint64_t end_sample,
                                        auto_float_t *gains_start,
                                        auto_float_t *gains_end, int num);

/* Storage for one processing block at a time, with an interpolation table
 * for blocks of up to max_samples samples. fixed_gains_init() and
 * interp_gains_init() build their block in the storage instead of
 * allocating it; the block's destroy() does nothing, and the next init
 * replaces the block. interp_gains_init() grows the table for a longer
 * block. */
typedef struct ProcessingBlockStorage processing_block_storage_t;

processing_block_storage_t *processing_block_storage_create(
    uint64_t max_samples);
void processing_block_storage_destroy(processing_block_storage_t *storage);
processing_block_t *fixed_gains_init(processing_block_storage_t *storage,
                                     uint64_t start_sample, uint64_t end_sample,
                                     auto_float_t *gain, int num);
processing_block_t *interp_gains_init(processing_block_storage_t *storage,
                                      uint64_t start_sample,
                                      uint64_t end_sample,
                                      auto_float_t *gains_start,
                                      auto_float_t *gains_end, int num);

#endif
