#ifndef GWORLD_SCENE_NODE_PRIVATE_H
#define GWORLD_SCENE_NODE_PRIVATE_H

#include "gworld-scene-node.h"

GWorldSceneCubeNode *_gworld_scene_cube_node_new(GWorldSceneNodeId id,
                                                 double latitude,
                                                 double longitude,
                                                 double altitude_amsl,
                                                 double width_m,
                                                 double depth_m,
                                                 double height_m);

GWorldSceneSphereNode *_gworld_scene_sphere_node_new(GWorldSceneNodeId id,
                                                     double latitude,
                                                     double longitude,
                                                     double altitude_amsl,
                                                     double diameter_m);

GWorldSceneCylinderNode *_gworld_scene_cylinder_node_new(GWorldSceneNodeId id,
                                                        double latitude,
                                                        double longitude,
                                                        double altitude_amsl,
                                                        double diameter_m,
                                                        double height_m);

GWorldSceneModelNode *_gworld_scene_model_node_new(GWorldSceneNodeId id,
                                                   const char *model_path,
                                                   double latitude,
                                                   double longitude,
                                                   double altitude_amsl);

GWorldSceneBillboardNode *_gworld_scene_billboard_node_new(GWorldSceneNodeId id,
                                                           const char *image_path,
                                                           double latitude,
                                                           double longitude,
                                                           double altitude);

GWorldSceneGroundOverlayNode *_gworld_scene_ground_overlay_node_new(GWorldSceneNodeId id,
                                                                    const char *image_path,
                                                                    double top_left_latitude,
                                                                    double top_left_longitude,
                                                                    double top_right_latitude,
                                                                    double top_right_longitude,
                                                                    double bottom_right_latitude,
                                                                    double bottom_right_longitude,
                                                                    double bottom_left_latitude,
                                                                    double bottom_left_longitude);

GWorldScenePolylineNode *_gworld_scene_polyline_node_new(GWorldSceneNodeId id);

GWorldScenePolygonNode *_gworld_scene_polygon_node_new(GWorldSceneNodeId id);

GWorldSceneCircleNode *_gworld_scene_circle_node_new(GWorldSceneNodeId id,
                                                     double latitude,
                                                     double longitude,
                                                     double altitude_amsl,
                                                     double radius_m);

GWorldSceneTextLabelNode *_gworld_scene_text_label_node_new(GWorldSceneNodeId id,
                                                            const char *text,
                                                            double latitude,
                                                            double longitude,
                                                            double altitude_amsl);

#endif /* GWORLD_SCENE_NODE_PRIVATE_H */
