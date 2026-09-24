#pragma once

#include <libsoup/soup.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <map>
#include <string>
#include <vector>

// Keep terrain requests open until the test explicitly completes a tile.
// Imagery remains available, as in a scene with a slow elevation provider.
struct StalledTileServer {
  SoupServer *server = soup_server_new(nullptr, nullptr);
  std::string base;
  std::map<std::string, SoupServerMessage *> pending;
  char *png = nullptr;
  gsize png_size = 0;

  void pause(SoupServerMessage *message)
  {
#if SOUP_CHECK_VERSION(3, 2, 0)
    soup_server_message_pause(message);
#else
    soup_server_pause_message(server, message);
#endif
  }

  void unpause(SoupServerMessage *message)
  {
#if SOUP_CHECK_VERSION(3, 2, 0)
    soup_server_message_unpause(message);
#else
    soup_server_unpause_message(server, message);
#endif
  }

  StalledTileServer()
  {
    g_autoptr(GdkPixbuf) tile = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 1, 1);
    gdk_pixbuf_fill(tile, 0x208020ff);
    g_autoptr(GError) error = nullptr;
    g_assert_true(gdk_pixbuf_save_to_buffer(tile, &png, &png_size, "png", &error, nullptr));
    g_assert_no_error(error);
    soup_server_add_handler(server, nullptr,
      [](SoupServer *, SoupServerMessage *message, const char *path, GHashTable *, gpointer data) {
        auto &self = *static_cast<StalledTileServer *>(data);
        if (g_str_has_prefix(path, "/terrain/")) {
          // Context recreation can cancel and restart the same tile request.
          auto previous = self.pending.find(path);
          if (previous != self.pending.end()) {
            soup_server_message_set_status(previous->second, SOUP_STATUS_NOT_FOUND, nullptr);
            self.unpause(previous->second);
            g_object_unref(previous->second);
          }
          self.pending[path] = SOUP_SERVER_MESSAGE(g_object_ref(message));
          self.pause(message);
        } else {
          soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
          soup_server_message_set_response(message, "image/png", SOUP_MEMORY_COPY, self.png, self.png_size);
        }
      }, this, nullptr);
    g_assert_true(soup_server_listen_local(server, 0, SOUP_SERVER_LISTEN_IPV4_ONLY, &error));
    g_assert_no_error(error);
    GSList *uris = soup_server_get_uris(server);
    g_autofree char *uri = g_uri_to_string(static_cast<GUri *>(uris->data));
    base = uri;
    g_slist_free_full(uris, reinterpret_cast<GDestroyNotify>(g_uri_unref));
  }

  void complete(const char *path, int height)
  {
    auto it = pending.find(path);
    g_assert_true(it != pending.end());
    std::vector<char> bytes(17 * 17 * 2);
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
      bytes[i] = static_cast<char>(height >> 8);
      bytes[i + 1] = static_cast<char>(height & 255);
    }
    soup_server_message_set_status(it->second, SOUP_STATUS_OK, nullptr);
    soup_server_message_set_response(it->second, "application/octet-stream", SOUP_MEMORY_COPY,
                                      bytes.data(), bytes.size());
    unpause(it->second);
    g_object_unref(it->second);
    pending.erase(it);
  }

  ~StalledTileServer()
  {
    for (const auto &entry : pending) {
      soup_server_message_set_status(entry.second, SOUP_STATUS_NOT_FOUND, nullptr);
      unpause(entry.second);
      g_object_unref(entry.second);
    }
    soup_server_disconnect(server);
    g_object_unref(server);
    g_free(png);
  }
};
