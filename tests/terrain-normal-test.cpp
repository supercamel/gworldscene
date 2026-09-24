#include "gworld-scene-terrain-private.h"
#include "gworld-scene-camera-private.h"
#include "gworld-scene-geo-private.h"
#include <glib.h>
#include <cmath>
#include <functional>

using namespace gworld_scene;

namespace {
std::shared_ptr<TerrainTile> make_tile(int lat, int lon, int size,
                                      const std::function<double(double, double)> &height)
{
  auto tile = std::make_shared<TerrainTile>();
  tile->lat = lat;
  tile->lon = lon;
  tile->dimension = size;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
      tile->heights.push_back(static_cast<int16_t>(std::lround(height(
        lat + 1.0 - static_cast<double>(y) / (size - 1),
        lon + static_cast<double>(x) / (size - 1)))));
  return tile;
}

void assert_normal(glm::vec3 actual, glm::dvec3 expected, double tolerance = 1e-5)
{
  g_assert_cmpfloat_with_epsilon(glm::length(actual), 1.0, 1e-6);
  g_assert_cmpfloat(glm::length(glm::dvec3(actual) - expected), <, tolerance);
}

void test_flat_and_fallback()
{
  auto tile = make_tile(62, 147, 17, [](double, double) { return 250.0; });
  TerrainTileMap tiles{{terrain_key(62, 147), tile}};
  for (int y : {0, 8, 16}) {
    for (int x : {0, 8, 16}) {
      const auto up = local_frame_at(63.0 - y / 16.0, 147.0 + x / 16.0, 61.0, 146.0).up;
      assert_normal(terrain_normal_at(tiles, *tile, x, y, 61, 146), up);
    }
  }
  auto missing = make_tile(62, 146, 17, [](double, double) { return 0.0; });
  assert_normal(terrain_normal_at(tiles, *missing, 16, 8, 61, 146),
                local_frame_at(62.5, 147, 61, 146).up);
}

void test_slope_and_missing_neighbours()
{
  // A known plane: 16000 metres/degree east and 8000 metres/degree north.
  auto tile = make_tile(0, 0, 65, [](double lat, double lon) { return 16000 * lon + 8000 * lat; });
  TerrainTileMap tiles{{terrain_key(0, 0), tile}};
  for (int y : {0, 32, 64}) {
    for (int x : {0, 32, 64}) {
      const double lat = 1.0 - y / 64.0, lon = x / 64.0;
      const auto frame = local_frame_at(lat, lon, 0.5, 0.5);
      // Independent metric derivative from displaced geodetic positions.
      const double east_m = glm::length(geodetic_to_ecef(lat, lon + 0.00001, 0) - geodetic_to_ecef(lat, lon, 0)) / 0.00001;
      const double north_m = glm::length(geodetic_to_ecef(lat + 0.00001, lon, 0) - geodetic_to_ecef(lat, lon, 0)) / 0.00001;
      assert_normal(terrain_normal_at(tiles, *tile, x, y, 0.5, 0.5),
                    glm::normalize(frame.up - 16000.0 / east_m * frame.east - 8000.0 / north_m * frame.north));
    }
  }
}

void test_shared_edges_and_dateline()
{
  for (int longitude : {0, 179}) {
    const int next = static_cast<int>(wrap_longitude(longitude + 1));
    const auto height = [=](double lat, double lon) {
      const double offset = wrap_longitude(lon - longitude);
      return 2000 * offset * offset + 1000 * lat * lat;
    };
    // Different resolutions must still agree at their shared endpoints.
    auto west = make_tile(0, longitude, 33, height);
    auto east = make_tile(0, next, 65, height);
    auto north = make_tile(1, longitude, 65, height);
    TerrainTileMap tiles{{terrain_key(0, longitude), west}, {terrain_key(0, next), east},
                         {terrain_key(1, longitude), north}};
    for (int y : {0, 8, 16, 24, 32})
      assert_normal(terrain_normal_at(tiles, *west, 32, y, 0.5, longitude),
                    terrain_normal_at(tiles, *east, 0, y * 2, 0.5, longitude), 1e-6);
    for (int x : {0, 8, 16, 24, 32})
      assert_normal(terrain_normal_at(tiles, *west, x, 0, 0.5, longitude),
                    terrain_normal_at(tiles, *north, x * 2, 64, 0.5, longitude), 1e-6);
  }
}

void test_height_edges_and_voids()
{
  auto tile = make_tile(-1, 179, 17, [](double, double) { return 123.0; });
  tile->heights[8 * 17 + 8] = -32768;
  TerrainTileMap tiles{{terrain_key(-1, 179), tile}};
  double height = 0;
  g_assert_true(terrain_height_at(tiles, 0, -180, height));
  g_assert_cmpfloat(height, ==, 123);
  g_assert_true(terrain_height_at(tiles, -0.5, 179.5, height));
  g_assert_cmpfloat(height, ==, 123);
  assert_normal(terrain_normal_at(tiles, *tile, 8, 8, -0.5, 179.5), {0, 1, 0});
  g_assert_false(terrain_height_at(tiles, -0.5, -179.5, height));
}
}

int main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/terrain-normals/flat-and-fallback", test_flat_and_fallback);
  g_test_add_func("/terrain-normals/slopes-and-missing-neighbours", test_slope_and_missing_neighbours);
  g_test_add_func("/terrain-normals/shared-edges-and-dateline", test_shared_edges_and_dateline);
  g_test_add_func("/terrain-normals/height-edges-and-voids", test_height_edges_and_voids);
  return g_test_run();
}
