#ifndef GWORLD_SCENE_VIEW_H
#define GWORLD_SCENE_VIEW_H

#include <gtk/gtk.h>

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

G_END_DECLS

#endif /* GWORLD_SCENE_VIEW_H */
