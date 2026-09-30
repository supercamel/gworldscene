#ifndef GWORLD_SCENE_TERRAIN_SOURCE_PRIVATE_H
#define GWORLD_SCENE_TERRAIN_SOURCE_PRIVATE_H
#include "gworld-scene-terrain-source.h"
#include "gworld-scene-terrain-private.h"
namespace gworld_scene {
TerrainTileMap terrain_source_snapshot(GWorldSceneTerrainSource *source);
std::shared_ptr<const TerrainTile> terrain_source_tile(GWorldSceneTerrainSource *source,int lat,int lon);
void terrain_source_request(GWorldSceneTerrainSource *source,int lat,int lon);
}
#endif
