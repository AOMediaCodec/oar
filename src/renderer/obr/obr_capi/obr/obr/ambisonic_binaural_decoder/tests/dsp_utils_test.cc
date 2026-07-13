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

#include "obr/ambisonic_binaural_decoder/dsp_utils.h"

#include <cstddef>
#include <vector>

#include "gtest/gtest.h"
#include "obr/audio_buffer/audio_buffer.h"
#include "obr/common/constants.h"

namespace obr {

namespace {

const size_t kHalfHannWindowLength = 8;
const float kExpectedHalfHannWindow[] = {0.0000000f, 0.04322727f, 0.1654347f,
                                         0.3454915f, 0.55226423f, 0.7500000f,
                                         0.9045085f, 0.98907380f};

const size_t kHannWindowLength = 15;
const float kExpectedHannWindow[] = {
    0.0000000f, 0.0495156f, 0.1882551f, 0.3887395f, 0.6112605f,
    0.8117449f, 0.9504844f, 1.0000000f, 0.9504844f, 0.8117449f,
    0.6112605f, 0.3887395f, 0.1882551f, 0.0495156f, 0.0000000f};

// Tests that the ceiled input size in frames matches the expected multiple of
// frames per buffer for arbitrary inputs.
TEST(DspUtilsTest, CeilToMultipleOfFramesPerBufferTest) {
  const size_t kFramesPerBuffer = 512;
  const std::vector<size_t> kInput = {0, 100, 512, 1000, 5000, 10240};
  const std::vector<size_t> kExpectedOutput = {512,  512,  512,
                                               1024, 5120, 10240};

  for (size_t i = 0; i < kInput.size(); ++i) {
    EXPECT_EQ(kExpectedOutput[i],
              CeilToMultipleOfFramesPerBuffer(kInput[i], kFramesPerBuffer));
  }
}

// Tests half-Hann window calculation against values returned by MATLAB's hann()
// function.
TEST(DspUtilsTest, GenerateHalfHannWindowTest) {
  AudioBuffer half_hann_window(kNumMonoChannels, kHalfHannWindowLength);
  GenerateHannWindow(false, kHalfHannWindowLength, &half_hann_window[0]);
  for (size_t i = 0; i < kHalfHannWindowLength; ++i) {
    EXPECT_NEAR(half_hann_window[0][i], kExpectedHalfHannWindow[i],
                kEpsilonFloat);
  }
}

// Tests Hann window generation for odd window lengths.
TEST(DspUtilsTest, GenerateHannWindowOddLengthTest) {
  AudioBuffer hann_window(kNumMonoChannels, kHannWindowLength);
  GenerateHannWindow(true, kHannWindowLength, &hann_window[0]);
  for (size_t i = 0; i < kHannWindowLength; ++i) {
    EXPECT_NEAR(hann_window[0][i], kExpectedHannWindow[i], kEpsilonFloat);
  }
}

}  // namespace

}  // namespace obr
