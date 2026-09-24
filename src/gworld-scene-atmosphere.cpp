#include "gworld-scene-atmosphere-private.h"
#include "gworld-scene-geo-private.h"
#include <cmath>

namespace gworld_scene {
AtmosphereFrame atmosphere_frame(double latitude, double longitude)
{
  constexpr double radius_km = 6371.0;
  constexpr double polar_radius = kWgs84A * (1.0 - 1.0 / 298.257223563);
  const double lat = deg_to_rad(latitude), lon = deg_to_rad(longitude);
  const glm::dvec3 east(-std::sin(lon), std::cos(lon), 0.0);
  const glm::dvec3 up(std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat));
  const glm::dvec3 south(std::sin(lat) * std::cos(lon), std::sin(lat) * std::sin(lon), -std::cos(lat));
  const glm::dmat3 to_ecef(east, up, south);
  const glm::dvec3 center = glm::transpose(to_ecef) * -geodetic_to_ecef(latitude, longitude, 0.0);
  const glm::dmat3 scale(glm::dvec3(radius_km / kWgs84A, 0, 0),
                         glm::dvec3(0, radius_km / kWgs84A, 0),
                         glm::dvec3(0, 0, radius_km / polar_radius));
  return {glm::vec3(center), glm::mat3(scale * to_ecef)};
}
} // namespace gworld_scene
