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
  GWORLD_SCENE_PRIMITIVE_POLYLINE,
  GWORLD_SCENE_PRIMITIVE_POLYGON,
  GWORLD_SCENE_PRIMITIVE_CIRCLE,
  GWORLD_SCENE_PRIMITIVE_TEXT_LABEL,
} GWorldScenePrimitive;

typedef enum {
  GWORLD_SCENE_ALTITUDE_AMSL,
  GWORLD_SCENE_ALTITUDE_AGL,
  GWORLD_SCENE_ALTITUDE_CLAMP_TO_GROUND,
} GWorldSceneAltitudeMode;

typedef struct {
  double latitude;
  double longitude;
  double altitude_amsl;
} GWorldSceneGeoPoint;

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

#define GWORLD_TYPE_SCENE_POLYLINE_NODE (gworld_scene_polyline_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldScenePolylineNode,
                     gworld_scene_polyline_node,
                     GWORLD,
                     SCENE_POLYLINE_NODE,
                     GWorldSceneNode)

#define GWORLD_TYPE_SCENE_POLYGON_NODE (gworld_scene_polygon_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldScenePolygonNode,
                     gworld_scene_polygon_node,
                     GWORLD,
                     SCENE_POLYGON_NODE,
                     GWorldSceneNode)

#define GWORLD_TYPE_SCENE_CIRCLE_NODE (gworld_scene_circle_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneCircleNode,
                     gworld_scene_circle_node,
                     GWORLD,
                     SCENE_CIRCLE_NODE,
                     GWorldSceneNode)

#define GWORLD_TYPE_SCENE_TEXT_LABEL_NODE (gworld_scene_text_label_node_get_type())
G_DECLARE_FINAL_TYPE(GWorldSceneTextLabelNode,
                     gworld_scene_text_label_node,
                     GWORLD,
                     SCENE_TEXT_LABEL_NODE,
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

void gworld_scene_polyline_node_clear_points(GWorldScenePolylineNode *self);

void gworld_scene_polyline_node_append_point(GWorldScenePolylineNode *self,
                                             double latitude,
                                             double longitude,
                                             double altitude_amsl);

/**
 * gworld_scene_polyline_node_set_points:
 * @self: a polyline node
 * @points: (array length=n_points) (nullable): geospatial points to copy
 * @n_points: number of points in @points
 */
void gworld_scene_polyline_node_set_points(GWorldScenePolylineNode *self,
                                           const GWorldSceneGeoPoint *points,
                                           gsize n_points);

/**
 * gworld_scene_polyline_node_get_points:
 * @self: a polyline node
 * @n_points: (out) (optional): return location for the number of points
 *
 * Returns: (transfer none) (array length=n_points) (nullable): the node-owned
 *   point array
 */
const GWorldSceneGeoPoint *gworld_scene_polyline_node_get_points(GWorldScenePolylineNode *self,
                                                                 gsize *n_points);

void gworld_scene_polyline_node_set_width(GWorldScenePolylineNode *self,
                                          double width_m);

double gworld_scene_polyline_node_get_width(GWorldScenePolylineNode *self);

void gworld_scene_polyline_node_set_opacity(GWorldScenePolylineNode *self,
                                            double opacity);

double gworld_scene_polyline_node_get_opacity(GWorldScenePolylineNode *self);

void gworld_scene_polyline_node_set_altitude_mode(GWorldScenePolylineNode *self,
                                                  GWorldSceneAltitudeMode altitude_mode);

GWorldSceneAltitudeMode gworld_scene_polyline_node_get_altitude_mode(GWorldScenePolylineNode *self);

void gworld_scene_polygon_node_clear_points(GWorldScenePolygonNode *self);

void gworld_scene_polygon_node_append_point(GWorldScenePolygonNode *self,
                                            double latitude,
                                            double longitude,
                                            double altitude_amsl);

/**
 * gworld_scene_polygon_node_set_points:
 * @self: a polygon node
 * @points: (array length=n_points) (nullable): geospatial points to copy
 * @n_points: number of points in @points
 */
void gworld_scene_polygon_node_set_points(GWorldScenePolygonNode *self,
                                          const GWorldSceneGeoPoint *points,
                                          gsize n_points);

/**
 * gworld_scene_polygon_node_get_points:
 * @self: a polygon node
 * @n_points: (out) (optional): return location for the number of points
 *
 * Returns: (transfer none) (array length=n_points) (nullable): the node-owned
 *   point array
 */
const GWorldSceneGeoPoint *gworld_scene_polygon_node_get_points(GWorldScenePolygonNode *self,
                                                               gsize *n_points);

void gworld_scene_polygon_node_set_fill_color(GWorldScenePolygonNode *self,
                                              double red,
                                              double green,
                                              double blue,
                                              double alpha);

void gworld_scene_polygon_node_get_fill_color(GWorldScenePolygonNode *self,
                                              double *red,
                                              double *green,
                                              double *blue,
                                              double *alpha);

void gworld_scene_polygon_node_set_outline_color(GWorldScenePolygonNode *self,
                                                 double red,
                                                 double green,
                                                 double blue,
                                                 double alpha);

void gworld_scene_polygon_node_get_outline_color(GWorldScenePolygonNode *self,
                                                 double *red,
                                                 double *green,
                                                 double *blue,
                                                 double *alpha);

void gworld_scene_polygon_node_set_outline_width(GWorldScenePolygonNode *self,
                                                 double width_m);

double gworld_scene_polygon_node_get_outline_width(GWorldScenePolygonNode *self);

void gworld_scene_polygon_node_set_altitude_mode(GWorldScenePolygonNode *self,
                                                 GWorldSceneAltitudeMode altitude_mode);

GWorldSceneAltitudeMode gworld_scene_polygon_node_get_altitude_mode(GWorldScenePolygonNode *self);

void gworld_scene_circle_node_set_radius(GWorldSceneCircleNode *self,
                                         double radius_m);

double gworld_scene_circle_node_get_radius(GWorldSceneCircleNode *self);

void gworld_scene_circle_node_set_segments(GWorldSceneCircleNode *self,
                                           guint segments);

guint gworld_scene_circle_node_get_segments(GWorldSceneCircleNode *self);

void gworld_scene_circle_node_set_fill_color(GWorldSceneCircleNode *self,
                                             double red,
                                             double green,
                                             double blue,
                                             double alpha);

void gworld_scene_circle_node_get_fill_color(GWorldSceneCircleNode *self,
                                             double *red,
                                             double *green,
                                             double *blue,
                                             double *alpha);

void gworld_scene_circle_node_set_outline_color(GWorldSceneCircleNode *self,
                                                double red,
                                                double green,
                                                double blue,
                                                double alpha);

void gworld_scene_circle_node_get_outline_color(GWorldSceneCircleNode *self,
                                                double *red,
                                                double *green,
                                                double *blue,
                                                double *alpha);

void gworld_scene_circle_node_set_outline_width(GWorldSceneCircleNode *self,
                                                double width_m);

double gworld_scene_circle_node_get_outline_width(GWorldSceneCircleNode *self);

void gworld_scene_circle_node_set_altitude_mode(GWorldSceneCircleNode *self,
                                                GWorldSceneAltitudeMode altitude_mode);

GWorldSceneAltitudeMode gworld_scene_circle_node_get_altitude_mode(GWorldSceneCircleNode *self);

void gworld_scene_text_label_node_set_text(GWorldSceneTextLabelNode *self,
                                           const char *text);

const char *gworld_scene_text_label_node_get_text(GWorldSceneTextLabelNode *self);

void gworld_scene_text_label_node_set_font(GWorldSceneTextLabelNode *self,
                                           const char *font);

const char *gworld_scene_text_label_node_get_font(GWorldSceneTextLabelNode *self);

void gworld_scene_text_label_node_set_text_color(GWorldSceneTextLabelNode *self,
                                                 double red,
                                                 double green,
                                                 double blue,
                                                 double alpha);

void gworld_scene_text_label_node_get_text_color(GWorldSceneTextLabelNode *self,
                                                 double *red,
                                                 double *green,
                                                 double *blue,
                                                 double *alpha);

void gworld_scene_text_label_node_set_background_color(GWorldSceneTextLabelNode *self,
                                                       double red,
                                                       double green,
                                                       double blue,
                                                       double alpha);

void gworld_scene_text_label_node_get_background_color(GWorldSceneTextLabelNode *self,
                                                       double *red,
                                                       double *green,
                                                       double *blue,
                                                       double *alpha);

void gworld_scene_text_label_node_set_padding(GWorldSceneTextLabelNode *self,
                                              double padding_px);

double gworld_scene_text_label_node_get_padding(GWorldSceneTextLabelNode *self);

void gworld_scene_text_label_node_set_size_limits(GWorldSceneTextLabelNode *self,
                                                  double min_px,
                                                  double max_px);

void gworld_scene_text_label_node_get_size_limits(GWorldSceneTextLabelNode *self,
                                                  double *min_px,
                                                  double *max_px);

void gworld_scene_text_label_node_set_reference_size(GWorldSceneTextLabelNode *self,
                                                     double size_px,
                                                     double distance_m);

void gworld_scene_text_label_node_get_reference_size(GWorldSceneTextLabelNode *self,
                                                     double *size_px,
                                                     double *distance_m);

void gworld_scene_text_label_node_set_max_visible_distance(GWorldSceneTextLabelNode *self,
                                                           double distance_m);

double gworld_scene_text_label_node_get_max_visible_distance(GWorldSceneTextLabelNode *self);

void gworld_scene_text_label_node_set_altitude_mode(GWorldSceneTextLabelNode *self,
                                                    GWorldSceneAltitudeMode altitude_mode);

GWorldSceneAltitudeMode gworld_scene_text_label_node_get_altitude_mode(GWorldSceneTextLabelNode *self);

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
