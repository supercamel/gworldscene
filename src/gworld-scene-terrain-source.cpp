#include "gworld-scene-terrain-source-private.h"
#include <atomic>
#include <cmath>
#include <set>

struct TerrainSourceState {
  gworld_scene::TerrainTileMap tiles;
  std::set<std::string> requested;
  std::shared_ptr<std::atomic<guint64>> bytes=std::make_shared<std::atomic<guint64>>(0);
};
struct _GWorldSceneTerrainSource { GObject parent_instance; TerrainSourceState *state; };
G_DEFINE_TYPE(GWorldSceneTerrainSource,gworld_scene_terrain_source,G_TYPE_OBJECT)
static guint changed_signal,needed_signal;
static void finalize(GObject *object) {
  delete GWORLD_SCENE_TERRAIN_SOURCE(object)->state;
  G_OBJECT_CLASS(gworld_scene_terrain_source_parent_class)->finalize(object);
}
static void gworld_scene_terrain_source_class_init(GWorldSceneTerrainSourceClass *klass) {
  G_OBJECT_CLASS(klass)->finalize=finalize;
  /**
   * GWorldSceneTerrainSource::changed:
   * @self: the source
   *
   * Resident samples changed. Existing shared snapshots remain immutable.
   */
  changed_signal=g_signal_new("changed",G_TYPE_FROM_CLASS(klass),G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,G_TYPE_NONE,0);
  /**
   * GWorldSceneTerrainSource::tile-needed:
   * @self: the source
   * @latitude: south edge of the missing one-degree tile
   * @longitude: west edge of the missing one-degree tile
   *
   * Fetch demand shared across attached views. Return promptly and supply data
   * later with put_tile(). Emitted once per missing tile until remove_tile()
   * explicitly permits retry. At most 1024 outstanding requests are retained;
   * views retry demand when samples/change notifications free capacity.
   */
  needed_signal=g_signal_new("tile-needed",G_TYPE_FROM_CLASS(klass),G_SIGNAL_RUN_LAST,0,nullptr,nullptr,nullptr,
                            G_TYPE_NONE,2,G_TYPE_INT,G_TYPE_INT);
}
static void gworld_scene_terrain_source_init(GWorldSceneTerrainSource *self) {self->state=new TerrainSourceState;}
GWorldSceneTerrainSource *gworld_scene_terrain_source_new(void) {
  return GWORLD_SCENE_TERRAIN_SOURCE(g_object_new(GWORLD_TYPE_SCENE_TERRAIN_SOURCE,nullptr));
}
gboolean gworld_scene_terrain_source_put_tile(GWorldSceneTerrainSource *self,
  int lat,int lon,int dimension,GBytes *samples,GError **error) {
  g_return_val_if_fail(GWORLD_IS_SCENE_TERRAIN_SOURCE(self),FALSE);
  if(lat < -90 || lat>89 || lon < -180 || lon>179 || dimension<2 || dimension>3601 || !samples ||
     g_bytes_get_size(samples)!=gsize(dimension)*dimension*2) {
    g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid one-degree terrain tile");return FALSE;
  }
  auto *s=self->state;
  const guint64 bytes=guint64(dimension)*dimension*2;
  if(s->bytes->load()+bytes>128ULL*1024*1024) {
    g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Shared terrain memory limit reached; release tiles or wait for workers to drain");return FALSE;
  }
  const auto accounting=s->bytes;
  accounting->fetch_add(bytes);
  std::shared_ptr<gworld_scene::TerrainTile> tile(new gworld_scene::TerrainTile,
    [accounting,bytes](gworld_scene::TerrainTile *p){delete p;accounting->fetch_sub(bytes);});
  tile->lat=lat;tile->lon=lon;tile->dimension=dimension;tile->heights.resize(gsize(dimension)*dimension);
  auto *data=static_cast<const guint8 *>(g_bytes_get_data(samples,nullptr));
  for(gsize i=0;i<tile->heights.size();++i) {
    const unsigned v=(unsigned(data[i*2])<<8)|data[i*2+1];
    tile->heights[i]=static_cast<int16_t>(v>=32768 ? int(v)-65536 : int(v));
  }
  const auto key=gworld_scene::terrain_key(lat,lon);
  s->tiles[key]=tile;s->requested.erase(key);
  g_signal_emit(self,changed_signal,0);
  return TRUE;
}
void gworld_scene_terrain_source_remove_tile(GWorldSceneTerrainSource *self,int lat,int lon) {
  g_return_if_fail(GWORLD_IS_SCENE_TERRAIN_SOURCE(self));
  const auto key=gworld_scene::terrain_key(lat,lon);
  self->state->tiles.erase(key);self->state->requested.erase(key);
  g_signal_emit(self,changed_signal,0);
}
guint64 gworld_scene_terrain_source_get_memory_used(GWorldSceneTerrainSource *self) {
  g_return_val_if_fail(GWORLD_IS_SCENE_TERRAIN_SOURCE(self),0);return self->state->bytes->load();
}
gboolean gworld_scene_terrain_source_sample(GWorldSceneTerrainSource *self,double lat,double lon,double *height) {
  g_return_val_if_fail(GWORLD_IS_SCENE_TERRAIN_SOURCE(self),FALSE);
  if(height)*height=0;
  if(!std::isfinite(lat)||!std::isfinite(lon)||lat < -90||lat>90||lon < -180||lon>180)return FALSE;
  double value=0;
  const bool found=gworld_scene::terrain_height_at(self->state->tiles,lat,lon,value);
  if(height && found)*height=value;
  return found;
}
namespace gworld_scene {
TerrainTileMap terrain_source_snapshot(GWorldSceneTerrainSource *source) {return source->state->tiles;}
std::shared_ptr<const TerrainTile> terrain_source_tile(GWorldSceneTerrainSource *source,int lat,int lon) {
  const auto it=source->state->tiles.find(terrain_key(lat,lon));
  return it==source->state->tiles.end()?nullptr:it->second;
}
void terrain_source_request(GWorldSceneTerrainSource *source,int lat,int lon) {
  auto *s=source->state;const auto key=terrain_key(lat,lon);
  if(s->tiles.count(key)||s->requested.size()>=1024||!s->requested.insert(key).second)return;
  g_signal_emit(source,needed_signal,0,lat,lon);
}
}

double gworld_scene_terrain_source_get_altitude(GWorldSceneTerrainSource *self,double lat,double lon,GError **error)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_TERRAIN_SOURCE(self),0);
  if(!std::isfinite(lat)||!std::isfinite(lon)||lat < -90||lat>90||lon < -180||lon>180) {
    g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid terrain coordinate");return 0;
  }
  double h=0;
  if(!gworld_scene_terrain_source_sample(self,lat,lon,&h))
    g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"Terrain height is unavailable");
  return h;
}
