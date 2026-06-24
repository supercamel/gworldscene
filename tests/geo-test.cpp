#include "gworld-scene-geo-private.h"

#include <glib.h>

#include <cmath>

namespace {

void
assert_near(double actual, double expected, double tolerance)
{
  g_assert_cmpfloat_with_epsilon(actual, expected, tolerance);
}

void
test_same_position_is_origin()
{
  const glm::dvec3 p = gworld_scene::geodetic_to_scene(-35.0024,
                                                       147.4648,
                                                       1200.0,
                                                       -35.0024,
                                                       147.4648,
                                                       1200.0);
  assert_near(p.x, 0.0, 0.001);
  assert_near(p.y, 0.0, 0.001);
  assert_near(p.z, 0.0, 0.001);
}

void
test_lat_lon_map_to_scene_axes()
{
  const double lat = -35.0024;
  const double lon = 147.4648;
  const double north_lat = lat + 1000.0 / gworld_scene::kEarthMetersPerDegree;
  const double east_lon = lon + 1000.0 / (gworld_scene::kEarthMetersPerDegree * std::cos(gworld_scene::deg_to_rad(lat)));

  const glm::dvec3 north = gworld_scene::geodetic_to_scene(north_lat, lon, 0.0, lat, lon, 0.0);
  assert_near(north.x, 0.0, 5.0);
  assert_near(north.z, -1000.0, 20.0);

  const glm::dvec3 east = gworld_scene::geodetic_to_scene(lat, east_lon, 0.0, lat, lon, 0.0);
  assert_near(east.x, 1000.0, 20.0);
  assert_near(east.z, 0.0, 5.0);

  const glm::dvec3 up = gworld_scene::geodetic_to_scene(lat, lon, 250.0, lat, lon, 0.0);
  assert_near(up.y, 250.0, 0.001);
}

void
test_ned_maps_to_scene_axes()
{
  const glm::dvec3 scene = gworld_scene::ned_to_scene_vector(glm::dvec3(10.0, 20.0, 30.0));
  assert_near(scene.x, 20.0, 0.001);
  assert_near(scene.y, -30.0, 0.001);
  assert_near(scene.z, -10.0, 0.001);
}

void
test_ned_translation_round_trips_to_scene_offset()
{
  const double lat = -35.0024;
  const double lon = 147.4648;
  const double alt = 1200.0;
  double translated_lat = 0.0;
  double translated_lon = 0.0;
  double translated_alt = 0.0;
  gworld_scene::translate_geodetic_ned(lat,
                                       lon,
                                       alt,
                                       1000.0,
                                       500.0,
                                       100.0,
                                       &translated_lat,
                                       &translated_lon,
                                       &translated_alt);

  const glm::dvec3 offset = gworld_scene::geodetic_to_scene(translated_lat,
                                                            translated_lon,
                                                            translated_alt,
                                                            lat,
                                                            lon,
                                                            alt);
  assert_near(offset.x, 500.0, 20.0);
  assert_near(offset.y, -100.0, 1.0);
  assert_near(offset.z, -1000.0, 20.0);
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/geo/same-position-is-origin", test_same_position_is_origin);
  g_test_add_func("/geo/lat-lon-map-to-scene-axes", test_lat_lon_map_to_scene_axes);
  g_test_add_func("/geo/ned-maps-to-scene-axes", test_ned_maps_to_scene_axes);
  g_test_add_func("/geo/ned-translation-round-trips-to-scene-offset", test_ned_translation_round_trips_to_scene_offset);
  return g_test_run();
}
