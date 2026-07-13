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

#ifndef OBR_AMBISONIC_ROTATOR_TESTS_SPHERICAL_ANGLE_H_
#define OBR_AMBISONIC_ROTATOR_TESTS_SPHERICAL_ANGLE_H_

#include "obr/common/misc_math.h"

// This code is forked from Resonance Audio's `spherical_angle.h`.
namespace obr {

// Represents angular position on a sphere in terms of azimuth and elevation.
class SphericalAngle {
 public:
  // Constructs a spherical angle with the given azimuth and elevation.
  SphericalAngle(float azimuth, float elevation);

  // Constructs a default spherical angle (azimuth = 0, elevation = 0).
  SphericalAngle();

  // Constructs a spherical angle from a given one.
  SphericalAngle(const SphericalAngle& other);

  SphericalAngle& operator=(const SphericalAngle& other);

  /*!\brief Returns a spherical angle representation of given `world_position`
   * (World Space).
   *
   * \param world_position 3D position in world space.
   * \return Spherical angle that represents the `world_position`.
   */
  static SphericalAngle FromWorldPosition(const WorldPosition& world_position);

  // Returns a spherical angle from azimuth and elevation in degrees.
  static SphericalAngle FromDegrees(float azimuth_degrees,
                                    float elevation_degrees);

  /*!\brief Returns another spherical angle with the same elevation but the
   * azimuth sign flipped.
   *
   * \return Horizontally flipped version of the spherical angle.
   */
  SphericalAngle FlipAzimuth() const;

  /*!\brief Returns the `WorldPosition` coordinates (World Space) on the unit
   * sphere corresponding to this spherical angle. Uses ADM coordinates where
   * X=right, Y=forward, Z=up. Azimuth 0=forward, positive=left; elevation
   * 0=horizontal, positive=up. The transformation is defined as:
   * x = -cos(elevation) * sin(azimuth)
   * y = cos(elevation) * cos(azimuth)
   * z = sin(elevation)
   *
   * \return 3D position in world space.
   */
  WorldPosition GetWorldPositionOnUnitSphere() const;

  /*!\brief Returns the rotated version of the spherical angle using given
   * `WorldRotation`.
   *
   *  \param rotation Rotation to be applied to the spherical angle.
   *  \return Rotated version of the spherical angle.
   */
  SphericalAngle Rotate(const WorldRotation& rotation) const;

  void set_azimuth(float azimuth) { azimuth_ = azimuth; }

  void set_elevation(float elevation) { elevation_ = elevation; }

  float azimuth() const { return azimuth_; }

  float elevation() const { return elevation_; }

  bool operator==(const SphericalAngle& other) const;

 private:
  float azimuth_;
  float elevation_;
};

}  // namespace obr

#endif  // OBR_AMBISONIC_ROTATOR_TESTS_SPHERICAL_ANGLE_H_
