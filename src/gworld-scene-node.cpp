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

G_DEFINE_TYPE_WITH_PRIVATE(GWorldSceneNode, gworld_scene_node, G_TYPE_OBJECT)
G_DEFINE_TYPE(GWorldSceneCubeNode, gworld_scene_cube_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneSphereNode, gworld_scene_sphere_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneCylinderNode, gworld_scene_cylinder_node, GWORLD_TYPE_SCENE_NODE)
G_DEFINE_TYPE(GWorldSceneModelNode, gworld_scene_model_node, GWORLD_TYPE_SCENE_NODE)

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
                      GWORLD_SCENE_PRIMITIVE_MODEL,
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
