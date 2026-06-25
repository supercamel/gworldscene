#include "gworld-scene-picking-private.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace gworld_scene {

bool
pick_ray_from_widget_point(int viewport_width,
                           int viewport_height,
                           double widget_x,
                           double widget_y,
                           const glm::dmat4 &projection,
                           const CameraPose &camera_pose,
                           PickRay &ray)
{
  const int width = std::max(1, viewport_width);
  const int height = std::max(1, viewport_height);
  const glm::dmat4 view = glm::lookAt(camera_pose.eye, camera_pose.center, camera_pose.up);
  const glm::dmat4 inverse_mvp = glm::inverse(projection * view);

  const double ndc_x = (widget_x / static_cast<double>(width)) * 2.0 - 1.0;
  const double ndc_y = 1.0 - (widget_y / static_cast<double>(height)) * 2.0;
  glm::dvec4 near_world = inverse_mvp * glm::dvec4(ndc_x, ndc_y, -1.0, 1.0);
  glm::dvec4 far_world = inverse_mvp * glm::dvec4(ndc_x, ndc_y, 1.0, 1.0);
  if (std::abs(near_world.w) < 1e-12 || std::abs(far_world.w) < 1e-12)
    return false;

  near_world /= near_world.w;
  far_world /= far_world.w;
  const glm::dvec3 direction = glm::dvec3(far_world) - glm::dvec3(near_world);
  if (glm::length(direction) <= 0.000001)
    return false;

  ray.origin = camera_pose.eye;
  ray.direction = glm::normalize(direction);
  ray.mvp = projection * view;
  ray.viewport_width = width;
  ray.viewport_height = height;
  return true;
}

bool
ray_intersects_triangle(const PickRay &ray,
                        const glm::dvec3 &p0,
                        const glm::dvec3 &p1,
                        const glm::dvec3 &p2,
                        double &distance)
{
  constexpr double epsilon = 1e-7;
  const glm::dvec3 edge1 = p1 - p0;
  const glm::dvec3 edge2 = p2 - p0;
  const glm::dvec3 h = glm::cross(ray.direction, edge2);
  const double a = glm::dot(edge1, h);
  if (std::abs(a) < epsilon)
    return false;

  const double f = 1.0 / a;
  const glm::dvec3 s = ray.origin - p0;
  const double u = f * glm::dot(s, h);
  if (u < 0.0 || u > 1.0)
    return false;

  const glm::dvec3 q = glm::cross(s, edge1);
  const double v = f * glm::dot(ray.direction, q);
  if (v < 0.0 || u + v > 1.0)
    return false;

  const double t = f * glm::dot(edge2, q);
  if (t <= epsilon)
    return false;

  distance = t;
  return true;
}

bool
ray_intersects_sphere(const PickRay &ray,
                      const glm::dvec3 &center,
                      double radius,
                      double &distance)
{
  radius = std::max(radius, 0.1);
  const glm::dvec3 oc = ray.origin - center;
  const double b = glm::dot(oc, ray.direction);
  const double c = glm::dot(oc, oc) - radius * radius;
  const double discriminant = b * b - c;
  if (discriminant < 0.0)
    return false;

  const double root = std::sqrt(discriminant);
  const double t0 = -b - root;
  const double t1 = -b + root;
  if (t0 > 0.0) {
    distance = t0;
    return true;
  }
  if (t1 > 0.0) {
    distance = t1;
    return true;
  }
  return false;
}

bool
project_scene_point_to_widget(const PickRay &ray,
                              const glm::dvec3 &point,
                              double &x,
                              double &y,
                              double &depth)
{
  const glm::dvec4 clip = ray.mvp * glm::dvec4(point, 1.0);
  if (std::abs(clip.w) < 1e-12)
    return false;

  const glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
  if (ndc.z < -1.0 || ndc.z > 1.0)
    return false;

  x = (ndc.x * 0.5 + 0.5) * static_cast<double>(ray.viewport_width);
  y = (1.0 - (ndc.y * 0.5 + 0.5)) * static_cast<double>(ray.viewport_height);
  depth = ndc.z;
  return true;
}

} // namespace gworld_scene
