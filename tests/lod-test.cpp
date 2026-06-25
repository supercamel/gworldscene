#include "gworld-scene-lod-private.h"

#include <glib.h>

namespace {

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

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/lod/globe-texture-zoom-steps", test_globe_texture_zoom_steps);
  g_test_add_func("/lod/near-globe-ranges-are-bounded", test_near_globe_ranges_are_bounded);
  g_test_add_func("/lod/far-globe-ranges-are-global", test_far_globe_ranges_are_global);
  return g_test_run();
}
