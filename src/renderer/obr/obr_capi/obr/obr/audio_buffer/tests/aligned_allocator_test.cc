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

#include "obr/audio_buffer/aligned_allocator.h"

#include <cstddef>
#include <vector>

#include "gtest/gtest.h"

namespace obr {
namespace {

// Helper method to test memory alignment.
template <size_t Alignment>
void TestAlignedAllocator() {
  static const size_t kRuns = 1000;
  for (size_t run = 0; run < kRuns; ++run) {
    std::vector<float, AlignedAllocator<float, Alignment> > aligned_vector(1);
    const bool is_aligned =
        ((reinterpret_cast<size_t>(&aligned_vector[0]) & (Alignment - 1)) == 0);
    EXPECT_TRUE(is_aligned);
  }
}

// Allocates multiple std::vectors using the AlignedAllocator and tests if the
// allocated memory is aligned.
TEST(AlignedAlocatorTest, TestAlignment) {
  TestAlignedAllocator<2>();
  TestAlignedAllocator<4>();
  TestAlignedAllocator<16>();
  TestAlignedAllocator<32>();
  TestAlignedAllocator<64>();
}

}  // namespace
}  // namespace obr
