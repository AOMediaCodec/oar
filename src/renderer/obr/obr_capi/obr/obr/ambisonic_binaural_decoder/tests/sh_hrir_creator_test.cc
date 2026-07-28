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

#include "obr/ambisonic_binaural_decoder/sh_hrir_creator.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <memory>
#include <string>

#include "gtest/gtest.h"
#include "obr/ambisonic_binaural_decoder/resampler.h"
#include "obr/audio_buffer/audio_buffer.h"

namespace obr {

namespace {

// The bundled HRIR assets are recorded at this rate; loading at it performs
// no resampling, which makes it the reference for the invariance tests.
const int kHrirNativeRateHz = 48000;

// Magnitude of the filter's frequency response (DTFT) at `frequency_hz`.
double FilterMagnitudeAt(const AudioBuffer& filter, size_t channel,
                         double frequency_hz, int sample_rate_hz) {
  std::complex<double> response(0.0, 0.0);
  for (size_t n = 0; n < filter.num_frames(); ++n) {
    const double phase = -2.0 * M_PI * frequency_hz * static_cast<double>(n) /
                         static_cast<double>(sample_rate_hz);
    response += static_cast<double>(filter[channel][n]) *
                std::complex<double>(std::cos(phase), std::sin(phase));
  }
  return std::abs(response);
}

// L2 norm of the frequency response magnitudes across all channels. Per-
// channel magnitudes can be cancellation-dominated (and thus noise-limited)
// at frequencies where that channel has a null; the cross-channel norm is
// always well-conditioned and still scales linearly with any gain error.
double FilterMagnitudeNormAt(const AudioBuffer& filter, double frequency_hz,
                             int sample_rate_hz) {
  double sum_squares = 0.0;
  for (size_t channel = 0; channel < filter.num_channels(); ++channel) {
    const double magnitude =
        FilterMagnitudeAt(filter, channel, frequency_hz, sample_rate_hz);
    sum_squares += magnitude * magnitude;
  }
  return std::sqrt(sum_squares);
}

std::unique_ptr<AudioBuffer> LoadHrirs(const std::string& asset_name,
                                       int target_sample_rate_hz) {
  Resampler resampler;
  return CreateShHrirsFromAssets(asset_name, target_sample_rate_hz,
                                 &resampler);
}

// Regression test for rate-dependent binaural loudness: resampling an impulse
// response with a (waveform-preserving) signal resampler scales the gain of
// the filter it realizes by target_rate/native_rate unless compensated -
// +6 dB at 96 kHz with the 48 kHz-native assets. The realized frequency
// response must be invariant to the target rate at every physical frequency.
TEST(ShHrirCreatorTest, FilterResponseIsInvariantToTargetRate) {
  const auto reference = LoadHrirs("3OAAmbientL", kHrirNativeRateHz);

  const int kTargetRates[] = {44100, 88200, 96000};
  const double kProbeFrequenciesHz[] = {500.0, 1000.0, 2000.0, 4000.0, 8000.0};

  for (const int target_rate : kTargetRates) {
    const auto resampled = LoadHrirs("3OAAmbientL", target_rate);
    ASSERT_EQ(reference->num_channels(), resampled->num_channels());
    for (const double frequency_hz : kProbeFrequenciesHz) {
      const double reference_norm =
          FilterMagnitudeNormAt(*reference, frequency_hz, kHrirNativeRateHz);
      const double resampled_norm =
          FilterMagnitudeNormAt(*resampled, frequency_hz, target_rate);
      ASSERT_GT(reference_norm, 0.0);
      // 0.02 is roughly 0.17 dB, far below the uncompensated errors
      // (+6 dB at 96k) while leaving room for resampling filter ripple.
      EXPECT_NEAR(1.0, resampled_norm / reference_norm, 0.02)
          << frequency_hz << " Hz, target " << target_rate << " Hz";
    }
  }
}

// The compensation must apply uniformly to every channel and every bundled
// asset variant (ambisonic order, filter profile, ear).
TEST(ShHrirCreatorTest, FilterResponseIsInvariantPerChannelAcrossAssets) {
  const std::string kAssets[] = {"1OADirectL", "1OAReverberantR", "2OAAmbientR",
                                 "3OADirectR"};
  const int kTargetRate = 96000;
  const double kProbeFrequencyHz = 1000.0;

  for (const std::string& asset : kAssets) {
    const auto reference = LoadHrirs(asset, kHrirNativeRateHz);
    const auto resampled = LoadHrirs(asset, kTargetRate);
    ASSERT_EQ(reference->num_channels(), resampled->num_channels());

    double max_magnitude = 0.0;
    for (size_t channel = 0; channel < reference->num_channels(); ++channel) {
      max_magnitude = std::max(
          max_magnitude, FilterMagnitudeAt(*reference, channel,
                                           kProbeFrequencyHz,
                                           kHrirNativeRateHz));
    }
    ASSERT_GT(max_magnitude, 0.0);

    for (size_t channel = 0; channel < reference->num_channels(); ++channel) {
      const double reference_magnitude = FilterMagnitudeAt(
          *reference, channel, kProbeFrequencyHz, kHrirNativeRateHz);
      // Channels with a null at the probe frequency are noise-limited:
      // their DTFT is a residue of cancelling terms, so the ratio is
      // meaningless. The cross-channel norm test above covers overall gain.
      if (reference_magnitude < 0.02 * max_magnitude) {
        continue;
      }
      const double resampled_magnitude =
          FilterMagnitudeAt(*resampled, channel, kProbeFrequencyHz,
                            kTargetRate);
      EXPECT_NEAR(1.0, resampled_magnitude / reference_magnitude, 0.03)
          << asset << ", channel " << channel;
    }
  }
}

// Loading at the assets' native rate must not resample (or rescale) at all.
TEST(ShHrirCreatorTest, NativeRateLoadIsUnmodified) {
  const auto first = LoadHrirs("3OAAmbientL", kHrirNativeRateHz);
  ASSERT_NE(first, nullptr);
  EXPECT_EQ(16U, first->num_channels());
  EXPECT_GT(first->num_frames(), 0U);

  // A second load must be bit-identical (no hidden resampler state involved).
  const auto second = LoadHrirs("3OAAmbientL", kHrirNativeRateHz);
  ASSERT_EQ(first->num_frames(), second->num_frames());
  for (size_t channel = 0; channel < first->num_channels(); ++channel) {
    for (size_t n = 0; n < first->num_frames(); ++n) {
      ASSERT_EQ((*first)[channel][n], (*second)[channel][n]);
    }
  }
}

// The resampled impulse responses must cover the same time span: the tap
// count scales with the rate ratio.
TEST(ShHrirCreatorTest, ResampledLengthMatchesRateRatio) {
  const auto reference = LoadHrirs("3OAAmbientL", kHrirNativeRateHz);
  const int kTargetRates[] = {44100, 88200, 96000};
  for (const int target_rate : kTargetRates) {
    const auto resampled = LoadHrirs("3OAAmbientL", target_rate);
    const double expected_frames = static_cast<double>(target_rate) /
                                   static_cast<double>(kHrirNativeRateHz) *
                                   static_cast<double>(reference->num_frames());
    EXPECT_NEAR(expected_frames, static_cast<double>(resampled->num_frames()),
                1.0)
        << "target " << target_rate << " Hz";
  }
}

}  // namespace

}  // namespace obr
