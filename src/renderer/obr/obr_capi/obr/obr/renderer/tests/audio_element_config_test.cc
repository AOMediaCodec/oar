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

#include "obr/renderer/audio_element_config.h"

#include "gtest/gtest.h"
#include "obr/renderer/audio_element_type.h"

namespace obr {
namespace {

// Test creation of an audio element configuration.
TEST(AudioElementConfigTest, CreateAudioElementConfig) {
  AudioElementConfig audio_element_config(AudioElementType::kLayoutMono);
  EXPECT_EQ(audio_element_config.GetType(), AudioElementType::kLayoutMono);

  audio_element_config.SetFirstChannelIndex(0);
  EXPECT_EQ(audio_element_config.GetFirstChannelIndex(), 0);
  EXPECT_EQ(audio_element_config.GetNumberOfInputChannels(), 1);

  auto input_channels = audio_element_config.GetLoudspeakerChannels();
  EXPECT_EQ(input_channels.size(), 1);
}

// Test `kObjectMono` audio element configuration.
TEST(AudioElementConfigTest, CreateMonoObjectAudioElementConfig) {
  AudioElementConfig audio_element_config(AudioElementType::kObjectMono);
  EXPECT_EQ(audio_element_config.GetType(), AudioElementType::kObjectMono);
  EXPECT_EQ(audio_element_config.GetNumberOfInputChannels(), 1);

  auto& object_channels = audio_element_config.GetObjectChannels();
  EXPECT_EQ(object_channels.size(), 1);

  // Set azimuth, elevation, and distance.
  for (auto& channel : object_channels) {
    channel.SetAzimuth(0);
    channel.SetElevation(0);
    channel.SetDistance(1);
  }
}

// Test `kObjectDual` audio element configuration.
TEST(AudioElementConfigTest, CreateDualObjectAudioElementConfig) {
  AudioElementConfig audio_element_config(AudioElementType::kObjectDual);
  EXPECT_EQ(audio_element_config.GetType(), AudioElementType::kObjectDual);
  EXPECT_EQ(audio_element_config.GetNumberOfInputChannels(), 2);

  auto& object_channels = audio_element_config.GetObjectChannels();
  EXPECT_EQ(object_channels.size(), 2);
}

// Test `kObjectDual` channels can have independent positions.
TEST(AudioElementConfigTest, DualObjectChannelsHaveIndependentPositions) {
  AudioElementConfig audio_element_config(AudioElementType::kObjectDual);

  auto& object_channels = audio_element_config.GetObjectChannels();
  ASSERT_EQ(object_channels.size(), 2);

  // Set channel 0 to left position.
  object_channels[0].SetAzimuth(-90.0f);
  object_channels[0].SetElevation(15.0f);
  object_channels[0].SetDistance(0.5f);

  // Set channel 1 to right position.
  object_channels[1].SetAzimuth(90.0f);
  object_channels[1].SetElevation(-15.0f);
  object_channels[1].SetDistance(1.0f);

  // Verify positions are stored independently.
  EXPECT_FLOAT_EQ(object_channels[0].GetAzimuth(), -90.0f);
  EXPECT_FLOAT_EQ(object_channels[0].GetElevation(), 15.0f);
  EXPECT_FLOAT_EQ(object_channels[0].GetDistance(), 0.5f);

  EXPECT_FLOAT_EQ(object_channels[1].GetAzimuth(), 90.0f);
  EXPECT_FLOAT_EQ(object_channels[1].GetElevation(), -15.0f);
  EXPECT_FLOAT_EQ(object_channels[1].GetDistance(), 1.0f);
}

// Test `kObjectDual` with different binaural filter profiles.
TEST(AudioElementConfigTest, DualObjectWithFilterProfiles) {
  using FilterType = BinauralFilterProfile;

  AudioElementConfig direct_config(AudioElementType::kObjectDual,
                                   FilterType::kDirect);
  EXPECT_EQ(direct_config.GetType(), AudioElementType::kObjectDual);
  EXPECT_EQ(direct_config.GetBinauralFilterProfile(), FilterType::kDirect);
  EXPECT_EQ(direct_config.GetNumberOfInputChannels(), 2);

  AudioElementConfig ambient_config(AudioElementType::kObjectDual,
                                    FilterType::kAmbient);
  EXPECT_EQ(ambient_config.GetType(), AudioElementType::kObjectDual);
  EXPECT_EQ(ambient_config.GetBinauralFilterProfile(), FilterType::kAmbient);
  EXPECT_EQ(ambient_config.GetNumberOfInputChannels(), 2);

  AudioElementConfig reverberant_config(AudioElementType::kObjectDual,
                                        FilterType::kReverberant);
  EXPECT_EQ(reverberant_config.GetType(), AudioElementType::kObjectDual);
  EXPECT_EQ(reverberant_config.GetBinauralFilterProfile(),
            FilterType::kReverberant);
  EXPECT_EQ(reverberant_config.GetNumberOfInputChannels(), 2);
}

// Test construction with all BinauralFilterProfile values.
TEST(AudioElementConfigTest, CreateAudioElementConfigWithFilterType) {
  using FilterType = BinauralFilterProfile;
  AudioElementConfig direct_config(AudioElementType::kLayoutMono,
                                   FilterType::kDirect);
  EXPECT_EQ(direct_config.GetType(), AudioElementType::kLayoutMono);
  EXPECT_EQ(direct_config.GetBinauralFilterProfile(), FilterType::kDirect);

  AudioElementConfig ambient_config(AudioElementType::kLayoutMono,
                                    FilterType::kAmbient);
  EXPECT_EQ(ambient_config.GetType(), AudioElementType::kLayoutMono);
  EXPECT_EQ(ambient_config.GetBinauralFilterProfile(), FilterType::kAmbient);

  AudioElementConfig reverberant_config(AudioElementType::kLayoutMono,
                                        FilterType::kReverberant);
  EXPECT_EQ(reverberant_config.GetType(), AudioElementType::kLayoutMono);
  EXPECT_EQ(reverberant_config.GetBinauralFilterProfile(),
            FilterType::kReverberant);
}

}  // namespace
}  // namespace obr
