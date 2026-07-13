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

}  // namespace
}  // namespace obr
