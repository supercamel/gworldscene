#ifndef GWORLD_SCENE_GEO_PRIVATE_H
#define GWORLD_SCENE_GEO_PRIVATE_H

#include <glm/glm.hpp>

namespace gworld_scene {

constexpr double kEarthMetersPerDegree = 111320.0;
constexpr double kWgs84A = 6378137.0;

double deg_to_rad(double degrees);

glm::dvec3 geodetic_to_ecef(double lat_deg, double lon_deg, double h);

glm::dvec3 geodetic_to_scene(double lat_deg,
                             double lon_deg,
                             double h,
                             double ref_lat_deg,
                             double ref_lon_deg,
                             double ref_h);

glm::dvec3 ned_to_scene_vector(const glm::dvec3 &ned);

void translate_geodetic_ned(double latitude,
                            double longitude,
                            double altitude_amsl,
                            double north_m,
                            double east_m,
                            double down_m,
                            double *translated_latitude,
                            double *translated_longitude,
                            double *translated_altitude_amsl);

} // namespace gworld_scene

#endif /* GWORLD_SCENE_GEO_PRIVATE_H */
