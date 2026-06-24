#include "gworld-scene-light-private.h"

#include "gworld-scene-geo-private.h"

#include <algorithm>
#include <cmath>

namespace gworld_scene {

namespace {

constexpr double kPi = 3.14159265358979323846;

double
rad_to_deg(double radians)
{
  return radians * 180.0 / kPi;
}

glm::dvec3
safe_normalize(const glm::dvec3 &value, const glm::dvec3 &fallback)
{
  const double length = glm::length(value);
  if (length <= 0.000001)
    return fallback;
  return value / length;
}

double
wrap_hour(double hour)
{
  double wrapped = std::fmod(hour, 24.0);
  if (wrapped < 0.0)
    wrapped += 24.0;
  return wrapped;
}

} // namespace

glm::dvec3
sun_direction_from_position(double azimuth_deg, double elevation_deg)
{
  const double azimuth_rad = deg_to_rad(azimuth_deg);
  const double elevation_rad = deg_to_rad(std::clamp(elevation_deg, -90.0, 90.0));
  const double horizontal = std::cos(elevation_rad);
  return safe_normalize(glm::dvec3(std::sin(azimuth_rad) * horizontal,
                                   std::cos(azimuth_rad) * horizontal,
                                   std::sin(elevation_rad)),
                        glm::dvec3(0.0, 0.0, 1.0));
}

glm::dvec3
sun_direction_from_time(double latitude_deg,
                        double local_solar_hour,
                        double declination_deg)
{
  const double latitude_rad = deg_to_rad(std::clamp(latitude_deg, -89.9, 89.9));
  const double declination_rad = deg_to_rad(std::clamp(declination_deg, -89.9, 89.9));
  const double hour_angle = deg_to_rad((wrap_hour(local_solar_hour) - 12.0) * 15.0);

  const double east = -std::cos(declination_rad) * std::sin(hour_angle);
  const double north = std::cos(latitude_rad) * std::sin(declination_rad) -
                       std::sin(latitude_rad) * std::cos(declination_rad) * std::cos(hour_angle);
  const double up = std::sin(latitude_rad) * std::sin(declination_rad) +
                    std::cos(latitude_rad) * std::cos(declination_rad) * std::cos(hour_angle);
  return safe_normalize(glm::dvec3(east, north, up), glm::dvec3(0.0, 0.0, 1.0));
}

SunPosition
sun_position_from_direction(const glm::dvec3 &enu_direction)
{
  const glm::dvec3 direction = safe_normalize(enu_direction, glm::dvec3(0.0, 0.0, 1.0));
  SunPosition position;
  position.azimuth_deg = rad_to_deg(std::atan2(direction.x, direction.y));
  if (position.azimuth_deg < 0.0)
    position.azimuth_deg += 360.0;
  position.elevation_deg = rad_to_deg(std::asin(std::clamp(direction.z, -1.0, 1.0)));
  return position;
}

} // namespace gworld_scene
