#include "gworld-scene-lod-private.h"

#include "gworld-scene-geo-private.h"

#include <algorithm>
#include <cmath>

namespace gworld_scene {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMercatorMaxLatitude = 85.05112878;

TileRange
full_world_range(int zoom)
{
  const int max_tile = (1 << zoom) - 1;
  TileRange range;
  range.z = zoom;
  range.x_min = 0;
  range.x_max = max_tile;
  range.y_min = 0;
  range.y_max = max_tile;
  return range;
}

} // namespace

bool
TileRange::valid() const
{
  return z >= 0 && x_min <= x_max && y_min <= y_max;
}

int
TileRange::width_tiles() const
{
  return valid() ? x_max - x_min + 1 : 0;
}

int
TileRange::height_tiles() const
{
  return valid() ? y_max - y_min + 1 : 0;
}

std::string
TileRange::key() const
{
  return std::to_string(z) + ":" + std::to_string(x_min) + ":" + std::to_string(x_max) +
         ":" + std::to_string(y_min) + ":" + std::to_string(y_max);
}

double
slippy_tile_x_for_longitude(double longitude_deg, int zoom)
{
  const double n = static_cast<double>(1 << zoom);
  return (longitude_deg + 180.0) / 360.0 * n;
}

double
slippy_tile_y_for_latitude(double latitude_deg, int zoom)
{
  const double clamped_lat = std::clamp(latitude_deg, -kMercatorMaxLatitude, kMercatorMaxLatitude);
  const double lat_rad = deg_to_rad(clamped_lat);
  const double n = static_cast<double>(1 << zoom);
  return (1.0 - std::asinh(std::tan(lat_rad)) / kPi) * 0.5 * n;
}

int
globe_texture_zoom_for_altitude(double altitude_amsl)
{
  if (altitude_amsl < 100000.0)
    return 7;
  if (altitude_amsl < 250000.0)
    return 6;
  if (altitude_amsl < 750000.0)
    return 5;
  if (altitude_amsl < 2500000.0)
    return 4;
  return 3;
}

TileRange
globe_texture_range_for_camera(double latitude,
                               double longitude,
                               double altitude_amsl)
{
  const int zoom = globe_texture_zoom_for_altitude(altitude_amsl);
  if (zoom <= 3)
    return full_world_range(zoom);

  const int tile_count = 1 << zoom;
  const int center_x = std::clamp(static_cast<int>(std::floor(slippy_tile_x_for_longitude(longitude, zoom))),
                                  0,
                                  tile_count - 1);
  const int center_y = std::clamp(static_cast<int>(std::floor(slippy_tile_y_for_latitude(latitude, zoom))),
                                  0,
                                  tile_count - 1);
  const int half_span = zoom >= 6 ? 2 : 3;

  TileRange range;
  range.z = zoom;
  range.x_min = std::clamp(center_x - half_span, 0, tile_count - 1);
  range.x_max = std::clamp(center_x + half_span, 0, tile_count - 1);
  range.y_min = std::clamp(center_y - half_span, 0, tile_count - 1);
  range.y_max = std::clamp(center_y + half_span, 0, tile_count - 1);
  return range;
}

} // namespace gworld_scene
