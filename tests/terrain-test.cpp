#include "gworld-scene-view.h"

#include <glib/gstdio.h>
#include <libsoup/soup.h>

#include <filesystem>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

std::vector<char>
hgt_bytes(int height)
{
  std::vector<char> bytes(17 * 17 * 2);
  for (std::size_t i = 0; i < bytes.size(); i += 2) {
    bytes[i] = static_cast<char>(height >> 8);
    bytes[i + 1] = static_cast<char>(height & 255);
  }
  return bytes;
}

struct TileServer {
  SoupServer *server = soup_server_new(nullptr, nullptr);
  std::string base;
  std::map<std::string, int> requests;
  char *png = nullptr;
  gsize png_size = 0;
  bool offline = false;

  TileServer()
  {
    g_autoptr(GdkPixbuf) tile = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    gdk_pixbuf_fill(tile, 0x208020ff);
    g_autoptr(GError) error = nullptr;
    g_assert_true(gdk_pixbuf_save_to_buffer(tile, &png, &png_size, "png", &error, nullptr));
    g_assert_no_error(error);
    soup_server_add_handler(server, nullptr,
      [](SoupServer *, SoupServerMessage *message, const char *path, GHashTable *, gpointer data) {
        auto &self = *static_cast<TileServer *>(data);
        ++self.requests[path];
        if (self.offline && !g_str_has_prefix(path, "/tiles/")) {
          soup_server_message_set_status(message, SOUP_STATUS_NOT_FOUND, nullptr);
        } else if (g_str_has_prefix(path, "/tiles/")) {
          int z = 0, x = 0, y = 0;
          g_assert_cmpint(std::sscanf(path, "/tiles/%d/%d/%d.png", &z, &x, &y), ==, 3);
          g_assert_cmpint(x, >=, 0);
          g_assert_cmpint(x, <, 1 << z);
          soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
          soup_server_message_set_response(message, "image/png", SOUP_MEMORY_COPY, self.png, self.png_size);
        } else {
          const bool second = g_str_has_prefix(path, "/two/");
          const bool west = std::string(path).find("W180") != std::string::npos;
          const auto bytes = hgt_bytes((second ? 200 : 100) + (west ? 2 : 1));
          soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
          soup_server_message_set_response(message, "application/octet-stream", SOUP_MEMORY_COPY,
                                            bytes.data(), bytes.size());
        }
      }, this, nullptr);
    g_assert_true(soup_server_listen_local(server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));
    g_assert_no_error(error);
    GSList *uris = soup_server_get_uris(server);
    g_autofree char *uri = g_uri_to_string(static_cast<GUri *>(uris->data));
    base = uri;
    g_slist_free_full(uris, reinterpret_cast<GDestroyNotify>(g_uri_unref));
  }

  ~TileServer()
  {
    soup_server_disconnect(server);
    g_object_unref(server);
    g_free(png);
  }
};

void
wait_for_heights(GWorldSceneView *view, double east, double west)
{
  const gint64 deadline = g_get_monotonic_time() + 10 * G_USEC_PER_SEC;
  while (g_get_monotonic_time() < deadline) {
    double east_height = 0.0, west_height = 0.0;
    if (gworld_scene_view_sample_terrain_altitude(view, 0.5, 179.999, &east_height) &&
        gworld_scene_view_sample_terrain_altitude(view, 0.5, -179.999, &west_height) &&
        east_height == east && west_height == west)
      return;
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
  g_error("Terrain did not reach expected heights %.0f / %.0f", east, west);
}

void
test_loading_during_continuous_frames()
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
  TileServer server;
  g_autofree char *cache = g_dir_make_tmp("gworldscene-busy-terrain-test-XXXXXX", nullptr);
  g_assert_nonnull(cache);
  auto *view = GWORLD_SCENE_VIEW(gworld_scene_view_new());
  g_object_ref_sink(view);
  gworld_scene_view_set_cache_directory(view, cache);
  gworld_scene_view_set_map_tile_url_template(view, (server.base + "tiles/{z}/{x}/{y}.png").c_str());
  gworld_scene_view_set_terrain_server(view, (server.base + "one/{tile}.hgt").c_str());
  gworld_scene_view_set_camera(view, 0.5, 179.999, 1500.0);

  // An always-ready source above idle priority models continuous GTK frames
  // on a busy renderer. Tile refreshes must still make progress between frames.
  guint frames = 0;
  const guint busy_source = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
    [](gpointer data) -> gboolean {
      ++*static_cast<guint *>(data);
      return G_SOURCE_CONTINUE;
    }, &frames, nullptr);
  bool loaded = false;
  const gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
  while (g_get_monotonic_time() < deadline) {
    double height = 0.0;
    if (gworld_scene_view_sample_terrain_altitude(view, 0.5, 179.999, &height) && height == 101.0) {
      loaded = true;
      break;
    }
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
  g_source_remove(busy_source);
  g_object_run_dispose(G_OBJECT(view));
  g_object_unref(view);
  const gint64 cleanup_deadline = g_get_monotonic_time() + 100 * 1000;
  while (g_get_monotonic_time() < cleanup_deadline) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
  std::filesystem::remove_all(cache);
  g_assert_cmpuint(frames, >, 0);
  g_assert_true(loaded);
}

void
test_provider_cache_and_dateline()
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
  TileServer server;
  g_autofree char *cache = g_dir_make_tmp("gworldscene-terrain-test-XXXXXX", nullptr);
  g_assert_nonnull(cache);
  const std::filesystem::path root(cache);
  std::filesystem::create_directories(root / "terrain");
  const auto legacy = hgt_bytes(999);
  // Neither old raw nor old archive paths may be attributed to the new provider.
  for (const char *name : {"N00E179.hgt", "N00W180.hgt.zip"}) {
    g_assert_true(g_file_set_contents((root / "terrain" / name).c_str(), legacy.data(), legacy.size(), nullptr));
  }

  auto *view = GWORLD_SCENE_VIEW(gworld_scene_view_new());
  g_object_ref_sink(view);
  gworld_scene_view_set_cache_directory(view, cache);
  gworld_scene_view_set_map_tile_url_template(view, (server.base + "tiles/{z}/{x}/{y}.png").c_str());
  const std::string one = server.base + "one/{tile}.hgt?token=test.zip";
  const std::string two = server.base + "two/{tile}.hgt";
  gworld_scene_view_set_terrain_server(view, one.c_str());
  gworld_scene_view_set_camera(view, 0.5, 179.999, 1500.0);
  wait_for_heights(view, 101.0, 102.0);
  g_assert_cmpint(server.requests["/one/N00E179.hgt"], ==, 1);
  g_assert_cmpint(server.requests["/one/N00W180.hgt"], ==, 1);
  // URI query strings must not turn raw terrain files into archive cache names.
  g_autofree char *one_key = g_compute_checksum_for_string(G_CHECKSUM_SHA256, one.c_str(), -1);
  g_assert_true(std::filesystem::exists(root / "terrain" / one_key / "N00E179.hgt"));

  gworld_scene_view_set_terrain_server(view, two.c_str());
  wait_for_heights(view, 201.0, 202.0);
  g_assert_cmpint(server.requests["/two/N00E179.hgt"], ==, 1);
  g_assert_cmpint(server.requests["/two/N00W180.hgt"], ==, 1);
  double seam_height = 0.0;
  g_assert_true(gworld_scene_view_sample_terrain_altitude(view, 0.5, 180.0, &seam_height));
  g_assert_cmpfloat(seam_height, ==, 202.0);

  // Return across the dateline and switch back with the server offline. This
  // must reload source one's cached elevations, without requests or source two's data.
  server.offline = true;
  gworld_scene_view_set_cache_enabled(view, FALSE);
  gworld_scene_view_set_terrain_server(view, one.c_str());
  gworld_scene_view_set_camera(view, 0.5, -179.999, 1500.0);
  gworld_scene_view_set_cache_enabled(view, TRUE);
  wait_for_heights(view, 101.0, 102.0);
  g_assert_cmpint(server.requests["/one/N00E179.hgt"], ==, 1);
  g_assert_cmpint(server.requests["/one/N00W180.hgt"], ==, 1);

  // Request URLs may contain credentials. Check both successful imagery and
  // failed terrain diagnostics with a deliberately fake query value.
  struct Diagnostics {
    bool texture_finished = false;
    bool terrain_unavailable = false;
    bool invalid_uri = false;
  } diagnostics;
  const guint log_handler = g_log_set_handler("GWorldScene", G_LOG_LEVEL_DEBUG,
    [](const gchar *, GLogLevelFlags, const gchar *message, gpointer data) {
      auto &seen = *static_cast<Diagnostics *>(data);
      g_assert_null(std::strstr(message, "test.zip"));
      if (g_str_has_prefix(message, "Texture download finished:"))
        seen.texture_finished = true;
      if (g_str_has_prefix(message, "Terrain tile unavailable:"))
        seen.terrain_unavailable = true;
      if (g_str_has_prefix(message, "Terrain download failed:") &&
          std::strstr(message, "Invalid tile URI") != nullptr)
        seen.invalid_uri = true;
    }, &diagnostics);
  gworld_scene_view_set_map_tile_url_template(view,
    (server.base + "tiles/{z}/{x}/{y}.png?token=test.zip").c_str());
  gworld_scene_view_set_terrain_server(view,
    (server.base + "missing/{tile}.hgt?token=test.zip").c_str());
  const gint64 diagnostic_deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
  while ((!diagnostics.texture_finished || !diagnostics.terrain_unavailable) &&
         g_get_monotonic_time() < diagnostic_deadline) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
  g_assert_true(diagnostics.texture_finished);
  g_assert_true(diagnostics.terrain_unavailable);
  gworld_scene_view_set_terrain_server(view, "not-a-uri/{tile}?token=test.zip");
  const gint64 invalid_deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
  while (!diagnostics.invalid_uri && g_get_monotonic_time() < invalid_deadline) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
  g_assert_true(diagnostics.invalid_uri);
  g_log_remove_handler("GWorldScene", log_handler);

  g_object_run_dispose(G_OBJECT(view));
  g_object_unref(view);
  // Cancelled callbacks retain the view until delivered; let them release it.
  const gint64 deadline = g_get_monotonic_time() + 100 * 1000;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(1000);
  }
  g_assert_true(std::filesystem::exists(root / "terrain" / "N00E179.hgt"));
  std::filesystem::remove_all(root);
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/terrain/provider-cache-and-dateline", test_provider_cache_and_dateline);
  g_test_add_func("/terrain/loading-during-continuous-frames", test_loading_during_continuous_frames);
  return g_test_run();
}
