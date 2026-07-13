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

#include "obr/common/ambisonic_utils.h"

#include <cstddef>

#include "gtest/gtest.h"

namespace obr {

namespace {

// Ambisonic Channel Numbers (ACNs) used in the tests.
const size_t kAcnChannels[] = {0, 1, 4, 8, 16, 32};

// Number of different ACNs to be tested.
const size_t kNumAcnChannels = sizeof(kAcnChannels) / sizeof(kAcnChannels[0]);

// Tests that the Ambisonic order for a given channel returned by the
// GetPeriphonicAmbisonicOrderForChannel() method is correct.
TEST(UtilsTest, GetPeriphonicAmbisonicOrderForChannelTest) {
  const int kExpectedOrders[] = {0, 1, 2, 2, 4, 5};
  for (size_t channel = 0; channel < kNumAcnChannels; ++channel) {
    EXPECT_EQ(kExpectedOrders[channel],
              GetPeriphonicAmbisonicOrderForChannel(kAcnChannels[channel]));
  }
}

// Tests that the Ambisonic degree for a given channel returned by the
// GetPeriphonicAmbisonicDegreeForChannel() method is correct.
TEST(UtilsTest, GetPeriphonicAmbisonicDegreeForChannelTest) {
  const int kExpectedDegrees[] = {0, -1, -2, 2, -4, 2};
  for (size_t channel = 0; channel < kNumAcnChannels; ++channel) {
    EXPECT_EQ(kExpectedDegrees[channel],
              GetPeriphonicAmbisonicDegreeForChannel(kAcnChannels[channel]));
  }
}

// Tests that the ambisonic order validation returns the expected results for
// arbitrary number of channels.
TEST(UtilsTest, IsValidAmbisonicOrderTest) {
  for (size_t valid_channel : {1, 4, 9, 16, 25, 36}) {
    EXPECT_TRUE(IsValidAmbisonicOrder(valid_channel));
  }
  for (size_t invalid_channel : {2, 3, 5, 8, 50, 99}) {
    EXPECT_FALSE(IsValidAmbisonicOrder(invalid_channel));
  }
}

}  // namespace

}  // namespace obr
