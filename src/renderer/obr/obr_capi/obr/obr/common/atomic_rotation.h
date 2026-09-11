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

#ifndef OBR_COMMON_ATOMIC_ROTATION_H_
#define OBR_COMMON_ATOMIC_ROTATION_H_

#include <atomic>
#include <cstdint>

#include "obr/common/misc_math.h"

namespace obr {

/*!\brief Lock-free handoff of a head rotation to the audio thread.
 *
 * `ObrImpl::Process()` runs on a real-time audio thread, where it must never
 * wait on another thread. Head rotation is written from wherever the head
 * tracker lives -- typically a sensor callback or the application's control
 * thread -- so the two need to exchange a value without a lock.
 *
 * Guarding the quaternion with `mutex_` instead would make audio progress
 * depend on the control thread being scheduled. `absl::Mutex` does not
 * implement priority inheritance, so a control thread preempted while holding
 * the lock stalls the audio thread until the scheduler runs it again, which is
 * heard as a dropout. Nor can the quaternion simply be made a
 * `std::atomic<WorldRotation>`: at 16 bytes that is not lock-free on the
 * platforms obr targets, and the implementation would take a hidden lock on
 * the audio thread -- the very thing being avoided.
 *
 * This is therefore a seqlock. The writer never waits. The reader detects a
 * write that landed mid-copy by re-reading the sequence counter, and retries.
 *
 * The retry budget is deliberately bounded. A textbook seqlock reader spins
 * until it gets a clean copy, which is unbounded if the writer is preempted
 * between its two counter updates -- on an audio thread that trades a lock for
 * a spin, which is no better. Instead `Load()` gives up after a few attempts
 * and returns the caller's fallback, so the read is wait-free. Reusing the
 * previous block's rotation for one buffer is inaudible; missing a deadline is
 * not.
 *
 * The quaternion is stored as four `std::atomic<float>` so that concurrent
 * access is defined behaviour rather than a data race that happens to be
 * benign in practice. Relaxed ordering suffices for the components because the
 * sequence counter carries the acquire/release edges.
 *
 * Single writer, single reader.
 */
class AtomicWorldRotation {
 public:
  AtomicWorldRotation() { Store(WorldRotation()); }

  /*!\brief Publishes a rotation. Never blocks. Call from one thread only.
   *
   * \param rotation Rotation to publish.
   */
  void Store(const WorldRotation& rotation) {
    const uint32_t sequence = sequence_.load(std::memory_order_relaxed);

    // An odd counter marks the value as being written.
    sequence_.store(sequence + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    w_.store(rotation.w(), std::memory_order_relaxed);
    x_.store(rotation.x(), std::memory_order_relaxed);
    y_.store(rotation.y(), std::memory_order_relaxed);
    z_.store(rotation.z(), std::memory_order_relaxed);

    // Even again: the value is settled and safe to read.
    std::atomic_thread_fence(std::memory_order_release);
    sequence_.store(sequence + 2, std::memory_order_relaxed);
  }

  /*!\brief Reads the most recently published rotation. Never blocks.
   *
   * \param fallback Returned if no consistent value could be read within the
   *        retry budget, which means a write was in flight throughout. The
   *        caller should pass the value it last read.
   * \return The published rotation, or `fallback`.
   */
  WorldRotation Load(const WorldRotation& fallback) const {
    for (int attempt = 0; attempt < kMaxLoadAttempts; ++attempt) {
      const uint32_t before = sequence_.load(std::memory_order_acquire);

      // Odd means a write is in flight; there is no point reading the
      // components until it completes.
      if ((before & 1u) != 0u) {
        continue;
      }

      const float w = w_.load(std::memory_order_relaxed);
      const float x = x_.load(std::memory_order_relaxed);
      const float y = y_.load(std::memory_order_relaxed);
      const float z = z_.load(std::memory_order_relaxed);

      std::atomic_thread_fence(std::memory_order_acquire);

      // Unchanged counter means no write overlapped the four loads above,
      // so the components belong to the same quaternion.
      if (sequence_.load(std::memory_order_relaxed) == before) {
        return WorldRotation(w, x, y, z);
      }
    }

    return fallback;
  }

 private:
  // Enough that losing every attempt requires the writer to be preempted
  // mid-write, which is the case the fallback exists for.
  static constexpr int kMaxLoadAttempts = 4;

  std::atomic<uint32_t> sequence_{0};
  std::atomic<float> w_{1.0f};
  std::atomic<float> x_{0.0f};
  std::atomic<float> y_{0.0f};
  std::atomic<float> z_{0.0f};
};

}  // namespace obr

#endif  // OBR_COMMON_ATOMIC_ROTATION_H_
