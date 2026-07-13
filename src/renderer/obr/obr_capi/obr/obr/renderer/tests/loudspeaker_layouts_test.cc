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

#include "obr/renderer/loudspeaker_layouts.h"

#include "gtest/gtest.h"
#include "obr/renderer/audio_element_type.h"

namespace obr {
namespace {

// Test that the loudspeaker layout is correctly returned for a given layout
// name.
TEST(LoudspeakerLayoutsTest, GetLoudspeakerLayout) {
  LoudspeakerLayouts loudspeaker_layouts;

  auto layout =
      loudspeaker_layouts.getLoudspeakerLayout(AudioElementType::kLayoutMono);
  EXPECT_EQ(layout.size(), 1);
  EXPECT_EQ(layout[0].GetID(), "kC");

  layout =
      loudspeaker_layouts.getLoudspeakerLayout(AudioElementType::kLayoutStereo);
  EXPECT_EQ(layout.size(), 2);
  EXPECT_EQ(layout[0].GetID(), "kL30");
  EXPECT_EQ(layout[1].GetID(), "kR30");
}

// Test 5.1.2 layout uses updated speaker names.
TEST(LoudspeakerLayoutsTest, GetLayout5_1_2) {
  LoudspeakerLayouts loudspeaker_layouts;

  auto layout = loudspeaker_layouts.getLoudspeakerLayout(
      AudioElementType::kLayout5_1_2_ch);
  EXPECT_EQ(layout.size(), 8);
  // Verify the upper speakers use the new naming convention (kUL/kUR).
  EXPECT_EQ(layout[6].GetID(), "kUL30");
  EXPECT_EQ(layout[7].GetID(), "kUR30");
}

// Test 7.1.4 layout uses updated speaker names.
TEST(LoudspeakerLayoutsTest, GetLayout7_1_4) {
  LoudspeakerLayouts loudspeaker_layouts;

  auto layout = loudspeaker_layouts.getLoudspeakerLayout(
      AudioElementType::kLayout7_1_4_ch);
  EXPECT_EQ(layout.size(), 12);
  // Verify the upper speakers use the new naming convention (kUL/kUR).
  EXPECT_EQ(layout[8].GetID(), "kUL45");
  EXPECT_EQ(layout[9].GetID(), "kUR45");
  EXPECT_EQ(layout[10].GetID(), "kUL135");
  EXPECT_EQ(layout[11].GetID(), "kUR135");
}

// Test new 9.1.6 layout.
TEST(LoudspeakerLayoutsTest, GetLayout9_1_6) {
  LoudspeakerLayouts loudspeaker_layouts;

  auto layout = loudspeaker_layouts.getLoudspeakerLayout(
      AudioElementType::kLayout9_1_6_ch);
  EXPECT_EQ(layout.size(), 16);
  // First speaker is kL60.
  EXPECT_EQ(layout[0].GetID(), "kL60");
  // LFE1 at position 3.
  EXPECT_EQ(layout[3].GetID(), "kLFE1");
}

// Test new 7.1.5.4 layout.
TEST(LoudspeakerLayoutsTest, GetLayout7_1_5_4) {
  LoudspeakerLayouts loudspeaker_layouts;

  auto layout = loudspeaker_layouts.getLoudspeakerLayout(
      AudioElementType::kLayout7_1_5_4_ch);
  EXPECT_EQ(layout.size(), 17);
  // Verify top speaker.
  EXPECT_EQ(layout[10].GetID(), "kT");
  // Verify bottom speakers.
  EXPECT_EQ(layout[13].GetID(), "kBL45");
  EXPECT_EQ(layout[14].GetID(), "kBR45");
}

// Test subset layout.
TEST(LoudspeakerLayoutsTest, GetSubsetLayout) {
  LoudspeakerLayouts loudspeaker_layouts;

  auto layout =
      loudspeaker_layouts.getLoudspeakerLayout(AudioElementType::kSubsetLFE);
  EXPECT_EQ(layout.size(), 1);
  EXPECT_EQ(layout[0].GetID(), "kLFE");

  layout = loudspeaker_layouts.getLoudspeakerLayout(
      AudioElementType::kSubsetStereo_TF);
  EXPECT_EQ(layout.size(), 2);
  EXPECT_EQ(layout[0].GetID(), "kUL45");
  EXPECT_EQ(layout[1].GetID(), "kUR45");
}

}  // namespace
}  // namespace obr
