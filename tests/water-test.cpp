#include "gworld-scene-water-private.h"
#include "water-fixture-private.h"
#include <gdal_priv.h>
#include <algorithm>
#include <zlib.h>

namespace {
void test_water_geometry() {
  for (int z : {0, 8, 14}) {
    std::vector<unsigned char> mask;
    std::string error;
    g_assert_true(gworld_scene::decode_water_mask(water_fixture::tile(), z, -1, 0, mask, error));
    g_assert_cmpuint(mask.size(), ==, 512 * 512);
    g_assert_cmpuint(mask[256 * 512 + 100], ==, 255); // water
    g_assert_cmpuint(mask[256 * 512 + 256], ==, 0);   // island
    g_assert_cmpuint(mask[20 * 512 + 20], ==, 0);     // outside
  }
}
void test_land_and_filtered_water() {
  for (auto bytes : {water_fixture::tile(false, true), water_fixture::tile(false, false, true),
                     water_fixture::tile(false, false, false, "landcover")}) {
    std::vector<unsigned char> mask;
    std::string error;
    g_assert_true(gworld_scene::decode_water_mask(bytes, 0, 0, 0, mask, error));
    g_assert_cmpuint(mask.size(), ==, 512 * 512);
    g_assert_true(std::all_of(mask.begin(), mask.end(), [](unsigned char v) { return v == 0; }));
  }
}
void test_bad_water() {
  for (auto bytes : {water_fixture::Bytes{}, water_fixture::Bytes{255,255,255,255}, water_fixture::Bytes(9 * 1024 * 1024)}) {
    std::vector<unsigned char> mask(10, 255);
    std::string error;
    g_assert_false(gworld_scene::decode_water_mask(bytes, 0, 0, 0, mask, error));
    g_assert_true(mask.empty()); g_assert_false(error.empty());
  }
}
void test_compressed_water() {
  for (bool oversized : {false, true}) {
    const auto bytes = oversized ? water_fixture::Bytes(9 * 1024 * 1024, 0) : water_fixture::tile();
    water_fixture::Bytes compressed(compressBound(bytes.size()) + 32);
    z_stream stream = {};
    g_assert_cmpint(deflateInit2(&stream, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY), ==, Z_OK);
    stream.next_in = const_cast<Bytef *>(bytes.data()); stream.avail_in = bytes.size();
    stream.next_out = compressed.data(); stream.avail_out = compressed.size();
    g_assert_cmpint(deflate(&stream, Z_FINISH), ==, Z_STREAM_END);
    compressed.resize(stream.total_out); deflateEnd(&stream);
    std::vector<unsigned char> mask;
    std::string error;
    g_assert_cmpint(gworld_scene::decode_water_mask(compressed,0,0,0,mask,error), ==, !oversized);
    if (!oversized) {
      g_assert_cmpuint(mask[256*512+100], ==, 255);
      g_assert_cmpuint(mask[256*512+256], ==, 0);
    } else g_assert_true(mask.empty());
  }
}
void test_cache_namespace() {
  using gworld_scene::water_cache_path;
  const auto a = water_cache_path("/cache", "https://a/{z}/{x}/{y}?key=secret", 3, -1, 2);
  g_assert_true(a == water_cache_path("/cache", "https://a/{z}/{x}/{y}?key=secret", 3, 7, 2));
  g_assert_true(a != water_cache_path("/cache", "https://b/{z}/{x}/{y}?key=secret", 3, 7, 2));
  g_assert_true(a.find("secret") == std::string::npos);
  g_assert_true(a.find("/water/") != std::string::npos);
}
}
int main(int argc, char **argv) {
  g_test_init(&argc, &argv, nullptr); GDALAllRegister();
  g_test_add_func("/water/geometry-and-islands", test_water_geometry);
  g_test_add_func("/water/land-and-filtered-water", test_land_and_filtered_water);
  g_test_add_func("/water/malformed", test_bad_water);
  g_test_add_func("/water/compressed-and-bounded", test_compressed_water);
  g_test_add_func("/water/cache-namespace", test_cache_namespace);
  return g_test_run();
}
