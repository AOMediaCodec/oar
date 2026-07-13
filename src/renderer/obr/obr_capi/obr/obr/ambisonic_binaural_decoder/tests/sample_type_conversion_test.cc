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

#include "obr/ambisonic_binaural_decoder/sample_type_conversion.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace obr {

namespace {

TEST(MiscMath, Int16ToFloatTest) {
  static const int16_t kMinInt16 = -0x7FFF;
  static const int16_t kMaxInt16 = 0x7FFF;
  static const float kMinFloat = -1.0f;
  static const float kMaxFloat = 1.0f;

  static const float kFloatRange = kMaxFloat - kMinFloat;
  static const uint16_t kInt16Range = kMaxInt16 - kMinInt16;

  for (int16_t i = kMinInt16; i < kMaxInt16;
       i = static_cast<int16_t>(i + 0xFF)) {
    const float mapped_float =
        static_cast<float>(i) / static_cast<float>(kInt16Range) * kFloatRange;
    float float_result = 0.0f;
    ConvertSampleToFloatFormat(i, &float_result);
    EXPECT_FLOAT_EQ(mapped_float, float_result);
  }
}

TEST(MiscMath, FloatToInt16Test) {
  static const int16_t kMinInt16 = -0x7FFF;
  static const int16_t kMaxInt16 = 0x7FFF;
  static const float kMinFloat = -1.0f;
  static const float kMaxFloat = 1.0f;
  // NOTE: Int16 maximum is 0x7FFF, NOT 0x8000; see scheme 2) in
  // http://goo.gl/NTRQ1a for background.
  static const float kFloatRange = kMaxFloat - kMinFloat;
  static const uint16_t kInt16Range = kMaxInt16 - kMinInt16;

  for (float i = kMinFloat; i < kMaxFloat; i += 0.005f) {
    const int16_t mapped_int =
        static_cast<int16_t>(i * kInt16Range / kFloatRange);
    int16_t int16_result = 0;
    ConvertSampleFromFloatFormat(i, &int16_result);
    EXPECT_EQ(mapped_int, int16_result);
  }
}

TEST(MiscMath, FloatToInt16TestPositiveSaturate) {
  // Maximum positive value is 2^15 - 1
  static const int16_t kMaxInt16 = 0x7FFF;
  static const float kMaxFloat = 1.0f;
  int16_t int16_result = 0;
  ConvertSampleFromFloatFormat(2 * kMaxFloat, &int16_result);
  EXPECT_EQ(kMaxInt16, int16_result);
}

TEST(MiscMath, FloatToInt16TestNegativeSaturate) {
  static const int16_t kMinInt16 = -0x7FFF;
  static const float kMinFloat = -1.0f;
  int16_t int16_result = 0;
  ConvertSampleFromFloatFormat(2 * kMinFloat, &int16_result);
  EXPECT_EQ(kMinInt16, int16_result);
}

}  // namespace

}  // namespace obr
