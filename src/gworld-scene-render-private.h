#ifndef GWORLD_SCENE_RENDER_PRIVATE_H
#define GWORLD_SCENE_RENDER_PRIVATE_H

#include <epoxy/gl.h>
#include <cstddef>
#include <vector>

namespace gworld_scene {
float srgb_to_linear(float value);
float linear_to_srgb(float value);
void premultiply_imagery(std::vector<unsigned char> &pixels);
void configure_imagery_sampler();
std::size_t mipmap_storage_bytes(int width, int height);

// GL resources are owned by the view's context and released on unrealize.
struct LinearRenderTarget {
  GLuint framebuffer = 0;
  GLuint texture = 0;
  GLuint depth = 0;
  GLuint program = 0;
  GLuint vao = 0;
  int width = 0;
  int height = 0;
  GLint destination = 0;
  GLint viewport[4] = {};
  bool begin();
  void present();
  void destroy();
};
} // namespace gworld_scene
#endif
