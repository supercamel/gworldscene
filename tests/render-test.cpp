#include "gworld-scene-view.h"
#include "gworld-scene-view-shaders-private.h"
#include "stalled-tile-server-private.h"

#include <glib/gstdio.h>
#include <filesystem>
#include <functional>
#include <vector>

namespace {

bool
wait_until(const std::function<bool()> &ready)
{
  const gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
  while (g_get_monotonic_time() < deadline) {
    g_main_context_iteration(nullptr, FALSE);
    if (ready())
      return true;
    g_usleep(1000);
  }
  return false;
}

int
count_color_pixels(const std::vector<unsigned char> &pixels, int channel)
{
  int count = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    if (pixels[i + channel] > 50 &&
        pixels[i + channel] > pixels[i + (channel + 1) % 3] * 2 &&
        pixels[i + channel] > pixels[i + (channel + 2) % 3] * 2)
      ++count;
  }
  return count;
}

std::vector<unsigned char>
render_frame(GtkGLArea *area, const std::function<void()> &draw = {})
{
  gtk_gl_area_make_current(area);
  g_assert_no_error(gtk_gl_area_get_error(area));
  g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);

  // Deliberately use a framebuffer larger than the widget's logical allocation
  // and separate read/draw bindings, as GTK may do on scaled displays.
  constexpr int size = 256;
  GLuint fbos[2] = {};
  GLuint color = 0;
  GLuint depth = 0;
  glGenFramebuffers(2, fbos);
  glBindFramebuffer(GL_FRAMEBUFFER, fbos[0]);
  glGenTextures(1, &color);
  glBindTexture(GL_TEXTURE_2D, color);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  glGenRenderbuffers(1, &depth);
  glBindRenderbuffer(GL_RENDERBUFFER, depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
  g_assert_cmpuint(glCheckFramebufferStatus(GL_FRAMEBUFFER), ==, GL_FRAMEBUFFER_COMPLETE);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[1]);
  glViewport(0, 0, size, size);

  if (draw) {
    draw();
  } else {
    gboolean handled = FALSE;
    g_signal_emit_by_name(area, "render", gtk_gl_area_get_context(area), &handled);
    g_assert_true(handled);
  }
  g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);
  GLint viewport[4] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  g_assert_cmpint(viewport[2], ==, size);
  g_assert_cmpint(viewport[3], ==, size);
  GLint binding = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &binding);
  g_assert_cmpuint(binding, ==, fbos[0]);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &binding);
  g_assert_cmpuint(binding, ==, fbos[1]);

  std::vector<unsigned char> pixels(size * size * 4);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
  glReadPixels(0, 0, size, size, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(2, fbos);
  glDeleteRenderbuffers(1, &depth);
  glDeleteTextures(1, &color);
  return pixels;
}

void
check_dateline_imagery(GtkGLArea *area)
{
  const GLuint program = gworld_scene_view_create_program();
  g_assert_cmpuint(program, !=, 0);
  GLuint vao = 0, vbo = 0, texture = 0;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenTextures(1, &texture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  const unsigned char colors[] = {255, 0, 0, 255, 0, 255, 0, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  const float identity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  for (float center : {180.0f, -180.0f, 0.0f}) {
    const auto pixels = render_frame(area, [&]() {
      const float vertices[] = {
        -1, -1, 0, -0.5f, center - 1, 1, -1, 0, -0.5f, center + 1,
        -1,  1, 0, -0.5f, center - 1, 1,  1, 0, -0.5f, center + 1,
      };
      glUseProgram(program);
      glUniformMatrix4fv(glGetUniformLocation(program, "mvp"), 1, GL_FALSE, identity);
      glUniformMatrix4fv(glGetUniformLocation(program, "light_mvp"), 1, GL_FALSE, identity);
      glUniform3f(glGetUniformLocation(program, "ambient_color"), 1, 1, 1);
      glUniform1f(glGetUniformLocation(program, "ambient_strength"), 1);
      glUniform3f(glGetUniformLocation(program, "sun_direction"), 0, 1, 0);
      glUniform1i(glGetUniformLocation(program, "has_base_texture"), TRUE);
      glUniform1i(glGetUniformLocation(program, "base_texture"), 0);
      glUniform1i(glGetUniformLocation(program, "globe_atlas_valid"), TRUE);
      glUniform4f(glGetUniformLocation(program, "globe_atlas_range"), 8, center < 0 ? -1 : 255, 128, 0);
      glUniform2f(glGetUniformLocation(program, "globe_atlas_size"), 2, 1);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
      glVertexAttrib3f(5, 0, 1, 0);
      glVertexAttrib4f(6, 0, 0, 1, 1);
      glVertexAttrib1f(7, 2); // Globe material, blue when no imagery covers it.
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_BLEND);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glBindVertexArray(0);
      glUseProgram(0);
    });
    for (int x : {64, 128, 192}) {
      const auto *pixel = pixels.data() + (128 * 256 + x) * 4;
      g_test_message("Dateline center=%.0f x=%d pixel=%u,%u,%u", center, x, pixel[0], pixel[1], pixel[2]);
      if (center == 0) {
        // The atlas at the dateline must not create an interpolated strip at Greenwich.
        g_assert_cmpuint(pixel[2], ==, 255);
        g_assert_cmpuint(pixel[0], ==, 0);
        g_assert_cmpuint(pixel[1], ==, 0);
      } else {
        g_assert_cmpuint(pixel[x < 128 ? 0 : 1], ==, 255);
        g_assert_cmpuint(pixel[x < 128 ? 1 : 0], ==, 0);
      }
    }
  }
  glDeleteTextures(1, &texture);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
}

void
check_imagery_blending(GtkGLArea *area)
{
  const GLuint program = gworld_scene_view_create_program();
  g_assert_cmpuint(program, !=, 0);
  GLuint vao = 0, vbo = 0, textures[2] = {};
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenTextures(2, textures);
  const float identity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
  const float vertices[] = {
    -1, -1, 0, -0.5f, -5.625f, 1, -1, 0, -0.5f, 5.625f,
    -1,  1, 0, -0.5f, -5.625f, 1,  1, 0, -0.5f, 5.625f,
  };
  for (const char *layer : {"far", "mid", "detail"}) {
    for (int missing : {0, 1, 2}) {
      const auto pixels = render_frame(area, [&]() {
        glUseProgram(program);
        glUniformMatrix4fv(glGetUniformLocation(program, "mvp"), 1, GL_FALSE, identity);
        glUniformMatrix4fv(glGetUniformLocation(program, "light_mvp"), 1, GL_FALSE, identity);
        glUniform3f(glGetUniformLocation(program, "ambient_color"), 1, 1, 1);
        glUniform1f(glGetUniformLocation(program, "ambient_strength"), 1);
        glUniform3f(glGetUniformLocation(program, "sun_direction"), 0, 1, 0);
        for (const char *name : {"far", "mid", "detail"}) {
          glUniform1i(glGetUniformLocation(program, (std::string("has_") + name + "_texture").c_str()), FALSE);
          glUniform1i(glGetUniformLocation(program, (std::string(name) + "_atlas_valid").c_str()), FALSE);
        }
        for (int i = 0; i < 2; ++i) {
          const std::string name = i == 0 ? "base" : layer;
          const unsigned char rgba[] = {static_cast<unsigned char>(i == 1 ? 255 : 0), 0,
                                        static_cast<unsigned char>(i == 0 ? 255 : 0),
                                        static_cast<unsigned char>(missing == i + 1 ? 0 : 255)};
          glActiveTexture(GL_TEXTURE0 + i);
          glBindTexture(GL_TEXTURE_2D, textures[i]);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
          glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
          glUniform1i(glGetUniformLocation(program, ("has_" + name + "_texture").c_str()), TRUE);
          glUniform1i(glGetUniformLocation(program, (name + "_texture").c_str()), i);
          glUniform1i(glGetUniformLocation(program, (name + "_atlas_valid").c_str()), TRUE);
          glUniform4f(glGetUniformLocation(program, (name + "_atlas_range").c_str()), 8, 124, 126, 0);
          glUniform2f(glGetUniformLocation(program, (name + "_atlas_size").c_str()), 8, 4);
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
        glVertexAttrib3f(5, 0, 1, 0);
        glVertexAttrib1f(7, 0); // Terrain: exercise the imagery stack, not globe sampling.
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
        glUseProgram(0);
      });
      const auto *edge = pixels.data() + (128 * 256) * 4;
      const auto *blend = edge + 8 * 4;
      const auto *inner = edge + 64 * 4;
      if (missing == 0) {
        g_assert_cmpuint(edge[2], >, edge[0] * 3);
        g_assert_cmpuint(blend[0], >, 50);
        g_assert_cmpuint(blend[2], >, 50);
        g_assert_cmpuint(inner[0], >, inner[2] * 3);
      } else {
        // Missing coarse tiles keep fine detail all the way to the edge;
        // missing fine tiles keep the coarse layer, including the interior.
        const int channel = missing == 1 ? 0 : 2;
        for (const auto *pixel : {edge, blend, inner})
          g_assert_cmpuint(pixel[channel], >, pixel[2 - channel] * 3);
      }
    }
  }
  glDeleteTextures(2, textures);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
}

void
set_movement_key(GtkWidget *view, guint key, bool pressed)
{
  gboolean handled = FALSE;
#if GWORLD_SCENE_GTK_MAJOR == 4
  g_autoptr(GListModel) controllers = gtk_widget_observe_controllers(view);
  for (guint i = 0; i < g_list_model_get_n_items(controllers); ++i) {
    g_autoptr(GObject) controller = G_OBJECT(g_list_model_get_item(controllers, i));
    if (!GTK_IS_EVENT_CONTROLLER_KEY(controller))
      continue;
    if (pressed)
      g_signal_emit_by_name(controller, "key-pressed", key, 0, static_cast<GdkModifierType>(0), &handled);
    else
      g_signal_emit_by_name(controller, "key-released", key, 0, static_cast<GdkModifierType>(0));
    if (pressed)
      g_assert_true(handled);
    return;
  }
  g_assert_not_reached();
#else
  GdkEventKey event = {};
  event.type = pressed ? GDK_KEY_PRESS : GDK_KEY_RELEASE;
  event.window = gtk_widget_get_window(view);
  event.keyval = key;
  g_signal_emit_by_name(view, pressed ? "key-press-event" : "key-release-event", &event, &handled);
  if (pressed)
    g_assert_true(handled);
#endif
}

void
check_camera_crosses_dateline(GWorldSceneView *view)
{
  gtk_widget_map(GTK_WIDGET(view));
  gworld_scene_view_set_camera(view, 0.0, 179.999, 1000.0);
  for (guint key : {GDK_KEY_d, GDK_KEY_a}) {
    set_movement_key(GTK_WIDGET(view), key, true);
    const gint64 deadline = g_get_monotonic_time() + 5 * G_USEC_PER_SEC;
    bool crossed = false;
    while (g_get_monotonic_time() < deadline) {
      g_main_context_iteration(nullptr, FALSE);
      double longitude = 0.0;
      gworld_scene_view_get_camera(view, nullptr, &longitude, nullptr);
      crossed = key == GDK_KEY_d ? longitude < -179.0 : longitude > 179.0;
      if (crossed)
        break;
      g_usleep(1000);
    }
    set_movement_key(GTK_WIDGET(view), key, false);
    g_assert_true(crossed);
  }
}

void
test_render(gconstpointer data)
{
  const bool use_es = GPOINTER_TO_INT(data);
#if GWORLD_SCENE_GTK_MAJOR == 4
  if (!gtk_init_check()) {
#else
  if (!gtk_init_check(nullptr, nullptr)) {
#endif
    g_test_skip("No GTK display available; run under xvfb-run to exercise GL");
    return;
  }

  g_autofree char *cache_dir = g_dir_make_tmp("gworldscene-render-XXXXXX", nullptr);
  g_assert_nonnull(cache_dir);
  StalledTileServer server;
#if GWORLD_SCENE_GTK_MAJOR == 4
  GtkWidget *window = gtk_window_new();
#else
  GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#endif
  auto *view = GWORLD_SCENE_VIEW(gworld_scene_view_new());
  auto *area = GTK_GL_AREA(view);
  if (use_es) {
#if GWORLD_SCENE_GTK_MAJOR == 4 && GTK_CHECK_VERSION(4, 12, 0)
    gtk_gl_area_set_allowed_apis(area, GDK_GL_API_GLES);
#else
    gtk_gl_area_set_use_es(area, TRUE);
#endif
  }
  gworld_scene_view_set_cache_directory(view, cache_dir);
  gworld_scene_view_set_terrain_server(view, (server.base + "terrain/{tile}.hgt").c_str());
  gworld_scene_view_set_map_tile_url_template(view, (server.base + "tiles/{z}/{x}/{y}.png").c_str());
  gworld_scene_view_set_camera(view, 0.5, 0.5, 1000.0);
  gworld_scene_view_set_camera_mode(view, GWORLD_SCENE_CAMERA_MODE_FREE);
  gworld_scene_view_set_camera_orientation(view, 0.0, -89.0);
  gworld_scene_view_set_sun_position(view, 90.0, 60.0);
  auto *cube = gworld_scene_view_add_cube(view, 0.5, 0.5, 250.0, 400.0, 400.0, 400.0);
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(cube), 1.0, 0.0, 0.0);
  gtk_window_set_default_size(GTK_WINDOW(window), 128, 128);
#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(view));
  gtk_window_present(GTK_WINDOW(window));
#else
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
  gtk_widget_show_all(window);
#endif

  // Downloads stay pending throughout these checks. Neither the initial scene
  // nor later terrain/scene edits may depend on draining the download queue.
  g_assert_true(wait_until([&]() {
    return gtk_widget_get_realized(GTK_WIDGET(view)) &&
           server.pending.count("/terrain/N00E000.hgt") != 0;
  }));
  gtk_gl_area_make_current(area);
  g_assert_no_error(gtk_gl_area_get_error(area));
  if (use_es)
    g_assert_true(gdk_gl_context_get_use_es(gtk_gl_area_get_context(area)));
  const char *expected_api = g_getenv("GWORLD_SCENE_TEST_EXPECT_API");
  if (expected_api != nullptr)
    g_assert_cmpint(epoxy_is_desktop_gl(), ==, g_str_equal(expected_api, "gl"));
  g_test_message("Rendering with %s", glGetString(GL_VERSION));

  // Check every shader, including programs whose geometry is absent in this scene.
  for (auto create : {gworld_scene_view_create_program, gworld_scene_view_create_shadow_program,
                      gworld_scene_view_create_billboard_program, gworld_scene_view_create_sun_program,
                      gworld_scene_view_create_sky_program}) {
    GLuint program = create();
    g_assert_cmpuint(program, !=, 0);
    glDeleteProgram(program);
  }
  g_assert_true(wait_until([&]() {
    const auto pixels = render_frame(area);
    return count_color_pixels(pixels, 0) > 100 && count_color_pixels(pixels, 1) > 1000;
  }));

  // A distant completed tile must not remove the imagery fallback underneath
  // the camera while its own terrain tile is still stalled.
  g_assert_true(wait_until([&]() { return server.pending.count("/terrain/N00E001.hgt") != 0; }));
  server.complete("/terrain/N00E001.hgt", 100);
  g_assert_true(wait_until([&]() {
    double height = 0;
    return gworld_scene_view_sample_terrain_altitude(view, 0.5, 1.5, &height) && height == 100;
  }));
  gworld_scene_node_set_color(GWORLD_SCENE_NODE(cube), 0.0, 0.0, 1.0);
  g_assert_true(wait_until([&]() {
    const auto pixels = render_frame(area);
    return count_color_pixels(pixels, 2) > 100 && count_color_pixels(pixels, 1) > 1000;
  }));

  // Completing just the camera's elevation tile must update the rendered mesh.
  // Elevation 800 lies above the cube, so the green ground now occludes it.
  server.complete("/terrain/N00E000.hgt", 800);
  g_assert_true(wait_until([&]() {
    double height = 0;
    if (!gworld_scene_view_sample_terrain_altitude(view, 0.5, 0.5, &height) || height != 800)
      return false;
    const auto pixels = render_frame(area);
    return count_color_pixels(pixels, 2) == 0 && count_color_pixels(pixels, 1) > 1000;
  }));
  g_assert_false(server.pending.empty());
  const auto before = render_frame(area);

  // Recreate the context without changing scene state or rebuilding the CPU mesh.
  gtk_widget_unrealize(GTK_WIDGET(view));
  gtk_widget_realize(GTK_WIDGET(view));
  // Imagery released from CPU memory is reloaded asynchronously from its cache.
  g_assert_true(wait_until([&]() { return before == render_frame(area); }));
  check_dateline_imagery(area);
  check_imagery_blending(area);
  gworld_scene_view_set_cache_enabled(view, FALSE);
  check_camera_crosses_dateline(view);

#if GWORLD_SCENE_GTK_MAJOR == 4
  gtk_window_destroy(GTK_WINDOW(window));
#else
  gtk_widget_destroy(window);
#endif
  std::filesystem::remove_all(cache_dir);
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_data_func("/render/automatic", GINT_TO_POINTER(FALSE), test_render);
  g_test_add_data_func("/render/gles", GINT_TO_POINTER(TRUE), test_render);
  return g_test_run();
}
