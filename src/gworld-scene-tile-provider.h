#ifndef GWORLD_SCENE_TILE_PROVIDER_H
#define GWORLD_SCENE_TILE_PROVIDER_H

#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS
#define GWORLD_TYPE_SCENE_TILE_PROVIDER (gworld_scene_tile_provider_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneTileProvider, gworld_scene_tile_provider, GWORLD, SCENE_TILE_PROVIDER, GObject)

/**
 * GWorldSceneTileProvider::tile-requested:
 * @self: the provider
 * @request_id: unique request identity, never reused by this provider
 * @zoom: source zoom level
 * @x: wrapped tile column
 * @y: tile row
 *
 * Supply immutable pixels with complete_tile() or complete_annotated(), or
 * complete with null to mark unavailable. At most 16 requests await completion.
 * Requests are deferred to the creating main context after demand-changed.
 */
/**
 * GWorldSceneTileProvider::tile-released:
 * @self: the provider
 * @request_id: retired request identity
 *
 * Emitted once per dispatched request after its source pixel leases release.
 * Cancel outstanding application work for this identity. Derived atlas pixels
 * may still be displayed; attribution has a separate lifetime.
 */
/**
 * GWorldSceneTileProvider::annotation-released:
 * @self: the provider
 * @annotation: opaque attribution identity
 *
 * All pixels and derived atlas/presentation references to this identity have
 * released. The application may now retire its corresponding credit record.
 */
/**
 * GWorldSceneTileProvider::demand-changed:
 * @self: the provider
 * @revision: monotonically increasing demand revision
 *
 * The accepted coordinate set changed. dup_demand() includes queued requests
 * that have not yet emitted tile-requested. An empty set is reported on clear.
 */
/**
 * GWorldSceneTileProvider::coverage-changed:
 * @self: the provider
 *
 * Ready, failed, held, overflow, unsupported or reduced-detail state changed.
 * Use the provider getters to inspect the current coverage.
 */
/**
 * GWorldSceneTileProvider::changed:
 * @self: the provider
 *
 * Imagery was completed or cleared. Attached views refresh their atlases.
 */
/**
 * GWorldSceneTileProvider::drained:
 * @self: the provider
 *
 * Emitted once after close and release of all source and derived references.
 * Can be emitted synchronously by close if no references remain.
 */

/**
 * gworld_scene_tile_provider_new:
 * @minimum_zoom: minimum source zoom, 0 through 22
 * @maximum_zoom: maximum source zoom, minimum through 22
 * @tile_size: square image size, 256 or 512
 *
 * Create on the GTK/main-context thread. Use all public methods on that thread,
 * and iterate its main context to deliver deferred requests and releases.
 * A provider belongs to one view. Close it before retiring application state.
 * Returns: (transfer full) (nullable): a provider, or null for invalid limits
 */
GWorldSceneTileProvider *gworld_scene_tile_provider_new(gint minimum_zoom, gint maximum_zoom, gint tile_size);

/**
 * gworld_scene_tile_provider_complete_tile:
 * @self: the provider
 * @request_id: identity from tile-requested
 * @image: (nullable) (transfer none): immutable validated pixels, or null for failure
 *
 * Main-thread only. A successful image remains referenced until tile-released.
 * Retired/duplicate identities and incorrect image dimensions return false.
 * Incorrect dimensions complete that identity as unavailable.
 * Returns: whether the live completion was accepted
 */
gboolean gworld_scene_tile_provider_complete_tile(GWorldSceneTileProvider *self, guint64 request_id, GdkPixbuf *image);

/**
 * gworld_scene_tile_provider_complete_annotated:
 * @self: the provider
 * @request_id: active request identity
 * @image: (nullable) (transfer none): immutable pixels, or null for failure
 * @source_zoom: source image level, between provider minimum and requested level
 * @annotation: (nullable): opaque ASCII attribution ID, at most 128 bytes
 *
 * Main-thread only. Supply a requested tile or an ancestor covering it. Pixels may be sampled only
 * within the original requested footprint. The annotation-released signal is
 * independent of tile-released and includes derived atlas/presentation lifetime.
 * Close drains only after both pixel and annotation references are gone.
 * Returns: whether the live completion was accepted
 */
gboolean gworld_scene_tile_provider_complete_annotated(GWorldSceneTileProvider *self, guint64 request_id,
                                                       GdkPixbuf *image, gint source_zoom, const gchar *annotation);
/**
 * gworld_scene_tile_provider_dup_demand:
 * @self: the provider
 *
 * Current active requested source coordinates, including queued requests, as
 * z/x/y strings. No request IDs, image ancestors, metadata or credentials.
 * Returns: (array zero-terminated=1) (transfer full): owned coordinate strings
 */
gchar **gworld_scene_tile_provider_dup_demand(GWorldSceneTileProvider *self);
/**
 * gworld_scene_tile_provider_get_annotation_count:
 * @self: the provider
 *
 * Returns: number of distinct attribution identities still retained, including
 * derived atlas and presentation references after source pixels have released
 */
guint gworld_scene_tile_provider_get_annotation_count(GWorldSceneTileProvider *self);

/**
 * gworld_scene_tile_provider_clear:
 * @self: the provider
 *
 * Retire all requests. Worker-held pixels release asynchronously on the creating
 * main context. New requests use new identities. Main-thread only.
 */
void gworld_scene_tile_provider_clear(GWorldSceneTileProvider *self);
/**
 * gworld_scene_tile_provider_close:
 * @self: the provider
 *
 * Clear and refuse further requests. The drained signal occurs once all worker
 * leases and derived annotation references have released, possibly during this
 * call. Main-thread only.
 */
void gworld_scene_tile_provider_close(GWorldSceneTileProvider *self);
gint gworld_scene_tile_provider_get_min_zoom(GWorldSceneTileProvider *self);
gint gworld_scene_tile_provider_get_max_zoom(GWorldSceneTileProvider *self);
gint gworld_scene_tile_provider_get_tile_size(GWorldSceneTileProvider *self);
guint gworld_scene_tile_provider_get_tile_count(GWorldSceneTileProvider *self);
guint gworld_scene_tile_provider_get_ready_count(GWorldSceneTileProvider *self);
guint gworld_scene_tile_provider_get_failed_count(GWorldSceneTileProvider *self);
guint gworld_scene_tile_provider_get_held_count(GWorldSceneTileProvider *self);
guint gworld_scene_tile_provider_get_worker_count(GWorldSceneTileProvider *self);
guint gworld_scene_tile_provider_get_overflow_count(GWorldSceneTileProvider *self);
guint gworld_scene_tile_provider_get_unsupported_count(GWorldSceneTileProvider *self);
gboolean gworld_scene_tile_provider_get_reduced_detail(GWorldSceneTileProvider *self);
gboolean gworld_scene_tile_provider_get_closed(GWorldSceneTileProvider *self);

G_END_DECLS
#endif
