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
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

#include "benchmark/benchmark.h"
#include "obr/ambisonic_encoder/ambisonic_encoder.h"
#include "obr/audio_buffer/audio_buffer.h"
#include "obr/common/ambisonic_utils.h"

namespace obr {
namespace {
// TODO(b/373302873): Remove randomness.

// Measure execution time of coefficient calculation.
void BM_SHCalculation(benchmark::State& state) {
  const size_t number_of_input_channels = 128;
  const size_t ambisonic_order = 3;

  // Create an array of azimuth/elevation pairs with random directions.
  std::vector<std::pair<float, float>> directions;
  for (int i = 0; i < number_of_input_channels; i++) {
    float azimuth = static_cast<float>(rand() / RAND_MAX) * 360.0f;
    float elevation = static_cast<float>(rand() / RAND_MAX) * 180.0f - 90.0f;
    directions.emplace_back(azimuth, elevation);
  }

  // Create an Ambisonic encoder object.
  AmbisonicEncoder encoder(number_of_input_channels, ambisonic_order);

  for (auto _ : state) {
    (void)_;  // Suppress unused variable warning.
    // Assign sources to the encoder at all available input channels.
    for (size_t i = 0; i < number_of_input_channels; i++) {
      encoder.SetSource(i, 1.0f, directions[i].first, directions[i].second,
                        1.0f);
    }
  }
}

BENCHMARK(BM_SHCalculation);

// Measure matrix multiplication time at different numbers of input channels for
// static and dynamic sources.
void BM_MatrixMultiplication(benchmark::State& state) {
  const size_t buffer_size_per_channel = 256;
  const size_t number_of_input_channels = state.range(0);
  const size_t ambisonic_order = 3;
  const bool dynamic = (state.range(1) != 0);

  // Create input buffer
  AudioBuffer input_buffer(number_of_input_channels, buffer_size_per_channel);
  input_buffer.Clear();

  // Fill input buffer with random data.
  for (auto ch = 0; ch < input_buffer.num_channels(); ch++) {
    AudioBuffer::Channel& channel = input_buffer[ch];
    for (float& sample : channel) {
      sample = static_cast<float>(rand() / RAND_MAX) - 0.5f;
    }
  }

  // Create output buffer.
  AudioBuffer output_buffer(GetNumPeriphonicComponents(ambisonic_order),
                            buffer_size_per_channel);

  // Create an Ambisonic encoder object.
  AmbisonicEncoder encoder(number_of_input_channels, ambisonic_order);

  // Create an array of azimuth/elevation pairs with random directions.
  std::vector<std::pair<float, float>> directions;
  while (directions.size() < number_of_input_channels) {
    float azimuth = static_cast<float>(rand() / RAND_MAX) * 360.0f;
    float elevation = static_cast<float>(rand() / RAND_MAX) * 180.0f - 90.0f;
    directions.emplace_back(azimuth, elevation);
  }

  // Assign sources to the encoder at all available input channels.
  for (size_t i = 0; i < number_of_input_channels; i++) {
    encoder.SetSource(i, 1.0f, directions[i].first, directions[i].second, 1.0f);
  }

  size_t iter = 0;
  for (auto _ : state) {
    (void)_;  // Suppress unused variable warning.
    if (dynamic) {
      // Change the target azimuth slightly each iteration so sources are
      // dynamic and interpolation work is exercised.
      const float delta = (iter & 1) ? 1.0f : -1.0f;  // degrees
      for (size_t i = 0; i < number_of_input_channels; ++i) {
        float az = directions[i].first + delta;
        encoder.SetSource(i, 1.0f, az, directions[i].second, 1.0f);
      }
    }

    // Perform matrix multiplication / encoding.
    encoder.ProcessPlanarAudioData(input_buffer, &output_buffer);
    ++iter;
  }
}

BENCHMARK(BM_MatrixMultiplication)
    ->Args({16, 0})  // 16 inputs, static
    ->Args({16, 1})  // 16 inputs, dynamic
    ->Args({32, 0})
    ->Args({32, 1})
    ->Args({64, 0})
    ->Args({64, 1})
    ->Args({128, 0})
    ->Args({128, 1});

}  // namespace
}  // namespace obr
