#include "gworld-scene-camera-private.h"

#include <glib.h>

#include <cmath>

namespace {

void
assert_near(double actual, double expected, double tolerance)
{
  g_assert_cmpfloat_with_epsilon(actual, expected, tolerance);
}

double
horizontal_distance_from_focus(const gworld_scene::CameraPose &pose,
                               const gworld_scene::LocalFrame &frame)
{
  const glm::dvec3 offset = pose.eye - frame.origin;
  return glm::length(offset - frame.up * glm::dot(offset, frame.up));
}

void
test_local_frame_axes()
{
  const gworld_scene::LocalFrame frame =
    gworld_scene::local_frame_at(-35.0024, 147.4648, -35.0024, 147.4648);

  assert_near(glm::length(frame.east), 1.0, 0.001);
  assert_near(glm::length(frame.north), 1.0, 0.001);
  assert_near(glm::length(frame.up), 1.0, 0.001);
  assert_near(glm::dot(frame.east, frame.north), 0.0, 0.001);
  assert_near(glm::dot(frame.east, frame.up), 0.0, 0.001);
  assert_near(glm::dot(frame.north, frame.up), 0.0, 0.001);
}

void
test_orbit_blend_steps()
{
  assert_near(gworld_scene::orbit_camera_blend_for_altitude(10000.0), 0.0, 0.001);
  assert_near(gworld_scene::orbit_camera_blend_for_altitude(35000.0), 0.0, 0.001);
  g_assert_cmpfloat(gworld_scene::orbit_camera_blend_for_altitude(60000.0), >, 0.0);
  g_assert_cmpfloat(gworld_scene::orbit_camera_blend_for_altitude(60000.0), <, 1.0);
  assert_near(gworld_scene::orbit_camera_blend_for_altitude(90000.0), 1.0, 0.001);
}

void
test_heading_and_pitch_normalization()
{
  assert_near(gworld_scene::normalize_heading_degrees(-10.0), 350.0, 0.001);
  assert_near(gworld_scene::normalize_heading_degrees(730.0), 10.0, 0.001);
  assert_near(gworld_scene::clamp_camera_pitch(-120.0), -89.0, 0.001);
  assert_near(gworld_scene::clamp_camera_pitch(120.0), 89.0, 0.001);
}

void
test_nadir_blend_steps()
{
  assert_near(gworld_scene::orbit_camera_nadir_blend_for_altitude(250000.0), 0.0, 0.001);
  assert_near(gworld_scene::orbit_camera_nadir_blend_for_altitude(300000.0), 0.0, 0.001);
  g_assert_cmpfloat(gworld_scene::orbit_camera_nadir_blend_for_altitude(650000.0), >, 0.0);
  g_assert_cmpfloat(gworld_scene::orbit_camera_nadir_blend_for_altitude(650000.0), <, 1.0);
  assert_near(gworld_scene::orbit_camera_nadir_blend_for_altitude(1000000.0), 1.0, 0.001);
}

void
test_movement_is_heading_relative_at_low_altitude()
{
  const glm::dvec2 direction =
    gworld_scene::camera_movement_direction_for_input(10000.0, 90.0, true, false, false, false);

  assert_near(direction.x, 1.0, 0.001);
  assert_near(direction.y, 0.0, 0.001);
}

void
test_movement_is_cardinal_at_orbit_altitude()
{
  const glm::dvec2 north =
    gworld_scene::camera_movement_direction_for_input(250000.0, 90.0, true, false, false, false);
  const glm::dvec2 east =
    gworld_scene::camera_movement_direction_for_input(250000.0, 180.0, false, false, false, true);
  const glm::dvec2 northeast =
    gworld_scene::camera_movement_direction_for_input(250000.0, 270.0, true, false, false, true);

  assert_near(north.x, 0.0, 0.001);
  assert_near(north.y, 1.0, 0.001);
  assert_near(east.x, 1.0, 0.001);
  assert_near(east.y, 0.0, 0.001);
  assert_near(glm::length(northeast), 1.0, 0.001);
  assert_near(northeast.x, std::sqrt(0.5), 0.001);
  assert_near(northeast.y, std::sqrt(0.5), 0.001);
}

void
test_movement_transition_stays_normalized()
{
  const glm::dvec2 direction =
    gworld_scene::camera_movement_direction_for_input(60000.0, 180.0, true, false, false, false);

  assert_near(glm::length(direction), 1.0, 0.001);
}

void
test_orbit_camera_keeps_geodetic_altitude()
{
  const double latitude = -35.0024;
  const double longitude = 147.4648;
  const double altitude = 250000.0;
  const gworld_scene::LocalFrame frame =
    gworld_scene::local_frame_at(latitude, longitude, latitude, longitude);
  const gworld_scene::CameraPose pose =
    gworld_scene::orbit_camera_pose(latitude,
                                    longitude,
                                    altitude,
                                    35.0,
                                    -68.0,
                                    latitude,
                                    longitude);

  const glm::dvec3 offset = pose.eye - frame.origin;
  assert_near(glm::dot(offset, frame.up), altitude, 0.01);
  g_assert_cmpfloat(glm::length(offset - frame.up * altitude), >, 1000.0);
  assert_near(glm::length(pose.up), 1.0, 0.001);
}

void
test_orbit_camera_zoom_out_lifts_over_focus()
{
  const double latitude = -35.0024;
  const double longitude = 147.4648;
  const gworld_scene::LocalFrame frame =
    gworld_scene::local_frame_at(latitude, longitude, latitude, longitude);
  const gworld_scene::CameraPose low =
    gworld_scene::orbit_camera_pose(latitude, longitude, 90000.0, 35.0, -26.0, latitude, longitude);
  const gworld_scene::CameraPose mid =
    gworld_scene::orbit_camera_pose(latitude, longitude, 250000.0, 35.0, -26.0, latitude, longitude);
  const gworld_scene::CameraPose nadir =
    gworld_scene::orbit_camera_pose(latitude, longitude, 650000.0, 35.0, -26.0, latitude, longitude);

  const double low_horizontal = horizontal_distance_from_focus(low, frame);
  const double mid_horizontal = horizontal_distance_from_focus(mid, frame);
  const double nadir_horizontal = horizontal_distance_from_focus(nadir, frame);

  g_assert_cmpfloat(glm::dot(mid.eye - frame.origin, frame.up), >,
                    glm::dot(low.eye - frame.origin, frame.up));
  assert_near(mid_horizontal, low_horizontal, 1.0);
  g_assert_cmpfloat(nadir_horizontal, <, mid_horizontal);
}

void
test_far_orbit_camera_pulls_toward_nadir()
{
  const double latitude = -35.0024;
  const double longitude = 147.4648;
  const double altitude = 1000000.0;
  const gworld_scene::LocalFrame frame =
    gworld_scene::local_frame_at(latitude, longitude, latitude, longitude);
  const gworld_scene::CameraPose pose =
    gworld_scene::orbit_camera_pose(latitude,
                                    longitude,
                                    altitude,
                                    35.0,
                                    -35.0,
                                    latitude,
                                    longitude);
  const glm::dvec3 offset = pose.eye - frame.origin;
  const double horizontal_distance = horizontal_distance_from_focus(pose, frame);

  assert_near(glm::dot(offset, frame.up), altitude, 0.01);
  g_assert_cmpfloat(horizontal_distance / altitude, <, 0.04);
}

void
test_blended_camera_matches_orbit_at_high_altitude()
{
  const double latitude = -35.0024;
  const double longitude = 147.4648;
  const gworld_scene::CameraPose orbit =
    gworld_scene::orbit_camera_pose(latitude, longitude, 250000.0, 35.0, -68.0, latitude, longitude);
  const gworld_scene::CameraPose blended =
    gworld_scene::blended_camera_pose(latitude, longitude, 250000.0, 35.0, -68.0, latitude, longitude);

  assert_near(glm::length(blended.eye - orbit.eye), 0.0, 0.001);
  assert_near(glm::length(blended.center - orbit.center), 0.0, 0.001);
}

void
test_camera_pose_mode_selects_free_or_default()
{
  const double latitude = -35.0024;
  const double longitude = 147.4648;
  const double altitude = 250000.0;
  const gworld_scene::CameraPose free_pose =
    gworld_scene::camera_pose_for_mode(gworld_scene::CameraMode::Free,
                                       latitude,
                                       longitude,
                                       altitude,
                                       35.0,
                                       -32.0,
                                       latitude,
                                       longitude);
  const gworld_scene::CameraPose local =
    gworld_scene::local_camera_pose(latitude, longitude, altitude, 35.0, -32.0, latitude, longitude);
  const gworld_scene::CameraPose default_pose =
    gworld_scene::camera_pose_for_mode(gworld_scene::CameraMode::Default,
                                       latitude,
                                       longitude,
                                       altitude,
                                       35.0,
                                       -32.0,
                                       latitude,
                                       longitude);
  const gworld_scene::CameraPose blended =
    gworld_scene::blended_camera_pose(latitude, longitude, altitude, 35.0, -32.0, latitude, longitude);

  assert_near(glm::length(free_pose.eye - local.eye), 0.0, 0.001);
  assert_near(glm::length(free_pose.center - local.center), 0.0, 0.001);
  assert_near(glm::length(default_pose.eye - blended.eye), 0.0, 0.001);
  assert_near(glm::length(default_pose.center - blended.center), 0.0, 0.001);
}

void
test_camera_orientation_for_scene_target()
{
  const double latitude = -35.0024;
  const double longitude = 147.4648;
  const double altitude = 1200.0;
  const gworld_scene::LocalFrame frame =
    gworld_scene::local_frame_at(latitude, longitude, latitude, longitude);
  const glm::dvec3 eye = frame.origin + frame.up * altitude;

  const gworld_scene::CameraOrientation north =
    gworld_scene::camera_orientation_for_scene_target(latitude,
                                                      longitude,
                                                      altitude,
                                                      eye + frame.north * 2000.0,
                                                      latitude,
                                                      longitude);
  const gworld_scene::CameraOrientation east =
    gworld_scene::camera_orientation_for_scene_target(latitude,
                                                      longitude,
                                                      altitude,
                                                      eye + frame.east * 2000.0,
                                                      latitude,
                                                      longitude);
  const gworld_scene::CameraOrientation up =
    gworld_scene::camera_orientation_for_scene_target(latitude,
                                                    longitude,
                                                    altitude,
                                                    eye + frame.up * 2000.0,
                                                    latitude,
                                                    longitude);

  assert_near(north.heading_deg, 0.0, 0.001);
  assert_near(north.pitch_deg, 0.0, 0.001);
  assert_near(east.heading_deg, 90.0, 0.001);
  assert_near(east.pitch_deg, 0.0, 0.001);
  assert_near(up.pitch_deg, 89.0, 0.001);
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/camera/local-frame-axes", test_local_frame_axes);
  g_test_add_func("/camera/orbit-blend-steps", test_orbit_blend_steps);
  g_test_add_func("/camera/heading-and-pitch-normalization", test_heading_and_pitch_normalization);
  g_test_add_func("/camera/nadir-blend-steps", test_nadir_blend_steps);
  g_test_add_func("/camera/movement-is-heading-relative-at-low-altitude", test_movement_is_heading_relative_at_low_altitude);
  g_test_add_func("/camera/movement-is-cardinal-at-orbit-altitude", test_movement_is_cardinal_at_orbit_altitude);
  g_test_add_func("/camera/movement-transition-stays-normalized", test_movement_transition_stays_normalized);
  g_test_add_func("/camera/orbit-camera-keeps-geodetic-altitude", test_orbit_camera_keeps_geodetic_altitude);
  g_test_add_func("/camera/orbit-camera-zoom-out-lifts-over-focus", test_orbit_camera_zoom_out_lifts_over_focus);
  g_test_add_func("/camera/far-orbit-camera-pulls-toward-nadir", test_far_orbit_camera_pulls_toward_nadir);
  g_test_add_func("/camera/blended-camera-matches-orbit-at-high-altitude", test_blended_camera_matches_orbit_at_high_altitude);
  g_test_add_func("/camera/camera-pose-mode-selects-free-or-default", test_camera_pose_mode_selects_free_or_default);
  g_test_add_func("/camera/camera-orientation-for-scene-target", test_camera_orientation_for_scene_target);
  return g_test_run();
}
