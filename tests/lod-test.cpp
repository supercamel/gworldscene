#include "gworld-scene-lod-private.h"
#include "gworld-scene-geo-private.h"

#include <glib.h>
#include <algorithm>
#include <cmath>

namespace {

bool
range_contains(const gworld_scene::TileRange &range, double latitude, double longitude)
{
  const double x = gworld_scene::slippy_tile_x_for_longitude(longitude, range.z);
  const double y = gworld_scene::slippy_tile_y_for_latitude(latitude, range.z);
  return x >= range.x_min && x < range.x_max + 1 && y >= range.y_min && y < range.y_max + 1;
}

void
test_terrain_imagery_distance_steps()
{
  const double latitude = -16.8878, longitude = 145.7048;
  const auto plan = gworld_scene::terrain_imagery_plan(latitude, longitude, 100.0, 90000.0);
  const double meters_per_lon = gworld_scene::kEarthMetersPerDegree * std::cos(gworld_scene::deg_to_rad(latitude));
  // The previous selection fell from zoom 16 to zoom 12 a few km out.
  // Keep zoom 14 there, then step to zoom 12 tens of km away.
  const gworld_scene::ImageryBand bands[] = {plan.ultra, plan.detail, plan.mid, plan.far, plan.base};
  const double distances[] = {300.0, 1500.0, 6000.0, 25000.0, 80000.0};
  for (int i = 0; i < 5; ++i) {
    g_test_message("Imagery band %d: zoom=%d radius=%.0fm", i, bands[i].range.z, bands[i].radius_m);
    g_assert_cmpint(bands[i].range.z, ==, 18 - i * 2);
    g_assert_true(range_contains(bands[i].range, latitude, longitude + distances[i] / meters_per_lon));
    if (i > 0)
      g_assert_false(range_contains(bands[i - 1].range, latitude, longitude + distances[i] / meters_per_lon));
  }
}

void
test_terrain_imagery_capacity_and_altitude()
{
  for (double latitude : {-80.0, -16.8878, 0.0, 80.0}) {
    for (double longitude : {-180.0, 0.0, 179.999}) {
      for (double altitude : {20.0, 1000.0, 5000.0, 25000.0, 150000.0}) {
        for (int pixels : {256, 1024, 4096}) {
          const auto plan = gworld_scene::terrain_imagery_plan(latitude, longitude, altitude,
                                                               150000.0, 64, pixels, 64);
          for (const auto &band : {plan.ultra, plan.detail, plan.mid, plan.far, plan.base}) {
            if (!band.range.valid())
              continue;
            g_assert_cmpint(band.range.width_tiles() * band.range.height_tiles(), <=, 64);
            g_assert_cmpint(band.range.width_tiles() * 256, <=, pixels);
            g_assert_cmpint(band.range.height_tiles() * 256, <=, pixels);
          }
          g_assert_cmpint(plan.detail.range.z - plan.mid.range.z, ==, 2);
          g_assert_cmpint(plan.mid.range.z - plan.far.range.z, ==, 2);
          // Radius fitting uses a bounded binary search; allow centimetres of
          // rounding when adjacent levels meet the same one-tile boundary.
          g_assert_cmpfloat(plan.detail.radius_m, <=, plan.mid.radius_m + 0.02);
          g_assert_cmpfloat(plan.mid.radius_m, <=, plan.far.radius_m + 0.02);
          g_assert_cmpint(plan.ultra.range.valid(), ==, altitude < 1000.0);
          // The outer fallback must cover the full requested geographic extent.
          const double delta_lat = 150000.0 / gworld_scene::kEarthMetersPerDegree;
          const double delta_lon = delta_lat / std::max(0.05, std::cos(gworld_scene::deg_to_rad(latitude)));
          auto expected = gworld_scene::tile_range_for_bounds(latitude - delta_lat, latitude + delta_lat,
                                                               longitude - delta_lon, longitude + delta_lon,
                                                               plan.base.range.z);
          g_assert_true(expected.key() == plan.base.range.key());
        }
      }
    }
  }
  const auto normal = gworld_scene::terrain_imagery_plan(-16.8878, 145.7048, 100.0, 150000.0);
  const auto larger = gworld_scene::terrain_imagery_plan(-16.8878, 145.7048, 100.0, 150000.0,
                                                        1024, 8192, 1024, 2000.0);
  g_assert_cmpint(larger.detail.range.z, ==, normal.detail.range.z);
  g_assert_cmpfloat(larger.detail.radius_m, >, normal.detail.radius_m * 3.0);
  g_assert_cmpfloat(larger.mid.radius_m, >, normal.mid.radius_m * 3.0);
}

void
test_globe_texture_zoom_steps()
{
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(45000.0), ==, 8);
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(119999.0), ==, 8);
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(120000.0), ==, 7);
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(299999.0), ==, 7);
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(300000.0), ==, 6);
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(899999.0), ==, 6);
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(900000.0), ==, 5);
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(5999999.0), ==, 5);
  g_assert_cmpint(gworld_scene::globe_texture_zoom_for_altitude(6000000.0), ==, 4);
}

void
test_near_globe_ranges_are_bounded()
{
  const gworld_scene::TileRange near_range =
    gworld_scene::globe_texture_range_for_camera(-35.0024, 147.4648, 60000.0);
  g_assert_true(near_range.valid());
  g_assert_cmpint(near_range.z, ==, 8);
  g_assert_cmpint(near_range.width_tiles(), <=, 5);
  g_assert_cmpint(near_range.height_tiles(), <=, 5);
  g_assert_cmpint(near_range.width_tiles() * near_range.height_tiles(), <=, 25);

  const int center_x =
    static_cast<int>(gworld_scene::slippy_tile_x_for_longitude(147.4648, near_range.z));
  const int center_y =
    static_cast<int>(gworld_scene::slippy_tile_y_for_latitude(-35.0024, near_range.z));
  g_assert_cmpint(center_x, >=, near_range.x_min);
  g_assert_cmpint(center_x, <=, near_range.x_max);
  g_assert_cmpint(center_y, >=, near_range.y_min);
  g_assert_cmpint(center_y, <=, near_range.y_max);
}

void
test_far_globe_ranges_are_global()
{
  const gworld_scene::TileRange bounded_mid_range =
    gworld_scene::globe_texture_range_for_camera(-35.0024, 147.4648, 180000.0);
  g_assert_cmpint(bounded_mid_range.z, ==, 7);
  g_assert_cmpint(bounded_mid_range.width_tiles(), <=, 7);
  g_assert_cmpint(bounded_mid_range.height_tiles(), <=, 7);

  const gworld_scene::TileRange medium_range =
    gworld_scene::globe_texture_range_for_camera(-35.0024, 147.4648, 600000.0);
  g_assert_cmpint(medium_range.z, ==, 6);
  g_assert_cmpint(medium_range.width_tiles(), <=, 11);
  g_assert_cmpint(medium_range.height_tiles(), <=, 11);

  const gworld_scene::TileRange high_range =
    gworld_scene::globe_texture_range_for_camera(-35.0024, 147.4648, 1500000.0);
  g_assert_cmpint(high_range.z, ==, 5);
  g_assert_cmpint(high_range.width_tiles(), <=, 15);
  g_assert_cmpint(high_range.height_tiles(), <=, 15);

  const gworld_scene::TileRange far_range =
    gworld_scene::globe_texture_range_for_camera(-35.0024, 147.4648, 7000000.0);
  g_assert_cmpint(far_range.z, ==, 4);
  g_assert_cmpint(far_range.width_tiles(), ==, 16);
  g_assert_cmpint(far_range.height_tiles(), ==, 16);
}

void
test_globe_ranges_respect_texture_limits()
{
  for (double altitude : {60000.0, 180000.0, 600000.0, 1500000.0, 7000000.0}) {
    const auto original = gworld_scene::globe_texture_range_for_camera(0.0, 0.0, altitude);
    for (int limit : {1, 4, 8}) {
      const auto range = gworld_scene::globe_texture_range_for_camera(0.0, 0.0, altitude, limit);
      g_assert_true(range.valid());
      g_assert_cmpint(range.width_tiles(), <=, limit);
      g_assert_cmpint(range.height_tiles(), <=, limit);
      const int scale = 1 << (original.z - range.z);
      // Lower resolution must retain coverage of the original geographic bounds.
      g_assert_cmpint(range.x_min * scale, <=, original.x_min);
      g_assert_cmpint((range.x_max + 1) * scale, >=, original.x_max + 1);
      g_assert_cmpint(range.y_min * scale, <=, original.y_min);
      g_assert_cmpint((range.y_max + 1) * scale, >=, original.y_max + 1);
    }
  }
  const auto global = gworld_scene::globe_texture_range_for_camera(0.0, 0.0, 7000000.0, 8);
  g_assert_cmpint(global.z, ==, 3);
  g_assert_cmpint(global.width_tiles(), ==, 8);
  g_assert_cmpint(global.height_tiles(), ==, 8);
}

void
test_ranges_cross_dateline()
{
  for (double longitude : {-180.0, 180.0}) {
    const auto range = gworld_scene::tile_range_for_bounds(-0.1, 0.1, longitude - 0.1, longitude + 0.1, 8);
    g_assert_cmpint(range.width_tiles(), ==, 2);
    g_assert_cmpint(gworld_scene::wrap_tile_x(range.x_min, range.z), ==, 255);
    g_assert_cmpint(gworld_scene::wrap_tile_x(range.x_max, range.z), ==, 0);
    const auto global = gworld_scene::tile_range_for_bounds(-90.0, 90.0, longitude - 180.0, longitude + 180.0, 4);
    g_assert_cmpint(global.width_tiles(), ==, 16);
    g_assert_cmpint(global.height_tiles(), ==, 16);

    const auto globe = gworld_scene::globe_texture_range_for_camera(0.0, longitude, 60000.0);
    g_assert_cmpint(globe.width_tiles(), ==, 5);
    for (int limit : {1, 2, 4, 8}) {
      const auto small = gworld_scene::globe_texture_range_for_camera(0.0, longitude, 60000.0, limit);
      g_assert_cmpint(small.width_tiles(), <=, limit);
      g_assert_cmpint(small.height_tiles(), <=, limit);
      // The lower-resolution atlas must still include both sides of the seam.
      bool has_east = false;
      bool has_west = false;
      for (int x = small.x_min; x <= small.x_max; ++x) {
        has_east |= gworld_scene::wrap_tile_x(x, small.z) == (1 << small.z) - 1;
        has_west |= gworld_scene::wrap_tile_x(x, small.z) == 0;
      }
      g_assert_true(has_east && has_west);
    }
  }
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/lod/terrain-imagery-distance-steps", test_terrain_imagery_distance_steps);
  g_test_add_func("/lod/terrain-imagery-capacity-and-altitude", test_terrain_imagery_capacity_and_altitude);
  g_test_add_func("/lod/ranges-cross-dateline", test_ranges_cross_dateline);
  g_test_add_func("/lod/globe-ranges-respect-texture-limits", test_globe_ranges_respect_texture_limits);
  g_test_add_func("/lod/globe-texture-zoom-steps", test_globe_texture_zoom_steps);
  g_test_add_func("/lod/near-globe-ranges-are-bounded", test_near_globe_ranges_are_bounded);
  g_test_add_func("/lod/far-globe-ranges-are-global", test_far_globe_ranges_are_global);
  return g_test_run();
}
