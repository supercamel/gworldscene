#ifndef GWORLD_SCENE_NODE_H
#define GWORLD_SCENE_NODE_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef guint64 GWorldSceneNodeId;

typedef enum {
  GWORLD_SCENE_PRIMITIVE_CUBE,
  GWORLD_SCENE_PRIMITIVE_SPHERE,
  GWORLD_SCENE_PRIMITIVE_CYLINDER,
  GWORLD_SCENE_PRIMITIVE_MODEL,
  GWORLD_SCENE_PRIMITIVE_BILLBOARD,
  GWORLD_SCENE_PRIMITIVE_GROUND_OVERLAY,
} GWorldScenePrimitive;

typedef enum {
  GWORLD_SCENE_ALTITUDE_AMSL,
  GWORLD_SCENE_ALTITUDE_AGL,
} GWorldSceneAltitudeMode;

#define GWORLD_TYPE_SCENE_NODE (gworld_scene_node_get_type())

G_DECLARE_DERIVABLE_TYPE(GWorldSceneNode, gworld_scene_node, GWORLD, SCENE_NODE, GObject)

struct _GWorldSceneNodeClass {
  GObjectClass parent_class;
  gpointer padding[8];
};

#define GWORLD_TYPE_SCENE_CUBE_NODE (gworld_scene_cube_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneCubeNode,
                     gworld_scene_cube_node,
                     GWORLD,
                     SCENE_CUBE_NODE,
                     GWorldSceneNode)

#define GWORLD_TYPE_SCENE_SPHERE_NODE (gworld_scene_sphere_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneSphereNode,
                     gworld_scene_sphere_node,
                     GWORLD,
                     SCENE_SPHERE_NODE,
                     GWorldSceneNode)

#define GWORLD_TYPE_SCENE_CYLINDER_NODE (gworld_scene_cylinder_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneCylinderNode,
                     gworld_scene_cylinder_node,
                     GWORLD,
                     SCENE_CYLINDER_NODE,
                     GWorldSceneNode)

#define GWORLD_TYPE_SCENE_MODEL_NODE (gworld_scene_model_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneModelNode,
                     gworld_scene_model_node,
                     GWORLD,
                     SCENE_MODEL_NODE,
                     GWorldSceneNode)

#define GWORLD_TYPE_SCENE_BILLBOARD_NODE (gworld_scene_billboard_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneBillboardNode,
                     gworld_scene_billboard_node,
                     GWORLD,
                     SCENE_BILLBOARD_NODE,
                     GWorldSceneNode)

#define GWORLD_TYPE_SCENE_GROUND_OVERLAY_NODE (gworld_scene_ground_overlay_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneGroundOverlayNode,
                     gworld_scene_ground_overlay_node,
                     GWORLD,
                     SCENE_GROUND_OVERLAY_NODE,
                     GWorldSceneNode)

GWorldSceneNodeId gworld_scene_node_get_id(GWorldSceneNode *self);

GWorldScenePrimitive gworld_scene_node_get_primitive(GWorldSceneNode *self);

void gworld_scene_node_set_position(GWorldSceneNode *self,
                                    double latitude,
                                    double longitude,
                                    double altitude_amsl);

void gworld_scene_node_get_position(GWorldSceneNode *self,
                                    double *latitude,
                                    double *longitude,
                                    double *altitude_amsl);

void gworld_scene_node_translate_ned(GWorldSceneNode *self,
                                     double north_m,
                                     double east_m,
                                     double down_m);

void gworld_scene_node_slew_position(GWorldSceneNode *self,
                                     double latitude,
                                     double longitude,
                                     double altitude_amsl);

void gworld_scene_node_set_orientation_ned(GWorldSceneNode *self,
                                           double yaw_deg,
                                           double pitch_deg,
                                           double roll_deg);

void gworld_scene_node_get_orientation_ned(GWorldSceneNode *self,
                                           double *yaw_deg,
                                           double *pitch_deg,
                                           double *roll_deg);

void gworld_scene_node_rotate_ned(GWorldSceneNode *self,
                                  double yaw_delta_deg,
                                  double pitch_delta_deg,
                                  double roll_delta_deg);

void gworld_scene_node_set_scale(GWorldSceneNode *self,
                                 double scale_x,
                                 double scale_y,
                                 double scale_z);

void gworld_scene_node_get_scale(GWorldSceneNode *self,
                                 double *scale_x,
                                 double *scale_y,
                                 double *scale_z);

void gworld_scene_node_set_color(GWorldSceneNode *self,
                                 double red,
                                 double green,
                                 double blue);

void gworld_scene_node_get_color(GWorldSceneNode *self,
                                 double *red,
                                 double *green,
                                 double *blue);

void gworld_scene_cube_node_set_dimensions(GWorldSceneCubeNode *self,
                                           double width_m,
                                           double depth_m,
                                           double height_m);

void gworld_scene_cube_node_get_dimensions(GWorldSceneCubeNode *self,
                                           double *width_m,
                                           double *depth_m,
                                           double *height_m);

void gworld_scene_sphere_node_set_diameter(GWorldSceneSphereNode *self,
                                           double diameter_m);

double gworld_scene_sphere_node_get_diameter(GWorldSceneSphereNode *self);

void gworld_scene_cylinder_node_set_size(GWorldSceneCylinderNode *self,
                                         double diameter_m,
                                         double height_m);

void gworld_scene_cylinder_node_get_size(GWorldSceneCylinderNode *self,
                                         double *diameter_m,
                                         double *height_m);

void gworld_scene_model_node_set_model_path(GWorldSceneModelNode *self,
                                            const char *model_path);

const char *gworld_scene_model_node_get_model_path(GWorldSceneModelNode *self);

void gworld_scene_billboard_node_set_image_path(GWorldSceneBillboardNode *self,
                                                const char *image_path);

const char *gworld_scene_billboard_node_get_image_path(GWorldSceneBillboardNode *self);

void gworld_scene_billboard_node_set_size_limits(GWorldSceneBillboardNode *self,
                                                 double min_px,
                                                 double max_px);

void gworld_scene_billboard_node_get_size_limits(GWorldSceneBillboardNode *self,
                                                 double *min_px,
                                                 double *max_px);

void gworld_scene_billboard_node_set_reference_size(GWorldSceneBillboardNode *self,
                                                    double size_px,
                                                    double distance_m);

void gworld_scene_billboard_node_get_reference_size(GWorldSceneBillboardNode *self,
                                                    double *size_px,
                                                    double *distance_m);

void gworld_scene_billboard_node_set_max_visible_distance(GWorldSceneBillboardNode *self,
                                                          double distance_m);

double gworld_scene_billboard_node_get_max_visible_distance(GWorldSceneBillboardNode *self);

void gworld_scene_billboard_node_set_altitude_mode(GWorldSceneBillboardNode *self,
                                                   GWorldSceneAltitudeMode altitude_mode);

GWorldSceneAltitudeMode gworld_scene_billboard_node_get_altitude_mode(GWorldSceneBillboardNode *self);

void gworld_scene_ground_overlay_node_set_image_path(GWorldSceneGroundOverlayNode *self,
                                                     const char *image_path);

const char *gworld_scene_ground_overlay_node_get_image_path(GWorldSceneGroundOverlayNode *self);

void gworld_scene_ground_overlay_node_set_corners(GWorldSceneGroundOverlayNode *self,
                                                  double top_left_latitude,
                                                  double top_left_longitude,
                                                  double top_right_latitude,
                                                  double top_right_longitude,
                                                  double bottom_right_latitude,
                                                  double bottom_right_longitude,
                                                  double bottom_left_latitude,
                                                  double bottom_left_longitude);

void gworld_scene_ground_overlay_node_get_corners(GWorldSceneGroundOverlayNode *self,
                                                  double *top_left_latitude,
                                                  double *top_left_longitude,
                                                  double *top_right_latitude,
                                                  double *top_right_longitude,
                                                  double *bottom_right_latitude,
                                                  double *bottom_right_longitude,
                                                  double *bottom_left_latitude,
                                                  double *bottom_left_longitude);

void gworld_scene_ground_overlay_node_set_opacity(GWorldSceneGroundOverlayNode *self,
                                                  double opacity);

double gworld_scene_ground_overlay_node_get_opacity(GWorldSceneGroundOverlayNode *self);

void gworld_scene_ground_overlay_node_set_altitude_offset(GWorldSceneGroundOverlayNode *self,
                                                          double altitude_offset_m);

double gworld_scene_ground_overlay_node_get_altitude_offset(GWorldSceneGroundOverlayNode *self);

void gworld_scene_node_set_dimensions(GWorldSceneNode *self,
                                      double width_m,
                                      double depth_m,
                                      double height_m);

void gworld_scene_node_get_dimensions(GWorldSceneNode *self,
                                      double *width_m,
                                      double *depth_m,
                                      double *height_m);

void gworld_scene_node_set_diameter(GWorldSceneNode *self, double diameter_m);

const char *gworld_scene_node_get_model_path(GWorldSceneNode *self);

G_END_DECLS

#endif /* GWORLD_SCENE_NODE_H */
