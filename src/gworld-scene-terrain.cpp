#include "gworld-scene-terrain-private.h"
#include "gworld-scene-camera-private.h"
#include "gworld-scene-geo-private.h"
#include <algorithm>
#include <cmath>

namespace gworld_scene {
std::string terrain_key(int latitude, int longitude)
{
  return std::to_string(latitude) + "," + std::to_string(longitude);
}

int16_t
get_height_clamped(const TerrainTile &tile, int x, int y)
{
  x = std::clamp(x, 0, tile.dimension - 1);
  y = std::clamp(y, 0, tile.dimension - 1);
  return tile.heights[static_cast<std::size_t>(y * tile.dimension + x)];
}

int16_t
get_safe_height(const TerrainTile &tile, int x, int y)
{
  const int16_t h = get_height_clamped(tile, x, y);
  if (h != -32768)
    return h;

  for (int radius = 1; radius < 8; ++radius) {
    for (int oy = -radius; oy <= radius; ++oy) {
      for (int ox = -radius; ox <= radius; ++ox) {
        const int16_t candidate = get_height_clamped(tile, x + ox, y + oy);
        if (candidate != -32768)
          return candidate;
      }
    }
  }

  return 0;
}


namespace {
bool contains(const TerrainTile &tile, double lat, double lon)
{
  const double u = wrap_longitude(lon - tile.lon);
  return tile.dimension > 1 && !tile.heights.empty() &&
         lat >= tile.lat - 1e-9 && lat <= tile.lat + 1.0 + 1e-9 &&
         u >= -1e-9 && u <= 1.0 + 1e-9;
}

// At a shared edge either tile can provide the endpoint. Prefer the finer
// source consistently, including when only one of the neighbours is loaded.
const TerrainTile *tile_at(const TerrainTileMap &tiles, double lat, double lon)
{
  lon = wrap_longitude(lon);
  const int south = static_cast<int>(std::floor(lat));
  const int west = static_cast<int>(std::floor(lon));
  const TerrainTile *best = nullptr;
  for (int dy = 0; dy >= -1; --dy) {
    if (dy && lat - south > 1e-9) continue;
    for (int dx = 0; dx >= -1; --dx) {
      if (dx && lon - west > 1e-9) continue;
      const int longitude = static_cast<int>(wrap_longitude(west + dx));
      auto it = tiles.find(terrain_key(south + dy, longitude));
      if (it != tiles.end() && contains(*it->second, lat, lon) &&
          (!best || it->second->dimension > best->dimension))
        best = it->second.get();
    }
  }
  return best;
}

double sample(const TerrainTile &tile, double lat, double lon)
{
  const double x = std::clamp(wrap_longitude(lon - tile.lon), 0.0, 1.0) * (tile.dimension - 1);
  const double y = std::clamp(tile.lat + 1.0 - lat, 0.0, 1.0) * (tile.dimension - 1);
  const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
  const double a = x - x0, b = y - y0;
  return (1.0 - b) * ((1.0 - a) * get_safe_height(tile, x0, y0) + a * get_safe_height(tile, x0 + 1, y0)) +
         b * ((1.0 - a) * get_safe_height(tile, x0, y0 + 1) + a * get_safe_height(tile, x0 + 1, y0 + 1));
}
} // namespace

bool terrain_height_at(const TerrainTileMap &tiles, double lat, double lon, double &height)
{
  const auto *tile = tile_at(tiles, lat, lon);
  if (!tile) return false;
  height = sample(*tile, lat, lon);
  return true;
}

glm::vec3 terrain_normal_at(const TerrainTileMap &tiles, const TerrainTile &tile,
                            int x, int y, double origin_latitude, double origin_longitude)
{
  const double lat = tile.lat + 1.0 - static_cast<double>(y) / (tile.dimension - 1);
  const double lon = tile.lon + static_cast<double>(x) / (tile.dimension - 1);
  const auto frame = local_frame_at(lat, lon, origin_latitude, origin_longitude);
  // Synthetic sea-level tiles must not develop cliffs into loaded terrain.
  auto own = tiles.find(terrain_key(tile.lat, tile.lon));
  if (own == tiles.end()) return glm::vec3(frame.up);

  const auto *center_tile = tile_at(tiles, lat, lon);
  const int dimension = center_tile ? std::max(tile.dimension, center_tile->dimension) : tile.dimension;
  const double step = 1.0 / (dimension - 1);
  const double center = center_tile ? sample(*center_tile, lat, lon) : sample(tile, lat, lon);
  const auto derivative = [&](double dlat, double dlon, double meters) {
    double a = center, b = center;
    const bool have_a = terrain_height_at(tiles, lat - dlat, lon - dlon, a);
    const bool have_b = terrain_height_at(tiles, lat + dlat, lon + dlon, b);
    const int spans = static_cast<int>(have_a) + static_cast<int>(have_b);
    return spans && meters > 1e-6 ? (b - a) / (spans * meters) : 0.0;
  };
  // WGS84 radii of curvature: one elevation sample has a different eastward
  // footprint at each latitude. The derivative spacing is independent of LOD.
  constexpr double eccentricity_squared = 6.6943799901413165e-3;
  const double phi = deg_to_rad(lat);
  const double q = 1.0 - eccentricity_squared * std::sin(phi) * std::sin(phi);
  const double east_m = deg_to_rad(step) * kWgs84A / std::sqrt(q) * std::abs(std::cos(phi));
  const double north_m = deg_to_rad(step) * kWgs84A * (1.0 - eccentricity_squared) / std::pow(q, 1.5);
  return glm::vec3(glm::normalize(frame.up - derivative(0.0, step, east_m) * frame.east -
                                  derivative(step, 0.0, north_m) * frame.north));
}
} // namespace gworld_scene
