#include "gworld-scene-atmosphere-private.h"
#include "gworld-scene-geo-private.h"
#include "gworld-scene-shadow-private.h"
#include <glib.h>
#include <cmath>

namespace {
void test_atmosphere_frame() {
  for (double lat : {-89.0, -45.0, 0.0, 45.0, 89.0})
    for (double lon : {-180.0, 0.0, 179.0}) {
      const auto frame = gworld_scene::atmosphere_frame(lat, lon);
      for (double distance : {0.0, 0.3, 10.0}) {
        const auto point = gworld_scene::geodetic_to_scene(lat, lon + distance, 0, lat, lon, 0);
        const auto atmosphere = frame.from_scene * (glm::vec3(point) - frame.planet_center);
        g_assert_cmpfloat(std::abs(glm::length(atmosphere) - 6371.0f), <, 0.003f);
      }
      const auto raised = frame.from_scene * (glm::vec3(0,1000,0) - frame.planet_center);
      g_assert_cmpfloat(std::abs(glm::length(raised) - 6372.0f), <, 0.01f);
    }
}
void test_shadow_coverage() {
  const gworld_scene::CameraPose camera{{0,100,0},{0,100,-1},{0,1,0}};
  for (glm::dvec3 light : {glm::dvec3(0,1,0), glm::dvec3(1,0.1,1), glm::dvec3(-1,0.5,0)}) {
    const auto shadow = gworld_scene::shadow_cascades(camera, 1.6, glm::radians(45.0), 2, 60000, light, 2048);
    double previous = 2;
    for (int i = 0; i < 3; ++i) {
      g_assert_cmpfloat(shadow.splits[i], >, previous);
      g_assert_cmpfloat(shadow.texel_meters[i], >, 0);
      for (double depth : {i ? previous * 0.85 : previous, double(shadow.splits[i])})
        for (double x : {-1.0,1.0})
          for (double y : {-1.0,1.0}) {
            const glm::vec3 point = glm::vec3(camera.eye) + glm::vec3(x * depth * 1.6 * std::tan(glm::radians(22.5)),
              y * depth * std::tan(glm::radians(22.5)), -depth);
            const auto clip = shadow.matrices[i] * glm::vec4(point,1);
            for (int c=0;c<3;++c) g_assert_cmpfloat(std::abs(clip[c] / clip.w), <=, 1.0001f);
          }
      previous = shadow.splits[i];
    }
    g_assert_cmpfloat(std::abs(shadow.splits.z - 60000), <, 0.1);
  }
}
void test_shadow_stability() {
  gworld_scene::CameraPose camera{{0,100,0},{0,100,-1},{0,1,0}};
  const auto first = gworld_scene::shadow_cascades(camera,1,glm::radians(45.0),2,5000,{0,1,0},2048);
  camera.eye.x += first.texel_meters.x * 0.1;
  camera.center.x += first.texel_meters.x * 0.1;
  const auto second = gworld_scene::shadow_cascades(camera,1,glm::radians(45.0),2,5000,{0,1,0},2048);
  for (int c=0;c<4;++c) for(int r=0;r<4;++r)
    g_assert_cmpfloat(std::abs(first.matrices[0][c][r] - second.matrices[0][c][r]), <, 1e-6);
}
}
int main(int argc,char **argv) {
  g_test_init(&argc,&argv,nullptr);
  g_test_add_func("/graphics/atmosphere-ellipsoid-frame",test_atmosphere_frame);
  g_test_add_func("/graphics/shadow-frustum-coverage",test_shadow_coverage);
  g_test_add_func("/graphics/shadow-subtexel-stability",test_shadow_stability);
  return g_test_run();
}
