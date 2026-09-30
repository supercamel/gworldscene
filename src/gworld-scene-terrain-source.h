#ifndef GWORLD_SCENE_TERRAIN_SOURCE_H
#define GWORLD_SCENE_TERRAIN_SOURCE_H
#include <gio/gio.h>
G_BEGIN_DECLS
#define GWORLD_TYPE_SCENE_TERRAIN_SOURCE (gworld_scene_terrain_source_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneTerrainSource, gworld_scene_terrain_source, GWORLD, SCENE_TERRAIN_SOURCE, GObject)
/**
 * gworld_scene_terrain_source_new:
 *
 * A shared, application-fed collection of immutable one-degree elevation tiles.
 * All methods and signals run on the creating/main thread. Decoded samples are
 * shared by attached views and their workers. The source does no network/disk
 * I/O. The application supplies its own cache, fetch policy and source identity.
 * Returns: (transfer full): a new source, with a 128 MiB resident sample limit
 */
GWorldSceneTerrainSource *gworld_scene_terrain_source_new(void);
/**
 * gworld_scene_terrain_source_put_tile:
 * @self: a terrain source
 * @latitude: south edge of the one-degree tile, -90 to 89
 * @longitude: west edge of the tile, -180 to 179
 * @dimension: samples per side, 2 to 3601, including shared boundary samples
 * @samples: signed 16-bit big-endian heights in metres AMSL, row-major north to south
 * @error: return location for validation or memory-limit error
 *
 * Copies/decodes a complete square tile once. -32768 means unknown. Replacement
 * is atomic and old worker snapshots stay valid. Caller retains @samples.
 * Use one source per height datum/provider; swapping a view's source fences its
 * previous terrain work. The memory limit includes retired worker-held tiles.
 * Returns: %TRUE on success, otherwise the old tile is unchanged
 */
gboolean gworld_scene_terrain_source_put_tile(GWorldSceneTerrainSource *self,
  int latitude, int longitude, int dimension, GBytes *samples, GError **error);
/**
 * gworld_scene_terrain_source_remove_tile:
 * @self: a terrain source
 * @latitude: south edge in degrees
 * @longitude: west edge in degrees
 *
 * Removes resident data and resets request suppression for this tile. Attached
 * views requesting it again will emit tile-needed. Worker snapshots drain normally.
 */
void gworld_scene_terrain_source_remove_tile(GWorldSceneTerrainSource *self,int latitude,int longitude);
/**
 * gworld_scene_terrain_source_get_memory_used:
 * @self: a terrain source
 * Returns: bytes held by current or retired shared elevation samples
 */
guint64 gworld_scene_terrain_source_get_memory_used(GWorldSceneTerrainSource *self);
/**
 * gworld_scene_terrain_source_sample:
 * @self: a terrain source
 * @latitude: sample latitude in degrees
 * @longitude: sample longitude in degrees
 * @altitude_amsl: (out) (transfer none): elevation in metres, zero only as an unused output on failure
 * Returns: %TRUE when a valid sample is available; %FALSE is unknown, not sea level
 */
gboolean gworld_scene_terrain_source_sample(GWorldSceneTerrainSource *self,
  double latitude,double longitude,double *altitude_amsl);
/**
 * gworld_scene_terrain_source_get_altitude:
 * @self: a terrain source
 * @latitude: sample latitude in degrees
 * @longitude: sample longitude in degrees
 * @error: return location for unavailable/invalid-coordinate errors
 *
 * Single-value form of sample(), convenient for bindings that omit boolean
 * return values alongside scalar outputs. Missing/void terrain raises
 * G_IO_ERROR_NOT_FOUND, invalid coordinates G_IO_ERROR_INVALID_ARGUMENT.
 * Returns: terrain elevation in metres AMSL, or zero only with an error set
 */
double gworld_scene_terrain_source_get_altitude(GWorldSceneTerrainSource *self,
  double latitude,double longitude,GError **error);
G_END_DECLS
#endif
