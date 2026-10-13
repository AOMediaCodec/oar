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

#include "block_processing_channel.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cdeque.h"
#include "clog.h"
#include "cvalue.h"
#include "definitions.h"
#include "interpret_object_metadata.h"
#include "gain_calculator/gain_calculator.h"
#include "object_based_audio_renderer.h"
#include "processing_block.h"

/* The metadata blocks a channel holds at creation; add_metadata() doubles
 * the ring when a caller queues more blocks than it holds. */
#define def_metadata_ring_size 8

struct BlockProcessingChannel {
  /* Metadata blocks not yet interpreted, oldest first, in a ring of
   * metadata_capacity blocks starting at metadata_head. */
  metadata_block_t* metadata_blocks;
  int metadata_capacity;
  int metadata_head;
  int metadata_count;

  interpret_object_metadata_t* interpret_object_metadata;
  deque_t* processing_queue;
};

static int _refil_processing_queue(block_processing_channel_t* self,
                                   int sample_rate, uint64_t start);
static void __processing_block_destroy(processing_block_t* block);
static void _debug_print_metadata_block(metadata_block_t* metadata,
                                        const char* prefix);

block_processing_channel_t* block_processing_channel_create(
    layout_gain_calculator_t* gain_calculator, uint64_t max_block_samples) {
  block_processing_channel_t* self = 0;
  interpret_object_metadata_t* interpret_object_metadata =
      interpret_object_metadata_create(gain_calculator, max_block_samples);
  if (!interpret_object_metadata) return 0;

  self = def_mallocz(block_processing_channel_t, 1);
  if (!self) {
    interpret_object_metadata_destroy(interpret_object_metadata);
    return 0;
  }

  self->interpret_object_metadata = interpret_object_metadata;

  self->metadata_blocks = def_mallocz(metadata_block_t, def_metadata_ring_size);
  if (!self->metadata_blocks) {
    error(
        "could not allocate memory for metadata blocks in block processing "
        "channel");
    block_processing_channel_destroy(self);
    return 0;
  }
  self->metadata_capacity = def_metadata_ring_size;

  /* The queue holds one processing block at a time. */
  self->processing_queue = deque_new();
  if (!self->processing_queue || deque_reserve(self->processing_queue, 1) < 0) {
    block_processing_channel_destroy(self);
    return 0;
  }

  return self;
}

int block_processing_channel_add_metadata(block_processing_channel_t* self,
                                          metadata_block_t* metadata) {
  if (!self || !metadata) return -22;

  if (self->metadata_count == self->metadata_capacity) {
    const int capacity = self->metadata_capacity * 2;
    metadata_block_t* blocks = def_mallocz(metadata_block_t, capacity);
    if (!blocks) return -12;
    for (int i = 0; i < self->metadata_count; ++i) {
      blocks[i] = self->metadata_blocks[(self->metadata_head + i) %
                                        self->metadata_capacity];
    }
    free(self->metadata_blocks);
    self->metadata_blocks = blocks;
    self->metadata_capacity = capacity;
    self->metadata_head = 0;
  }

  self->metadata_blocks[(self->metadata_head + self->metadata_count) %
                        self->metadata_capacity] = *metadata;
  ++self->metadata_count;

  _debug_print_metadata_block(metadata, "Original ");

  return 0;
}

int block_processing_channel_process(block_processing_channel_t* self,
                                     int sample_rate, uint64_t offset,
                                     float* input, int num_samples,
                                     int channels, float* output) {
  uint64_t start_sample = offset;
  uint64_t end_sample = start_sample + num_samples;

  _refil_processing_queue(self, sample_rate, start_sample);

  while (!deque_is_empty(self->processing_queue)) {
    value_wrap_t v;
    processing_block_t* processing_block =
        def_value_wrap_ptr(deque_at(self->processing_queue, 0));

    processing_block->process(processing_block, start_sample, input,
                              num_samples, channels, output);

    if (processing_block->last_sample < end_sample) {
      /* The block ends inside this call; the next one must not start
       * before it ends. */
      const uint64_t next_start = processing_block->last_sample;
      deque_pop_front(self->processing_queue, &v);
      processing_block->destroy(processing_block);
      _refil_processing_queue(self, sample_rate, next_start);
    } else if (processing_block->last_sample == end_sample) {
      deque_pop_front(self->processing_queue, &v);
      processing_block->destroy(processing_block);
    } else
      break;
  }

  return 0;
}

void block_processing_channel_destroy(block_processing_channel_t* self) {
  if (!self) return;
  /* A queued processing block lives in the interpreter's storage, so the
   * queue goes before the interpreter. */
  if (self->processing_queue)
    deque_free(self->processing_queue,
               def_default_free_ptr(__processing_block_destroy));
  if (self->interpret_object_metadata)
    interpret_object_metadata_destroy(self->interpret_object_metadata);
  def_free(self->metadata_blocks);
  free(self);
}

/* Takes the oldest metadata block out of the ring into *block. */
static int _next_metadata_block(block_processing_channel_t* self,
                                metadata_block_t* block) {
  if (self->metadata_count == 0) return 0;
  *block = self->metadata_blocks[self->metadata_head];
  self->metadata_head = (self->metadata_head + 1) % self->metadata_capacity;
  --self->metadata_count;
  return 1;
}

static int _refil_processing_queue(block_processing_channel_t* self,
                                   int sample_rate, uint64_t start) {
  while (deque_is_empty(self->processing_queue)) {
    metadata_block_t block;
    processing_block_t* processing_block = 0;
    if (!_next_metadata_block(self, &block)) return 0;

    processing_block = interpret_object_metadata_process(
        self->interpret_object_metadata, sample_rate, &block);
    if (!processing_block) continue;
    if (processing_block->first_sample < start) {
      error(
          "metadata underrun: metadata starting at sample %llu arrived after "
          "sample %llu",
          (unsigned long long)processing_block->first_sample,
          (unsigned long long)start);
      processing_block->destroy(processing_block);
      return -22;
    }

    deque_push_back(self->processing_queue,
                    def_value_wrap_instance_ptr(processing_block));
  }
  return 1;
}

static void __processing_block_destroy(processing_block_t* block) {
  if (block->destroy) block->destroy(block);
}

static void _debug_print_metadata_block(metadata_block_t* metadata,
                                        const char* prefix) {
#ifdef __dbg__
  if (!metadata) {
    debug("%sMetadata block is NULL", prefix ? prefix : "");
    return;
  }

  debug(
      "%sMetadata block - track_id: %u, start: %lu, duration: %lu, "
      "azimuth: %f, elevation: %f, distance: %f",
      prefix ? prefix : "", metadata->track_id, metadata->start,
      metadata->duration, metadata->azimuth, metadata->elevation,
      metadata->distance);
#endif
}
