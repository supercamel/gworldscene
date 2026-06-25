#ifndef GWORLD_SCENE_PICKING_PRIVATE_H
#define GWORLD_SCENE_PICKING_PRIVATE_H

#include "gworld-scene-camera-private.h"

#include <glm/glm.hpp>

namespace gworld_scene {

struct PickRay {
  glm::dvec3 origin;
  glm::dvec3 direction;
  glm::dmat4 mvp = glm::dmat4(1.0);
  int viewport_width = 1;
  int viewport_height = 1;
};

bool pick_ray_from_widget_point(int viewport_width,
                                int viewport_height,
                                double widget_x,
                                double widget_y,
                                const glm::dmat4 &projection,
                                const CameraPose &camera_pose,
                                PickRay &ray);

bool ray_intersects_triangle(const PickRay &ray,
                             const glm::dvec3 &p0,
                             const glm::dvec3 &p1,
                             const glm::dvec3 &p2,
                             double &distance);

bool ray_intersects_sphere(const PickRay &ray,
                           const glm::dvec3 &center,
                           double radius,
                           double &distance);

bool project_scene_point_to_widget(const PickRay &ray,
                                   const glm::dvec3 &point,
                                   double &x,
                                   double &y,
                                   double &depth);

} // namespace gworld_scene

#endif /* GWORLD_SCENE_PICKING_PRIVATE_H */
