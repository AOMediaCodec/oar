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

#include "obr/renderer/processing_group.h"

#include <cstddef>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "obr/ambisonic_binaural_decoder/fft_manager.h"
#include "obr/ambisonic_binaural_decoder/resampler.h"
#include "obr/audio_buffer/audio_buffer.h"
#include "obr/common/misc_math.h"
#include "obr/common/test_util.h"
#include "obr/renderer/audio_element_config.h"
#include "obr/renderer/audio_element_type.h"

namespace obr {
namespace {

using ::absl_testing::IsOk;

constexpr int kBufferSizePerChannel = 128;
constexpr int kSamplingRate = 48000;

class ProcessingGroupTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fft_manager_ = std::make_unique<FftManager>(kBufferSizePerChannel);
    resampler_ = std::make_unique<Resampler>();
  }

  std::unique_ptr<FftManager> fft_manager_;
  std::unique_ptr<Resampler> resampler_;
};

// Test ProcessingGroupKey equality operator.
TEST(ProcessingGroupKeyTest, TestEquality) {
  ProcessingGroupKey key1{1, BinauralFilterProfile::kDirect};
  ProcessingGroupKey key2{1, BinauralFilterProfile::kDirect};
  ProcessingGroupKey key3{2, BinauralFilterProfile::kDirect};
  ProcessingGroupKey key4{1, BinauralFilterProfile::kAmbient};

  EXPECT_TRUE(key1 == key2);
  EXPECT_FALSE(key1 == key3);
  EXPECT_FALSE(key1 == key4);
}

// Test ProcessingGroupKey less-than operator.
TEST(ProcessingGroupKeyTest, TestLessThan) {
  ProcessingGroupKey key1{1, BinauralFilterProfile::kDirect};
  ProcessingGroupKey key2{2, BinauralFilterProfile::kDirect};
  ProcessingGroupKey key3{1, BinauralFilterProfile::kAmbient};

  EXPECT_TRUE(key1 < key2);
  EXPECT_FALSE(key2 < key1);
  EXPECT_TRUE(key1 < key3);
}

// Test ProcessingGroup construction.
TEST_F(ProcessingGroupTest, TestConstruction) {
  const ProcessingGroupKey key{2, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0, 1};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);

  EXPECT_EQ(group.GetKey().ambisonic_order, 2);
  EXPECT_EQ(group.GetKey().filter_profile, BinauralFilterProfile::kDirect);
  EXPECT_EQ(group.GetAudioElementIndices().size(), 2);
  EXPECT_EQ(group.GetAmbisonicOrder(), 2);
  EXPECT_EQ(group.GetFilterProfile(), BinauralFilterProfile::kDirect);
}

// Test ProcessingGroup initialization with Direct filter profile.
TEST_F(ProcessingGroupTest, TestInitializeDirectFilter) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);

  EXPECT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());
}

// Test ProcessingGroup initialization with Ambient filter profile.
TEST_F(ProcessingGroupTest, TestInitializeAmbientFilter) {
  const ProcessingGroupKey key{2, BinauralFilterProfile::kAmbient};
  const std::vector<size_t> indices = {0, 1};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);

  EXPECT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());
}

// Test ProcessingGroup initialization with Reverberant filter profile.
TEST_F(ProcessingGroupTest, TestInitializeReverberantFilter) {
  const ProcessingGroupKey key{3, BinauralFilterProfile::kReverberant};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);

  EXPECT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());
}

// Test ProcessingGroup initialization with different Ambisonic orders.
TEST_F(ProcessingGroupTest, TestInitializeDifferentOrders) {
  const std::vector<int> orders = {1, 2, 3, 4};

  for (const int order : orders) {
    const ProcessingGroupKey key{order, BinauralFilterProfile::kDirect};
    const std::vector<size_t> indices = {0};

    ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);

    EXPECT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk())
        << "Failed to initialize group with order " << order;
  }
}

// Test ProcessingGroup::Process with Ambisonics-only content.
TEST_F(ProcessingGroupTest, TestProcessAmbisonicsOnly) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create audio elements vector with one 1OA element.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);

  // Create input and output buffers and fill input with test data.
  // 1OA has 4 channels
  AudioBuffer input_buffer(4, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);  // Binaural output

  // Fill input with test data.
  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  const WorldRotation rotation;  // Identity rotation
  group.Process(input_buffer, audio_elements, false, rotation, &output_buffer);

  // Verify output is non-zero (processing occurred).
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test ProcessingGroup::Process with loudspeaker layout requiring encoding.
TEST_F(ProcessingGroupTest, TestProcessLoudspeakerLayout) {
  const ProcessingGroupKey key{2, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create audio elements vector with stereo layout.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::kLayoutStereo,
                              BinauralFilterProfile::kDirect);

  // Update encoder.
  ASSERT_THAT(group.UpdateAmbisonicEncoder(audio_elements), IsOk());

  // Create input and output buffers and fill input with test data.
  // Stereo has 2 channels
  AudioBuffer input_buffer(2, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);  // Binaural output

  // Fill input with test data.
  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  const WorldRotation rotation;
  group.Process(input_buffer, audio_elements, false, rotation, &output_buffer);

  // Verify output is non-zero.
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test ProcessingGroup::Process with audio object.
TEST_F(ProcessingGroupTest, TestProcessAudioObject) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create audio elements vector with object.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::kObjectMono,
                              BinauralFilterProfile::kDirect);

  // Set object position.
  auto& object_channels = audio_elements[0].GetObjectChannels();
  for (auto& obj : object_channels) {
    obj.SetAzimuth(45.0f);
    obj.SetElevation(0.0f);
    obj.SetDistance(1.0f);
  }

  // Update encoder.
  ASSERT_THAT(group.UpdateAmbisonicEncoder(audio_elements), IsOk());

  // Create input and output buffers and fill input with test data.
  // Object has 1 channel
  AudioBuffer input_buffer(1, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);  // Binaural output

  // Fill input with test data.
  GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[0]);

  const WorldRotation rotation;
  group.Process(input_buffer, audio_elements, false, rotation, &output_buffer);

  // Verify output is non-zero.
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test ProcessingGroup::Process with head tracking enabled.
TEST_F(ProcessingGroupTest, TestProcessWithHeadTracking) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create audio elements vector with 1OA.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);

  // Create input and output buffers and fill input with test data.
  AudioBuffer input_buffer(4, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);

  // Fill input with test data.
  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  // Create a rotation (90 degrees around Y-axis).
  const WorldRotation rotation(0.707f, 0.0f, 0.707f, 0.0f);

  // Process with head tracking enabled.
  group.Process(input_buffer, audio_elements, true, rotation, &output_buffer);

  // Verify output is non-zero.
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test ProcessingGroup::Process with mixed content (Ambisonics + Object).
TEST_F(ProcessingGroupTest, TestProcessMixedContent) {
  const ProcessingGroupKey key{2, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0, 1};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create audio elements: 2OA + Object.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k2OA,
                              BinauralFilterProfile::kDirect);
  audio_elements.emplace_back(AudioElementType::kObjectMono,
                              BinauralFilterProfile::kDirect);

  // Set up channel indices.
  audio_elements[1].SetFirstChannelIndex(9);  // After 2OA's 9 channels

  // Set object position.
  auto& object_channels = audio_elements[1].GetObjectChannels();
  for (auto& obj : object_channels) {
    obj.SetAzimuth(90.0f);
    obj.SetElevation(30.0f);
    obj.SetDistance(0.5f);
  }

  // Update encoder.
  ASSERT_THAT(group.UpdateAmbisonicEncoder(audio_elements), IsOk());

  // Create input and output buffers (9 channels for 2OA + 1 for object) and
  // fill input with test data.
  // 9 channels for 2OA + 1 for object
  AudioBuffer input_buffer(10, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);

  // Fill input with test data.
  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  const WorldRotation rotation;
  group.Process(input_buffer, audio_elements, false, rotation, &output_buffer);

  // Verify output is non-zero.
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test ProcessingGroup::UpdateAmbisonicEncoder with no sources.
TEST_F(ProcessingGroupTest, TestUpdateEncoderNoSources) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create audio elements with only Ambisonics (no objects/layouts to encode).
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);

  // Should succeed even with no sources to encode.
  EXPECT_THAT(group.UpdateAmbisonicEncoder(audio_elements), IsOk());
}

// Test ProcessingGroup::UpdateAmbisonicEncoder with object position updates.
TEST_F(ProcessingGroupTest, TestUpdateEncoderObjectPositions) {
  const ProcessingGroupKey key{2, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create audio elements with object.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::kObjectMono,
                              BinauralFilterProfile::kDirect);

  // Set initial object position.
  auto& object_channels = audio_elements[0].GetObjectChannels();
  for (auto& obj : object_channels) {
    obj.SetAzimuth(0.0f);
    obj.SetElevation(0.0f);
    obj.SetDistance(1.0f);
  }

  EXPECT_THAT(group.UpdateAmbisonicEncoder(audio_elements), IsOk());

  // Update object position.
  for (auto& obj : object_channels) {
    obj.SetAzimuth(180.0f);
    obj.SetElevation(45.0f);
    obj.SetDistance(0.8f);
  }

  // Update encoder again with new position.
  EXPECT_THAT(group.UpdateAmbisonicEncoder(audio_elements), IsOk());
}

// Test ProcessingGroup move semantics.
TEST_F(ProcessingGroupTest, TestMoveConstruction) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group1(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group1.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Move construct group2 from group1.
  ProcessingGroup group2(std::move(group1));

  EXPECT_EQ(group2.GetAmbisonicOrder(), 1);
  EXPECT_EQ(group2.GetFilterProfile(), BinauralFilterProfile::kDirect);
}

// Test ProcessingGroup move assignment.
TEST_F(ProcessingGroupTest, TestMoveAssignment) {
  const ProcessingGroupKey key1{1, BinauralFilterProfile::kDirect};
  const ProcessingGroupKey key2{2, BinauralFilterProfile::kAmbient};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group1(key1, indices, kBufferSizePerChannel, kSamplingRate);
  ProcessingGroup group2(key2, indices, kBufferSizePerChannel, kSamplingRate);

  ASSERT_THAT(group1.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Move assign group2 from group1.
  group2 = std::move(group1);

  EXPECT_EQ(group2.GetAmbisonicOrder(), 1);
  EXPECT_EQ(group2.GetFilterProfile(), BinauralFilterProfile::kDirect);
}

// Test ProcessingGroup with all three filter profiles processing same input.
TEST_F(ProcessingGroupTest, TestAllFilterProfilesWithSameInput) {
  const std::vector<BinauralFilterProfile> profiles = {
      BinauralFilterProfile::kDirect, BinauralFilterProfile::kAmbient,
      BinauralFilterProfile::kReverberant};

  for (const auto& profile : profiles) {
    const ProcessingGroupKey key{1, profile};
    const std::vector<size_t> indices = {0};

    ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
    ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk())
        << "Failed to initialize with profile " << static_cast<int>(profile);

    std::vector<AudioElementConfig> audio_elements;
    audio_elements.emplace_back(AudioElementType::k1OA, profile);

    // Create input and output buffers and fill input with consistent test data.
    AudioBuffer input_buffer(4, kBufferSizePerChannel);
    AudioBuffer output_buffer(2, kBufferSizePerChannel);

    // Fill with consistent test data.
    for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
      GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate,
                                  &input_buffer[ch], ch);
    }

    const WorldRotation rotation;
    group.Process(input_buffer, audio_elements, false, rotation,
                  &output_buffer);

    // Verify output is produced for all profiles.
    EXPECT_TRUE(HasNonZeroOutput(output_buffer))
        << "No output for profile " << static_cast<int>(profile);
  }
}

// Test world-locked element (default) with head tracking rotation.
TEST_F(ProcessingGroupTest, TestWorldLockedElementWithRotation) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create world-locked audio element (head_locked = false, default).
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);
  EXPECT_FALSE(audio_elements[0].IsHeadLocked());  // Default is world-locked

  // Create input buffer with consistent test data.
  AudioBuffer input_buffer(4, kBufferSizePerChannel);
  AudioBuffer output_buffer1(2, kBufferSizePerChannel);
  AudioBuffer output_buffer2(2, kBufferSizePerChannel);

  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  // Process with identity rotation (no head movement).
  const WorldRotation identity_rotation;
  group.Process(input_buffer, audio_elements, true, identity_rotation,
                &output_buffer1);

  // Process with a significant rotation (90 degrees around Y-axis).
  const WorldRotation rotated(0.707f, 0.0f, 0.707f, 0.0f);
  group.Process(input_buffer, audio_elements, true, rotated, &output_buffer2);

  // Outputs should be different because world-locked elements are rotated.
  // (The rotation should affect the spatial characteristics.)
  EXPECT_TRUE(HasNonZeroOutput(output_buffer1));
  EXPECT_TRUE(HasNonZeroOutput(output_buffer2));
  // Outputs should differ (not identical) due to rotation affecting
  // world-locked.
  bool outputs_differ = false;
  for (size_t ch = 0; ch < 2; ++ch) {
    for (size_t i = 0; i < kBufferSizePerChannel; ++i) {
      if (std::abs(output_buffer1[ch][i] - output_buffer2[ch][i]) > 1e-5f) {
        outputs_differ = true;
        break;
      }
    }
    if (outputs_differ) break;
  }
  EXPECT_TRUE(outputs_differ) << "World-locked element should be affected by "
                                 "rotation";
}

// Test head-locked element with head tracking rotation.
TEST_F(ProcessingGroupTest, TestHeadLockedElementWithRotation) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create head-locked audio element.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);
  audio_elements[0].SetHeadLocked(true);
  EXPECT_TRUE(audio_elements[0].IsHeadLocked());

  // Create input buffer with consistent test data.
  AudioBuffer input_buffer(4, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);

  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  // Process with head tracking enabled and a rotation.
  // Since element is head-locked, rotation should not affect it significantly.
  const WorldRotation rotated(0.707f, 0.0f, 0.707f, 0.0f);
  group.Process(input_buffer, audio_elements, true, rotated, &output_buffer);

  // Verify that head-locked element produces valid binaural output.
  // The critical property is that it processes successfully and produces
  // output.
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test mixed world-locked and head-locked elements.
TEST_F(ProcessingGroupTest, TestMixedWorldAndHeadLockedElements) {
  const ProcessingGroupKey key{2, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0, 1};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create two elements: one world-locked, one head-locked.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k2OA,
                              BinauralFilterProfile::kDirect);
  audio_elements.emplace_back(AudioElementType::kObjectMono,
                              BinauralFilterProfile::kDirect);
  audio_elements[0].SetHeadLocked(false);     // World-locked
  audio_elements[1].SetHeadLocked(true);      // Head-locked
  audio_elements[1].SetFirstChannelIndex(9);  // After 2OA's 9 channels

  // Set object position.
  auto& object_channels = audio_elements[1].GetObjectChannels();
  for (auto& obj : object_channels) {
    obj.SetAzimuth(90.0f);
    obj.SetElevation(0.0f);
    obj.SetDistance(1.0f);
  }

  ASSERT_THAT(group.UpdateAmbisonicEncoder(audio_elements), IsOk());

  AudioBuffer input_buffer(10, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);

  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  const WorldRotation rotation(0.707f, 0.0f, 0.707f, 0.0f);
  group.Process(input_buffer, audio_elements, true, rotation, &output_buffer);

  // Should produce valid binaural output with both elements mixed.
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test all elements world-locked (edge case).
TEST_F(ProcessingGroupTest, TestAllElementsWorldLocked) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);
  // Explicitly set to world-locked (which is the default).
  audio_elements[0].SetHeadLocked(false);

  AudioBuffer input_buffer(4, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);

  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  const WorldRotation rotation(0.707f, 0.0f, 0.707f, 0.0f);
  group.Process(input_buffer, audio_elements, true, rotation, &output_buffer);

  // Should process without issues even with all elements world-locked.
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test all elements head-locked (edge case).
TEST_F(ProcessingGroupTest, TestAllElementsHeadLocked) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);
  // Explicitly set to head-locked.
  audio_elements[0].SetHeadLocked(true);

  AudioBuffer input_buffer(4, kBufferSizePerChannel);
  AudioBuffer output_buffer(2, kBufferSizePerChannel);

  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  const WorldRotation rotation(0.707f, 0.0f, 0.707f, 0.0f);
  group.Process(input_buffer, audio_elements, true, rotation, &output_buffer);

  // Should process without issues even with all elements head-locked.
  EXPECT_TRUE(HasNonZeroOutput(output_buffer));
}

// Test head-locked override: disable global head tracking.
TEST_F(ProcessingGroupTest, TestHeadTrackingDisabledOverride) {
  const ProcessingGroupKey key{1, BinauralFilterProfile::kDirect};
  const std::vector<size_t> indices = {0};

  ProcessingGroup group(key, indices, kBufferSizePerChannel, kSamplingRate);
  ASSERT_THAT(group.Initialize(fft_manager_.get(), resampler_.get()), IsOk());

  // Create a world-locked element.
  std::vector<AudioElementConfig> audio_elements;
  audio_elements.emplace_back(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);
  audio_elements[0].SetHeadLocked(false);  // World-locked by default

  AudioBuffer input_buffer(4, kBufferSizePerChannel);
  AudioBuffer output_buffer1(2, kBufferSizePerChannel);
  AudioBuffer output_buffer2(2, kBufferSizePerChannel);

  for (size_t ch = 0; ch < input_buffer.num_channels(); ++ch) {
    GenerateSineWithRandomPhase(440.0f, 0.5f, kSamplingRate, &input_buffer[ch],
                                ch);
  }

  const WorldRotation rotation(0.707f, 0.0f, 0.707f, 0.0f);

  // Process with head tracking enabled (should be affected by rotation).
  group.Process(input_buffer, audio_elements, true, rotation, &output_buffer1);

  // Process with head tracking disabled (should not be affected by rotation).
  group.Process(input_buffer, audio_elements, false, rotation, &output_buffer2);

  // Outputs should be different when head tracking is enabled vs disabled.
  bool outputs_differ = false;
  for (size_t ch = 0; ch < 2; ++ch) {
    for (size_t i = 0; i < kBufferSizePerChannel; ++i) {
      if (std::abs(output_buffer1[ch][i] - output_buffer2[ch][i]) > 1e-5f) {
        outputs_differ = true;
        break;
      }
    }
    if (outputs_differ) break;
  }
  EXPECT_TRUE(outputs_differ)
      << "Global head tracking enable/disable should override per-element "
         "settings";
}

// Test element head-locked status accessors.
TEST_F(ProcessingGroupTest, TestHeadLockedAccessors) {
  // Create element with default (world-locked).
  AudioElementConfig element1(AudioElementType::k1OA,
                              BinauralFilterProfile::kDirect);
  EXPECT_FALSE(element1.IsHeadLocked());  // Default is world-locked

  // Set to head-locked.
  element1.SetHeadLocked(true);
  EXPECT_TRUE(element1.IsHeadLocked());

  // Set back to world-locked.
  element1.SetHeadLocked(false);
  EXPECT_FALSE(element1.IsHeadLocked());

  // Create another element with different type.
  AudioElementConfig element2(AudioElementType::kObjectMono,
                              BinauralFilterProfile::kAmbient);
  EXPECT_FALSE(element2.IsHeadLocked());  // Default is world-locked

  // Set to head-locked.
  element2.SetHeadLocked(true);
  EXPECT_TRUE(element2.IsHeadLocked());
}

}  // namespace
}  // namespace obr
