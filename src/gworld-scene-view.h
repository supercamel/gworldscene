#ifndef GWORLD_SCENE_VIEW_H
#define GWORLD_SCENE_VIEW_H

#include <gtk/gtk.h>

#include "gworld-scene-node.h"

G_BEGIN_DECLS

#define GWORLD_TYPE_SCENE_VIEW (gworld_scene_view_get_type())

G_DECLARE_FINAL_TYPE(GWorldSceneView, gworld_scene_view, GWORLD, SCENE_VIEW, GtkGLArea)

GtkWidget *gworld_scene_view_new(void);

void gworld_scene_view_set_camera(GWorldSceneView *self,
                                  double latitude,
                                  double longitude,
                                  double altitude_amsl);

void gworld_scene_view_get_camera(GWorldSceneView *self,
                                  double *latitude,
                                  double *longitude,
                                  double *altitude_amsl);

void gworld_scene_view_set_camera_orientation(GWorldSceneView *self,
                                              double heading_deg,
                                              double pitch_deg);

void gworld_scene_view_get_camera_orientation(GWorldSceneView *self,
                                              double *heading_deg,
                                              double *pitch_deg);

void gworld_scene_view_set_terrain_server(GWorldSceneView *self,
                                          const char *terrain_server);

const char *gworld_scene_view_get_terrain_server(GWorldSceneView *self);

void gworld_scene_view_set_map_tile_url_template(GWorldSceneView *self,
                                                 const char *url_template);

const char *gworld_scene_view_get_map_tile_url_template(GWorldSceneView *self);

void gworld_scene_view_set_cache_directory(GWorldSceneView *self,
                                           const char *cache_directory);

const char *gworld_scene_view_get_cache_directory(GWorldSceneView *self);

void gworld_scene_view_set_cache_enabled(GWorldSceneView *self,
                                         gboolean cache_enabled);

gboolean gworld_scene_view_get_cache_enabled(GWorldSceneView *self);

void gworld_scene_view_set_sun_position(GWorldSceneView *self,
                                        double azimuth_deg,
                                        double elevation_deg);

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

void gworld_scene_view_get_fog_range(GWorldSceneView *self,
                                     double *start_m,
                                     double *end_m);

void gworld_scene_view_set_fog_color(GWorldSceneView *self,
                                     double red,
                                     double green,
                                     double blue);

void gworld_scene_view_get_fog_color(GWorldSceneView *self,
                                     double *red,
                                     double *green,
                                     double *blue);

void gworld_scene_view_set_shadows_enabled(GWorldSceneView *self,
                                           gboolean shadows_enabled);

gboolean gworld_scene_view_get_shadows_enabled(GWorldSceneView *self);

void gworld_scene_view_set_terrain_normal_smoothing(GWorldSceneView *self,
                                                    double smoothing);

double gworld_scene_view_get_terrain_normal_smoothing(GWorldSceneView *self);

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

gboolean gworld_scene_view_remove_node(GWorldSceneView *self, GWorldSceneNode *node);

void gworld_scene_view_clear_nodes(GWorldSceneView *self);

G_END_DECLS

#endif /* GWORLD_SCENE_VIEW_H */
