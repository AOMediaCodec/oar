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

#include "obr/common/atomic_rotation.h"

#include <atomic>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "obr/common/misc_math.h"

namespace obr {
namespace {

// A rotation whose components are `k`, `k + 1`, `k + 2` and `k + 3`, so a
// load that mixes two stores shows as unequal steps. Integers below 2^24 are
// exact in a float.
WorldRotation Numbered(int k) {
  return WorldRotation(static_cast<float>(k), static_cast<float>(k + 1),
                       static_cast<float>(k + 2), static_cast<float>(k + 3));
}

bool IsNumbered(const WorldRotation& r) {
  return r.x() - r.w() == 1.0f && r.y() - r.w() == 2.0f &&
         r.z() - r.w() == 3.0f;
}

const WorldRotation kFallback(-7.0f, -7.0f, -7.0f, -7.0f);

bool IsFallback(const WorldRotation& r) {
  return r.w() == kFallback.w() && r.x() == kFallback.x() &&
         r.y() == kFallback.y() && r.z() == kFallback.z();
}

TEST(AtomicWorldRotationTest, DefaultIsIdentity) {
  const AtomicWorldRotation rotation;
  const WorldRotation loaded = rotation.Load(kFallback);
  EXPECT_EQ(loaded.w(), 1.0f);
  EXPECT_EQ(loaded.x(), 0.0f);
  EXPECT_EQ(loaded.y(), 0.0f);
  EXPECT_EQ(loaded.z(), 0.0f);
}

TEST(AtomicWorldRotationTest, LoadReturnsTheLastStore) {
  AtomicWorldRotation rotation;
  rotation.Store(WorldRotation(0.5f, -0.5f, 0.25f, 0.75f));
  rotation.Store(Numbered(10));
  const WorldRotation loaded = rotation.Load(kFallback);
  EXPECT_EQ(loaded.w(), 10.0f);
  EXPECT_EQ(loaded.x(), 11.0f);
  EXPECT_EQ(loaded.y(), 12.0f);
  EXPECT_EQ(loaded.z(), 13.0f);
}

// One writer stores increasing numbered rotations while the test thread
// loads. Every load returns a whole store or the fallback, and the stores it
// returns never go back in time.
TEST(AtomicWorldRotationTest, NoTornLoadsUnderAConcurrentWriter) {
  AtomicWorldRotation rotation(Numbered(0));
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    for (int k = 1; !stop.load(std::memory_order_relaxed); ++k) {
      rotation.Store(Numbered(k & ((1 << 20) - 1)));
    }
  });

  float last_w = 0.0f;
  int torn = 0;
  int backwards = 0;
  for (int i = 0; i < 200000; ++i) {
    const WorldRotation loaded = rotation.Load(kFallback);
    if (IsFallback(loaded)) continue;
    if (!IsNumbered(loaded)) {
      ++torn;
      continue;
    }
    // The writer wraps at 2^20; a smaller value after a large one is a wrap.
    if (loaded.w() < last_w && last_w - loaded.w() < (1 << 19)) ++backwards;
    last_w = loaded.w();
  }
  stop.store(true, std::memory_order_relaxed);
  writer.join();

  EXPECT_EQ(torn, 0);
  EXPECT_EQ(backwards, 0);
}

// Several writers store as fast as they can. `Load()` returns after at most
// `kMaxLoadAttempts` attempts each time, with a whole store or the fallback.
TEST(AtomicWorldRotationTest, LoadIsBoundedUnderAWriteStorm) {
  AtomicWorldRotation rotation(Numbered(0));
  std::atomic<bool> stop{false};
  std::vector<std::thread> writers;
  for (int t = 0; t < 4; ++t) {
    writers.emplace_back([&rotation, &stop, t] {
      for (int k = t; !stop.load(std::memory_order_relaxed); k += 4) {
        rotation.Store(Numbered(k & ((1 << 20) - 1)));
      }
    });
  }

  int torn = 0;
  int fallbacks = 0;
  for (int i = 0; i < 100000; ++i) {
    const WorldRotation loaded = rotation.Load(kFallback);
    if (IsFallback(loaded)) {
      ++fallbacks;
    } else if (!IsNumbered(loaded)) {
      ++torn;
    }
  }
  stop.store(true, std::memory_order_relaxed);
  for (auto& writer : writers) writer.join();

  EXPECT_EQ(torn, 0);
  // Whether a load fell back depends on scheduling; record it.
  RecordProperty("fallbacks", fallbacks);
}

}  // namespace
}  // namespace obr
