#include "gworld-scene-geo-private.h"

#include <algorithm>
#include <cmath>

namespace gworld_scene {

namespace {

constexpr double kWgs84F = 1.0 / 298.257223563;
constexpr double kWgs84B = kWgs84A * (1.0 - kWgs84F);
constexpr double kWgs84E2 = 1.0 - (kWgs84B * kWgs84B) / (kWgs84A * kWgs84A);

} // namespace

double
deg_to_rad(double degrees)
{
  return degrees * 3.14159265358979323846 / 180.0;
}

glm::dvec3
geodetic_to_ecef(double lat_deg, double lon_deg, double h)
{
  const double lat = deg_to_rad(lat_deg);
  const double lon = deg_to_rad(lon_deg);
  const double sin_lat = std::sin(lat);
  const double cos_lat = std::cos(lat);
  const double sin_lon = std::sin(lon);
  const double cos_lon = std::cos(lon);
  const double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sin_lat * sin_lat);

  return glm::dvec3((n + h) * cos_lat * cos_lon,
                    (n + h) * cos_lat * sin_lon,
                    (n * (1.0 - kWgs84E2) + h) * sin_lat);
}

glm::dvec3
geodetic_to_scene(double lat_deg,
                  double lon_deg,
                  double h,
                  double ref_lat_deg,
                  double ref_lon_deg,
                  double ref_h)
{
  const glm::dvec3 ref_ecef = geodetic_to_ecef(ref_lat_deg, ref_lon_deg, ref_h);
  const glm::dvec3 ecef = geodetic_to_ecef(lat_deg, lon_deg, h);
  const glm::dvec3 d = ecef - ref_ecef;

  const double lat0 = deg_to_rad(ref_lat_deg);
  const double lon0 = deg_to_rad(ref_lon_deg);
  const double sin_lat = std::sin(lat0);
  const double cos_lat = std::cos(lat0);
  const double sin_lon = std::sin(lon0);
  const double cos_lon = std::cos(lon0);

  const double east = -sin_lon * d.x + cos_lon * d.y;
  const double north = -sin_lat * cos_lon * d.x - sin_lat * sin_lon * d.y + cos_lat * d.z;
  const double up = cos_lat * cos_lon * d.x + cos_lat * sin_lon * d.y + sin_lat * d.z;

  return glm::dvec3(east, up, -north);
}

glm::dvec3
ned_to_scene_vector(const glm::dvec3 &ned)
{
  return glm::dvec3(ned.y, -ned.z, -ned.x);
}

void
translate_geodetic_ned(double latitude,
                       double longitude,
                       double altitude_amsl,
                       double north_m,
                       double east_m,
                       double down_m,
                       double *translated_latitude,
                       double *translated_longitude,
                       double *translated_altitude_amsl)
{
  const double lon_scale = std::max(0.05, std::cos(deg_to_rad(latitude)));

  if (translated_latitude)
    *translated_latitude = std::clamp(latitude + north_m / kEarthMetersPerDegree, -90.0, 90.0);
  if (translated_longitude)
    *translated_longitude = std::clamp(longitude + east_m / (kEarthMetersPerDegree * lon_scale), -180.0, 180.0);
  if (translated_altitude_amsl)
    *translated_altitude_amsl = altitude_amsl - down_m;
}

} // namespace gworld_scene
