#ifndef GWORLD_SCENE_LOD_PRIVATE_H
#define GWORLD_SCENE_LOD_PRIVATE_H

#include <string>

namespace gworld_scene {

struct TileRange {
  int z = 0;
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

int globe_texture_zoom_for_altitude(double altitude_amsl);

TileRange globe_texture_range_for_camera(double latitude,
                                         double longitude,
                                         double altitude_amsl);

} // namespace gworld_scene

#endif /* GWORLD_SCENE_LOD_PRIVATE_H */
