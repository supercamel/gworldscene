#include "gworld-scene-node-private.h"
#include "gworld-scene-geo-private.h"

#include <glib.h>

namespace {

void
assert_near(double actual, double expected, double tolerance)
{
  g_assert_cmpfloat_with_epsilon(actual, expected, tolerance);
}

void
test_new_node_has_nonzero_defaults()
{
  GWorldSceneCubeNode *cube = _gworld_scene_cube_node_new(42,
                                                          -35.0024,
                                                          147.4648,
                                                          1200.0,
                                                          900.0,
                                                          800.0,
                                                          700.0);
  GWorldSceneNode *node = GWORLD_SCENE_NODE(cube);

  g_assert_cmpuint(gworld_scene_node_get_id(node), ==, 42);
  g_assert_cmpuint(gworld_scene_node_get_primitive(node), ==, GWORLD_SCENE_PRIMITIVE_CUBE);
  g_assert_true(GWORLD_IS_SCENE_NODE(cube));
  g_assert_true(GWORLD_IS_SCENE_CUBE_NODE(cube));

  double width = 0.0;
  double depth = 0.0;
  double height = 0.0;
  gworld_scene_cube_node_get_dimensions(cube, &width, &depth, &height);
  assert_near(width, 900.0, 0.001);
  assert_near(depth, 800.0, 0.001);
  assert_near(height, 700.0, 0.001);

  double scale_x = 0.0;
  double scale_y = 0.0;
  double scale_z = 0.0;
  gworld_scene_node_get_scale(node, &scale_x, &scale_y, &scale_z);
  assert_near(scale_x, 1.0, 0.001);
  assert_near(scale_y, 1.0, 0.001);
  assert_near(scale_z, 1.0, 0.001);

  g_object_unref(node);
}

void
test_translate_ned_updates_geodetic_position()
{
  const double lat = -35.0024;
  const double lon = 147.4648;
  const double alt = 1200.0;
  GWorldSceneNode *node = GWORLD_SCENE_NODE(_gworld_scene_cube_node_new(7,
                                                                        lat,
                                                                        lon,
                                                                        alt,
                                                                        100.0,
                                                                        100.0,
                                                                        100.0));

  gworld_scene_node_translate_ned(node, 1000.0, 500.0, 100.0);

  double translated_lat = 0.0;
  double translated_lon = 0.0;
  double translated_alt = 0.0;
  gworld_scene_node_get_position(node,
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

  g_object_unref(node);
}

void
test_mutations_emit_changed()
{
  GWorldSceneNode *node = GWORLD_SCENE_NODE(_gworld_scene_sphere_node_new(9,
                                                                         -35.0024,
                                                                         147.4648,
                                                                         1200.0,
                                                                         300.0));
  unsigned int changed_count = 0;
  g_signal_connect(node,
                   "changed",
                   G_CALLBACK(+[](GWorldSceneNode *, gpointer user_data) {
                     auto *count = static_cast<unsigned int *>(user_data);
                     ++(*count);
                   }),
                   &changed_count);

  gworld_scene_node_set_color(node, 1.0, 0.0, 0.0);
  gworld_scene_node_set_scale(node, 2.0, 2.0, 2.0);
  gworld_scene_node_set_orientation_ned(node, 45.0, 5.0, 10.0);

  g_assert_cmpuint(changed_count, ==, 3);
  g_object_unref(node);
}

void
test_model_node_stores_path_and_uses_white_default_color()
{
  GWorldSceneModelNode *model = _gworld_scene_model_node_new(11,
                                                             "/tmp/example-model.obj",
                                                             -35.0024,
                                                             147.4648,
                                                             1200.0);
  GWorldSceneNode *node = GWORLD_SCENE_NODE(model);

  g_assert_cmpuint(gworld_scene_node_get_id(node), ==, 11);
  g_assert_cmpuint(gworld_scene_node_get_primitive(node), ==, GWORLD_SCENE_PRIMITIVE_MODEL);
  g_assert_true(GWORLD_IS_SCENE_MODEL_NODE(model));
  g_assert_cmpstr(gworld_scene_model_node_get_model_path(model), ==, "/tmp/example-model.obj");
  g_assert_cmpstr(gworld_scene_node_get_model_path(node), ==, "/tmp/example-model.obj");

  double red = 0.0;
  double green = 0.0;
  double blue = 0.0;
  gworld_scene_node_get_color(node, &red, &green, &blue);
  assert_near(red, 1.0, 0.001);
  assert_near(green, 1.0, 0.001);
  assert_near(blue, 1.0, 0.001);

  g_object_unref(node);
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/scene-node/new-node-has-nonzero-defaults", test_new_node_has_nonzero_defaults);
  g_test_add_func("/scene-node/translate-ned-updates-geodetic-position", test_translate_ned_updates_geodetic_position);
  g_test_add_func("/scene-node/mutations-emit-changed", test_mutations_emit_changed);
  g_test_add_func("/scene-node/model-node-stores-path-and-uses-white-default-color", test_model_node_stores_path_and_uses_white_default_color);
  return g_test_run();
}
