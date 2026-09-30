#include "gworld-scene-terrain-source-private.h"
#include <glib.h>
#include <vector>
static void test_source() {
  g_autoptr(GWorldSceneTerrainSource) source=gworld_scene_terrain_source_new();
  int requests=0;
  g_signal_connect(source,"tile-needed",G_CALLBACK(+[](GWorldSceneTerrainSource*,int lat,int lon,gpointer n){
    g_assert_cmpint(lat,==,0);g_assert_cmpint(lon,==,0);++*static_cast<int*>(n);
  }),&requests);
  gworld_scene::terrain_source_request(source,0,0);gworld_scene::terrain_source_request(source,0,0);
  g_assert_cmpint(requests,==,1);
  const guint8 raw[]={0,100,0,200,1,44,1,144}; // North row 100,200; south 300,400.
  g_autoptr(GBytes) bytes=g_bytes_new(raw,sizeof raw);
  GError *error=nullptr;
  g_assert_true(gworld_scene_terrain_source_put_tile(source,0,0,2,bytes,&error));g_assert_no_error(error);
  double h=0;
  g_assert_true(gworld_scene_terrain_source_sample(source,.5,.5,&h));g_assert_cmpfloat(h,==,250);
  g_assert_true(gworld_scene_terrain_source_sample(source,1,0,&h));g_assert_cmpfloat(h,==,100);
  auto first=gworld_scene::terrain_source_snapshot(source),second=gworld_scene::terrain_source_snapshot(source);
  g_assert_true(first.at("0,0").get()==second.at("0,0").get()); // No duplicated decoded tile.
  g_assert_cmpuint(gworld_scene_terrain_source_get_memory_used(source),==,8);
  g_assert_false(gworld_scene_terrain_source_put_tile(source,0,0,3,bytes,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&error);
  g_assert_true(gworld_scene_terrain_source_sample(source,.5,.5,&h));g_assert_cmpfloat(h,==,250);
  gworld_scene_terrain_source_remove_tile(source,0,0);
  g_assert_false(gworld_scene_terrain_source_sample(source,.5,.5,&h));
  g_assert_cmpuint(gworld_scene_terrain_source_get_memory_used(source),==,8); // Retired worker-held data counted.
  first.clear();second.clear();g_assert_cmpuint(gworld_scene_terrain_source_get_memory_used(source),==,0);
  gworld_scene::terrain_source_request(source,0,0);g_assert_cmpint(requests,==,2);
  const guint8 void_raw[]={128,0,128,0,128,0,128,0};
  g_autoptr(GBytes) voids=g_bytes_new(void_raw,sizeof void_raw);
  g_assert_true(gworld_scene_terrain_source_put_tile(source,0,0,2,voids,&error));
  g_assert_false(gworld_scene_terrain_source_sample(source,.5,.5,&h)); // Unknown is not sea level.
  // Memory cap is enforced before allocation and keeps existing data intact.
  std::vector<guint8> large(3601*3601*2,0);
  g_autoptr(GBytes) big=g_bytes_new(large.data(),large.size());
  for(int i=1;i<=5;++i)g_assert_true(gworld_scene_terrain_source_put_tile(source,i,0,3601,big,&error));
  g_assert_false(gworld_scene_terrain_source_put_tile(source,6,0,3601,big,&error));
  g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE);g_clear_error(&error);
}
int main(int argc,char **argv) {g_test_init(&argc,&argv,nullptr);g_test_add_func("/terrain-source/sharing",test_source);return g_test_run();}
