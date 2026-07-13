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

#include "obr/audio_buffer/channel_view.h"

#include <cstddef>
#include <vector>

#include "gtest/gtest.h"
#include "obr/audio_buffer/audio_buffer.h"

namespace obr {

namespace {

const float kTestData[] = {0.0f, 1.0f, 2.0f};
const size_t kTestDataSize = sizeof(kTestData) / sizeof(float);

typedef std::vector<float> Buffer;

// Tests initialization of `ChannelView` class.
TEST(ChannelView, InitializationTest) {
  AudioBuffer test_buffer(1, kTestDataSize);
  ChannelView& test_buffer_view = test_buffer[0];
  for (size_t i = 0; i < kTestDataSize; ++i) {
    test_buffer_view[i] = kTestData[i];
  }

  EXPECT_EQ(test_buffer.num_frames(), test_buffer_view.size());
  EXPECT_EQ(&test_buffer[0][0], test_buffer_view.begin());
  EXPECT_EQ(&test_buffer[0][0] + test_buffer.num_frames(),
            test_buffer_view.end());
}

// Tests iterators and array subscript of `ChannelView`.
TEST(ChannelView, IteratorAndArraySubscriptTest) {
  AudioBuffer test_buffer(1, kTestDataSize);
  ChannelView& test_buffer_view = test_buffer[0];
  for (size_t i = 0; i < kTestDataSize; ++i) {
    test_buffer_view[i] = kTestData[i];
  }

  for (size_t i = 0; i < test_buffer_view.size(); i++) {
    EXPECT_EQ(test_buffer[0][i], test_buffer_view[i]);
    EXPECT_EQ(kTestData[i], test_buffer_view[i]);
  }

  // Test range-based for-loops.
  for (float& sample : test_buffer_view) {
    sample *= 2.0f;
  }
  size_t idx = 0;
  for (const float& sample : test_buffer_view) {
    EXPECT_EQ(kTestData[idx] * 2.0f, sample);
    ++idx;
  }
}

// Tests copy-assignment operators.
TEST(ChannelView, CopyAssignmentTest) {
  AudioBuffer test_buffer(1, kTestDataSize);
  ChannelView& test_buffer_view = test_buffer[0];
  for (size_t i = 0; i < kTestDataSize; ++i) {
    test_buffer_view[i] = kTestData[i];
  }

  AudioBuffer target_buffer(1, kTestDataSize);
  ChannelView& target_vector_view = target_buffer[0];
  for (size_t i = 0; i < kTestDataSize; ++i) {
    target_vector_view[i] = -1.0f;
  }

  // Copy assignment from ChannelView.
  target_vector_view = test_buffer_view;

  for (size_t i = 0; i < test_buffer_view.size(); i++) {
    EXPECT_EQ(test_buffer_view[i], target_vector_view[i]);
  }

  AudioBuffer target2_buffer(1, kTestDataSize);
  ChannelView& target2_vector_view = target2_buffer[0];
  for (size_t i = 0; i < kTestDataSize; ++i) {
    target2_vector_view[i] = -1.0f;
  }

  // Copy assignment from AudioBuffer channel.
  target2_vector_view = test_buffer[0];

  for (size_t i = 0; i < test_buffer_view.size(); i++) {
    EXPECT_EQ(test_buffer[0][i], target2_vector_view[i]);
  }
}

// Tests addition-assignment operator.
TEST(ChannelView, AdditionOperatorTest) {
  // Here an AudioBuffer is used to ensure that the data is aligned.
  AudioBuffer test_buffer(1, kTestDataSize);
  ChannelView& test_buffer_view = test_buffer[0];
  for (size_t i = 0; i < kTestDataSize; ++i) {
    test_buffer_view[i] = kTestData[i];
  }

  // Execute `ChannelView`s addition operator.
  test_buffer_view += test_buffer_view;

  for (size_t i = 0; i < test_buffer_view.size(); i++) {
    EXPECT_EQ(kTestData[i] * 2.0f, test_buffer_view[i]);
  }
}

// Tests subtraction-assignment operator.
TEST(ChannelView, SubtractionOperatorTest) {
  // Here an AudioBuffer is used to ensure that the data is aligned.
  AudioBuffer test_buffer(1, kTestDataSize);
  ChannelView& test_buffer_view = test_buffer[0];
  for (size_t i = 0; i < kTestDataSize; ++i) {
    test_buffer_view[i] = kTestData[i];
  }

  // Execute `ChannelView`s subtraction operator.
  test_buffer_view -= test_buffer_view;

  for (size_t i = 0; i < test_buffer_view.size(); i++) {
    EXPECT_EQ(0.0f, test_buffer_view[i]);
  }
}

// Tests Clear method.
TEST(ChannelView, ClearTest) {
  AudioBuffer test_buffer(1, kTestDataSize);
  ChannelView& test_buffer_view = test_buffer[0];
  for (size_t i = 0; i < kTestDataSize; ++i) {
    test_buffer_view[i] = kTestData[i];
  }

  test_buffer_view.Clear();

  for (const float& sample : test_buffer_view) {
    EXPECT_EQ(0.0f, sample);
  }
}

}  // namespace

}  // namespace obr
