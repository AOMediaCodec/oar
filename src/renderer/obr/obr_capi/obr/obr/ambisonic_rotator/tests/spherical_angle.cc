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

#include <cmath>

#include "obr/common/constants.h"
#include "obr/common/misc_math.h"

namespace obr {

SphericalAngle::SphericalAngle(float azimuth, float elevation)
    : azimuth_(azimuth), elevation_(elevation) {}

SphericalAngle::SphericalAngle() : SphericalAngle(0.0f, 0.0f) {}

SphericalAngle::SphericalAngle(const SphericalAngle& other)
    : azimuth_(other.azimuth_), elevation_(other.elevation_) {}

SphericalAngle& SphericalAngle::operator=(const SphericalAngle& other) {
  if (&other == this) {
    return *this;
  }
  this->azimuth_ = other.azimuth_;
  this->elevation_ = other.elevation_;
  return *this;
}

SphericalAngle SphericalAngle::FromWorldPosition(
    const WorldPosition& world_position) {
  // ADM coordinates: X=right, Y=forward, Z=up
  // Azimuth: 0=forward (+Y), positive=left (toward -X)
  // Elevation: 0=horizontal, positive=up (+Z)
  return SphericalAngle(
      std::atan2(-world_position[0], world_position[1]),
      std::atan2(world_position[2],
                 std::sqrt(world_position[0] * world_position[0] +
                           world_position[1] * world_position[1])));
}

SphericalAngle SphericalAngle::FromDegrees(float azimuth_degrees,
                                           float elevation_degrees) {
  return SphericalAngle(azimuth_degrees * kRadiansFromDegrees,
                        elevation_degrees * kRadiansFromDegrees);
}

SphericalAngle SphericalAngle::FlipAzimuth() const {
  return SphericalAngle(-azimuth_, elevation_);
}

WorldPosition SphericalAngle::GetWorldPositionOnUnitSphere() const {
  // ADM coordinates: X=right, Y=forward, Z=up
  // Azimuth: 0=forward (+Y), positive=left (toward -X)
  // Elevation: 0=horizontal, positive=up (+Z)
  return WorldPosition(-std::cos(elevation_) * std::sin(azimuth_),
                       std::cos(elevation_) * std::cos(azimuth_),
                       std::sin(elevation_));
}

SphericalAngle SphericalAngle::Rotate(const WorldRotation& rotation) const {
  const WorldPosition original_world_position = GetWorldPositionOnUnitSphere();
  const WorldPosition rotated_world_position =
      rotation * original_world_position;
  return FromWorldPosition(rotated_world_position);
}

bool SphericalAngle::operator==(const SphericalAngle& other) const {
  return (azimuth_ == other.azimuth_) && (elevation_ == other.elevation_);
}

}  // namespace obr
