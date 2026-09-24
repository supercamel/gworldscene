#include "gworld-scene-render-private.h"
#include "gworld-scene-view-shaders-private.h"

#include <algorithm>
#include <cmath>

namespace gworld_scene {
float srgb_to_linear(float value)
{
  return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float value)
{
  return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

void premultiply_imagery(std::vector<unsigned char> &pixels)
{
  // Store sRGB-encoded, premultiplied *linear* RGB. Hardware decoding followed
  // by mipmap/anisotropic filtering then keeps missing tiles from darkening
  // their neighbours. Opaque satellite/map tiles need no conversion.
  for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
    if (pixels[i + 3] == 255) continue;
    const float alpha = pixels[i + 3] / 255.0f;
    for (int c = 0; c < 3; ++c) {
      const float linear = srgb_to_linear(pixels[i + c] / 255.0f) * alpha;
      const float encoded = linear_to_srgb(linear);
      pixels[i + c] = static_cast<unsigned char>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
    }
  }
}

void configure_imagery_sampler()
{
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic") ||
      epoxy_has_gl_extension("GL_ARB_texture_filter_anisotropic")) {
    GLfloat maximum = 1.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximum);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maximum));
  }
}

std::size_t mipmap_storage_bytes(int width, int height)
{
  if (width <= 0 || height <= 0) return 0;
  std::size_t bytes = 0;
  for (;;) {
    bytes += static_cast<std::size_t>(width) * height * 4;
    if (width == 1 && height == 1) return bytes;
    width = std::max(1, width / 2);
    height = std::max(1, height / 2);
  }
}

bool LinearRenderTarget::begin()
{
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &destination);
  glGetIntegerv(GL_VIEWPORT, viewport);
  if (viewport[2] <= 0 || viewport[3] <= 0) return false;
  if (!program) program = gworld_scene_view_create_present_program();
  if (!program) return false;
  if (!vao) glGenVertexArrays(1, &vao);
  if (!framebuffer) glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
  if (width != viewport[2] || height != viewport[3]) {
    GLint previous_texture = 0, previous_renderbuffer = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
    glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous_renderbuffer);
    if (!texture) glGenTextures(1, &texture);
    if (!depth) glGenRenderbuffers(1, &depth);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    const bool half_float = epoxy_is_desktop_gl() ||
      epoxy_has_gl_extension("GL_EXT_color_buffer_half_float") ||
      epoxy_has_gl_extension("GL_EXT_color_buffer_float");
    glTexImage2D(GL_TEXTURE_2D, 0, half_float ? GL_RGBA16F : GL_RGBA8,
                 viewport[2], viewport[3], 0, GL_RGBA, half_float ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, viewport[2], viewport[3]);
    glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    const bool complete = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
    glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(previous_renderbuffer));
    if (!complete) {
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(destination));
      return false;
    }
    width = viewport[2];
    height = viewport[3];
  }
  glViewport(0, 0, width, height);
  return true;
}

void LinearRenderTarget::present()
{
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(destination));
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  GLint encoding = GL_LINEAR;
  const GLenum attachment = destination ? GL_COLOR_ATTACHMENT0 :
    (epoxy_is_desktop_gl() ? GL_BACK_LEFT : GL_BACK);
  glGetFramebufferAttachmentParameteriv(GL_DRAW_FRAMEBUFFER, attachment,
                                         GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING, &encoding);
  // GLES 3 automatically encodes sRGB attachments. Desktop GL and GLES with
  // write-control expose a switch; preserve GTK's setting across this pass.
  const bool srgb_control = epoxy_is_desktop_gl() || epoxy_has_gl_extension("GL_EXT_sRGB_write_control");
  const bool srgb_enabled = srgb_control && glIsEnabled(GL_FRAMEBUFFER_SRGB);
  if (srgb_control) glEnable(GL_FRAMEBUFFER_SRGB);
  const bool depth_enabled = glIsEnabled(GL_DEPTH_TEST);
  const bool blend_enabled = glIsEnabled(GL_BLEND);
  const bool cull_enabled = glIsEnabled(GL_CULL_FACE);
  GLint previous_program = 0, previous_vao = 0, active = 0, previous_texture = 0;
  glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  glActiveTexture(GL_TEXTURE0);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glDisable(GL_CULL_FACE);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "scene_texture"), 0);
  glUniform1i(glGetUniformLocation(program, "encode_srgb"), encoding != GL_SRGB);
  glBindTexture(GL_TEXTURE_2D, texture);
  glBindVertexArray(vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(static_cast<GLuint>(previous_vao));
  glUseProgram(static_cast<GLuint>(previous_program));
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_texture));
  glActiveTexture(static_cast<GLenum>(active));
  if (depth_enabled) glEnable(GL_DEPTH_TEST);
  if (blend_enabled) glEnable(GL_BLEND);
  if (cull_enabled) glEnable(GL_CULL_FACE);
  if (srgb_control && !srgb_enabled) glDisable(GL_FRAMEBUFFER_SRGB);
}

void LinearRenderTarget::destroy()
{
  if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
  if (texture) glDeleteTextures(1, &texture);
  if (depth) glDeleteRenderbuffers(1, &depth);
  if (program) glDeleteProgram(program);
  if (vao) glDeleteVertexArrays(1, &vao);
  *this = LinearRenderTarget();
}
} // namespace gworld_scene
