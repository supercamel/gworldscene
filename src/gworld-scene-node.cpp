#include "gworld-scene-node-private.h"
#include "gworld-scene-geo-private.h"

#include <algorithm>
#include <cmath>

namespace {

enum {
  PROP_0,
  PROP_ID,
  PROP_PRIMITIVE,
  N_PROPS,
};

enum {
  CHANGED,
  N_SIGNALS,
};

GParamSpec *properties[N_PROPS];
guint signals[N_SIGNALS];

double
normalize_degrees(double degrees)
{
  double result = std::fmod(degrees, 360.0);
  if (result < 0.0)
    result += 360.0;
  return result;
}

double
positive_dimension(double value)
{
  return std::max(0.001, value);
}

double
clamp_unit(double value)
{
  return std::clamp(value, 0.0, 1.0);
}

GWorldSceneAltitudeMode
valid_altitude_mode(GWorldSceneAltitudeMode altitude_mode)
{
  if (altitude_mode == GWORLD_SCENE_ALTITUDE_AGL ||
      altitude_mode == GWORLD_SCENE_ALTITUDE_CLAMP_TO_GROUND)
    return altitude_mode;
  return GWORLD_SCENE_ALTITUDE_AMSL;
}

GWorldSceneGeoPoint
sanitized_geo_point(double latitude, double longitude, double altitude_amsl)
{
  GWorldSceneGeoPoint point;
  point.latitude = std::clamp(latitude, -90.0, 90.0);
  point.longitude = std::clamp(longitude, -180.0, 180.0);
  point.altitude_amsl = altitude_amsl;
  return point;
}

void
set_rgba(double rgba[4], double red, double green, double blue, double alpha)
{
  rgba[0] = clamp_unit(red);
  rgba[1] = clamp_unit(green);
  rgba[2] = clamp_unit(blue);
  rgba[3] = clamp_unit(alpha);
}

void
get_rgba(const double rgba[4], double *red, double *green, double *blue, double *alpha)
{
  if (red)
    *red = rgba[0];
  if (green)
    *green = rgba[1];
  if (blue)
    *blue = rgba[2];
  if (alpha)
    *alpha = rgba[3];
}

void
set_points(GArray *points, const GWorldSceneGeoPoint *source, gsize n_points)
{
  g_array_set_size(points, 0);
  if (source == nullptr || n_points == 0)
    return;

  for (gsize i = 0; i < n_points; ++i) {
    const GWorldSceneGeoPoint point =
      sanitized_geo_point(source[i].latitude, source[i].longitude, source[i].altitude_amsl);
    g_array_append_val(points, point);
  }
}

void
emit_changed(GWorldSceneNode *node)
{
  g_signal_emit(node, signals[CHANGED], 0);
}

} // namespace

struct GWorldSceneNodePrivate {
  GWorldSceneNodeId id = 0;
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_amsl = 0.0;
  double yaw_deg = 0.0;
  double pitch_deg = 0.0;
  double roll_deg = 0.0;
  double scale_x = 1.0;
  double scale_y = 1.0;
  double scale_z = 1.0;
  double red = 0.96;
  double green = 0.54;
  double blue = 0.20;
};

struct _GWorldSceneCubeNode {
  GWorldSceneNode parent_instance;

  double width_m = 10.0;
  double depth_m = 10.0;
  double height_m = 10.0;
};

struct _GWorldSceneSphereNode {
  GWorldSceneNode parent_instance;

  double diameter_m = 10.0;
};

struct _GWorldSceneCylinderNode {
  GWorldSceneNode parent_instance;

  double diameter_m = 10.0;
  double height_m = 10.0;
};

struct _GWorldSceneModelNode {
  GWorldSceneNode parent_instance;

  gchar *model_path = nullptr;
};

struct _GWorldSceneBillboardNode {
  GWorldSceneNode parent_instance;

  gchar *image_path = nullptr;
  double min_px = 24.0;
  double max_px = 96.0;
  double reference_size_px = 48.0;
  double reference_distance_m = 1000.0;
  double max_visible_distance_m = 0.0;
  GWorldSceneAltitudeMode altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
};

struct _GWorldSceneGroundOverlayNode {
  GWorldSceneNode parent_instance;

  gchar *image_path = nullptr;
  double latitude[4] = {0.0, 0.0, 0.0, 0.0};
  double longitude[4] = {0.0, 0.0, 0.0, 0.0};
  double opacity = 1.0;
  double altitude_offset_m = 1.0;
};

struct _GWorldScenePolylineNode {
  GWorldSceneNode parent_instance;

  GArray *points = nullptr;
  double width_m = 8.0;
  double opacity = 1.0;
  GWorldSceneAltitudeMode altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
};

struct _GWorldScenePolygonNode {
  GWorldSceneNode parent_instance;

  GArray *points = nullptr;
  double fill_rgba[4] = {0.1, 0.55, 0.95, 0.28};
  double outline_rgba[4] = {0.05, 0.28, 0.85, 0.9};
  double outline_width_m = 6.0;
  GWorldSceneAltitudeMode altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
};

struct _GWorldSceneCircleNode {
  GWorldSceneNode parent_instance;

  double radius_m = 100.0;
  guint segments = 96;
  double fill_rgba[4] = {0.1, 0.55, 0.95, 0.22};
  double outline_rgba[4] = {0.05, 0.28, 0.85, 0.9};
  double outline_width_m = 6.0;
  GWorldSceneAltitudeMode altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
};

struct _GWorldSceneTextLabelNode {
  GWorldSceneNode parent_instance;

  gchar *text = nullptr;
  gchar *font = nullptr;
  double text_rgba[4] = {1.0, 1.0, 1.0, 1.0};
  double background_rgba[4] = {0.02, 0.025, 0.035, 0.72};
  double padding_px = 6.0;
  double min_px = 36.0;
  double max_px = 220.0;
  double reference_size_px = 96.0;
  double reference_distance_m = 3000.0;
  double max_visible_distance_m = 0.0;
  GWorldSceneAltitudeMode altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
};

G_DEFINE_TYPE_WITH_PRIVATE(GWorldSceneNode, gworld_scene_node, G_TYPE_OBJECT)
G_DEFINE_TYPE(GWorldSceneCubeNode, gworld_scene_cube_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneSphereNode, gworld_scene_sphere_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneCylinderNode, gworld_scene_cylinder_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneModelNode, gworld_scene_model_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneBillboardNode, gworld_scene_billboard_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneGroundOverlayNode, gworld_scene_ground_overlay_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldScenePolylineNode, gworld_scene_polyline_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldScenePolygonNode, gworld_scene_polygon_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneCircleNode, gworld_scene_circle_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneTextLabelNode, gworld_scene_text_label_node, GWORLD_TYPE_SCENE_NODE)

static GWorldScenePrimitive
primitive_for_node(GWorldSceneNode *self)
{
  if (GWORLD_IS_SCENE_CUBE_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_CUBE;
  if (GWORLD_IS_SCENE_SPHERE_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_SPHERE;
  if (GWORLD_IS_SCENE_CYLINDER_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_CYLINDER;
  if (GWORLD_IS_SCENE_MODEL_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_MODEL;
  if (GWORLD_IS_SCENE_BILLBOARD_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_BILLBOARD;
  if (GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_GROUND_OVERLAY;
  if (GWORLD_IS_SCENE_POLYLINE_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_POLYLINE;
  if (GWORLD_IS_SCENE_POLYGON_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_POLYGON;
  if (GWORLD_IS_SCENE_CIRCLE_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_CIRCLE;
  if (GWORLD_IS_SCENE_TEXT_LABEL_NODE(self))
    return GWORLD_SCENE_PRIMITIVE_TEXT_LABEL;
  return GWORLD_SCENE_PRIMITIVE_CUBE;
}

static GWorldSceneNodePrivate *
node_priv(GWorldSceneNode *self)
{
  return static_cast<GWorldSceneNodePrivate *>(gworld_scene_node_get_instance_private(self));
}

static void
initialize_node(GWorldSceneNode *node,
                GWorldSceneNodeId id,
                double latitude,
                double longitude,
                double altitude_amsl)
{
  auto *priv = node_priv(node);
  priv->id = id;
  priv->latitude = std::clamp(latitude, -90.0, 90.0);
  priv->longitude = std::clamp(longitude, -180.0, 180.0);
  priv->altitude_amsl = altitude_amsl;
}

static void
gworld_scene_node_get_property(GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  auto *self = GWORLD_SCENE_NODE(object);

  switch (prop_id) {
  case PROP_ID:
    g_value_set_uint64(value, node_priv(self)->id);
    break;
  case PROP_PRIMITIVE:
    g_value_set_uint(value, primitive_for_node(self));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
gworld_scene_node_class_init(GWorldSceneNodeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->get_property = gworld_scene_node_get_property;

  properties[PROP_ID] =
    g_param_spec_uint64("id",
                        "ID",
                        "Stable scene node identifier",
                        0,
                        G_MAXUINT64,
                        0,
                        static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  properties[PROP_PRIMITIVE] =
    g_param_spec_uint("primitive",
                      "Primitive",
                      "Scene primitive compatibility kind",
                      GWORLD_SCENE_PRIMITIVE_CUBE,
                      GWORLD_SCENE_PRIMITIVE_TEXT_LABEL,
                      GWORLD_SCENE_PRIMITIVE_CUBE,
                      static_cast<GParamFlags>(G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_properties(object_class, N_PROPS, properties);

  signals[CHANGED] =
    g_signal_new("changed",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 nullptr,
                 nullptr,
                 nullptr,
                 G_TYPE_NONE,
                 0);
}

static void
gworld_scene_node_init(GWorldSceneNode *self)
{
  auto *priv = node_priv(self);
  priv->id = 0;
  priv->latitude = 0.0;
  priv->longitude = 0.0;
  priv->altitude_amsl = 0.0;
  priv->yaw_deg = 0.0;
  priv->pitch_deg = 0.0;
  priv->roll_deg = 0.0;
  priv->scale_x = 1.0;
  priv->scale_y = 1.0;
  priv->scale_z = 1.0;
  priv->red = 0.96;
  priv->green = 0.54;
  priv->blue = 0.20;
}

static void
gworld_scene_cube_node_class_init(GWorldSceneCubeNodeClass *klass)
{
  (void)klass;
}

static void
gworld_scene_cube_node_init(GWorldSceneCubeNode *self)
{
  self->width_m = 10.0;
  self->depth_m = 10.0;
  self->height_m = 10.0;
}

static void
gworld_scene_sphere_node_class_init(GWorldSceneSphereNodeClass *klass)
{
  (void)klass;
}

static void
gworld_scene_sphere_node_init(GWorldSceneSphereNode *self)
{
  self->diameter_m = 10.0;
}

static void
gworld_scene_cylinder_node_class_init(GWorldSceneCylinderNodeClass *klass)
{
  (void)klass;
}

static void
gworld_scene_cylinder_node_init(GWorldSceneCylinderNode *self)
{
  self->diameter_m = 10.0;
  self->height_m = 10.0;
}

static void
gworld_scene_model_node_finalize(GObject *object)
{
  auto *self = GWORLD_SCENE_MODEL_NODE(object);
  g_clear_pointer(&self->model_path, g_free);
  G_OBJECT_CLASS(gworld_scene_model_node_parent_class)->finalize(object);
}

static void
gworld_scene_model_node_class_init(GWorldSceneModelNodeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = gworld_scene_model_node_finalize;
}

static void
gworld_scene_model_node_init(GWorldSceneModelNode *self)
{
  self->model_path = nullptr;
  auto *priv = node_priv(GWORLD_SCENE_NODE(self));
  priv->red = 1.0;
  priv->green = 1.0;
  priv->blue = 1.0;
}

static void
gworld_scene_billboard_node_finalize(GObject *object)
{
  auto *self = GWORLD_SCENE_BILLBOARD_NODE(object);
  g_clear_pointer(&self->image_path, g_free);
  G_OBJECT_CLASS(gworld_scene_billboard_node_parent_class)->finalize(object);
}

static void
gworld_scene_billboard_node_class_init(GWorldSceneBillboardNodeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = gworld_scene_billboard_node_finalize;
}

static void
gworld_scene_billboard_node_init(GWorldSceneBillboardNode *self)
{
  self->image_path = nullptr;
  self->min_px = 24.0;
  self->max_px = 96.0;
  self->reference_size_px = 48.0;
  self->reference_distance_m = 1000.0;
  self->max_visible_distance_m = 0.0;
  self->altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
  auto *priv = node_priv(GWORLD_SCENE_NODE(self));
  priv->red = 1.0;
  priv->green = 1.0;
  priv->blue = 1.0;
}

static void
set_ground_overlay_corner_values(GWorldSceneGroundOverlayNode *self,
                                 double top_left_latitude,
                                 double top_left_longitude,
                                 double top_right_latitude,
                                 double top_right_longitude,
                                 double bottom_right_latitude,
                                 double bottom_right_longitude,
                                 double bottom_left_latitude,
                                 double bottom_left_longitude)
{
  self->latitude[0] = std::clamp(top_left_latitude, -90.0, 90.0);
  self->longitude[0] = std::clamp(top_left_longitude, -180.0, 180.0);
  self->latitude[1] = std::clamp(top_right_latitude, -90.0, 90.0);
  self->longitude[1] = std::clamp(top_right_longitude, -180.0, 180.0);
  self->latitude[2] = std::clamp(bottom_right_latitude, -90.0, 90.0);
  self->longitude[2] = std::clamp(bottom_right_longitude, -180.0, 180.0);
  self->latitude[3] = std::clamp(bottom_left_latitude, -90.0, 90.0);
  self->longitude[3] = std::clamp(bottom_left_longitude, -180.0, 180.0);
}

static void
gworld_scene_ground_overlay_node_finalize(GObject *object)
{
  auto *self = GWORLD_SCENE_GROUND_OVERLAY_NODE(object);
  g_clear_pointer(&self->image_path, g_free);
  G_OBJECT_CLASS(gworld_scene_ground_overlay_node_parent_class)->finalize(object);
}

static void
gworld_scene_ground_overlay_node_class_init(GWorldSceneGroundOverlayNodeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = gworld_scene_ground_overlay_node_finalize;
}

static void
gworld_scene_ground_overlay_node_init(GWorldSceneGroundOverlayNode *self)
{
  self->image_path = nullptr;
  self->opacity = 1.0;
  self->altitude_offset_m = 1.0;
  for (int i = 0; i < 4; ++i) {
    self->latitude[i] = 0.0;
    self->longitude[i] = 0.0;
  }
  auto *priv = node_priv(GWORLD_SCENE_NODE(self));
  priv->red = 1.0;
  priv->green = 1.0;
  priv->blue = 1.0;
}

static void
gworld_scene_polyline_node_finalize(GObject *object)
{
  auto *self = GWORLD_SCENE_POLYLINE_NODE(object);
  g_clear_pointer(&self->points, g_array_unref);
  G_OBJECT_CLASS(gworld_scene_polyline_node_parent_class)->finalize(object);
}

static void
gworld_scene_polyline_node_class_init(GWorldScenePolylineNodeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = gworld_scene_polyline_node_finalize;
}

static void
gworld_scene_polyline_node_init(GWorldScenePolylineNode *self)
{
  self->points = g_array_new(FALSE, FALSE, sizeof(GWorldSceneGeoPoint));
  self->width_m = 8.0;
  self->opacity = 1.0;
  self->altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
  auto *priv = node_priv(GWORLD_SCENE_NODE(self));
  priv->red = 0.1;
  priv->green = 0.55;
  priv->blue = 0.95;
}

static void
gworld_scene_polygon_node_finalize(GObject *object)
{
  auto *self = GWORLD_SCENE_POLYGON_NODE(object);
  g_clear_pointer(&self->points, g_array_unref);
  G_OBJECT_CLASS(gworld_scene_polygon_node_parent_class)->finalize(object);
}

static void
gworld_scene_polygon_node_class_init(GWorldScenePolygonNodeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = gworld_scene_polygon_node_finalize;
}

static void
gworld_scene_polygon_node_init(GWorldScenePolygonNode *self)
{
  self->points = g_array_new(FALSE, FALSE, sizeof(GWorldSceneGeoPoint));
  set_rgba(self->fill_rgba, 0.1, 0.55, 0.95, 0.28);
  set_rgba(self->outline_rgba, 0.05, 0.28, 0.85, 0.9);
  self->outline_width_m = 6.0;
  self->altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
}

static void
gworld_scene_circle_node_class_init(GWorldSceneCircleNodeClass *klass)
{
  (void)klass;
}

static void
gworld_scene_circle_node_init(GWorldSceneCircleNode *self)
{
  self->radius_m = 100.0;
  self->segments = 96;
  set_rgba(self->fill_rgba, 0.1, 0.55, 0.95, 0.22);
  set_rgba(self->outline_rgba, 0.05, 0.28, 0.85, 0.9);
  self->outline_width_m = 6.0;
  self->altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
}

static void
gworld_scene_text_label_node_finalize(GObject *object)
{
  auto *self = GWORLD_SCENE_TEXT_LABEL_NODE(object);
  g_clear_pointer(&self->text, g_free);
  g_clear_pointer(&self->font, g_free);
  G_OBJECT_CLASS(gworld_scene_text_label_node_parent_class)->finalize(object);
}

static void
gworld_scene_text_label_node_class_init(GWorldSceneTextLabelNodeClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = gworld_scene_text_label_node_finalize;
}

static void
gworld_scene_text_label_node_init(GWorldSceneTextLabelNode *self)
{
  self->text = nullptr;
  self->font = g_strdup("Sans Bold 18");
  set_rgba(self->text_rgba, 1.0, 1.0, 1.0, 1.0);
  set_rgba(self->background_rgba, 0.02, 0.025, 0.035, 0.72);
  self->padding_px = 6.0;
  self->min_px = 36.0;
  self->max_px = 220.0;
  self->reference_size_px = 96.0;
  self->reference_distance_m = 3000.0;
  self->max_visible_distance_m = 0.0;
  self->altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
  auto *priv = node_priv(GWORLD_SCENE_NODE(self));
  priv->red = 1.0;
  priv->green = 1.0;
  priv->blue = 1.0;
}

GWorldSceneCubeNode *
_gworld_scene_cube_node_new(GWorldSceneNodeId id,
                            double latitude,
                            double longitude,
                            double altitude_amsl,
                            double width_m,
                            double depth_m,
                            double height_m)
{
  auto *node = GWORLD_SCENE_CUBE_NODE(g_object_new(GWORLD_TYPE_SCENE_CUBE_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, latitude, longitude, altitude_amsl);
  node->width_m = positive_dimension(width_m);
  node->depth_m = positive_dimension(depth_m);
  node->height_m = positive_dimension(height_m);
  return node;
}

GWorldSceneSphereNode *
_gworld_scene_sphere_node_new(GWorldSceneNodeId id,
                              double latitude,
                              double longitude,
                              double altitude_amsl,
                              double diameter_m)
{
  auto *node = GWORLD_SCENE_SPHERE_NODE(g_object_new(GWORLD_TYPE_SCENE_SPHERE_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, latitude, longitude, altitude_amsl);
  node->diameter_m = positive_dimension(diameter_m);
  return node;
}

GWorldSceneCylinderNode *
_gworld_scene_cylinder_node_new(GWorldSceneNodeId id,
                                double latitude,
                                double longitude,
                                double altitude_amsl,
                                double diameter_m,
                                double height_m)
{
  auto *node = GWORLD_SCENE_CYLINDER_NODE(g_object_new(GWORLD_TYPE_SCENE_CYLINDER_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, latitude, longitude, altitude_amsl);
  node->diameter_m = positive_dimension(diameter_m);
  node->height_m = positive_dimension(height_m);
  return node;
}

GWorldSceneModelNode *
_gworld_scene_model_node_new(GWorldSceneNodeId id,
                             const char *model_path,
                             double latitude,
                             double longitude,
                             double altitude_amsl)
{
  auto *node = GWORLD_SCENE_MODEL_NODE(g_object_new(GWORLD_TYPE_SCENE_MODEL_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, latitude, longitude, altitude_amsl);
  node->model_path = g_strdup(model_path ? model_path : "");
  return node;
}

GWorldSceneBillboardNode *
_gworld_scene_billboard_node_new(GWorldSceneNodeId id,
                                 const char *image_path,
                                 double latitude,
                                 double longitude,
                                 double altitude)
{
  auto *node = GWORLD_SCENE_BILLBOARD_NODE(g_object_new(GWORLD_TYPE_SCENE_BILLBOARD_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, latitude, longitude, altitude);
  node->image_path = g_strdup(image_path ? image_path : "");
  return node;
}

GWorldSceneGroundOverlayNode *
_gworld_scene_ground_overlay_node_new(GWorldSceneNodeId id,
                                      const char *image_path,
                                      double top_left_latitude,
                                      double top_left_longitude,
                                      double top_right_latitude,
                                      double top_right_longitude,
                                      double bottom_right_latitude,
                                      double bottom_right_longitude,
                                      double bottom_left_latitude,
                                      double bottom_left_longitude)
{
  auto *node = GWORLD_SCENE_GROUND_OVERLAY_NODE(g_object_new(GWORLD_TYPE_SCENE_GROUND_OVERLAY_NODE, nullptr));
  const double center_latitude =
    (top_left_latitude + top_right_latitude + bottom_right_latitude + bottom_left_latitude) * 0.25;
  const double center_longitude =
    (top_left_longitude + top_right_longitude + bottom_right_longitude + bottom_left_longitude) * 0.25;
  initialize_node(GWORLD_SCENE_NODE(node), id, center_latitude, center_longitude, 0.0);
  node->image_path = g_strdup(image_path ? image_path : "");
  set_ground_overlay_corner_values(node,
                                   top_left_latitude,
                                   top_left_longitude,
                                   top_right_latitude,
                                   top_right_longitude,
                                   bottom_right_latitude,
                                   bottom_right_longitude,
                                   bottom_left_latitude,
                                   bottom_left_longitude);
  return node;
}

GWorldScenePolylineNode *
_gworld_scene_polyline_node_new(GWorldSceneNodeId id)
{
  auto *node = GWORLD_SCENE_POLYLINE_NODE(g_object_new(GWORLD_TYPE_SCENE_POLYLINE_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, 0.0, 0.0, 0.0);
  return node;
}

GWorldScenePolygonNode *
_gworld_scene_polygon_node_new(GWorldSceneNodeId id)
{
  auto *node = GWORLD_SCENE_POLYGON_NODE(g_object_new(GWORLD_TYPE_SCENE_POLYGON_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, 0.0, 0.0, 0.0);
  return node;
}

GWorldSceneCircleNode *
_gworld_scene_circle_node_new(GWorldSceneNodeId id,
                              double latitude,
                              double longitude,
                              double altitude_amsl,
                              double radius_m)
{
  auto *node = GWORLD_SCENE_CIRCLE_NODE(g_object_new(GWORLD_TYPE_SCENE_CIRCLE_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, latitude, longitude, altitude_amsl);
  node->radius_m = positive_dimension(radius_m);
  return node;
}

GWorldSceneTextLabelNode *
_gworld_scene_text_label_node_new(GWorldSceneNodeId id,
                                  const char *text,
                                  double latitude,
                                  double longitude,
                                  double altitude_amsl)
{
  auto *node = GWORLD_SCENE_TEXT_LABEL_NODE(g_object_new(GWORLD_TYPE_SCENE_TEXT_LABEL_NODE, nullptr));
  initialize_node(GWORLD_SCENE_NODE(node), id, latitude, longitude, altitude_amsl);
  g_free(node->text);
  node->text = g_strdup(text ? text : "");
  return node;
}

GWorldSceneNodeId
gworld_scene_node_get_id(GWorldSceneNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_NODE(self), 0);
  return node_priv(self)->id;
}

GWorldScenePrimitive
gworld_scene_node_get_primitive(GWorldSceneNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_NODE(self), GWORLD_SCENE_PRIMITIVE_CUBE);
  return primitive_for_node(self);
}

void
gworld_scene_node_set_position(GWorldSceneNode *self,
                               double latitude,
                               double longitude,
                               double altitude_amsl)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  priv->latitude = std::clamp(latitude, -90.0, 90.0);
  priv->longitude = std::clamp(longitude, -180.0, 180.0);
  priv->altitude_amsl = altitude_amsl;
  emit_changed(self);
}

void
gworld_scene_node_get_position(GWorldSceneNode *self,
                               double *latitude,
                               double *longitude,
                               double *altitude_amsl)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  if (latitude)
    *latitude = priv->latitude;
  if (longitude)
    *longitude = priv->longitude;
  if (altitude_amsl)
    *altitude_amsl = priv->altitude_amsl;
}

void
gworld_scene_node_translate_ned(GWorldSceneNode *self,
                                double north_m,
                                double east_m,
                                double down_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  gworld_scene::translate_geodetic_ned(priv->latitude,
                                       priv->longitude,
                                       priv->altitude_amsl,
                                       north_m,
                                       east_m,
                                       down_m,
                                       &priv->latitude,
                                       &priv->longitude,
                                       &priv->altitude_amsl);
  emit_changed(self);
}

void
gworld_scene_node_slew_position(GWorldSceneNode *self,
                                double latitude,
                                double longitude,
                                double altitude_amsl)
{
  gworld_scene_node_set_position(self, latitude, longitude, altitude_amsl);
}

void
gworld_scene_node_set_orientation_ned(GWorldSceneNode *self,
                                      double yaw_deg,
                                      double pitch_deg,
                                      double roll_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  priv->yaw_deg = normalize_degrees(yaw_deg);
  priv->pitch_deg = pitch_deg;
  priv->roll_deg = roll_deg;
  emit_changed(self);
}

void
gworld_scene_node_get_orientation_ned(GWorldSceneNode *self,
                                      double *yaw_deg,
                                      double *pitch_deg,
                                      double *roll_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  if (yaw_deg)
    *yaw_deg = priv->yaw_deg;
  if (pitch_deg)
    *pitch_deg = priv->pitch_deg;
  if (roll_deg)
    *roll_deg = priv->roll_deg;
}

void
gworld_scene_node_rotate_ned(GWorldSceneNode *self,
                             double yaw_delta_deg,
                             double pitch_delta_deg,
                             double roll_delta_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  priv->yaw_deg = normalize_degrees(priv->yaw_deg + yaw_delta_deg);
  priv->pitch_deg += pitch_delta_deg;
  priv->roll_deg += roll_delta_deg;
  emit_changed(self);
}

void
gworld_scene_node_set_scale(GWorldSceneNode *self,
                            double scale_x,
                            double scale_y,
                            double scale_z)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  priv->scale_x = positive_dimension(scale_x);
  priv->scale_y = positive_dimension(scale_y);
  priv->scale_z = positive_dimension(scale_z);
  emit_changed(self);
}

void
gworld_scene_node_get_scale(GWorldSceneNode *self,
                            double *scale_x,
                            double *scale_y,
                            double *scale_z)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  if (scale_x)
    *scale_x = priv->scale_x;
  if (scale_y)
    *scale_y = priv->scale_y;
  if (scale_z)
    *scale_z = priv->scale_z;
}

void
gworld_scene_node_set_color(GWorldSceneNode *self,
                            double red,
                            double green,
                            double blue)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  priv->red = std::clamp(red, 0.0, 1.0);
  priv->green = std::clamp(green, 0.0, 1.0);
  priv->blue = std::clamp(blue, 0.0, 1.0);
  emit_changed(self);
}

void
gworld_scene_node_get_color(GWorldSceneNode *self,
                            double *red,
                            double *green,
                            double *blue)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  auto *priv = node_priv(self);
  if (red)
    *red = priv->red;
  if (green)
    *green = priv->green;
  if (blue)
    *blue = priv->blue;
}

void
gworld_scene_cube_node_set_dimensions(GWorldSceneCubeNode *self,
                                      double width_m,
                                      double depth_m,
                                      double height_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_CUBE_NODE(self));
  self->width_m = positive_dimension(width_m);
  self->depth_m = positive_dimension(depth_m);
  self->height_m = positive_dimension(height_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_cube_node_get_dimensions(GWorldSceneCubeNode *self,
                                      double *width_m,
                                      double *depth_m,
                                      double *height_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_CUBE_NODE(self));
  if (width_m)
    *width_m = self->width_m;
  if (depth_m)
    *depth_m = self->depth_m;
  if (height_m)
    *height_m = self->height_m;
}

void
gworld_scene_sphere_node_set_diameter(GWorldSceneSphereNode *self, double diameter_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_SPHERE_NODE(self));
  self->diameter_m = positive_dimension(diameter_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_sphere_node_get_diameter(GWorldSceneSphereNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_SPHERE_NODE(self), 0.0);
  return self->diameter_m;
}

void
gworld_scene_cylinder_node_set_size(GWorldSceneCylinderNode *self,
                                    double diameter_m,
                                    double height_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_CYLINDER_NODE(self));
  self->diameter_m = positive_dimension(diameter_m);
  self->height_m = positive_dimension(height_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_cylinder_node_get_size(GWorldSceneCylinderNode *self,
                                    double *diameter_m,
                                    double *height_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_CYLINDER_NODE(self));
  if (diameter_m)
    *diameter_m = self->diameter_m;
  if (height_m)
    *height_m = self->height_m;
}

void
gworld_scene_model_node_set_model_path(GWorldSceneModelNode *self, const char *model_path)
{
  g_return_if_fail(GWORLD_IS_SCENE_MODEL_NODE(self));
  g_free(self->model_path);
  self->model_path = g_strdup(model_path ? model_path : "");
  emit_changed(GWORLD_SCENE_NODE(self));
}

const char *
gworld_scene_model_node_get_model_path(GWorldSceneModelNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_MODEL_NODE(self), nullptr);
  return self->model_path != nullptr && self->model_path[0] != '\0' ? self->model_path : nullptr;
}

void
gworld_scene_billboard_node_set_image_path(GWorldSceneBillboardNode *self, const char *image_path)
{
  g_return_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self));
  g_free(self->image_path);
  self->image_path = g_strdup(image_path ? image_path : "");
  emit_changed(GWORLD_SCENE_NODE(self));
}

const char *
gworld_scene_billboard_node_get_image_path(GWorldSceneBillboardNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self), nullptr);
  return self->image_path != nullptr && self->image_path[0] != '\0' ? self->image_path : nullptr;
}

void
gworld_scene_billboard_node_set_size_limits(GWorldSceneBillboardNode *self,
                                            double min_px,
                                            double max_px)
{
  g_return_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self));
  self->min_px = std::max(1.0, min_px);
  self->max_px = std::max(self->min_px, max_px);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_billboard_node_get_size_limits(GWorldSceneBillboardNode *self,
                                            double *min_px,
                                            double *max_px)
{
  g_return_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self));
  if (min_px)
    *min_px = self->min_px;
  if (max_px)
    *max_px = self->max_px;
}

void
gworld_scene_billboard_node_set_reference_size(GWorldSceneBillboardNode *self,
                                               double size_px,
                                               double distance_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self));
  self->reference_size_px = std::max(1.0, size_px);
  self->reference_distance_m = std::max(1.0, distance_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_billboard_node_get_reference_size(GWorldSceneBillboardNode *self,
                                               double *size_px,
                                               double *distance_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self));
  if (size_px)
    *size_px = self->reference_size_px;
  if (distance_m)
    *distance_m = self->reference_distance_m;
}

void
gworld_scene_billboard_node_set_max_visible_distance(GWorldSceneBillboardNode *self,
                                                     double distance_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self));
  self->max_visible_distance_m = std::max(0.0, distance_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_billboard_node_get_max_visible_distance(GWorldSceneBillboardNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self), 0.0);
  return self->max_visible_distance_m;
}

void
gworld_scene_billboard_node_set_altitude_mode(GWorldSceneBillboardNode *self,
                                              GWorldSceneAltitudeMode altitude_mode)
{
  g_return_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self));
  if (altitude_mode != GWORLD_SCENE_ALTITUDE_AGL)
    altitude_mode = GWORLD_SCENE_ALTITUDE_AMSL;
  self->altitude_mode = altitude_mode;
  emit_changed(GWORLD_SCENE_NODE(self));
}

GWorldSceneAltitudeMode
gworld_scene_billboard_node_get_altitude_mode(GWorldSceneBillboardNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_BILLBOARD_NODE(self), GWORLD_SCENE_ALTITUDE_AMSL);
  return self->altitude_mode;
}

void
gworld_scene_ground_overlay_node_set_image_path(GWorldSceneGroundOverlayNode *self,
                                                const char *image_path)
{
  g_return_if_fail(GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self));
  g_free(self->image_path);
  self->image_path = g_strdup(image_path ? image_path : "");
  emit_changed(GWORLD_SCENE_NODE(self));
}

const char *
gworld_scene_ground_overlay_node_get_image_path(GWorldSceneGroundOverlayNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self), nullptr);
  return self->image_path != nullptr && self->image_path[0] != '\0' ? self->image_path : nullptr;
}

void
gworld_scene_ground_overlay_node_set_corners(GWorldSceneGroundOverlayNode *self,
                                             double top_left_latitude,
                                             double top_left_longitude,
                                             double top_right_latitude,
                                             double top_right_longitude,
                                             double bottom_right_latitude,
                                             double bottom_right_longitude,
                                             double bottom_left_latitude,
                                             double bottom_left_longitude)
{
  g_return_if_fail(GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self));
  set_ground_overlay_corner_values(self,
                                   top_left_latitude,
                                   top_left_longitude,
                                   top_right_latitude,
                                   top_right_longitude,
                                   bottom_right_latitude,
                                   bottom_right_longitude,
                                   bottom_left_latitude,
                                   bottom_left_longitude);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_ground_overlay_node_get_corners(GWorldSceneGroundOverlayNode *self,
                                             double *top_left_latitude,
                                             double *top_left_longitude,
                                             double *top_right_latitude,
                                             double *top_right_longitude,
                                             double *bottom_right_latitude,
                                             double *bottom_right_longitude,
                                             double *bottom_left_latitude,
                                             double *bottom_left_longitude)
{
  g_return_if_fail(GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self));
  if (top_left_latitude)
    *top_left_latitude = self->latitude[0];
  if (top_left_longitude)
    *top_left_longitude = self->longitude[0];
  if (top_right_latitude)
    *top_right_latitude = self->latitude[1];
  if (top_right_longitude)
    *top_right_longitude = self->longitude[1];
  if (bottom_right_latitude)
    *bottom_right_latitude = self->latitude[2];
  if (bottom_right_longitude)
    *bottom_right_longitude = self->longitude[2];
  if (bottom_left_latitude)
    *bottom_left_latitude = self->latitude[3];
  if (bottom_left_longitude)
    *bottom_left_longitude = self->longitude[3];
}

void
gworld_scene_ground_overlay_node_set_opacity(GWorldSceneGroundOverlayNode *self,
                                             double opacity)
{
  g_return_if_fail(GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self));
  self->opacity = std::clamp(opacity, 0.0, 1.0);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_ground_overlay_node_get_opacity(GWorldSceneGroundOverlayNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self), 0.0);
  return self->opacity;
}

void
gworld_scene_ground_overlay_node_set_altitude_offset(GWorldSceneGroundOverlayNode *self,
                                                     double altitude_offset_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self));
  self->altitude_offset_m = std::clamp(altitude_offset_m, -1000.0, 10000.0);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_ground_overlay_node_get_altitude_offset(GWorldSceneGroundOverlayNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(self), 0.0);
  return self->altitude_offset_m;
}

void
gworld_scene_polyline_node_clear_points(GWorldScenePolylineNode *self)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self));
  g_array_set_size(self->points, 0);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_polyline_node_append_point(GWorldScenePolylineNode *self,
                                        double latitude,
                                        double longitude,
                                        double altitude_amsl)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self));
  const GWorldSceneGeoPoint point = sanitized_geo_point(latitude, longitude, altitude_amsl);
  g_array_append_val(self->points, point);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_polyline_node_set_points(GWorldScenePolylineNode *self,
                                      const GWorldSceneGeoPoint *points,
                                      gsize n_points)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self));
  set_points(self->points, points, n_points);
  emit_changed(GWORLD_SCENE_NODE(self));
}

const GWorldSceneGeoPoint *
gworld_scene_polyline_node_get_points(GWorldScenePolylineNode *self, gsize *n_points)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self), nullptr);
  if (n_points)
    *n_points = self->points != nullptr ? self->points->len : 0;
  return self->points != nullptr && self->points->len > 0
           ? reinterpret_cast<const GWorldSceneGeoPoint *>(self->points->data)
           : nullptr;
}

void
gworld_scene_polyline_node_set_width(GWorldScenePolylineNode *self, double width_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self));
  self->width_m = positive_dimension(width_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_polyline_node_get_width(GWorldScenePolylineNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self), 0.0);
  return self->width_m;
}

void
gworld_scene_polyline_node_set_opacity(GWorldScenePolylineNode *self, double opacity)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self));
  self->opacity = clamp_unit(opacity);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_polyline_node_get_opacity(GWorldScenePolylineNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self), 0.0);
  return self->opacity;
}

void
gworld_scene_polyline_node_set_altitude_mode(GWorldScenePolylineNode *self,
                                             GWorldSceneAltitudeMode altitude_mode)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self));
  self->altitude_mode = valid_altitude_mode(altitude_mode);
  emit_changed(GWORLD_SCENE_NODE(self));
}

GWorldSceneAltitudeMode
gworld_scene_polyline_node_get_altitude_mode(GWorldScenePolylineNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_POLYLINE_NODE(self), GWORLD_SCENE_ALTITUDE_AMSL);
  return self->altitude_mode;
}

void
gworld_scene_polygon_node_clear_points(GWorldScenePolygonNode *self)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  g_array_set_size(self->points, 0);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_polygon_node_append_point(GWorldScenePolygonNode *self,
                                       double latitude,
                                       double longitude,
                                       double altitude_amsl)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  const GWorldSceneGeoPoint point = sanitized_geo_point(latitude, longitude, altitude_amsl);
  g_array_append_val(self->points, point);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_polygon_node_set_points(GWorldScenePolygonNode *self,
                                     const GWorldSceneGeoPoint *points,
                                     gsize n_points)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  set_points(self->points, points, n_points);
  emit_changed(GWORLD_SCENE_NODE(self));
}

const GWorldSceneGeoPoint *
gworld_scene_polygon_node_get_points(GWorldScenePolygonNode *self, gsize *n_points)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self), nullptr);
  if (n_points)
    *n_points = self->points != nullptr ? self->points->len : 0;
  return self->points != nullptr && self->points->len > 0
           ? reinterpret_cast<const GWorldSceneGeoPoint *>(self->points->data)
           : nullptr;
}

void
gworld_scene_polygon_node_set_fill_color(GWorldScenePolygonNode *self,
                                         double red,
                                         double green,
                                         double blue,
                                         double alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  set_rgba(self->fill_rgba, red, green, blue, alpha);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_polygon_node_get_fill_color(GWorldScenePolygonNode *self,
                                         double *red,
                                         double *green,
                                         double *blue,
                                         double *alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  get_rgba(self->fill_rgba, red, green, blue, alpha);
}

void
gworld_scene_polygon_node_set_outline_color(GWorldScenePolygonNode *self,
                                            double red,
                                            double green,
                                            double blue,
                                            double alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  set_rgba(self->outline_rgba, red, green, blue, alpha);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_polygon_node_get_outline_color(GWorldScenePolygonNode *self,
                                            double *red,
                                            double *green,
                                            double *blue,
                                            double *alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  get_rgba(self->outline_rgba, red, green, blue, alpha);
}

void
gworld_scene_polygon_node_set_outline_width(GWorldScenePolygonNode *self, double width_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  self->outline_width_m = std::max(0.0, width_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_polygon_node_get_outline_width(GWorldScenePolygonNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self), 0.0);
  return self->outline_width_m;
}

void
gworld_scene_polygon_node_set_altitude_mode(GWorldScenePolygonNode *self,
                                            GWorldSceneAltitudeMode altitude_mode)
{
  g_return_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self));
  self->altitude_mode = valid_altitude_mode(altitude_mode);
  emit_changed(GWORLD_SCENE_NODE(self));
}

GWorldSceneAltitudeMode
gworld_scene_polygon_node_get_altitude_mode(GWorldScenePolygonNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_POLYGON_NODE(self), GWORLD_SCENE_ALTITUDE_AMSL);
  return self->altitude_mode;
}

void
gworld_scene_circle_node_set_radius(GWorldSceneCircleNode *self, double radius_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self));
  self->radius_m = positive_dimension(radius_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_circle_node_get_radius(GWorldSceneCircleNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self), 0.0);
  return self->radius_m;
}

void
gworld_scene_circle_node_set_segments(GWorldSceneCircleNode *self, guint segments)
{
  g_return_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self));
  self->segments = std::clamp(segments, 12u, 720u);
  emit_changed(GWORLD_SCENE_NODE(self));
}

guint
gworld_scene_circle_node_get_segments(GWorldSceneCircleNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self), 0);
  return self->segments;
}

void
gworld_scene_circle_node_set_fill_color(GWorldSceneCircleNode *self,
                                        double red,
                                        double green,
                                        double blue,
                                        double alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self));
  set_rgba(self->fill_rgba, red, green, blue, alpha);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_circle_node_get_fill_color(GWorldSceneCircleNode *self,
                                        double *red,
                                        double *green,
                                        double *blue,
                                        double *alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self));
  get_rgba(self->fill_rgba, red, green, blue, alpha);
}

void
gworld_scene_circle_node_set_outline_color(GWorldSceneCircleNode *self,
                                           double red,
                                           double green,
                                           double blue,
                                           double alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self));
  set_rgba(self->outline_rgba, red, green, blue, alpha);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_circle_node_get_outline_color(GWorldSceneCircleNode *self,
                                           double *red,
                                           double *green,
                                           double *blue,
                                           double *alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self));
  get_rgba(self->outline_rgba, red, green, blue, alpha);
}

void
gworld_scene_circle_node_set_outline_width(GWorldSceneCircleNode *self, double width_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self));
  self->outline_width_m = std::max(0.0, width_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_circle_node_get_outline_width(GWorldSceneCircleNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self), 0.0);
  return self->outline_width_m;
}

void
gworld_scene_circle_node_set_altitude_mode(GWorldSceneCircleNode *self,
                                           GWorldSceneAltitudeMode altitude_mode)
{
  g_return_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self));
  self->altitude_mode = valid_altitude_mode(altitude_mode);
  emit_changed(GWORLD_SCENE_NODE(self));
}

GWorldSceneAltitudeMode
gworld_scene_circle_node_get_altitude_mode(GWorldSceneCircleNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_CIRCLE_NODE(self), GWORLD_SCENE_ALTITUDE_AMSL);
  return self->altitude_mode;
}

void
gworld_scene_text_label_node_set_text(GWorldSceneTextLabelNode *self, const char *text)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  g_free(self->text);
  self->text = g_strdup(text ? text : "");
  emit_changed(GWORLD_SCENE_NODE(self));
}

const char *
gworld_scene_text_label_node_get_text(GWorldSceneTextLabelNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self), nullptr);
  return self->text != nullptr && self->text[0] != '\0' ? self->text : nullptr;
}

void
gworld_scene_text_label_node_set_font(GWorldSceneTextLabelNode *self, const char *font)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  g_free(self->font);
  self->font = g_strdup(font && font[0] != '\0' ? font : "Sans Bold 18");
  emit_changed(GWORLD_SCENE_NODE(self));
}

const char *
gworld_scene_text_label_node_get_font(GWorldSceneTextLabelNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self), nullptr);
  return self->font != nullptr ? self->font : "Sans Bold 18";
}

void
gworld_scene_text_label_node_set_text_color(GWorldSceneTextLabelNode *self,
                                            double red,
                                            double green,
                                            double blue,
                                            double alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  set_rgba(self->text_rgba, red, green, blue, alpha);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_text_label_node_get_text_color(GWorldSceneTextLabelNode *self,
                                            double *red,
                                            double *green,
                                            double *blue,
                                            double *alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  get_rgba(self->text_rgba, red, green, blue, alpha);
}

void
gworld_scene_text_label_node_set_background_color(GWorldSceneTextLabelNode *self,
                                                  double red,
                                                  double green,
                                                  double blue,
                                                  double alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  set_rgba(self->background_rgba, red, green, blue, alpha);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_text_label_node_get_background_color(GWorldSceneTextLabelNode *self,
                                                  double *red,
                                                  double *green,
                                                  double *blue,
                                                  double *alpha)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  get_rgba(self->background_rgba, red, green, blue, alpha);
}

void
gworld_scene_text_label_node_set_padding(GWorldSceneTextLabelNode *self, double padding_px)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  self->padding_px = std::clamp(padding_px, 0.0, 256.0);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_text_label_node_get_padding(GWorldSceneTextLabelNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self), 0.0);
  return self->padding_px;
}

void
gworld_scene_text_label_node_set_size_limits(GWorldSceneTextLabelNode *self,
                                             double min_px,
                                             double max_px)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  self->min_px = std::max(1.0, min_px);
  self->max_px = std::max(self->min_px, max_px);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_text_label_node_get_size_limits(GWorldSceneTextLabelNode *self,
                                             double *min_px,
                                             double *max_px)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  if (min_px)
    *min_px = self->min_px;
  if (max_px)
    *max_px = self->max_px;
}

void
gworld_scene_text_label_node_set_reference_size(GWorldSceneTextLabelNode *self,
                                                double size_px,
                                                double distance_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  self->reference_size_px = std::max(1.0, size_px);
  self->reference_distance_m = std::max(1.0, distance_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

void
gworld_scene_text_label_node_get_reference_size(GWorldSceneTextLabelNode *self,
                                                double *size_px,
                                                double *distance_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  if (size_px)
    *size_px = self->reference_size_px;
  if (distance_m)
    *distance_m = self->reference_distance_m;
}

void
gworld_scene_text_label_node_set_max_visible_distance(GWorldSceneTextLabelNode *self,
                                                      double distance_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  self->max_visible_distance_m = std::max(0.0, distance_m);
  emit_changed(GWORLD_SCENE_NODE(self));
}

double
gworld_scene_text_label_node_get_max_visible_distance(GWorldSceneTextLabelNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self), 0.0);
  return self->max_visible_distance_m;
}

void
gworld_scene_text_label_node_set_altitude_mode(GWorldSceneTextLabelNode *self,
                                               GWorldSceneAltitudeMode altitude_mode)
{
  g_return_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self));
  self->altitude_mode = valid_altitude_mode(altitude_mode);
  emit_changed(GWORLD_SCENE_NODE(self));
}

GWorldSceneAltitudeMode
gworld_scene_text_label_node_get_altitude_mode(GWorldSceneTextLabelNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_TEXT_LABEL_NODE(self), GWORLD_SCENE_ALTITUDE_AMSL);
  return self->altitude_mode;
}

void
gworld_scene_node_set_dimensions(GWorldSceneNode *self,
                                 double width_m,
                                 double depth_m,
                                 double height_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  if (GWORLD_IS_SCENE_CUBE_NODE(self)) {
    gworld_scene_cube_node_set_dimensions(GWORLD_SCENE_CUBE_NODE(self), width_m, depth_m, height_m);
  } else if (GWORLD_IS_SCENE_SPHERE_NODE(self)) {
    gworld_scene_sphere_node_set_diameter(GWORLD_SCENE_SPHERE_NODE(self), width_m);
  } else if (GWORLD_IS_SCENE_CYLINDER_NODE(self)) {
    gworld_scene_cylinder_node_set_size(GWORLD_SCENE_CYLINDER_NODE(self),
                                        (width_m + depth_m) * 0.5,
                                        height_m);
  }
}

void
gworld_scene_node_get_dimensions(GWorldSceneNode *self,
                                 double *width_m,
                                 double *depth_m,
                                 double *height_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  if (GWORLD_IS_SCENE_CUBE_NODE(self)) {
    gworld_scene_cube_node_get_dimensions(GWORLD_SCENE_CUBE_NODE(self), width_m, depth_m, height_m);
  } else if (GWORLD_IS_SCENE_SPHERE_NODE(self)) {
    const double diameter = gworld_scene_sphere_node_get_diameter(GWORLD_SCENE_SPHERE_NODE(self));
    if (width_m)
      *width_m = diameter;
    if (depth_m)
      *depth_m = diameter;
    if (height_m)
      *height_m = diameter;
  } else if (GWORLD_IS_SCENE_CYLINDER_NODE(self)) {
    double diameter = 0.0;
    double height = 0.0;
    gworld_scene_cylinder_node_get_size(GWORLD_SCENE_CYLINDER_NODE(self), &diameter, &height);
    if (width_m)
      *width_m = diameter;
    if (depth_m)
      *depth_m = diameter;
    if (height_m)
      *height_m = height;
  } else {
    if (width_m)
      *width_m = 1.0;
    if (depth_m)
      *depth_m = 1.0;
    if (height_m)
      *height_m = 1.0;
  }
}

void
gworld_scene_node_set_diameter(GWorldSceneNode *self, double diameter_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_NODE(self));
  if (GWORLD_IS_SCENE_SPHERE_NODE(self)) {
    gworld_scene_sphere_node_set_diameter(GWORLD_SCENE_SPHERE_NODE(self), diameter_m);
  } else if (GWORLD_IS_SCENE_CYLINDER_NODE(self)) {
    double old_diameter = 0.0;
    double height = 0.0;
    gworld_scene_cylinder_node_get_size(GWORLD_SCENE_CYLINDER_NODE(self), &old_diameter, &height);
    gworld_scene_cylinder_node_set_size(GWORLD_SCENE_CYLINDER_NODE(self), diameter_m, height);
  } else if (GWORLD_IS_SCENE_CUBE_NODE(self)) {
    double width = 0.0;
    double depth = 0.0;
    double height = 0.0;
    gworld_scene_cube_node_get_dimensions(GWORLD_SCENE_CUBE_NODE(self), &width, &depth, &height);
    gworld_scene_cube_node_set_dimensions(GWORLD_SCENE_CUBE_NODE(self), diameter_m, diameter_m, height);
  }
}

const char *
gworld_scene_node_get_model_path(GWorldSceneNode *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_NODE(self), nullptr);
  if (!GWORLD_IS_SCENE_MODEL_NODE(self))
    return nullptr;
  return gworld_scene_model_node_get_model_path(GWORLD_SCENE_MODEL_NODE(self));
}
