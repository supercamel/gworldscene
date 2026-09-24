#ifndef GWORLD_SCENE_TILE_PROVIDER_PRIVATE_H
#define GWORLD_SCENE_TILE_PROVIDER_PRIVATE_H
#include "gworld-scene-tile-provider.h"
#include <memory>
#include <string>
#include <vector>

namespace gworld_scene {
struct ImageryTile { int z, x, y; };
// Opaque attribution identity, independent of source pixel memory. Atlas results
// and GPU presentation slots retain it until their derived pixels are retired.
struct ImageryAnnotation {
  GWorldSceneTileProvider *provider;
  std::string token;
  ~ImageryAnnotation();
};
using ImageryAnnotations = std::vector<std::shared_ptr<ImageryAnnotation>>;
// The pixbuf is read-only. Destruction can happen on a worker; its ownership
// notification is always dispatched by an idle source on the creating context.
struct ImageryLease {
  GWorldSceneTileProvider *provider;
  GdkPixbuf *image;
  guint64 id;
  int z, x, y;
  int coverage_z, coverage_x, coverage_y;
  std::shared_ptr<ImageryAnnotation> annotation;
  ~ImageryLease();
};
using ImagerySnapshot = std::vector<std::shared_ptr<ImageryLease>>;
void imagery_update(GWorldSceneTileProvider *self, const std::vector<ImageryTile> &wanted);
ImagerySnapshot imagery_snapshot(GWorldSceneTileProvider *self);
bool imagery_sample(const ImagerySnapshot &snapshot, int z, int x, int y, std::vector<unsigned char> &rgba,
                    std::shared_ptr<ImageryAnnotation> *annotation = nullptr);
}
#endif
