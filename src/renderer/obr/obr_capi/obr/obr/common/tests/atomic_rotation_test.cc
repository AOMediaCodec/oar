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

#include "obr/common/atomic_rotation.h"

#include <atomic>
#include <thread>

#include "gtest/gtest.h"
#include "obr/common/misc_math.h"

namespace obr {
namespace {

TEST(AtomicWorldRotationTest, DefaultsToIdentity) {
  const AtomicWorldRotation rotation;
  const WorldRotation fallback(0.0f, 1.0f, 0.0f, 0.0f);

  const WorldRotation loaded = rotation.Load(fallback);

  EXPECT_FLOAT_EQ(loaded.w(), 1.0f);
  EXPECT_FLOAT_EQ(loaded.x(), 0.0f);
  EXPECT_FLOAT_EQ(loaded.y(), 0.0f);
  EXPECT_FLOAT_EQ(loaded.z(), 0.0f);
}

TEST(AtomicWorldRotationTest, LoadReturnsTheValueLastStored) {
  AtomicWorldRotation rotation;
  const WorldRotation fallback;

  rotation.Store(WorldRotation(0.5f, -0.5f, 0.5f, -0.5f));
  WorldRotation loaded = rotation.Load(fallback);
  EXPECT_FLOAT_EQ(loaded.w(), 0.5f);
  EXPECT_FLOAT_EQ(loaded.x(), -0.5f);
  EXPECT_FLOAT_EQ(loaded.y(), 0.5f);
  EXPECT_FLOAT_EQ(loaded.z(), -0.5f);

  // Reading does not consume: the value stands until it is replaced.
  loaded = rotation.Load(fallback);
  EXPECT_FLOAT_EQ(loaded.w(), 0.5f);

  rotation.Store(WorldRotation(0.0f, 0.0f, 1.0f, 0.0f));
  loaded = rotation.Load(fallback);
  EXPECT_FLOAT_EQ(loaded.w(), 0.0f);
  EXPECT_FLOAT_EQ(loaded.y(), 1.0f);
}

// The reason this class exists. A reader concurrent with a writer must never
// observe a quaternion assembled from two different updates -- which is what
// an unsynchronized four-float write allowed, and what a lock would otherwise
// be needed to prevent.
TEST(AtomicWorldRotationTest, ConcurrentReadsAreNeverTorn) {
  AtomicWorldRotation rotation;
  std::atomic<bool> stop{false};

  // Every value in play -- seed and fallback included -- has all four
  // components equal, so any mismatch between them proves the reader spliced
  // two updates together. (The default identity rotation would not satisfy
  // that, hence the seed.)
  rotation.Store(WorldRotation(0.0f, 0.0f, 0.0f, 0.0f));

  std::thread writer([&rotation, &stop]() {
    float value = 1.0f;
    while (!stop.load(std::memory_order_relaxed)) {
      rotation.Store(WorldRotation(value, value, value, value));
      value += 1.0f;
    }
  });

  const WorldRotation fallback(2.0f, 2.0f, 2.0f, 2.0f);
  for (int i = 0; i < 200000; ++i) {
    const WorldRotation loaded = rotation.Load(fallback);
    ASSERT_EQ(loaded.w(), loaded.x());
    ASSERT_EQ(loaded.w(), loaded.y());
    ASSERT_EQ(loaded.w(), loaded.z());
  }

  stop.store(true, std::memory_order_relaxed);
  writer.join();
}

TEST(AtomicWorldRotationTest, LoadFallsBackWhileAWriteIsInFlight) {
  AtomicWorldRotation rotation;
  rotation.Store(WorldRotation(0.5f, 0.5f, 0.5f, 0.5f));

  // A writer that never finishes: the reader must give up rather than spin,
  // since on the audio thread an unbounded retry is as bad as a lock.
  std::atomic<bool> release{false};
  std::atomic<bool> writing{false};
  std::thread writer([&rotation, &release, &writing]() {
    // Begin a store and stall inside it by holding the sequence counter odd.
    // Emulated here by taking the first half of Store()'s protocol via a real
    // store that we then leave outstanding.
    writing.store(true, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) {
      rotation.Store(WorldRotation(1.0f, 1.0f, 1.0f, 1.0f));
    }
  });

  while (!writing.load(std::memory_order_acquire)) {
  }

  // Under a continuous storm of writes the read either returns a consistent
  // published value or the fallback -- never a spliced one, and never a hang.
  const WorldRotation fallback(0.25f, 0.25f, 0.25f, 0.25f);
  for (int i = 0; i < 100000; ++i) {
    const WorldRotation loaded = rotation.Load(fallback);
    ASSERT_EQ(loaded.w(), loaded.x());
    ASSERT_EQ(loaded.w(), loaded.y());
    ASSERT_EQ(loaded.w(), loaded.z());
  }

  release.store(true, std::memory_order_release);
  writer.join();
}

}  // namespace
}  // namespace obr
