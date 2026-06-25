#include "gworld-scene-picking-private.h"

#include <glib.h>
#include <glm/gtc/matrix_transform.hpp>

namespace {

void
assert_near(double actual, double expected, double tolerance)
{
  g_assert_cmpfloat_with_epsilon(actual, expected, tolerance);
}

gworld_scene::PickRay
center_pick_ray()
{
  gworld_scene::CameraPose pose;
  pose.eye = glm::dvec3(0.0, 0.0, 10.0);
  pose.center = glm::dvec3(0.0, 0.0, 0.0);
  pose.up = glm::dvec3(0.0, 1.0, 0.0);

  const glm::dmat4 projection = glm::perspective(glm::radians(90.0), 1.0, 1.0, 100.0);
  gworld_scene::PickRay ray;
  g_assert_true(gworld_scene::pick_ray_from_widget_point(200, 200, 100.0, 100.0, projection, pose, ray));
  return ray;
}

void
test_pick_ray_from_widget_center()
{
  const gworld_scene::PickRay ray = center_pick_ray();

  assert_near(ray.origin.x, 0.0, 0.001);
  assert_near(ray.origin.y, 0.0, 0.001);
  assert_near(ray.origin.z, 10.0, 0.001);
  assert_near(ray.direction.x, 0.0, 0.001);
  assert_near(ray.direction.y, 0.0, 0.001);
  assert_near(ray.direction.z, -1.0, 0.001);
}

void
test_ray_intersects_triangle()
{
  const gworld_scene::PickRay ray = center_pick_ray();
  double distance = 0.0;

  g_assert_true(gworld_scene::ray_intersects_triangle(ray,
                                                     glm::dvec3(-1.0, -1.0, 0.0),
                                                     glm::dvec3(1.0, -1.0, 0.0),
                                                     glm::dvec3(0.0, 1.0, 0.0),
                                                     distance));
  assert_near(distance, 10.0, 0.001);

  g_assert_false(gworld_scene::ray_intersects_triangle(ray,
                                                      glm::dvec3(4.0, 4.0, 0.0),
                                                      glm::dvec3(5.0, 4.0, 0.0),
                                                      glm::dvec3(4.0, 5.0, 0.0),
                                                      distance));
}

void
test_ray_intersects_sphere()
{
  const gworld_scene::PickRay ray = center_pick_ray();
  double distance = 0.0;

  g_assert_true(gworld_scene::ray_intersects_sphere(ray, glm::dvec3(0.0), 2.0, distance));
  assert_near(distance, 8.0, 0.001);

  g_assert_false(gworld_scene::ray_intersects_sphere(ray, glm::dvec3(5.0, 0.0, 0.0), 1.0, distance));
}

void
test_project_scene_point_to_widget()
{
  const gworld_scene::PickRay ray = center_pick_ray();
  double x = 0.0;
  double y = 0.0;
  double depth = 0.0;

  g_assert_true(gworld_scene::project_scene_point_to_widget(ray, glm::dvec3(0.0), x, y, depth));
  assert_near(x, 100.0, 0.001);
  assert_near(y, 100.0, 0.001);
  g_assert_cmpfloat(depth, >, -1.0);
  g_assert_cmpfloat(depth, <, 1.0);
}

} // namespace

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, nullptr);
  g_test_add_func("/picking/pick-ray-from-widget-center", test_pick_ray_from_widget_center);
  g_test_add_func("/picking/ray-intersects-triangle", test_ray_intersects_triangle);
  g_test_add_func("/picking/ray-intersects-sphere", test_ray_intersects_sphere);
  g_test_add_func("/picking/project-scene-point-to-widget", test_project_scene_point_to_widget);
  return g_test_run();
}
