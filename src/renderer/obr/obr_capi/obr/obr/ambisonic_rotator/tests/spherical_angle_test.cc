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

#include "spherical_angle.h"

#include <cstddef>
#include <vector>

#include "gtest/gtest.h"
#include "obr/common/constants.h"
#include "obr/common/misc_math.h"

namespace obr {

namespace {

// Spherical angle to be used in the rotation tests.
const float kAzimuth = 0.0f;
const float kElevation = 0.0f;
const SphericalAngle kSphericalAngle(0.0f, 0.0f);

// Arbitrary rotation angle to be used in the rotation tests.
const float kRotationAngle = 10.0f * kRadiansFromDegrees;

// Tests that the GetWorldPositionOnUnitSphere() and FromWorldPosition()
// functions act as perfect inverses of one another for angles defined on the
// unit sphere (in this case the vraudio cube speaker layout).
TEST(SphericalAngleTest, CartesianToSphericalAndBackTest) {
  // Azimuth and elevation angles of the cubic spherical loudspeaker array.
  const std::vector<SphericalAngle> kCubeAngles = {
      SphericalAngle::FromDegrees(45.0f, 35.26f),
      SphericalAngle::FromDegrees(-45.0f, 35.26f),
      SphericalAngle::FromDegrees(-135.0f, 35.26f),
      SphericalAngle::FromDegrees(135.0f, 35.26f),
      SphericalAngle::FromDegrees(45.0f, -35.26f),
      SphericalAngle::FromDegrees(-45.0f, -35.26f),
      SphericalAngle::FromDegrees(-135.0f, -35.26f),
      SphericalAngle::FromDegrees(135.0f, -35.26f)};

  for (size_t i = 0; i < kCubeAngles.size(); ++i) {
    const WorldPosition position =
        kCubeAngles[i].GetWorldPositionOnUnitSphere();
    const SphericalAngle angle = SphericalAngle::FromWorldPosition(position);
    EXPECT_EQ(kCubeAngles[i].azimuth(), angle.azimuth());
    EXPECT_EQ(kCubeAngles[i].elevation(), angle.elevation());
  }
}

// Tests the horizontal angle flip across the median plane.
TEST(SphericalAngleTest, FlipTest) {
  const std::vector<SphericalAngle> kTestAngles = {
      SphericalAngle::FromDegrees(45.0f, 35.26f),
      SphericalAngle::FromDegrees(-15.0f, -10.0f)};

  for (size_t i = 0; i < kTestAngles.size(); ++i) {
    SphericalAngle flipped_spherical_angle = kTestAngles[i].FlipAzimuth();

    // Check if the flipped spherical anglee is correct.
    EXPECT_NEAR(kTestAngles[i].azimuth(), -flipped_spherical_angle.azimuth(),
                kEpsilonFloat);
    EXPECT_NEAR((kTestAngles[i].elevation()),
                flipped_spherical_angle.elevation(), kEpsilonFloat);
  }
}

// Tests that the Rotate() function correctly rotates the spherical angle
// against the x axis (right facing in ADM coordinates).
TEST(SphericalAngleTest, RotateXTest) {
  const WorldPosition kAxis = {1.0f, 0.0f, 0.0f};
  const WorldRotation kRotation(AngleAxisf(kRotationAngle, kAxis));
  // Rotate against the x axis (right facing). This changes elevation.

  const SphericalAngle kXrotatedSphericalAngle =
      kSphericalAngle.Rotate(kRotation);

  // Check if the rotated spherical angle is correct. Positive rotation around
  // X (right) tilts the direction upward (positive elevation).
  EXPECT_NEAR(kAzimuth, kXrotatedSphericalAngle.azimuth(), kEpsilonFloat);
  EXPECT_NEAR((kElevation + kRotationAngle),
              kXrotatedSphericalAngle.elevation(), kEpsilonFloat);
}

// Tests that the Rotate() function correctly rotates the spherical angle
// against the y axis (forward facing in ADM coordinates).
TEST(SphericalAngleTest, RotateYTest) {
  const WorldPosition kAxis(0.0f, 1.0f, 0.0f);
  const WorldRotation kRotation(AngleAxisf(kRotationAngle, kAxis));
  // Rotate against the y axis (forward facing). At azimuth=0, elevation=0,
  // we are pointing along the Y axis, so rotation around Y produces no change.
  const SphericalAngle kYrotatedSphericalAngle =
      kSphericalAngle.Rotate(kRotation);

  // Check if the rotated spherical angle is correct (no change when rotating
  // a direction around an axis it lies on).
  EXPECT_NEAR(kAzimuth, kYrotatedSphericalAngle.azimuth(), kEpsilonFloat);
  EXPECT_NEAR(kElevation, kYrotatedSphericalAngle.elevation(), kEpsilonFloat);
}

// Tests that the Rotate() function correctly rotates the spherical angle
// against the Z axis (upward facing in ADM coordinates).
TEST(SphericalAngleTest, RotateZTest) {
  const WorldPosition kAxis = {0.0f, 0.0f, 1.0f};
  const WorldRotation kRotation(AngleAxisf(kRotationAngle, kAxis));
  // Rotate against the z axis (upward facing). This changes azimuth.
  const SphericalAngle kZrotatedSphericalAngle =
      kSphericalAngle.Rotate(kRotation);

  // Check if the rotated spherical angle is correct. Positive rotation around
  // Z (up) moves azimuth in the positive direction (leftward rotation).
  EXPECT_NEAR((kAzimuth + kRotationAngle), kZrotatedSphericalAngle.azimuth(),
              kEpsilonFloat);
  EXPECT_NEAR(kElevation, kZrotatedSphericalAngle.elevation(), kEpsilonFloat);
}

}  // namespace

}  // namespace obr
