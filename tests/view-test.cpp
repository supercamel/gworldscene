#include "gworld-scene-view.h"

#include <glib.h>

namespace {

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

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/scene-view/pick-signals", test_view_exposes_pick_signals);
  return g_test_run();
}
