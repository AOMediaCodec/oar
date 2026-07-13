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

#include "obr/common/misc_math.h"

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <vector>

#include "gtest/gtest.h"

namespace obr {

namespace {

TEST(MiscMath, GreatestCommonDivisorTest) {
  const std::vector<int> a_values = {2, 10, 3, 5, 48000, 7, -2, 2, -3};
  const std::vector<int> b_values = {8, 4, 1, 10, 24000, 13, 6, -6, -9};
  const std::vector<int> expected = {2, 2, 1, 5, 24000, 1, 2, 2, 3};

  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(expected[i], FindGcd(a_values[i], b_values[i]));
  }
}

TEST(MiscMath, NextPowTwoTest) {
  const std::vector<size_t> inputs = {2, 10, 3, 5, 48000, 7, 23, 32};
  const std::vector<size_t> expected = {2, 16, 4, 8, 65536, 8, 32, 32};

  for (size_t i = 0; i < inputs.size(); ++i) {
    EXPECT_EQ(expected[i], NextPowTwo(inputs[i]));
  }
}

TEST(MiscMath, FastReciprocalSqrtTest) {
  const std::vector<float> kNumbers{130.0f, 13.0f,  1.3f,
                                    0.13f,  0.013f, 0.0013f};
  const float kSqrtEpsilon = 2e-3f;
  for (auto& number : kNumbers) {
    const float actual = std::sqrt(number);
    const float approximate = 1.0f / FastReciprocalSqrt(number);
    EXPECT_LT(std::abs(actual - approximate) / actual, kSqrtEpsilon);
  }
}

TEST(MiscMath, IntegerPow) {
  const float kFloatValue = 1.5f;
  const float kNegativeFloatValue = -3.3f;
  const size_t kSizeTValue = 11U;
  const int kIntValue = 5;
  const int kNegativeIntValue = -13;

  for (int exponent = 0; exponent < 5; ++exponent) {
    EXPECT_FLOAT_EQ(IntegerPow(kFloatValue, exponent),
                    std::pow(kFloatValue, static_cast<float>(exponent)));
    EXPECT_FLOAT_EQ(
        IntegerPow(kNegativeFloatValue, exponent),
        std::pow(kNegativeFloatValue, static_cast<float>(exponent)));
    EXPECT_EQ(IntegerPow(kSizeTValue, exponent),
              std::pow(kSizeTValue, exponent));
    EXPECT_EQ(IntegerPow(kIntValue, exponent), std::pow(kIntValue, exponent));
    EXPECT_EQ(IntegerPow(kNegativeIntValue, exponent),
              std::pow(kNegativeIntValue, exponent));
  }
}

TEST(MiscMath, UnwrapAzimuthElevation) {
  struct TestCase {
    float azimuth_in;
    float elevation_in;
    float azimuth_expected;
    float elevation_expected;
  };

  const std::vector<TestCase> cases = {
      // No change needed
      {0.0f, 0.0f, 0.0f, 0.0f},
      {180.0f, 45.0f, 180.0f, 45.0f},
      {-180.0f, 45.0f, 180.0f, 45.0f},
      // Azimuth wrap
      {370.0f, 10.0f, 10.0f, 10.0f},
      {-190.0f, 10.0f, 170.0f, 10.0f},
      // Elevation > 90, should mirror and rotate azimuth
      {30.0f, 100.0f, -150.0f, 80.0f},
      // Elevation < -90, should mirror and rotate azimuth
      {60.0f, -100.0f, -120.0f, -80.0f},
      // Multiple wraps
      {10.0f, 270.0f, -170.0f, -90.0f},
      {10.0f, -270.0f, -170.0f, 90.0f},
      // Edge cases
      {0.0f, 90.0f, 0.0f, 90.0f},
      {0.0f, -90.0f, 0.0f, -90.0f},
  };

  for (auto [azimuth_in, elevation_in, azimuth_expected, elevation_expected] :
       cases) {
    UnwrapAzimuthElevation(azimuth_in, elevation_in);
    EXPECT_NEAR(azimuth_in, azimuth_expected, 1e-4f);
    EXPECT_NEAR(elevation_in, elevation_expected, 1e-4f);
  }
}

}  // namespace

}  // namespace obr
