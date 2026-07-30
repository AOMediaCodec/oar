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

#include "obr/ambisonic_encoder/ambisonic_encoder.h"

#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "gtest/gtest.h"
#include "obr/audio_buffer/audio_buffer.h"
#include "obr/common/ambisonic_utils.h"

namespace obr {
namespace {

// Test the Ambisonic encoder class for a number of single sources, 3OA output.
TEST(AmbisonicEncoderTest, TestOneSampleBufferOneSource) {
  const size_t buffer_size = 1;
  const int number_of_input_channels = 1;
  const int ambisonic_order = 3;

  const absl::flat_hash_map<std::pair<float, float>, std::vector<float>>
      expected_output = {
          // clang-format off
{{ 0.000000000000f,  0.000000000000f},
 { 1.000000000000f,  0.000000000000f,  0.000000000000f,  1.000000000000f,
   0.000000000000f,  0.000000000000f, -0.500000000000f,  0.000000000000f,
   0.866025403784f,  0.000000000000f,  0.000000000000f,  0.000000000000f,
   0.000000000000f, -0.612372435696f,  0.000000000000f,  0.790569415042f}},
{{-45.00000000000f,  30.00000000000f},
 { 1.000000000000f, -0.612372435696f,  0.500000000000f,  0.612372435696f,
  -0.649519052838f, -0.530330085890f, -0.125000000000f,  0.530330085890f,
   0.000000000000f, -0.363092188707f, -0.726184377414f, -0.093750000000f,
  -0.437500000000f,  0.093750000000f,  0.000000000000f, -0.363092188707f}},
{{12.000000000000f,  0.000000000000f},
 { 1.000000000000f,  0.207911690818f,  0.000000000000f,  0.978147600734f,
   0.352244265554f,  0.000000000000f, -0.500000000000f,  0.000000000000f,
   0.791153573830f,  0.464685043075f,  0.000000000000f, -0.127319388516f,
   0.000000000000f, -0.598990628731f,  0.000000000000f,  0.639584092002f}},
{{120.00000000000f, -90.00000000000f},
 { 1.000000000000f,  0.000000000000f, -1.000000000000f,  0.000000000000f,
   0.000000000000f,  0.000000000000f,  1.000000000000f,  0.000000000000f,
   0.000000000000f,  0.000000000000f,  0.000000000000f,  0.000000000000f,
  -1.000000000000f,  0.000000000000f,  0.000000000000f,  0.000000000000f}},
          // clang-format on
      };

  // Evaluation precision.
  const float kEpsilon = 1e-7;

  // Run the test for AudioBuffer-based callback.
  for (const auto& pair : expected_output) {
    const auto tested_direction = pair.first;
    const auto expected_coefficients = pair.second;

    // Create an Ambisonic encoder object.
    AmbisonicEncoder encoder(number_of_input_channels, ambisonic_order);

    // Add a source with a given direction.
    encoder.SetSource(0, 1.0f, tested_direction.first, tested_direction.second,
                      1.0f);

    // Create input buffer with 1 channel.
    AudioBuffer input_buffer(number_of_input_channels, buffer_size);

    // Fill input buffer with ones.
    for (auto ch = 0; ch < input_buffer.num_channels(); ch++) {
      AudioBuffer::Channel& channel = input_buffer[ch];
      for (float& sample : channel) {
        sample = 1.0f;
      }
    }

    // Create output buffer with 16 channels.
    AudioBuffer output_buffer(GetNumPeriphonicComponents(ambisonic_order),
                              buffer_size);

    encoder.ProcessPlanarAudioData(input_buffer, &output_buffer);

    // Check if the output buffer matches the expected output buffer.
    for (auto ch = 0; ch < output_buffer.num_channels(); ch++) {
      AudioBuffer::Channel& channel = output_buffer[ch];
      for (float i : channel) {
        EXPECT_NEAR(i, expected_coefficients[ch], kEpsilon);
      }
    }
  }
}

// A source position set after the source was created but before any audio
// has been processed must take effect from the very first frame, with no
// ramp from the previously set (e.g. default) position.
TEST(AmbisonicEncoderTest, PreRenderPositionUpdateTakesEffectImmediately) {
  const size_t buffer_size = 64;
  const int number_of_input_channels = 1;
  const int ambisonic_order = 1;

  const float kEpsilon = 1e-6;

  AmbisonicEncoder encoder(number_of_input_channels, ambisonic_order);

  // Source is first registered at the default front position, then moved to
  // hard left before any processing (mirrors an element being added and its
  // metadata updated before the first render call).
  encoder.SetSource(0, 1.0f, 0.0f, 0.0f, 1.0f);
  encoder.SetSource(0, 1.0f, 90.0f, 0.0f, 1.0f);

  AudioBuffer input_buffer(number_of_input_channels, buffer_size);
  for (float& sample : input_buffer[0]) {
    sample = 1.0f;
  }

  AudioBuffer output_buffer(GetNumPeriphonicComponents(ambisonic_order),
                            buffer_size);
  encoder.ProcessPlanarAudioData(input_buffer, &output_buffer);

  // First-order ACN/SN3D coefficients for azimuth 90, elevation 0:
  // ACN0 = 1, ACN1 = 1, ACN2 = 0, ACN3 = 0. Every frame of the block,
  // including the first, must carry these coefficients.
  const std::vector<float> expected_coefficients = {1.0f, 1.0f, 0.0f, 0.0f};
  for (auto ch = 0; ch < output_buffer.num_channels(); ch++) {
    for (float sample : output_buffer[ch]) {
      EXPECT_NEAR(sample, expected_coefficients[ch], kEpsilon);
    }
  }
}

// Once audio has been processed, position updates must still ramp across the
// next block to avoid clicks: the block starts at the previously audible
// position and ends at the new one.
TEST(AmbisonicEncoderTest, PostRenderPositionUpdateRampsAcrossBlock) {
  const size_t buffer_size = 64;
  const int number_of_input_channels = 1;
  const int ambisonic_order = 1;

  const float kEpsilon = 1e-6;

  AmbisonicEncoder encoder(number_of_input_channels, ambisonic_order);
  encoder.SetSource(0, 1.0f, 0.0f, 0.0f, 1.0f);

  AudioBuffer input_buffer(number_of_input_channels, buffer_size);
  for (float& sample : input_buffer[0]) {
    sample = 1.0f;
  }

  AudioBuffer output_buffer(GetNumPeriphonicComponents(ambisonic_order),
                            buffer_size);

  // First block renders the source at the front.
  encoder.ProcessPlanarAudioData(input_buffer, &output_buffer);

  // Move the source to hard left and render another block.
  encoder.SetSource(0, 1.0f, 90.0f, 0.0f, 1.0f);
  encoder.ProcessPlanarAudioData(input_buffer, &output_buffer);

  // First-order ACN/SN3D coefficients (ACN0..ACN3): front is {1, 0, 0, 1},
  // hard left is {1, 1, 0, 0}. The block must start at the front and end
  // hard left.
  const std::vector<float> front_coefficients = {1.0f, 0.0f, 0.0f, 1.0f};
  const std::vector<float> left_coefficients = {1.0f, 1.0f, 0.0f, 0.0f};
  for (auto ch = 0; ch < output_buffer.num_channels(); ch++) {
    EXPECT_NEAR(output_buffer[ch][0], front_coefficients[ch], kEpsilon);
    EXPECT_NEAR(output_buffer[ch][buffer_size - 1], left_coefficients[ch],
                kEpsilon);
  }
}

}  // namespace
}  // namespace obr
