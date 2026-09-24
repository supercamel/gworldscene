#ifndef GWORLD_SCENE_LOD_PRIVATE_H
#define GWORLD_SCENE_LOD_PRIVATE_H

#include <string>

namespace gworld_scene {

struct TileRange {
  int z = 0;
  // X remains contiguous across the dateline and may be outside [0, 2^z).
  // Wrap it only when addressing a provider tile or its cache entry.
  int x_min = 0;
  int x_max = -1;
  int y_min = 0;
  int y_max = -1;

  bool valid() const;
  int width_tiles() const;
  int height_tiles() const;
  std::string key() const;
};

double slippy_tile_x_for_longitude(double longitude_deg, int zoom);
double slippy_tile_y_for_latitude(double latitude_deg, int zoom);

int wrap_tile_x(int x, int zoom);

// Longitude bounds are an ordered, unwrapped interval (e.g. 179 to 181).
TileRange tile_range_for_bounds(double min_latitude, double max_latitude,
                                double min_longitude, double max_longitude, int zoom);

struct ImageryBand {
  TileRange range;
  double radius_m = 0.0;
};

struct TerrainImageryPlan {
  ImageryBand ultra;
  ImageryBand detail;
  ImageryBand mid;
  ImageryBand far;
  ImageryBand base;
};

// Successive distance bands use two zoom levels less detail (4x ground texel
// size). Fit their coverage to the tile budget, retaining the chosen resolution.
// The base layer always covers the whole terrain radius, reducing zoom if needed.
TerrainImageryPlan terrain_imagery_plan(double latitude, double longitude,
                                        double altitude_agl, double terrain_radius_m,
                                        int max_tiles = 64, int max_pixels = 4096,
                                        int max_ultra_tiles = 256, double ultra_radius_m = 1000.0);

int globe_texture_zoom_for_altitude(double altitude_amsl);

TileRange globe_texture_range_for_camera(double latitude,
                                         double longitude,
                                         double altitude_amsl,
                                         int max_tiles_per_axis = 16);

} // namespace gworld_scene

#endif /* GWORLD_SCENE_LOD_PRIVATE_H */
