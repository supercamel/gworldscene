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

TileRange
fit_range_to_texture_limit(TileRange range, int max_tiles_per_axis)
{
  const int limit = std::max(1, max_tiles_per_axis);
  while (range.z > 0 &&
         (range.width_tiles() > limit || range.height_tiles() > limit)) {
    --range.z;
    range.x_min = static_cast<int>(std::floor(range.x_min / 2.0));
    range.x_max = static_cast<int>(std::floor(range.x_max / 2.0));
    range.y_min /= 2;
    range.y_max /= 2;
    if (range.width_tiles() >= (1 << range.z)) {
      range.x_min = 0;
      range.x_max = (1 << range.z) - 1;
    }
  }
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
wrap_tile_x(int x, int zoom)
{
  const int count = 1 << zoom;
  return ((x % count) + count) % count;
}

TileRange
tile_range_for_bounds(double min_latitude, double max_latitude,
                       double min_longitude, double max_longitude, int zoom)
{
  const int count = 1 << zoom;
  TileRange range;
  range.z = zoom;
  range.x_min = static_cast<int>(std::floor(slippy_tile_x_for_longitude(min_longitude, zoom)));
  range.x_max = static_cast<int>(std::floor(slippy_tile_x_for_longitude(max_longitude, zoom)));
  if (range.x_max - range.x_min + 1 >= count) {
    range.x_min = 0;
    range.x_max = count - 1;
  }
  range.y_min = std::clamp(static_cast<int>(std::floor(slippy_tile_y_for_latitude(max_latitude, zoom))), 0, count - 1);
  range.y_max = std::clamp(static_cast<int>(std::floor(slippy_tile_y_for_latitude(min_latitude, zoom))), 0, count - 1);
  return range;
}

TerrainImageryPlan
terrain_imagery_plan(double latitude, double longitude, double altitude_agl,
                      double terrain_radius_m, int max_tiles, int max_pixels,
                      int max_ultra_tiles, double ultra_radius_m)
{
  const double latitude_scale = std::max(0.05, std::cos(deg_to_rad(latitude)));
  const int max_axis = std::max(1, max_pixels / 256);
  const double radius = std::max(0.0, terrain_radius_m);
  auto range_for_radius = [&](double r, int zoom) {
    const double lat_delta = r / kEarthMetersPerDegree;
    const double lon_delta = std::min(180.0, lat_delta / latitude_scale);
    return tile_range_for_bounds(std::max(-90.0, latitude - lat_delta),
                                  std::min(90.0, latitude + lat_delta),
                                  longitude - lon_delta, longitude + lon_delta, zoom);
  };
  auto fits = [&](const TileRange &range, int tiles) {
    return range.width_tiles() <= max_axis && range.height_tiles() <= max_axis &&
           range.width_tiles() * range.height_tiles() <= std::max(1, tiles);
  };
  auto band = [&](int zoom, double desired_radius, int tiles) {
    const int axis = std::min(max_axis, static_cast<int>(std::sqrt(std::max(1, tiles))));
    const double meters_per_tile = 360.0 * kEarthMetersPerDegree * latitude_scale / (1 << zoom);
    double upper = std::min(desired_radius, meters_per_tile * std::max(0.5, axis - 1.0) * 0.5);
    double lower = 0.0;
    // Mercator's north/south scale varies across a large band. Search coverage
    // at this zoom rather than dropping the entire band to a coarser level.
    for (int i = 0; i < 24; ++i) {
      const double candidate = (lower + upper) * 0.5;
      if (fits(range_for_radius(candidate, zoom), tiles))
        lower = candidate;
      else
        upper = candidate;
    }
    return ImageryBand{range_for_radius(lower, zoom), lower};
  };

  int near_zoom = 18;
  if (altitude_agl >= 85000.0) near_zoom = 13;
  else if (altitude_agl >= 32000.0) near_zoom = 14;
  else if (altitude_agl >= 14000.0) near_zoom = 15;
  else if (altitude_agl >= 6500.0) near_zoom = 16;
  else if (altitude_agl >= 1800.0) near_zoom = 17;

  TerrainImageryPlan plan;
  const double agl = std::max(0.0, altitude_agl);
  if (ultra_radius_m > agl) {
    const double lateral = std::sqrt(ultra_radius_m * ultra_radius_m - agl * agl);
    plan.ultra = band(near_zoom, std::min(radius, lateral), max_ultra_tiles);
  }
  plan.detail = band(near_zoom - 2, radius, max_tiles);
  plan.mid = band(near_zoom - 4, radius, max_tiles);
  plan.far = band(near_zoom - 6, radius, max_tiles);
  for (int zoom = near_zoom - 8; zoom >= 0; --zoom) {
    plan.base = {range_for_radius(radius, zoom), radius};
    if (fits(plan.base.range, max_tiles))
      break;
  }
  return plan;
}

int
globe_texture_zoom_for_altitude(double altitude_amsl)
{
  if (altitude_amsl < 120000.0)
    return 8;
  if (altitude_amsl < 300000.0)
    return 7;
  if (altitude_amsl < 900000.0)
    return 6;
  if (altitude_amsl < 6000000.0)
    return 5;
  return 4;
}

int
globe_texture_half_span_for_zoom(int zoom)
{
  if (zoom >= 8)
    return 2;
  if (zoom == 7)
    return 3;
  if (zoom == 6)
    return 5;
  return 7;
}

TileRange
globe_texture_range_for_camera(double latitude,
                               double longitude,
                               double altitude_amsl,
                               int max_tiles_per_axis)
{
  const int zoom = globe_texture_zoom_for_altitude(altitude_amsl);
  if (zoom <= 4)
    return fit_range_to_texture_limit(full_world_range(zoom), max_tiles_per_axis);

  const int tile_count = 1 << zoom;
  const int center_x = std::clamp(static_cast<int>(std::floor(slippy_tile_x_for_longitude(longitude, zoom))),
                                  0,
                                  tile_count - 1);
  const int center_y = std::clamp(static_cast<int>(std::floor(slippy_tile_y_for_latitude(latitude, zoom))),
                                  0,
                                  tile_count - 1);
  const int half_span = globe_texture_half_span_for_zoom(zoom);

  TileRange range;
  range.z = zoom;
  range.x_min = center_x - half_span;
  range.x_max = center_x + half_span;
  range.y_min = std::clamp(center_y - half_span, 0, tile_count - 1);
  range.y_max = std::clamp(center_y + half_span, 0, tile_count - 1);
  return fit_range_to_texture_limit(range, max_tiles_per_axis);
}

} // namespace gworld_scene
