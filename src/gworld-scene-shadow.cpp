#include "gworld-scene-shadow-private.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace gworld_scene {
ShadowCascades shadow_cascades(const CameraPose &camera, double aspect, double fov_y,
                               double near_m, double far_m, const glm::dvec3 &sun,
                               int resolution)
{
  ShadowCascades result;
  near_m = std::max(1.0, near_m);
  far_m = std::max(near_m + 1.0, far_m);
  resolution = std::max(1, resolution);
  const glm::dvec3 forward = glm::normalize(camera.center - camera.eye);
  const glm::dvec3 right = glm::normalize(glm::cross(forward, camera.up));
  const glm::dvec3 up = glm::normalize(glm::cross(right, forward));
  const glm::dvec3 light = glm::normalize(sun);
  const glm::dvec3 reference_up = std::abs(light.y) > 0.9 ? glm::dvec3(0, 0, -1) : glm::dvec3(0, 1, 0);
  const glm::dvec3 light_right = glm::normalize(glm::cross(reference_up, light));
  const glm::dvec3 light_up = glm::cross(light, light_right);
  const double slope = std::tan(fov_y * 0.5);
  double previous = near_m;
  for (int i = 0; i < kShadowCascades; ++i) {
    const double fraction = static_cast<double>(i + 1) / kShadowCascades;
    const double split = 0.85 * near_m * std::pow(far_m / near_m, fraction) +
                         0.15 * (near_m + (far_m - near_m) * fraction);
    result.splits[i] = static_cast<float>(split);
    // Overlap the previous cascade so transitions can be blended.
    const double start = i == 0 ? previous : previous * 0.85;
    glm::dvec3 center = camera.eye + forward * (start + split) * 0.5;
    double radius = 0.0;
    for (double distance : {start, split})
      for (double x : {-1.0, 1.0})
        for (double y : {-1.0, 1.0}) {
          const glm::dvec3 point = camera.eye + forward * distance +
            (right * x * aspect + up * y) * distance * slope;
          radius = std::max(radius, glm::length(point - center));
        }
    // A rotation-invariant sphere and a snapped light-space origin prevent
    // sub-texel camera motion from making the shadow silhouette shimmer.
    radius = std::ceil(radius / 16.0) * 16.0 + 2.0;
    // Leave room for the light-space center to move while snapping.
    radius /= std::max(0.1, 1.0 - std::sqrt(2.0) / resolution);
    const double texel = 2.0 * radius / resolution;
    center += light_right * (std::round(glm::dot(center, light_right) / texel) * texel - glm::dot(center, light_right));
    center += light_up * (std::round(glm::dot(center, light_up) / texel) * texel - glm::dot(center, light_up));
    const double depth = radius * 4.0;
    const glm::dmat4 view = glm::lookAt(center + light * depth, center, light_up);
    result.matrices[i] = glm::mat4(glm::ortho(-radius, radius, -radius, radius, 0.0, depth * 2.0) * view);
    result.texel_meters[i] = static_cast<float>(texel);
    previous = split;
  }
  return result;
}
} // namespace gworld_scene
