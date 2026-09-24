#include "gworld-scene-view.h"

#include <glib.h>
#include <utility>
#include <cmath>

namespace {

template <typename Node, typename Getter, std::size_t... I>
void
assert_optional_outputs(Node *node, Getter getter, std::index_sequence<I...>)
{
  double expected[sizeof...(I)] = {};
  getter(node, &expected[I]...);
  getter(node, (static_cast<void>(I), nullptr)...);
  for (std::size_t selected = 0; selected < sizeof...(I); ++selected) {
    double value = -12345.0;
    getter(node, (I == selected ? &value : nullptr)...);
    g_assert_cmpfloat(value, ==, expected[selected]);
  }
}

template <typename Node, typename... Outputs>
void
assert_optional_outputs(Node *node, void (*getter)(Node *, Outputs...))
{
  assert_optional_outputs(node, getter, std::index_sequence_for<Outputs...>{});
}

void
test_getters_accept_optional_outputs()
{
#if GWORLD_SCENE_GTK_MAJOR == 4
  const bool initialized = gtk_init_check();
#else
  const bool initialized = gtk_init_check(nullptr, nullptr);
#endif
  if (!initialized) {
    g_test_skip("No GTK display available; run under xvfb-run");
    return;
  }
  auto *view = GWORLD_SCENE_VIEW(gworld_scene_view_new());
  g_object_ref_sink(view);
  gworld_scene_view_set_cache_enabled(view, FALSE);
  g_assert_true(gworld_scene_view_get_atmosphere_enabled(view));
  g_assert_false(gworld_scene_view_get_fog_enabled(view));
  g_assert_false(gworld_scene_view_get_shadows_enabled(view));
  g_assert_false(gworld_scene_view_get_water_enabled(view));
  g_object_set(view, "atmosphere-density", 0.5, "atmosphere-haze", 2.0, "water-wave-strength", 0.0, nullptr);
  g_assert_cmpfloat(gworld_scene_view_get_atmosphere_density(view), ==, 0.5);
  g_assert_cmpfloat(gworld_scene_view_get_atmosphere_haze(view), ==, 2.0);
  g_assert_cmpfloat(gworld_scene_view_get_water_wave_strength(view), ==, 0.0);
  gworld_scene_view_set_atmosphere_density(view, NAN);
  g_assert_cmpfloat(gworld_scene_view_get_atmosphere_density(view), ==, 1.0);
  gworld_scene_view_set_water_tile_url_template(view, "https://example.invalid/{z}/{x}/{y}.pbf");
  gworld_scene_view_set_water_tile_url_template(view, gworld_scene_view_get_water_tile_url_template(view));
  g_assert_cmpstr(gworld_scene_view_get_water_tile_url_template(view), ==, "https://example.invalid/{z}/{x}/{y}.pbf");
  gworld_scene_view_set_water_tile_url_template(view, nullptr);
  g_assert_nonnull(g_strstr_len(gworld_scene_view_get_water_tile_url_template(view), -1, "openfreemap.org"));

  gworld_scene_view_set_free_camera_position(view, -35.0, 149.0, 600.0);
  gworld_scene_view_set_free_camera_orientation(view, 25.0, -10.0);
  auto *cube = gworld_scene_view_add_cube(view, -34.0, 148.0, 725.0, 11.0, 23.0, 37.0);
  auto *node = GWORLD_SCENE_NODE(cube);
  auto *cylinder = gworld_scene_view_add_cylinder(view, 0, 0, 0, 19, 43);
  auto *billboard = gworld_scene_view_add_billboard(view, "unused.png", 0, 0, 0);
  auto *overlay = gworld_scene_view_add_ground_overlay(view, "unused.png", -34, 144, -35, 145, -36, 146, -37, 147);
  auto *polygon = gworld_scene_view_add_polygon(view);
  auto *circle = gworld_scene_view_add_circle(view, 0, 0, 0, 100);
  auto *label = gworld_scene_view_add_text_label(view, "Binding test", 0, 0, 0);

  assert_optional_outputs(view, gworld_scene_view_get_camera);
  assert_optional_outputs(view, gworld_scene_view_get_camera_orientation);
  assert_optional_outputs(view, gworld_scene_view_get_free_camera_position);
  assert_optional_outputs(view, gworld_scene_view_get_free_camera_orientation);
  assert_optional_outputs(view, gworld_scene_view_get_sun_position);
  assert_optional_outputs(view, gworld_scene_view_get_fog_range);
  assert_optional_outputs(view, gworld_scene_view_get_fog_color);
  assert_optional_outputs(node, gworld_scene_node_get_position);
  assert_optional_outputs(node, gworld_scene_node_get_orientation_ned);
  assert_optional_outputs(node, gworld_scene_node_get_scale);
  assert_optional_outputs(node, gworld_scene_node_get_color);
  assert_optional_outputs(node, gworld_scene_node_get_dimensions);
  assert_optional_outputs(cube, gworld_scene_cube_node_get_dimensions);
  assert_optional_outputs(cylinder, gworld_scene_cylinder_node_get_size);
  assert_optional_outputs(billboard, gworld_scene_billboard_node_get_size_limits);
  assert_optional_outputs(billboard, gworld_scene_billboard_node_get_reference_size);
  assert_optional_outputs(overlay, gworld_scene_ground_overlay_node_get_corners);
  assert_optional_outputs(polygon, gworld_scene_polygon_node_get_fill_color);
  assert_optional_outputs(polygon, gworld_scene_polygon_node_get_outline_color);
  assert_optional_outputs(circle, gworld_scene_circle_node_get_fill_color);
  assert_optional_outputs(circle, gworld_scene_circle_node_get_outline_color);
  assert_optional_outputs(label, gworld_scene_text_label_node_get_text_color);
  assert_optional_outputs(label, gworld_scene_text_label_node_get_background_color);
  assert_optional_outputs(label, gworld_scene_text_label_node_get_size_limits);
  assert_optional_outputs(label, gworld_scene_text_label_node_get_reference_size);
  g_object_run_dispose(G_OBJECT(view));
  g_object_unref(view);
}

void
test_view_exposes_pick_signals()
{
  gpointer klass = g_type_class_ref(GWORLD_TYPE_SCENE_VIEW);
  g_assert_nonnull(klass);

  auto assert_signal = [](const char *name,
                          GType return_type,
                          unsigned int n_params,
                          const GType *params) {
    const guint signal_id = g_signal_lookup(name, GWORLD_TYPE_SCENE_VIEW);
    g_assert_cmpuint(signal_id, !=, 0);

    GSignalQuery query = {};
    g_signal_query(signal_id, &query);
    g_assert_cmpuint(query.return_type, ==, return_type);
    g_assert_cmpuint(query.n_params, ==, n_params);
    for (unsigned int i = 0; i < n_params; ++i)
      g_assert_cmpuint(query.param_types[i], ==, params[i]);
  };

  const GType node_params[] = {
    GWORLD_TYPE_SCENE_NODE,
    G_TYPE_DOUBLE,
    G_TYPE_DOUBLE,
    G_TYPE_DOUBLE,
    G_TYPE_UINT,
  };
  const GType ground_params[] = {
    G_TYPE_DOUBLE,
    G_TYPE_DOUBLE,
    G_TYPE_DOUBLE,
    G_TYPE_UINT,
  };

  assert_signal("node-clicked", G_TYPE_NONE, G_N_ELEMENTS(node_params), node_params);
  assert_signal("node-double-clicked", G_TYPE_NONE, G_N_ELEMENTS(node_params), node_params);
  assert_signal("ground-clicked", G_TYPE_NONE, G_N_ELEMENTS(ground_params), ground_params);
  assert_signal("ground-double-clicked", G_TYPE_NONE, G_N_ELEMENTS(ground_params), ground_params);

  g_type_class_unref(klass);
}

void
test_view_exposes_texture_memory_budget_property()
{
  gpointer klass = g_type_class_ref(GWORLD_TYPE_SCENE_VIEW);
  g_assert_nonnull(klass);

  GParamSpec *pspec =
    g_object_class_find_property(G_OBJECT_CLASS(klass), "texture-memory-budget-mib");
  g_assert_nonnull(pspec);
  g_assert_cmpuint(G_PARAM_SPEC_VALUE_TYPE(pspec), ==, G_TYPE_UINT);

  auto *uint_pspec = G_PARAM_SPEC_UINT(pspec);
  g_assert_cmpuint(uint_pspec->minimum, ==, 0);
  g_assert_cmpuint(uint_pspec->maximum, ==, 65536);
  g_assert_cmpuint(uint_pspec->default_value, ==, 0);
  g_assert_true((pspec->flags & G_PARAM_READWRITE) == G_PARAM_READWRITE);
  g_assert_true((pspec->flags & G_PARAM_EXPLICIT_NOTIFY) != 0);

  g_type_class_unref(klass);
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/scene-view/getters-accept-optional-outputs", test_getters_accept_optional_outputs);
  g_test_add_func("/scene-view/pick-signals", test_view_exposes_pick_signals);
  g_test_add_func("/scene-view/texture-memory-budget-property",
                  test_view_exposes_texture_memory_budget_property);
  return g_test_run();
}
