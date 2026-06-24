#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GWorldScene"
#endif

#include "gworld-scene-view.h"
#include "gworld-scene-camera-private.h"
#include "gworld-scene-geo-private.h"
#include "gworld-scene-light-private.h"
#include "gworld-scene-lod-private.h"
#include "gworld-scene-node-private.h"
#include "gworld-scene-view-private.h"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/texture.h>
#include <epoxy/gl.h>
#include <gdal_priv.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <libsoup/soup.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMercatorMaxLatitude = 85.05112878;
constexpr int kAtlasTilePixels = 256;
constexpr int kMaxAtlasTiles = 64;
constexpr int kMaxAtlasPixels = 4096;
constexpr guint kSceneUpdateDelayMs = 180;
constexpr int kMaxTerrainTileLoadsPerUpdate = 10;
constexpr int kMaxTerrainDownloadsPerUpdate = 8;
constexpr int kMaxTextureDownloadsPerUpdate = 36;
constexpr gint64 kTerrainRetryBaseDelayUs = 5 * G_USEC_PER_SEC;
constexpr gint64 kTerrainRetryMaxDelayUs = 120 * G_USEC_PER_SEC;
constexpr double kMinCameraPitchDeg = -89.0;
constexpr double kMaxCameraPitchDeg = 89.0;
constexpr double kGlobeRenderAltitudeM = 45000.0;
constexpr double kTerrainDisableAltitudeM = 180000.0;
constexpr int kShadowMapSize = 2048;
constexpr double kShadowMaxAltitudeM = kTerrainDisableAltitudeM;
constexpr int kGlobeLatSegments = 64;
constexpr int kGlobeLonSegments = 128;
constexpr int kVertexStride = 16;
constexpr int kSphereSegments = 24;
constexpr int kSphereRings = 12;
constexpr int kCylinderSegments = 32;
constexpr int kModelTextureTilePixels = 512;
constexpr int kModelTextureAtlasColumns = 4;
constexpr int kModelTextureAtlasRows = 4;
constexpr int kModelTextureAtlasPixels = kModelTextureTilePixels * kModelTextureAtlasColumns;
constexpr float kMaterialTerrain = 0.0f;
constexpr float kMaterialSolidNode = 1.0f;
constexpr float kMaterialGlobe = 2.0f;
constexpr float kMaterialTexturedModel = 3.0f;

constexpr const char *kDefaultTerrainServer = "https://flightops.silvertone.com.au/terrain/data/";
constexpr const char *kDefaultMapTileTemplate = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";

using gworld_scene::deg_to_rad;
using gworld_scene::kEarthMetersPerDegree;
using gworld_scene::ned_to_scene_vector;

struct TileCoord {
  int z = 0;
  int x = 0;
  int y = 0;
};

struct LatLonBounds {
  double min_lat = 0.0;
  double max_lat = 0.0;
  double min_lon = 0.0;
  double max_lon = 0.0;
};

struct AtlasRange {
  int z = 0;
  int x_min = 0;
  int x_max = -1;
  int y_min = 0;
  int y_max = -1;

  bool valid() const
  {
    return z >= 0 && x_min <= x_max && y_min <= y_max;
  }

  int width_tiles() const
  {
    return valid() ? x_max - x_min + 1 : 0;
  }

  int height_tiles() const
  {
    return valid() ? y_max - y_min + 1 : 0;
  }

  std::string key() const
  {
    return std::to_string(z) + ":" + std::to_string(x_min) + ":" + std::to_string(x_max) +
           ":" + std::to_string(y_min) + ":" + std::to_string(y_max);
  }
};

struct TerrainTile {
  int lat = 0;
  int lon = 0;
  int dimension = 0;
  std::vector<int16_t> heights;
};

struct TerrainCandidate {
  int lat = 0;
  int lon = 0;
  double distance = 0.0;
};

struct ModelVertex {
  glm::dvec3 position;
  glm::dvec3 normal;
  glm::vec3 color;
  glm::vec2 texcoord = glm::vec2(-1.0f);
  bool has_texture = false;
};

struct ModelMesh {
  bool loaded = false;
  bool failed = false;
  std::string error;
  std::vector<ModelVertex> vertices;
  std::vector<unsigned int> indices;
};

struct TexCoords {
  float detail_u = 0.0f;
  float detail_v = 0.0f;
  float mid_u = 0.0f;
  float mid_v = 0.0f;
  float base_u = 0.0f;
  float base_v = 0.0f;
};

enum class TextureLayer {
  Detail,
  Mid,
  Base,
  Globe,
};

} // namespace

struct SceneNode {
  GWorldSceneNodeId id = 0;
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_amsl = 0.0;
  double yaw_deg = 0.0;
  double pitch_deg = 0.0;
  double roll_deg = 0.0;
  double width_m = 10.0;
  double depth_m = 10.0;
  double height_m = 10.0;
  double scale_x = 1.0;
  double scale_y = 1.0;
  double scale_z = 1.0;
  glm::vec3 color = glm::vec3(0.96f, 0.54f, 0.20f);
  std::string model_path;
};

struct GWorldSceneViewState {
  std::mutex mutex;

  double latitude = -35.0024;
  double longitude = 147.4648;
  double altitude_amsl = 5200.0;
  double heading_deg = 35.0;
  double pitch_deg = -26.0;
  double mesh_origin_latitude = -35.0024;
  double mesh_origin_longitude = 147.4648;
  double sun_azimuth_deg = 228.0;
  double sun_elevation_deg = 50.0;
  double sun_time_of_day = 14.0;
  double sun_declination_deg = 0.0;
  bool sun_uses_time_of_day = false;
  bool fog_enabled = true;
  double fog_start_m = 12000.0;
  double fog_end_m = 120000.0;
  glm::vec3 fog_color = glm::vec3(0.60f, 0.72f, 0.86f);
  bool shadows_enabled = true;
  double terrain_normal_smoothing = 0.88;

  std::string terrain_server = kDefaultTerrainServer;
  std::string map_tile_template = kDefaultMapTileTemplate;
  std::string cache_directory;
  bool cache_enabled = true;

  std::unordered_map<std::string, TerrainTile> terrain_tiles;
  std::unordered_set<std::string> wanted_terrain_keys;
  std::unordered_set<std::string> pending;
  std::unordered_set<std::string> unavailable_terrain_keys;
  std::unordered_map<std::string, unsigned int> terrain_failure_counts;
  std::unordered_map<std::string, gint64> terrain_retry_after_us;

  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  bool mesh_dirty = true;
  std::string mesh_key;
  bool mesh_includes_terrain = true;
  guint64 scene_revision = 0;
  GWorldSceneNodeId next_node_id = 1;
  std::unordered_map<GWorldSceneNodeId, GWorldSceneNode *> scene_nodes;
  std::unordered_map<std::string, ModelMesh> model_meshes;
  std::unordered_map<std::string, int> model_texture_slots;
  AtlasRange mesh_atlas_range;
  AtlasRange mesh_mid_atlas_range;
  AtlasRange mesh_base_atlas_range;

  std::vector<unsigned char> texture_pixels;
  int texture_width = 0;
  int texture_height = 0;
  bool texture_dirty = false;
  std::string loaded_texture_key;
  std::string wanted_texture_key;
  std::string pending_texture_build_key;
  AtlasRange texture_atlas_range;

  std::vector<unsigned char> mid_texture_pixels;
  int mid_texture_width = 0;
  int mid_texture_height = 0;
  bool mid_texture_dirty = false;
  std::string loaded_mid_texture_key;
  std::string wanted_mid_texture_key;
  std::string pending_mid_texture_build_key;
  AtlasRange mid_texture_atlas_range;

  std::vector<unsigned char> base_texture_pixels;
  int base_texture_width = 0;
  int base_texture_height = 0;
  bool base_texture_dirty = false;
  std::string loaded_base_texture_key;
  std::string wanted_base_texture_key;
  std::string pending_base_texture_build_key;
  AtlasRange base_texture_atlas_range;

  std::vector<unsigned char> globe_texture_pixels;
  int globe_texture_width = 0;
  int globe_texture_height = 0;
  bool globe_texture_dirty = false;
  std::string loaded_globe_texture_key;
  std::string wanted_globe_texture_key;
  std::string pending_globe_texture_build_key;
  AtlasRange globe_texture_atlas_range;
  guint64 texture_source_revision = 0;

  std::vector<unsigned char> model_texture_pixels;
  int model_texture_width = 0;
  int model_texture_height = 0;
  bool model_texture_dirty = false;

  GLuint program = 0;
  GLuint shadow_program = 0;
  GLuint shadow_fbo = 0;
  GLuint shadow_depth_texture = 0;
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
  GLuint texture = 0;
  GLuint mid_texture = 0;
  GLuint base_texture = 0;
  GLuint globe_texture = 0;
  GLuint model_texture = 0;
  std::size_t index_count = 0;

  std::vector<float> globe_vertices;
  std::vector<unsigned int> globe_indices;
  bool globe_mesh_dirty = true;
  std::string globe_mesh_key;
  GLuint globe_vao = 0;
  GLuint globe_vbo = 0;
  GLuint globe_ebo = 0;
  std::size_t globe_index_count = 0;

  double rotate_drag_last_x = 0.0;
  double rotate_drag_last_y = 0.0;

  bool move_forward = false;
  bool move_backward = false;
  bool move_left = false;
  bool move_right = false;
  bool move_fast = false;
  gint64 last_tick_time_us = 0;
  guint scene_update_source = 0;
};

namespace {

struct DownloadJob {
  std::string uri;
  std::string path;
  std::string pending_key;
  std::string kind;
  int terrain_lat = 0;
  int terrain_lon = 0;
};

struct TextureAtlasBuildJob {
  AtlasRange range;
  TextureLayer layer = TextureLayer::Detail;
  std::string cache_dir;
  std::string texture_template;
  std::string pending_key;
  guint64 source_revision = 0;
};

struct TextureAtlasBuildResult {
  AtlasRange range;
  TextureLayer layer = TextureLayer::Detail;
  std::string pending_key;
  guint64 source_revision = 0;
  std::vector<unsigned char> pixels;
  int width = 0;
  int height = 0;
  int loaded_count = 0;
  int missing_count = 0;
  int decode_failed_count = 0;
};

const char *
texture_layer_name(TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return "detail";
  case TextureLayer::Mid:
    return "mid";
  case TextureLayer::Base:
    return "base";
  case TextureLayer::Globe:
    return "globe";
  }
  return "detail";
}

std::vector<unsigned char> &
texture_layer_pixels(GWorldSceneViewState *state, TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return state->texture_pixels;
  case TextureLayer::Mid:
    return state->mid_texture_pixels;
  case TextureLayer::Base:
    return state->base_texture_pixels;
  case TextureLayer::Globe:
    return state->globe_texture_pixels;
  }
  return state->texture_pixels;
}

int &
texture_layer_width(GWorldSceneViewState *state, TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return state->texture_width;
  case TextureLayer::Mid:
    return state->mid_texture_width;
  case TextureLayer::Base:
    return state->base_texture_width;
  case TextureLayer::Globe:
    return state->globe_texture_width;
  }
  return state->texture_width;
}

int &
texture_layer_height(GWorldSceneViewState *state, TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return state->texture_height;
  case TextureLayer::Mid:
    return state->mid_texture_height;
  case TextureLayer::Base:
    return state->base_texture_height;
  case TextureLayer::Globe:
    return state->globe_texture_height;
  }
  return state->texture_height;
}

bool &
texture_layer_dirty(GWorldSceneViewState *state, TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return state->texture_dirty;
  case TextureLayer::Mid:
    return state->mid_texture_dirty;
  case TextureLayer::Base:
    return state->base_texture_dirty;
  case TextureLayer::Globe:
    return state->globe_texture_dirty;
  }
  return state->texture_dirty;
}

std::string &
texture_layer_loaded_key(GWorldSceneViewState *state, TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return state->loaded_texture_key;
  case TextureLayer::Mid:
    return state->loaded_mid_texture_key;
  case TextureLayer::Base:
    return state->loaded_base_texture_key;
  case TextureLayer::Globe:
    return state->loaded_globe_texture_key;
  }
  return state->loaded_texture_key;
}

std::string &
texture_layer_wanted_key(GWorldSceneViewState *state, TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return state->wanted_texture_key;
  case TextureLayer::Mid:
    return state->wanted_mid_texture_key;
  case TextureLayer::Base:
    return state->wanted_base_texture_key;
  case TextureLayer::Globe:
    return state->wanted_globe_texture_key;
  }
  return state->wanted_texture_key;
}

std::string &
texture_layer_pending_key(GWorldSceneViewState *state, TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return state->pending_texture_build_key;
  case TextureLayer::Mid:
    return state->pending_mid_texture_build_key;
  case TextureLayer::Base:
    return state->pending_base_texture_build_key;
  case TextureLayer::Globe:
    return state->pending_globe_texture_build_key;
  }
  return state->pending_texture_build_key;
}

AtlasRange &
texture_layer_active_range(GWorldSceneViewState *state, TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Detail:
    return state->texture_atlas_range;
  case TextureLayer::Mid:
    return state->mid_texture_atlas_range;
  case TextureLayer::Base:
    return state->base_texture_atlas_range;
  case TextureLayer::Globe:
    return state->globe_texture_atlas_range;
  }
  return state->texture_atlas_range;
}

std::string
replace_all(std::string value, const std::string &needle, const std::string &replacement)
{
  std::size_t pos = 0;
  while ((pos = value.find(needle, pos)) != std::string::npos) {
    value.replace(pos, needle.size(), replacement);
    pos += replacement.size();
  }
  return value;
}

bool
contains(const std::string &value, const std::string &needle)
{
  return value.find(needle) != std::string::npos;
}

bool
ends_with(const std::string &value, const std::string &suffix)
{
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string
hgt_tile_name(int lat_floor, int lon_floor)
{
  char buffer[32];
  std::snprintf(buffer,
                sizeof buffer,
                "%c%02d%c%03d",
                lat_floor >= 0 ? 'N' : 'S',
                std::abs(lat_floor),
                lon_floor >= 0 ? 'E' : 'W',
                std::abs(lon_floor));
  return buffer;
}

std::string
terrain_key(int lat_floor, int lon_floor)
{
  return std::to_string(lat_floor) + "," + std::to_string(lon_floor);
}

int
texture_zoom_for_altitude(double altitude_amsl)
{
  if (altitude_amsl < 1800.0)
    return 16;
  if (altitude_amsl < 6500.0)
    return 15;
  if (altitude_amsl < 14000.0)
    return 14;
  if (altitude_amsl < 32000.0)
    return 13;
  if (altitude_amsl < 85000.0)
    return 12;
  return 11;
}

int
detail_texture_zoom_for_altitude(double altitude_amsl)
{
  return std::clamp(texture_zoom_for_altitude(altitude_amsl) + 1, 0, 18);
}

int
base_texture_zoom_for_altitude(double altitude_amsl)
{
  return std::clamp(texture_zoom_for_altitude(altitude_amsl) - 6, 4, 10);
}

int
mid_texture_zoom_for_altitude(double altitude_amsl)
{
  return std::clamp(texture_zoom_for_altitude(altitude_amsl) - 3, 6, 13);
}

int
terrain_step_for_altitude(double altitude_amsl)
{
  if (altitude_amsl < 1200.0)
    return 4;
  if (altitude_amsl < 3500.0)
    return 6;
  if (altitude_amsl < 10000.0)
    return 10;
  if (altitude_amsl < 30000.0)
    return 18;
  return 32;
}

double
terrain_build_radius_for_altitude(double altitude_amsl)
{
  const double horizon_m = std::sqrt(std::max(0.0, 2.0 * gworld_scene::kWgs84A * altitude_amsl));
  return std::clamp(std::max(altitude_amsl * 70.0, horizon_m * 1.35), 90000.0, 650000.0);
}

double
texture_radius_for_altitude(double altitude_amsl)
{
  return std::clamp(altitude_amsl * 2.1, 5250.0, 27000.0);
}

double
terrain_near_radius_for_altitude(double altitude_amsl, double terrain_radius)
{
  return std::min(terrain_radius, std::clamp(altitude_amsl * 6.0, 10000.0, 45000.0));
}

double
terrain_mid_radius_for_altitude(double altitude_amsl, double terrain_radius)
{
  const double near_radius = terrain_near_radius_for_altitude(altitude_amsl, terrain_radius);
  return std::min(terrain_radius, std::max(near_radius, near_radius * 3.0));
}

double
mid_texture_radius_for_altitude(double altitude_amsl, double terrain_radius)
{
  return std::min(terrain_radius, terrain_mid_radius_for_altitude(altitude_amsl, terrain_radius) * 1.15);
}

double
terrain_far_radius_for_altitude(double altitude_amsl, double terrain_radius)
{
  const double mid_radius = terrain_mid_radius_for_altitude(altitude_amsl, terrain_radius);
  return std::min(terrain_radius, std::max(mid_radius, mid_radius * 2.0));
}

int
terrain_lod_step(int base_step, int lod)
{
  if (lod <= 0)
    return std::max(1, base_step);
  if (lod == 1)
    return std::max(base_step * 2, 16);
  if (lod == 2)
    return std::max(base_step * 6, 64);
  return std::max(base_step * 14, 128);
}

std::string
default_cache_directory()
{
  return std::string(g_get_user_cache_dir()) + G_DIR_SEPARATOR_S + "gworldscene";
}

std::string
join_path(const std::string &a, const std::string &b)
{
  if (a.empty())
    return b;
  if (a.back() == G_DIR_SEPARATOR)
    return a + b;
  return a + G_DIR_SEPARATOR_S + b;
}

void
ensure_parent_directory(const std::string &path)
{
  g_autofree char *parent = g_path_get_dirname(path.c_str());
  g_mkdir_with_parents(parent, 0755);
}

std::string
texture_uri_from_template(const std::string &templ, TileCoord tile)
{
  std::string uri = templ;
  uri = replace_all(uri, "{z}", std::to_string(tile.z));
  uri = replace_all(uri, "{x}", std::to_string(tile.x));
  uri = replace_all(uri, "{y}", std::to_string(tile.y));
  return uri;
}

std::string
terrain_uri_from_template(const std::string &templ, const std::string &tile_name)
{
  const bool has_placeholder = contains(templ, "{tile}") || contains(templ, "{name}");
  std::string uri = replace_all(templ, "{tile}", tile_name);
  uri = replace_all(uri, "{name}", tile_name);

  if (!has_placeholder) {
    if (!uri.empty() && uri.back() != '/')
      uri += "/";
    uri += tile_name + ".hgt.zip";
  }

  return uri;
}

std::string
terrain_cache_path_for_uri(const std::string &cache_dir,
                           const std::string &uri,
                           const std::string &tile_name)
{
  const std::string extension = contains(uri, ".zip") ? ".hgt.zip" : ".hgt";
  return join_path(join_path(cache_dir, "terrain"), tile_name + extension);
}

std::string
texture_extension_for_uri(const std::string &uri)
{
  const std::size_t query_pos = uri.find('?');
  const std::string path = query_pos == std::string::npos ? uri : uri.substr(0, query_pos);
  if (ends_with(path, ".jpg") || ends_with(path, ".jpeg"))
    return "jpg";
  if (ends_with(path, ".webp"))
    return "webp";
  return "png";
}

std::string
texture_cache_provider_key(const std::string &templ)
{
  return std::to_string(g_str_hash(templ.c_str()));
}

std::string
texture_cache_path(const std::string &cache_dir,
                   const std::string &uri,
                   const std::string &templ,
                   TileCoord tile)
{
  return join_path(join_path(join_path(join_path(cache_dir, "textures"),
                                      texture_cache_provider_key(templ)),
                             std::to_string(tile.z)),
                   join_path(std::to_string(tile.x),
                             std::to_string(tile.y) + "." + texture_extension_for_uri(uri)));
}

double
lon_to_tile_x(double lon_deg, int zoom)
{
  const double n = static_cast<double>(1 << zoom);
  return (lon_deg + 180.0) / 360.0 * n;
}

double
lat_to_tile_y(double lat_deg, int zoom)
{
  const double clamped_lat = std::clamp(lat_deg, -kMercatorMaxLatitude, kMercatorMaxLatitude);
  const double lat_rad = deg_to_rad(clamped_lat);
  const double n = static_cast<double>(1 << zoom);
  return (1.0 - std::asinh(std::tan(lat_rad)) / kPi) * 0.5 * n;
}

AtlasRange
lat_lon_bounds_to_tile_range(const LatLonBounds &bounds, int zoom)
{
  const int n = 1 << zoom;
  const double x0 = lon_to_tile_x(bounds.min_lon, zoom);
  const double x1 = lon_to_tile_x(bounds.max_lon, zoom);
  const double y0 = lat_to_tile_y(bounds.max_lat, zoom);
  const double y1 = lat_to_tile_y(bounds.min_lat, zoom);

  AtlasRange range;
  range.z = zoom;
  range.x_min = std::clamp(static_cast<int>(std::floor(std::min(x0, x1))), 0, n - 1);
  range.x_max = std::clamp(static_cast<int>(std::floor(std::max(x0, x1))), 0, n - 1);
  range.y_min = std::clamp(static_cast<int>(std::floor(std::min(y0, y1))), 0, n - 1);
  range.y_max = std::clamp(static_cast<int>(std::floor(std::max(y0, y1))), 0, n - 1);
  return range;
}

AtlasRange
select_atlas_range(const LatLonBounds &bounds, int desired_zoom)
{
  AtlasRange fallback;
  for (int zoom = std::clamp(desired_zoom, 0, 18); zoom >= 0; --zoom) {
    AtlasRange range = lat_lon_bounds_to_tile_range(bounds, zoom);
    fallback = range;
    const int width = range.width_tiles();
    const int height = range.height_tiles();
    if (width * height <= kMaxAtlasTiles &&
        width * kAtlasTilePixels <= kMaxAtlasPixels &&
        height * kAtlasTilePixels <= kMaxAtlasPixels) {
      return range;
    }
  }
  return fallback;
}

AtlasRange
globe_atlas_range(double latitude, double longitude, double altitude_amsl)
{
  const gworld_scene::TileRange lod_range =
    gworld_scene::globe_texture_range_for_camera(latitude, longitude, altitude_amsl);
  AtlasRange range;
  range.z = lod_range.z;
  range.x_min = lod_range.x_min;
  range.x_max = lod_range.x_max;
  range.y_min = lod_range.y_min;
  range.y_max = lod_range.y_max;
  return range;
}

LatLonBounds
bounds_around_camera(double latitude, double longitude, double radius_m)
{
  const double lat_delta = radius_m / kEarthMetersPerDegree;
  const double lon_scale = std::max(0.05, std::cos(deg_to_rad(latitude)));
  const double lon_delta = radius_m / (kEarthMetersPerDegree * lon_scale);

  LatLonBounds bounds;
  bounds.min_lat = std::clamp(latitude - lat_delta, -90.0, 90.0);
  bounds.max_lat = std::clamp(latitude + lat_delta, -90.0, 90.0);
  bounds.min_lon = std::clamp(longitude - lon_delta, -180.0, 180.0);
  bounds.max_lon = std::clamp(longitude + lon_delta, -180.0, 180.0);
  return bounds;
}

glm::dvec3
geodetic_to_enu(double lat_deg,
                double lon_deg,
                double h,
                double ref_lat_deg,
                double ref_lon_deg,
                double ref_h)
{
  return gworld_scene::geodetic_to_scene(lat_deg, lon_deg, h, ref_lat_deg, ref_lon_deg, ref_h);
}

glm::dvec3
safe_normalize(const glm::dvec3 &value, const glm::dvec3 &fallback)
{
  const double length = glm::length(value);
  if (length <= 0.000001)
    return fallback;
  return value / length;
}

glm::dvec3
sun_enu_for_state(const GWorldSceneViewState *state, double latitude)
{
  if (state->sun_uses_time_of_day)
    return gworld_scene::sun_direction_from_time(latitude,
                                                state->sun_time_of_day,
                                                state->sun_declination_deg);
  return gworld_scene::sun_direction_from_position(state->sun_azimuth_deg,
                                                  state->sun_elevation_deg);
}

glm::vec3
sun_scene_direction_from_enu(const glm::dvec3 &enu)
{
  const glm::dvec3 scene = safe_normalize(glm::dvec3(enu.x, enu.z, -enu.y),
                                         glm::dvec3(-0.45, 0.76, 0.38));
  return glm::vec3(static_cast<float>(scene.x),
                   static_cast<float>(scene.y),
                   static_cast<float>(scene.z));
}

double
smoothstep_value(double edge0, double edge1, double value)
{
  const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

void
effective_fog_range(double configured_start_m,
                    double configured_end_m,
                    double altitude_m,
                    bool render_globe,
                    double &start_m,
                    double &end_m)
{
  const double configured_start = std::max(0.0, configured_start_m);
  const double configured_end = std::max(configured_start + 1.0, configured_end_m);

  if (render_globe) {
    start_m = std::max(configured_start, altitude_m * 0.65);
    end_m = std::max(configured_end, altitude_m * 4.8);
  } else {
    const double local_start = std::clamp(altitude_m * 2.0, 2500.0, 25000.0);
    const double local_end = std::clamp(altitude_m * 16.0, 35000.0, 180000.0);
    start_m = std::min(configured_start, local_start);
    end_m = std::min(configured_end, local_end);
  }

  end_m = std::max(end_m, start_m + 1.0);
}

uint16_t
read_le16(const unsigned char *data)
{
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t
read_le32(const unsigned char *data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

bool
inflate_deflated_zip_member(const unsigned char *data,
                            std::size_t size,
                            std::size_t uncompressed_size,
                            std::vector<unsigned char> &out)
{
  out.assign(uncompressed_size, 0);

  z_stream stream = {};
  stream.next_in = const_cast<Bytef *>(reinterpret_cast<const Bytef *>(data));
  stream.avail_in = static_cast<uInt>(size);
  stream.next_out = reinterpret_cast<Bytef *>(out.data());
  stream.avail_out = static_cast<uInt>(out.size());

  if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
    return false;

  const int result = inflate(&stream, Z_FINISH);
  inflateEnd(&stream);
  return result == Z_STREAM_END && stream.total_out == uncompressed_size;
}

bool
extract_hgt_from_zip(const std::vector<unsigned char> &zip_bytes,
                     std::vector<unsigned char> &hgt_bytes)
{
  std::size_t pos = 0;
  while (pos + 30 <= zip_bytes.size()) {
    if (read_le32(zip_bytes.data() + pos) != 0x04034b50)
      return false;

    const uint16_t flags = read_le16(zip_bytes.data() + pos + 6);
    const uint16_t method = read_le16(zip_bytes.data() + pos + 8);
    const uint32_t compressed_size = read_le32(zip_bytes.data() + pos + 18);
    const uint32_t uncompressed_size = read_le32(zip_bytes.data() + pos + 22);
    const uint16_t name_len = read_le16(zip_bytes.data() + pos + 26);
    const uint16_t extra_len = read_le16(zip_bytes.data() + pos + 28);

    const std::size_t name_offset = pos + 30;
    const std::size_t data_offset = name_offset + name_len + extra_len;
    if (data_offset > zip_bytes.size())
      return false;

    if ((flags & 0x08) != 0 || compressed_size == 0 || uncompressed_size == 0)
      return false;

    const std::string member_name(reinterpret_cast<const char *>(zip_bytes.data() + name_offset),
                                  name_len);
    if (data_offset + compressed_size > zip_bytes.size())
      return false;

    if (ends_with(member_name, ".hgt") || ends_with(member_name, ".HGT")) {
      if (method == 0) {
        hgt_bytes.assign(zip_bytes.begin() + static_cast<std::ptrdiff_t>(data_offset),
                         zip_bytes.begin() + static_cast<std::ptrdiff_t>(data_offset + compressed_size));
        return hgt_bytes.size() == uncompressed_size;
      }
      if (method == 8) {
        return inflate_deflated_zip_member(zip_bytes.data() + data_offset,
                                           compressed_size,
                                           uncompressed_size,
                                           hgt_bytes);
      }
      return false;
    }

    pos = data_offset + compressed_size;
  }

  return false;
}

bool
read_file_bytes(const std::string &path, std::vector<unsigned char> &bytes)
{
  gchar *contents = nullptr;
  gsize length = 0;
  g_autoptr(GError) error = nullptr;
  if (!g_file_get_contents(path.c_str(), &contents, &length, &error))
    return false;
  g_autofree gchar *owned_contents = contents;

  const auto *data = reinterpret_cast<const unsigned char *>(contents);
  bytes.assign(data, data + length);
  return true;
}

bool
parse_hgt_bytes(const std::vector<unsigned char> &bytes,
                std::vector<int16_t> &heights,
                int &dimension)
{
  if (bytes.size() < 2 || (bytes.size() % 2) != 0)
    return false;

  const std::size_t sample_count = bytes.size() / 2;
  const auto root = static_cast<int>(std::llround(std::sqrt(static_cast<double>(sample_count))));
  if (root <= 1 || static_cast<std::size_t>(root * root) != sample_count)
    return false;

  dimension = root;
  heights.resize(sample_count);
  for (std::size_t i = 0; i < sample_count; ++i) {
    const uint16_t value = (static_cast<uint16_t>(bytes[i * 2]) << 8) | bytes[i * 2 + 1];
    heights[i] = static_cast<int16_t>(value);
  }

  return true;
}

bool
read_hgt_file(const std::string &path, std::vector<int16_t> &heights, int &dimension)
{
  std::vector<unsigned char> bytes;
  if (!read_file_bytes(path, bytes))
    return false;

  if (bytes.size() >= 4 && read_le32(bytes.data()) == 0x04034b50) {
    std::vector<unsigned char> unzipped;
    if (!extract_hgt_from_zip(bytes, unzipped))
      return false;
    return parse_hgt_bytes(unzipped, heights, dimension);
  }

  return parse_hgt_bytes(bytes, heights, dimension);
}

int16_t
get_height_clamped(const TerrainTile &tile, int x, int y)
{
  x = std::clamp(x, 0, tile.dimension - 1);
  y = std::clamp(y, 0, tile.dimension - 1);
  return tile.heights[static_cast<std::size_t>(y * tile.dimension + x)];
}

int16_t
get_safe_height(const TerrainTile &tile, int x, int y)
{
  const int16_t h = get_height_clamped(tile, x, y);
  if (h != -32768)
    return h;

  for (int radius = 1; radius < 8; ++radius) {
    for (int oy = -radius; oy <= radius; ++oy) {
      for (int ox = -radius; ox <= radius; ++ox) {
        const int16_t candidate = get_height_clamped(tile, x + ox, y + oy);
        if (candidate != -32768)
          return candidate;
      }
    }
  }

  return 0;
}

void
sample_to_lat_lon(const TerrainTile &tile, int sample_x, int sample_y, double &lat, double &lon)
{
  const double u = static_cast<double>(sample_x) / static_cast<double>(tile.dimension - 1);
  const double v = static_cast<double>(sample_y) / static_cast<double>(tile.dimension - 1);
  lat = static_cast<double>(tile.lat + 1) * (1.0 - v) + static_cast<double>(tile.lat) * v;
  lon = static_cast<double>(tile.lon) * (1.0 - u) + static_cast<double>(tile.lon + 1) * u;
}

void
uv_for_lat_lon(double lat, double lon, const AtlasRange &range, float &u, float &v)
{
  if (!range.valid()) {
    u = 0.0f;
    v = 0.0f;
    return;
  }

  const double denom_u = static_cast<double>((range.x_max + 1) - range.x_min);
  const double denom_v = static_cast<double>((range.y_max + 1) - range.y_min);
  u = static_cast<float>((lon_to_tile_x(lon, range.z) - static_cast<double>(range.x_min)) / denom_u);
  v = static_cast<float>((lat_to_tile_y(lat, range.z) - static_cast<double>(range.y_min)) / denom_v);
}

void
append_vertex(std::vector<float> &vertices,
              const glm::dvec3 &position,
              const TexCoords &uv,
              const glm::vec3 &normal,
              const glm::vec3 &color = glm::vec3(1.0f),
              float material = kMaterialTerrain)
{
  vertices.push_back(static_cast<float>(position.x));
  vertices.push_back(static_cast<float>(position.y));
  vertices.push_back(static_cast<float>(position.z));
  vertices.push_back(uv.detail_u);
  vertices.push_back(uv.detail_v);
  vertices.push_back(uv.mid_u);
  vertices.push_back(uv.mid_v);
  vertices.push_back(uv.base_u);
  vertices.push_back(uv.base_v);
  vertices.push_back(normal.x);
  vertices.push_back(normal.y);
  vertices.push_back(normal.z);
  vertices.push_back(color.r);
  vertices.push_back(color.g);
  vertices.push_back(color.b);
  vertices.push_back(material);
}

glm::vec3
clamp_color(const glm::vec3 &color)
{
  return glm::vec3(std::clamp(color.r, 0.0f, 1.0f),
                   std::clamp(color.g, 0.0f, 1.0f),
                   std::clamp(color.b, 0.0f, 1.0f));
}

bool
copy_pixbuf_rgba_scaled(GdkPixbuf *pixbuf,
                        int target_width,
                        int target_height,
                        std::vector<unsigned char> &pixels,
                        std::string *error_message = nullptr)
{
  if (pixbuf == nullptr || target_width <= 0 || target_height <= 0) {
    if (error_message)
      *error_message = "invalid image";
    return false;
  }

  GdkPixbuf *working = nullptr;
  if (gdk_pixbuf_get_width(pixbuf) != target_width ||
      gdk_pixbuf_get_height(pixbuf) != target_height) {
    working = gdk_pixbuf_scale_simple(pixbuf,
                                      target_width,
                                      target_height,
                                      GDK_INTERP_BILINEAR);
  } else {
    working = GDK_PIXBUF(g_object_ref(pixbuf));
  }

  if (working == nullptr) {
    if (error_message)
      *error_message = "failed to scale image";
    return false;
  }

  const int width = gdk_pixbuf_get_width(working);
  const int height = gdk_pixbuf_get_height(working);
  const int rowstride = gdk_pixbuf_get_rowstride(working);
  const int channels = gdk_pixbuf_get_n_channels(working);
  const bool has_alpha = gdk_pixbuf_get_has_alpha(working);
  const auto *source = gdk_pixbuf_get_pixels(working);
  if (width <= 0 || height <= 0 || source == nullptr || channels < 3) {
    if (error_message) {
      *error_message = "invalid image buffer: " + std::to_string(width) + "x" +
                       std::to_string(height) + ", channels=" +
                       std::to_string(channels) + ", rowstride=" +
                       std::to_string(rowstride);
    }
    g_object_unref(working);
    return false;
  }

  pixels.assign(static_cast<std::size_t>(target_width * target_height * 4), 0);
  for (int y = 0; y < target_height; ++y) {
    for (int x = 0; x < target_width; ++x) {
      const std::size_t src = static_cast<std::size_t>(y * rowstride + x * channels);
      const std::size_t dst = static_cast<std::size_t>((y * target_width + x) * 4);
      pixels[dst + 0] = source[src + 0];
      pixels[dst + 1] = source[src + 1];
      pixels[dst + 2] = source[src + 2];
      pixels[dst + 3] = has_alpha && channels >= 4 ? source[src + 3] : 255;
    }
  }

  g_object_unref(working);
  return true;
}

bool
load_image_rgba_scaled(const std::string &path,
                       int target_width,
                       int target_height,
                       std::vector<unsigned char> &pixels,
                       std::string *error_message = nullptr)
{
  g_autoptr(GError) error = nullptr;
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path.c_str(), &error);
  if (pixbuf == nullptr) {
    if (error_message)
      *error_message = error ? error->message : "unknown image load error";
    return false;
  }

  const bool result = copy_pixbuf_rgba_scaled(pixbuf,
                                             target_width,
                                             target_height,
                                             pixels,
                                             error_message);
  g_object_unref(pixbuf);
  return result;
}

bool
load_embedded_texture_rgba_scaled(const aiTexture *texture,
                                  int target_width,
                                  int target_height,
                                  std::vector<unsigned char> &pixels,
                                  std::string *error_message = nullptr)
{
  if (texture == nullptr || target_width <= 0 || target_height <= 0) {
    if (error_message)
      *error_message = "invalid embedded texture";
    return false;
  }

  if (texture->mHeight == 0) {
    if (texture->mWidth == 0 || texture->pcData == nullptr) {
      if (error_message)
        *error_message = "empty embedded texture";
      return false;
    }

    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    GError *error = nullptr;
    if (!gdk_pixbuf_loader_write(loader,
                                 reinterpret_cast<const guchar *>(texture->pcData),
                                 texture->mWidth,
                                 &error)) {
      if (error_message)
        *error_message = error ? error->message : "embedded texture decode failed";
      g_clear_error(&error);
      g_object_unref(loader);
      return false;
    }

    if (!gdk_pixbuf_loader_close(loader, &error)) {
      if (error_message)
        *error_message = error ? error->message : "embedded texture decode failed";
      g_clear_error(&error);
      g_object_unref(loader);
      return false;
    }

    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
    const bool result = copy_pixbuf_rgba_scaled(pixbuf,
                                               target_width,
                                               target_height,
                                               pixels,
                                               error_message);
    g_object_unref(loader);
    return result;
  }

  if (texture->mWidth == 0 || texture->pcData == nullptr) {
    if (error_message)
      *error_message = "empty embedded texture";
    return false;
  }

  pixels.assign(static_cast<std::size_t>(target_width * target_height * 4), 0);
  for (int y = 0; y < target_height; ++y) {
    const int sy = std::clamp(static_cast<int>((static_cast<double>(y) / target_height) *
                                               static_cast<double>(texture->mHeight)),
                              0,
                              static_cast<int>(texture->mHeight) - 1);
    for (int x = 0; x < target_width; ++x) {
      const int sx = std::clamp(static_cast<int>((static_cast<double>(x) / target_width) *
                                                 static_cast<double>(texture->mWidth)),
                                0,
                                static_cast<int>(texture->mWidth) - 1);
      const aiTexel &texel = texture->pcData[sy * texture->mWidth + sx];
      const std::size_t dst = static_cast<std::size_t>((y * target_width + x) * 4);
      pixels[dst + 0] = texel.r;
      pixels[dst + 1] = texel.g;
      pixels[dst + 2] = texel.b;
      pixels[dst + 3] = texel.a;
    }
  }

  return true;
}

std::string
model_directory_for_path(const std::string &path)
{
  g_autofree char *directory = g_path_get_dirname(path.c_str());
  return directory ? directory : ".";
}

std::string
model_texture_path_from_reference(const std::string &model_path,
                                  const std::string &texture_reference)
{
  if (texture_reference.empty())
    return std::string();

  if (g_str_has_prefix(texture_reference.c_str(), "file://")) {
    g_autofree char *filename = g_filename_from_uri(texture_reference.c_str(), nullptr, nullptr);
    return filename ? filename : std::string();
  }

  g_autofree char *unescaped = g_uri_unescape_string(texture_reference.c_str(), nullptr);
  std::string path = unescaped ? unescaped : texture_reference;
  std::replace(path.begin(), path.end(), '\\', G_DIR_SEPARATOR);

  if (g_path_is_absolute(path.c_str()))
    return path;

  return join_path(model_directory_for_path(model_path), path);
}

bool
load_model_texture_pixels(const aiScene *scene,
                          const std::string &model_path,
                          const std::string &texture_reference,
                          std::vector<unsigned char> &pixels,
                          std::string *error_message = nullptr)
{
  if (scene != nullptr) {
    const aiTexture *embedded = scene->GetEmbeddedTexture(texture_reference.c_str());
    if (embedded != nullptr) {
      return load_embedded_texture_rgba_scaled(embedded,
                                              kModelTextureTilePixels,
                                              kModelTextureTilePixels,
                                              pixels,
                                              error_message);
    }
  }

  const std::string texture_path =
    model_texture_path_from_reference(model_path, texture_reference);
  if (texture_path.empty()) {
    if (error_message)
      *error_message = "texture reference is not a local file";
    return false;
  }

  return load_image_rgba_scaled(texture_path,
                                kModelTextureTilePixels,
                                kModelTextureTilePixels,
                                pixels,
                                error_message);
}

bool
material_texture_reference(const aiMaterial *material, aiString &texture_reference)
{
  if (material == nullptr)
    return false;

  if (aiGetMaterialTexture(material,
                           aiTextureType_BASE_COLOR,
                           0,
                           &texture_reference) == AI_SUCCESS &&
      texture_reference.C_Str()[0] != '\0') {
    return true;
  }

  return aiGetMaterialTexture(material,
                              aiTextureType_DIFFUSE,
                              0,
                              &texture_reference) == AI_SUCCESS &&
         texture_reference.C_Str()[0] != '\0';
}

glm::vec2
model_atlas_uv_for_slot(int slot, const aiVector3D &source_uv)
{
  const int column = slot % kModelTextureAtlasColumns;
  const int row = slot / kModelTextureAtlasColumns;
  const float u = std::clamp(source_uv.x, 0.0f, 1.0f);
  const float v = std::clamp(source_uv.y, 0.0f, 1.0f);
  const float atlas_u = (static_cast<float>(column) + u) /
                        static_cast<float>(kModelTextureAtlasColumns);
  const float atlas_v = (static_cast<float>(row) + v) /
                        static_cast<float>(kModelTextureAtlasRows);
  const float inset = 0.5f / static_cast<float>(kModelTextureAtlasPixels);
  return glm::vec2(std::clamp(atlas_u,
                              static_cast<float>(column) /
                                static_cast<float>(kModelTextureAtlasColumns) + inset,
                              static_cast<float>(column + 1) /
                                static_cast<float>(kModelTextureAtlasColumns) - inset),
                   std::clamp(atlas_v,
                              static_cast<float>(row) /
                                static_cast<float>(kModelTextureAtlasRows) + inset,
                              static_cast<float>(row + 1) /
                                static_cast<float>(kModelTextureAtlasRows) - inset));
}

void
copy_model_texture_tile_to_atlas(GWorldSceneViewState *state,
                                 int slot,
                                 const std::vector<unsigned char> &tile_pixels)
{
  if (state->model_texture_pixels.empty()) {
    state->model_texture_width = kModelTextureAtlasPixels;
    state->model_texture_height = kModelTextureAtlasPixels;
    state->model_texture_pixels.assign(static_cast<std::size_t>(state->model_texture_width) *
                                         static_cast<std::size_t>(state->model_texture_height) * 4,
                                       0);
  }

  const int column = slot % kModelTextureAtlasColumns;
  const int row = slot / kModelTextureAtlasColumns;
  const int dst_x0 = column * kModelTextureTilePixels;
  const int dst_y0 = row * kModelTextureTilePixels;

  for (int y = 0; y < kModelTextureTilePixels; ++y) {
    const std::size_t src_offset = static_cast<std::size_t>(y * kModelTextureTilePixels * 4);
    const std::size_t dst_offset =
      static_cast<std::size_t>(((dst_y0 + y) * state->model_texture_width + dst_x0) * 4);
    std::memcpy(state->model_texture_pixels.data() + dst_offset,
                tile_pixels.data() + src_offset,
                static_cast<std::size_t>(kModelTextureTilePixels * 4));
  }

  state->model_texture_dirty = true;
}

int
model_texture_slot_for_material(GWorldSceneViewState *state,
                                const aiScene *scene,
                                const aiMaterial *material,
                                const std::string &model_path)
{
  aiString texture_reference;
  if (!material_texture_reference(material, texture_reference))
    return -1;

  const std::string reference = texture_reference.C_Str();
  const std::string key = model_path + "|" + reference;
  auto iter = state->model_texture_slots.find(key);
  if (iter != state->model_texture_slots.end())
    return iter->second;

  const int max_slots = kModelTextureAtlasColumns * kModelTextureAtlasRows;
  const int slot = static_cast<int>(state->model_texture_slots.size());
  if (slot >= max_slots) {
    g_warning("Model texture atlas is full: model=%s texture=%s capacity=%d",
              model_path.c_str(),
              reference.c_str(),
              max_slots);
    return -1;
  }

  std::vector<unsigned char> tile_pixels;
  std::string error_message;
  if (!load_model_texture_pixels(scene,
                                 model_path,
                                 reference,
                                 tile_pixels,
                                 &error_message)) {
    g_warning("Failed to load model texture: model=%s texture=%s error=%s",
              model_path.c_str(),
              reference.c_str(),
              error_message.empty() ? "unknown error" : error_message.c_str());
    return -1;
  }

  copy_model_texture_tile_to_atlas(state, slot, tile_pixels);
  state->model_texture_slots.emplace(key, slot);
  g_debug("Loaded model texture: model=%s texture=%s slot=%d",
          model_path.c_str(),
          reference.c_str(),
          slot);
  return slot;
}

glm::vec3
material_color_for_mesh(const aiScene *scene, const aiMesh *mesh)
{
  if (scene != nullptr &&
      mesh != nullptr &&
      mesh->mMaterialIndex < scene->mNumMaterials &&
      scene->mMaterials[mesh->mMaterialIndex] != nullptr) {
    aiColor4D base_color;
    if (aiGetMaterialColor(scene->mMaterials[mesh->mMaterialIndex],
                           AI_MATKEY_BASE_COLOR,
                           &base_color) == AI_SUCCESS) {
      return clamp_color(glm::vec3(base_color.r, base_color.g, base_color.b));
    }

    aiColor4D diffuse;
    if (aiGetMaterialColor(scene->mMaterials[mesh->mMaterialIndex],
                           AI_MATKEY_COLOR_DIFFUSE,
                           &diffuse) == AI_SUCCESS) {
      return clamp_color(glm::vec3(diffuse.r, diffuse.g, diffuse.b));
    }
  }
  return glm::vec3(1.0f);
}

glm::dvec3
assimp_vec_to_glm(const aiVector3D &value)
{
  return glm::dvec3(value.x, value.y, value.z);
}

bool
load_model_mesh_from_file(GWorldSceneViewState *state, const std::string &path, ModelMesh &mesh)
{
  Assimp::Importer importer;
  const unsigned int flags = aiProcess_Triangulate |
                             aiProcess_JoinIdenticalVertices |
                             aiProcess_GenSmoothNormals |
                             aiProcess_ImproveCacheLocality |
                             aiProcess_PreTransformVertices |
                             aiProcess_OptimizeMeshes |
                             aiProcess_FindInvalidData;
  const aiScene *scene = importer.ReadFile(path, flags);
  if (scene == nullptr || scene->mNumMeshes == 0) {
    mesh.failed = true;
    mesh.error = importer.GetErrorString();
    if (mesh.error.empty())
      mesh.error = "No meshes found";
    return false;
  }

  mesh.vertices.clear();
  mesh.indices.clear();

  for (unsigned int mesh_index = 0; mesh_index < scene->mNumMeshes; ++mesh_index) {
    const aiMesh *source = scene->mMeshes[mesh_index];
    if (source == nullptr || source->mNumVertices == 0)
      continue;

    const glm::vec3 material_color = material_color_for_mesh(scene, source);
    const aiMaterial *material = (source->mMaterialIndex < scene->mNumMaterials)
                                   ? scene->mMaterials[source->mMaterialIndex]
                                   : nullptr;
    const bool has_normals = source->HasNormals();
    const bool has_vertex_colors = source->HasVertexColors(0);
    const bool has_texcoords = source->HasTextureCoords(0);
    const int texture_slot = has_texcoords
                               ? model_texture_slot_for_material(state, scene, material, path)
                               : -1;

    for (unsigned int face_index = 0; face_index < source->mNumFaces; ++face_index) {
      const aiFace &face = source->mFaces[face_index];
      if (face.mNumIndices != 3)
        continue;

      glm::dvec3 face_normal(0.0, 1.0, 0.0);
      if (!has_normals) {
        const glm::dvec3 p0 = assimp_vec_to_glm(source->mVertices[face.mIndices[0]]);
        const glm::dvec3 p1 = assimp_vec_to_glm(source->mVertices[face.mIndices[1]]);
        const glm::dvec3 p2 = assimp_vec_to_glm(source->mVertices[face.mIndices[2]]);
        face_normal = glm::cross(p1 - p0, p2 - p0);
        face_normal = safe_normalize(face_normal, glm::dvec3(0.0, 1.0, 0.0));
      }

      for (unsigned int corner = 0; corner < 3; ++corner) {
        const unsigned int source_index = face.mIndices[corner];
        if (source_index >= source->mNumVertices)
          continue;

        ModelVertex vertex;
        vertex.position = assimp_vec_to_glm(source->mVertices[source_index]);
        vertex.normal = has_normals
                          ? safe_normalize(assimp_vec_to_glm(source->mNormals[source_index]),
                                           face_normal)
                          : face_normal;
        vertex.color = material_color;
        if (has_vertex_colors) {
          const aiColor4D &color = source->mColors[0][source_index];
          vertex.color = clamp_color(glm::vec3(color.r, color.g, color.b));
        }
        if (texture_slot >= 0 && has_texcoords) {
          vertex.texcoord = model_atlas_uv_for_slot(texture_slot,
                                                    source->mTextureCoords[0][source_index]);
          vertex.has_texture = true;
        }

        mesh.indices.push_back(static_cast<unsigned int>(mesh.vertices.size()));
        mesh.vertices.push_back(vertex);
      }
    }
  }

  mesh.loaded = !mesh.vertices.empty() && !mesh.indices.empty();
  mesh.failed = !mesh.loaded;
  if (mesh.failed)
    mesh.error = "No triangle geometry found";
  return mesh.loaded;
}

const ModelMesh *
model_mesh_for_path(GWorldSceneViewState *state, const std::string &path)
{
  if (path.empty())
    return nullptr;

  auto iter = state->model_meshes.find(path);
  if (iter == state->model_meshes.end()) {
    ModelMesh mesh;
    load_model_mesh_from_file(state, path, mesh);
    iter = state->model_meshes.emplace(path, std::move(mesh)).first;
    if (iter->second.failed) {
      g_warning("Failed to load model '%s': %s",
                path.c_str(),
                iter->second.error.empty() ? "unknown error" : iter->second.error.c_str());
    }
  }

  return iter->second.loaded ? &iter->second : nullptr;
}

void
append_triangle(std::vector<float> &vertices,
                std::vector<unsigned int> &indices,
                const glm::dvec3 &p0,
                const glm::dvec3 &p1,
                const glm::dvec3 &p2,
                const TexCoords &uv0,
                const TexCoords &uv1,
                const TexCoords &uv2)
{
  glm::dvec3 n64 = glm::cross(p1 - p0, p2 - p0);
  if (glm::length(n64) <= 0.000001)
    n64 = glm::dvec3(0.0, 1.0, 0.0);
  else
    n64 = glm::normalize(n64);
  if (n64.y < 0.0)
    n64 = -n64;

  const glm::vec3 normal(static_cast<float>(n64.x),
                         static_cast<float>(n64.y),
                         static_cast<float>(n64.z));
  const unsigned int base = static_cast<unsigned int>(vertices.size() / kVertexStride);
  append_vertex(vertices, p0, uv0, normal);
  append_vertex(vertices, p1, uv1, normal);
  append_vertex(vertices, p2, uv2, normal);
  indices.push_back(base);
  indices.push_back(base + 1);
  indices.push_back(base + 2);
}

glm::dmat3
node_ned_rotation(const SceneNode &node)
{
  glm::dmat4 transform(1.0);
  transform = glm::rotate(transform, deg_to_rad(node.yaw_deg), glm::dvec3(0.0, 0.0, 1.0));
  transform = glm::rotate(transform, deg_to_rad(node.pitch_deg), glm::dvec3(0.0, 1.0, 0.0));
  transform = glm::rotate(transform, deg_to_rad(node.roll_deg), glm::dvec3(1.0, 0.0, 0.0));
  return glm::dmat3(transform);
}

glm::dvec3
node_vertex_position(const SceneNode &node,
                     const glm::dmat3 &rotation,
                     const glm::dvec3 &center,
                     double north,
                     double east,
                     double down)
{
  const glm::dvec3 scaled(north * node.scale_x, east * node.scale_y, down * node.scale_z);
  return center + ned_to_scene_vector(rotation * scaled);
}

void
append_solid_triangle(std::vector<float> &vertices,
                      std::vector<unsigned int> &indices,
                      const glm::dvec3 &p0,
                      const glm::dvec3 &p1,
                      const glm::dvec3 &p2,
                      const glm::vec3 &color)
{
  glm::dvec3 n64 = glm::cross(p1 - p0, p2 - p0);
  if (glm::length(n64) <= 0.000001)
    n64 = glm::dvec3(0.0, 1.0, 0.0);
  else
    n64 = glm::normalize(n64);

  const glm::vec3 normal(static_cast<float>(n64.x),
                         static_cast<float>(n64.y),
                         static_cast<float>(n64.z));
  const TexCoords uv{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
  const unsigned int base = static_cast<unsigned int>(vertices.size() / kVertexStride);
  append_vertex(vertices, p0, uv, normal, color, kMaterialSolidNode);
  append_vertex(vertices, p1, uv, normal, color, kMaterialSolidNode);
  append_vertex(vertices, p2, uv, normal, color, kMaterialSolidNode);
  indices.push_back(base);
  indices.push_back(base + 1);
  indices.push_back(base + 2);
}

void
append_solid_quad(std::vector<float> &vertices,
                  std::vector<unsigned int> &indices,
                  const glm::dvec3 &p0,
                  const glm::dvec3 &p1,
                  const glm::dvec3 &p2,
                  const glm::dvec3 &p3,
                  const glm::vec3 &color)
{
  append_solid_triangle(vertices, indices, p0, p1, p2, color);
  append_solid_triangle(vertices, indices, p0, p2, p3, color);
}

void
append_cube_node(GWorldSceneViewState *state, const SceneNode &node)
{
  const glm::dvec3 center = geodetic_to_enu(node.latitude,
                                            node.longitude,
                                            node.altitude_amsl,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0);
  const glm::dmat3 rotation = node_ned_rotation(node);
  const double n = std::max(0.001, node.depth_m) * 0.5;
  const double e = std::max(0.001, node.width_m) * 0.5;
  const double d = std::max(0.001, node.height_m) * 0.5;

  glm::dvec3 p[8];
  int i = 0;
  for (double north : {-n, n}) {
    for (double east : {-e, e}) {
      for (double down : {-d, d})
        p[i++] = node_vertex_position(node, rotation, center, north, east, down);
    }
  }

  append_solid_quad(state->vertices, state->indices, p[0], p[1], p[3], p[2], node.color);
  append_solid_quad(state->vertices, state->indices, p[4], p[6], p[7], p[5], node.color);
  append_solid_quad(state->vertices, state->indices, p[0], p[4], p[5], p[1], node.color);
  append_solid_quad(state->vertices, state->indices, p[2], p[3], p[7], p[6], node.color);
  append_solid_quad(state->vertices, state->indices, p[0], p[2], p[6], p[4], node.color);
  append_solid_quad(state->vertices, state->indices, p[1], p[5], p[7], p[3], node.color);
}

void
append_sphere_node(GWorldSceneViewState *state, const SceneNode &node)
{
  const glm::dvec3 center = geodetic_to_enu(node.latitude,
                                            node.longitude,
                                            node.altitude_amsl,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0);
  const glm::dmat3 rotation = node_ned_rotation(node);
  const double radius = std::max(0.001, node.width_m) * 0.5;

  for (int ring = 0; ring < kSphereRings; ++ring) {
    const double phi0 = -kPi * 0.5 + kPi * static_cast<double>(ring) / kSphereRings;
    const double phi1 = -kPi * 0.5 + kPi * static_cast<double>(ring + 1) / kSphereRings;
    for (int seg = 0; seg < kSphereSegments; ++seg) {
      const double theta0 = 2.0 * kPi * static_cast<double>(seg) / kSphereSegments;
      const double theta1 = 2.0 * kPi * static_cast<double>(seg + 1) / kSphereSegments;
      auto point = [&](double phi, double theta) {
        const double c = std::cos(phi);
        return node_vertex_position(node,
                                    rotation,
                                    center,
                                    radius * c * std::cos(theta),
                                    radius * c * std::sin(theta),
                                    radius * std::sin(phi));
      };
      append_solid_quad(state->vertices,
                        state->indices,
                        point(phi0, theta0),
                        point(phi0, theta1),
                        point(phi1, theta1),
                        point(phi1, theta0),
                        node.color);
    }
  }
}

void
append_cylinder_node(GWorldSceneViewState *state, const SceneNode &node)
{
  const glm::dvec3 center = geodetic_to_enu(node.latitude,
                                            node.longitude,
                                            node.altitude_amsl,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0);
  const glm::dmat3 rotation = node_ned_rotation(node);
  const double radius = std::max(0.001, node.width_m) * 0.5;
  const double half_height = std::max(0.001, node.height_m) * 0.5;
  const glm::dvec3 top = node_vertex_position(node, rotation, center, 0.0, 0.0, -half_height);
  const glm::dvec3 bottom = node_vertex_position(node, rotation, center, 0.0, 0.0, half_height);

  for (int seg = 0; seg < kCylinderSegments; ++seg) {
    const double theta0 = 2.0 * kPi * static_cast<double>(seg) / kCylinderSegments;
    const double theta1 = 2.0 * kPi * static_cast<double>(seg + 1) / kCylinderSegments;
    auto rim = [&](double theta, double down) {
      return node_vertex_position(node,
                                  rotation,
                                  center,
                                  radius * std::cos(theta),
                                  radius * std::sin(theta),
                                  down);
    };
    const glm::dvec3 t0 = rim(theta0, -half_height);
    const glm::dvec3 t1 = rim(theta1, -half_height);
    const glm::dvec3 b0 = rim(theta0, half_height);
    const glm::dvec3 b1 = rim(theta1, half_height);
    append_solid_quad(state->vertices, state->indices, t0, b0, b1, t1, node.color);
    append_solid_triangle(state->vertices, state->indices, top, t1, t0, node.color);
    append_solid_triangle(state->vertices, state->indices, bottom, b0, b1, node.color);
  }
}

void
append_model_node(GWorldSceneViewState *state, const SceneNode &node)
{
  const ModelMesh *mesh = model_mesh_for_path(state, node.model_path);
  if (mesh == nullptr)
    return;

  const glm::dvec3 center = geodetic_to_enu(node.latitude,
                                            node.longitude,
                                            node.altitude_amsl,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0);
  const glm::dmat3 rotation = node_ned_rotation(node);

  for (unsigned int model_index : mesh->indices) {
    if (model_index >= mesh->vertices.size())
      continue;

    const ModelVertex &model_vertex = mesh->vertices[model_index];

    const glm::dvec3 &p = model_vertex.position;
    const glm::dvec3 position =
      node_vertex_position(node, rotation, center, p.z, p.x, -p.y);

    const glm::dvec3 &n = model_vertex.normal;
    const glm::dvec3 normal_ned =
      safe_normalize(glm::dvec3(n.z, n.x, -n.y), glm::dvec3(0.0, 0.0, -1.0));
    const glm::dvec3 normal_scene =
      safe_normalize(ned_to_scene_vector(rotation * normal_ned), glm::dvec3(0.0, 1.0, 0.0));
    const glm::vec3 color = clamp_color(model_vertex.color * node.color);
    TexCoords uv{-1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f};
    float material = kMaterialSolidNode;
    if (model_vertex.has_texture) {
      uv.detail_u = model_vertex.texcoord.x;
      uv.detail_v = model_vertex.texcoord.y;
      material = kMaterialTexturedModel;
    }

    const unsigned int base = static_cast<unsigned int>(state->vertices.size() / kVertexStride);
    append_vertex(state->vertices,
                  position,
                  uv,
                  glm::vec3(static_cast<float>(normal_scene.x),
                            static_cast<float>(normal_scene.y),
                            static_cast<float>(normal_scene.z)),
                  color,
                  material);
    state->indices.push_back(base);
  }
}

void
append_scene_nodes(GWorldSceneViewState *state)
{
  for (const auto &entry : state->scene_nodes) {
    GWorldSceneNode *scene_node = entry.second;
    if (!GWORLD_IS_SCENE_NODE(scene_node))
      continue;

    SceneNode node;
    node.id = gworld_scene_node_get_id(scene_node);
    gworld_scene_node_get_position(scene_node,
                                   &node.latitude,
                                   &node.longitude,
                                   &node.altitude_amsl);
    gworld_scene_node_get_orientation_ned(scene_node,
                                          &node.yaw_deg,
                                          &node.pitch_deg,
                                          &node.roll_deg);
    gworld_scene_node_get_scale(scene_node,
                                &node.scale_x,
                                &node.scale_y,
                                &node.scale_z);
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    gworld_scene_node_get_color(scene_node, &red, &green, &blue);
    node.color = glm::vec3(static_cast<float>(red),
                           static_cast<float>(green),
                           static_cast<float>(blue));

    if (GWORLD_IS_SCENE_CUBE_NODE(scene_node)) {
      gworld_scene_cube_node_get_dimensions(GWORLD_SCENE_CUBE_NODE(scene_node),
                                            &node.width_m,
                                            &node.depth_m,
                                            &node.height_m);
      append_cube_node(state, node);
    } else if (GWORLD_IS_SCENE_SPHERE_NODE(scene_node)) {
      const double diameter = gworld_scene_sphere_node_get_diameter(GWORLD_SCENE_SPHERE_NODE(scene_node));
      node.width_m = diameter;
      node.depth_m = diameter;
      node.height_m = diameter;
      append_sphere_node(state, node);
    } else if (GWORLD_IS_SCENE_CYLINDER_NODE(scene_node)) {
      double diameter = 0.0;
      gworld_scene_cylinder_node_get_size(GWORLD_SCENE_CYLINDER_NODE(scene_node),
                                          &diameter,
                                          &node.height_m);
      node.width_m = diameter;
      node.depth_m = diameter;
      append_cylinder_node(state, node);
    } else if (GWORLD_IS_SCENE_MODEL_NODE(scene_node)) {
      const char *model_path = gworld_scene_model_node_get_model_path(GWORLD_SCENE_MODEL_NODE(scene_node));
      if (model_path != nullptr)
        node.model_path = model_path;
      append_model_node(state, node);
    }
  }
}

void
append_globe_vertex(std::vector<float> &vertices,
                    const glm::dvec3 &position,
                    double latitude,
                    double longitude,
                    const AtlasRange &range,
                    const glm::vec3 &normal)
{
  TexCoords uv{-1.0f, -1.0f, -1.0f, -1.0f, 0.0f, 0.0f};
  uv_for_lat_lon(latitude, longitude, range, uv.base_u, uv.base_v);
  append_vertex(vertices, position, uv, normal, glm::vec3(0.08f, 0.22f, 0.46f), kMaterialGlobe);
}

void
rebuild_globe_mesh(GWorldSceneViewState *state, const AtlasRange &range)
{
  const std::string key = std::to_string(static_cast<int>(std::llround(state->mesh_origin_latitude * 10000.0))) +
                          ":" + std::to_string(static_cast<int>(std::llround(state->mesh_origin_longitude * 10000.0))) +
                          ":" + range.key();
  if (state->globe_mesh_key == key && !state->globe_vertices.empty())
    return;

  state->globe_vertices.clear();
  state->globe_indices.clear();
  state->globe_vertices.reserve(static_cast<std::size_t>((kGlobeLatSegments + 1) *
                                                         (kGlobeLonSegments + 1) *
                                                         kVertexStride));
  state->globe_indices.reserve(static_cast<std::size_t>(kGlobeLatSegments *
                                                        kGlobeLonSegments *
                                                        6));

  for (int lat_i = 0; lat_i <= kGlobeLatSegments; ++lat_i) {
    const double latitude = 90.0 - 180.0 * static_cast<double>(lat_i) / kGlobeLatSegments;
    for (int lon_i = 0; lon_i <= kGlobeLonSegments; ++lon_i) {
      const double longitude = -180.0 + 360.0 * static_cast<double>(lon_i) / kGlobeLonSegments;
      const glm::dvec3 position = geodetic_to_enu(latitude,
                                                  longitude,
                                                  0.0,
                                                  state->mesh_origin_latitude,
                                                  state->mesh_origin_longitude,
                                                  0.0);
      glm::dvec3 normal64 = geodetic_to_enu(latitude,
                                            longitude,
                                            1000.0,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0) -
                            position;
      if (glm::length(normal64) <= 0.000001)
        normal64 = glm::dvec3(0.0, 1.0, 0.0);
      else
        normal64 = glm::normalize(normal64);

      append_globe_vertex(state->globe_vertices,
                          position,
                          latitude,
                          longitude,
                          range,
                          glm::vec3(static_cast<float>(normal64.x),
                                    static_cast<float>(normal64.y),
                                    static_cast<float>(normal64.z)));
    }
  }

  const int row_width = kGlobeLonSegments + 1;
  for (int lat_i = 0; lat_i < kGlobeLatSegments; ++lat_i) {
    for (int lon_i = 0; lon_i < kGlobeLonSegments; ++lon_i) {
      const unsigned int p00 = static_cast<unsigned int>(lat_i * row_width + lon_i);
      const unsigned int p10 = p00 + 1;
      const unsigned int p01 = static_cast<unsigned int>((lat_i + 1) * row_width + lon_i);
      const unsigned int p11 = p01 + 1;
      state->globe_indices.push_back(p00);
      state->globe_indices.push_back(p01);
      state->globe_indices.push_back(p10);
      state->globe_indices.push_back(p10);
      state->globe_indices.push_back(p01);
      state->globe_indices.push_back(p11);
    }
  }

  state->globe_mesh_key = key;
  state->globe_mesh_dirty = true;
}

void
append_flat_mesh(GWorldSceneViewState *state,
                 const AtlasRange &detail_range,
                 const AtlasRange &mid_range,
                 const AtlasRange &base_range)
{
  const double radius = terrain_build_radius_for_altitude(state->altitude_amsl);
  const glm::dvec3 p00(-radius, 0.0, radius);
  const glm::dvec3 p10(radius, 0.0, radius);
  const glm::dvec3 p01(-radius, 0.0, -radius);
  const glm::dvec3 p11(radius, 0.0, -radius);
  const TexCoords uv00{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  const TexCoords uv10{1.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f};
  const TexCoords uv01{0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
  const TexCoords uv11{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

  state->vertices.clear();
  state->indices.clear();
  append_triangle(state->vertices, state->indices, p00, p10, p01, uv00, uv10, uv01);
  append_triangle(state->vertices, state->indices, p10, p11, p01, uv10, uv11, uv01);
  state->mesh_atlas_range = detail_range;
  state->mesh_mid_atlas_range = mid_range;
  state->mesh_base_atlas_range = base_range;
  state->mesh_dirty = true;
}

void
append_terrain_tile_mesh_ring(GWorldSceneViewState *state,
                              const TerrainTile &tile,
                              const AtlasRange &detail_range,
                              const AtlasRange &mid_range,
                              const AtlasRange &base_range,
                              double inner_radius_m,
                              double outer_radius_m,
                              int sample_step)
{
  const double tile_center_lat = static_cast<double>(tile.lat) + 0.5;
  const double tile_center_lon = static_cast<double>(tile.lon) + 0.5;
  const glm::dvec3 tile_center = geodetic_to_enu(tile_center_lat,
                                                 tile_center_lon,
                                                 0.0,
                                                 state->mesh_origin_latitude,
                                                 state->mesh_origin_longitude,
                                                 0.0);
  const double tile_half_diag = 0.5 * std::sqrt(std::pow(kEarthMetersPerDegree, 2.0) +
                                               std::pow(kEarthMetersPerDegree * std::cos(deg_to_rad(tile_center_lat)), 2.0));
  const double tile_distance = std::hypot(tile_center.x, tile_center.z);
  if (tile_distance > outer_radius_m + tile_half_diag ||
      tile_distance + tile_half_diag < inner_radius_m)
    return;

  sample_step = std::clamp(sample_step, 1, tile.dimension - 1);

  for (int sy = 0; sy < tile.dimension - 1; sy += sample_step) {
    for (int sx = 0; sx < tile.dimension - 1; sx += sample_step) {
      const int sx1 = std::min(tile.dimension - 1, sx + sample_step);
      const int sy1 = std::min(tile.dimension - 1, sy + sample_step);

      double lat00 = 0.0;
      double lon00 = 0.0;
      double lat10 = 0.0;
      double lon10 = 0.0;
      double lat01 = 0.0;
      double lon01 = 0.0;
      double lat11 = 0.0;
      double lon11 = 0.0;
      sample_to_lat_lon(tile, sx, sy, lat00, lon00);
      sample_to_lat_lon(tile, sx1, sy, lat10, lon10);
      sample_to_lat_lon(tile, sx, sy1, lat01, lon01);
      sample_to_lat_lon(tile, sx1, sy1, lat11, lon11);

      const double center_lat = (lat00 + lat10 + lat01 + lat11) * 0.25;
      const double center_lon = (lon00 + lon10 + lon01 + lon11) * 0.25;
      const glm::dvec3 center = geodetic_to_enu(center_lat,
                                               center_lon,
                                               0.0,
                                               state->mesh_origin_latitude,
                                               state->mesh_origin_longitude,
                                               0.0);
      const double center_distance = std::hypot(center.x, center.z);
      if (center_distance <= inner_radius_m || center_distance > outer_radius_m + 2000.0)
        continue;

      const glm::dvec3 p00 = geodetic_to_enu(lat00,
                                             lon00,
                                             static_cast<double>(get_safe_height(tile, sx, sy)),
                                             state->mesh_origin_latitude,
                                             state->mesh_origin_longitude,
                                             0.0);
      const glm::dvec3 p10 = geodetic_to_enu(lat10,
                                             lon10,
                                             static_cast<double>(get_safe_height(tile, sx1, sy)),
                                             state->mesh_origin_latitude,
                                             state->mesh_origin_longitude,
                                             0.0);
      const glm::dvec3 p01 = geodetic_to_enu(lat01,
                                             lon01,
                                             static_cast<double>(get_safe_height(tile, sx, sy1)),
                                             state->mesh_origin_latitude,
                                             state->mesh_origin_longitude,
                                             0.0);
      const glm::dvec3 p11 = geodetic_to_enu(lat11,
                                             lon11,
                                             static_cast<double>(get_safe_height(tile, sx1, sy1)),
                                             state->mesh_origin_latitude,
                                             state->mesh_origin_longitude,
                                             0.0);

      TexCoords uv00;
      TexCoords uv10;
      TexCoords uv01;
      TexCoords uv11;
      uv_for_lat_lon(lat00, lon00, detail_range, uv00.detail_u, uv00.detail_v);
      uv_for_lat_lon(lat10, lon10, detail_range, uv10.detail_u, uv10.detail_v);
      uv_for_lat_lon(lat01, lon01, detail_range, uv01.detail_u, uv01.detail_v);
      uv_for_lat_lon(lat11, lon11, detail_range, uv11.detail_u, uv11.detail_v);
      uv_for_lat_lon(lat00, lon00, mid_range, uv00.mid_u, uv00.mid_v);
      uv_for_lat_lon(lat10, lon10, mid_range, uv10.mid_u, uv10.mid_v);
      uv_for_lat_lon(lat01, lon01, mid_range, uv01.mid_u, uv01.mid_v);
      uv_for_lat_lon(lat11, lon11, mid_range, uv11.mid_u, uv11.mid_v);
      uv_for_lat_lon(lat00, lon00, base_range, uv00.base_u, uv00.base_v);
      uv_for_lat_lon(lat10, lon10, base_range, uv10.base_u, uv10.base_v);
      uv_for_lat_lon(lat01, lon01, base_range, uv01.base_u, uv01.base_v);
      uv_for_lat_lon(lat11, lon11, base_range, uv11.base_u, uv11.base_v);

      append_triangle(state->vertices, state->indices, p00, p10, p01, uv00, uv10, uv01);
      append_triangle(state->vertices, state->indices, p10, p11, p01, uv10, uv11, uv01);
    }
  }
}

void
append_terrain_tile_mesh(GWorldSceneViewState *state,
                         const TerrainTile &tile,
                         const AtlasRange &detail_range,
                         const AtlasRange &mid_range,
                         const AtlasRange &base_range,
                         double radius_m,
                         int base_sample_step)
{
  const double near_radius = terrain_near_radius_for_altitude(state->altitude_amsl, radius_m);
  const double mid_radius = terrain_mid_radius_for_altitude(state->altitude_amsl, radius_m);
  const double far_radius = terrain_far_radius_for_altitude(state->altitude_amsl, radius_m);

  append_terrain_tile_mesh_ring(state,
                                tile,
                                detail_range,
                                mid_range,
                                base_range,
                                0.0,
                                near_radius,
                                terrain_lod_step(base_sample_step, 0));
  if (mid_radius > near_radius) {
    append_terrain_tile_mesh_ring(state,
                                  tile,
                                  detail_range,
                                  mid_range,
                                  base_range,
                                  near_radius,
                                  mid_radius,
                                  terrain_lod_step(base_sample_step, 1));
  }
  if (far_radius > mid_radius) {
    append_terrain_tile_mesh_ring(state,
                                  tile,
                                  detail_range,
                                  mid_range,
                                  base_range,
                                  mid_radius,
                                  far_radius,
                                  terrain_lod_step(base_sample_step, 2));
  }
  if (radius_m > far_radius) {
    append_terrain_tile_mesh_ring(state,
                                  tile,
                                  detail_range,
                                  mid_range,
                                  base_range,
                                  far_radius,
                                  radius_m,
                                  terrain_lod_step(base_sample_step, 3));
  }
}

void
rebuild_world_mesh(GWorldSceneViewState *state,
                   const AtlasRange &detail_range,
                   const AtlasRange &mid_range,
                   const AtlasRange &base_range,
                   bool include_terrain)
{
  const double radius_m = terrain_build_radius_for_altitude(state->altitude_amsl);
  const int sample_step = terrain_step_for_altitude(state->altitude_amsl);

  const std::string key = std::to_string(static_cast<int>(std::llround(state->latitude * 100000.0))) +
                          ":" + std::to_string(static_cast<int>(std::llround(state->longitude * 100000.0))) +
                          ":" + std::to_string(static_cast<int>(state->altitude_amsl)) +
                          ":" + std::to_string(sample_step) +
                          ":" + detail_range.key() +
                          ":" + mid_range.key() +
                          ":" + base_range.key() +
                          ":" + std::to_string(state->terrain_tiles.size()) +
                          ":" + std::to_string(state->scene_revision) +
                          ":" + (include_terrain ? "terrain" : "scene-only");

  if (state->mesh_key == key && !state->vertices.empty())
    return;

  state->mesh_origin_latitude = state->latitude;
  state->mesh_origin_longitude = state->longitude;
  state->vertices.clear();
  state->indices.clear();
  state->globe_mesh_key.clear();

  if (include_terrain) {
    for (const auto &entry : state->terrain_tiles)
      append_terrain_tile_mesh(state, entry.second, detail_range, mid_range, base_range, radius_m, sample_step);

    if (state->vertices.empty())
      append_flat_mesh(state, detail_range, mid_range, base_range);
  }

  append_scene_nodes(state);

  state->mesh_key = key;
  state->mesh_includes_terrain = include_terrain;
  state->mesh_atlas_range = detail_range;
  state->mesh_mid_atlas_range = mid_range;
  state->mesh_base_atlas_range = base_range;
  state->mesh_dirty = true;
}

GLuint
compile_shader(GLenum type, const char *source)
{
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {0};
    glGetShaderInfoLog(shader, sizeof log, nullptr, log);
    g_warning("Shader compile failed: %s", log);
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint
create_program()
{
  static const char *vertex_source = R"GLSL(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 detail_texcoord;
layout(location = 2) in vec2 mid_texcoord;
layout(location = 3) in vec2 base_texcoord;
layout(location = 4) in vec3 normal;
layout(location = 5) in vec3 vertex_color;
layout(location = 6) in float material;
uniform mat4 mvp;
uniform mat4 light_mvp;
out vec2 v_detail_texcoord;
out vec2 v_mid_texcoord;
out vec2 v_base_texcoord;
out vec3 v_normal;
out vec3 v_color;
out float v_material;
out float v_height;
out vec3 v_world_position;
out vec4 v_light_position;
void main() {
  vec4 world_position = vec4(position, 1.0);
  gl_Position = mvp * world_position;
  v_detail_texcoord = detail_texcoord;
  v_mid_texcoord = mid_texcoord;
  v_base_texcoord = base_texcoord;
  v_normal = normal;
  v_color = vertex_color;
  v_material = material;
  v_height = position.y;
  v_world_position = position;
  v_light_position = light_mvp * world_position;
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
in vec2 v_detail_texcoord;
in vec2 v_mid_texcoord;
in vec2 v_base_texcoord;
in vec3 v_normal;
in vec3 v_color;
in float v_material;
in float v_height;
in vec3 v_world_position;
in vec4 v_light_position;
uniform sampler2D detail_texture;
uniform sampler2D mid_texture;
uniform sampler2D base_texture;
uniform sampler2D shadow_texture;
uniform sampler2D model_texture;
uniform bool has_detail_texture;
uniform bool has_mid_texture;
uniform bool has_base_texture;
uniform bool has_shadow_texture;
uniform bool has_model_texture;
uniform vec3 sun_direction;
uniform float ambient_strength;
uniform float sun_strength;
uniform vec3 camera_position;
uniform bool fog_enabled;
uniform vec3 fog_color;
uniform float fog_start;
uniform float fog_end;
uniform float fog_density;
uniform float terrain_normal_smoothing;
out vec4 color;

vec3 lighting_normal() {
  vec3 normal = normalize(v_normal);
  if (v_material < 0.5 && terrain_normal_smoothing > 0.0) {
    vec3 local_up = vec3(0.0, 1.0, 0.0);
    float slope = 1.0 - clamp(abs(dot(normal, local_up)), 0.0, 1.0);
    float smoothing = clamp(terrain_normal_smoothing * (1.0 - slope * 0.28), 0.0, 1.0);
    normal = normalize(mix(normal, local_up, smoothing));
  }
  return normal;
}

float shadow_visibility(vec3 normal) {
  if (!has_shadow_texture || (v_material > 1.5 && v_material < 2.5))
    return 1.0;

  vec3 projected = v_light_position.xyz / v_light_position.w;
  projected = projected * 0.5 + 0.5;
  if (projected.z > 1.0 ||
      projected.x < 0.0 || projected.x > 1.0 ||
      projected.y < 0.0 || projected.y > 1.0)
    return 1.0;

  vec2 texel_size = 1.0 / vec2(textureSize(shadow_texture, 0));
  float bias = max(0.0012 * (1.0 - dot(normal, sun_direction)), 0.00035);
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      float depth = texture(shadow_texture, projected.xy + vec2(x, y) * texel_size).r;
      lit += projected.z - bias <= depth ? 1.0 : 0.0;
    }
  }

  return mix(0.42, 1.0, lit / 9.0);
}

float fog_amount() {
  if (!fog_enabled)
    return 0.0;

  float distance_to_camera = length(v_world_position - camera_position);
  float range = max(fog_end - fog_start, 1.0);
  float linear_fog = clamp((distance_to_camera - fog_start) / range, 0.0, 1.0);
  float density_fog = 1.0 - exp(-distance_to_camera * max(fog_density, 0.0));
  return clamp(max(linear_fog, density_fog), 0.0, 1.0);
}

void main() {
  vec3 normal = lighting_normal();
  float diffuse = clamp(dot(normal, normalize(sun_direction)), 0.0, 1.0);
  float visibility = shadow_visibility(normal);
  float shade = ambient_strength + diffuse * sun_strength * visibility;
  vec3 low = vec3(0.26, 0.36, 0.24);
  vec3 high = vec3(0.76, 0.72, 0.62);
  vec3 terrain_tint = mix(low, high, clamp(v_height / 1800.0, 0.0, 1.0));
  bool in_detail = v_detail_texcoord.x >= 0.0 && v_detail_texcoord.x <= 1.0 && v_detail_texcoord.y >= 0.0 && v_detail_texcoord.y <= 1.0;
  bool in_mid = v_mid_texcoord.x >= 0.0 && v_mid_texcoord.x <= 1.0 && v_mid_texcoord.y >= 0.0 && v_mid_texcoord.y <= 1.0;
  bool in_base = v_base_texcoord.x >= 0.0 && v_base_texcoord.x <= 1.0 && v_base_texcoord.y >= 0.0 && v_base_texcoord.y <= 1.0;
  vec4 base_texel = (has_base_texture && in_base) ? texture(base_texture, v_base_texcoord) : vec4(0.0);
  vec4 mid_texel = (has_mid_texture && in_mid) ? texture(mid_texture, v_mid_texcoord) : vec4(0.0);
  vec4 detail_texel = (has_detail_texture && in_detail) ? texture(detail_texture, v_detail_texcoord) : vec4(0.0);
  vec4 texel = detail_texel.a > 0.01 ? detail_texel : (mid_texel.a > 0.01 ? mid_texel : base_texel);
  vec3 terrain_base = mix(terrain_tint, texel.rgb, texel.a * 0.88);
  vec3 globe_base = texel.a > 0.01 ? texel.rgb : v_color;
  bool is_globe = v_material > 1.5 && v_material < 2.5;
  bool is_textured_model = v_material > 2.5;
  vec4 model_texel = (has_model_texture && is_textured_model) ? texture(model_texture, v_detail_texcoord) : vec4(0.0);
  vec3 object_base = (is_textured_model && model_texel.a > 0.01)
                       ? mix(v_color, model_texel.rgb * v_color, model_texel.a)
                       : v_color;
  vec3 base = is_globe ? globe_base : (v_material > 0.5 ? object_base : terrain_base);
  vec3 lit_color = base * clamp(shade, 0.0, 1.35);
  color = vec4(mix(lit_color, fog_color, fog_amount()), 1.0);
}
)GLSL";

  GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
  GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
  if (vertex == 0 || fragment == 0) {
    if (vertex)
      glDeleteShader(vertex);
    if (fragment)
      glDeleteShader(fragment);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {0};
    glGetProgramInfoLog(program, sizeof log, nullptr, log);
    g_warning("Program link failed: %s", log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

GLuint
create_shadow_program()
{
  static const char *vertex_source = R"GLSL(
#version 330 core
layout(location = 0) in vec3 position;
uniform mat4 light_mvp;
void main() {
  gl_Position = light_mvp * vec4(position, 1.0);
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
void main() {
}
)GLSL";

  GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source);
  GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
  if (vertex == 0 || fragment == 0) {
    if (vertex)
      glDeleteShader(vertex);
    if (fragment)
      glDeleteShader(fragment);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (ok != GL_TRUE) {
    char log[1024] = {0};
    glGetProgramInfoLog(program, sizeof log, nullptr, log);
    g_warning("Shadow program link failed: %s", log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

bool
ensure_shadow_resources(GWorldSceneViewState *state)
{
  if (state->shadow_program == 0)
    state->shadow_program = create_shadow_program();
  if (state->shadow_program == 0)
    return false;

  if (state->shadow_depth_texture == 0) {
    glGenTextures(1, &state->shadow_depth_texture);
    glBindTexture(GL_TEXTURE_2D, state->shadow_depth_texture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_DEPTH_COMPONENT24,
                 kShadowMapSize,
                 kShadowMapSize,
                 0,
                 GL_DEPTH_COMPONENT,
                 GL_FLOAT,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border_color[] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border_color);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  if (state->shadow_fbo == 0) {
    GLint previous_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
    glGenFramebuffers(1, &state->shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, state->shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_DEPTH_ATTACHMENT,
                           GL_TEXTURE_2D,
                           state->shadow_depth_texture,
                           0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      g_warning("Shadow framebuffer is incomplete: 0x%x", status);
      return false;
    }
  }

  return true;
}

glm::mat4
shadow_light_matrix(const glm::dvec3 &center,
                    const glm::vec3 &sun_direction,
                    double extent_m)
{
  const glm::dvec3 light_direction =
    safe_normalize(glm::dvec3(sun_direction), glm::dvec3(-0.45, 0.76, 0.38));
  const double extent = std::max(1000.0, extent_m);
  const double depth = extent * 4.0;
  glm::dvec3 up(0.0, 1.0, 0.0);
  if (std::abs(glm::dot(light_direction, up)) > 0.9)
    up = glm::dvec3(0.0, 0.0, -1.0);

  const glm::dvec3 eye = center + light_direction * depth;
  const glm::mat4 light_view = glm::lookAt(glm::vec3(eye),
                                           glm::vec3(center),
                                           glm::vec3(up));
  const glm::mat4 light_projection = glm::ortho(static_cast<float>(-extent),
                                                static_cast<float>(extent),
                                                static_cast<float>(-extent),
                                                static_cast<float>(extent),
                                                1.0f,
                                                static_cast<float>(depth * 2.0));
  return light_projection * light_view;
}

bool
render_shadow_map(GWorldSceneViewState *state, const glm::mat4 &light_mvp)
{
  if (state->index_count == 0 || !ensure_shadow_resources(state))
    return false;

  GLint previous_fbo = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);

  glViewport(0, 0, kShadowMapSize, kShadowMapSize);
  glBindFramebuffer(GL_FRAMEBUFFER, state->shadow_fbo);
  glClear(GL_DEPTH_BUFFER_BIT);
  glUseProgram(state->shadow_program);
  glUniformMatrix4fv(glGetUniformLocation(state->shadow_program, "light_mvp"),
                     1,
                     GL_FALSE,
                     glm::value_ptr(light_mvp));
  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(2.0f, 4.0f);
  glBindVertexArray(state->vao);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(state->index_count), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glDisable(GL_POLYGON_OFFSET_FILL);
  glUseProgram(0);
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_fbo));
  return true;
}

void
delete_gl_resources(GWorldSceneViewState *state)
{
  if (state->texture)
    glDeleteTextures(1, &state->texture);
  if (state->mid_texture)
    glDeleteTextures(1, &state->mid_texture);
  if (state->base_texture)
    glDeleteTextures(1, &state->base_texture);
  if (state->globe_texture)
    glDeleteTextures(1, &state->globe_texture);
  if (state->model_texture)
    glDeleteTextures(1, &state->model_texture);
  if (state->shadow_depth_texture)
    glDeleteTextures(1, &state->shadow_depth_texture);
  if (state->shadow_fbo)
    glDeleteFramebuffers(1, &state->shadow_fbo);
  if (state->globe_ebo)
    glDeleteBuffers(1, &state->globe_ebo);
  if (state->globe_vbo)
    glDeleteBuffers(1, &state->globe_vbo);
  if (state->globe_vao)
    glDeleteVertexArrays(1, &state->globe_vao);
  if (state->ebo)
    glDeleteBuffers(1, &state->ebo);
  if (state->vbo)
    glDeleteBuffers(1, &state->vbo);
  if (state->vao)
    glDeleteVertexArrays(1, &state->vao);
  if (state->program)
    glDeleteProgram(state->program);
  if (state->shadow_program)
    glDeleteProgram(state->shadow_program);

  state->texture = 0;
  state->mid_texture = 0;
  state->base_texture = 0;
  state->globe_texture = 0;
  state->model_texture = 0;
  state->shadow_depth_texture = 0;
  state->shadow_fbo = 0;
  state->globe_ebo = 0;
  state->globe_vbo = 0;
  state->globe_vao = 0;
  state->ebo = 0;
  state->vbo = 0;
  state->vao = 0;
  state->program = 0;
  state->shadow_program = 0;
  state->index_count = 0;
  state->globe_index_count = 0;
}

void
configure_scene_vertex_attributes()
{
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kVertexStride * sizeof(float), reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        kVertexStride * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        kVertexStride * sizeof(float),
                        reinterpret_cast<void *>(5 * sizeof(float)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        kVertexStride * sizeof(float),
                        reinterpret_cast<void *>(7 * sizeof(float)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4,
                        3,
                        GL_FLOAT,
                        GL_FALSE,
                        kVertexStride * sizeof(float),
                        reinterpret_cast<void *>(9 * sizeof(float)));
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5,
                        3,
                        GL_FLOAT,
                        GL_FALSE,
                        kVertexStride * sizeof(float),
                        reinterpret_cast<void *>(12 * sizeof(float)));
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6,
                        1,
                        GL_FLOAT,
                        GL_FALSE,
                        kVertexStride * sizeof(float),
                        reinterpret_cast<void *>(15 * sizeof(float)));
}

void
upload_vertex_index_buffers(GLuint &vao,
                            GLuint &vbo,
                            GLuint &ebo,
                            const std::vector<float> &vertices,
                            const std::vector<unsigned int> &indices,
                            std::size_t &index_count)
{
  if (vao == 0)
    glGenVertexArrays(1, &vao);
  if (vbo == 0)
    glGenBuffers(1, &vbo);
  if (ebo == 0)
    glGenBuffers(1, &ebo);

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
               vertices.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
               indices.data(),
               GL_STATIC_DRAW);

  configure_scene_vertex_attributes();

  glBindVertexArray(0);
  index_count = indices.size();
}

void
upload_mesh_if_needed(GWorldSceneViewState *state)
{
  if (!state->mesh_dirty)
    return;

  upload_vertex_index_buffers(state->vao,
                              state->vbo,
                              state->ebo,
                              state->vertices,
                              state->indices,
                              state->index_count);
  state->mesh_dirty = false;
}

void
upload_globe_mesh_if_needed(GWorldSceneViewState *state)
{
  if (!state->globe_mesh_dirty)
    return;

  upload_vertex_index_buffers(state->globe_vao,
                              state->globe_vbo,
                              state->globe_ebo,
                              state->globe_vertices,
                              state->globe_indices,
                              state->globe_index_count);
  state->globe_mesh_dirty = false;
}

void
upload_texture_buffer_if_needed(GLuint &texture,
                                const char *label,
                                const std::vector<unsigned char> &pixels,
                                int width,
                                int height,
                                bool &dirty)
{
  if (!dirty)
    return;

  if (pixels.empty() || width <= 0 || height <= 0) {
    if (texture) {
      glDeleteTextures(1, &texture);
      texture = 0;
      g_debug("Texture upload cleared: layer=%s", label);
    }
    dirty = false;
    return;
  }

  const std::size_t expected_size = static_cast<std::size_t>(width) *
                                    static_cast<std::size_t>(height) * 4;
  if (pixels.size() < expected_size) {
    g_warning("Texture upload skipped: layer=%s size=%dx%d bytes=%zu expected=%zu",
              label,
              width,
              height,
              pixels.size(),
              expected_size);
    dirty = false;
    return;
  }

  if (texture == 0)
    glGenTextures(1, &texture);

  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D,
               0,
               GL_RGBA8,
               width,
               height,
               0,
               GL_RGBA,
               GL_UNSIGNED_BYTE,
               pixels.data());
  const GLenum error = glGetError();
  glBindTexture(GL_TEXTURE_2D, 0);

  if (error != GL_NO_ERROR) {
    g_warning("Texture upload reported GL error: layer=%s texture=%u size=%dx%d error=0x%x",
              label,
              texture,
              width,
              height,
              error);
  } else {
    g_debug("Texture uploaded: layer=%s texture=%u size=%dx%d bytes=%zu",
            label,
            texture,
            width,
            height,
            pixels.size());
  }

  dirty = false;
}

void
upload_textures_if_needed(GWorldSceneViewState *state)
{
  upload_texture_buffer_if_needed(state->texture,
                                  "detail",
                                  state->texture_pixels,
                                  state->texture_width,
                                  state->texture_height,
                                  state->texture_dirty);
  upload_texture_buffer_if_needed(state->mid_texture,
                                  "mid",
                                  state->mid_texture_pixels,
                                  state->mid_texture_width,
                                  state->mid_texture_height,
                                  state->mid_texture_dirty);
  upload_texture_buffer_if_needed(state->base_texture,
                                  "base",
                                  state->base_texture_pixels,
                                  state->base_texture_width,
                                  state->base_texture_height,
                                  state->base_texture_dirty);
  upload_texture_buffer_if_needed(state->globe_texture,
                                  "globe",
                                  state->globe_texture_pixels,
                                  state->globe_texture_width,
                                  state->globe_texture_height,
                                  state->globe_texture_dirty);
  upload_texture_buffer_if_needed(state->model_texture,
                                  "model",
                                  state->model_texture_pixels,
                                  state->model_texture_width,
                                  state->model_texture_height,
                                  state->model_texture_dirty);
}

} // namespace

static gboolean scheduled_scene_request_cb(gpointer user_data);

GWorldSceneViewBackend::GWorldSceneViewBackend(GWorldSceneView *owner)
  : owner_(owner),
    state_(new GWorldSceneViewState)
{
  state_->cache_directory = default_cache_directory();
}

GWorldSceneViewBackend::~GWorldSceneViewBackend()
{
  if (state_ != nullptr && state_->scene_update_source != 0)
    g_source_remove(state_->scene_update_source);
  delete state_;
  state_ = nullptr;
}

GWorldSceneView *
GWorldSceneViewBackend::owner() const
{
  return owner_;
}

GWorldSceneViewState &
GWorldSceneViewBackend::state()
{
  return *state_;
}

const GWorldSceneViewState &
GWorldSceneViewBackend::state() const
{
  return *state_;
}

void
GWorldSceneViewBackend::schedule_scene_requests(guint delay_ms)
{
  guint old_source = 0;
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    old_source = state_->scene_update_source;
    state_->scene_update_source = 0;
  }

  if (old_source != 0)
    g_source_remove(old_source);

  const guint source = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE,
                                          delay_ms,
                                          scheduled_scene_request_cb,
                                          g_object_ref(owner_),
                                          g_object_unref);
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->scene_update_source = source;
  }
}

void
GWorldSceneViewBackend::ensure_scene_requests_scheduled(guint delay_ms)
{
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->scene_update_source != 0)
      return;
  }

  const guint source = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE,
                                          delay_ms,
                                          scheduled_scene_request_cb,
                                          g_object_ref(owner_),
                                          g_object_unref);
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->scene_update_source = source;
  }
}

typedef struct _GWorldSceneViewPrivate {
  GWorldSceneViewBackend *backend;
} GWorldSceneViewPrivate;

struct _GWorldSceneView {
  GtkGLArea parent_instance;
};

G_DEFINE_TYPE_WITH_PRIVATE(GWorldSceneView, gworld_scene_view, GTK_TYPE_GL_AREA)

enum {
  PROP_0,
  PROP_LATITUDE,
  PROP_LONGITUDE,
  PROP_ALTITUDE_AMSL,
  PROP_TERRAIN_SERVER,
  PROP_MAP_TILE_URL_TEMPLATE,
  PROP_CACHE_DIRECTORY,
  PROP_CACHE_ENABLED,
  PROP_SUN_AZIMUTH_DEG,
  PROP_SUN_ELEVATION_DEG,
  PROP_SUN_TIME_OF_DAY,
  PROP_FOG_ENABLED,
  PROP_SHADOWS_ENABLED,
  PROP_TERRAIN_NORMAL_SMOOTHING,
  N_PROPS,
};

static GParamSpec *properties[N_PROPS];

static GWorldSceneViewBackend *
get_backend(GWorldSceneView *self)
{
  auto *priv = static_cast<GWorldSceneViewPrivate *>(gworld_scene_view_get_instance_private(self));
  return priv->backend;
}

static GWorldSceneViewState *
get_state(GWorldSceneView *self)
{
  return &get_backend(self)->state();
}

static void queue_scene_requests(GWorldSceneView *self);
static void schedule_scene_requests(GWorldSceneView *self, guint delay_ms = kSceneUpdateDelayMs);
static void ensure_scene_requests_scheduled(GWorldSceneView *self, guint delay_ms = kSceneUpdateDelayMs);
static void on_scene_node_changed(GWorldSceneNode *node, gpointer user_data);

static gboolean
scheduled_scene_request_cb(gpointer user_data)
{
  auto *self = GWORLD_SCENE_VIEW(user_data);
  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->scene_update_source = 0;
  }

  queue_scene_requests(self);
  return G_SOURCE_REMOVE;
}

static void
schedule_scene_requests(GWorldSceneView *self, guint delay_ms)
{
  get_backend(self)->schedule_scene_requests(delay_ms);
}

static void
ensure_scene_requests_scheduled(GWorldSceneView *self, guint delay_ms)
{
  get_backend(self)->ensure_scene_requests_scheduled(delay_ms);
}

static bool
load_image_rgba_256(const std::string &path,
                    std::vector<unsigned char> &pixels,
                    std::string *error_message = nullptr)
{
  return load_image_rgba_scaled(path,
                                kAtlasTilePixels,
                                kAtlasTilePixels,
                                pixels,
                                error_message);
}

static void
texture_atlas_build_job_free(TextureAtlasBuildJob *job)
{
  delete job;
}

static void
texture_atlas_build_result_free(TextureAtlasBuildResult *result)
{
  delete result;
}

static void
texture_atlas_build_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  (void)source_object;
  (void)cancellable;

  auto *job = static_cast<TextureAtlasBuildJob *>(task_data);
  auto *result = new TextureAtlasBuildResult;
  result->range = job->range;
  result->layer = job->layer;
  result->pending_key = job->pending_key;
  result->source_revision = job->source_revision;

  const int tiles_w = job->range.width_tiles();
  const int tiles_h = job->range.height_tiles();
  const int width = tiles_w * kAtlasTilePixels;
  const int height = tiles_h * kAtlasTilePixels;
  result->pixels.assign(static_cast<std::size_t>(width * height * 4), 0);
  result->width = width;
  result->height = height;

  for (int ty = job->range.y_min; ty <= job->range.y_max; ++ty) {
    for (int tx = job->range.x_min; tx <= job->range.x_max; ++tx) {
      TileCoord tile{job->range.z, tx, ty};
      const std::string uri = texture_uri_from_template(job->texture_template, tile);
      const std::string path = texture_cache_path(job->cache_dir, uri, job->texture_template, tile);
      if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) {
        ++result->missing_count;
        continue;
      }

      std::vector<unsigned char> tile_pixels;
      std::string load_error;
      if (!load_image_rgba_256(path, tile_pixels, &load_error)) {
        ++result->decode_failed_count;
        g_warning("Texture tile decode failed: layer=%s z=%d x=%d y=%d path=%s error=%s",
                  texture_layer_name(job->layer),
                  tile.z,
                  tile.x,
                  tile.y,
                  path.c_str(),
                  load_error.empty() ? "unknown error" : load_error.c_str());
        continue;
      }

      ++result->loaded_count;
      const int dst_x = (tx - job->range.x_min) * kAtlasTilePixels;
      const int dst_y = (ty - job->range.y_min) * kAtlasTilePixels;
      for (int y = 0; y < kAtlasTilePixels; ++y) {
        const std::size_t src = static_cast<std::size_t>(y * kAtlasTilePixels * 4);
        const std::size_t dst = static_cast<std::size_t>(((dst_y + y) * width + dst_x) * 4);
        std::memcpy(result->pixels.data() + dst, tile_pixels.data() + src, kAtlasTilePixels * 4);
      }
    }
  }

  if (result->loaded_count == 0) {
    g_debug("Texture atlas build produced no pixels: layer=%s range=%s missing=%d decode_failed=%d",
            texture_layer_name(job->layer),
            job->range.key().c_str(),
            result->missing_count,
            result->decode_failed_count);
    result->pixels.clear();
    result->width = 0;
    result->height = 0;
  }

  g_task_return_pointer(task,
                        result,
                        reinterpret_cast<GDestroyNotify>(texture_atlas_build_result_free));
}

static void
texture_atlas_build_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  (void)user_data;

  auto *self = GWORLD_SCENE_VIEW(source_object);
  auto *state = get_state(self);
  auto *task = G_TASK(result);

  g_autoptr(GError) error = nullptr;
  auto *atlas_result = static_cast<TextureAtlasBuildResult *>(g_task_propagate_pointer(task, &error));
  if (atlas_result == nullptr)
    return;

  const std::string range_key = atlas_result->range.key();
  bool applied = false;
  bool cleared_pending = false;
  std::string wanted_key_snapshot;
  guint64 state_revision = 0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    auto &pending_key = texture_layer_pending_key(state, atlas_result->layer);
    if (pending_key == atlas_result->pending_key) {
      pending_key.clear();
      cleared_pending = true;
    }
    const std::string &wanted_key = texture_layer_wanted_key(state, atlas_result->layer);
    wanted_key_snapshot = wanted_key;
    state_revision = state->texture_source_revision;

    if (wanted_key == range_key && atlas_result->source_revision == state->texture_source_revision) {
      auto &pixels = texture_layer_pixels(state, atlas_result->layer);
      auto &texture_width = texture_layer_width(state, atlas_result->layer);
      auto &texture_height = texture_layer_height(state, atlas_result->layer);
      auto &dirty = texture_layer_dirty(state, atlas_result->layer);
      auto &loaded_key = texture_layer_loaded_key(state, atlas_result->layer);
      auto &active_range = texture_layer_active_range(state, atlas_result->layer);

      pixels = std::move(atlas_result->pixels);
      texture_width = atlas_result->width;
      texture_height = atlas_result->height;
      dirty = true;
      loaded_key = range_key;
      active_range = atlas_result->range;
      state->mesh_key.clear();
      applied = true;
    }
  }

  if (applied) {
    g_debug("Texture atlas applied: layer=%s range=%s loaded=%d missing=%d decode_failed=%d size=%dx%d revision=%" G_GUINT64_FORMAT,
            texture_layer_name(atlas_result->layer),
            range_key.c_str(),
            atlas_result->loaded_count,
            atlas_result->missing_count,
            atlas_result->decode_failed_count,
            atlas_result->width,
            atlas_result->height,
            atlas_result->source_revision);
  } else {
    g_debug("Texture atlas discarded: layer=%s range=%s wanted=%s loaded=%d missing=%d decode_failed=%d result_revision=%" G_GUINT64_FORMAT " current_revision=%" G_GUINT64_FORMAT " pending_cleared=%s",
            texture_layer_name(atlas_result->layer),
            range_key.c_str(),
            wanted_key_snapshot.c_str(),
            atlas_result->loaded_count,
            atlas_result->missing_count,
            atlas_result->decode_failed_count,
            atlas_result->source_revision,
            state_revision,
            cleared_pending ? "yes" : "no");
  }

  texture_atlas_build_result_free(atlas_result);
  if (applied) {
    queue_scene_requests(self);
  } else {
    ensure_scene_requests_scheduled(self, 20);
  }
}

static void
request_texture_atlas_build(GWorldSceneView *self, const AtlasRange &range, TextureLayer layer)
{
  auto *state = get_state(self);
  if (!range.valid()) {
    std::lock_guard<std::mutex> lock(state->mutex);
    auto &pixels = texture_layer_pixels(state, layer);
    auto &width = texture_layer_width(state, layer);
    auto &height = texture_layer_height(state, layer);
    auto &dirty = texture_layer_dirty(state, layer);
    auto &loaded_key = texture_layer_loaded_key(state, layer);
    auto &wanted_key = texture_layer_wanted_key(state, layer);
    auto &active_range = texture_layer_active_range(state, layer);
    auto &pending_key = texture_layer_pending_key(state, layer);
    pixels.clear();
    width = 0;
    height = 0;
    dirty = true;
    loaded_key.clear();
    wanted_key.clear();
    pending_key.clear();
    active_range = AtlasRange();
    state->mesh_key.clear();
    return;
  }

  const std::string range_key = range.key();
  const std::string pending_key = std::string(texture_layer_name(layer)) + ":" + range_key;
  std::string cache_dir;
  std::string texture_template;
  guint64 source_revision = 0;

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    cache_dir = state->cache_directory;
    texture_template = state->map_tile_template;
    source_revision = state->texture_source_revision;
    auto &wanted_key = texture_layer_wanted_key(state, layer);
    auto &pending_layer_key = texture_layer_pending_key(state, layer);
    const std::string &loaded_key = texture_layer_loaded_key(state, layer);
    wanted_key = range_key;

    if (loaded_key == range_key) {
      g_debug("Texture atlas already loaded: layer=%s range=%s",
              texture_layer_name(layer),
              range_key.c_str());
      return;
    }
    if (!pending_layer_key.empty()) {
      g_debug("Texture atlas build already pending: layer=%s wanted=%s pending=%s",
              texture_layer_name(layer),
              range_key.c_str(),
              pending_layer_key.c_str());
      return;
    }

    pending_layer_key = pending_key;
  }

  g_debug("Texture atlas build queued: layer=%s range=%s pending=%s revision=%" G_GUINT64_FORMAT,
          texture_layer_name(layer),
          range_key.c_str(),
          pending_key.c_str(),
          source_revision);

  auto *job = new TextureAtlasBuildJob;
  job->range = range;
  job->layer = layer;
  job->cache_dir = cache_dir;
  job->texture_template = texture_template;
  job->pending_key = pending_key;
  job->source_revision = source_revision;

  GTask *task = g_task_new(self, nullptr, texture_atlas_build_done, nullptr);
  g_task_set_task_data(task, job, reinterpret_cast<GDestroyNotify>(texture_atlas_build_job_free));
  g_task_run_in_thread(task, texture_atlas_build_thread);
  g_object_unref(task);
}

static void
download_job_free(DownloadJob *job)
{
  delete job;
}

gint64
note_terrain_download_failure_locked(GWorldSceneViewState *state,
                                     const std::string &key,
                                     bool unavailable,
                                     gint64 now_us)
{
  if (unavailable) {
    state->unavailable_terrain_keys.insert(key);
    state->terrain_failure_counts.erase(key);
    state->terrain_retry_after_us.erase(key);
    return 0;
  }

  unsigned int &failures = state->terrain_failure_counts[key];
  ++failures;
  const unsigned int backoff_step = std::min(failures - 1, 5u);
  const gint64 delay_us = std::min(kTerrainRetryMaxDelayUs,
                                   kTerrainRetryBaseDelayUs *
                                     (static_cast<gint64>(1) << backoff_step));
  state->terrain_retry_after_us[key] = now_us + delay_us;
  return delay_us;
}

void
note_terrain_download_success_locked(GWorldSceneViewState *state, const std::string &key)
{
  state->unavailable_terrain_keys.erase(key);
  state->terrain_failure_counts.erase(key);
  state->terrain_retry_after_us.erase(key);
}

void
clear_terrain_download_state_locked(GWorldSceneViewState *state)
{
  state->unavailable_terrain_keys.clear();
  state->terrain_failure_counts.clear();
  state->terrain_retry_after_us.clear();
}

static void
download_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  (void)source_object;
  auto *job = static_cast<DownloadJob *>(task_data);

  ensure_parent_directory(job->path);

  g_autoptr(SoupSession) session = soup_session_new();
  g_autoptr(SoupMessage) message = soup_message_new("GET", job->uri.c_str());
  if (message == nullptr) {
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "Invalid URI: %s", job->uri.c_str());
    return;
  }

  soup_message_headers_append(soup_message_get_request_headers(message),
                              "User-Agent",
                              "GWorldScene/0.1 (GTK4 GLArea)");

  g_autoptr(GError) error = nullptr;
  GBytes *bytes = soup_session_send_and_read(session, message, cancellable, &error);
  g_autoptr(GBytes) owned_bytes = bytes;
  const guint status = soup_message_get_status(message);
  if (bytes == nullptr) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }
  if (status < 200 || status >= 300) {
    const GIOErrorEnum code =
      (status == 404 || status == 410) ? G_IO_ERROR_NOT_FOUND : G_IO_ERROR_FAILED;
    g_task_return_new_error(task, G_IO_ERROR, code, "HTTP %u for %s", status, job->uri.c_str());
    return;
  }

  gsize size = 0;
  const auto *data = static_cast<const char *>(g_bytes_get_data(bytes, &size));
  if (!g_file_set_contents(job->path.c_str(), data, size, &error)) {
    g_task_return_error(task, g_steal_pointer(&error));
    return;
  }

  g_task_return_boolean(task, TRUE);
}

static void
download_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  (void)user_data;

  auto *self = GWORLD_SCENE_VIEW(source_object);
  auto *task = G_TASK(result);
  auto *job = static_cast<DownloadJob *>(g_task_get_task_data(task));
  auto *state = get_state(self);

  g_autoptr(GError) error = nullptr;
  const gboolean ok = g_task_propagate_boolean(task, &error);

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->pending.erase(job->pending_key);
  }

  if (!ok) {
    if (job->kind == "texture") {
      g_warning("Texture download failed: key=%s uri=%s cache=%s error=%s",
                job->pending_key.c_str(),
                job->uri.c_str(),
                job->path.c_str(),
                error ? error->message : "unknown error");
    } else {
      const std::string key = terrain_key(job->terrain_lat, job->terrain_lon);
      const bool unavailable = error != nullptr &&
                               error->domain == G_IO_ERROR &&
                               error->code == G_IO_ERROR_NOT_FOUND;
      gint64 retry_delay_us = 0;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        retry_delay_us = note_terrain_download_failure_locked(state,
                                                              key,
                                                              unavailable,
                                                              g_get_monotonic_time());
      }
      if (unavailable) {
        g_debug("Terrain tile unavailable: key=%s uri=%s cache=%s error=%s",
                job->pending_key.c_str(),
                job->uri.c_str(),
                job->path.c_str(),
                error ? error->message : "unknown error");
      } else {
        g_debug("Terrain download failed: key=%s uri=%s cache=%s retry_in_ms=%" G_GINT64_FORMAT " error=%s",
                job->pending_key.c_str(),
                job->uri.c_str(),
                job->path.c_str(),
                retry_delay_us / 1000,
                error ? error->message : "unknown error");
        ensure_scene_requests_scheduled(self,
                                        static_cast<guint>(std::clamp<gint64>(retry_delay_us / 1000,
                                                                              1000,
                                                                              60000)));
      }
    }
    return;
  }

  if (job->kind == "terrain") {
    TerrainTile tile;
    tile.lat = job->terrain_lat;
    tile.lon = job->terrain_lon;
    if (read_hgt_file(job->path, tile.heights, tile.dimension)) {
      const std::string key = terrain_key(tile.lat, tile.lon);
      std::lock_guard<std::mutex> lock(state->mutex);
      state->terrain_tiles[key] = std::move(tile);
      note_terrain_download_success_locked(state, key);
      state->mesh_key.clear();
    }
  } else if (job->kind == "texture") {
    g_debug("Texture download finished: key=%s uri=%s cache=%s",
            job->pending_key.c_str(),
            job->uri.c_str(),
            job->path.c_str());
    std::lock_guard<std::mutex> lock(state->mutex);
    state->loaded_texture_key.clear();
    state->loaded_mid_texture_key.clear();
    state->loaded_base_texture_key.clear();
    state->loaded_globe_texture_key.clear();
  }

  schedule_scene_requests(self, 120);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
start_download(GWorldSceneView *self,
               const std::string &kind,
               const std::string &pending_key,
               const std::string &uri,
               const std::string &path,
               int terrain_lat,
               int terrain_lon)
{
  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->pending.find(pending_key) != state->pending.end()) {
      if (kind == "texture") {
        g_debug("Texture download already pending: key=%s uri=%s cache=%s",
                pending_key.c_str(),
                uri.c_str(),
                path.c_str());
      }
      return;
    }
    state->pending.insert(pending_key);
  }

  if (kind == "texture") {
    g_debug("Texture download queued: key=%s uri=%s cache=%s",
            pending_key.c_str(),
            uri.c_str(),
            path.c_str());
  }

  auto *job = new DownloadJob;
  job->kind = kind;
  job->pending_key = pending_key;
  job->uri = uri;
  job->path = path;
  job->terrain_lat = terrain_lat;
  job->terrain_lon = terrain_lon;

  GTask *task = g_task_new(self, nullptr, download_done, nullptr);
  g_task_set_task_data(task, job, reinterpret_cast<GDestroyNotify>(download_job_free));
  g_task_run_in_thread(task, download_thread);
  g_object_unref(task);
}

static bool
load_cached_terrain_tile(GWorldSceneView *self,
                         int tile_lat,
                         int tile_lon,
                         const std::string &tile_name,
                         const std::string &path)
{
  auto *state = get_state(self);
  const std::string key = terrain_key(tile_lat, tile_lon);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->terrain_tiles.find(key) != state->terrain_tiles.end())
      return true;
  }

  std::string load_path = path;
  const std::string raw_path = join_path(join_path(state->cache_directory, "terrain"), tile_name + ".hgt");
  const std::string zip_path = join_path(join_path(state->cache_directory, "terrain"), tile_name + ".hgt.zip");
  if (g_file_test(raw_path.c_str(), G_FILE_TEST_EXISTS))
    load_path = raw_path;
  else if (g_file_test(zip_path.c_str(), G_FILE_TEST_EXISTS))
    load_path = zip_path;
  else if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
    return false;

  TerrainTile tile;
  tile.lat = tile_lat;
  tile.lon = tile_lon;
  if (!read_hgt_file(load_path, tile.heights, tile.dimension))
    return false;

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->terrain_tiles[key] = std::move(tile);
    state->mesh_key.clear();
  }
  return true;
}

static void
queue_scene_requests(GWorldSceneView *self)
{
  auto *state = get_state(self);

  double latitude = 0.0;
  double longitude = 0.0;
  double altitude = 0.0;
  std::string cache_dir;
  std::string terrain_template;
  std::string texture_template;
  bool cache_enabled = true;

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    latitude = state->latitude;
    longitude = state->longitude;
    altitude = state->altitude_amsl;
    cache_dir = state->cache_directory;
    terrain_template = state->terrain_server;
    texture_template = state->map_tile_template;
    cache_enabled = state->cache_enabled;
  }

  const double radius_m = terrain_build_radius_for_altitude(altitude);
  const bool render_globe = altitude >= kGlobeRenderAltitudeM;
  const bool include_terrain = altitude < kTerrainDisableAltitudeM;
  const LatLonBounds terrain_bounds = bounds_around_camera(latitude, longitude, radius_m);
  const LatLonBounds texture_bounds =
    bounds_around_camera(latitude, longitude, texture_radius_for_altitude(altitude));
  const LatLonBounds mid_texture_bounds =
    bounds_around_camera(latitude, longitude, mid_texture_radius_for_altitude(altitude, radius_m));
  const AtlasRange atlas_range = select_atlas_range(texture_bounds, detail_texture_zoom_for_altitude(altitude));
  const AtlasRange mid_atlas_range =
    select_atlas_range(mid_texture_bounds, mid_texture_zoom_for_altitude(altitude));
  const AtlasRange base_atlas_range =
    select_atlas_range(terrain_bounds, base_texture_zoom_for_altitude(altitude));
  const AtlasRange globe_range = globe_atlas_range(latitude, longitude, altitude);

  if (cache_enabled) {
    if (include_terrain) {
      std::unordered_set<std::string> keep_keys;
      std::vector<TerrainCandidate> candidates;
      const int lat_min = static_cast<int>(std::floor(terrain_bounds.min_lat));
      const int lat_max = static_cast<int>(std::floor(terrain_bounds.max_lat));
      const int lon_min = static_cast<int>(std::floor(terrain_bounds.min_lon));
      const int lon_max = static_cast<int>(std::floor(terrain_bounds.max_lon));

      for (int lat = lat_min; lat <= lat_max; ++lat) {
        for (int lon = lon_min; lon <= lon_max; ++lon) {
          const std::string key = terrain_key(lat, lon);
          keep_keys.insert(key);
          const glm::dvec3 center = geodetic_to_enu(static_cast<double>(lat) + 0.5,
                                                    static_cast<double>(lon) + 0.5,
                                                    0.0,
                                                    latitude,
                                                    longitude,
                                                    0.0);
          candidates.push_back({lat, lon, std::hypot(center.x, center.z)});
        }
      }

      std::sort(candidates.begin(), candidates.end(), [](const TerrainCandidate &a, const TerrainCandidate &b) {
        return a.distance < b.distance;
      });

      int terrain_loads = 0;
      int terrain_downloads = 0;
      bool has_more_terrain_work = false;
      guint terrain_reschedule_ms = 60000;

      for (const TerrainCandidate &candidate : candidates) {
        const int lat = candidate.lat;
        const int lon = candidate.lon;
        const std::string key = terrain_key(lat, lon);
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          if (state->terrain_tiles.find(key) != state->terrain_tiles.end())
            continue;
        }

        const std::string name = hgt_tile_name(lat, lon);
        const std::string uri = terrain_uri_from_template(terrain_template, name);
        const std::string path = terrain_cache_path_for_uri(cache_dir, uri, name);

        if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS) ||
            g_file_test(join_path(join_path(cache_dir, "terrain"), name + ".hgt").c_str(), G_FILE_TEST_EXISTS) ||
            g_file_test(join_path(join_path(cache_dir, "terrain"), name + ".hgt.zip").c_str(), G_FILE_TEST_EXISTS)) {
          if (terrain_loads >= kMaxTerrainTileLoadsPerUpdate) {
            has_more_terrain_work = true;
            terrain_reschedule_ms = 350;
            continue;
          }
          ++terrain_loads;
          load_cached_terrain_tile(self, lat, lon, name, path);
          continue;
        }

        {
          std::lock_guard<std::mutex> lock(state->mutex);
          if (state->unavailable_terrain_keys.find(key) != state->unavailable_terrain_keys.end())
            continue;

          auto retry_iter = state->terrain_retry_after_us.find(key);
          if (retry_iter != state->terrain_retry_after_us.end()) {
            const gint64 now_us = g_get_monotonic_time();
            if (retry_iter->second > now_us) {
              has_more_terrain_work = true;
              const guint retry_delay_ms =
                static_cast<guint>(std::clamp<gint64>((retry_iter->second - now_us) / 1000,
                                                      350,
                                                      60000));
              terrain_reschedule_ms = std::min(terrain_reschedule_ms, retry_delay_ms);
              continue;
            }
          }
        }

        if (terrain_downloads >= kMaxTerrainDownloadsPerUpdate) {
          has_more_terrain_work = true;
          terrain_reschedule_ms = 350;
          continue;
        }

        ++terrain_downloads;
        start_download(self, "terrain", "terrain:" + key, uri, path, lat, lon);
      }

      {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const std::string &pending_key : state->pending) {
          if (pending_key.rfind("terrain:", 0) == 0) {
            has_more_terrain_work = true;
            terrain_reschedule_ms = 350;
            break;
          }
        }
      }

      if (has_more_terrain_work)
        schedule_scene_requests(self, terrain_reschedule_ms);

      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->wanted_terrain_keys = keep_keys;
        for (auto it = state->terrain_tiles.begin(); it != state->terrain_tiles.end();) {
          if (keep_keys.find(it->first) == keep_keys.end())
            it = state->terrain_tiles.erase(it);
          else
            ++it;
        }
      }
    } else {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->wanted_terrain_keys.clear();
      if (!state->terrain_tiles.empty()) {
        state->terrain_tiles.clear();
        state->mesh_key.clear();
      }
    }

    int texture_downloads = 0;
    bool has_more_texture_work = false;
    auto request_texture_range = [&](const AtlasRange &range) {
      if (!range.valid())
        return;

      for (int ty = range.y_min; ty <= range.y_max; ++ty) {
        for (int tx = range.x_min; tx <= range.x_max; ++tx) {
          TileCoord tile{range.z, tx, ty};
          const std::string uri = texture_uri_from_template(texture_template, tile);
          const std::string path = texture_cache_path(cache_dir, uri, texture_template, tile);
          if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
            continue;
          if (texture_downloads >= kMaxTextureDownloadsPerUpdate) {
            has_more_texture_work = true;
            continue;
          }
          const std::string key = "texture:" + std::to_string(tile.z) + "/" +
                                  std::to_string(tile.x) + "/" + std::to_string(tile.y);
          ++texture_downloads;
          start_download(self, "texture", key, uri, path, 0, 0);
        }
      }
    };

    if (include_terrain) {
      request_texture_range(base_atlas_range);
      request_texture_range(mid_atlas_range);
      request_texture_range(atlas_range);
    }
    if (render_globe)
      request_texture_range(globe_range);

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      for (const std::string &pending_key : state->pending) {
        if (pending_key.rfind("texture:", 0) == 0) {
          has_more_texture_work = true;
          break;
        }
      }
    }

    if (has_more_texture_work)
      schedule_scene_requests(self, 350);
  }

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    const AtlasRange mesh_atlas_range =
      state->texture_atlas_range.valid() ? state->texture_atlas_range : atlas_range;
    const AtlasRange mesh_mid_atlas_range =
      state->mid_texture_atlas_range.valid() ? state->mid_texture_atlas_range : mid_atlas_range;
    const AtlasRange mesh_base_atlas_range =
      state->base_texture_atlas_range.valid() ? state->base_texture_atlas_range : base_atlas_range;
    const AtlasRange mesh_globe_atlas_range =
      state->globe_texture_atlas_range.valid() ? state->globe_texture_atlas_range : globe_range;
    rebuild_world_mesh(state, mesh_atlas_range, mesh_mid_atlas_range, mesh_base_atlas_range, include_terrain);
    if (render_globe)
      rebuild_globe_mesh(state, mesh_globe_atlas_range);
  }

  if (include_terrain) {
    request_texture_atlas_build(self, base_atlas_range, TextureLayer::Base);
    request_texture_atlas_build(self, mid_atlas_range, TextureLayer::Mid);
    request_texture_atlas_build(self, atlas_range, TextureLayer::Detail);
  }
  if (render_globe)
    request_texture_atlas_build(self, globe_range, TextureLayer::Globe);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
on_realize(GtkGLArea *area, gpointer user_data)
{
  (void)user_data;

  auto *self = GWORLD_SCENE_VIEW(area);
  auto *state = get_state(self);

  gtk_gl_area_make_current(area);
  if (gtk_gl_area_get_error(area) != nullptr)
    return;

  state->program = create_program();
  schedule_scene_requests(self);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
on_unrealize(GtkGLArea *area, gpointer user_data)
{
  (void)user_data;

  gtk_gl_area_make_current(area);
  if (gtk_gl_area_get_error(area) != nullptr)
    return;

  auto *state = get_state(GWORLD_SCENE_VIEW(area));
  delete_gl_resources(state);
}

static gboolean
on_render(GtkGLArea *area, GdkGLContext *context, gpointer user_data)
{
  (void)context;
  (void)user_data;

  auto *self = GWORLD_SCENE_VIEW(area);
  auto *state = get_state(self);

  if (state->program == 0)
    state->program = create_program();
  if (state->program == 0)
    return TRUE;

  glEnable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  double altitude = 0.0;
  double heading = 0.0;
  double pitch = 0.0;
  double radius_m = 0.0;
  double latitude = 0.0;
  double longitude = 0.0;
  double mesh_origin_latitude = 0.0;
  double mesh_origin_longitude = 0.0;
  bool render_globe = false;
  bool render_terrain = true;
  bool mesh_includes_terrain = true;
  double sun_azimuth = 0.0;
  double sun_elevation = 0.0;
  double sun_time_of_day = 0.0;
  double sun_declination = 0.0;
  bool sun_uses_time_of_day = false;
  bool fog_enabled = false;
  double fog_start = 0.0;
  double fog_end = 0.0;
  glm::vec3 fog_color(0.60f, 0.72f, 0.86f);
  bool shadows_enabled = false;
  double terrain_normal_smoothing = 0.0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    latitude = state->latitude;
    longitude = state->longitude;
    altitude = std::max(25.0, state->altitude_amsl);
    heading = state->heading_deg;
    pitch = std::clamp(state->pitch_deg, kMinCameraPitchDeg, kMaxCameraPitchDeg);
    radius_m = terrain_build_radius_for_altitude(state->altitude_amsl);
    mesh_origin_latitude = state->mesh_origin_latitude;
    mesh_origin_longitude = state->mesh_origin_longitude;
    render_globe = altitude >= kGlobeRenderAltitudeM;
    render_terrain = altitude < kTerrainDisableAltitudeM;
    mesh_includes_terrain = state->mesh_includes_terrain;
    sun_azimuth = state->sun_azimuth_deg;
    sun_elevation = state->sun_elevation_deg;
    sun_time_of_day = state->sun_time_of_day;
    sun_declination = state->sun_declination_deg;
    sun_uses_time_of_day = state->sun_uses_time_of_day;
    fog_enabled = state->fog_enabled;
    fog_start = state->fog_start_m;
    fog_end = state->fog_end_m;
    fog_color = state->fog_color;
    shadows_enabled = state->shadows_enabled;
    terrain_normal_smoothing = state->terrain_normal_smoothing;
    upload_mesh_if_needed(state);
    if (render_globe)
      upload_globe_mesh_if_needed(state);
    upload_textures_if_needed(state);
  }

  const int width = std::max(1, gtk_widget_get_width(GTK_WIDGET(area)));
  const int height = std::max(1, gtk_widget_get_height(GTK_WIDGET(area)));
  glViewport(0, 0, width, height);

  double far_plane_m = std::max(100000.0, radius_m * 3.0 + altitude * 4.0);
  if (render_globe)
    far_plane_m = std::max(far_plane_m, altitude + gworld_scene::kWgs84A * 2.4);
  const float far_plane = static_cast<float>(far_plane_m);
  const float near_plane = render_globe
                             ? static_cast<float>(std::clamp(altitude * 0.002, 25.0, 25000.0))
                             : 2.0f;
  const glm::mat4 projection = glm::perspective(glm::radians(45.0f),
                                                static_cast<float>(width) / static_cast<float>(height),
                                                near_plane,
                                                far_plane);

  const glm::dvec3 sun_enu =
    sun_uses_time_of_day
      ? gworld_scene::sun_direction_from_time(latitude, sun_time_of_day, sun_declination)
      : gworld_scene::sun_direction_from_position(sun_azimuth, sun_elevation);
  const glm::vec3 sun_direction = sun_scene_direction_from_enu(sun_enu);
  const float daylight = static_cast<float>(smoothstep_value(-0.08, 0.18, sun_enu.z));
  const float ambient_strength = 0.14f + daylight * 0.34f;
  const float sun_strength = daylight * 0.64f;
  effective_fog_range(fog_start, fog_end, altitude, render_globe, fog_start, fog_end);
  const float fog_density =
    fog_enabled ? static_cast<float>(1.0 / std::max(fog_end * 2.8, 1.0)) : 0.0f;

  const gworld_scene::CameraPose camera_pose =
    gworld_scene::blended_camera_pose(latitude,
                                      longitude,
                                      altitude,
                                      heading,
                                      pitch,
                                      mesh_origin_latitude,
                                      mesh_origin_longitude);
  const glm::mat4 view = glm::lookAt(glm::vec3(camera_pose.eye),
                                     glm::vec3(camera_pose.center),
                                     glm::vec3(camera_pose.up));
  const glm::mat4 mvp = projection * view;
  const bool render_world_mesh = state->index_count > 0 && (render_terrain || !mesh_includes_terrain);

  glm::mat4 light_mvp(1.0f);
  bool shadow_available = false;
  if (shadows_enabled &&
      render_world_mesh &&
      altitude < kShadowMaxAltitudeM &&
      sun_enu.z > 0.04) {
    const gworld_scene::LocalFrame frame =
      gworld_scene::local_frame_at(latitude, longitude, mesh_origin_latitude, mesh_origin_longitude);
    const double shadow_extent =
      std::clamp(std::max(terrain_near_radius_for_altitude(altitude, radius_m),
                          altitude * 4.0),
                 2500.0,
                 120000.0);
    light_mvp = shadow_light_matrix(frame.origin, sun_direction, shadow_extent);
    shadow_available = render_shadow_map(state, light_mvp);
  }

  glViewport(0, 0, width, height);
  glClearColor(fog_color.r, fog_color.g, fog_color.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram(state->program);
  glUniformMatrix4fv(glGetUniformLocation(state->program, "mvp"),
                     1,
                     GL_FALSE,
                     glm::value_ptr(mvp));
  glUniformMatrix4fv(glGetUniformLocation(state->program, "light_mvp"),
                     1,
                     GL_FALSE,
                     glm::value_ptr(light_mvp));
  glUniform1i(glGetUniformLocation(state->program, "detail_texture"), 0);
  glUniform1i(glGetUniformLocation(state->program, "mid_texture"), 1);
  glUniform1i(glGetUniformLocation(state->program, "base_texture"), 2);
  glUniform1i(glGetUniformLocation(state->program, "shadow_texture"), 3);
  glUniform1i(glGetUniformLocation(state->program, "model_texture"), 4);
  glUniform3fv(glGetUniformLocation(state->program, "sun_direction"),
               1,
               glm::value_ptr(sun_direction));
  glUniform1f(glGetUniformLocation(state->program, "ambient_strength"), ambient_strength);
  glUniform1f(glGetUniformLocation(state->program, "sun_strength"), sun_strength);
  glUniform3fv(glGetUniformLocation(state->program, "camera_position"),
               1,
               glm::value_ptr(glm::vec3(camera_pose.eye)));
  glUniform1i(glGetUniformLocation(state->program, "fog_enabled"), fog_enabled);
  glUniform3fv(glGetUniformLocation(state->program, "fog_color"),
               1,
               glm::value_ptr(fog_color));
  glUniform1f(glGetUniformLocation(state->program, "fog_start"), static_cast<float>(fog_start));
  glUniform1f(glGetUniformLocation(state->program, "fog_end"), static_cast<float>(fog_end));
  glUniform1f(glGetUniformLocation(state->program, "fog_density"), fog_density);
  glUniform1f(glGetUniformLocation(state->program, "terrain_normal_smoothing"),
              static_cast<float>(std::clamp(terrain_normal_smoothing, 0.0, 1.0)));
  if (shadow_available) {
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, state->shadow_depth_texture);
  }

  if (render_globe && state->globe_index_count > 0) {
    glUniform1i(glGetUniformLocation(state->program, "has_detail_texture"), FALSE);
    glUniform1i(glGetUniformLocation(state->program, "has_mid_texture"), FALSE);
    glUniform1i(glGetUniformLocation(state->program, "has_base_texture"), state->globe_texture != 0);
    glUniform1i(glGetUniformLocation(state->program, "has_shadow_texture"), FALSE);
    glUniform1i(glGetUniformLocation(state->program, "has_model_texture"), FALSE);
    if (state->globe_texture != 0) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, state->globe_texture);
    }
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBindVertexArray(state->globe_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(state->globe_index_count), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    glDisable(GL_CULL_FACE);
  }

  if (render_world_mesh) {
    glDisable(GL_CULL_FACE);
    glUniform1i(glGetUniformLocation(state->program, "has_detail_texture"), state->texture != 0);
    glUniform1i(glGetUniformLocation(state->program, "has_mid_texture"), state->mid_texture != 0);
    glUniform1i(glGetUniformLocation(state->program, "has_base_texture"), state->base_texture != 0);
    glUniform1i(glGetUniformLocation(state->program, "has_shadow_texture"), shadow_available);
    glUniform1i(glGetUniformLocation(state->program, "has_model_texture"), state->model_texture != 0);

    if (state->texture != 0) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, state->texture);
    }
    if (state->mid_texture != 0) {
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D, state->mid_texture);
    }
    if (state->base_texture != 0) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, state->base_texture);
    }
    if (state->model_texture != 0) {
      glActiveTexture(GL_TEXTURE4);
      glBindTexture(GL_TEXTURE_2D, state->model_texture);
    }

    glBindVertexArray(state->vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(state->index_count), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
  }
  glUseProgram(0);

  return TRUE;
}

static void
move_camera_by_enu(GWorldSceneView *self, double east_m, double north_m, double up_m)
{
  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    gworld_scene::translate_geodetic_ned(state->latitude,
                                         state->longitude,
                                         state->altitude_amsl,
                                         north_m,
                                         east_m,
                                         -up_m,
                                         &latitude,
                                         &longitude,
                                         &altitude);
    state->latitude = latitude;
    state->longitude = longitude;
    state->altitude_amsl = std::clamp(altitude, 25.0, 10000000.0);
    state->mesh_key.clear();
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_LATITUDE]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_LONGITUDE]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ALTITUDE_AMSL]);

  ensure_scene_requests_scheduled(self);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
rotate_camera_pixels(GWorldSceneView *self, double dx, double dy)
{
  auto *state = get_state(self);
  double heading = 0.0;
  double pitch = 0.0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    heading = state->heading_deg + dx * 0.18;
    pitch = std::clamp(state->pitch_deg - dy * 0.14, kMinCameraPitchDeg, kMaxCameraPitchDeg);
  }
  gworld_scene_view_set_camera_orientation(self, heading, pitch);
}

static void
on_rotate_drag_begin(GtkGestureDrag *gesture, double start_x, double start_y, gpointer user_data)
{
  (void)gesture;
  (void)start_x;
  (void)start_y;
  auto *self = GWORLD_SCENE_VIEW(user_data);
  gtk_widget_grab_focus(GTK_WIDGET(self));
  auto *state = get_state(self);
  std::unique_lock<std::mutex> lock(state->mutex);
  state->rotate_drag_last_x = 0.0;
  state->rotate_drag_last_y = 0.0;
}

static void
on_rotate_drag_update(GtkGestureDrag *gesture, double offset_x, double offset_y, gpointer user_data)
{
  (void)gesture;
  auto *self = GWORLD_SCENE_VIEW(user_data);
  auto *state = get_state(self);
  double dx = 0.0;
  double dy = 0.0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    dx = offset_x - state->rotate_drag_last_x;
    dy = offset_y - state->rotate_drag_last_y;
    state->rotate_drag_last_x = offset_x;
    state->rotate_drag_last_y = offset_y;
  }
  rotate_camera_pixels(self, dx, dy);
}

static gboolean
on_scroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer user_data)
{
  (void)controller;
  (void)dx;
  auto *self = GWORLD_SCENE_VIEW(user_data);
  gtk_widget_grab_focus(GTK_WIDGET(self));

  double latitude = 0.0;
  double longitude = 0.0;
  double altitude = 0.0;
  gworld_scene_view_get_camera(self, &latitude, &longitude, &altitude);
  const double factor = std::exp(dy * 0.16);
  altitude = std::clamp(altitude * factor, 25.0, 10000000.0);
  gworld_scene_view_set_camera(self, latitude, longitude, altitude);
  return TRUE;
}

static bool
set_movement_key(GWorldSceneViewState *state, guint keyval, bool pressed)
{
  switch (keyval) {
  case GDK_KEY_w:
  case GDK_KEY_W:
    state->move_forward = pressed;
    return true;
  case GDK_KEY_s:
  case GDK_KEY_S:
    state->move_backward = pressed;
    return true;
  case GDK_KEY_a:
  case GDK_KEY_A:
    state->move_left = pressed;
    return true;
  case GDK_KEY_d:
  case GDK_KEY_D:
    state->move_right = pressed;
    return true;
  case GDK_KEY_Shift_L:
  case GDK_KEY_Shift_R:
    state->move_fast = pressed;
    return true;
  default:
    return false;
  }
}

static gboolean
on_tick(GtkWidget *widget, GdkFrameClock *frame_clock, gpointer user_data)
{
  (void)user_data;

  auto *self = GWORLD_SCENE_VIEW(widget);
  auto *state = get_state(self);

  const gint64 now_us = gdk_frame_clock_get_frame_time(frame_clock);
  double altitude = 0.0;
  double heading = 0.0;
  bool forward = false;
  bool backward = false;
  bool left = false;
  bool right = false;
  bool fast = false;
  double dt = 0.0;

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->last_tick_time_us != 0)
      dt = static_cast<double>(now_us - state->last_tick_time_us) / 1000000.0;
    state->last_tick_time_us = now_us;

    altitude = std::max(25.0, state->altitude_amsl);
    heading = state->heading_deg;
    forward = state->move_forward;
    backward = state->move_backward;
    left = state->move_left;
    right = state->move_right;
    fast = state->move_fast;
  }

  if (dt <= 0.0 || (!forward && !backward && !left && !right))
    return G_SOURCE_CONTINUE;

  dt = std::min(dt, 0.05);
  const glm::dvec2 direction =
    gworld_scene::camera_movement_direction_for_input(altitude, heading, forward, backward, left, right);
  if (glm::length(direction) <= 0.000001)
    return G_SOURCE_CONTINUE;

  const double base_speed_mps = altitude >= kGlobeRenderAltitudeM
                                  ? std::clamp(altitude * 0.75, 1200.0, 250000.0)
                                  : std::clamp(altitude * 0.65, 35.0, 4000.0);
  const double speed_mps = base_speed_mps * (fast ? 3.0 : 1.0);
  move_camera_by_enu(self, direction.x * speed_mps * dt, direction.y * speed_mps * dt, 0.0);

  return G_SOURCE_CONTINUE;
}

static gboolean
on_key_pressed(GtkEventControllerKey *controller,
               guint keyval,
               guint keycode,
               GdkModifierType modifiers,
               gpointer user_data)
{
  (void)controller;
  (void)keycode;
  auto *self = GWORLD_SCENE_VIEW(user_data);
  auto *state = get_state(self);

  double heading = 0.0;
  double pitch = 0.0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (modifiers & GDK_SHIFT_MASK)
      state->move_fast = true;
    if (set_movement_key(state, keyval, true))
      return TRUE;

    heading = state->heading_deg;
    pitch = state->pitch_deg;
  }

  switch (keyval) {
  case GDK_KEY_Left:
    gworld_scene_view_set_camera_orientation(self, heading - 6.0, pitch);
    return TRUE;
  case GDK_KEY_Right:
    gworld_scene_view_set_camera_orientation(self, heading + 6.0, pitch);
    return TRUE;
  case GDK_KEY_Up:
    gworld_scene_view_set_camera_orientation(self,
                                            heading,
                                            std::clamp(pitch - 4.0,
                                                       kMinCameraPitchDeg,
                                                       kMaxCameraPitchDeg));
    return TRUE;
  case GDK_KEY_Down:
    gworld_scene_view_set_camera_orientation(self,
                                            heading,
                                            std::clamp(pitch + 4.0,
                                                       kMinCameraPitchDeg,
                                                       kMaxCameraPitchDeg));
    return TRUE;
  default:
    break;
  }

  return FALSE;
}

static void
on_key_released(GtkEventControllerKey *controller,
                guint keyval,
                guint keycode,
                GdkModifierType modifiers,
                gpointer user_data)
{
  (void)controller;
  (void)keycode;
  auto *self = GWORLD_SCENE_VIEW(user_data);
  auto *state = get_state(self);
  std::unique_lock<std::mutex> lock(state->mutex);
  set_movement_key(state, keyval, false);
  if ((modifiers & GDK_SHIFT_MASK) == 0 && keyval != GDK_KEY_Shift_L && keyval != GDK_KEY_Shift_R)
    state->move_fast = false;
}

static void
on_click_pressed(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
  (void)gesture;
  (void)n_press;
  (void)x;
  (void)y;
  gtk_widget_grab_focus(GTK_WIDGET(user_data));
}

static void
gworld_scene_view_set_property(GObject *object,
                               guint prop_id,
                               const GValue *value,
                               GParamSpec *pspec)
{
  auto *self = GWORLD_SCENE_VIEW(object);

  switch (prop_id) {
  case PROP_LATITUDE:
  case PROP_LONGITUDE:
  case PROP_ALTITUDE_AMSL: {
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude_amsl = 0.0;
    gworld_scene_view_get_camera(self, &latitude, &longitude, &altitude_amsl);
    if (prop_id == PROP_LATITUDE)
      latitude = g_value_get_double(value);
    else if (prop_id == PROP_LONGITUDE)
      longitude = g_value_get_double(value);
    else
      altitude_amsl = g_value_get_double(value);
    gworld_scene_view_set_camera(self, latitude, longitude, altitude_amsl);
    break;
  }
  case PROP_TERRAIN_SERVER:
    gworld_scene_view_set_terrain_server(self, g_value_get_string(value));
    break;
  case PROP_MAP_TILE_URL_TEMPLATE:
    gworld_scene_view_set_map_tile_url_template(self, g_value_get_string(value));
    break;
  case PROP_CACHE_DIRECTORY:
    gworld_scene_view_set_cache_directory(self, g_value_get_string(value));
    break;
  case PROP_CACHE_ENABLED:
    gworld_scene_view_set_cache_enabled(self, g_value_get_boolean(value));
    break;
  case PROP_SUN_AZIMUTH_DEG: {
    double azimuth = 0.0;
    double elevation = 0.0;
    gworld_scene_view_get_sun_position(self, &azimuth, &elevation);
    gworld_scene_view_set_sun_position(self, g_value_get_double(value), elevation);
    break;
  }
  case PROP_SUN_ELEVATION_DEG: {
    double azimuth = 0.0;
    double elevation = 0.0;
    gworld_scene_view_get_sun_position(self, &azimuth, &elevation);
    gworld_scene_view_set_sun_position(self, azimuth, g_value_get_double(value));
    break;
  }
  case PROP_SUN_TIME_OF_DAY:
    gworld_scene_view_set_sun_time_of_day(self, g_value_get_double(value));
    break;
  case PROP_FOG_ENABLED:
    gworld_scene_view_set_fog_enabled(self, g_value_get_boolean(value));
    break;
  case PROP_SHADOWS_ENABLED:
    gworld_scene_view_set_shadows_enabled(self, g_value_get_boolean(value));
    break;
  case PROP_TERRAIN_NORMAL_SMOOTHING:
    gworld_scene_view_set_terrain_normal_smoothing(self, g_value_get_double(value));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
gworld_scene_view_get_property(GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
  auto *self = GWORLD_SCENE_VIEW(object);
  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);

  switch (prop_id) {
  case PROP_LATITUDE:
    g_value_set_double(value, state->latitude);
    break;
  case PROP_LONGITUDE:
    g_value_set_double(value, state->longitude);
    break;
  case PROP_ALTITUDE_AMSL:
    g_value_set_double(value, state->altitude_amsl);
    break;
  case PROP_TERRAIN_SERVER:
    g_value_set_string(value, state->terrain_server.c_str());
    break;
  case PROP_MAP_TILE_URL_TEMPLATE:
    g_value_set_string(value, state->map_tile_template.c_str());
    break;
  case PROP_CACHE_DIRECTORY:
    g_value_set_string(value, state->cache_directory.c_str());
    break;
  case PROP_CACHE_ENABLED:
    g_value_set_boolean(value, state->cache_enabled);
    break;
  case PROP_SUN_AZIMUTH_DEG: {
    const gworld_scene::SunPosition sun_position =
      gworld_scene::sun_position_from_direction(sun_enu_for_state(state, state->latitude));
    g_value_set_double(value, sun_position.azimuth_deg);
    break;
  }
  case PROP_SUN_ELEVATION_DEG: {
    const gworld_scene::SunPosition sun_position =
      gworld_scene::sun_position_from_direction(sun_enu_for_state(state, state->latitude));
    g_value_set_double(value, sun_position.elevation_deg);
    break;
  }
  case PROP_SUN_TIME_OF_DAY:
    g_value_set_double(value, state->sun_time_of_day);
    break;
  case PROP_FOG_ENABLED:
    g_value_set_boolean(value, state->fog_enabled);
    break;
  case PROP_SHADOWS_ENABLED:
    g_value_set_boolean(value, state->shadows_enabled);
    break;
  case PROP_TERRAIN_NORMAL_SMOOTHING:
    g_value_set_double(value, state->terrain_normal_smoothing);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void
gworld_scene_view_dispose(GObject *object)
{
  G_OBJECT_CLASS(gworld_scene_view_parent_class)->dispose(object);
}

static void
gworld_scene_view_finalize(GObject *object)
{
  auto *priv = static_cast<GWorldSceneViewPrivate *>(
    gworld_scene_view_get_instance_private(GWORLD_SCENE_VIEW(object)));
  {
    GWorldSceneViewState &state = priv->backend->state();
    std::lock_guard<std::mutex> lock(state.mutex);
    for (auto &entry : state.scene_nodes) {
      g_signal_handlers_disconnect_by_func(entry.second,
                                           reinterpret_cast<gpointer>(on_scene_node_changed),
                                           object);
      g_object_unref(entry.second);
    }
    state.scene_nodes.clear();
  }
  delete priv->backend;
  priv->backend = nullptr;

  G_OBJECT_CLASS(gworld_scene_view_parent_class)->finalize(object);
}

static void
gworld_scene_view_class_init(GWorldSceneViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->set_property = gworld_scene_view_set_property;
  object_class->get_property = gworld_scene_view_get_property;
  object_class->dispose = gworld_scene_view_dispose;
  object_class->finalize = gworld_scene_view_finalize;

  properties[PROP_LATITUDE] =
    g_param_spec_double("latitude",
                        "Latitude",
                        "Camera latitude in degrees",
                        -90.0,
                        90.0,
                        -35.0024,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_LONGITUDE] =
    g_param_spec_double("longitude",
                        "Longitude",
                        "Camera longitude in degrees",
                        -180.0,
                        180.0,
                        147.4648,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_ALTITUDE_AMSL] =
    g_param_spec_double("altitude-amsl",
                        "Altitude AMSL",
                        "Camera altitude above mean sea level in metres",
                        -500.0,
                        10000000.0,
                        4200.0,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_TERRAIN_SERVER] =
    g_param_spec_string("terrain-server",
                        "Terrain Server",
                        "Terrain URL template or base directory; use {tile} for names like S34E147",
                        kDefaultTerrainServer,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_MAP_TILE_URL_TEMPLATE] =
    g_param_spec_string("map-tile-url-template",
                        "Map Tile URL Template",
                        "Slippy map URL template using {z}, {x}, and {y}",
                        kDefaultMapTileTemplate,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_CACHE_DIRECTORY] =
    g_param_spec_string("cache-directory",
                        "Cache Directory",
                        "Directory used for cached terrain and map tiles",
                        nullptr,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_CACHE_ENABLED] =
    g_param_spec_boolean("cache-enabled",
                         "Cache Enabled",
                         "Whether terrain and map tiles are cached on disk",
                         TRUE,
                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_SUN_AZIMUTH_DEG] =
    g_param_spec_double("sun-azimuth-deg",
                        "Sun Azimuth",
                        "Sun azimuth in degrees clockwise from geographic north",
                        0.0,
                        360.0,
                        228.0,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_SUN_ELEVATION_DEG] =
    g_param_spec_double("sun-elevation-deg",
                        "Sun Elevation",
                        "Sun elevation in degrees above the horizon",
                        -90.0,
                        90.0,
                        50.0,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_SUN_TIME_OF_DAY] =
    g_param_spec_double("sun-time-of-day",
                        "Sun Time Of Day",
                        "Approximate local solar hour used by time-of-day sun mode",
                        0.0,
                        24.0,
                        14.0,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_FOG_ENABLED] =
    g_param_spec_boolean("fog-enabled",
                         "Fog Enabled",
                         "Whether distance fog is applied",
                         TRUE,
                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_SHADOWS_ENABLED] =
    g_param_spec_boolean("shadows-enabled",
                         "Shadows Enabled",
                         "Whether the local terrain and scene nodes use the directional shadow map",
                         TRUE,
                         static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));
  properties[PROP_TERRAIN_NORMAL_SMOOTHING] =
    g_param_spec_double("terrain-normal-smoothing",
                        "Terrain Normal Smoothing",
                        "Shader-side smoothing amount applied to terrain lighting normals",
                        0.0,
                        1.0,
                        0.88,
                        static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY));

  g_object_class_install_properties(object_class, N_PROPS, properties);
}

static void
gworld_scene_view_init(GWorldSceneView *self)
{
  auto *priv = static_cast<GWorldSceneViewPrivate *>(gworld_scene_view_get_instance_private(self));
  priv->backend = new GWorldSceneViewBackend(self);

  gtk_gl_area_set_required_version(GTK_GL_AREA(self), 3, 3);
  gtk_gl_area_set_has_depth_buffer(GTK_GL_AREA(self), TRUE);
  gtk_gl_area_set_auto_render(GTK_GL_AREA(self), TRUE);
  gtk_widget_set_hexpand(GTK_WIDGET(self), TRUE);
  gtk_widget_set_vexpand(GTK_WIDGET(self), TRUE);
  gtk_widget_set_focusable(GTK_WIDGET(self), TRUE);

  static gsize registered = 0;
  if (g_once_init_enter(&registered)) {
    GDALAllRegister();
    Assimp::Importer importer;
    (void)importer;
    g_once_init_leave(&registered, 1);
  }

  auto *click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
  g_signal_connect(click, "pressed", G_CALLBACK(on_click_pressed), self);
  gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(click));

  auto *rotate = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(rotate), GDK_BUTTON_PRIMARY);
  g_signal_connect(rotate, "drag-begin", G_CALLBACK(on_rotate_drag_begin), self);
  g_signal_connect(rotate, "drag-update", G_CALLBACK(on_rotate_drag_update), self);
  gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(rotate));

  auto *scroll = gtk_event_controller_scroll_new(static_cast<GtkEventControllerScrollFlags>(
    GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE));
  g_signal_connect(scroll, "scroll", G_CALLBACK(on_scroll), self);
  gtk_widget_add_controller(GTK_WIDGET(self), scroll);

  auto *key = gtk_event_controller_key_new();
  g_signal_connect(key, "key-pressed", G_CALLBACK(on_key_pressed), self);
  g_signal_connect(key, "key-released", G_CALLBACK(on_key_released), self);
  gtk_widget_add_controller(GTK_WIDGET(self), key);

  gtk_widget_add_tick_callback(GTK_WIDGET(self), on_tick, nullptr, nullptr);

  g_signal_connect(self, "realize", G_CALLBACK(on_realize), nullptr);
  g_signal_connect(self, "unrealize", G_CALLBACK(on_unrealize), nullptr);
  g_signal_connect(self, "render", G_CALLBACK(on_render), nullptr);
}

GtkWidget *
gworld_scene_view_new(void)
{
  return GTK_WIDGET(g_object_new(GWORLD_TYPE_SCENE_VIEW, nullptr));
}

void
gworld_scene_view_set_camera(GWorldSceneView *self,
                             double latitude,
                             double longitude,
                             double altitude_amsl)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->latitude = std::clamp(latitude, -90.0, 90.0);
    state->longitude = std::clamp(longitude, -180.0, 180.0);
    state->altitude_amsl = std::clamp(altitude_amsl, 25.0, 10000000.0);
    state->mesh_key.clear();
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_LATITUDE]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_LONGITUDE]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ALTITUDE_AMSL]);

  schedule_scene_requests(self);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_get_camera(GWorldSceneView *self,
                             double *latitude,
                             double *longitude,
                             double *altitude_amsl)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  if (latitude)
    *latitude = state->latitude;
  if (longitude)
    *longitude = state->longitude;
  if (altitude_amsl)
    *altitude_amsl = state->altitude_amsl;
}

void
gworld_scene_view_set_camera_orientation(GWorldSceneView *self,
                                         double heading_deg,
                                         double pitch_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->heading_deg = std::fmod(heading_deg, 360.0);
    if (state->heading_deg < 0.0)
      state->heading_deg += 360.0;
    state->pitch_deg = std::clamp(pitch_deg, kMinCameraPitchDeg, kMaxCameraPitchDeg);
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_get_camera_orientation(GWorldSceneView *self,
                                         double *heading_deg,
                                         double *pitch_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  if (heading_deg)
    *heading_deg = state->heading_deg;
  if (pitch_deg)
    *pitch_deg = state->pitch_deg;
}

void
gworld_scene_view_set_terrain_server(GWorldSceneView *self, const char *terrain_server)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->terrain_server = terrain_server ? terrain_server : kDefaultTerrainServer;
    state->terrain_tiles.clear();
    clear_terrain_download_state_locked(state);
    state->mesh_key.clear();
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_TERRAIN_SERVER]);
  schedule_scene_requests(self, 20);
}

const char *
gworld_scene_view_get_terrain_server(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);
  return get_state(self)->terrain_server.c_str();
}

void
gworld_scene_view_set_map_tile_url_template(GWorldSceneView *self, const char *url_template)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->map_tile_template = url_template ? url_template : kDefaultMapTileTemplate;
    state->texture_pixels.clear();
    state->texture_width = 0;
    state->texture_height = 0;
    state->texture_dirty = true;
    state->loaded_texture_key.clear();
    state->mid_texture_pixels.clear();
    state->mid_texture_width = 0;
    state->mid_texture_height = 0;
    state->mid_texture_dirty = true;
    state->loaded_mid_texture_key.clear();
    state->loaded_base_texture_key.clear();
    state->base_texture_pixels.clear();
    state->base_texture_width = 0;
    state->base_texture_height = 0;
    state->base_texture_dirty = true;
    state->loaded_globe_texture_key.clear();
    state->globe_texture_pixels.clear();
    state->globe_texture_width = 0;
    state->globe_texture_height = 0;
    state->globe_texture_dirty = true;
    state->wanted_texture_key.clear();
    state->wanted_mid_texture_key.clear();
    state->wanted_base_texture_key.clear();
    state->wanted_globe_texture_key.clear();
    state->pending_texture_build_key.clear();
    state->pending_mid_texture_build_key.clear();
    state->pending_base_texture_build_key.clear();
    state->pending_globe_texture_build_key.clear();
    state->texture_atlas_range = AtlasRange();
    state->mid_texture_atlas_range = AtlasRange();
    state->base_texture_atlas_range = AtlasRange();
    state->globe_texture_atlas_range = AtlasRange();
    ++state->texture_source_revision;
    state->mesh_key.clear();
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_MAP_TILE_URL_TEMPLATE]);
  schedule_scene_requests(self, 20);
}

const char *
gworld_scene_view_get_map_tile_url_template(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);
  return get_state(self)->map_tile_template.c_str();
}

void
gworld_scene_view_set_cache_directory(GWorldSceneView *self, const char *cache_directory)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->cache_directory = cache_directory ? cache_directory : default_cache_directory();
    state->terrain_tiles.clear();
    clear_terrain_download_state_locked(state);
    state->texture_pixels.clear();
    state->texture_width = 0;
    state->texture_height = 0;
    state->texture_dirty = true;
    state->loaded_texture_key.clear();
    state->mid_texture_pixels.clear();
    state->mid_texture_width = 0;
    state->mid_texture_height = 0;
    state->mid_texture_dirty = true;
    state->loaded_mid_texture_key.clear();
    state->loaded_base_texture_key.clear();
    state->base_texture_pixels.clear();
    state->base_texture_width = 0;
    state->base_texture_height = 0;
    state->base_texture_dirty = true;
    state->loaded_globe_texture_key.clear();
    state->globe_texture_pixels.clear();
    state->globe_texture_width = 0;
    state->globe_texture_height = 0;
    state->globe_texture_dirty = true;
    state->wanted_texture_key.clear();
    state->wanted_mid_texture_key.clear();
    state->wanted_base_texture_key.clear();
    state->wanted_globe_texture_key.clear();
    state->pending_texture_build_key.clear();
    state->pending_mid_texture_build_key.clear();
    state->pending_base_texture_build_key.clear();
    state->pending_globe_texture_build_key.clear();
    state->texture_atlas_range = AtlasRange();
    state->mid_texture_atlas_range = AtlasRange();
    state->base_texture_atlas_range = AtlasRange();
    state->globe_texture_atlas_range = AtlasRange();
    ++state->texture_source_revision;
    state->mesh_key.clear();
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_CACHE_DIRECTORY]);
  schedule_scene_requests(self, 20);
}

const char *
gworld_scene_view_get_cache_directory(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);
  return get_state(self)->cache_directory.c_str();
}

void
gworld_scene_view_set_cache_enabled(GWorldSceneView *self, gboolean cache_enabled)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->cache_enabled = cache_enabled;
    clear_terrain_download_state_locked(state);
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_CACHE_ENABLED]);
  schedule_scene_requests(self, 20);
}

gboolean
gworld_scene_view_get_cache_enabled(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), FALSE);
  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->cache_enabled;
}

void
gworld_scene_view_set_sun_position(GWorldSceneView *self,
                                   double azimuth_deg,
                                   double elevation_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->sun_azimuth_deg = std::fmod(azimuth_deg, 360.0);
    if (state->sun_azimuth_deg < 0.0)
      state->sun_azimuth_deg += 360.0;
    state->sun_elevation_deg = std::clamp(elevation_deg, -90.0, 90.0);
    state->sun_uses_time_of_day = false;
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SUN_AZIMUTH_DEG]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SUN_ELEVATION_DEG]);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_get_sun_position(GWorldSceneView *self,
                                   double *azimuth_deg,
                                   double *elevation_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  const gworld_scene::SunPosition sun_position =
    gworld_scene::sun_position_from_direction(sun_enu_for_state(state, state->latitude));
  if (azimuth_deg)
    *azimuth_deg = sun_position.azimuth_deg;
  if (elevation_deg)
    *elevation_deg = sun_position.elevation_deg;
}

void
gworld_scene_view_set_sun_time_of_day(GWorldSceneView *self,
                                      double local_solar_hour)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->sun_time_of_day = std::fmod(local_solar_hour, 24.0);
    if (state->sun_time_of_day < 0.0)
      state->sun_time_of_day += 24.0;
    state->sun_uses_time_of_day = true;
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SUN_TIME_OF_DAY]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SUN_AZIMUTH_DEG]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SUN_ELEVATION_DEG]);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

double
gworld_scene_view_get_sun_time_of_day(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), 0.0);
  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->sun_time_of_day;
}

void
gworld_scene_view_set_fog_enabled(GWorldSceneView *self, gboolean fog_enabled)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->fog_enabled = fog_enabled;
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_FOG_ENABLED]);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

gboolean
gworld_scene_view_get_fog_enabled(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), FALSE);
  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->fog_enabled;
}

void
gworld_scene_view_set_fog_range(GWorldSceneView *self, double start_m, double end_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->fog_start_m = std::max(0.0, start_m);
    state->fog_end_m = std::max(state->fog_start_m + 1.0, end_m);
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_get_fog_range(GWorldSceneView *self, double *start_m, double *end_m)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  if (start_m)
    *start_m = state->fog_start_m;
  if (end_m)
    *end_m = state->fog_end_m;
}

void
gworld_scene_view_set_fog_color(GWorldSceneView *self,
                                double red,
                                double green,
                                double blue)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->fog_color = glm::vec3(static_cast<float>(std::clamp(red, 0.0, 1.0)),
                                 static_cast<float>(std::clamp(green, 0.0, 1.0)),
                                 static_cast<float>(std::clamp(blue, 0.0, 1.0)));
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_get_fog_color(GWorldSceneView *self,
                                double *red,
                                double *green,
                                double *blue)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  if (red)
    *red = state->fog_color.r;
  if (green)
    *green = state->fog_color.g;
  if (blue)
    *blue = state->fog_color.b;
}

void
gworld_scene_view_set_shadows_enabled(GWorldSceneView *self, gboolean shadows_enabled)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->shadows_enabled = shadows_enabled;
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SHADOWS_ENABLED]);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

gboolean
gworld_scene_view_get_shadows_enabled(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), FALSE);
  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->shadows_enabled;
}

void
gworld_scene_view_set_terrain_normal_smoothing(GWorldSceneView *self, double smoothing)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->terrain_normal_smoothing = std::clamp(smoothing, 0.0, 1.0);
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_TERRAIN_NORMAL_SMOOTHING]);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

double
gworld_scene_view_get_terrain_normal_smoothing(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), 0.0);
  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->terrain_normal_smoothing;
}

static void
mark_scene_nodes_changed(GWorldSceneView *self, GWorldSceneViewState *state)
{
  (void)self;
  ++state->scene_revision;
  state->mesh_key.clear();
}

static void
schedule_scene_nodes_changed(GWorldSceneView *self)
{
  schedule_scene_requests(self, 20);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static void
on_scene_node_changed(GWorldSceneNode *node, gpointer user_data)
{
  (void)node;
  auto *self = GWORLD_SCENE_VIEW(user_data);
  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    mark_scene_nodes_changed(self, state);
  }
  schedule_scene_nodes_changed(self);
}

static void
add_scene_node(GWorldSceneView *self, GWorldSceneNode *node)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));
  g_return_if_fail(GWORLD_IS_SCENE_NODE(node));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    g_signal_connect(node, "changed", G_CALLBACK(on_scene_node_changed), self);
    state->scene_nodes.emplace(gworld_scene_node_get_id(node), node);
    mark_scene_nodes_changed(self, state);
  }
  schedule_scene_nodes_changed(self);
}

GWorldSceneCubeNode *
gworld_scene_view_add_cube(GWorldSceneView *self,
                           double latitude,
                           double longitude,
                           double altitude_amsl,
                           double width_m,
                           double depth_m,
                           double height_m)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);

  GWorldSceneCubeNode *node = nullptr;
  {
    auto *state = get_state(self);
    std::lock_guard<std::mutex> lock(state->mutex);
    node = _gworld_scene_cube_node_new(state->next_node_id++,
                                       latitude,
                                       longitude,
                                       altitude_amsl,
                                       width_m,
                                       depth_m,
                                       height_m);
  }
  add_scene_node(self, GWORLD_SCENE_NODE(node));
  return node;
}

GWorldSceneSphereNode *
gworld_scene_view_add_sphere(GWorldSceneView *self,
                             double latitude,
                             double longitude,
                             double altitude_amsl,
                             double diameter_m)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);

  GWorldSceneSphereNode *node = nullptr;
  {
    auto *state = get_state(self);
    std::lock_guard<std::mutex> lock(state->mutex);
    node = _gworld_scene_sphere_node_new(state->next_node_id++,
                                         latitude,
                                         longitude,
                                         altitude_amsl,
                                         diameter_m);
  }
  add_scene_node(self, GWORLD_SCENE_NODE(node));
  return node;
}

GWorldSceneCylinderNode *
gworld_scene_view_add_cylinder(GWorldSceneView *self,
                               double latitude,
                               double longitude,
                               double altitude_amsl,
                               double diameter_m,
                               double height_m)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);

  GWorldSceneCylinderNode *node = nullptr;
  {
    auto *state = get_state(self);
    std::lock_guard<std::mutex> lock(state->mutex);
    node = _gworld_scene_cylinder_node_new(state->next_node_id++,
                                           latitude,
                                           longitude,
                                           altitude_amsl,
                                           diameter_m,
                                           height_m);
  }
  add_scene_node(self, GWORLD_SCENE_NODE(node));
  return node;
}

GWorldSceneModelNode *
gworld_scene_view_add_model(GWorldSceneView *self,
                            const char *model_path,
                            double latitude,
                            double longitude,
                            double altitude_amsl)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);
  g_return_val_if_fail(model_path != nullptr && model_path[0] != '\0', nullptr);

  GWorldSceneModelNode *node = nullptr;
  {
    auto *state = get_state(self);
    std::lock_guard<std::mutex> lock(state->mutex);
    node = _gworld_scene_model_node_new(state->next_node_id++,
                                        model_path,
                                        latitude,
                                        longitude,
                                        altitude_amsl);
  }
  add_scene_node(self, GWORLD_SCENE_NODE(node));
  return node;
}

gboolean
gworld_scene_view_remove_node(GWorldSceneView *self, GWorldSceneNode *node)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), FALSE);
  g_return_val_if_fail(GWORLD_IS_SCENE_NODE(node), FALSE);

  auto *state = get_state(self);
  std::unique_lock<std::mutex> lock(state->mutex);
  const GWorldSceneNodeId node_id = gworld_scene_node_get_id(node);
  auto iter = state->scene_nodes.find(node_id);
  if (iter == state->scene_nodes.end() || iter->second != node)
    return FALSE;
  g_signal_handlers_disconnect_by_func(iter->second,
                                       reinterpret_cast<gpointer>(on_scene_node_changed),
                                       self);
  g_object_unref(iter->second);
  state->scene_nodes.erase(iter);
  mark_scene_nodes_changed(self, state);
  lock.unlock();
  schedule_scene_nodes_changed(self);
  return TRUE;
}

void
gworld_scene_view_clear_nodes(GWorldSceneView *self)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  std::unique_lock<std::mutex> lock(state->mutex);
  if (state->scene_nodes.empty())
    return;
  for (auto &entry : state->scene_nodes) {
    g_signal_handlers_disconnect_by_func(entry.second,
                                         reinterpret_cast<gpointer>(on_scene_node_changed),
                                         self);
    g_object_unref(entry.second);
  }
  state->scene_nodes.clear();
  mark_scene_nodes_changed(self, state);
  lock.unlock();
  schedule_scene_nodes_changed(self);
}
