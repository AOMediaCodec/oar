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

#include "obr/renderer/audio_element_type.h"

#include <string>
#include <vector>

#include "absl/status/status_matchers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace obr {
namespace {

using ::absl_testing::IsOkAndHolds;

// Test that the AudioElementType enum is correctly converted to a string.
TEST(AudioElementTypeTest, AudioElementTypeToString) {
  // Ambisonics types.
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::k1OA),
              IsOkAndHolds("k1OA"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::k2OA),
              IsOkAndHolds("k2OA"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::k3OA),
              IsOkAndHolds("k3OA"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::k4OA),
              IsOkAndHolds("k4OA"));
  // Loudspeaker layout types.
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayoutMono),
              IsOkAndHolds("kLayoutMono"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayoutStereo),
              IsOkAndHolds("kLayoutStereo"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout5_1_0_ch),
              IsOkAndHolds("kLayout5_1_0_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout5_1_2_ch),
              IsOkAndHolds("kLayout5_1_2_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout5_1_4_ch),
              IsOkAndHolds("kLayout5_1_4_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout7_1_0_ch),
              IsOkAndHolds("kLayout7_1_0_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout7_1_2_ch),
              IsOkAndHolds("kLayout7_1_2_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout7_1_4_ch),
              IsOkAndHolds("kLayout7_1_4_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout3_1_2_ch),
              IsOkAndHolds("kLayout3_1_2_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout9_1_6_ch),
              IsOkAndHolds("kLayout9_1_6_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout9_1_6_ch_alt),
              IsOkAndHolds("kLayout9_1_6_ch_alt"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout7_1_5_4_ch),
              IsOkAndHolds("kLayout7_1_5_4_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kLayout10_2_9_3_ch),
              IsOkAndHolds("kLayout10_2_9_3_ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetLFE),
              IsOkAndHolds("kSubsetLFE"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetStereo_S),
              IsOkAndHolds("kSubsetStereo_S"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetStereo_SS),
              IsOkAndHolds("kSubsetStereo_SS"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetStereo_RS),
              IsOkAndHolds("kSubsetStereo_RS"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetStereo_TF),
              IsOkAndHolds("kSubsetStereo_TF"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetStereo_TB),
              IsOkAndHolds("kSubsetStereo_TB"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetTop_4ch),
              IsOkAndHolds("kSubsetTop_4ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubset3_0ch),
              IsOkAndHolds("kSubset3_0ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetStereo_F),
              IsOkAndHolds("kSubsetStereo_F"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetStereo_Si),
              IsOkAndHolds("kSubsetStereo_Si"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetStereo_TpSi),
              IsOkAndHolds("kSubsetStereo_TpSi"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetTop_6ch),
              IsOkAndHolds("kSubsetTop_6ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetLFE_Pair),
              IsOkAndHolds("kSubsetLFE_Pair"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetBottom_3ch),
              IsOkAndHolds("kSubsetBottom_3ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetBottom_4ch),
              IsOkAndHolds("kSubsetBottom_4ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetTop_1ch),
              IsOkAndHolds("kSubsetTop_1ch"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kSubsetTop_5ch),
              IsOkAndHolds("kSubsetTop_5ch"));
  // Object types.
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kObjectMono),
              IsOkAndHolds("kObjectMono"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kObjectDual),
              IsOkAndHolds("kObjectDual"));
  // Passthrough types.
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kPassthroughMono),
              IsOkAndHolds("kPassthroughMono"));
  EXPECT_THAT(GetAudioElementTypeStr(AudioElementType::kPassthroughStereo),
              IsOkAndHolds("kPassthroughStereo"));
}

// Test that the AudioElementType string is correctly converted to an enum.
TEST(AudioElementTypeTest, AudioElementTypeFromString) {
  // Ambisonics types.
  EXPECT_THAT(GetAudioElementTypeFromStr("k1OA"),
              IsOkAndHolds(AudioElementType::k1OA));
  EXPECT_THAT(GetAudioElementTypeFromStr("k2OA"),
              IsOkAndHolds(AudioElementType::k2OA));
  EXPECT_THAT(GetAudioElementTypeFromStr("k3OA"),
              IsOkAndHolds(AudioElementType::k3OA));
  EXPECT_THAT(GetAudioElementTypeFromStr("k4OA"),
              IsOkAndHolds(AudioElementType::k4OA));
  // Loudspeaker layout types.
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayoutMono"),
              IsOkAndHolds(AudioElementType::kLayoutMono));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayoutStereo"),
              IsOkAndHolds(AudioElementType::kLayoutStereo));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout5_1_0_ch"),
              IsOkAndHolds(AudioElementType::kLayout5_1_0_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout5_1_2_ch"),
              IsOkAndHolds(AudioElementType::kLayout5_1_2_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout5_1_4_ch"),
              IsOkAndHolds(AudioElementType::kLayout5_1_4_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout7_1_0_ch"),
              IsOkAndHolds(AudioElementType::kLayout7_1_0_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout7_1_2_ch"),
              IsOkAndHolds(AudioElementType::kLayout7_1_2_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout7_1_4_ch"),
              IsOkAndHolds(AudioElementType::kLayout7_1_4_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout3_1_2_ch"),
              IsOkAndHolds(AudioElementType::kLayout3_1_2_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout9_1_6_ch"),
              IsOkAndHolds(AudioElementType::kLayout9_1_6_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout9_1_6_ch_alt"),
              IsOkAndHolds(AudioElementType::kLayout9_1_6_ch_alt));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout7_1_5_4_ch"),
              IsOkAndHolds(AudioElementType::kLayout7_1_5_4_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kLayout10_2_9_3_ch"),
              IsOkAndHolds(AudioElementType::kLayout10_2_9_3_ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetLFE"),
              IsOkAndHolds(AudioElementType::kSubsetLFE));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetStereo_S"),
              IsOkAndHolds(AudioElementType::kSubsetStereo_S));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetStereo_SS"),
              IsOkAndHolds(AudioElementType::kSubsetStereo_SS));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetStereo_RS"),
              IsOkAndHolds(AudioElementType::kSubsetStereo_RS));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetStereo_TF"),
              IsOkAndHolds(AudioElementType::kSubsetStereo_TF));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetStereo_TB"),
              IsOkAndHolds(AudioElementType::kSubsetStereo_TB));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetTop_4ch"),
              IsOkAndHolds(AudioElementType::kSubsetTop_4ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubset3_0ch"),
              IsOkAndHolds(AudioElementType::kSubset3_0ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetStereo_F"),
              IsOkAndHolds(AudioElementType::kSubsetStereo_F));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetStereo_Si"),
              IsOkAndHolds(AudioElementType::kSubsetStereo_Si));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetStereo_TpSi"),
              IsOkAndHolds(AudioElementType::kSubsetStereo_TpSi));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetTop_6ch"),
              IsOkAndHolds(AudioElementType::kSubsetTop_6ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetLFE_Pair"),
              IsOkAndHolds(AudioElementType::kSubsetLFE_Pair));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetBottom_3ch"),
              IsOkAndHolds(AudioElementType::kSubsetBottom_3ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetBottom_4ch"),
              IsOkAndHolds(AudioElementType::kSubsetBottom_4ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetTop_1ch"),
              IsOkAndHolds(AudioElementType::kSubsetTop_1ch));
  EXPECT_THAT(GetAudioElementTypeFromStr("kSubsetTop_5ch"),
              IsOkAndHolds(AudioElementType::kSubsetTop_5ch));
  // Object types.
  EXPECT_THAT(GetAudioElementTypeFromStr("kObjectMono"),
              IsOkAndHolds(AudioElementType::kObjectMono));
  EXPECT_THAT(GetAudioElementTypeFromStr("kObjectDual"),
              IsOkAndHolds(AudioElementType::kObjectDual));
  // Passthrough types.
  EXPECT_THAT(GetAudioElementTypeFromStr("kPassthroughMono"),
              IsOkAndHolds(AudioElementType::kPassthroughMono));
  EXPECT_THAT(GetAudioElementTypeFromStr("kPassthroughStereo"),
              IsOkAndHolds(AudioElementType::kPassthroughStereo));
}

// Test that an invalid AudioElementType string returns an error.
TEST(AudioElementTypeTest, AudioElementTypeFromStringError) {
  EXPECT_FALSE(GetAudioElementTypeFromStr("kInvalid").ok());
}

// Test that a list of available AudioElementTypes is returned.
TEST(AudioElementTypeTest, GetAvailableAudioElementTypesAsStr) {
  const std::vector<std::string> expected = {
      "k1OA",
      "k2OA",
      "k3OA",
      "k4OA",
      "kLayoutMono",
      "kLayoutStereo",
      "kLayout5_1_0_ch",
      "kLayout5_1_2_ch",
      "kLayout5_1_4_ch",
      "kLayout7_1_0_ch",
      "kLayout7_1_2_ch",
      "kLayout7_1_4_ch",
      "kLayout3_1_2_ch",
      "kLayout9_1_6_ch",
      "kLayout9_1_6_ch_alt",
      "kLayout7_1_5_4_ch",
      "kLayout10_2_9_3_ch",
      "kSubsetLFE",
      "kSubsetStereo_S",
      "kSubsetStereo_SS",
      "kSubsetStereo_RS",
      "kSubsetStereo_TF",
      "kSubsetStereo_TB",
      "kSubsetTop_4ch",
      "kSubset3_0ch",
      "kSubsetStereo_F",
      "kSubsetStereo_Si",
      "kSubsetStereo_TpSi",
      "kSubsetTop_6ch",
      "kSubsetLFE_Pair",
      "kSubsetBottom_3ch",
      "kSubsetBottom_4ch",
      "kSubsetTop_1ch",
      "kSubsetTop_5ch",
      "kObjectMono",
      "kObjectDual",
      "kPassthroughMono",
      "kPassthroughStereo",
  };
  EXPECT_EQ(GetAvailableAudioElementTypesAsStr(), expected);
}

// Test IsAmbisonicsType.
TEST(AudioElementTypeTest, IsAmbisonicsType) {
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kPassthroughMono));
  EXPECT_TRUE(IsAmbisonicsType(AudioElementType::k1OA));
  EXPECT_TRUE(IsAmbisonicsType(AudioElementType::k2OA));
  EXPECT_TRUE(IsAmbisonicsType(AudioElementType::k3OA));
  EXPECT_TRUE(IsAmbisonicsType(AudioElementType::k4OA));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayoutMono));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayoutStereo));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout5_1_0_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout5_1_2_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout5_1_4_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout7_1_0_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout7_1_2_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout7_1_4_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout3_1_2_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout9_1_6_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout9_1_6_ch_alt));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout7_1_5_4_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kLayout10_2_9_3_ch));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kSubsetLFE));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kObjectMono));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kObjectDual));
  EXPECT_FALSE(IsAmbisonicsType(AudioElementType::kPassthroughStereo));
}

// Test IsLoudspeakerLayoutType.
TEST(AudioElementTypeTest, IsLoudspeakerLayoutType) {
  EXPECT_FALSE(IsLoudspeakerLayoutType(AudioElementType::k1OA));
  EXPECT_FALSE(IsLoudspeakerLayoutType(AudioElementType::k2OA));
  EXPECT_FALSE(IsLoudspeakerLayoutType(AudioElementType::k3OA));
  EXPECT_FALSE(IsLoudspeakerLayoutType(AudioElementType::k4OA));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayoutMono));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayoutStereo));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout5_1_0_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout5_1_2_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout5_1_4_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout7_1_0_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout7_1_2_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout7_1_4_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout3_1_2_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout9_1_6_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout9_1_6_ch_alt));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout7_1_5_4_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kLayout10_2_9_3_ch));
  EXPECT_TRUE(IsLoudspeakerLayoutType(AudioElementType::kSubsetLFE));
  EXPECT_FALSE(IsLoudspeakerLayoutType(AudioElementType::kObjectMono));
  EXPECT_FALSE(IsLoudspeakerLayoutType(AudioElementType::kObjectDual));
}

// Test IsObjectType.
TEST(AudioElementTypeTest, IsObjectType) {
  EXPECT_FALSE(IsObjectType(AudioElementType::k1OA));
  EXPECT_FALSE(IsObjectType(AudioElementType::k2OA));
  EXPECT_FALSE(IsObjectType(AudioElementType::k3OA));
  EXPECT_FALSE(IsObjectType(AudioElementType::k4OA));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayoutMono));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayoutStereo));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout5_1_0_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout5_1_2_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout5_1_4_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout7_1_0_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout7_1_2_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout7_1_4_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout3_1_2_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout9_1_6_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout9_1_6_ch_alt));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout7_1_5_4_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kLayout10_2_9_3_ch));
  EXPECT_FALSE(IsObjectType(AudioElementType::kSubsetLFE));
  EXPECT_TRUE(IsObjectType(AudioElementType::kObjectMono));
  EXPECT_TRUE(IsObjectType(AudioElementType::kObjectDual));
}

// Test GetAmbisonicOrder.
TEST(AudioElementTypeTest, GetAmbisonicOrder) {
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kPassthroughMono).ok());
  EXPECT_THAT(GetAmbisonicOrder(AudioElementType::k1OA), IsOkAndHolds(1));
  EXPECT_THAT(GetAmbisonicOrder(AudioElementType::k2OA), IsOkAndHolds(2));
  EXPECT_THAT(GetAmbisonicOrder(AudioElementType::k3OA), IsOkAndHolds(3));
  EXPECT_THAT(GetAmbisonicOrder(AudioElementType::k4OA), IsOkAndHolds(4));
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayoutMono).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayoutStereo).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout5_1_0_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout5_1_2_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout5_1_4_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout7_1_0_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout7_1_2_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout7_1_4_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout3_1_2_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout9_1_6_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout9_1_6_ch_alt).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout7_1_5_4_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kLayout10_2_9_3_ch).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kSubsetLFE).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kObjectMono).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kObjectDual).ok());
  EXPECT_FALSE(GetAmbisonicOrder(AudioElementType::kPassthroughStereo).ok());
}

// Test IsPassthroughType.
TEST(AudioElementTypeTest, IsPassthroughType) {
  EXPECT_TRUE(IsPassthroughType(AudioElementType::kPassthroughMono));
  EXPECT_TRUE(IsPassthroughType(AudioElementType::kPassthroughStereo));
  EXPECT_FALSE(IsPassthroughType(AudioElementType::k1OA));
  EXPECT_FALSE(IsPassthroughType(AudioElementType::k2OA));
  EXPECT_FALSE(IsPassthroughType(AudioElementType::k3OA));
  EXPECT_FALSE(IsPassthroughType(AudioElementType::k4OA));
  EXPECT_FALSE(IsPassthroughType(AudioElementType::kLayoutMono));
  EXPECT_FALSE(IsPassthroughType(AudioElementType::kLayoutStereo));
  EXPECT_FALSE(IsPassthroughType(AudioElementType::kObjectMono));
  EXPECT_FALSE(IsPassthroughType(AudioElementType::kObjectDual));
}

}  // namespace

}  // namespace obr
