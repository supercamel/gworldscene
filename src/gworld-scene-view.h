#ifndef GWORLD_SCENE_VIEW_H
#define GWORLD_SCENE_VIEW_H

#include <gtk/gtk.h>

#include "gworld-scene-node.h"
#include "gworld-scene-terrain-source.h"
#include "gworld-scene-tile-provider.h"

G_BEGIN_DECLS

#define GWORLD_TYPE_SCENE_VIEW (gworld_scene_view_get_type())

G_DECLARE_FINAL_TYPE(GWorldSceneView, gworld_scene_view, GWORLD, SCENE_VIEW, GtkGLArea)

/**
 * GWorldSceneFrame:
 *
 * Owned CPU image and timing for one explicit scene render. Frame metadata is
 * immutable; treat the borrowed image as read-only. No widgets or GL resources
 * are retained. Available in both GTK variants.
 */
#define GWORLD_TYPE_SCENE_FRAME (gworld_scene_frame_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneFrame, gworld_scene_frame, GWORLD, SCENE_FRAME, GObject)

/**
 * gworld_scene_frame_get_image:
 * @self: a captured frame
 * Returns: (transfer none): top-down, opaque sRGB RGBA image; treat as read-only
 */
GdkPixbuf *gworld_scene_frame_get_image(GWorldSceneFrame *self);
/**
 * gworld_scene_frame_get_timestamp:
 * @self: a captured frame
 * Returns: the caller's timestamp in microseconds (caller chooses clock domain)
 */
gint64 gworld_scene_frame_get_timestamp(GWorldSceneFrame *self);
/**
 * gworld_scene_frame_get_completed_time:
 * @self: a captured frame
 * Returns: GLib monotonic completion time in microseconds
 */
gint64 gworld_scene_frame_get_completed_time(GWorldSceneFrame *self);
/**
 * gworld_scene_frame_get_sequence:
 * @self: a captured frame
 * Returns: monotonically increasing successful capture sequence for this view
 */
guint64 gworld_scene_frame_get_sequence(GWorldSceneFrame *self);

/**
 * gworld_scene_view_set_camera_pose:
 * @self: a scene view
 * @latitude: eye latitude in degrees, -90 to 90
 * @longitude: eye longitude in degrees, -180 to 180
 * @altitude_amsl: exact eye altitude in metres AMSL, -12000 to 10000000
 * @qw: quaternion scalar component
 * @qx: quaternion X component
 * @qy: quaternion Y component
 * @qz: quaternion Z component
 * @error: return location for a validation error
 *
 * Atomically sets the free eye and complete orientation. The nonzero quaternion
 * is normalized and rotates optical forward/right/down axes into geographic
 * north/east/down. Identity looks north with image up toward zenith. No orbit,
 * pitch or 25 metre altitude clamp is applied. Legacy orientation setters and
 * changing camera mode leave this exact-pose path. Call on the GTK thread.
 * Returns: %TRUE on success; invalid inputs leave the previous pose unchanged
 */
gboolean gworld_scene_view_set_camera_pose(GWorldSceneView *self,
  double latitude, double longitude, double altitude_amsl,
  double qw, double qx, double qy, double qz, GError **error);

/**
 * gworld_scene_view_get_camera_quaternion:
 * @self: a scene view
 * @qw: (out) (optional) (transfer none): normalized scalar component
 * @qx: (out) (optional) (transfer none): normalized X component
 * @qy: (out) (optional) (transfer none): normalized Y component
 * @qz: (out) (optional) (transfer none): normalized Z component
 *
 * Reads the exact camera quaternion, or identity when exact pose is inactive.
 * Use get_camera_pose_enabled() to distinguish those states.
 */
void gworld_scene_view_get_camera_quaternion(GWorldSceneView *self,
  double *qw, double *qx, double *qy, double *qz);

/**
 * gworld_scene_view_get_camera_pose_enabled:
 * @self: a scene view
 * Returns: whether the exact quaternion camera pose is active
 */
gboolean gworld_scene_view_get_camera_pose_enabled(GWorldSceneView *self);

/**
 * gworld_scene_view_set_camera_projection:
 * @self: a scene view
 * @width: calibrated sensor width in pixels, 1 to 8192
 * @height: calibrated sensor height in pixels, 1 to 8192
 * @fx: positive horizontal focal length in pixels
 * @fy: positive vertical focal length in pixels
 * @cx: principal point measured from the left image edge in pixels
 * @cy: principal point measured from the top image edge in pixels
 * @near_m: near clipping distance in metres, at least 0.001
 * @far_m: far clipping distance in metres, greater than near_m, at most 100000000
 * @error: return location for a validation error
 *
 * Sets a calibrated pinhole projection. Pixel centers are at half-integers.
 * Viewport/capture resizing letterboxes without changing optics. World geometry
 * uses logarithmic depth to retain precision with close clipping. Lens
 * distortion and image resampling belong to the caller. Call on the GTK thread.
 * Returns: %TRUE on success; invalid inputs leave the previous projection unchanged
 */
gboolean gworld_scene_view_set_camera_projection(GWorldSceneView *self,
  int width, int height, double fx, double fy, double cx, double cy,
  double near_m, double far_m, GError **error);

/**
 * gworld_scene_view_reset_camera_projection:
 * @self: a scene view
 *
 * Restores the legacy 45 degree vertical field of view and automatic clipping.
 * Does not change the current pose.
 */
void gworld_scene_view_reset_camera_projection(GWorldSceneView *self);

/**
 * gworld_scene_view_set_offscreen_enabled:
 * @self: a scene view
 * @enabled: whether an offscreen consumer needs terrain and imagery updates
 *
 * Retains external imagery demand while unmapped. Enable before hiding a view
 * used for capture; disable when its consumer stops. Does not create a timer,
 * realize the widget or render frames automatically. Call on the GTK thread.
 */
void gworld_scene_view_set_offscreen_enabled(GWorldSceneView *self, gboolean enabled);
/**
 * gworld_scene_view_get_offscreen_enabled:
 * @self: a scene view
 * Returns: whether offscreen demand is enabled
 */
gboolean gworld_scene_view_get_offscreen_enabled(GWorldSceneView *self);

/**
 * gworld_scene_view_capture_frame:
 * @self: a realized scene view
 * @width: output width in pixels, 1 to 8192
 * @height: output height in pixels, 1 to 8192
 * @timestamp_us: caller-provided frame timestamp in microseconds
 * @error: return location for a render/context/validation error
 *
 * Renders current scene state into a retained offscreen framebuffer and reads
 * back one owned image. The view must be realized, but need not be mapped or
 * visible; no GTK overlays enter the image. Works before first mapping when
 * the view and its native ancestor have been explicitly realized. No network
 * work is waited on: currently available terrain/assets are rendered. Maximum
 * output is 16777216 pixels. Call on the GTK thread outside a render callback;
 * rendering and readback are synchronous, and callers must bound their cadence.
 * Sharing this frame among displays and encoders avoids duplicate scene renders.
 * Returns: (transfer full) (nullable): a captured frame, or %NULL on error
 */
GWorldSceneFrame *gworld_scene_view_capture_frame(GWorldSceneView *self,
  int width, int height, gint64 timestamp_us, GError **error);

/**
 * gworld_scene_view_capture_frame_scaled:
 * @self: a realized scene view
 * @sensor_width: render width in pixels, 1 to 8192
 * @sensor_height: render height in pixels, 1 to 8192
 * @crop_factor: finite centered digital crop factor, at least 1
 * @output_width: returned image width, 1 to 8192
 * @output_height: returned image height, 1 to 8192
 * @timestamp_us: caller-provided timestamp in microseconds
 * @error: return location for a render/context/validation error
 *
 * Renders at sensor resolution, then crops and scales with GPU linear filtering
 * before CPU readback. Both sensor and output are limited to 16777216 pixels;
 * the crop must retain at least one sensor pixel on each axis. Projection and
 * imagery demand use sensor dimensions. Same threading, lifetime and attribution
 * guarantees as capture_frame(). Buffers are retained across calls.
 * Returns: (transfer full) (nullable): an owned captured frame, or %NULL on error
 */
GWorldSceneFrame *gworld_scene_view_capture_frame_scaled(GWorldSceneView *self,
  int sensor_width, int sensor_height, double crop_factor,
  int output_width, int output_height, gint64 timestamp_us, GError **error);

/**
 * gworld_scene_view_request_capture_frame_scaled:
 * @self: a realized scene view
 * @sensor_width: render width in pixels
 * @sensor_height: render height in pixels
 * @crop_factor: centered digital crop factor
 * @output_width: returned image width
 * @output_height: returned image height
 * @timestamp_us: caller-provided capture timestamp
 * @error: return location for an error
 *
 * Enqueues GPU rendering/readback with the same limits as capture_frame_scaled().
 * Does not wait for GPU completion. Only one asynchronous request may be pending.
 * Poll on subsequent GTK main-loop turns. Unrealize cancels pending captures.
 * Returns: %TRUE if enqueued, %FALSE on validation, context, or busy error
 */
gboolean gworld_scene_view_request_capture_frame_scaled(GWorldSceneView *self,
  int sensor_width,int sensor_height,double crop_factor,
  int output_width,int output_height,gint64 timestamp_us,GError **error);

/**
 * gworld_scene_view_poll_capture_frame:
 * @self: a scene view
 * @error: return location for an error
 *
 * Checks the pending GPU fence with zero timeout. Once ready, copies its pixels
 * into an immutable frame and retires the request. Does not wait for GPU work.
 * Returns: (transfer full) (nullable): the completed frame, or %NULL if none is ready or on error
 */
GWorldSceneFrame *gworld_scene_view_poll_capture_frame(GWorldSceneView *self,GError **error);

typedef enum {
  GWORLD_SCENE_CAMERA_MODE_DEFAULT,
  GWORLD_SCENE_CAMERA_MODE_FREE,
} GWorldSceneCameraMode;

GtkWidget *gworld_scene_view_new(void);

void gworld_scene_view_set_camera(GWorldSceneView *self,
                                  double latitude,
                                  double longitude,
                                  double altitude_amsl);

/**
 * gworld_scene_view_get_camera:
 * @self: a scene view
 * @latitude: (out) (optional) (transfer none): return location for latitude in degrees
 * @longitude: (out) (optional) (transfer none): return location for longitude in degrees
 * @altitude_amsl: (out) (optional) (transfer none): return location for altitude in metres above
 *   mean sea level
 *
 * Reads the stored camera position. In default mode this is the orbit
 * reference position; in free mode it is the eye position.
 */
void gworld_scene_view_get_camera(GWorldSceneView *self,
                                  double *latitude,
                                  double *longitude,
                                  double *altitude_amsl);

void gworld_scene_view_set_camera_orientation(GWorldSceneView *self,
                                              double heading_deg,
                                              double pitch_deg);

/**
 * gworld_scene_view_get_camera_orientation:
 * @self: a scene view
 * @heading_deg: (out) (optional) (transfer none): return location for heading in degrees clockwise
 *   from geographic north
 * @pitch_deg: (out) (optional) (transfer none): return location for elevation angle in degrees,
 *   positive upward
 *
 * Reads the stored camera orientation.
 */
void gworld_scene_view_get_camera_orientation(GWorldSceneView *self,
                                              double *heading_deg,
                                              double *pitch_deg);

/**
 * gworld_scene_view_set_camera_mode:
 * @self: a scene view
 * @camera_mode: default orbit/blended camera, or free local camera
 *
 * Default mode preserves the interactive Google-Earth style orbit blend. Free
 * mode renders the stored camera position as the actual eye position at all
 * altitudes.
 */
void gworld_scene_view_set_camera_mode(GWorldSceneView *self,
                                       GWorldSceneCameraMode camera_mode);

GWorldSceneCameraMode gworld_scene_view_get_camera_mode(GWorldSceneView *self);

/**
 * gworld_scene_view_set_free_camera_position:
 * @self: a scene view
 *
 * Sets the free camera eye position and switches the view to free mode.
 */
void gworld_scene_view_set_free_camera_position(GWorldSceneView *self,
                                                double latitude,
                                                double longitude,
                                                double altitude_amsl);

/**
 * gworld_scene_view_get_free_camera_position:
 * @self: a scene view
 * @latitude: (out) (optional) (transfer none): return location for latitude in degrees
 * @longitude: (out) (optional) (transfer none): return location for longitude in degrees
 * @altitude_amsl: (out) (optional) (transfer none): return location for altitude in metres above
 *   mean sea level
 *
 * Reads the stored camera position without changing camera mode.
 * In free mode this is the eye position.
 */
void gworld_scene_view_get_free_camera_position(GWorldSceneView *self,
                                                double *latitude,
                                                double *longitude,
                                                double *altitude_amsl);

/**
 * gworld_scene_view_set_free_camera_orientation:
 * @self: a scene view
 * @azimuth_deg: clockwise from geographic north
 * @pitch_deg: elevation angle, positive upward
 *
 * Sets the free camera orientation and switches the view to free mode.
 */
void gworld_scene_view_set_free_camera_orientation(GWorldSceneView *self,
                                                   double azimuth_deg,
                                                   double pitch_deg);

/**
 * gworld_scene_view_get_free_camera_orientation:
 * @self: a scene view
 * @azimuth_deg: (out) (optional) (transfer none): return location for azimuth in degrees clockwise
 *   from geographic north
 * @pitch_deg: (out) (optional) (transfer none): return location for elevation angle in degrees,
 *   positive upward
 *
 * Reads the stored camera orientation without changing camera mode.
 */
void gworld_scene_view_get_free_camera_orientation(GWorldSceneView *self,
                                                   double *azimuth_deg,
                                                   double *pitch_deg);

void gworld_scene_view_set_free_camera_azimuth(GWorldSceneView *self,
                                               double azimuth_deg);

void gworld_scene_view_set_free_camera_pitch(GWorldSceneView *self,
                                             double pitch_deg);

/**
 * gworld_scene_view_look_at_location:
 * @self: a scene view
 *
 * Rotates the current camera to face the location and switches to free mode.
 * The target altitude is interpreted as AMSL.
 */
void gworld_scene_view_look_at_location(GWorldSceneView *self,
                                        double latitude,
                                        double longitude,
                                        double altitude_amsl);

/**
 * gworld_scene_view_look_at_node:
 * @self: a scene view
 * @node: a scene node
 *
 * Rotates the current camera to face @node and switches to free mode.
 */
void gworld_scene_view_look_at_node(GWorldSceneView *self,
                                    GWorldSceneNode *node);

/**
 * gworld_scene_view_set_terrain_source:
 * @self: a scene view
 * @source: (nullable): shared application-fed terrain, or %NULL for built-in terrain loading
 *
 * Attaches a retained source. Views share immutable decoded data; missing tiles
 * emit tile-needed on the source. Built-in terrain network/cache reads are
 * disabled while attached, including with caching disabled. Source replacement
 * cancels/fences old terrain work and invalidates geometry. Main-thread only.
 */
void gworld_scene_view_set_terrain_source(GWorldSceneView *self,GWorldSceneTerrainSource *source);
/**
 * gworld_scene_view_get_terrain_source:
 * @self: a scene view
 * Returns: (transfer none) (nullable): the attached application source
 */
GWorldSceneTerrainSource *gworld_scene_view_get_terrain_source(GWorldSceneView *self);

void gworld_scene_view_set_terrain_server(GWorldSceneView *self,
                                          const char *terrain_server);

const char *gworld_scene_view_get_terrain_server(GWorldSceneView *self);

void gworld_scene_view_set_map_tile_url_template(GWorldSceneView *self,
                                                 const char *url_template);

const char *gworld_scene_view_get_map_tile_url_template(GWorldSceneView *self);

/**
 * gworld_scene_view_set_tile_provider:
 * @self: a scene view
 * @provider: (nullable) (transfer none): application imagery provider for this view
 *
 * Use application-supplied imagery, bypassing native imagery HTTP/disk access.
 * Null leaves a neutral imagery background. The old provider is cleared, but
 * worker-held leases may release later. Terrain and water retain independent
 * settings. Calling set_map_tile_url_template explicitly returns to legacy mode.
 */
void gworld_scene_view_set_tile_provider(GWorldSceneView *self, GWorldSceneTileProvider *provider);

/**
 * gworld_scene_view_get_imagery_ready:
 * @self: a scene view
 *
 * Returns: whether external visible coverage is complete and all requested
 * atlases have reached the active GPU textures for this source revision
 */
gboolean gworld_scene_view_get_imagery_ready(GWorldSceneView *self);

void gworld_scene_view_set_cache_directory(GWorldSceneView *self,
                                           const char *cache_directory);

const char *gworld_scene_view_get_cache_directory(GWorldSceneView *self);

void gworld_scene_view_set_cache_enabled(GWorldSceneView *self,
                                         gboolean cache_enabled);

gboolean gworld_scene_view_get_cache_enabled(GWorldSceneView *self);

void gworld_scene_view_set_texture_memory_budget_mib(GWorldSceneView *self,
                                                     guint budget_mib);

guint gworld_scene_view_get_texture_memory_budget_mib(GWorldSceneView *self);

void gworld_scene_view_set_sun_position(GWorldSceneView *self,
                                        double azimuth_deg,
                                        double elevation_deg);

/**
 * gworld_scene_view_get_sun_position:
 * @self: a scene view
 * @azimuth_deg: (out) (optional) (transfer none): return location for azimuth in degrees clockwise
 *   from geographic north
 * @elevation_deg: (out) (optional) (transfer none): return location for elevation in degrees above
 *   the horizon
 *
 * Reads the current sun position, including time-of-day lighting.
 */
void gworld_scene_view_get_sun_position(GWorldSceneView *self,
                                        double *azimuth_deg,
                                        double *elevation_deg);

void gworld_scene_view_set_sun_time_of_day(GWorldSceneView *self,
                                           double local_solar_hour);

double gworld_scene_view_get_sun_time_of_day(GWorldSceneView *self);

void gworld_scene_view_set_fog_enabled(GWorldSceneView *self,
                                       gboolean fog_enabled);

gboolean gworld_scene_view_get_fog_enabled(GWorldSceneView *self);

void gworld_scene_view_set_fog_range(GWorldSceneView *self,
                                     double start_m,
                                     double end_m);

/**
 * gworld_scene_view_get_fog_range:
 * @self: a scene view
 * @start_m: (out) (optional) (transfer none): return location for fog start distance in metres
 * @end_m: (out) (optional) (transfer none): return location for fog end distance in metres
 *
 * Reads the fog distance range.
 */
void gworld_scene_view_get_fog_range(GWorldSceneView *self,
                                     double *start_m,
                                     double *end_m);

void gworld_scene_view_set_fog_color(GWorldSceneView *self,
                                     double red,
                                     double green,
                                     double blue);

/**
 * gworld_scene_view_get_fog_color:
 * @self: a scene view
 * @red: (out) (optional) (transfer none): return location for red component in [0, 1]
 * @green: (out) (optional) (transfer none): return location for green component in [0, 1]
 * @blue: (out) (optional) (transfer none): return location for blue component in [0, 1]
 *
 * Reads the fog color.
 */
void gworld_scene_view_get_fog_color(GWorldSceneView *self,
                                     double *red,
                                     double *green,
                                     double *blue);

/**
 * gworld_scene_view_set_atmosphere_enabled:
 * @self: a scene view
 * @enabled: whether to render the sky and aerial perspective with atmospheric scattering
 *
 * Enabled by default. Disabling restores the gradient sky. Distance fog is controlled independently.
 */
void gworld_scene_view_set_atmosphere_enabled(GWorldSceneView *self, gboolean enabled);
gboolean gworld_scene_view_get_atmosphere_enabled(GWorldSceneView *self);

/**
 * gworld_scene_view_set_atmosphere_density:
 * @self: a scene view
 * @density: air-density multiplier in [0, 4], default 1
 */
void gworld_scene_view_set_atmosphere_density(GWorldSceneView *self, double density);
double gworld_scene_view_get_atmosphere_density(GWorldSceneView *self);

/**
 * gworld_scene_view_set_atmosphere_haze:
 * @self: a scene view
 * @haze: aerosol-scattering multiplier in [0, 4], default 1
 */
void gworld_scene_view_set_atmosphere_haze(GWorldSceneView *self, double haze);
double gworld_scene_view_get_atmosphere_haze(GWorldSceneView *self);

/**
 * gworld_scene_view_set_water_enabled:
 * @self: a scene view
 * @enabled: whether to load water boundaries and render reflective water
 *
 * Disabled by default. Uses OSM-derived water polygons from OpenFreeMap unless
 * a custom tile URL is configured. Water data loads asynchronously and has its
 * own cache. Imagery stays visible while water data is unavailable.
 */
void gworld_scene_view_set_water_enabled(GWorldSceneView *self, gboolean enabled);
gboolean gworld_scene_view_get_water_enabled(GWorldSceneView *self);

/**
 * gworld_scene_view_set_water_wave_strength:
 * @self: a scene view
 * @strength: wave-normal strength in [0, 2], default 0.35; zero gives still water
 */
void gworld_scene_view_set_water_wave_strength(GWorldSceneView *self, double strength);
double gworld_scene_view_get_water_wave_strength(GWorldSceneView *self);

/**
 * gworld_scene_view_set_water_tile_url_template:
 * @self: a scene view
 * @url_template: (nullable): HTTP(S) MVT URL containing {z}, {x}, {y}; NULL restores the default
 *
 * Tiles must use the OpenMapTiles water layer schema. Attribution for a custom
 * provider is the application's responsibility in addition to the built-in OSM
 * and OpenMapTiles credits. Changing providers invalidates pending water work.
 */
void gworld_scene_view_set_water_tile_url_template(GWorldSceneView *self, const char *url_template);
/**
 * gworld_scene_view_get_water_tile_url_template:
 * @self: a scene view
 *
 * Returns: (transfer none): the current water tile URL template
 */
const char *gworld_scene_view_get_water_tile_url_template(GWorldSceneView *self);

void gworld_scene_view_set_shadows_enabled(GWorldSceneView *self,
                                           gboolean shadows_enabled);

gboolean gworld_scene_view_get_shadows_enabled(GWorldSceneView *self);

void gworld_scene_view_set_terrain_normal_smoothing(GWorldSceneView *self,
                                                    double smoothing);

double gworld_scene_view_get_terrain_normal_smoothing(GWorldSceneView *self);

/**
 * gworld_scene_view_sample_terrain_altitude:
 * @self: a scene view
 * @latitude: latitude in degrees
 * @longitude: longitude in degrees
 * @altitude_amsl: (out) (transfer none): terrain altitude above mean sea level in metres
 *
 * Samples already-loaded terrain. Returns %FALSE when the corresponding
 * terrain tile has not loaded yet.
 */
gboolean gworld_scene_view_sample_terrain_altitude(GWorldSceneView *self,
                                                   double latitude,
                                                   double longitude,
                                                   double *altitude_amsl);

/**
 * gworld_scene_view_add_cube:
 * @self: a scene view
 *
 * Returns: (transfer none): the view-owned cube node
 */
GWorldSceneCubeNode *gworld_scene_view_add_cube(GWorldSceneView *self,
                                                double latitude,
                                                double longitude,
                                                double altitude_amsl,
                                                double width_m,
                                                double depth_m,
                                                double height_m);

/**
 * gworld_scene_view_add_sphere:
 * @self: a scene view
 *
 * Returns: (transfer none): the view-owned sphere node
 */
GWorldSceneSphereNode *gworld_scene_view_add_sphere(GWorldSceneView *self,
                                                    double latitude,
                                                    double longitude,
                                                    double altitude_amsl,
                                                    double diameter_m);

/**
 * gworld_scene_view_add_cylinder:
 * @self: a scene view
 *
 * Returns: (transfer none): the view-owned cylinder node
 */
GWorldSceneCylinderNode *gworld_scene_view_add_cylinder(GWorldSceneView *self,
                                                        double latitude,
                                                        double longitude,
                                                        double altitude_amsl,
                                                        double diameter_m,
                                                        double height_m);

/**
 * gworld_scene_view_add_model:
 * @self: a scene view
 * @model_path: local path to a model file supported by Assimp
 *
 * The imported model is interpreted in local NED-friendly axes:
 * +Z is north/forward, +X is east/right, and +Y is up. Model units are
 * treated as metres before the node scale is applied.
 *
 * Returns: (transfer none): the view-owned model node
 */
GWorldSceneModelNode *gworld_scene_view_add_model(GWorldSceneView *self,
                                                  const char *model_path,
                                                  double latitude,
                                                  double longitude,
                                                  double altitude_amsl);

/**
 * gworld_scene_view_add_billboard:
 * @self: a scene view
 * @image_path: local path to an image file
 *
 * Adds a camera-facing image marker at the requested geodetic position. The
 * altitude is interpreted as AMSL by default; use
 * gworld_scene_billboard_node_set_altitude_mode() to interpret it as AGL.
 *
 * Returns: (transfer none): the view-owned billboard node
 */
GWorldSceneBillboardNode *gworld_scene_view_add_billboard(GWorldSceneView *self,
                                                          const char *image_path,
                                                          double latitude,
                                                          double longitude,
                                                          double altitude);

/**
 * gworld_scene_view_add_ground_overlay:
 * @self: a scene view
 * @image_path: local path to an image file
 *
 * Adds an image draped over terrain. Corner coordinates are image-space
 * corners in top-left, top-right, bottom-right, bottom-left order.
 *
 * Returns: (transfer none): the view-owned ground overlay node
 */
GWorldSceneGroundOverlayNode *gworld_scene_view_add_ground_overlay(GWorldSceneView *self,
                                                                   const char *image_path,
                                                                   double top_left_latitude,
                                                                   double top_left_longitude,
                                                                   double top_right_latitude,
                                                                   double top_right_longitude,
                                                                   double bottom_right_latitude,
                                                                   double bottom_right_longitude,
                                                                   double bottom_left_latitude,
                                                                   double bottom_left_longitude);

/**
 * gworld_scene_view_add_polyline:
 * @self: a scene view
 *
 * Adds an initially-empty geospatial polyline. Add points with
 * gworld_scene_polyline_node_append_point().
 *
 * Returns: (transfer none): the view-owned polyline node
 */
GWorldScenePolylineNode *gworld_scene_view_add_polyline(GWorldSceneView *self);

/**
 * gworld_scene_view_add_polygon:
 * @self: a scene view
 *
 * Adds an initially-empty geospatial polygon. Add points with
 * gworld_scene_polygon_node_append_point().
 *
 * Returns: (transfer none): the view-owned polygon node
 */
GWorldScenePolygonNode *gworld_scene_view_add_polygon(GWorldSceneView *self);

/**
 * gworld_scene_view_add_circle:
 * @self: a scene view
 *
 * Adds a geospatial circle centered at @latitude/@longitude.
 *
 * Returns: (transfer none): the view-owned circle node
 */
GWorldSceneCircleNode *gworld_scene_view_add_circle(GWorldSceneView *self,
                                                    double latitude,
                                                    double longitude,
                                                    double altitude_amsl,
                                                    double radius_m);

/**
 * gworld_scene_view_add_text_label:
 * @self: a scene view
 *
 * Adds a camera-facing text label at the requested geodetic position.
 *
 * Returns: (transfer none): the view-owned text label node
 */
GWorldSceneTextLabelNode *gworld_scene_view_add_text_label(GWorldSceneView *self,
                                                           const char *text,
                                                           double latitude,
                                                           double longitude,
                                                           double altitude_amsl);

gboolean gworld_scene_view_remove_node(GWorldSceneView *self, GWorldSceneNode *node);

void gworld_scene_view_clear_nodes(GWorldSceneView *self);

G_END_DECLS

#endif /* GWORLD_SCENE_VIEW_H */
