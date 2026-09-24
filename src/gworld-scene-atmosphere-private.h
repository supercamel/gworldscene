#ifndef GWORLD_SCENE_ATMOSPHERE_PRIVATE_H
#define GWORLD_SCENE_ATMOSPHERE_PRIVATE_H

#include <glm/glm.hpp>

namespace gworld_scene {
// Map the WGS84 ellipsoid to a spherical atmosphere measured in kilometres.
// The scene uses local east/up/south metres around its current mesh origin.
struct AtmosphereFrame {
  glm::vec3 planet_center;
  glm::mat3 from_scene;
};
AtmosphereFrame atmosphere_frame(double origin_latitude, double origin_longitude);
} // namespace gworld_scene
#endif
