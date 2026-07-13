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

#include "obr/common/test_util.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>

#include "gtest/gtest.h"
#include "obr/audio_buffer/audio_buffer.h"
#include "obr/common/constants.h"

namespace obr {

namespace {

TEST(TestUtilTest, GenerateSineWave_SuccessfulGeneration) {
  const size_t kLengthStart = 1;
  const size_t kLengthStop = 200;
  const size_t kLengthStep = 10;
  for (size_t length = kLengthStart; length <= kLengthStop;
       length += kLengthStep) {
    const float kFrequencyStart = 0.0f;
    const float kFrequencyStop = 2000.0f;
    const float kFrequencyStep = 100.0f;
    for (float frequency = kFrequencyStart; frequency <= kFrequencyStop;
         frequency += kFrequencyStep) {
      const int kSampleRate = 2000;
      AudioBuffer expected_signal(1, length);
      AudioBuffer::Channel& expected_signal_view = expected_signal[0];
      for (size_t i = 0; i < length; i++) {
        const float phase = static_cast<float>(i) * 2.0f *
                            std::numbers::pi_v<float> / kSampleRate * frequency;
        const float expected_value_float = std::sin(phase);
        expected_signal_view[i] = expected_value_float;
      }

      AudioBuffer sine_wave(1U, length);
      GenerateSineWave(frequency, kSampleRate, &sine_wave[0]);
      EXPECT_TRUE(CompareAudioBuffers(sine_wave[0], expected_signal_view,
                                      kEpsilonFloat));
    }
  }
}

TEST(TestUtilTest, GenerateSawToothSignal_SuccessfulGeneration) {
  const size_t kLengthStart = 1;
  const size_t kLengthStop = 200;
  const size_t kLengthStep = 10;
  for (size_t length = kLengthStart; length <= kLengthStop;
       length += kLengthStep) {
    const size_t kToothLengthStart = 1;
    const size_t kToothLengthStop = 20;
    const size_t kToothLengthStep = 3;
    for (size_t tooth_length = kToothLengthStart;
         tooth_length <= kToothLengthStop; tooth_length += kToothLengthStep) {
      AudioBuffer expected_signal(1, length);
      AudioBuffer::Channel& expected_signal_view = expected_signal[0];
      for (size_t i = 0; i < length; i++) {
        const float expected_value = static_cast<float>(i % tooth_length) /
                                         static_cast<float>(tooth_length) *
                                         2.0f -
                                     1.0f;
        expected_signal_view[i] = expected_value;
      }

      AudioBuffer signal(1, length);
      GenerateSawToothSignal(tooth_length, &signal[0]);
      EXPECT_TRUE(
          CompareAudioBuffers(signal[0], expected_signal_view, kEpsilonFloat));
    }
  }
}

TEST(TestUtilTest, GenerateDiracImpulseFilterFloat_SuccessfulGeneration) {
  const size_t kLengthStart = 1;
  const size_t kLengthStop = 100;
  const size_t kLengthStep = 10;
  for (size_t length = kLengthStart; length <= kLengthStop;
       length += kLengthStep) {
    const size_t kDelayStart = 1;
    const size_t kDelayStop = length - 1;
    const size_t kDelayStep = 3;
    for (size_t delay = kDelayStart; delay <= kDelayStop; delay += kDelayStep) {
      AudioBuffer expected_signal(1, length);
      GenerateSilence(&expected_signal[0]);
      expected_signal[0][delay] = 1.0f;

      AudioBuffer dirac_buffer(1, length);
      GenerateDiracImpulseFilter(delay, &dirac_buffer[0]);
      EXPECT_TRUE(CompareAudioBuffers(dirac_buffer[0], expected_signal[0],
                                      kEpsilonFloat));
    }
  }
}

TEST(TestUtilTest, GenerateIncreasingSignal_SuccessfulGeneration) {
  const size_t kLengthStart = 1;
  const size_t kLengthStop = 200;
  const size_t kLengthStep = 10;
  for (size_t length = kLengthStart; length <= kLengthStop;
       length += kLengthStep) {
    AudioBuffer expected_signal(1, length);
    AudioBuffer::Channel& expected_signal_view = expected_signal[0];
    for (size_t i = 0; i < length; i++) {
      const float expected_value =
          static_cast<float>(i) / static_cast<float>(length) * 2.0f - 1.0f;
      expected_signal_view[i] = expected_value;
    }

    AudioBuffer signal(1, length);
    GenerateIncreasingSignal(&signal[0]);
    EXPECT_TRUE(
        CompareAudioBuffers(signal[0], expected_signal[0], kEpsilonFloat));
  }
}

TEST(TestUtilTest, DelayCompare_SuccessfulEqualDelay) {
  const size_t kLengthStart = 1;
  const size_t kLengthStop = 100;
  const size_t kLengthStep = 10;
  for (size_t length = kLengthStart; length <= kLengthStop;
       length += kLengthStep) {
    AudioBuffer original_signal(1, length);
    AudioBuffer::Channel& original_signal_view = original_signal[0];

    GenerateIncreasingSignal(&original_signal_view);

    const size_t kDelayStart = 0;
    const size_t kDelayStop = length - 1;
    const size_t kDelayStep = 3;
    for (size_t delay = kDelayStart; delay <= kDelayStop; delay += kDelayStep) {
      AudioBuffer delayed_signal(1, length + delay);
      AudioBuffer::Channel& delayed_signal_view = delayed_signal[0];
      GenerateSilence(&delayed_signal_view);
      std::copy(original_signal_view.begin(), original_signal_view.end(),
                delayed_signal_view.begin() + delay);
      const size_t result = DelayCompare(
          original_signal_view, delayed_signal_view, delay, kEpsilonFloat);
      EXPECT_EQ(delayed_signal_view.size(), result);
    }
  }
}

TEST(TestUtilTest, DelayCompare_SuccessfulNotEqualDelay) {
  const size_t kLengthStart = 1;
  const size_t kLengthStop = 20;
  const size_t kLengthStep = 10;
  for (size_t length = kLengthStart; length <= kLengthStop;
       length += kLengthStep) {
    AudioBuffer original_signal(1, length);
    AudioBuffer::Channel& original_signal_view = original_signal[0];

    GenerateIncreasingSignal(&original_signal_view);

    const size_t kDelayStart = 1;
    const size_t kDelayStop = length - 1;
    const size_t kDelayStep = 3;
    for (size_t delay = kDelayStart; delay <= kDelayStop; delay += kDelayStep) {
      // Test altering first delayed element.
      {
        AudioBuffer delayed_signal(1, length + delay);
        AudioBuffer::Channel& delayed_signal_view = delayed_signal[0];
        std::copy(original_signal_view.begin(), original_signal_view.end(),
                  delayed_signal_view.begin() + delay);
        delayed_signal_view[delay] = -100.0f;
        const size_t result = DelayCompare(
            original_signal_view, delayed_signal_view, delay, kEpsilonFloat);
        EXPECT_NE(delayed_signal_view.size(), result);
      }
      // Test altering last delayed element.
      {
        AudioBuffer delayed_signal(1, length + delay);
        AudioBuffer::Channel& delayed_signal_view = delayed_signal[0];
        std::copy(original_signal_view.begin(), original_signal_view.end(),
                  delayed_signal_view.begin() + delay);
        delayed_signal_view[delayed_signal_view.size() - 1] = -100.0f;
        const size_t result = DelayCompare(
            original_signal_view, delayed_signal_view, delay, kEpsilonFloat);
        EXPECT_NE(delayed_signal_view.size(), result);
      }
    }
  }
}

TEST(TestUtilTest, GenerateSilence_ProducesZeroBuffer) {
  const size_t kLength = 128;
  AudioBuffer buffer(1, kLength);
  // Fill with non-zero values first.
  auto& buffer_channel = buffer[0];
  for (auto& sample : buffer_channel) {
    sample = 1.0f;
  }
  GenerateSilence(&buffer_channel);
  for (const auto sample : buffer_channel) {
    EXPECT_EQ(sample, 0.0f);
  }
}

TEST(TestUtilTest, GenerateSineWithRandomPhase_ProducesNonZeroOutput) {
  const size_t kLength = 256;
  const int kSampleRate = 48000;
  AudioBuffer buffer(1, kLength);
  GenerateSineWithRandomPhase(440.0f, 0.5f, kSampleRate, &buffer[0]);
  EXPECT_TRUE(HasNonZeroOutput(buffer));
}

TEST(TestUtilTest, GenerateSineWithRandomPhase_RespectAmplitude) {
  const size_t kLength = 256;
  const int kSampleRate = 48000;
  const float kAmplitude = 0.5f;
  AudioBuffer buffer(1, kLength);
  GenerateSineWithRandomPhase(440.0f, kAmplitude, kSampleRate, &buffer[0]);
  for (size_t i = 0; i < buffer.num_frames(); ++i) {
    EXPECT_LE(std::abs(buffer[0][i]), kAmplitude + kEpsilonFloat);
  }
}

TEST(TestUtilTest,
     GenerateSineWithRandomPhase_DifferentSeedsProduceDifferentPhases) {
  const size_t kLength = 256;
  const int kSampleRate = 48000;
  AudioBuffer buffer1(1, kLength);
  AudioBuffer buffer2(1, kLength);
  GenerateSineWithRandomPhase(440.0f, 0.5f, kSampleRate, &buffer1[0], 42);
  GenerateSineWithRandomPhase(440.0f, 0.5f, kSampleRate, &buffer2[0], 123);
  // Buffers should differ due to different random phases.
  EXPECT_FALSE(CompareAudioBuffers(buffer1[0], buffer2[0], kEpsilonFloat));
}

TEST(TestUtilTest, GenerateSineWithRandomPhase_SameSeedProducesSameOutput) {
  const size_t kLength = 256;
  const int kSampleRate = 48000;
  AudioBuffer buffer1(1, kLength);
  AudioBuffer buffer2(1, kLength);
  GenerateSineWithRandomPhase(440.0f, 0.5f, kSampleRate, &buffer1[0], 42);
  GenerateSineWithRandomPhase(440.0f, 0.5f, kSampleRate, &buffer2[0], 42);
  EXPECT_TRUE(CompareAudioBuffers(buffer1[0], buffer2[0], kEpsilonFloat));
}

TEST(TestUtilTest, HasNonZeroOutput_ReturnsFalseForSilence) {
  const size_t kLength = 128;
  AudioBuffer buffer(2, kLength);
  GenerateSilence(&buffer[0]);
  GenerateSilence(&buffer[1]);
  EXPECT_FALSE(HasNonZeroOutput(buffer));
}

TEST(TestUtilTest, HasNonZeroOutput_ReturnsTrueForNonZero) {
  const size_t kLength = 128;
  AudioBuffer buffer(2, kLength);
  GenerateSilence(&buffer[0]);
  GenerateSilence(&buffer[1]);
  buffer[1][64] = 0.001f;
  EXPECT_TRUE(HasNonZeroOutput(buffer));
}

TEST(TestUtilTest, CompareAudioBuffers_ReturnsTrueForIdenticalBuffers) {
  const size_t kLength = 128;
  AudioBuffer buffer1(1, kLength);
  AudioBuffer buffer2(1, kLength);
  GenerateSineWave(440.0f, 48000, &buffer1[0]);
  GenerateSineWave(440.0f, 48000, &buffer2[0]);
  EXPECT_TRUE(CompareAudioBuffers(buffer1[0], buffer2[0], kEpsilonFloat));
}

TEST(TestUtilTest, CompareAudioBuffers_ReturnsFalseForDifferentBuffers) {
  const size_t kLength = 128;
  AudioBuffer buffer1(1, kLength);
  AudioBuffer buffer2(1, kLength);
  GenerateSineWave(440.0f, 48000, &buffer1[0]);
  GenerateSineWave(880.0f, 48000, &buffer2[0]);
  EXPECT_FALSE(CompareAudioBuffers(buffer1[0], buffer2[0], kEpsilonFloat));
}

TEST(TestUtilTest, CompareAudioBuffers_ReturnsFalseForDifferentSizes) {
  AudioBuffer buffer1(1, 128);
  AudioBuffer buffer2(1, 256);
  GenerateSilence(&buffer1[0]);
  GenerateSilence(&buffer2[0]);
  EXPECT_FALSE(CompareAudioBuffers(buffer1[0], buffer2[0], kEpsilonFloat));
}

TEST(TestUtilTest, CalculateSignalEnergy_ReturnsCorrectEnergy) {
  const size_t kLength = 4;
  AudioBuffer buffer(1, kLength);
  buffer[0][0] = 1.0f;
  buffer[0][1] = 2.0f;
  buffer[0][2] = 3.0f;
  buffer[0][3] = 4.0f;
  // Energy = 1^2 + 2^2 + 3^2 + 4^2 = 1 + 4 + 9 + 16 = 30
  EXPECT_NEAR(CalculateSignalEnergy(buffer[0]), 30.0, kEpsilonFloat);
}

TEST(TestUtilTest, CalculateSignalEnergy_ReturnsZeroForSilence) {
  const size_t kLength = 128;
  AudioBuffer buffer(1, kLength);
  GenerateSilence(&buffer[0]);
  EXPECT_NEAR(CalculateSignalEnergy(buffer[0]), 0.0, kEpsilonFloat);
}

TEST(TestUtilTest,
     GetKroneckerDeltaEncodedToAmbisonics_ReturnsCorrectChannelCount) {
  const size_t kNumFrames = 128;
  const int kOrder1 = 1;
  const int kOrder2 = 2;
  const int kOrder3 = 3;

  auto buffer1 = GetKroneckerDeltaEncodedToAmbisonics(kNumFrames, 0.0f, 0.0f,
                                                      1.0f, kOrder1);
  EXPECT_EQ(buffer1.num_channels(), 4);  // (1+1)^2 = 4
  EXPECT_EQ(buffer1.num_frames(), kNumFrames);

  auto buffer2 = GetKroneckerDeltaEncodedToAmbisonics(kNumFrames, 0.0f, 0.0f,
                                                      1.0f, kOrder2);
  EXPECT_EQ(buffer2.num_channels(), 9);  // (2+1)^2 = 9

  auto buffer3 = GetKroneckerDeltaEncodedToAmbisonics(kNumFrames, 0.0f, 0.0f,
                                                      1.0f, kOrder3);
  EXPECT_EQ(buffer3.num_channels(), 16);  // (3+1)^2 = 16
}

TEST(TestUtilTest, GetKroneckerDeltaEncodedToAmbisonics_HasNonZeroOutput) {
  const size_t kNumFrames = 128;
  auto buffer =
      GetKroneckerDeltaEncodedToAmbisonics(kNumFrames, 45.0f, 30.0f, 1.0f, 2);
  EXPECT_TRUE(HasNonZeroOutput(buffer));
}

TEST(
    TestUtilTest,
    GetKroneckerDeltaEncodedToAmbisonics_DifferentPositionsProduceDifferentOutput) {
  const size_t kNumFrames = 128;
  const int kOrder = 2;

  auto buffer_front = GetKroneckerDeltaEncodedToAmbisonics(kNumFrames, 0.0f,
                                                           0.0f, 1.0f, kOrder);
  auto buffer_left = GetKroneckerDeltaEncodedToAmbisonics(kNumFrames, 90.0f,
                                                          0.0f, 1.0f, kOrder);
  auto buffer_up = GetKroneckerDeltaEncodedToAmbisonics(kNumFrames, 0.0f, 90.0f,
                                                        1.0f, kOrder);

  // Different positions should produce different Ambisonic encodings.
  // Compare a channel that should differ (e.g., channel 1 for Y component).
  EXPECT_FALSE(
      CompareAudioBuffers(buffer_front[1], buffer_left[1], kEpsilonFloat));
  EXPECT_FALSE(
      CompareAudioBuffers(buffer_front[2], buffer_up[2], kEpsilonFloat));
}

TEST(TestUtilTest, GetBroadbandILD_ReturnsZeroForEqualEnergy) {
  const size_t kLength = 128;
  AudioBuffer buffer(2, kLength);
  GenerateSineWave(440.0f, 48000, &buffer[0]);
  GenerateSineWave(440.0f, 48000, &buffer[1]);

  double ild = GetBroadbandILD(buffer[0], buffer[1]);
  EXPECT_NEAR(ild, 0.0, kEpsilonFloat);
}

TEST(TestUtilTest, GetBroadbandILD_ReturnsPositiveWhenLeftLouder) {
  const size_t kLength = 128;
  AudioBuffer left(1, kLength);
  AudioBuffer right(1, kLength);
  GenerateSineWave(440.0f, 48000, &left[0]);
  GenerateSineWave(440.0f, 48000, &right[0]);

  // Scale right channel to be quieter.
  for (size_t i = 0; i < kLength; ++i) {
    right[0][i] *= 0.5f;
  }

  double ild = GetBroadbandILD(left[0], right[0]);
  EXPECT_GT(ild, 0.0);
  // Left is 2x amplitude, so 4x power, so ILD should be ~6 dB.
  EXPECT_NEAR(ild, 6.02, 0.1);
}

TEST(TestUtilTest, GetBroadbandILD_ReturnsNegativeWhenRightLouder) {
  const size_t kLength = 128;
  AudioBuffer left(1, kLength);
  AudioBuffer right(1, kLength);
  GenerateSineWave(440.0f, 48000, &left[0]);
  GenerateSineWave(440.0f, 48000, &right[0]);

  // Scale left channel to be quieter.
  for (size_t i = 0; i < kLength; ++i) {
    left[0][i] *= 0.5f;
  }

  double ild = GetBroadbandILD(left[0], right[0]);
  EXPECT_LT(ild, 0.0);
  // Right is 2x amplitude, so 4x power, so ILD should be ~-6 dB.
  EXPECT_NEAR(ild, -6.02, 0.1);
}

}  // namespace

}  // namespace obr
