#ifndef GWORLD_SCENE_CAMERA_PRIVATE_H
#define GWORLD_SCENE_CAMERA_PRIVATE_H

#include <glm/glm.hpp>

namespace gworld_scene {

struct LocalFrame {
  glm::dvec3 origin;
  glm::dvec3 east;
  glm::dvec3 north;
  glm::dvec3 up;
};

struct CameraPose {
  glm::dvec3 eye;
  glm::dvec3 center;
  glm::dvec3 up;
};

struct CameraOrientation {
  double heading_deg = 0.0;
  double pitch_deg = 0.0;
};

enum class CameraMode {
  Default,
  Free,
};

LocalFrame local_frame_at(double latitude,
                          double longitude,
                          double origin_latitude,
                          double origin_longitude);

double normalize_heading_degrees(double degrees);

double clamp_camera_pitch(double pitch_deg);

double orbit_camera_blend_for_altitude(double altitude_amsl);

double orbit_camera_nadir_blend_for_altitude(double altitude_amsl);

glm::dvec2 camera_movement_direction_for_input(double altitude_amsl,
                                               double heading_deg,
                                               bool forward,
                                               bool backward,
                                               bool left,
                                               bool right);

CameraPose local_camera_pose(double latitude,
                             double longitude,
                             double altitude_amsl,
                             double heading_deg,
                             double pitch_deg,
                             double origin_latitude,
                             double origin_longitude);

CameraPose orbit_camera_pose(double latitude,
                             double longitude,
                             double altitude_amsl,
                             double heading_deg,
                             double pitch_deg,
                             double origin_latitude,
                             double origin_longitude);

CameraPose blended_camera_pose(double latitude,
                               double longitude,
                               double altitude_amsl,
                               double heading_deg,
                               double pitch_deg,
                               double origin_latitude,
                               double origin_longitude);

CameraPose camera_pose_for_mode(CameraMode mode,
                                double latitude,
                                double longitude,
                                double altitude_amsl,
                                double heading_deg,
                                double pitch_deg,
                                double origin_latitude,
                                double origin_longitude);

CameraOrientation camera_orientation_for_scene_target(double camera_latitude,
                                                      double camera_longitude,
                                                      double camera_altitude_amsl,
                                                      const glm::dvec3 &target,
                                                      double origin_latitude,
                                                      double origin_longitude);

CameraOrientation camera_orientation_for_geodetic_target(double camera_latitude,
                                                         double camera_longitude,
                                                         double camera_altitude_amsl,
                                                         double target_latitude,
                                                         double target_longitude,
                                                         double target_altitude_amsl,
                                                         double origin_latitude,
                                                         double origin_longitude);

} // namespace gworld_scene

#endif /* GWORLD_SCENE_CAMERA_PRIVATE_H */
