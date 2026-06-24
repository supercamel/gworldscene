#include "gworld-scene-camera-private.h"

#include "gworld-scene-geo-private.h"

#include <algorithm>
#include <cmath>

namespace gworld_scene {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kOrbitCameraStartAltitudeM = 35000.0;
constexpr double kOrbitCameraFullAltitudeM = 90000.0;
constexpr double kOrbitHorizontalAnchorAltitudeM = kOrbitCameraFullAltitudeM;
constexpr double kNadirPitchStartAltitudeM = 300000.0;
constexpr double kNadirPitchFullAltitudeM = 1000000.0;
constexpr double kNadirPitchDeg = -88.5;

glm::dvec3
safe_normalize(const glm::dvec3 &value, const glm::dvec3 &fallback)
{
  const double length = glm::length(value);
  if (length <= 0.000001)
    return fallback;
  return value / length;
}

glm::dvec2
safe_normalize(const glm::dvec2 &value, const glm::dvec2 &fallback)
{
  const double length = glm::length(value);
  if (length <= 0.000001)
    return fallback;
  return value / length;
}

double
smoothstep(double edge0, double edge1, double value)
{
  const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

double
orbit_horizontal_distance(double altitude_amsl,
                          double requested_pitch_deg,
                          double effective_pitch_deg,
                          double nadir_blend)
{
  const double requested_elevation_rad = deg_to_rad(std::clamp(-requested_pitch_deg, 5.0, 89.0));
  const double effective_elevation_rad = deg_to_rad(std::clamp(-effective_pitch_deg, 5.0, 89.0));
  const double distance_for_current_altitude = altitude_amsl / std::tan(effective_elevation_rad);
  const double anchored_distance = kOrbitHorizontalAnchorAltitudeM / std::tan(requested_elevation_rad);
  return std::min(distance_for_current_altitude, anchored_distance) * (1.0 - nadir_blend);
}

glm::dvec2
blend_directions(const glm::dvec2 &from, const glm::dvec2 &to, double blend)
{
  const double from_length = glm::length(from);
  const double to_length = glm::length(to);
  if (from_length <= 0.000001)
    return safe_normalize(to, glm::dvec2(0.0, 0.0));
  if (to_length <= 0.000001)
    return safe_normalize(from, glm::dvec2(0.0, 0.0));

  const glm::dvec2 a = from / from_length;
  const glm::dvec2 b = to / to_length;
  const double from_angle = std::atan2(a.x, a.y);
  const double to_angle = std::atan2(b.x, b.y);
  double delta = std::fmod(to_angle - from_angle + kPi, kPi * 2.0);
  if (delta < 0.0)
    delta += kPi * 2.0;
  delta -= kPi;

  const double angle = from_angle + delta * blend;
  return glm::dvec2(std::sin(angle), std::cos(angle));
}

} // namespace

LocalFrame
local_frame_at(double latitude,
               double longitude,
               double origin_latitude,
               double origin_longitude)
{
  LocalFrame frame;
  frame.origin = geodetic_to_scene(latitude,
                                   longitude,
                                   0.0,
                                   origin_latitude,
                                   origin_longitude,
                                   0.0);
  frame.up = safe_normalize(geodetic_to_scene(latitude,
                                              longitude,
                                              1000.0,
                                              origin_latitude,
                                              origin_longitude,
                                              0.0) -
                            frame.origin,
                            glm::dvec3(0.0, 1.0, 0.0));

  const double north_sample_latitude = std::clamp(latitude + 0.0001, -89.9999, 89.9999);
  glm::dvec3 north = geodetic_to_scene(north_sample_latitude,
                                       longitude,
                                       0.0,
                                       origin_latitude,
                                       origin_longitude,
                                       0.0) -
                     frame.origin;
  north -= frame.up * glm::dot(north, frame.up);
  frame.north = safe_normalize(north, glm::dvec3(0.0, 0.0, -1.0));
  frame.east = safe_normalize(glm::cross(frame.north, frame.up), glm::dvec3(1.0, 0.0, 0.0));
  frame.north = safe_normalize(glm::cross(frame.up, frame.east), frame.north);
  return frame;
}

double
orbit_camera_blend_for_altitude(double altitude_amsl)
{
  return smoothstep(kOrbitCameraStartAltitudeM, kOrbitCameraFullAltitudeM, altitude_amsl);
}

double
orbit_camera_nadir_blend_for_altitude(double altitude_amsl)
{
  return smoothstep(kNadirPitchStartAltitudeM, kNadirPitchFullAltitudeM, altitude_amsl);
}

glm::dvec2
camera_movement_direction_for_input(double altitude_amsl,
                                    double heading_deg,
                                    bool forward,
                                    bool backward,
                                    bool left,
                                    bool right)
{
  const double heading_rad = deg_to_rad(heading_deg);
  const glm::dvec2 heading_forward(std::sin(heading_rad), std::cos(heading_rad));
  const glm::dvec2 heading_right(std::cos(heading_rad), -std::sin(heading_rad));

  glm::dvec2 heading_relative(0.0, 0.0);
  glm::dvec2 cardinal(0.0, 0.0);

  if (forward) {
    heading_relative += heading_forward;
    cardinal.y += 1.0;
  }
  if (backward) {
    heading_relative -= heading_forward;
    cardinal.y -= 1.0;
  }
  if (right) {
    heading_relative += heading_right;
    cardinal.x += 1.0;
  }
  if (left) {
    heading_relative -= heading_right;
    cardinal.x -= 1.0;
  }

  const double blend = orbit_camera_blend_for_altitude(altitude_amsl);
  return blend_directions(heading_relative, cardinal, blend);
}

CameraPose
local_camera_pose(double latitude,
                  double longitude,
                  double altitude_amsl,
                  double heading_deg,
                  double pitch_deg,
                  double origin_latitude,
                  double origin_longitude)
{
  const LocalFrame frame = local_frame_at(latitude, longitude, origin_latitude, origin_longitude);
  const double heading_rad = deg_to_rad(heading_deg);
  const double pitch_rad = deg_to_rad(pitch_deg);
  const double cos_pitch = std::cos(pitch_rad);
  const glm::dvec3 forward =
    safe_normalize(frame.east * (std::sin(heading_rad) * cos_pitch) +
                   frame.up * std::sin(pitch_rad) +
                   frame.north * (std::cos(heading_rad) * cos_pitch),
                   -frame.up);

  CameraPose pose;
  pose.eye = geodetic_to_scene(latitude,
                               longitude,
                               altitude_amsl,
                               origin_latitude,
                               origin_longitude,
                               0.0);
  pose.center = pose.eye + forward * std::max(altitude_amsl * 2.5, 1000.0);
  pose.up = frame.up;
  if (std::abs(glm::dot(forward, pose.up)) > 0.96)
    pose.up = frame.north;
  return pose;
}

CameraPose
orbit_camera_pose(double latitude,
                  double longitude,
                  double altitude_amsl,
                  double heading_deg,
                  double pitch_deg,
                  double origin_latitude,
                  double origin_longitude)
{
  const LocalFrame frame = local_frame_at(latitude, longitude, origin_latitude, origin_longitude);
  const double heading_rad = deg_to_rad(heading_deg);
  const double nadir_blend = orbit_camera_nadir_blend_for_altitude(altitude_amsl);
  const double effective_pitch = pitch_deg * (1.0 - nadir_blend) + kNadirPitchDeg * nadir_blend;
  const double horizontal_distance =
    orbit_horizontal_distance(altitude_amsl, pitch_deg, effective_pitch, nadir_blend);
  const glm::dvec3 ground_direction =
    safe_normalize(frame.east * std::sin(heading_rad) +
                   frame.north * std::cos(heading_rad),
                   frame.north);

  CameraPose pose;
  pose.eye = frame.origin + frame.up * altitude_amsl - ground_direction * horizontal_distance;
  pose.center = frame.origin;
  const glm::dvec3 forward = safe_normalize(pose.center - pose.eye, -frame.up);
  pose.up = std::abs(glm::dot(forward, frame.up)) > 0.96 ? frame.north : frame.up;
  return pose;
}

CameraPose
blended_camera_pose(double latitude,
                    double longitude,
                    double altitude_amsl,
                    double heading_deg,
                    double pitch_deg,
                    double origin_latitude,
                    double origin_longitude)
{
  const double blend = orbit_camera_blend_for_altitude(altitude_amsl);
  if (blend <= 0.0) {
    return local_camera_pose(latitude,
                             longitude,
                             altitude_amsl,
                             heading_deg,
                             pitch_deg,
                             origin_latitude,
                             origin_longitude);
  }
  if (blend >= 1.0) {
    return orbit_camera_pose(latitude,
                             longitude,
                             altitude_amsl,
                             heading_deg,
                             pitch_deg,
                             origin_latitude,
                             origin_longitude);
  }

  const CameraPose local = local_camera_pose(latitude,
                                            longitude,
                                            altitude_amsl,
                                            heading_deg,
                                            pitch_deg,
                                            origin_latitude,
                                            origin_longitude);
  const CameraPose orbit = orbit_camera_pose(latitude,
                                            longitude,
                                            altitude_amsl,
                                            heading_deg,
                                            pitch_deg,
                                            origin_latitude,
                                            origin_longitude);

  CameraPose pose;
  pose.eye = glm::mix(local.eye, orbit.eye, blend);
  pose.center = glm::mix(local.center, orbit.center, blend);
  pose.up = safe_normalize(glm::mix(local.up, orbit.up, blend), orbit.up);
  return pose;
}

} // namespace gworld_scene
