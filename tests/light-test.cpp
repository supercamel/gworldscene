#include "gworld-scene-light-private.h"

#include <glib.h>

#include <cmath>

namespace {

void
assert_near(double actual, double expected, double tolerance)
{
  g_assert_cmpfloat_with_epsilon(actual, expected, tolerance);
}

void
test_sun_position_direction_round_trip()
{
  const glm::dvec3 direction = gworld_scene::sun_direction_from_position(90.0, 30.0);
  const gworld_scene::SunPosition position = gworld_scene::sun_position_from_direction(direction);

  assert_near(glm::length(direction), 1.0, 0.001);
  assert_near(direction.x, std::cos(glm::radians(30.0)), 0.001);
  assert_near(direction.y, 0.0, 0.001);
  assert_near(direction.z, 0.5, 0.001);
  assert_near(position.azimuth_deg, 90.0, 0.001);
  assert_near(position.elevation_deg, 30.0, 0.001);
}

void
test_sun_time_moves_east_to_west()
{
  const glm::dvec3 sunrise = gworld_scene::sun_direction_from_time(0.0, 6.0);
  const glm::dvec3 noon = gworld_scene::sun_direction_from_time(0.0, 12.0);
  const glm::dvec3 sunset = gworld_scene::sun_direction_from_time(0.0, 18.0);

  g_assert_cmpfloat(sunrise.x, >, 0.9);
  assert_near(sunrise.z, 0.0, 0.001);
  assert_near(noon.z, 1.0, 0.001);
  g_assert_cmpfloat(sunset.x, <, -0.9);
  assert_near(sunset.z, 0.0, 0.001);
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/light/sun-position-direction-round-trip", test_sun_position_direction_round_trip);
  g_test_add_func("/light/sun-time-moves-east-to-west", test_sun_time_moves_east_to_west);
  return g_test_run();
}
