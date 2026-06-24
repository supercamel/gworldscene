#ifndef GWORLD_SCENE_LIGHT_PRIVATE_H
#define GWORLD_SCENE_LIGHT_PRIVATE_H

#include <glm/glm.hpp>

namespace gworld_scene {

struct SunPosition {
  double azimuth_deg = 0.0;
  double elevation_deg = 45.0;
};

glm::dvec3 sun_direction_from_position(double azimuth_deg, double elevation_deg);

glm::dvec3 sun_direction_from_time(double latitude_deg,
                                   double local_solar_hour,
                                   double declination_deg = 0.0);

SunPosition sun_position_from_direction(const glm::dvec3 &enu_direction);

} // namespace gworld_scene

#endif /* GWORLD_SCENE_LIGHT_PRIVATE_H */
