/*
 * Copyright (c) 2026, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 3-Clause Clear License
 * and the Alliance for Open Media Patent License 1.0. If the BSD 3-Clause Clear
 * License was not distributed with this source code in the LICENSE file, you
 * can obtain it at www.aomedia.org/license/software-license/bsd-3-c-c. If the
 * Alliance for Open Media Patent License 1.0 was not distributed with this
 * source code in the PATENTS file, you can obtain it at
 * www.aomedia.org/license/patent.
 */

#ifndef OBR_COMMON_ATOMIC_ROTATION_H_
#define OBR_COMMON_ATOMIC_ROTATION_H_

#include <atomic>
#include <cstdint>

#include "obr/common/misc_math.h"

namespace obr {

/*!\brief A `WorldRotation` written by one or more threads and read by
 * another without a lock.
 *
 * The four quaternion components are atomic floats behind a sequence
 * counter that is odd while a write is in progress. `std::atomic` of the
 * whole 16-byte quaternion is not guaranteed to be lock-free on every
 * platform OBR targets.
 * `Store()` waits for a concurrent `Store()` to finish; `Load()` never waits:
 * after `kMaxLoadAttempts` attempts that overlap a write it returns the
 * caller's previous value.
 */
class AtomicWorldRotation {
 public:
  /*!\brief The number of times `Load()` reads the components before it
   * returns the fallback.
   */
  static constexpr int kMaxLoadAttempts = 4;

  /*!\brief Constructs an identity rotation. */
  AtomicWorldRotation() : AtomicWorldRotation(WorldRotation()) {}

  /*!\brief Constructs a rotation holding `rotation`. */
  explicit AtomicWorldRotation(const WorldRotation& rotation)
      : w_(rotation.w()),
        x_(rotation.x()),
        y_(rotation.y()),
        z_(rotation.z()) {}

  AtomicWorldRotation(const AtomicWorldRotation&) = delete;
  AtomicWorldRotation& operator=(const AtomicWorldRotation&) = delete;

  /*!\brief Publishes `rotation`. Safe to call from several threads. */
  void Store(const WorldRotation& rotation) {
    // Make the sequence odd. Another writer holding it odd is storing four
    // floats, so the wait is short while that writer runs; it spins for as
    // long as that writer is preempted. The acquire on success orders this
    // store after the previous writer's, whose final sequence store it reads.
    uint32_t sequence = sequence_.load(std::memory_order_relaxed);
    do {
      while (sequence & 1u) {
        sequence = sequence_.load(std::memory_order_relaxed);
      }
    } while (!sequence_.compare_exchange_weak(sequence, sequence + 1,
                                              std::memory_order_acquire,
                                              std::memory_order_relaxed));
    // Orders the odd sequence before the component stores, so a reader that
    // sees a new component also sees a changed sequence.
    std::atomic_thread_fence(std::memory_order_release);
    w_.store(rotation.w(), std::memory_order_relaxed);
    x_.store(rotation.x(), std::memory_order_relaxed);
    y_.store(rotation.y(), std::memory_order_relaxed);
    z_.store(rotation.z(), std::memory_order_relaxed);
    sequence_.store(sequence + 2, std::memory_order_release);
  }

  /*!\brief Returns the last stored rotation, or `fallback` when every one of
   * `kMaxLoadAttempts` reads overlapped a `Store()`.
   */
  WorldRotation Load(const WorldRotation& fallback) const {
    for (int attempt = 0; attempt < kMaxLoadAttempts; ++attempt) {
      const uint32_t before = sequence_.load(std::memory_order_acquire);
      if (before & 1u) continue;
      const float w = w_.load(std::memory_order_relaxed);
      const float x = x_.load(std::memory_order_relaxed);
      const float y = y_.load(std::memory_order_relaxed);
      const float z = z_.load(std::memory_order_relaxed);
      // Orders the component loads before the second sequence load.
      std::atomic_thread_fence(std::memory_order_acquire);
      if (sequence_.load(std::memory_order_relaxed) == before) {
        return WorldRotation(w, x, y, z);
      }
    }
    return fallback;
  }

 private:
  std::atomic<uint32_t> sequence_{0};
  std::atomic<float> w_;
  std::atomic<float> x_;
  std::atomic<float> y_;
  std::atomic<float> z_;
};

}  // namespace obr

#endif  // OBR_COMMON_ATOMIC_ROTATION_H_
