#include "gworld-scene-view.h"
#include "gworld-scene-view-shaders-private.h"
#include "gworld-scene-render-private.h"
#include "gworld-scene-atmosphere-private.h"
#include "gworld-scene-water-private.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "stalled-tile-server-private.h"

#include <glib/gstdio.h>
#include <filesystem>
#include <cstring>
#include <functional>
#include <vector>

namespace {

bool
wait_until(const std::function<bool()> &ready)
{
  const gint64 deadline = g_get_monotonic_time() + 15 * G_USEC_PER_SEC;
  while (g_get_monotonic_time() < deadline) {
    // Drain completion callbacks before an expensive software-rendered probe.
    for (int i = 0; i < 100 && g_main_context_pending(nullptr); ++i)
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
render_frame(GtkGLArea *area, const std::function<void()> &draw = {},
             GLenum format = GL_RGBA8, int inset = 0)
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
  glTexImage2D(GL_TEXTURE_2D, 0, format, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
  glGenRenderbuffers(1, &depth);
  glBindRenderbuffer(GL_RENDERBUFFER, depth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, size, size);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
  g_assert_cmpuint(glCheckFramebufferStatus(GL_FRAMEBUFFER), ==, GL_FRAMEBUFFER_COMPLETE);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[1]);
  glViewport(inset, inset, size - inset * 2, size - inset * 2);

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
  g_assert_cmpint(viewport[0], ==, inset);
  g_assert_cmpint(viewport[1], ==, inset);
  g_assert_cmpint(viewport[2], ==, size - inset * 2);
  g_assert_cmpint(viewport[3], ==, size - inset * 2);
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

std::vector<unsigned char> read_widget_frame(GtkGLArea *area)
{
  gtk_gl_area_make_current(area);
  gtk_gl_area_attach_buffers(area);
  GLint draw = 0, read = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, draw);
  const int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
#if GWORLD_SCENE_GTK_MAJOR == 4
  const int width = gtk_widget_get_width(GTK_WIDGET(area)) * scale;
  const int height = gtk_widget_get_height(GTK_WIDGET(area)) * scale;
#else
  const int width = gtk_widget_get_allocated_width(GTK_WIDGET(area)) * scale;
  const int height = gtk_widget_get_allocated_height(GTK_WIDGET(area)) * scale;
#endif
  std::vector<unsigned char> pixels(width * height * 4);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
  glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
  g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);
  return pixels;
}

int changed_pixels(const std::vector<unsigned char> &a, const std::vector<unsigned char> &b, int first_row = 0) {
  int count = 0;
  for (std::size_t i = first_row * 256 * 4; i < a.size(); i += 4)
    if (std::abs(int(a[i])-b[i]) + std::abs(int(a[i+1])-b[i+1]) + std::abs(int(a[i+2])-b[i+2]) > 8) ++count;
  return count;
}

void check_water_mask_priority(GtkGLArea *area) {
  const GLuint program = gworld_scene_view_create_program();
  GLuint vao = 0, vbo = 0, masks[2] = {};
  glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo); glGenTextures(2,masks);
  auto draw = [&](bool enabled, unsigned char water, unsigned char known) {
    return render_frame(area,[&]() {
      glUseProgram(program);
      const glm::mat4 identity(1);
      glUniformMatrix4fv(glGetUniformLocation(program,"mvp"),1,GL_FALSE,glm::value_ptr(identity));
      glUniform3f(glGetUniformLocation(program,"sun_direction"),0,1,0);
      glUniform3f(glGetUniformLocation(program,"ambient_color"),1,1,1);
      glUniform1f(glGetUniformLocation(program,"ambient_strength"),0.5);
      glUniform3f(glGetUniformLocation(program,"direct_light_color"),1,1,1);
      glUniform1f(glGetUniformLocation(program,"sun_strength"),0.5);
      glUniform3f(glGetUniformLocation(program,"camera_position"),0,10,10);
      glUniform3f(glGetUniformLocation(program,"planet_center"),0,-6371000,0);
      glUniform1i(glGetUniformLocation(program,"water_enabled"),enabled);
      for (int i=0;i<2;++i) {
        const std::string prefix = i ? "water_far" : "water_near";
        glUniform1i(glGetUniformLocation(program,(prefix+"_valid").c_str()),TRUE);
        glUniform1i(glGetUniformLocation(program,(prefix+"_texture").c_str()),7+i);
        glUniform4f(glGetUniformLocation(program,(prefix+"_range").c_str()),0,0,0,0);
        glUniform2f(glGetUniformLocation(program,(prefix+"_size").c_str()),1,1);
        glActiveTexture(GL_TEXTURE7+i); glBindTexture(GL_TEXTURE_2D,masks[i]);
        const unsigned char value[] = {i ? static_cast<unsigned char>(255) : water, i ? static_cast<unsigned char>(255) : known};
        glTexImage2D(GL_TEXTURE_2D,0,GL_RG8,1,1,0,GL_RG,GL_UNSIGNED_BYTE,value);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
      }
      const float vertices[] = {-1,-1,0,1,-1,0,-1,1,0,1,1,0};
      glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
      glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,nullptr);
      glVertexAttrib2f(1,0,0); // Longitude/latitude at the center of all masks.
      glVertexAttrib3f(5,0,1,0); glVertexAttrib4f(6,0.2,0.6,0.2,1); glVertexAttrib1f(7,2);
      glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
      glDrawArrays(GL_TRIANGLE_STRIP,0,4);
      glBindVertexArray(0); glUseProgram(0);
    });
  };
  const auto dry = draw(false,0,0);
  const auto missing_detail = draw(true,0,0);
  const auto known_land = draw(true,0,255);
  const auto known_water = draw(true,255,255);
  g_assert_cmpint(changed_pixels(dry,missing_detail), >, 10000);
  g_assert_true(dry == known_land);
  g_assert_true(missing_detail == known_water);
  glDeleteTextures(2,masks); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao); glDeleteProgram(program);
}

void check_imported_material(GWorldSceneView *view, const char *cache)
{
  // Minimal glTF quad with explicit metal/roughness factors and no textures.
  const float vertices[] = {-30,0,-30, 30,0,-30, -30,0,30, 30,0,30,
                              0,1,0, 0,1,0, 0,1,0, 0,1,0};
  std::vector<unsigned char> bytes;
  for (float v : vertices) {
    guint32 value; std::memcpy(&value, &v, sizeof value); value = GUINT32_TO_LE(value);
    const auto *p = reinterpret_cast<const unsigned char *>(&value); bytes.insert(bytes.end(),p,p+4);
  }
  for (guint16 index : {0,2,1,1,2,3}) {
    index = GUINT16_TO_LE(index);
    const auto *p = reinterpret_cast<const unsigned char *>(&index); bytes.insert(bytes.end(),p,p+2);
  }
  g_autofree char *base64 = g_base64_encode(bytes.data(),bytes.size());
  const std::string json = std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],
    "buffers":[{"byteLength":108,"uri":"data:application/octet-stream;base64,)") + base64 + R"("}],
    "bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":12}],
    "accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3","min":[-30,0,-30],"max":[30,0,30]},
                 {"bufferView":1,"componentType":5126,"count":4,"type":"VEC3"},{"bufferView":2,"componentType":5123,"count":6,"type":"SCALAR"}],
    "materials":[{"pbrMetallicRoughness":{"baseColorFactor":[0.5,0.5,0.5,1],"roughnessFactor":0.2,"metallicFactor":1}}],
    "meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":2,"material":0}]}]})";
  const std::string path = std::string(cache) + "/material.gltf";
  g_assert_true(g_file_set_contents(path.c_str(),json.c_str(),json.size(),nullptr));
  auto *area = GTK_GL_AREA(view);
  const auto ground = render_frame(area);
  auto *node = GWORLD_SCENE_NODE(gworld_scene_view_add_model(view,path.c_str(),0.5,0.5,900));
  std::vector<unsigned char> imported;
  g_assert_true(wait_until([&]() { imported = render_frame(area); return changed_pixels(ground,imported) > 2000; }));
  gworld_scene_node_set_roughness(node,0.8); gworld_scene_node_set_metallic(node,0);
  g_assert_true(wait_until([&]() { return changed_pixels(imported,render_frame(area)) > 2000; }));
  // Explicit overrides matching the file must reproduce the imported result.
  gworld_scene_node_set_roughness(node,0.2); gworld_scene_node_set_metallic(node,1);
  g_assert_true(wait_until([&]() { return changed_pixels(imported,render_frame(area)) == 0; }));
  gworld_scene_view_remove_node(view,node);
  g_assert_true(wait_until([&]() { return changed_pixels(ground,render_frame(area)) == 0; }));
}

void check_atmosphere(GtkGLArea *area) {
  const GLuint program = gworld_scene_view_create_sky_program();
  GLuint vao = 0, vbo = 0;
  glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
  gworld_scene::LinearRenderTarget target;
  auto sky = [&](float altitude, glm::vec3 sun, float density) {
    return render_frame(area, [&]() {
      g_assert_true(target.begin());
      const glm::vec3 camera(0,altitude,0);
      const glm::mat4 projection = glm::perspective(glm::radians(45.0f),1.0f,1.0f,1000000.0f);
      const glm::mat4 inverse = glm::inverse(projection * glm::lookAt(camera,camera+glm::vec3(0,1,0),glm::vec3(0,0,-1)));
      const auto frame = gworld_scene::atmosphere_frame(0,0);
      glUseProgram(program);
      glUniformMatrix4fv(glGetUniformLocation(program,"inverse_mvp"),1,GL_FALSE,glm::value_ptr(inverse));
      glUniform3fv(glGetUniformLocation(program,"camera_position"),1,glm::value_ptr(camera));
      glUniform3fv(glGetUniformLocation(program,"sun_direction"),1,glm::value_ptr(sun));
      glUniform3fv(glGetUniformLocation(program,"planet_center"),1,glm::value_ptr(frame.planet_center));
      glUniformMatrix3fv(glGetUniformLocation(program,"atmosphere_from_scene"),1,GL_FALSE,glm::value_ptr(frame.from_scene));
      glUniform1i(glGetUniformLocation(program,"atmosphere_enabled"),TRUE);
      glUniform1f(glGetUniformLocation(program,"atmosphere_density"),density);
      glUniform1f(glGetUniformLocation(program,"atmosphere_haze"),1);
      const float vertices[] = {-1,-1,1,-1,-1,1,1,1};
      glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
      glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STATIC_DRAW);
      glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,nullptr);
      glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND);
      glDrawArrays(GL_TRIANGLE_STRIP,0,4);
      glBindVertexArray(0); glUseProgram(0);
      target.present();
    });
  };
  const auto day = sky(1000,{0,0.8f,0.6f},1);
  const auto night = sky(1000,{0,-1,0},1);
  const auto space = sky(200000,{0,1,0},1);
  const auto vacuum = sky(1000,{0,1,0},0);
  const int center = (128 * 256 + 128) * 4;
  g_test_message("Sky day=%u,%u,%u night=%u,%u,%u space=%u,%u,%u", day[center],day[center+1],day[center+2],
    night[center],night[center+1],night[center+2],space[center],space[center+1],space[center+2]);
  g_assert_cmpint(day[center+2], >, day[center]);
  g_assert_cmpint(day[center+2], >, 60);
  for (int c=0;c<3;++c) {
    g_assert_cmpint(night[center+c], <, 25);
    g_assert_cmpint(space[center+c], <=, 1);
    g_assert_cmpint(vacuum[center+c], <=, 1);
  }
  target.destroy(); glDeleteProgram(program); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
}

void check_water_loading(GtkGLArea *area, StalledTileServer &server, const char *cache) {
  gworld_scene::WaterTiles tiles;
  const auto source = server.base + "water/sea/{z}/{x}/{y}.pbf";
  auto update = [&](const std::string &url, double longitude = 179.99) {
    gtk_gl_area_make_current(area);
    tiles.update(GTK_WIDGET(area),url,cache,true,0,longitude,1000,60000);
    g_assert_cmpuint(glGetError(), ==, GL_NO_ERROR);
  };
  g_assert_cmpuint(server.water_requests, ==, 0); // disabled view issued none
  g_assert_true(wait_until([&]() { update(source); return tiles.near_atlas.has_water && tiles.mid_atlas.has_water && tiles.far_atlas.has_water; }));
  g_assert_cmpuint(server.water_requests, >, 0);
  // Dateline wrapping keeps the same decoded tile set and does not blank it.
  update(source,-179.99);
  g_assert_true(tiles.near_atlas.has_water);
  // Finish all tiles, then cancel/recreate from disk with the provider offline.
  g_assert_true(wait_until([&]() { update(source); return tiles.ready(); }));
  tiles.cancel();
  const unsigned requests = server.water_requests;
  server.water_failed = true;
  g_assert_true(wait_until([&]() { update(source); return tiles.near_atlas.has_water && tiles.mid_atlas.has_water && tiles.far_atlas.has_water; }));
  // Tiles already decoded before cancellation must survive an outage.
  g_assert_cmpuint(server.water_requests, ==, requests);
  server.water_failed = false;
  const auto land = server.base + "water/land/{z}/{x}/{y}.pbf";
  update(land);
  g_assert_false(tiles.near_atlas.has_water); // old provider cleared immediately
  g_assert_true(wait_until([&]() { update(land); return tiles.ready(); }));
  g_assert_false(tiles.near_atlas.has_water);
  tiles.destroy_gl();
  g_assert_cmpuint(tiles.near_atlas.texture, ==, 0);
}

void
check_color_pipeline(GtkGLArea *area)
{
  gworld_scene::LinearRenderTarget target;
  const bool srgb_control = epoxy_is_desktop_gl() || epoxy_has_gl_extension("GL_EXT_sRGB_write_control");
  const bool original_srgb = srgb_control && glIsEnabled(GL_FRAMEBUFFER_SRGB);
  for (GLenum format : {GL_RGBA8, GL_SRGB8_ALPHA8}) {
    for (bool enabled : {false, true}) {
      if (srgb_control) {
        if (enabled) glEnable(GL_FRAMEBUFFER_SRGB);
        else glDisable(GL_FRAMEBUFFER_SRGB);
      }
      const auto pixels = render_frame(area, [&]() {
        g_assert_true(target.begin());
        glClearColor(0.5f, 0.25f, 0.04f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        target.present();
        if (srgb_control) g_assert_cmpint(glIsEnabled(GL_FRAMEBUFFER_SRGB), ==, enabled);
      }, format, 7);
      const auto *center = pixels.data() + (128 * 256 + 128) * 4;
      // Linear light -> display sRGB, exactly once, regardless of GTK's FBO.
      for (int c = 0; c < 3; ++c) {
        const int expected[] = {188, 137, 56};
        g_assert_cmpint(std::abs(static_cast<int>(center[c]) - expected[c]), <=, 2);
      }
    }
  }
  const auto highlights = render_frame(area, [&]() {
    g_assert_true(target.begin());
    glClearColor(1.0f, 0.5f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    target.present();
  });
  const auto *highlight = highlights.data() + (128 * 256 + 128) * 4;
  g_assert_cmpint(highlight[0], >=, 245);
  g_assert_cmpint(highlight[0], <=, 248);
  g_assert_cmpint(highlight[1], >=, 180);
  g_assert_cmpint(highlight[1], <=, 183);
  bool floating_point = false;
  const auto hdr = render_frame(area, [&]() {
    g_assert_true(target.begin());
    GLint component_type = 0;
    glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                           GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE, &component_type);
    floating_point = component_type == GL_FLOAT;
    g_test_message("Linear scene attachment: %s", floating_point ? "RGBA16F" : "RGBA8");
    glClearColor(2.0f, 0.5f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    target.present();
  });
  const auto *hdr_pixel = hdr.data() + (128 * 256 + 128) * 4;
  // Above-white input retains its channel ratios until tone mapping on HDR
  // targets; the portable RGBA8 fallback necessarily clips before that pass.
  g_assert_cmpint(std::abs(static_cast<int>(hdr_pixel[1]) - (floating_point ? 137 : 181)), <=, 2);
  target.destroy();
  if (srgb_control) {
    if (original_srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    else glDisable(GL_FRAMEBUFFER_SRGB);
  }
}

void
check_texture_filtering(GtkGLArea *area)
{
  // A 512-square checkerboard minified to 256 must average in linear light.
  // A translucent white quad over black must produce the same display value.
  const GLuint program = gworld_scene_view_create_billboard_program();
  GLuint vao = 0, vbo = 0, texture = 0;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenTextures(1, &texture);
  std::vector<unsigned char> texels(512 * 512 * 4, 255);
  for (int y = 0; y < 512; ++y) {
    for (int x = 0; x < 512; ++x) {
      for (int c = 0; c < 3; ++c)
        texels[(y * 512 + x) * 4 + c] = (x + y) % 2 ? 255 : 0;
    }
  }
  gworld_scene::LinearRenderTarget target;
  const float vertices[] = {-1,-1,0,0,0, 1,-1,0,1,0, -1,1,0,0,1, 1,1,0,1,1};
  const float identity[] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  for (bool blend : {false, true}) {
    const auto pixels = render_frame(area, [&]() {
      g_assert_true(target.begin());
      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture);
      if (blend) std::fill(texels.begin(), texels.end(), 255);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels.data());
      gworld_scene::configure_imagery_sampler();
      glGenerateMipmap(GL_TEXTURE_2D);
      glUseProgram(program);
      glUniformMatrix4fv(glGetUniformLocation(program, "mvp"), 1, GL_FALSE, identity);
      glUniform1i(glGetUniformLocation(program, "billboard_texture"), 0);
      glUniform1f(glGetUniformLocation(program, "opacity"), blend ? 0.5f : 1.0f);
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), nullptr);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(3 * sizeof(float)));
      glDisable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glDisable(GL_BLEND);
      glBindVertexArray(0);
      glUseProgram(0);
      target.present();
    });
    for (int x : {32, 128, 224}) {
      const auto *pixel = pixels.data() + (128 * 256 + x) * 4;
      for (int c = 0; c < 3; ++c)
        g_assert_cmpint(std::abs(static_cast<int>(pixel[c]) - 188), <=, 2);
    }
  }
  target.destroy();
  glDeleteTextures(1, &texture);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
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
  for (bool partial : {false, true}) {
    std::vector<unsigned char> colors = {255, 0, 0, static_cast<unsigned char>(partial ? 128 : 255), 0, 255, 0,
                                        static_cast<unsigned char>(partial ? 0 : 255)};
    gworld_scene::premultiply_imagery(colors);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, colors.data());
    gworld_scene::configure_imagery_sampler();
    glGenerateMipmap(GL_TEXTURE_2D);
    // Exact texel selection is needed for the longitude assertions below.
    if (epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic") ||
        epoxy_has_gl_extension("GL_ARB_texture_filter_anisotropic"))
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
    // Force the averaged level: missing green must leave saturated red, not a
    // dark fringe. This also exercises linear-space CPU premultiplication.
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, partial ? 1.0f : 0.0f);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, partial ? 1.0f : 0.0f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
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
        } else if (partial) {
          g_assert_cmpuint(pixel[0], >=, 250);
          g_assert_cmpuint(pixel[1], ==, 0);
          g_assert_cmpuint(pixel[2], ==, 0);
        } else {
          g_assert_cmpuint(pixel[x < 128 ? 0 : 1], ==, 255);
          g_assert_cmpuint(pixel[x < 128 ? 1 : 0], ==, 0);
        }
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
    const gint64 deadline = g_get_monotonic_time() + 15 * G_USEC_PER_SEC;
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
  // Keep the download/occlusion checks independent of atmospheric color shifts.
  // The atmosphere is exercised separately below.
  gworld_scene_view_set_atmosphere_enabled(view, FALSE);
  gworld_scene_view_set_shadows_enabled(view, TRUE);
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

  // Observe GTK's own frames, without invoking render ourselves. A staged
  // atlas upload must finish while the camera is stationary and water is off.
  g_assert_true(wait_until([&]() { return count_color_pixels(read_widget_frame(area), 1) > 500; }));

  // Check every shader, including programs whose geometry is absent in this scene.
  for (auto create : {gworld_scene_view_create_program, gworld_scene_view_create_shadow_program,
                      gworld_scene_view_create_billboard_program, gworld_scene_view_create_sun_program,
                      gworld_scene_view_create_sky_program, gworld_scene_view_create_present_program}) {
    GLuint program = create();
    g_assert_cmpuint(program, !=, 0);
    glDeleteProgram(program);
  }
  g_assert_true(wait_until([&]() {
    const auto pixels = render_frame(area);
    return count_color_pixels(pixels, 0) > 100 && count_color_pixels(pixels, 1) > 1000;
  }));

  const auto with_shadow = render_frame(area);
  gworld_scene_view_set_shadows_enabled(view, FALSE);
  const auto without_shadow = render_frame(area);
  g_assert_cmpint(changed_pixels(with_shadow, without_shadow), >, 20);
  gworld_scene_node_set_roughness(GWORLD_SCENE_NODE(cube), 0.15);
  gworld_scene_node_set_metallic(GWORLD_SCENE_NODE(cube), 1.0);
  g_assert_true(wait_until([&]() { return changed_pixels(without_shadow, render_frame(area)) > 100; }));
  gworld_scene_node_set_roughness(GWORLD_SCENE_NODE(cube), -1);
  gworld_scene_node_set_metallic(GWORLD_SCENE_NODE(cube), -1);
  gworld_scene_view_set_shadows_enabled(view, TRUE);

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
  check_water_mask_priority(area);
  check_atmosphere(area);
  check_water_loading(area, server, cache_dir);
  gworld_scene_view_set_shadows_enabled(view, FALSE);
  gworld_scene_view_set_water_wave_strength(view, 0);
  const auto dry = render_frame(area);
  gworld_scene_view_set_water_tile_url_template(view, (server.base + "water/sea/{z}/{x}/{y}.pbf").c_str());
  gworld_scene_view_set_water_enabled(view, TRUE);
  // Ignore the footer: differences must come from the rendered surface.
  g_assert_true(wait_until([&]() { return changed_pixels(dry, render_frame(area), 128) > 1000; }));
  gworld_scene_view_set_water_tile_url_template(view, (server.base + "water/land/{z}/{x}/{y}.pbf").c_str());
  g_assert_true(wait_until([&]() { return changed_pixels(dry, render_frame(area), 128) == 0; }));
  gworld_scene_view_set_water_enabled(view, FALSE);
  g_assert_true(dry == render_frame(area));
  gworld_scene_view_set_atmosphere_enabled(view, TRUE);
  gworld_scene_view_set_atmosphere_density(view, 4);
  gworld_scene_view_set_atmosphere_haze(view, 2);
  g_assert_cmpint(changed_pixels(dry, render_frame(area)), >, 1000);
  gworld_scene_view_set_atmosphere_density(view, 0);
  g_assert_cmpint(changed_pixels(dry, render_frame(area)), ==, 0);
  gworld_scene_view_set_atmosphere_enabled(view, FALSE);

  check_color_pipeline(area);
  check_texture_filtering(area);
  check_dateline_imagery(area);
  check_imagery_blending(area);
  check_imported_material(view, cache_dir);
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
