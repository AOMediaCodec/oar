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

#include "obr/renderer/obr_impl.h"

#include <cstddef>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "obr/audio_buffer/audio_buffer.h"
#include "obr/common/test_util.h"
#include "obr/renderer/audio_element_config.h"
#include "obr/renderer/audio_element_type.h"

namespace obr {
namespace {

using ::absl_testing::IsOk;

constexpr int kBufferSizePerChannel = 44;
constexpr int kSamplingRate = 48000;

// Test initialization of the ObrImpl class.
TEST(ObrImplTest, TestInitialization) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_EQ(renderer.GetBufferSizePerChannel(), kBufferSizePerChannel);
  EXPECT_EQ(renderer.GetSamplingRate(), kSamplingRate);
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 0);
  EXPECT_EQ(renderer.GetNumberOfOutputChannels(), 2);
}

// Test adding and removing 1 audio element.
TEST(ObrImplTest, TestAddAndRemoveAudioElement) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA), IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 16);

  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 0);
}

// Test adding 3OA and 7.1.4 Audio Elements.
TEST(ObrImplTest, TestAddAndRemoveMultipleAudioElements) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA), IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 16);

  // Adding 7.1.4 layout should succeed - mixed audio element types are
  // supported.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayout7_1_4_ch),
              IsOk());
  // 3OA has 16 channels, 7.1.4 has 12 channels.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 28);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Remove the 7.1.4 element.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 16);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 1);

  // Remove the 3OA element.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 0);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 0);

  // Removing from empty should fail.
  EXPECT_FALSE(renderer.RemoveLastAudioElement().ok());
}

// Test rendering of Ambisonic scenes containing a Kronecker delta at different
// azimuths.
TEST(ObrImplTest, TestRenderAmbisonicsAndMeasureBroadbandILD) {
  const size_t kTestSignalLength = 1000;
  const size_t num_frames = kTestSignalLength;
  const double kILDAccuracyThresholdDb = 3;

  std::vector<AudioElementType> audio_elements = {
      AudioElementType::k1OA,
      AudioElementType::k2OA,
      AudioElementType::k3OA,
      AudioElementType::k4OA,
  };

  const std::pair<float, double> azimuth_ILDs[] = {
      {0.0f, 0.0}, {90.0f, 15.0}, {180.0f, 0.0}, {270.0f, -15.0}};

  for (const auto audio_element : audio_elements) {
    for (const auto& azimuthILD : azimuth_ILDs) {
      const int ambisonic_order = GetAmbisonicOrder(audio_element).value();
      const float azimuth = azimuthILD.first;
      const float elevation = 0.0f;
      const float distance = 1.0f;
      const double expected_ILD = azimuthILD.second;

      ObrImpl renderer(num_frames, 48000);
      EXPECT_THAT(renderer.AddAudioElement(audio_element), IsOk());

      AudioBuffer test_ambisonic_scene = GetKroneckerDeltaEncodedToAmbisonics(
          kTestSignalLength, azimuth, elevation, distance, ambisonic_order);
      AudioBuffer binaural_output_buffer(2, num_frames);

      renderer.Process(test_ambisonic_scene, &binaural_output_buffer);

      // Measure ILD.
      const double ILD =
          GetBroadbandILD(binaural_output_buffer[0], binaural_output_buffer[1]);
      EXPECT_NEAR(ILD, expected_ILD, kILDAccuracyThresholdDb);
    }
  }
}

// Handles gracefully when input AudioBuffer has different number of channels
// than the declared number of input channels (outputs silence during dynamic
// reconfiguration).
TEST(ObrImplTest, TestProcessAudioBufferWithWrongNumberOfChannels) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA), IsOk());

  // During dynamic reconfiguration, the input buffer may have a channel count
  // that doesn't match the current configuration. This should be handled
  // gracefully by outputting silence rather than crashing.
  AudioBuffer input_buffer(17, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);

  // Should not crash - outputs silence and logs a warning.
  renderer.Process(input_buffer, &output_buffer);

  // Verify output is silent (all zeros).
  EXPECT_TRUE(HasAllZeroOutput(output_buffer));
}

// Fails when input AudioBuffer has different number of frames than the declared
// buffer size.
TEST(ObrImplTest, TestProcessAudioBufferWithWrongBufferSize) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA), IsOk());

  AudioBuffer input_buffer(16, kBufferSizePerChannel + 1);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);

  EXPECT_DEATH(renderer.Process(input_buffer, &output_buffer), "");
}

// Fails when output AudioBuffer is not initialized.
TEST(ObrImplTest, TestProcessAudioBufferWithUninitializedOutputBuffer) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA), IsOk());

  AudioBuffer input_buffer(16, kBufferSizePerChannel);

  EXPECT_DEATH(renderer.Process(input_buffer, nullptr), "");
}

// Fails when output AudioBuffer has different number of channels than 2.
TEST(ObrImplTest, TestProcessAudioBufferWithWrongNumberOfOutputChannels) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA), IsOk());

  AudioBuffer input_buffer(16, kBufferSizePerChannel);
  AudioBuffer output_buffer(3, kBufferSizePerChannel);

  EXPECT_DEATH(renderer.Process(input_buffer, &output_buffer), "");
}

// Fails when output AudioBuffer has different number of frames than the
// declared buffer size.
TEST(ObrImplTest, TestProcessAudioBufferWithWrongOutputBufferSize) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA), IsOk());

  AudioBuffer input_buffer(16, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel + 1);

  EXPECT_DEATH(renderer.Process(input_buffer, &output_buffer), "");
}

// Test that adding multiple audio elements with different audio element types
// but with the same binaural filter type succeeds and they are grouped
// together.
TEST(ObrImplTest,
     TestAddMultipleAudioElementsWithDifferentTypesSameFilterTypeSucceeds) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  // Add first element with k3OA and Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  // Add second element with k1OA and Direct filter (should succeed and share
  // processing group for binaural decoder with same filter profile).
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k1OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  // 3OA has 16 channels, 1OA has 4 channels.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 20);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test that adding multiple audio elements of the same type with different
// binaural filter types succeeds and they are placed in separate processing
// groups.
TEST(ObrImplTest,
     TestAddMultipleAudioElementsSameTypeDifferentFilterTypesSucceeds) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  // Add first element with k3OA and Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  // Add second element with k3OA and Reverberant filter (should succeed and
  // create a separate processing group).
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA,
                                       BinauralFilterProfile::kReverberant),
              IsOk());
  // Both are 3OA with 16 channels each.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 32);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test adding Ambisonics + Loudspeaker layouts with same filter profile.
TEST(ObrImplTest, TestAmbisonicsAndLoudspeakerLayoutsSameFilterProfile) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add 2OA (9 channels) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k2OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 9);

  // Add 7.1.4 layout (12 channels) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayout7_1_4_ch,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  // 2OA has 9 channels, 7.1.4 has 12 channels.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 21);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(21, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test adding Ambisonics + Loudspeaker layouts with different filter profiles.
TEST(ObrImplTest, TestAmbisonicsAndLoudspeakerLayoutsDifferentFilterProfiles) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add 3OA (16 channels) with Ambient filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA,
                                       BinauralFilterProfile::kAmbient),
              IsOk());

  // Add 5.1.2 layout (8 channels) with Reverberant filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayout5_1_2_ch,
                                       BinauralFilterProfile::kReverberant),
              IsOk());

  // 3OA has 16 channels, 5.1.2 has 8 channels.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 24);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(24, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test adding Ambisonics + Objects with same filter profile.
TEST(ObrImplTest, TestAmbisonicsAndObjectsSameFilterProfile) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add 1OA (4 channels) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k1OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Add Object (1 channel) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // 1OA has 4 channels, Object has 1 channel.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 5);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Set object position.
  EXPECT_THAT(renderer.UpdateObjectPosition(1, 45.0f, 0.0f, 1.0f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(5, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test adding Ambisonics + Objects with different filter profiles.
TEST(ObrImplTest, TestAmbisonicsAndObjectsDifferentFilterProfiles) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add 2OA (9 channels) with Ambient filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k2OA,
                                       BinauralFilterProfile::kAmbient),
              IsOk());

  // Add Object (1 channel) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // 2OA has 9 channels, Object has 1 channel.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 10);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Set object position.
  EXPECT_THAT(renderer.UpdateObjectPosition(1, 90.0f, 30.0f, 0.5f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(10, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test adding Loudspeaker layouts + Objects with same filter profile.
TEST(ObrImplTest, TestLoudspeakerLayoutsAndObjectsSameFilterProfile) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add 5.1.0 layout (6 channels) with Reverberant filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayout5_1_0_ch,
                                       BinauralFilterProfile::kReverberant),
              IsOk());

  // Add Object (1 channel) with Reverberant filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kReverberant),
              IsOk());

  // 5.1.0 has 6 channels, Object has 1 channel.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 7);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Set object position.
  EXPECT_THAT(renderer.UpdateObjectPosition(1, 180.0f, -15.0f, 1.0f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(7, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test adding Loudspeaker layouts + Objects with different filter profiles.
TEST(ObrImplTest, TestLoudspeakerLayoutsAndObjectsDifferentFilterProfiles) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add Stereo layout (2 channels) with Ambient filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayoutStereo,
                                       BinauralFilterProfile::kAmbient),
              IsOk());

  // Add Object (1 channel) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Stereo has 2 channels, Object has 1 channel.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 3);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Set object position.
  EXPECT_THAT(renderer.UpdateObjectPosition(1, 270.0f, 0.0f, 0.8f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(3, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test adding all three types together (Ambisonics + Loudspeaker + Objects)
// with same filter profile.
TEST(ObrImplTest, TestAllThreeTypesSameFilterProfile) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add 1OA (4 channels) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k1OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Add Stereo layout (2 channels) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayoutStereo,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Add Object (1 channel) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // 1OA has 4 channels, Stereo has 2 channels, Object has 1 channel.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 7);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 3);

  // Set object position.
  EXPECT_THAT(renderer.UpdateObjectPosition(2, 0.0f, 45.0f, 1.0f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(7, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test adding all three types together with different filter profiles.
TEST(ObrImplTest, TestAllThreeTypesDifferentFilterProfiles) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add 3OA (16 channels) with Ambient filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA,
                                       BinauralFilterProfile::kAmbient),
              IsOk());

  // Add 7.1.4 layout (12 channels) with Reverberant filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayout7_1_4_ch,
                                       BinauralFilterProfile::kReverberant),
              IsOk());

  // Add Object (1 channel) with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // 3OA has 16 channels, 7.1.4 has 12 channels, Object has 1 channel.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 29);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 3);

  // Set object position.
  EXPECT_THAT(renderer.UpdateObjectPosition(2, 135.0f, -30.0f, 0.7f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(29, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test multiple processing groups with different filter profiles (comprehensive
// test with multiple elements per profile).
TEST(ObrImplTest, TestMultipleProcessingGroupsWithDifferentFilterProfiles) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Direct filter profile group.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k2OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayoutStereo,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Ambient filter profile group.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k1OA,
                                       BinauralFilterProfile::kAmbient),
              IsOk());
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayout5_1_2_ch,
                                       BinauralFilterProfile::kAmbient),
              IsOk());

  // Reverberant filter profile group.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA,
                                       BinauralFilterProfile::kReverberant),
              IsOk());
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kReverberant),
              IsOk());

  // Total channels: 2OA(9) + Stereo(2) + Object(1) + 1OA(4) + 5.1.2(8) +
  // 3OA(16) + Object(1) = 41.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 41);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 7);

  // Set object positions.
  EXPECT_THAT(renderer.UpdateObjectPosition(2, 30.0f, 0.0f, 1.0f), IsOk());
  EXPECT_THAT(renderer.UpdateObjectPosition(6, 210.0f, 20.0f, 0.9f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(41, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  for (int i = 0; i < 7; ++i) {
    EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  }
}

// Test multiple processing groups with varying Ambisonic orders and filter
// profiles.
TEST(ObrImplTest, TestMultipleProcessingGroupsVaryingOrdersAndProfiles) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add elements that will create multiple distinct processing groups.
  // Group 1: 1OA + Direct.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k1OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Group 2: 1OA + Ambient.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k1OA,
                                       BinauralFilterProfile::kAmbient),
              IsOk());

  // Group 3: 2OA + Direct.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k2OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Group 4: 3OA + Reverberant.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k3OA,
                                       BinauralFilterProfile::kReverberant),
              IsOk());

  // Add an Object to Group 2 (1OA + Ambient).
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kAmbient),
              IsOk());

  // Total channels: 1OA(4) + 1OA(4) + 2OA(9) + 3OA(16) + Object(1) = 34.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 34);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 5);

  // Set object position.
  EXPECT_THAT(renderer.UpdateObjectPosition(4, 60.0f, -10.0f, 0.6f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(34, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  for (int i = 0; i < 5; ++i) {
    EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  }
}

// Test complex scenario with multiple objects at different positions.
TEST(ObrImplTest, TestMultipleObjectsWithDifferentPositions) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add Ambisonics.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k2OA,
                                       BinauralFilterProfile::kAmbient),
              IsOk());

  // Add multiple objects with Direct filter.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // 2OA has 9 channels, 3 Objects have 1 channel each.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 12);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 4);

  // Set positions for all objects at different locations.
  EXPECT_THAT(renderer.UpdateObjectPosition(1, 0.0f, 0.0f, 1.0f), IsOk());
  EXPECT_THAT(renderer.UpdateObjectPosition(2, 120.0f, 30.0f, 0.8f), IsOk());
  EXPECT_THAT(renderer.UpdateObjectPosition(3, 240.0f, -30.0f, 0.5f), IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(12, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  for (int i = 0; i < 4; ++i) {
    EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  }
}

// Test that limiter is enabled by default and limits peak output.
TEST(ObrImplTest, TestLimiterEnabledByDefault) {
  const size_t kNumFrames = 512;
  const int kSamplingRate = 48000;
  const float kHighAmplitude = 3.0f;  // Well above limiter ceiling (-0.5dB).

  ObrImpl renderer(kNumFrames, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayoutStereo),
              IsOk());

  // Create a high amplitude noise burst input.
  AudioBuffer input_buffer(2, kNumFrames);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[0], 42);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[1], 43);

  AudioBuffer output_buffer(2, kNumFrames);
  renderer.Process(input_buffer, &output_buffer);

  // With limiter enabled (default), peak should be limited to around -0.5dB
  // (~0.944).
  const float peak_left = GetPeakAmplitude(output_buffer[0]);
  const float peak_right = GetPeakAmplitude(output_buffer[1]);

  // The limiter ceiling is -0.5dB which is approximately 0.944 in linear.
  // Allow some margin for the limiter's release characteristics.
  EXPECT_LT(peak_left, 1.1f);
  EXPECT_LT(peak_right, 1.1f);
}

// Test that disabling the limiter allows peaks above the ceiling.
TEST(ObrImplTest, TestLimiterDisabledAllowsHighPeaks) {
  const size_t kNumFrames = 512;
  const int kSamplingRate = 48000;
  const float kHighAmplitude = 3.0f;  // Well above limiter ceiling.

  ObrImpl renderer(kNumFrames, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayoutStereo),
              IsOk());

  // Disable the limiter.
  renderer.EnableLimiter(false);

  // Create a high amplitude noise burst input.
  AudioBuffer input_buffer(2, kNumFrames);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[0], 42);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[1], 43);

  AudioBuffer output_buffer(2, kNumFrames);
  renderer.Process(input_buffer, &output_buffer);

  // With limiter disabled, output should have peaks above 1.0.
  const float peak_left = GetPeakAmplitude(output_buffer[0]);
  const float peak_right = GetPeakAmplitude(output_buffer[1]);

  // Without limiting, peaks should exceed 1.0 (the input is high amplitude).
  EXPECT_GT(peak_left, 1.0f);
  EXPECT_GT(peak_right, 1.0f);
}

// Test limiter effect on RMS and peak by comparing enabled vs disabled.
TEST(ObrImplTest, TestLimiterEffectOnRmsAndPeak) {
  const size_t kNumFrames = 512;
  const int kSamplingRate = 48000;
  const float kHighAmplitude = 3.0f;

  // Create renderer with limiter enabled.
  ObrImpl renderer_limited(kNumFrames, kSamplingRate);
  EXPECT_THAT(renderer_limited.AddAudioElement(AudioElementType::kLayoutStereo),
              IsOk());

  // Create renderer with limiter disabled.
  ObrImpl renderer_unlimited(kNumFrames, kSamplingRate);
  EXPECT_THAT(
      renderer_unlimited.AddAudioElement(AudioElementType::kLayoutStereo),
      IsOk());
  renderer_unlimited.EnableLimiter(false);

  // Create identical high amplitude noise burst inputs.
  AudioBuffer input_buffer(2, kNumFrames);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[0], 42);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[1], 43);

  AudioBuffer output_limited(2, kNumFrames);
  AudioBuffer output_unlimited(2, kNumFrames);

  renderer_limited.Process(input_buffer, &output_limited);
  renderer_unlimited.Process(input_buffer, &output_unlimited);

  // Get peak values.
  const float peak_limited_left = GetPeakAmplitude(output_limited[0]);
  const float peak_unlimited_left = GetPeakAmplitude(output_unlimited[0]);

  // Get RMS values.
  const double rms_limited_left = GetRmsAmplitude(output_limited[0]);
  const double rms_unlimited_left = GetRmsAmplitude(output_unlimited[0]);

  // With limiter: peak should be lower than without limiter.
  EXPECT_LT(peak_limited_left, peak_unlimited_left);

  // With limiter: RMS should also be lower due to gain reduction.
  EXPECT_LT(rms_limited_left, rms_unlimited_left);

  // Verify limiter is actually doing something significant.
  // Unlimited peak should be noticeably higher than limited peak.
  EXPECT_GT(peak_unlimited_left / peak_limited_left, 1.5f);
}

// Test re-enabling the limiter after disabling it.
TEST(ObrImplTest, TestLimiterReEnable) {
  const size_t kNumFrames = 512;
  const int kSamplingRate = 48000;
  const float kHighAmplitude = 3.0f;

  ObrImpl renderer(kNumFrames, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayoutStereo),
              IsOk());

  AudioBuffer input_buffer(2, kNumFrames);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[0], 42);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[1], 43);

  AudioBuffer output_buffer(2, kNumFrames);

  // First, disable limiter and process.
  renderer.EnableLimiter(false);
  renderer.Process(input_buffer, &output_buffer);
  const float peak_disabled = GetPeakAmplitude(output_buffer[0]);
  EXPECT_GT(peak_disabled, 1.0f);

  // Re-enable limiter and process again.
  renderer.EnableLimiter(true);
  renderer.Process(input_buffer, &output_buffer);
  const float peak_enabled = GetPeakAmplitude(output_buffer[0]);
  EXPECT_LT(peak_enabled, 1.1f);

  // Verify that re-enabling had an effect.
  EXPECT_LT(peak_enabled, peak_disabled);
}

// Test that dual object has 2 input channels.
TEST(ObrImplTest, TestDualObjectHasTwoChannels) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectDual), IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 2);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 1);
}

// Test that dual object can be rendered with 2-channel input.
TEST(ObrImplTest, TestDualObjectRenders) {
  const size_t kNumFrames = 512;
  const int kTestSamplingRate = 48000;

  ObrImpl renderer(kNumFrames, kTestSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectDual), IsOk());

  // Create 2-channel input.
  AudioBuffer input_buffer(2, kNumFrames);
  GenerateSineWave(440.0f, kTestSamplingRate, &input_buffer[0]);
  GenerateSineWave(880.0f, kTestSamplingRate, &input_buffer[1]);

  AudioBuffer output_buffer(2, kNumFrames);
  renderer.Process(input_buffer, &output_buffer);

  // Verify output is non-zero (processing occurred).
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test UpdateObjectPosition sets both channels to the same position.
TEST(ObrImplTest, TestDualObjectUpdateObjectPosition) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectDual), IsOk());

  // UpdateObjectPosition should succeed for dual objects.
  EXPECT_THAT(renderer.UpdateObjectPosition(0, 45.0f, 0.0f, 1.0f), IsOk());
}

// Test UpdateObjectChannelPosition sets individual channel positions.
TEST(ObrImplTest, TestDualObjectUpdateObjectChannelPosition) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectDual), IsOk());

  // Set channel 0 to the left.
  EXPECT_THAT(renderer.UpdateObjectChannelPosition(0, 0, -90.0f, 0.0f, 1.0f),
              IsOk());

  // Set channel 1 to the right.
  EXPECT_THAT(renderer.UpdateObjectChannelPosition(0, 1, 90.0f, 0.0f, 1.0f),
              IsOk());
}

// Test UpdateObjectChannelPosition fails with invalid channel index.
TEST(ObrImplTest, TestDualObjectUpdateObjectChannelPositionInvalidChannel) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectDual), IsOk());

  // Channel index 2 is invalid for a dual object.
  EXPECT_FALSE(
      renderer.UpdateObjectChannelPosition(0, 2, 0.0f, 0.0f, 1.0f).ok());
}

// Test UpdateObjectChannelPosition fails for mono object with channel index 1.
TEST(ObrImplTest, TestMonoObjectUpdateObjectChannelPositionInvalidChannel) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectMono), IsOk());

  // Channel index 0 is valid for mono object.
  EXPECT_THAT(renderer.UpdateObjectChannelPosition(0, 0, 0.0f, 0.0f, 1.0f),
              IsOk());

  // Channel index 1 is invalid for mono object.
  EXPECT_FALSE(
      renderer.UpdateObjectChannelPosition(0, 1, 0.0f, 0.0f, 1.0f).ok());
}

// Test dual object with both channels at (0,0) position produces roughly 0 ILD.
TEST(ObrImplTest, TestDualObjectBothChannelsSamePositionProducesEqualOutput) {
  const size_t kNumFrames = 1000;
  const int kTestSamplingRate = 48000;
  const double kILDThresholdDb = 1.0;  // Small threshold for center position.

  ObrImpl renderer(kNumFrames, kTestSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectDual,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Set both channels to center (0 degrees azimuth).
  EXPECT_THAT(renderer.UpdateObjectPosition(0, 0.0f, 0.0f, 1.0f), IsOk());

  // Create input with equal signal in both channels.
  AudioBuffer input_buffer(2, kNumFrames);
  GenerateDiracImpulseFilter(0, &input_buffer[0]);
  GenerateDiracImpulseFilter(0, &input_buffer[1]);

  AudioBuffer output_buffer(2, kNumFrames);
  renderer.Process(input_buffer, &output_buffer);

  // With both sources at center, ILD should be near zero.
  const double ILD = GetBroadbandILD(output_buffer[0], output_buffer[1]);
  EXPECT_NEAR(ILD, 0.0, kILDThresholdDb);
}

// Test combining dual object with other audio element types.
TEST(ObrImplTest, TestDualObjectWithOtherAudioElements) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add 1OA (4 channels).
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k1OA,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Add dual object (2 channels).
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kObjectDual,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // Add stereo layout (2 channels).
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayoutStereo,
                                       BinauralFilterProfile::kDirect),
              IsOk());

  // 1OA has 4 channels, dual object has 2 channels, stereo has 2 channels.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 8);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 3);

  // Set dual object channel positions (audio element index 1).
  EXPECT_THAT(renderer.UpdateObjectChannelPosition(1, 0, 45.0f, 15.0f, 1.0f),
              IsOk());
  EXPECT_THAT(renderer.UpdateObjectChannelPosition(1, 1, -45.0f, -15.0f, 1.0f),
              IsOk());

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(8, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);

  // Clean up.
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
  EXPECT_THAT(renderer.RemoveLastAudioElement(), IsOk());
}

// Test that kPassthroughMono has 1 input channel.
TEST(ObrImplTest, TestPassthroughMonoHasOneChannel) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kPassthroughMono),
              IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 1);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 1);
}

// Test that kPassthroughStereo has 2 input channels.
TEST(ObrImplTest, TestPassthroughBinauralHasTwoChannels) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kPassthroughStereo),
              IsOk());
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 2);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 1);
}

// Test that kPassthroughMono copies input to both L/R outputs.
TEST(ObrImplTest, TestPassthroughMonoCopiesInputToBothOutputs) {
  const size_t kNumFrames = 512;
  const int kTestSamplingRate = 48000;
  const float kInputAmplitude = 0.5f;

  ObrImpl renderer(kNumFrames, kTestSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kPassthroughMono),
              IsOk());

  // Create 1-channel input with a sine wave.
  AudioBuffer input_buffer(1, kNumFrames);
  GenerateSineWave(440.0f, kTestSamplingRate, &input_buffer[0]);

  // Scale the input.
  for (size_t i = 0; i < kNumFrames; ++i) {
    input_buffer[0][i] *= kInputAmplitude;
  }

  AudioBuffer output_buffer(2, kNumFrames);
  renderer.Process(input_buffer, &output_buffer);

  // Verify output is non-zero (passthrough occurred).
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));

  // Verify L and R are approximately equal (both receive the same input).
  // Also verify output matches input (amplitude is below limiter ceiling).
  for (size_t i = 0; i < kNumFrames; ++i) {
    EXPECT_NEAR(output_buffer[0][i], output_buffer[1][i], 0.0001f);
    EXPECT_NEAR(output_buffer[0][i], input_buffer[0][i], 0.0001f);
  }
}

// Test that kPassthroughStereo copies L/R input directly to L/R output.
TEST(ObrImplTest, TestPassthroughBinauralCopiesInputDirectly) {
  const size_t kNumFrames = 512;
  const int kTestSamplingRate = 48000;
  const float kInputAmplitude = 0.5f;

  ObrImpl renderer(kNumFrames, kTestSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kPassthroughStereo),
              IsOk());

  // Create 2-channel input with different sine waves on L and R.
  AudioBuffer input_buffer(2, kNumFrames);
  GenerateSineWave(440.0f, kTestSamplingRate, &input_buffer[0]);
  GenerateSineWave(880.0f, kTestSamplingRate, &input_buffer[1]);

  // Scale the input.
  for (size_t i = 0; i < kNumFrames; ++i) {
    input_buffer[0][i] *= kInputAmplitude;
    input_buffer[1][i] *= kInputAmplitude;
  }

  AudioBuffer output_buffer(2, kNumFrames);
  renderer.Process(input_buffer, &output_buffer);

  // Verify output is non-zero (passthrough occurred).
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));

  // As the input amplitude is below the limiter ceiling, output should match
  // input.
  for (size_t i = 0; i < kNumFrames; ++i) {
    EXPECT_NEAR(output_buffer[0][i], input_buffer[0][i], 0.0001f);
    EXPECT_NEAR(output_buffer[1][i], input_buffer[1][i], 0.0001f);
  }
}

// Test that passthrough elements can be combined with binaural processing
// elements.
TEST(ObrImplTest, TestPassthroughWithBinauralProcessing) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add passthrough element.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kPassthroughMono),
              IsOk());

  // Add binaural processing element.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::k1OA), IsOk());

  // kPassthroughMono has 1 channel, 1OA has 4 channels.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 5);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 2);

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(5, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);
}

// Test that kPassthroughStereo element can be combined with other passthrough
// and binaural elements.
TEST(ObrImplTest, TestBinauralPassthroughWithOtherElements) {
  ObrImpl renderer(kBufferSizePerChannel, kSamplingRate);

  // Add kPassthroughStereo (2 channels).
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kPassthroughStereo),
              IsOk());

  // Add kPassthroughMono (1 channel).
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kPassthroughMono),
              IsOk());

  // Add stereo layout (2 channels) for binaural processing.
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kLayoutStereo),
              IsOk());

  // kPassthroughStereo(2) + kPassthroughMono(1) + Stereo(2) = 5 channels.
  EXPECT_EQ(renderer.GetNumberOfInputChannels(), 5);
  EXPECT_EQ(renderer.GetNumberOfAudioElements(), 3);

  // Process to ensure no crashes occur.
  AudioBuffer input_buffer(5, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);
  renderer.Process(input_buffer, &output_buffer);
}

// Test that passthrough-only configuration still applies peak limiter.
TEST(ObrImplTest, TestPassthroughOnlyAppliesPeakLimiter) {
  const size_t kNumFrames = 512;
  const int kTestSamplingRate = 48000;
  const float kHighAmplitude = 3.0f;  // Well above limiter ceiling.

  ObrImpl renderer(kNumFrames, kTestSamplingRate);
  EXPECT_THAT(renderer.AddAudioElement(AudioElementType::kPassthroughStereo),
              IsOk());

  // Create high amplitude noise input.
  AudioBuffer input_buffer(2, kNumFrames);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[0], 42);
  GenerateWhiteNoise(kHighAmplitude, &input_buffer[1], 43);

  AudioBuffer output_buffer(2, kNumFrames);
  renderer.Process(input_buffer, &output_buffer);

  // With limiter enabled (default), peak should be limited.
  const float peak_left = GetPeakAmplitude(output_buffer[0]);
  const float peak_right = GetPeakAmplitude(output_buffer[1]);

  // The limiter ceiling is -0.5dB (~0.944). Allow some margin.
  EXPECT_LT(peak_left, 1.1f);
  EXPECT_LT(peak_right, 1.1f);
}

}  // namespace
}  // namespace obr
