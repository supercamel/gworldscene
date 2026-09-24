#ifndef GWORLD_SCENE_SHADOW_PRIVATE_H
#define GWORLD_SCENE_SHADOW_PRIVATE_H

#include "gworld-scene-camera-private.h"
#include <array>

namespace gworld_scene {
constexpr int kShadowCascades = 3;
struct ShadowCascades {
  std::array<glm::mat4, kShadowCascades> matrices;
  glm::vec3 splits;
  glm::vec3 texel_meters;
};
ShadowCascades shadow_cascades(const CameraPose &camera, double aspect, double fov_y_radians,
                               double near_m, double far_m, const glm::dvec3 &sun,
                               int resolution);
} // namespace gworld_scene
#endif
