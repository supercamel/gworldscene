#ifndef GWORLD_SCENE_TERRAIN_PRIVATE_H
#define GWORLD_SCENE_TERRAIN_PRIVATE_H

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gworld_scene {
struct TerrainTile {
  int lat = 0;
  int lon = 0;
  int dimension = 0;
  std::vector<int16_t> heights;
};
using TerrainTileMap = std::unordered_map<std::string, std::shared_ptr<const TerrainTile>>;
std::string terrain_key(int latitude, int longitude);
int16_t get_safe_height(const TerrainTile &tile, int x, int y);
bool terrain_height_at(const TerrainTileMap &tiles, double latitude, double longitude, double &height);
glm::vec3 terrain_normal_at(const TerrainTileMap &tiles, const TerrainTile &tile,
                            int x, int y, double origin_latitude, double origin_longitude);
} // namespace gworld_scene
#endif
