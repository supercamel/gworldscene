#ifndef GWORLD_SCENE_VIEW_H
#define GWORLD_SCENE_VIEW_H

#include <gtk/gtk.h>

#include "gworld-scene-node.h"
#include "gworld-scene-tile-provider.h"

G_BEGIN_DECLS

#define GWORLD_TYPE_SCENE_VIEW (gworld_scene_view_get_type())

G_DECLARE_FINAL_TYPE(GWorldSceneView, gworld_scene_view, GWORLD, SCENE_VIEW, GtkGLArea)

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
