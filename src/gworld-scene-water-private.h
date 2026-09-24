#ifndef GWORLD_SCENE_WATER_PRIVATE_H
#define GWORLD_SCENE_WATER_PRIVATE_H

#include "gworld-scene-lod-private.h"
#include <epoxy/gl.h>
#include <gtk/gtk.h>
#include <memory>
#include <string>
#include <vector>

namespace gworld_scene {
constexpr int kWaterTilePixels = 512;
constexpr const char *kDefaultWaterTileTemplate = "https://tiles.openfreemap.org/planet/latest/{z}/{x}/{y}.pbf";

// Rasterize only permanent surface-water polygons. GDAL preserves polygon holes
// (islands), clips tile buffers and georeferences the MVT in Web Mercator.
bool decode_water_mask(const std::vector<unsigned char> &bytes, int z, int x, int y,
                        std::vector<unsigned char> &mask, std::string &error);
std::string water_cache_path(const std::string &directory, const std::string &source, int z, int x, int y);

struct WaterTileStore;
struct WaterAtlas {
  GLuint texture = 0;
  TileRange range;
  unsigned long revision = 0;
  bool has_water = false;
};

class WaterTiles {
public:
  WaterTiles() = default;
  ~WaterTiles();
  void update(GtkWidget *widget, const std::string &source, const std::string &cache_directory,
               bool cache_enabled, double latitude, double longitude, double altitude,
               double radius_m);
  void bind(GLuint program) const;
  bool wants_frame(bool animate) const;
  bool ready() const;
  void cancel();
  void destroy_gl();
  // Context-owned GL state is only touched from update/bind/destroy_gl.
  WaterAtlas near_atlas, mid_atlas, far_atlas;
private:
  std::shared_ptr<WaterTileStore> store;
  std::string configuration;
};
} // namespace gworld_scene
#endif
