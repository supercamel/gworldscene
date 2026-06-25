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
#include <memory>
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
constexpr int kMaxTerrainTileLoadsPerUpdate = 3;
constexpr int kMaxTerrainDownloadsPerUpdate = 8;
constexpr int kMaxTextureDownloadsPerUpdate = 36;
constexpr int kMaxPendingTerrainDownloads = 12;
constexpr int kMaxPendingTextureDownloads = 48;
constexpr gint64 kTextureAtlasRefreshMinIntervalUs = 650 * 1000;
constexpr gint64 kTerrainRetryBaseDelayUs = 5 * G_USEC_PER_SEC;
constexpr gint64 kTerrainRetryMaxDelayUs = 120 * G_USEC_PER_SEC;
constexpr double kMinCameraPitchDeg = -89.0;
constexpr double kMaxCameraPitchDeg = 89.0;
constexpr double kGlobeRenderAltitudeM = 45000.0;
constexpr double kTerrainDisableAltitudeM = 180000.0;
constexpr int kShadowMapSize = 2048;
constexpr double kShadowMaxAltitudeM = kTerrainDisableAltitudeM;
constexpr int kMaxUltraAtlasTilesPerAxis = kMaxAtlasPixels / kAtlasTilePixels;
constexpr int kMaxUltraAtlasTiles = kMaxUltraAtlasTilesPerAxis * kMaxUltraAtlasTilesPerAxis;
constexpr int kGlobeLatSegments = 64;
constexpr int kGlobeLonSegments = 128;
constexpr int kVertexStride = 18;
constexpr int kSphereSegments = 24;
constexpr int kSphereRings = 12;
constexpr int kCylinderSegments = 32;
constexpr int kModelTextureTilePixels = 512;
constexpr int kModelTextureAtlasColumns = 4;
constexpr int kModelTextureAtlasRows = 4;
constexpr int kModelTextureAtlasPixels = kModelTextureTilePixels * kModelTextureAtlasColumns;
constexpr double kUltraTextureBubbleRadiusM = 1000.0;
constexpr double kMeshRecenterRadiusFraction = 0.18;
constexpr double kMeshRecenterMinDistanceM = 6000.0;
constexpr double kMeshRecenterMaxDistanceM = 30000.0;
constexpr double kDefaultPerfFrameThresholdMs = 24.0;
constexpr double kDefaultPerfSceneThresholdMs = 8.0;
constexpr double kDefaultPerfUploadThresholdMs = 3.0;
constexpr int kMaxSceneImageTexturePixels = 2048;
constexpr int kGroundOverlayCellMargin = 1;
constexpr double kGroundOverlayTerrainLoadMarginM = 250.0;
constexpr double kClickDragThresholdPx = 4.0;
constexpr double kClickCameraRotationThresholdDeg = 0.01;
constexpr double kSunAngularRadiusDeg = 0.266;
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

struct TextureCandidate {
  TileCoord tile;
  double priority = 0.0;
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

struct SceneImageTexture {
  GLuint texture = 0;
  int width = 0;
  int height = 0;
  bool failed = false;
  std::string error;
};

struct BillboardRenderItem {
  std::string image_path;
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_amsl = 0.0;
  double min_px = 24.0;
  double max_px = 96.0;
  double reference_size_px = 48.0;
  double reference_distance_m = 1000.0;
  double max_visible_distance_m = 0.0;
};

struct GroundOverlayRenderItem {
  std::string image_path;
  float opacity = 1.0f;
  std::vector<float> vertices;
  std::vector<unsigned int> indices;
};

struct GroundOverlayMapping {
  double latitude[4] = {0.0, 0.0, 0.0, 0.0};
  double longitude[4] = {0.0, 0.0, 0.0, 0.0};
  LatLonBounds bounds;
};

struct GroundOverlayVertex {
  double latitude = 0.0;
  double longitude = 0.0;
  double height_amsl = 0.0;
  double u = 0.0;
  double v = 0.0;
};

struct ScenePickRay {
  glm::dvec3 origin;
  glm::dvec3 direction;
  glm::dmat4 mvp = glm::dmat4(1.0);
  int viewport_width = 1;
  int viewport_height = 1;
};

struct ScenePickResult {
  bool hit = false;
  bool ground = false;
  GWorldSceneNode *node = nullptr;
  double distance_m = std::numeric_limits<double>::max();
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_amsl = 0.0;
};

struct TexCoords {
  float detail_u = 0.0f;
  float detail_v = 0.0f;
  float mid_u = 0.0f;
  float mid_v = 0.0f;
  float base_u = 0.0f;
  float base_v = 0.0f;
  float ultra_u = -1.0f;
  float ultra_v = -1.0f;
};

enum class TextureLayer {
  Ultra,
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
  double mesh_lod_altitude_amsl = 5200.0;
  double mesh_radius_m = 0.0;
  int mesh_sample_step = 0;
  GWorldSceneCameraMode camera_mode = GWORLD_SCENE_CAMERA_MODE_DEFAULT;
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

  std::unordered_map<std::string, std::shared_ptr<const TerrainTile>> terrain_tiles;
  std::unordered_set<std::string> wanted_terrain_keys;
  std::unordered_set<std::string> pending;
  std::unordered_map<std::string, GCancellable *> pending_cancellables;
  std::unordered_set<std::string> unavailable_terrain_keys;
  std::unordered_map<std::string, unsigned int> terrain_failure_counts;
  std::unordered_map<std::string, gint64> terrain_retry_after_us;

  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  bool mesh_dirty = true;
  std::string mesh_key;
  std::string pending_mesh_key;
  GCancellable *pending_mesh_cancellable = nullptr;
  bool mesh_build_refresh_needed = false;
  bool mesh_includes_terrain = true;
  guint64 terrain_revision = 0;
  guint64 scene_revision = 0;
  GWorldSceneNodeId next_node_id = 1;
  std::unordered_map<GWorldSceneNodeId, GWorldSceneNode *> scene_nodes;
  std::unordered_map<std::string, ModelMesh> model_meshes;
  std::unordered_map<std::string, int> model_texture_slots;
  std::unordered_map<std::string, SceneImageTexture> scene_image_textures;
  AtlasRange mesh_ultra_atlas_range;
  AtlasRange mesh_atlas_range;
  AtlasRange mesh_mid_atlas_range;
  AtlasRange mesh_base_atlas_range;
  double ultra_texture_lateral_radius_m = 0.0;

  std::vector<unsigned char> ultra_texture_pixels;
  int ultra_texture_width = 0;
  int ultra_texture_height = 0;
  bool ultra_texture_dirty = false;
  std::string loaded_ultra_texture_key;
  std::string wanted_ultra_texture_key;
  std::string pending_ultra_texture_build_key;
  AtlasRange ultra_texture_atlas_range;

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
  bool texture_refresh_pending = false;
  gint64 last_texture_refresh_us = 0;
  std::unordered_map<std::string, GCancellable *> pending_texture_build_cancellables;

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
  GLuint ultra_texture = 0;
  GLuint texture = 0;
  GLuint mid_texture = 0;
  GLuint base_texture = 0;
  GLuint globe_texture = 0;
  GLuint model_texture = 0;
  GLuint billboard_program = 0;
  GLuint billboard_vao = 0;
  GLuint billboard_vbo = 0;
  GLuint sky_program = 0;
  GLuint sky_vao = 0;
  GLuint sky_vbo = 0;
  GLuint sun_program = 0;
  GLuint sun_vao = 0;
  GLuint sun_vbo = 0;
  GLuint ground_overlay_vao = 0;
  GLuint ground_overlay_vbo = 0;
  GLuint ground_overlay_ebo = 0;
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
  guint click_button = 0;
  double click_press_x = 0.0;
  double click_press_y = 0.0;
  double click_press_heading_deg = 0.0;
  double click_press_pitch_deg = 0.0;
  bool click_dragged = false;

  bool move_forward = false;
  bool move_backward = false;
  bool move_left = false;
  bool move_right = false;
  bool move_fast = false;
  gint64 last_tick_time_us = 0;
  gint64 last_render_start_us = 0;
  guint64 perf_frame_counter = 0;
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

struct TerrainLoadJob {
  std::string path;
  std::string pending_key;
  std::string source = "cache";
  int terrain_lat = 0;
  int terrain_lon = 0;
};

struct TerrainLoadResult {
  TerrainTile tile;
  std::string path;
  std::string pending_key;
  std::string source = "cache";
  gint64 parse_duration_us = 0;
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
  gint64 build_duration_us = 0;
  std::vector<unsigned char> pixels;
  int width = 0;
  int height = 0;
  int loaded_count = 0;
  int missing_count = 0;
  int decode_failed_count = 0;
};

struct WorldMeshBuildJob {
  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_amsl = 0.0;
  double mesh_origin_latitude = 0.0;
  double mesh_origin_longitude = 0.0;
  bool current_mesh_empty = true;
  bool include_terrain = true;
  AtlasRange ultra_range;
  AtlasRange detail_range;
  AtlasRange mid_range;
  AtlasRange base_range;
  double ultra_lateral_radius_m = 0.0;
  guint64 terrain_revision = 0;
  guint64 scene_revision = 0;
  std::string key;
  std::vector<std::shared_ptr<const TerrainTile>> terrain_tiles;
};

struct WorldMeshBuildResult {
  std::string key;
  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  bool include_terrain = true;
  AtlasRange ultra_range;
  AtlasRange detail_range;
  AtlasRange mid_range;
  AtlasRange base_range;
  double ultra_lateral_radius_m = 0.0;
  double mesh_origin_latitude = 0.0;
  double mesh_origin_longitude = 0.0;
  double mesh_lod_altitude_amsl = 0.0;
  double mesh_radius_m = 0.0;
  int mesh_sample_step = 0;
  gint64 build_duration_us = 0;
};

struct PerfBufferUploadStats {
  bool uploaded = false;
  double duration_ms = 0.0;
  std::size_t vertex_bytes = 0;
  std::size_t index_bytes = 0;
  std::size_t indices = 0;
};

struct PerfTextureUploadStats {
  bool attempted = false;
  bool uploaded = false;
  bool deferred = false;
  const char *layer = "";
  double duration_ms = 0.0;
  int width = 0;
  int height = 0;
  std::size_t bytes = 0;
  GLenum error = GL_NO_ERROR;
};

const char *
texture_layer_name(TextureLayer layer)
{
  switch (layer) {
  case TextureLayer::Ultra:
    return "ultra";
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
  case TextureLayer::Ultra:
    return state->ultra_texture_pixels;
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
  case TextureLayer::Ultra:
    return state->ultra_texture_width;
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
  case TextureLayer::Ultra:
    return state->ultra_texture_height;
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
  case TextureLayer::Ultra:
    return state->ultra_texture_dirty;
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
  case TextureLayer::Ultra:
    return state->loaded_ultra_texture_key;
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
  case TextureLayer::Ultra:
    return state->wanted_ultra_texture_key;
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
  case TextureLayer::Ultra:
    return state->pending_ultra_texture_build_key;
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
  case TextureLayer::Ultra:
    return state->ultra_texture_atlas_range;
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

gint64
perf_now_us()
{
  return g_get_monotonic_time();
}

double
perf_elapsed_ms(gint64 start_us, gint64 end_us = perf_now_us())
{
  return static_cast<double>(end_us - start_us) / 1000.0;
}

bool
env_flag_enabled(const char *name)
{
  const char *value = g_getenv(name);
  if (value == nullptr || value[0] == '\0')
    return false;
  return g_ascii_strcasecmp(value, "0") != 0 &&
         g_ascii_strcasecmp(value, "false") != 0 &&
         g_ascii_strcasecmp(value, "no") != 0 &&
         g_ascii_strcasecmp(value, "off") != 0;
}

bool
perf_enabled()
{
  static const bool enabled = env_flag_enabled("GWORLD_SCENE_PERF");
  return enabled;
}

bool
perf_verbose()
{
  const char *value = g_getenv("GWORLD_SCENE_PERF");
  if (value == nullptr)
    return false;
  return g_ascii_strcasecmp(value, "2") == 0 ||
         g_ascii_strcasecmp(value, "all") == 0 ||
         g_ascii_strcasecmp(value, "verbose") == 0;
}

double
env_double_or_default(const char *name, double fallback)
{
  const char *value = g_getenv(name);
  if (value == nullptr || value[0] == '\0')
    return fallback;

  char *end = nullptr;
  const double parsed = g_ascii_strtod(value, &end);
  return end != value && std::isfinite(parsed) ? parsed : fallback;
}

double
perf_frame_threshold_ms()
{
  static const double threshold =
    env_double_or_default("GWORLD_SCENE_PERF_FRAME_MS", kDefaultPerfFrameThresholdMs);
  return threshold;
}

double
perf_scene_threshold_ms()
{
  static const double threshold =
    env_double_or_default("GWORLD_SCENE_PERF_SCENE_MS", kDefaultPerfSceneThresholdMs);
  return threshold;
}

double
perf_upload_threshold_ms()
{
  static const double threshold =
    env_double_or_default("GWORLD_SCENE_PERF_UPLOAD_MS", kDefaultPerfUploadThresholdMs);
  return threshold;
}

double
bytes_to_mib(std::size_t bytes)
{
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
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
ultra_texture_zoom_for_altitude(double altitude_amsl)
{
  return std::clamp(detail_texture_zoom_for_altitude(altitude_amsl) + 1, 0, 18);
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
ultra_texture_lateral_radius_for_agl(double altitude_agl_m)
{
  const double altitude_agl = std::clamp(altitude_agl_m, 0.0, kUltraTextureBubbleRadiusM);
  if (altitude_agl >= kUltraTextureBubbleRadiusM)
    return 0.0;
  return std::sqrt(kUltraTextureBubbleRadiusM * kUltraTextureBubbleRadiusM -
                   altitude_agl * altitude_agl);
}

double
ultra_texture_atlas_radius(double latitude, int zoom, double lateral_radius_m)
{
  if (lateral_radius_m <= 0.0)
    return 0.0;

  const double latitude_scale = std::max(0.05, std::cos(deg_to_rad(latitude)));
  const double meters_per_tile =
    360.0 * kEarthMetersPerDegree * latitude_scale / static_cast<double>(1 << zoom);
  const double max_tile_span = std::max(1.0, static_cast<double>(kMaxUltraAtlasTilesPerAxis - 1));
  const double max_radius_m = meters_per_tile * max_tile_span * 0.5;
  return std::min(lateral_radius_m, max_radius_m);
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

std::string
texture_download_key(TileCoord tile)
{
  return "texture:" + std::to_string(tile.z) + "/" +
         std::to_string(tile.x) + "/" + std::to_string(tile.y);
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

bool
atlas_range_fits(const AtlasRange &range, int max_tiles)
{
  const int width = range.width_tiles();
  const int height = range.height_tiles();
  return width * height <= max_tiles &&
         width * kAtlasTilePixels <= kMaxAtlasPixels &&
         height * kAtlasTilePixels <= kMaxAtlasPixels;
}

AtlasRange
select_atlas_range(const LatLonBounds &bounds, int desired_zoom, int max_tiles = kMaxAtlasTiles)
{
  AtlasRange fallback;
  for (int zoom = std::clamp(desired_zoom, 0, 18); zoom >= 0; --zoom) {
    AtlasRange range = lat_lon_bounds_to_tile_range(bounds, zoom);
    fallback = range;
    if (atlas_range_fits(range, max_tiles)) {
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

LatLonBounds
expand_bounds_m(const LatLonBounds &bounds, double margin_m)
{
  const double center_latitude = (bounds.min_lat + bounds.max_lat) * 0.5;
  const double lat_delta = margin_m / kEarthMetersPerDegree;
  const double lon_scale = std::max(0.05, std::cos(deg_to_rad(center_latitude)));
  const double lon_delta = margin_m / (kEarthMetersPerDegree * lon_scale);

  LatLonBounds expanded;
  expanded.min_lat = std::clamp(bounds.min_lat - lat_delta, -90.0, 90.0);
  expanded.max_lat = std::clamp(bounds.max_lat + lat_delta, -90.0, 90.0);
  expanded.min_lon = std::clamp(bounds.min_lon - lon_delta, -180.0, 180.0);
  expanded.max_lon = std::clamp(bounds.max_lon + lon_delta, -180.0, 180.0);
  return expanded;
}

bool
bounds_intersect(const LatLonBounds &a, const LatLonBounds &b)
{
  return a.min_lat <= b.max_lat &&
         a.max_lat >= b.min_lat &&
         a.min_lon <= b.max_lon &&
         a.max_lon >= b.min_lon;
}

LatLonBounds
ground_overlay_bounds_from_corners(const double latitude[4], const double longitude[4])
{
  LatLonBounds bounds;
  bounds.min_lat = latitude[0];
  bounds.max_lat = latitude[0];
  bounds.min_lon = longitude[0];
  bounds.max_lon = longitude[0];
  for (int i = 1; i < 4; ++i) {
    bounds.min_lat = std::min(bounds.min_lat, latitude[i]);
    bounds.max_lat = std::max(bounds.max_lat, latitude[i]);
    bounds.min_lon = std::min(bounds.min_lon, longitude[i]);
    bounds.max_lon = std::max(bounds.max_lon, longitude[i]);
  }
  return bounds;
}

AtlasRange
select_ultra_atlas_range(double latitude,
                         double longitude,
                         int zoom,
                         double &lateral_radius_m)
{
  for (int attempt = 0; attempt < 6 && lateral_radius_m > 0.0; ++attempt) {
    const LatLonBounds bounds = bounds_around_camera(latitude, longitude, lateral_radius_m);
    AtlasRange range = lat_lon_bounds_to_tile_range(bounds, zoom);
    if (atlas_range_fits(range, kMaxUltraAtlasTiles))
      return range;

    lateral_radius_m *= 0.88;
  }

  lateral_radius_m = 0.0;
  return AtlasRange();
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

int
quantized_key_value(double value, double quantum)
{
  return static_cast<int>(std::llround(value / std::max(quantum, 1.0)));
}

double
mesh_recenter_distance_for_radius(double radius_m)
{
  return std::clamp(radius_m * kMeshRecenterRadiusFraction,
                    kMeshRecenterMinDistanceM,
                    kMeshRecenterMaxDistanceM);
}

double
distance_from_mesh_origin_m(const GWorldSceneViewState *state)
{
  const glm::dvec3 offset = geodetic_to_enu(state->latitude,
                                            state->longitude,
                                            0.0,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0);
  return std::hypot(offset.x, offset.z);
}

std::string
world_mesh_key(double mesh_origin_latitude,
               double mesh_origin_longitude,
               double altitude_amsl,
               double radius_m,
               int sample_step,
               std::size_t terrain_tile_count,
               guint64 terrain_revision,
               guint64 scene_revision,
               bool include_terrain)
{
  const int altitude_bucket =
    quantized_key_value(altitude_amsl, std::clamp(altitude_amsl * 0.08, 50.0, 2500.0));
  const int radius_bucket = quantized_key_value(radius_m, 500.0);

  return std::to_string(static_cast<int>(std::llround(mesh_origin_latitude * 100000.0))) +
         ":" + std::to_string(static_cast<int>(std::llround(mesh_origin_longitude * 100000.0))) +
         ":" + std::to_string(altitude_bucket) +
         ":" + std::to_string(radius_bucket) +
         ":" + std::to_string(sample_step) +
         ":" + std::to_string(terrain_tile_count) +
         ":" + std::to_string(terrain_revision) +
         ":" + std::to_string(scene_revision) +
         ":" + (include_terrain ? "terrain" : "scene-only");
}

std::string
world_mesh_key_for_state_locked(GWorldSceneViewState *state,
                                bool include_terrain,
                                double &mesh_origin_latitude,
                                double &mesh_origin_longitude)
{
  const double radius_m = terrain_build_radius_for_altitude(state->altitude_amsl);
  const int sample_step = terrain_step_for_altitude(state->altitude_amsl);
  const bool recenter_mesh =
    state->vertices.empty() ||
    distance_from_mesh_origin_m(state) > mesh_recenter_distance_for_radius(radius_m);

  mesh_origin_latitude = recenter_mesh ? state->latitude : state->mesh_origin_latitude;
  mesh_origin_longitude = recenter_mesh ? state->longitude : state->mesh_origin_longitude;
  return world_mesh_key(mesh_origin_latitude,
                        mesh_origin_longitude,
                        state->altitude_amsl,
                        radius_m,
                        sample_step,
                        state->terrain_tiles.size(),
                        state->terrain_revision,
                        state->scene_revision,
                        include_terrain);
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
normalize_degrees(double degrees)
{
  double normalized = std::fmod(degrees, 360.0);
  if (normalized < 0.0)
    normalized += 360.0;
  return normalized;
}

double
rad_to_deg(double radians)
{
  return radians * 180.0 / kPi;
}

gworld_scene::CameraPose
camera_pose_for_mode(GWorldSceneCameraMode camera_mode,
                     double latitude,
                     double longitude,
                     double altitude_amsl,
                     double heading_deg,
                     double pitch_deg,
                     double origin_latitude,
                     double origin_longitude)
{
  const double pitch = std::clamp(pitch_deg, kMinCameraPitchDeg, kMaxCameraPitchDeg);
  if (camera_mode == GWORLD_SCENE_CAMERA_MODE_FREE) {
    return gworld_scene::local_camera_pose(latitude,
                                           longitude,
                                           altitude_amsl,
                                           heading_deg,
                                           pitch,
                                           origin_latitude,
                                           origin_longitude);
  }

  return gworld_scene::blended_camera_pose(latitude,
                                           longitude,
                                           altitude_amsl,
                                           heading_deg,
                                           pitch,
                                           origin_latitude,
                                           origin_longitude);
}

void
set_camera_position_locked(GWorldSceneViewState *state,
                           double latitude,
                           double longitude,
                           double altitude_amsl)
{
  state->latitude = std::clamp(latitude, -90.0, 90.0);
  state->longitude = std::clamp(longitude, -180.0, 180.0);
  state->altitude_amsl = std::clamp(altitude_amsl, 25.0, 10000000.0);
}

void
set_camera_orientation_locked(GWorldSceneViewState *state, double heading_deg, double pitch_deg)
{
  state->heading_deg = normalize_degrees(heading_deg);
  state->pitch_deg = std::clamp(pitch_deg, kMinCameraPitchDeg, kMaxCameraPitchDeg);
}

void
look_at_scene_position_locked(GWorldSceneViewState *state, const glm::dvec3 &target)
{
  const gworld_scene::LocalFrame frame =
    gworld_scene::local_frame_at(state->latitude,
                                 state->longitude,
                                 state->mesh_origin_latitude,
                                 state->mesh_origin_longitude);
  const glm::dvec3 eye =
    geodetic_to_enu(state->latitude,
                    state->longitude,
                    state->altitude_amsl,
                    state->mesh_origin_latitude,
                    state->mesh_origin_longitude,
                    0.0);
  const glm::dvec3 direction = safe_normalize(target - eye, frame.north);
  const double east = glm::dot(direction, frame.east);
  const double north = glm::dot(direction, frame.north);
  const double up = std::clamp(glm::dot(direction, frame.up), -1.0, 1.0);
  const double heading = rad_to_deg(std::atan2(east, north));
  const double pitch = rad_to_deg(std::asin(up));
  set_camera_orientation_locked(state, heading, pitch);
}

void
look_at_location_locked(GWorldSceneViewState *state,
                        double latitude,
                        double longitude,
                        double altitude_amsl)
{
  const glm::dvec3 target =
    geodetic_to_enu(latitude,
                    longitude,
                    altitude_amsl,
                    state->mesh_origin_latitude,
                    state->mesh_origin_longitude,
                    0.0);
  look_at_scene_position_locked(state, target);
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

bool
terrain_height_at_lat_lon_locked(GWorldSceneViewState *state,
                                 double latitude,
                                 double longitude,
                                 double &height_amsl)
{
  const int lat_floor = static_cast<int>(std::floor(latitude));
  const int lon_floor = static_cast<int>(std::floor(longitude));
  auto iter = state->terrain_tiles.find(terrain_key(lat_floor, lon_floor));
  if (iter == state->terrain_tiles.end())
    return false;

  const TerrainTile &tile = *iter->second;
  if (tile.dimension <= 1 || tile.heights.empty())
    return false;

  const double u = std::clamp(longitude - static_cast<double>(lon_floor), 0.0, 1.0);
  const double v = std::clamp(static_cast<double>(lat_floor + 1) - latitude, 0.0, 1.0);
  const double sx = u * static_cast<double>(tile.dimension - 1);
  const double sy = v * static_cast<double>(tile.dimension - 1);
  const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, tile.dimension - 1);
  const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, tile.dimension - 1);
  const int x1 = std::min(tile.dimension - 1, x0 + 1);
  const int y1 = std::min(tile.dimension - 1, y0 + 1);
  const double tx = sx - static_cast<double>(x0);
  const double ty = sy - static_cast<double>(y0);

  const double h00 = static_cast<double>(get_safe_height(tile, x0, y0));
  const double h10 = static_cast<double>(get_safe_height(tile, x1, y0));
  const double h01 = static_cast<double>(get_safe_height(tile, x0, y1));
  const double h11 = static_cast<double>(get_safe_height(tile, x1, y1));
  const double h0 = h00 * (1.0 - tx) + h10 * tx;
  const double h1 = h01 * (1.0 - tx) + h11 * tx;
  height_amsl = h0 * (1.0 - ty) + h1 * ty;
  return true;
}

void
collect_billboards_locked(GWorldSceneViewState *state, std::vector<BillboardRenderItem> &billboards)
{
  billboards.clear();
  billboards.reserve(state->scene_nodes.size());

  for (const auto &entry : state->scene_nodes) {
    GWorldSceneNode *scene_node = entry.second;
    if (!GWORLD_IS_SCENE_BILLBOARD_NODE(scene_node))
      continue;

    auto *billboard = GWORLD_SCENE_BILLBOARD_NODE(scene_node);
    const char *image_path = gworld_scene_billboard_node_get_image_path(billboard);
    if (image_path == nullptr)
      continue;

    BillboardRenderItem item;
    item.image_path = image_path;
    gworld_scene_node_get_position(scene_node,
                                   &item.latitude,
                                   &item.longitude,
                                   &item.altitude_amsl);
    if (gworld_scene_billboard_node_get_altitude_mode(billboard) == GWORLD_SCENE_ALTITUDE_AGL) {
      double terrain_height = 0.0;
      if (terrain_height_at_lat_lon_locked(state, item.latitude, item.longitude, terrain_height))
        item.altitude_amsl += terrain_height;
    }
    gworld_scene_billboard_node_get_size_limits(billboard, &item.min_px, &item.max_px);
    gworld_scene_billboard_node_get_reference_size(billboard,
                                                   &item.reference_size_px,
                                                   &item.reference_distance_m);
    item.max_visible_distance_m = gworld_scene_billboard_node_get_max_visible_distance(billboard);
    billboards.push_back(std::move(item));
  }
}

void
sample_to_lat_lon(const TerrainTile &tile, int sample_x, int sample_y, double &lat, double &lon)
{
  const double u = static_cast<double>(sample_x) / static_cast<double>(tile.dimension - 1);
  const double v = static_cast<double>(sample_y) / static_cast<double>(tile.dimension - 1);
  lat = static_cast<double>(tile.lat + 1) * (1.0 - v) + static_cast<double>(tile.lat) * v;
  lon = static_cast<double>(tile.lon) * (1.0 - u) + static_cast<double>(tile.lon + 1) * u;
}

unsigned int
append_ground_overlay_vertex(std::vector<float> &vertices,
                             const glm::dvec3 &position,
                             double u,
                             double v)
{
  const auto index = static_cast<unsigned int>(vertices.size() / 5);
  vertices.push_back(static_cast<float>(position.x));
  vertices.push_back(static_cast<float>(position.y));
  vertices.push_back(static_cast<float>(position.z));
  vertices.push_back(static_cast<float>(u));
  vertices.push_back(static_cast<float>(v));
  return index;
}

GroundOverlayMapping
ground_overlay_mapping_from_corners(const double latitude[4], const double longitude[4])
{
  GroundOverlayMapping mapping;
  for (int i = 0; i < 4; ++i) {
    mapping.latitude[i] = latitude[i];
    mapping.longitude[i] = longitude[i];
  }
  mapping.bounds = ground_overlay_bounds_from_corners(latitude, longitude);
  return mapping;
}

void
collect_ground_overlay_terrain_bounds_locked(GWorldSceneViewState *state,
                                             std::vector<LatLonBounds> &bounds)
{
  bounds.clear();
  for (const auto &entry : state->scene_nodes) {
    GWorldSceneNode *scene_node = entry.second;
    if (!GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(scene_node))
      continue;

    auto *overlay = GWORLD_SCENE_GROUND_OVERLAY_NODE(scene_node);
    const char *image_path = gworld_scene_ground_overlay_node_get_image_path(overlay);
    const double opacity = gworld_scene_ground_overlay_node_get_opacity(overlay);
    if (image_path == nullptr || opacity <= 0.0)
      continue;

    double lat[4] = {0.0, 0.0, 0.0, 0.0};
    double lon[4] = {0.0, 0.0, 0.0, 0.0};
    gworld_scene_ground_overlay_node_get_corners(overlay,
                                                 &lat[0],
                                                 &lon[0],
                                                 &lat[1],
                                                 &lon[1],
                                                 &lat[2],
                                                 &lon[2],
                                                 &lat[3],
                                                 &lon[3]);
    bounds.push_back(expand_bounds_m(ground_overlay_bounds_from_corners(lat, lon),
                                     kGroundOverlayTerrainLoadMarginM));
  }
}

bool
ground_overlay_uv_from_lat_lon(const GroundOverlayMapping &mapping,
                               double latitude,
                               double longitude,
                               double &u,
                               double &v)
{
  const double lat_a = mapping.latitude[0];
  const double lat_b = mapping.latitude[1] - mapping.latitude[0];
  const double lat_c = mapping.latitude[3] - mapping.latitude[0];
  const double lat_d = mapping.latitude[0] - mapping.latitude[1] - mapping.latitude[3] + mapping.latitude[2];
  const double lon_a = mapping.longitude[0];
  const double lon_b = mapping.longitude[1] - mapping.longitude[0];
  const double lon_c = mapping.longitude[3] - mapping.longitude[0];
  const double lon_d = mapping.longitude[0] - mapping.longitude[1] - mapping.longitude[3] + mapping.longitude[2];

  const double lon_span = mapping.bounds.max_lon - mapping.bounds.min_lon;
  const double lat_span = mapping.bounds.max_lat - mapping.bounds.min_lat;
  if (std::abs(lon_span) <= 1e-12 && std::abs(lat_span) <= 1e-12)
    return false;

  u = std::abs(lon_span) > 1e-12
        ? (longitude - mapping.bounds.min_lon) / lon_span
        : 0.5;
  v = std::abs(lat_span) > 1e-12
        ? (mapping.bounds.max_lat - latitude) / lat_span
        : 0.5;
  u = std::clamp(u, -2.0, 3.0);
  v = std::clamp(v, -2.0, 3.0);

  for (int i = 0; i < 10; ++i) {
    const double mapped_lat = lat_a + lat_b * u + lat_c * v + lat_d * u * v;
    const double mapped_lon = lon_a + lon_b * u + lon_c * v + lon_d * u * v;
    const double f_lat = mapped_lat - latitude;
    const double f_lon = mapped_lon - longitude;
    if (std::abs(f_lat) + std::abs(f_lon) < 1e-12)
      break;

    const double dlat_du = lat_b + lat_d * v;
    const double dlat_dv = lat_c + lat_d * u;
    const double dlon_du = lon_b + lon_d * v;
    const double dlon_dv = lon_c + lon_d * u;
    const double det = dlat_du * dlon_dv - dlat_dv * dlon_du;
    if (std::abs(det) < 1e-18)
      break;

    const double delta_u = (-f_lat * dlon_dv + dlat_dv * f_lon) / det;
    const double delta_v = (dlon_du * f_lat - dlat_du * f_lon) / det;
    if (!std::isfinite(delta_u) || !std::isfinite(delta_v))
      return false;

    u = std::clamp(u + delta_u, -2.0, 3.0);
    v = std::clamp(v + delta_v, -2.0, 3.0);
  }

  return std::isfinite(u) && std::isfinite(v);
}

bool
ground_overlay_hgt_vertex(const TerrainTile &tile,
                          const GroundOverlayMapping &mapping,
                          int sample_x,
                          int sample_y,
                          GroundOverlayVertex &vertex)
{
  double latitude = 0.0;
  double longitude = 0.0;
  sample_to_lat_lon(tile, sample_x, sample_y, latitude, longitude);

  double u = 0.0;
  double v = 0.0;
  if (!ground_overlay_uv_from_lat_lon(mapping, latitude, longitude, u, v))
    return false;

  vertex.latitude = latitude;
  vertex.longitude = longitude;
  vertex.height_amsl = static_cast<double>(get_safe_height(tile, sample_x, sample_y));
  vertex.u = u;
  vertex.v = v;
  return true;
}

GroundOverlayVertex
interpolate_ground_overlay_vertex(const GroundOverlayVertex &a,
                                  const GroundOverlayVertex &b,
                                  double t)
{
  t = std::clamp(t, 0.0, 1.0);
  GroundOverlayVertex out;
  out.latitude = a.latitude + (b.latitude - a.latitude) * t;
  out.longitude = a.longitude + (b.longitude - a.longitude) * t;
  out.height_amsl = a.height_amsl + (b.height_amsl - a.height_amsl) * t;
  out.u = a.u + (b.u - a.u) * t;
  out.v = a.v + (b.v - a.v) * t;
  return out;
}

double
ground_overlay_clip_value(const GroundOverlayVertex &vertex, int boundary)
{
  return boundary < 2 ? vertex.u : vertex.v;
}

double
ground_overlay_clip_target(int boundary)
{
  return (boundary == 0 || boundary == 2) ? 0.0 : 1.0;
}

bool
ground_overlay_clip_inside(const GroundOverlayVertex &vertex, int boundary)
{
  constexpr double epsilon = 1e-8;
  const double value = ground_overlay_clip_value(vertex, boundary);
  return (boundary == 0 || boundary == 2) ? value >= -epsilon : value <= 1.0 + epsilon;
}

GroundOverlayVertex
ground_overlay_clip_intersection(const GroundOverlayVertex &a,
                                 const GroundOverlayVertex &b,
                                 int boundary)
{
  const double av = ground_overlay_clip_value(a, boundary);
  const double bv = ground_overlay_clip_value(b, boundary);
  const double denom = bv - av;
  if (std::abs(denom) < 1e-12)
    return a;
  const double t = (ground_overlay_clip_target(boundary) - av) / denom;
  return interpolate_ground_overlay_vertex(a, b, t);
}

std::vector<GroundOverlayVertex>
clip_ground_overlay_polygon(std::vector<GroundOverlayVertex> polygon)
{
  std::vector<GroundOverlayVertex> output;
  output.reserve(8);

  for (int boundary = 0; boundary < 4; ++boundary) {
    if (polygon.empty())
      break;

    output.clear();
    GroundOverlayVertex previous = polygon.back();
    bool previous_inside = ground_overlay_clip_inside(previous, boundary);

    for (const GroundOverlayVertex &current : polygon) {
      const bool current_inside = ground_overlay_clip_inside(current, boundary);
      if (current_inside) {
        if (!previous_inside)
          output.push_back(ground_overlay_clip_intersection(previous, current, boundary));
        output.push_back(current);
      } else if (previous_inside) {
        output.push_back(ground_overlay_clip_intersection(previous, current, boundary));
      }
      previous = current;
      previous_inside = current_inside;
    }

    polygon = output;
  }

  return polygon;
}

glm::dvec3
ground_overlay_vertex_position(const GroundOverlayVertex &vertex,
                               double mesh_origin_latitude,
                               double mesh_origin_longitude,
                               double altitude_offset)
{
  return geodetic_to_enu(vertex.latitude,
                         vertex.longitude,
                         vertex.height_amsl + altitude_offset,
                         mesh_origin_latitude,
                         mesh_origin_longitude,
                         0.0);
}

void
append_clipped_ground_overlay_triangle(GroundOverlayRenderItem &item,
                                       const GroundOverlayVertex &a,
                                       const GroundOverlayVertex &b,
                                       const GroundOverlayVertex &c,
                                       double mesh_origin_latitude,
                                       double mesh_origin_longitude,
                                       double altitude_offset)
{
  std::vector<GroundOverlayVertex> polygon = clip_ground_overlay_polygon({a, b, c});
  if (polygon.size() < 3)
    return;

  for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
    const glm::dvec3 p0 = ground_overlay_vertex_position(polygon[0],
                                                         mesh_origin_latitude,
                                                         mesh_origin_longitude,
                                                         altitude_offset);
    const glm::dvec3 p1 = ground_overlay_vertex_position(polygon[i],
                                                         mesh_origin_latitude,
                                                         mesh_origin_longitude,
                                                         altitude_offset);
    const glm::dvec3 p2 = ground_overlay_vertex_position(polygon[i + 1],
                                                         mesh_origin_latitude,
                                                         mesh_origin_longitude,
                                                         altitude_offset);
    if (glm::length(glm::cross(p1 - p0, p2 - p0)) < 0.001)
      continue;

    const unsigned int i0 = append_ground_overlay_vertex(item.vertices, p0, polygon[0].u, polygon[0].v);
    const unsigned int i1 = append_ground_overlay_vertex(item.vertices, p1, polygon[i].u, polygon[i].v);
    const unsigned int i2 = append_ground_overlay_vertex(item.vertices, p2, polygon[i + 1].u, polygon[i + 1].v);
    item.indices.push_back(i0);
    item.indices.push_back(i1);
    item.indices.push_back(i2);
  }
}

bool
ground_overlay_tile_cell_range(const TerrainTile &tile,
                               const LatLonBounds &bounds,
                               int &x_min,
                               int &x_max,
                               int &y_min,
                               int &y_max)
{
  if (tile.dimension <= 1)
    return false;

  const double sample_scale = static_cast<double>(tile.dimension - 1);
  const double raw_x_min = (bounds.min_lon - static_cast<double>(tile.lon)) * sample_scale;
  const double raw_x_max = (bounds.max_lon - static_cast<double>(tile.lon)) * sample_scale;
  const double raw_y_min = (static_cast<double>(tile.lat + 1) - bounds.max_lat) * sample_scale;
  const double raw_y_max = (static_cast<double>(tile.lat + 1) - bounds.min_lat) * sample_scale;

  x_min = std::clamp(static_cast<int>(std::floor(std::min(raw_x_min, raw_x_max))) -
                       kGroundOverlayCellMargin,
                     0,
                     tile.dimension - 2);
  x_max = std::clamp(static_cast<int>(std::ceil(std::max(raw_x_min, raw_x_max))) +
                       kGroundOverlayCellMargin,
                     1,
                     tile.dimension - 1);
  y_min = std::clamp(static_cast<int>(std::floor(std::min(raw_y_min, raw_y_max))) -
                       kGroundOverlayCellMargin,
                     0,
                     tile.dimension - 2);
  y_max = std::clamp(static_cast<int>(std::ceil(std::max(raw_y_min, raw_y_max))) +
                       kGroundOverlayCellMargin,
                     1,
                     tile.dimension - 1);

  return x_min < x_max && y_min < y_max;
}

int
aligned_sample_start(int sample_min, int sample_step)
{
  sample_step = std::max(1, sample_step);
  return std::max(0, (sample_min / sample_step) * sample_step);
}

void
append_ground_overlay_tile_ring(const TerrainTile &tile,
                                const GroundOverlayMapping &mapping,
                                GroundOverlayRenderItem &item,
                                double mesh_origin_latitude,
                                double mesh_origin_longitude,
                                double altitude_offset,
                                double inner_radius_m,
                                double outer_radius_m,
                                int sample_step)
{
  if (tile.dimension <= 1 || tile.heights.empty())
    return;

  LatLonBounds tile_bounds;
  tile_bounds.min_lat = static_cast<double>(tile.lat);
  tile_bounds.max_lat = static_cast<double>(tile.lat + 1);
  tile_bounds.min_lon = static_cast<double>(tile.lon);
  tile_bounds.max_lon = static_cast<double>(tile.lon + 1);
  if (!bounds_intersect(tile_bounds, mapping.bounds))
    return;

  const double tile_center_lat = static_cast<double>(tile.lat) + 0.5;
  const double tile_center_lon = static_cast<double>(tile.lon) + 0.5;
  const glm::dvec3 tile_center = geodetic_to_enu(tile_center_lat,
                                                 tile_center_lon,
                                                 0.0,
                                                 mesh_origin_latitude,
                                                 mesh_origin_longitude,
                                                 0.0);
  const double tile_half_diag = 0.5 * std::sqrt(std::pow(kEarthMetersPerDegree, 2.0) +
                                               std::pow(kEarthMetersPerDegree *
                                                          std::cos(deg_to_rad(tile_center_lat)),
                                                        2.0));
  const double tile_distance = std::hypot(tile_center.x, tile_center.z);
  if (tile_distance > outer_radius_m + tile_half_diag ||
      tile_distance + tile_half_diag < inner_radius_m)
    return;

  sample_step = std::clamp(sample_step, 1, tile.dimension - 1);

  int x_min = 0;
  int x_max = 0;
  int y_min = 0;
  int y_max = 0;
  if (!ground_overlay_tile_cell_range(tile, mapping.bounds, x_min, x_max, y_min, y_max))
    return;

  const int x_start = aligned_sample_start(x_min, sample_step);
  const int y_start = aligned_sample_start(y_min, sample_step);

  for (int sy = y_start; sy < y_max; sy += sample_step) {
    const int sy1 = std::min(tile.dimension - 1, sy + sample_step);
    if (sy1 <= sy)
      continue;

    for (int sx = x_start; sx < x_max; sx += sample_step) {
      const int sx1 = std::min(tile.dimension - 1, sx + sample_step);
      if (sx1 <= sx)
        continue;

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

      LatLonBounds cell_bounds;
      cell_bounds.min_lat = std::min(std::min(lat00, lat10), std::min(lat01, lat11));
      cell_bounds.max_lat = std::max(std::max(lat00, lat10), std::max(lat01, lat11));
      cell_bounds.min_lon = std::min(std::min(lon00, lon10), std::min(lon01, lon11));
      cell_bounds.max_lon = std::max(std::max(lon00, lon10), std::max(lon01, lon11));
      if (!bounds_intersect(cell_bounds, mapping.bounds))
        continue;

      const double center_lat = (lat00 + lat10 + lat01 + lat11) * 0.25;
      const double center_lon = (lon00 + lon10 + lon01 + lon11) * 0.25;
      const glm::dvec3 center = geodetic_to_enu(center_lat,
                                               center_lon,
                                               0.0,
                                               mesh_origin_latitude,
                                               mesh_origin_longitude,
                                               0.0);
      const double center_distance = std::hypot(center.x, center.z);
      if (center_distance <= inner_radius_m || center_distance > outer_radius_m + 2000.0)
        continue;

      GroundOverlayVertex v00;
      GroundOverlayVertex v10;
      GroundOverlayVertex v01;
      GroundOverlayVertex v11;
      if (!ground_overlay_hgt_vertex(tile, mapping, sx, sy, v00) ||
          !ground_overlay_hgt_vertex(tile, mapping, sx1, sy, v10) ||
          !ground_overlay_hgt_vertex(tile, mapping, sx, sy1, v01) ||
          !ground_overlay_hgt_vertex(tile, mapping, sx1, sy1, v11))
        continue;

      append_clipped_ground_overlay_triangle(item,
                                             v00,
                                             v10,
                                             v01,
                                             mesh_origin_latitude,
                                             mesh_origin_longitude,
                                             altitude_offset);
      append_clipped_ground_overlay_triangle(item,
                                             v10,
                                             v11,
                                             v01,
                                             mesh_origin_latitude,
                                             mesh_origin_longitude,
                                             altitude_offset);
    }
  }
}

void
collect_ground_overlays_locked(GWorldSceneViewState *state,
                               std::vector<GroundOverlayRenderItem> &ground_overlays,
                               double mesh_origin_latitude,
                               double mesh_origin_longitude)
{
  ground_overlays.clear();
  ground_overlays.reserve(state->scene_nodes.size());

  const double lod_altitude =
    state->mesh_radius_m > 0.0 ? state->mesh_lod_altitude_amsl : state->altitude_amsl;
  const double radius_m =
    state->mesh_radius_m > 0.0 ? state->mesh_radius_m : terrain_build_radius_for_altitude(lod_altitude);
  const int base_sample_step =
    state->mesh_sample_step > 0 ? state->mesh_sample_step : terrain_step_for_altitude(lod_altitude);
  const double near_radius = terrain_near_radius_for_altitude(lod_altitude, radius_m);
  const double mid_radius = terrain_mid_radius_for_altitude(lod_altitude, radius_m);
  const double far_radius = terrain_far_radius_for_altitude(lod_altitude, radius_m);

  for (const auto &entry : state->scene_nodes) {
    GWorldSceneNode *scene_node = entry.second;
    if (!GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(scene_node))
      continue;

    auto *overlay = GWORLD_SCENE_GROUND_OVERLAY_NODE(scene_node);
    const char *image_path = gworld_scene_ground_overlay_node_get_image_path(overlay);
    const double opacity = gworld_scene_ground_overlay_node_get_opacity(overlay);
    if (image_path == nullptr || opacity <= 0.0)
      continue;

    double lat[4] = {0.0, 0.0, 0.0, 0.0};
    double lon[4] = {0.0, 0.0, 0.0, 0.0};
    gworld_scene_ground_overlay_node_get_corners(overlay,
                                                 &lat[0],
                                                 &lon[0],
                                                 &lat[1],
                                                 &lon[1],
                                                 &lat[2],
                                                 &lon[2],
                                                 &lat[3],
                                                 &lon[3]);

    const GroundOverlayMapping mapping = ground_overlay_mapping_from_corners(lat, lon);
    const double altitude_offset =
      gworld_scene_ground_overlay_node_get_altitude_offset(overlay);

    GroundOverlayRenderItem item;
    item.image_path = image_path;
    item.opacity = static_cast<float>(std::clamp(opacity, 0.0, 1.0));
    item.vertices.reserve(1024);
    item.indices.reserve(1024);

    for (const auto &tile_entry : state->terrain_tiles) {
      const TerrainTile &tile = *tile_entry.second;
      append_ground_overlay_tile_ring(tile,
                                      mapping,
                                      item,
                                      mesh_origin_latitude,
                                      mesh_origin_longitude,
                                      altitude_offset,
                                      0.0,
                                      near_radius,
                                      terrain_lod_step(base_sample_step, 0));
      if (mid_radius > near_radius) {
        append_ground_overlay_tile_ring(tile,
                                        mapping,
                                        item,
                                        mesh_origin_latitude,
                                        mesh_origin_longitude,
                                        altitude_offset,
                                        near_radius,
                                        mid_radius,
                                        terrain_lod_step(base_sample_step, 1));
      }
      if (far_radius > mid_radius) {
        append_ground_overlay_tile_ring(tile,
                                        mapping,
                                        item,
                                        mesh_origin_latitude,
                                        mesh_origin_longitude,
                                        altitude_offset,
                                        mid_radius,
                                        far_radius,
                                        terrain_lod_step(base_sample_step, 2));
      }
      if (radius_m > far_radius) {
        append_ground_overlay_tile_ring(tile,
                                        mapping,
                                        item,
                                        mesh_origin_latitude,
                                        mesh_origin_longitude,
                                        altitude_offset,
                                        far_radius,
                                        radius_m,
                                        terrain_lod_step(base_sample_step, 3));
      }
    }

    if (!item.indices.empty())
      ground_overlays.push_back(std::move(item));
  }
}

glm::dmat3 node_ned_rotation(const SceneNode &node);
glm::dvec3 node_vertex_position(const SceneNode &node,
                                const glm::dmat3 &rotation,
                                const glm::dvec3 &center,
                                double north,
                                double east,
                                double down);

bool
make_pick_ray_locked(GWorldSceneViewState *state,
                     GtkWidget *widget,
                     double widget_x,
                     double widget_y,
                     ScenePickRay &ray)
{
  const int width = std::max(1, gtk_widget_get_width(widget));
  const int height = std::max(1, gtk_widget_get_height(widget));
  const double altitude = std::max(25.0, state->altitude_amsl);
  const double radius_m = terrain_build_radius_for_altitude(state->altitude_amsl);
  const bool render_globe = altitude >= kGlobeRenderAltitudeM;

  double far_plane_m = std::max(100000.0, radius_m * 3.0 + altitude * 4.0);
  if (render_globe)
    far_plane_m = std::max(far_plane_m, altitude + gworld_scene::kWgs84A * 2.4);
  const double near_plane = render_globe ? std::clamp(altitude * 0.002, 25.0, 25000.0) : 2.0;

  const glm::dmat4 projection =
    glm::perspective(deg_to_rad(45.0),
                     static_cast<double>(width) / static_cast<double>(height),
                     near_plane,
                     far_plane_m);
  const gworld_scene::CameraPose camera_pose =
    camera_pose_for_mode(state->camera_mode,
                         state->latitude,
                         state->longitude,
                         altitude,
                         state->heading_deg,
                         state->pitch_deg,
                         state->mesh_origin_latitude,
                         state->mesh_origin_longitude);
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
ray_intersects_triangle(const ScenePickRay &ray,
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
ray_intersects_sphere(const ScenePickRay &ray,
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

glm::dvec3
mesh_vertex_position(const std::vector<float> &vertices, unsigned int vertex_index)
{
  const std::size_t base = static_cast<std::size_t>(vertex_index) * kVertexStride;
  return glm::dvec3(static_cast<double>(vertices[base]),
                    static_cast<double>(vertices[base + 1]),
                    static_cast<double>(vertices[base + 2]));
}

float
mesh_vertex_material(const std::vector<float> &vertices, unsigned int vertex_index)
{
  const std::size_t base = static_cast<std::size_t>(vertex_index) * kVertexStride;
  return vertices[base + 17];
}

void
scene_position_to_lat_lon_approx(const glm::dvec3 &position,
                                 double mesh_origin_latitude,
                                 double mesh_origin_longitude,
                                 double &latitude,
                                 double &longitude)
{
  gworld_scene::translate_geodetic_ned(mesh_origin_latitude,
                                       mesh_origin_longitude,
                                       0.0,
                                       -position.z,
                                       position.x,
                                       0.0,
                                       &latitude,
                                       &longitude,
                                       nullptr);
}

void
fill_pick_location_from_scene_position_locked(GWorldSceneViewState *state,
                                              const glm::dvec3 &position,
                                              ScenePickResult &result)
{
  scene_position_to_lat_lon_approx(position,
                                   state->mesh_origin_latitude,
                                   state->mesh_origin_longitude,
                                   result.latitude,
                                   result.longitude);
  double terrain_height = 0.0;
  if (terrain_height_at_lat_lon_locked(state, result.latitude, result.longitude, terrain_height))
    result.altitude_amsl = terrain_height;
  else
    result.altitude_amsl = position.y;
}

bool
project_scene_point_to_widget(const ScenePickRay &ray,
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

bool
scene_node_snapshot_locked(GWorldSceneNode *scene_node, SceneNode &node)
{
  if (!GWORLD_IS_SCENE_NODE(scene_node))
    return false;

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

  if (GWORLD_IS_SCENE_CUBE_NODE(scene_node)) {
    gworld_scene_cube_node_get_dimensions(GWORLD_SCENE_CUBE_NODE(scene_node),
                                          &node.width_m,
                                          &node.depth_m,
                                          &node.height_m);
  } else if (GWORLD_IS_SCENE_SPHERE_NODE(scene_node)) {
    const double diameter = gworld_scene_sphere_node_get_diameter(GWORLD_SCENE_SPHERE_NODE(scene_node));
    node.width_m = diameter;
    node.depth_m = diameter;
    node.height_m = diameter;
  } else if (GWORLD_IS_SCENE_CYLINDER_NODE(scene_node)) {
    double diameter = 0.0;
    gworld_scene_cylinder_node_get_size(GWORLD_SCENE_CYLINDER_NODE(scene_node),
                                        &diameter,
                                        &node.height_m);
    node.width_m = diameter;
    node.depth_m = diameter;
  } else if (GWORLD_IS_SCENE_MODEL_NODE(scene_node)) {
    const char *model_path = gworld_scene_model_node_get_model_path(GWORLD_SCENE_MODEL_NODE(scene_node));
    if (model_path != nullptr)
      node.model_path = model_path;
  }

  return true;
}

double
scene_node_bounding_radius(const SceneNode &node)
{
  const double north = std::max(0.001, node.depth_m * std::abs(node.scale_x));
  const double east = std::max(0.001, node.width_m * std::abs(node.scale_y));
  const double vertical = std::max(0.001, node.height_m * std::abs(node.scale_z));
  return 0.5 * std::sqrt(north * north + east * east + vertical * vertical);
}

bool
try_update_pick(ScenePickResult &best,
                GWorldSceneNode *node,
                double distance,
                double latitude,
                double longitude,
                double altitude_amsl,
                bool ground)
{
  if (distance < 0.0 || distance >= best.distance_m)
    return false;

  best.hit = true;
  best.ground = ground;
  best.node = node;
  best.distance_m = distance;
  best.latitude = latitude;
  best.longitude = longitude;
  best.altitude_amsl = altitude_amsl;
  return true;
}

bool
pick_billboard_node_locked(GWorldSceneViewState *state,
                           GWorldSceneNode *scene_node,
                           const ScenePickRay &ray,
                           double widget_x,
                           double widget_y,
                           ScenePickResult &best)
{
  auto *billboard = GWORLD_SCENE_BILLBOARD_NODE(scene_node);
  const char *image_path = gworld_scene_billboard_node_get_image_path(billboard);
  if (image_path == nullptr)
    return false;

  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_amsl = 0.0;
  gworld_scene_node_get_position(scene_node, &latitude, &longitude, &altitude_amsl);
  if (gworld_scene_billboard_node_get_altitude_mode(billboard) == GWORLD_SCENE_ALTITUDE_AGL) {
    double terrain_height = 0.0;
    if (terrain_height_at_lat_lon_locked(state, latitude, longitude, terrain_height))
      altitude_amsl += terrain_height;
  }

  const glm::dvec3 center = geodetic_to_enu(latitude,
                                            longitude,
                                            altitude_amsl,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0);
  const double distance = glm::length(center - ray.origin);
  const double max_visible_distance = gworld_scene_billboard_node_get_max_visible_distance(billboard);
  if (max_visible_distance > 0.0 && distance > max_visible_distance)
    return false;

  double center_x = 0.0;
  double center_y = 0.0;
  double depth = 0.0;
  if (!project_scene_point_to_widget(ray, center, center_x, center_y, depth))
    return false;

  double min_px = 0.0;
  double max_px = 0.0;
  double reference_size_px = 0.0;
  double reference_distance_m = 0.0;
  gworld_scene_billboard_node_get_size_limits(billboard, &min_px, &max_px);
  gworld_scene_billboard_node_get_reference_size(billboard,
                                                 &reference_size_px,
                                                 &reference_distance_m);
  const double target_width_px =
    std::clamp(reference_size_px * (reference_distance_m / std::max(distance, 1.0)),
               min_px,
               max_px);
  double aspect = 1.0;
  auto texture_iter = state->scene_image_textures.find(image_path);
  if (texture_iter != state->scene_image_textures.end() &&
      texture_iter->second.width > 0 &&
      texture_iter->second.height > 0) {
    aspect = static_cast<double>(texture_iter->second.width) /
             static_cast<double>(texture_iter->second.height);
  }
  const double target_height_px = target_width_px / std::max(aspect, 0.001);
  if (std::abs(widget_x - center_x) > target_width_px * 0.5 ||
      std::abs(widget_y - center_y) > target_height_px * 0.5)
    return false;

  return try_update_pick(best, scene_node, distance, latitude, longitude, altitude_amsl, false);
}

bool
pick_model_node_locked(GWorldSceneViewState *state,
                       GWorldSceneNode *scene_node,
                       const SceneNode &node,
                       const ScenePickRay &ray,
                       ScenePickResult &best)
{
  if (node.model_path.empty())
    return false;

  auto mesh_iter = state->model_meshes.find(node.model_path);
  if (mesh_iter == state->model_meshes.end() || !mesh_iter->second.loaded)
    return false;

  const ModelMesh &mesh = mesh_iter->second;
  const glm::dvec3 center = geodetic_to_enu(node.latitude,
                                            node.longitude,
                                            node.altitude_amsl,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0);
  const glm::dmat3 rotation = node_ned_rotation(node);
  bool hit = false;
  double best_distance = best.distance_m;
  glm::dvec3 best_position(0.0);

  for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
    const unsigned int i0 = mesh.indices[i];
    const unsigned int i1 = mesh.indices[i + 1];
    const unsigned int i2 = mesh.indices[i + 2];
    if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
      continue;

    auto transform_model_vertex = [&](unsigned int index) {
      const glm::dvec3 &p = mesh.vertices[index].position;
      return node_vertex_position(node, rotation, center, p.z, p.x, -p.y);
    };

    const glm::dvec3 p0 = transform_model_vertex(i0);
    const glm::dvec3 p1 = transform_model_vertex(i1);
    const glm::dvec3 p2 = transform_model_vertex(i2);
    double distance = 0.0;
    if (!ray_intersects_triangle(ray, p0, p1, p2, distance) || distance >= best_distance)
      continue;

    best_distance = distance;
    best_position = ray.origin + ray.direction * distance;
    hit = true;
  }

  if (!hit)
    return false;

  ScenePickResult candidate;
  fill_pick_location_from_scene_position_locked(state, best_position, candidate);
  return try_update_pick(best,
                         scene_node,
                         best_distance,
                         candidate.latitude,
                         candidate.longitude,
                         candidate.altitude_amsl,
                         false);
}

bool
pick_bounded_node_locked(GWorldSceneViewState *state,
                         GWorldSceneNode *scene_node,
                         const SceneNode &node,
                         const ScenePickRay &ray,
                         ScenePickResult &best)
{
  const glm::dvec3 center = geodetic_to_enu(node.latitude,
                                            node.longitude,
                                            node.altitude_amsl,
                                            state->mesh_origin_latitude,
                                            state->mesh_origin_longitude,
                                            0.0);
  double distance = 0.0;
  if (!ray_intersects_sphere(ray, center, scene_node_bounding_radius(node), distance))
    return false;

  const glm::dvec3 position = ray.origin + ray.direction * distance;
  ScenePickResult candidate;
  fill_pick_location_from_scene_position_locked(state, position, candidate);
  return try_update_pick(best,
                         scene_node,
                         distance,
                         candidate.latitude,
                         candidate.longitude,
                         candidate.altitude_amsl,
                         false);
}

bool
pick_terrain_locked(GWorldSceneViewState *state,
                    const ScenePickRay &ray,
                    ScenePickResult &terrain_pick)
{
  bool hit = false;
  double best_distance = std::numeric_limits<double>::max();
  glm::dvec3 best_position(0.0);

  for (std::size_t i = 0; i + 2 < state->indices.size(); i += 3) {
    const unsigned int i0 = state->indices[i];
    const unsigned int i1 = state->indices[i + 1];
    const unsigned int i2 = state->indices[i + 2];
    const std::size_t vertex_count = state->vertices.size() / kVertexStride;
    if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
      continue;
    if (std::abs(mesh_vertex_material(state->vertices, i0) - kMaterialTerrain) > 0.001f ||
        std::abs(mesh_vertex_material(state->vertices, i1) - kMaterialTerrain) > 0.001f ||
        std::abs(mesh_vertex_material(state->vertices, i2) - kMaterialTerrain) > 0.001f)
      continue;

    const glm::dvec3 p0 = mesh_vertex_position(state->vertices, i0);
    const glm::dvec3 p1 = mesh_vertex_position(state->vertices, i1);
    const glm::dvec3 p2 = mesh_vertex_position(state->vertices, i2);
    double distance = 0.0;
    if (!ray_intersects_triangle(ray, p0, p1, p2, distance) || distance >= best_distance)
      continue;

    best_distance = distance;
    best_position = ray.origin + ray.direction * distance;
    hit = true;
  }

  if (!hit)
    return false;

  fill_pick_location_from_scene_position_locked(state, best_position, terrain_pick);
  terrain_pick.hit = true;
  terrain_pick.ground = true;
  terrain_pick.distance_m = best_distance;
  return true;
}

bool
pick_ground_overlay_at_terrain_hit_locked(GWorldSceneViewState *state,
                                          const ScenePickResult &terrain_pick,
                                          ScenePickResult &best)
{
  bool picked_overlay = false;
  for (const auto &entry : state->scene_nodes) {
    GWorldSceneNode *scene_node = entry.second;
    if (!GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(scene_node))
      continue;

    auto *overlay = GWORLD_SCENE_GROUND_OVERLAY_NODE(scene_node);
    if (gworld_scene_ground_overlay_node_get_opacity(overlay) <= 0.0)
      continue;

    double lat[4] = {0.0, 0.0, 0.0, 0.0};
    double lon[4] = {0.0, 0.0, 0.0, 0.0};
    gworld_scene_ground_overlay_node_get_corners(overlay,
                                                 &lat[0],
                                                 &lon[0],
                                                 &lat[1],
                                                 &lon[1],
                                                 &lat[2],
                                                 &lon[2],
                                                 &lat[3],
                                                 &lon[3]);
    const GroundOverlayMapping mapping = ground_overlay_mapping_from_corners(lat, lon);
    double u = 0.0;
    double v = 0.0;
    if (!ground_overlay_uv_from_lat_lon(mapping,
                                        terrain_pick.latitude,
                                        terrain_pick.longitude,
                                        u,
                                        v))
      continue;
    if (u < -1e-6 || u > 1.0 + 1e-6 || v < -1e-6 || v > 1.0 + 1e-6)
      continue;

    picked_overlay = try_update_pick(best,
                                     scene_node,
                                     std::max(0.0, terrain_pick.distance_m - 0.01),
                                     terrain_pick.latitude,
                                     terrain_pick.longitude,
                                     terrain_pick.altitude_amsl,
                                     false) ||
                     picked_overlay;
  }
  return picked_overlay;
}

bool
pick_scene_locked(GWorldSceneViewState *state,
                  GtkWidget *widget,
                  double widget_x,
                  double widget_y,
                  ScenePickResult &best)
{
  ScenePickRay ray;
  if (!make_pick_ray_locked(state, widget, widget_x, widget_y, ray))
    return false;

  ScenePickResult terrain_pick;
  const bool has_terrain_pick = pick_terrain_locked(state, ray, terrain_pick);
  if (has_terrain_pick) {
    best = terrain_pick;
    pick_ground_overlay_at_terrain_hit_locked(state, terrain_pick, best);
  }

  for (const auto &entry : state->scene_nodes) {
    GWorldSceneNode *scene_node = entry.second;
    if (!GWORLD_IS_SCENE_NODE(scene_node) || GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(scene_node))
      continue;

    if (GWORLD_IS_SCENE_BILLBOARD_NODE(scene_node)) {
      pick_billboard_node_locked(state, scene_node, ray, widget_x, widget_y, best);
      continue;
    }

    SceneNode node;
    if (!scene_node_snapshot_locked(scene_node, node))
      continue;

    if (GWORLD_IS_SCENE_MODEL_NODE(scene_node)) {
      pick_model_node_locked(state, scene_node, node, ray, best);
      continue;
    }

    pick_bounded_node_locked(state, scene_node, node, ray, best);
  }

  return best.hit;
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
  vertices.push_back(uv.ultra_u);
  vertices.push_back(uv.ultra_v);
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
load_scene_image_rgba(const std::string &path,
                          std::vector<unsigned char> &pixels,
                          int &width,
                          int &height,
                          std::string *error_message = nullptr)
{
  g_autoptr(GError) error = nullptr;
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path.c_str(), &error);
  if (pixbuf == nullptr) {
    if (error_message)
      *error_message = error ? error->message : "unknown image load error";
    return false;
  }

  const int source_width = gdk_pixbuf_get_width(pixbuf);
  const int source_height = gdk_pixbuf_get_height(pixbuf);
  if (source_width <= 0 || source_height <= 0) {
    if (error_message)
      *error_message = "invalid billboard image dimensions";
    g_object_unref(pixbuf);
    return false;
  }

  const int longest = std::max(source_width, source_height);
  const double scale = longest > kMaxSceneImageTexturePixels
                         ? static_cast<double>(kMaxSceneImageTexturePixels) / static_cast<double>(longest)
                         : 1.0;
  width = std::max(1, static_cast<int>(std::lround(static_cast<double>(source_width) * scale)));
  height = std::max(1, static_cast<int>(std::lround(static_cast<double>(source_height) * scale)));

  const bool result = copy_pixbuf_rgba_scaled(pixbuf, width, height, pixels, error_message);
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
    if (GWORLD_IS_SCENE_BILLBOARD_NODE(scene_node))
      continue;
    if (GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(scene_node))
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
  (void)range;
  TexCoords uv;
  uv.detail_u = static_cast<float>(latitude);
  uv.detail_v = static_cast<float>(longitude);
  uv.mid_u = -1.0f;
  uv.mid_v = -1.0f;
  uv.base_u = -1.0f;
  uv.base_v = -1.0f;
  uv.ultra_u = -1.0f;
  uv.ultra_v = -1.0f;
  append_vertex(vertices, position, uv, normal, glm::vec3(0.08f, 0.22f, 0.46f), kMaterialGlobe);
}

bool
rebuild_globe_mesh(GWorldSceneViewState *state, const AtlasRange &range)
{
  const std::string key = std::to_string(static_cast<int>(std::llround(state->mesh_origin_latitude * 10000.0))) +
                          ":" + std::to_string(static_cast<int>(std::llround(state->mesh_origin_longitude * 10000.0)));
  if (state->globe_mesh_key == key && !state->globe_vertices.empty())
    return false;

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
  return true;
}

void
append_flat_mesh(GWorldSceneViewState *state,
                 const AtlasRange &ultra_range,
                 const AtlasRange &detail_range,
                 const AtlasRange &mid_range,
                 const AtlasRange &base_range)
{
  const double radius = terrain_build_radius_for_altitude(state->altitude_amsl);
  const glm::dvec3 p00(-radius, 0.0, radius);
  const glm::dvec3 p10(radius, 0.0, radius);
  const glm::dvec3 p01(-radius, 0.0, -radius);
  const glm::dvec3 p11(radius, 0.0, -radius);
  const double lat_delta = radius / kEarthMetersPerDegree;
  const double lon_scale = std::max(0.05, std::cos(deg_to_rad(state->latitude)));
  const double lon_delta = radius / (kEarthMetersPerDegree * lon_scale);
  auto lat_lon_uv = [](double latitude, double longitude) {
    TexCoords uv;
    uv.detail_u = static_cast<float>(latitude);
    uv.detail_v = static_cast<float>(longitude);
    uv.mid_u = -1.0f;
    uv.mid_v = -1.0f;
    uv.base_u = -1.0f;
    uv.base_v = -1.0f;
    uv.ultra_u = -1.0f;
    uv.ultra_v = -1.0f;
    return uv;
  };
  const TexCoords uv00 = lat_lon_uv(state->latitude - lat_delta, state->longitude - lon_delta);
  const TexCoords uv10 = lat_lon_uv(state->latitude - lat_delta, state->longitude + lon_delta);
  const TexCoords uv01 = lat_lon_uv(state->latitude + lat_delta, state->longitude - lon_delta);
  const TexCoords uv11 = lat_lon_uv(state->latitude + lat_delta, state->longitude + lon_delta);

  state->vertices.clear();
  state->indices.clear();
  append_triangle(state->vertices, state->indices, p00, p10, p01, uv00, uv10, uv01);
  append_triangle(state->vertices, state->indices, p10, p11, p01, uv10, uv11, uv01);
  state->mesh_ultra_atlas_range = ultra_range;
  state->mesh_atlas_range = detail_range;
  state->mesh_mid_atlas_range = mid_range;
  state->mesh_base_atlas_range = base_range;
  state->mesh_dirty = true;
}

void
append_terrain_tile_mesh_ring(GWorldSceneViewState *state,
                              const TerrainTile &tile,
                              const AtlasRange &ultra_range,
                              const AtlasRange &detail_range,
                              const AtlasRange &mid_range,
                              const AtlasRange &base_range,
                              double ultra_lateral_radius_m,
                              double inner_radius_m,
                              double outer_radius_m,
                              int sample_step)
{
  (void)ultra_range;
  (void)detail_range;
  (void)mid_range;
  (void)base_range;
  (void)ultra_lateral_radius_m;

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
      uv00.detail_u = static_cast<float>(lat00);
      uv00.detail_v = static_cast<float>(lon00);
      uv10.detail_u = static_cast<float>(lat10);
      uv10.detail_v = static_cast<float>(lon10);
      uv01.detail_u = static_cast<float>(lat01);
      uv01.detail_v = static_cast<float>(lon01);
      uv11.detail_u = static_cast<float>(lat11);
      uv11.detail_v = static_cast<float>(lon11);

      append_triangle(state->vertices, state->indices, p00, p10, p01, uv00, uv10, uv01);
      append_triangle(state->vertices, state->indices, p10, p11, p01, uv10, uv11, uv01);
    }
  }
}

void
append_terrain_tile_mesh(GWorldSceneViewState *state,
                         const TerrainTile &tile,
                         const AtlasRange &ultra_range,
                         const AtlasRange &detail_range,
                         const AtlasRange &mid_range,
                         const AtlasRange &base_range,
                         double ultra_lateral_radius_m,
                         double radius_m,
                         int base_sample_step)
{
  const double near_radius = terrain_near_radius_for_altitude(state->altitude_amsl, radius_m);
  const double mid_radius = terrain_mid_radius_for_altitude(state->altitude_amsl, radius_m);
  const double far_radius = terrain_far_radius_for_altitude(state->altitude_amsl, radius_m);

  append_terrain_tile_mesh_ring(state,
                                tile,
                                ultra_range,
                                detail_range,
                                mid_range,
                                base_range,
                                ultra_lateral_radius_m,
                                0.0,
                                near_radius,
                                terrain_lod_step(base_sample_step, 0));
  if (mid_radius > near_radius) {
    append_terrain_tile_mesh_ring(state,
                                  tile,
                                  ultra_range,
                                  detail_range,
                                  mid_range,
                                  base_range,
                                  ultra_lateral_radius_m,
                                  near_radius,
                                  mid_radius,
                                  terrain_lod_step(base_sample_step, 1));
  }
  if (far_radius > mid_radius) {
    append_terrain_tile_mesh_ring(state,
                                  tile,
                                  ultra_range,
                                  detail_range,
                                  mid_range,
                                  base_range,
                                  ultra_lateral_radius_m,
                                  mid_radius,
                                  far_radius,
                                  terrain_lod_step(base_sample_step, 2));
  }
  if (radius_m > far_radius) {
    append_terrain_tile_mesh_ring(state,
                                  tile,
                                  ultra_range,
                                  detail_range,
                                  mid_range,
                                  base_range,
                                  ultra_lateral_radius_m,
                                  far_radius,
                                  radius_m,
                                  terrain_lod_step(base_sample_step, 3));
  }
}

bool
rebuild_world_mesh(GWorldSceneViewState *state,
                   const AtlasRange &ultra_range,
                   const AtlasRange &detail_range,
                   const AtlasRange &mid_range,
                   const AtlasRange &base_range,
                   double ultra_lateral_radius_m,
                   bool include_terrain)
{
  const double radius_m = terrain_build_radius_for_altitude(state->altitude_amsl);
  const int sample_step = terrain_step_for_altitude(state->altitude_amsl);
  const bool recenter_mesh =
    state->vertices.empty() ||
    distance_from_mesh_origin_m(state) > mesh_recenter_distance_for_radius(radius_m);
  if (recenter_mesh) {
    state->mesh_origin_latitude = state->latitude;
    state->mesh_origin_longitude = state->longitude;
    state->globe_mesh_key.clear();
  }

  const std::string key = world_mesh_key(state->mesh_origin_latitude,
                                         state->mesh_origin_longitude,
                                         state->altitude_amsl,
                                         radius_m,
                                         sample_step,
                                         state->terrain_tiles.size(),
                                         state->terrain_revision,
                                         state->scene_revision,
                                         include_terrain);

  state->ultra_texture_lateral_radius_m = ultra_lateral_radius_m;
  if (state->mesh_key == key && !state->vertices.empty())
    return false;

  state->vertices.clear();
  state->indices.clear();

  if (include_terrain) {
    for (const auto &entry : state->terrain_tiles)
      append_terrain_tile_mesh(state,
                               *entry.second,
                               ultra_range,
                               detail_range,
                               mid_range,
                               base_range,
                               ultra_lateral_radius_m,
                               radius_m,
                               sample_step);

    if (state->vertices.empty())
      append_flat_mesh(state, ultra_range, detail_range, mid_range, base_range);
  }

  append_scene_nodes(state);

  state->mesh_key = key;
  state->mesh_includes_terrain = include_terrain;
  state->mesh_ultra_atlas_range = ultra_range;
  state->mesh_atlas_range = detail_range;
  state->mesh_mid_atlas_range = mid_range;
  state->mesh_base_atlas_range = base_range;
  state->ultra_texture_lateral_radius_m = ultra_lateral_radius_m;
  state->mesh_lod_altitude_amsl = state->altitude_amsl;
  state->mesh_radius_m = radius_m;
  state->mesh_sample_step = sample_step;
  state->mesh_dirty = true;
  return true;
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
	layout(location = 4) in vec2 ultra_texcoord;
	layout(location = 5) in vec3 normal;
	layout(location = 6) in vec3 vertex_color;
	layout(location = 7) in float material;
	uniform mat4 mvp;
	uniform mat4 light_mvp;
	uniform bool ultra_atlas_valid;
	uniform vec4 ultra_atlas_range;
	uniform vec2 ultra_atlas_size;
	uniform bool detail_atlas_valid;
	uniform vec4 detail_atlas_range;
	uniform vec2 detail_atlas_size;
	uniform bool mid_atlas_valid;
	uniform vec4 mid_atlas_range;
	uniform vec2 mid_atlas_size;
	uniform bool base_atlas_valid;
	uniform vec4 base_atlas_range;
	uniform vec2 base_atlas_size;
	uniform bool globe_atlas_valid;
	uniform vec4 globe_atlas_range;
	uniform vec2 globe_atlas_size;
	out vec2 v_detail_texcoord;
	out vec2 v_mid_texcoord;
	out vec2 v_base_texcoord;
	out vec2 v_ultra_texcoord;
	out vec3 v_normal;
out vec3 v_color;
out float v_material;
out float v_height;
out vec3 v_world_position;
out vec4 v_light_position;

const float MERCATOR_MAX_LATITUDE = 85.05112878;
const float PI = 3.14159265358979323846;

vec2 atlas_uv_for_lat_lon(vec2 lat_lon, vec4 range, vec2 size) {
  if (size.x <= 0.0 || size.y <= 0.0)
    return vec2(-1.0, -1.0);

  float n = exp2(range.x);
  float latitude = clamp(lat_lon.x, -MERCATOR_MAX_LATITUDE, MERCATOR_MAX_LATITUDE);
  float lat_rad = radians(latitude);
  float tile_x = (lat_lon.y + 180.0) / 360.0 * n;
  float tile_y = (1.0 - log(tan(PI * 0.25 + lat_rad * 0.5)) / PI) * 0.5 * n;
  return vec2((tile_x - range.y) / size.x,
              (tile_y - range.z) / size.y);
}

void main() {
  vec4 world_position = vec4(position, 1.0);
  gl_Position = mvp * world_position;
  bool is_terrain = material < 0.5;
  bool is_globe = material > 1.5 && material < 2.5;
  if (is_terrain) {
    vec2 lat_lon = detail_texcoord;
    v_detail_texcoord = detail_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, detail_atlas_range, detail_atlas_size) : vec2(-1.0, -1.0);
    v_mid_texcoord = mid_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, mid_atlas_range, mid_atlas_size) : vec2(-1.0, -1.0);
    v_base_texcoord = base_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, base_atlas_range, base_atlas_size) : vec2(-1.0, -1.0);
    v_ultra_texcoord = ultra_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, ultra_atlas_range, ultra_atlas_size) : vec2(-1.0, -1.0);
  } else if (is_globe) {
    vec2 lat_lon = detail_texcoord;
    v_detail_texcoord = vec2(-1.0, -1.0);
    v_mid_texcoord = vec2(-1.0, -1.0);
    v_base_texcoord = globe_atlas_valid ? atlas_uv_for_lat_lon(lat_lon, globe_atlas_range, globe_atlas_size) : vec2(-1.0, -1.0);
    v_ultra_texcoord = vec2(-1.0, -1.0);
  } else {
    v_detail_texcoord = detail_texcoord;
    v_mid_texcoord = mid_texcoord;
    v_base_texcoord = base_texcoord;
    v_ultra_texcoord = ultra_texcoord;
  }
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
	in vec2 v_ultra_texcoord;
in vec3 v_normal;
in vec3 v_color;
in float v_material;
in float v_height;
in vec3 v_world_position;
in vec4 v_light_position;
	uniform sampler2D ultra_texture;
	uniform sampler2D detail_texture;
uniform sampler2D mid_texture;
uniform sampler2D base_texture;
uniform sampler2D shadow_texture;
uniform sampler2D model_texture;
	uniform bool has_ultra_texture;
	uniform bool has_detail_texture;
uniform bool has_mid_texture;
uniform bool has_base_texture;
uniform bool has_shadow_texture;
uniform bool has_model_texture;
uniform vec3 sun_direction;
uniform vec3 ambient_color;
uniform vec3 direct_light_color;
uniform float ambient_strength;
uniform float sun_strength;
		uniform vec3 camera_position;
		uniform vec3 ultra_texture_center;
		uniform float ultra_texture_radius;
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
  vec3 light = ambient_color * ambient_strength +
               direct_light_color * diffuse * sun_strength * visibility;
  vec3 low = vec3(0.26, 0.36, 0.24);
  vec3 high = vec3(0.76, 0.72, 0.62);
  vec3 terrain_tint = mix(low, high, clamp(v_height / 1800.0, 0.0, 1.0));
	  bool in_detail = v_detail_texcoord.x >= 0.0 && v_detail_texcoord.x <= 1.0 && v_detail_texcoord.y >= 0.0 && v_detail_texcoord.y <= 1.0;
	  bool in_mid = v_mid_texcoord.x >= 0.0 && v_mid_texcoord.x <= 1.0 && v_mid_texcoord.y >= 0.0 && v_mid_texcoord.y <= 1.0;
	  bool in_base = v_base_texcoord.x >= 0.0 && v_base_texcoord.x <= 1.0 && v_base_texcoord.y >= 0.0 && v_base_texcoord.y <= 1.0;
	  bool in_ultra = v_ultra_texcoord.x >= 0.0 && v_ultra_texcoord.x <= 1.0 && v_ultra_texcoord.y >= 0.0 && v_ultra_texcoord.y <= 1.0;
	  vec4 base_texel = (has_base_texture && in_base) ? texture(base_texture, v_base_texcoord) : vec4(0.0);
	  vec4 mid_texel = (has_mid_texture && in_mid) ? texture(mid_texture, v_mid_texcoord) : vec4(0.0);
	  vec4 detail_texel = (has_detail_texture && in_detail) ? texture(detail_texture, v_detail_texcoord) : vec4(0.0);
	  vec4 ultra_texel = (has_ultra_texture && in_ultra) ? texture(ultra_texture, v_ultra_texcoord) : vec4(0.0);
	  vec4 texture_stack = detail_texel.a > 0.01 ? detail_texel : (mid_texel.a > 0.01 ? mid_texel : base_texel);
		  float ultra_lateral_distance = length((v_world_position - ultra_texture_center).xz);
	  float ultra_radius = max(ultra_texture_radius, 1.0);
	  float ultra_fade_width = clamp(ultra_radius * 0.22, 80.0, 220.0);
	  float ultra_blend = 1.0 - smoothstep(max(0.0, ultra_radius - ultra_fade_width),
	                                      ultra_radius,
	                                      ultra_lateral_distance);
	  ultra_blend *= ultra_texel.a > 0.01 ? 1.0 : 0.0;
	  vec4 texel = (has_ultra_texture && ultra_texture_radius > 1.0)
	                 ? mix(texture_stack, ultra_texel, ultra_blend)
	                 : texture_stack;
  vec3 terrain_base = mix(terrain_tint, texel.rgb, texel.a * 0.88);
  vec3 globe_base = texel.a > 0.01 ? texel.rgb : v_color;
  bool is_globe = v_material > 1.5 && v_material < 2.5;
  bool is_textured_model = v_material > 2.5;
  vec4 model_texel = (has_model_texture && is_textured_model) ? texture(model_texture, v_detail_texcoord) : vec4(0.0);
  vec3 object_base = (is_textured_model && model_texel.a > 0.01)
                       ? mix(v_color, model_texel.rgb * v_color, model_texel.a)
                       : v_color;
  vec3 base = is_globe ? globe_base : (v_material > 0.5 ? object_base : terrain_base);
  vec3 lit_color = base * clamp(light, vec3(0.0), vec3(1.45));
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

GLuint
create_billboard_program()
{
  static const char *vertex_source = R"GLSL(
#version 330 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec2 texcoord;
uniform mat4 mvp;
out vec2 v_texcoord;
void main() {
  gl_Position = mvp * vec4(position, 1.0);
  v_texcoord = texcoord;
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
in vec2 v_texcoord;
uniform sampler2D billboard_texture;
uniform float opacity;
out vec4 color;
void main() {
  vec4 texel = texture(billboard_texture, v_texcoord);
  texel.a *= opacity;
  if (texel.a < 0.01)
    discard;
  color = texel;
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
    g_warning("Billboard program link failed: %s", log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

GLuint
create_sun_program()
{
  static const char *vertex_source = R"GLSL(
#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec2 local_coord;
out vec2 v_local_coord;
void main() {
  v_local_coord = local_coord;
  gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
in vec2 v_local_coord;
uniform vec3 sun_color;
uniform float intensity;
uniform float core_ratio;
out vec4 color;
void main() {
  float distance_from_center = length(v_local_coord);
  if (distance_from_center > 1.0)
    discard;

  float core = 1.0 - smoothstep(core_ratio * 0.78, core_ratio, distance_from_center);
  float inner_glow = 1.0 - smoothstep(core_ratio, min(core_ratio * 2.4, 1.0), distance_from_center);
  float halo = pow(clamp(1.0 - distance_from_center, 0.0, 1.0), 2.2);
  float alpha = max(core, max(inner_glow * 0.42, halo * 0.28)) * intensity;
  vec3 white_hot = vec3(1.0, 0.98, 0.86);
  vec3 rgb = mix(sun_color, white_hot, clamp(core + inner_glow * 0.35, 0.0, 1.0));
  color = vec4(rgb, alpha);
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
    g_warning("Sun program link failed: %s", log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

GLuint
create_sky_program()
{
  static const char *vertex_source = R"GLSL(
#version 330 core
layout(location = 0) in vec2 position;
out vec2 v_ndc;
void main() {
  v_ndc = position;
  gl_Position = vec4(position, 0.0, 1.0);
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
in vec2 v_ndc;
uniform mat4 inverse_mvp;
uniform vec3 camera_position;
uniform vec3 sun_direction;
uniform vec3 day_horizon_color;
uniform vec3 day_zenith_color;
uniform vec3 twilight_color;
uniform vec3 night_color;
uniform float daylight;
uniform float twilight;
uniform float horizon_glow_strength;
out vec4 color;

void main() {
  vec4 far_world = inverse_mvp * vec4(v_ndc, 1.0, 1.0);
  far_world /= far_world.w;
  vec3 ray = normalize(far_world.xyz - camera_position);
  float up = ray.y;

  float sky_mix = smoothstep(-0.04, 0.82, up);
  vec3 day_sky = mix(day_horizon_color, day_zenith_color, sky_mix);
  vec3 twilight_sky = mix(twilight_color, day_sky, clamp(daylight, 0.0, 1.0));
  vec3 sky = mix(night_color, twilight_sky, clamp(daylight + twilight * 0.75, 0.0, 1.0));

  vec2 ray_horizontal = ray.xz;
  vec2 sun_horizontal = sun_direction.xz;
  float ray_len = length(ray_horizontal);
  float sun_len = length(sun_horizontal);
  float azimuth_alignment = 0.0;
  if (ray_len > 0.0001 && sun_len > 0.0001)
    azimuth_alignment = clamp(dot(ray_horizontal / ray_len, sun_horizontal / sun_len), 0.0, 1.0);

  float horizon_band = 1.0 - smoothstep(0.00, 0.20, abs(up));
  float broad_lobe = pow(azimuth_alignment, 5.5);
  float core_lobe = pow(azimuth_alignment, 22.0);
  float glow = horizon_band * (broad_lobe * 0.45 + core_lobe * 0.75) * horizon_glow_strength;
  vec3 amber = vec3(1.00, 0.35, 0.13);
  vec3 rose = vec3(0.90, 0.18, 0.16);
  vec3 glow_color = mix(amber, rose, clamp(twilight * 0.65, 0.0, 1.0));
  sky = mix(sky, glow_color, clamp(glow, 0.0, 0.82));

  color = vec4(sky, 1.0);
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
    g_warning("Sky program link failed: %s", log);
    glDeleteProgram(program);
    return 0;
  }

  return program;
}

bool
ensure_billboard_resources(GWorldSceneViewState *state)
{
  if (state->billboard_program == 0)
    state->billboard_program = create_billboard_program();
  if (state->billboard_program == 0)
    return false;

  const bool needs_setup = state->billboard_vao == 0 || state->billboard_vbo == 0;
  if (state->billboard_vao == 0)
    glGenVertexArrays(1, &state->billboard_vao);
  if (state->billboard_vbo == 0)
    glGenBuffers(1, &state->billboard_vbo);
  if (!needs_setup)
    return true;

  glBindVertexArray(state->billboard_vao);
  glBindBuffer(GL_ARRAY_BUFFER, state->billboard_vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(6 * 5 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        5 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

bool
ensure_sky_resources(GWorldSceneViewState *state)
{
  if (state->sky_program == 0)
    state->sky_program = create_sky_program();
  if (state->sky_program == 0)
    return false;

  const bool needs_setup = state->sky_vao == 0 || state->sky_vbo == 0;
  if (state->sky_vao == 0)
    glGenVertexArrays(1, &state->sky_vao);
  if (state->sky_vbo == 0)
    glGenBuffers(1, &state->sky_vbo);
  if (!needs_setup)
    return true;

  const float vertices[] = {
    -1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f, -1.0f,
     1.0f,  1.0f,
  };

  glBindVertexArray(state->sky_vao);
  glBindBuffer(GL_ARRAY_BUFFER, state->sky_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof vertices, vertices, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), reinterpret_cast<void *>(0));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

bool
ensure_sun_resources(GWorldSceneViewState *state)
{
  if (state->sun_program == 0)
    state->sun_program = create_sun_program();
  if (state->sun_program == 0)
    return false;

  const bool needs_setup = state->sun_vao == 0 || state->sun_vbo == 0;
  if (state->sun_vao == 0)
    glGenVertexArrays(1, &state->sun_vao);
  if (state->sun_vbo == 0)
    glGenBuffers(1, &state->sun_vbo);
  if (!needs_setup)
    return true;

  glBindVertexArray(state->sun_vao);
  glBindBuffer(GL_ARRAY_BUFFER, state->sun_vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(6 * 4 * sizeof(float)), nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        4 * sizeof(float),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

bool
ensure_ground_overlay_resources(GWorldSceneViewState *state)
{
  if (state->billboard_program == 0)
    state->billboard_program = create_billboard_program();
  if (state->billboard_program == 0)
    return false;

  const bool needs_setup =
    state->ground_overlay_vao == 0 ||
    state->ground_overlay_vbo == 0 ||
    state->ground_overlay_ebo == 0;
  if (state->ground_overlay_vao == 0)
    glGenVertexArrays(1, &state->ground_overlay_vao);
  if (state->ground_overlay_vbo == 0)
    glGenBuffers(1, &state->ground_overlay_vbo);
  if (state->ground_overlay_ebo == 0)
    glGenBuffers(1, &state->ground_overlay_ebo);
  if (!needs_setup)
    return true;

  glBindVertexArray(state->ground_overlay_vao);
  glBindBuffer(GL_ARRAY_BUFFER, state->ground_overlay_vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        5 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->ground_overlay_ebo);
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  return true;
}

SceneImageTexture *
ensure_scene_image_texture(GWorldSceneViewState *state, const std::string &path)
{
  if (path.empty())
    return nullptr;

  SceneImageTexture &texture = state->scene_image_textures[path];
  if (texture.texture != 0)
    return &texture;
  if (texture.failed)
    return nullptr;

  std::vector<unsigned char> pixels;
  std::string error_message;
  int width = 0;
  int height = 0;
  if (!load_scene_image_rgba(path, pixels, width, height, &error_message)) {
    texture.failed = true;
    texture.error = error_message;
    g_warning("Scene image load failed: %s: %s", path.c_str(), error_message.c_str());
    return nullptr;
  }

  glGenTextures(1, &texture.texture);
  glBindTexture(GL_TEXTURE_2D, texture.texture);
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
    g_warning("Scene image texture upload failed: %s size=%dx%d error=0x%x",
              path.c_str(),
              width,
              height,
              error);
    glDeleteTextures(1, &texture.texture);
    texture.texture = 0;
    texture.failed = true;
    texture.error = "OpenGL texture upload failed";
    return nullptr;
  }

  texture.width = width;
  texture.height = height;
  return &texture;
}

void
write_billboard_vertex(float *vertices, int &offset, const glm::dvec3 &position, float u, float v)
{
  vertices[offset++] = static_cast<float>(position.x);
  vertices[offset++] = static_cast<float>(position.y);
  vertices[offset++] = static_cast<float>(position.z);
  vertices[offset++] = u;
  vertices[offset++] = v;
}

void
render_sky_background(GWorldSceneViewState *state,
                      const glm::mat4 &mvp,
                      const gworld_scene::CameraPose &camera_pose,
                      const glm::vec3 &sun_direction,
                      const glm::vec3 &fog_color,
                      float daylight,
                      float twilight,
                      float horizon_glow_strength)
{
  if (!ensure_sky_resources(state))
    return;

  const glm::mat4 inverse_mvp = glm::inverse(mvp);
  const glm::vec3 day_horizon_color =
    glm::mix(fog_color, glm::vec3(0.76f, 0.86f, 1.0f), 0.45f);
  const glm::vec3 day_zenith_color =
    glm::mix(fog_color, glm::vec3(0.30f, 0.52f, 0.86f), 0.52f);
  const glm::vec3 twilight_color(0.26f, 0.25f, 0.42f);
  const glm::vec3 night_color(0.085f, 0.105f, 0.175f);

  glUseProgram(state->sky_program);
  glUniformMatrix4fv(glGetUniformLocation(state->sky_program, "inverse_mvp"),
                     1,
                     GL_FALSE,
                     glm::value_ptr(inverse_mvp));
  glUniform3fv(glGetUniformLocation(state->sky_program, "camera_position"),
               1,
               glm::value_ptr(glm::vec3(camera_pose.eye)));
  glUniform3fv(glGetUniformLocation(state->sky_program, "sun_direction"),
               1,
               glm::value_ptr(sun_direction));
  glUniform3fv(glGetUniformLocation(state->sky_program, "day_horizon_color"),
               1,
               glm::value_ptr(day_horizon_color));
  glUniform3fv(glGetUniformLocation(state->sky_program, "day_zenith_color"),
               1,
               glm::value_ptr(day_zenith_color));
  glUniform3fv(glGetUniformLocation(state->sky_program, "twilight_color"),
               1,
               glm::value_ptr(twilight_color));
  glUniform3fv(glGetUniformLocation(state->sky_program, "night_color"),
               1,
               glm::value_ptr(night_color));
  glUniform1f(glGetUniformLocation(state->sky_program, "daylight"), daylight);
  glUniform1f(glGetUniformLocation(state->sky_program, "twilight"), twilight);
  glUniform1f(glGetUniformLocation(state->sky_program, "horizon_glow_strength"), horizon_glow_strength);
  glBindVertexArray(state->sky_vao);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glDisable(GL_BLEND);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glBindVertexArray(0);
  glUseProgram(0);
}

void
render_sun_disc(GWorldSceneViewState *state,
                const glm::mat4 &mvp,
                const gworld_scene::CameraPose &camera_pose,
                const glm::vec3 &sun_direction,
                const glm::dvec3 &sun_enu,
                double far_plane_m,
                int viewport_width,
                int viewport_height)
{
  if (viewport_width <= 0 || viewport_height <= 0)
    return;

  const float visibility = static_cast<float>(smoothstep_value(-0.015, 0.04, sun_enu.z));
  if (visibility <= 0.001f)
    return;
  if (!ensure_sun_resources(state))
    return;

  const glm::dvec3 sun_scene_direction =
    safe_normalize(glm::dvec3(sun_direction), glm::dvec3(-0.45, 0.76, 0.38));
  const glm::dvec3 sun_position =
    camera_pose.eye + sun_scene_direction * std::max(1000.0, far_plane_m * 0.45);
  const glm::vec4 clip = mvp * glm::vec4(glm::vec3(sun_position), 1.0f);
  if (clip.w <= 0.0f)
    return;

  const glm::vec3 ndc = glm::vec3(clip) / clip.w;
  if (ndc.z < -1.0f || ndc.z > 1.0f)
    return;

  constexpr double vertical_fov_rad = 45.0 * kPi / 180.0;
  const double physical_radius_px =
    0.5 * static_cast<double>(viewport_height) * std::tan(deg_to_rad(kSunAngularRadiusDeg)) /
    std::tan(vertical_fov_rad * 0.5);
  const double core_radius_px = std::clamp(std::max(physical_radius_px, 5.5), 5.5, 14.0);
  const double glow_radius_px = std::clamp(core_radius_px * 7.5, 34.0, 120.0);
  const float radius_x = static_cast<float>((glow_radius_px * 2.0) / viewport_width);
  const float radius_y = static_cast<float>((glow_radius_px * 2.0) / viewport_height);
  if (ndc.x < -1.0f - radius_x ||
      ndc.x > 1.0f + radius_x ||
      ndc.y < -1.0f - radius_y ||
      ndc.y > 1.0f + radius_y) {
    return;
  }

  const float center_x = ndc.x;
  const float center_y = ndc.y;
  const float vertices[] = {
    center_x - radius_x, center_y + radius_y, -1.0f,  1.0f,
    center_x - radius_x, center_y - radius_y, -1.0f, -1.0f,
    center_x + radius_x, center_y - radius_y,  1.0f, -1.0f,
    center_x - radius_x, center_y + radius_y, -1.0f,  1.0f,
    center_x + radius_x, center_y - radius_y,  1.0f, -1.0f,
    center_x + radius_x, center_y + radius_y,  1.0f,  1.0f,
  };

  const float warmth = static_cast<float>(1.0 - smoothstep_value(0.02, 0.32, sun_enu.z));
  const glm::vec3 sun_color = glm::mix(glm::vec3(1.0f, 0.50f, 0.18f),
                                       glm::vec3(1.0f, 0.84f, 0.42f),
                                       1.0f - warmth);
  const float core_ratio = static_cast<float>(core_radius_px / glow_radius_px);

  glUseProgram(state->sun_program);
  glUniform3fv(glGetUniformLocation(state->sun_program, "sun_color"), 1, glm::value_ptr(sun_color));
  glUniform1f(glGetUniformLocation(state->sun_program, "intensity"), visibility);
  glUniform1f(glGetUniformLocation(state->sun_program, "core_ratio"), core_ratio);
  glBindVertexArray(state->sun_vao);
  glBindBuffer(GL_ARRAY_BUFFER, state->sun_vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof vertices, vertices);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glUseProgram(0);
}

void
render_billboards(GWorldSceneViewState *state,
                  const std::vector<BillboardRenderItem> &billboards,
                  const glm::mat4 &mvp,
                  const gworld_scene::CameraPose &camera_pose,
                  int viewport_height,
                  double mesh_origin_latitude,
                  double mesh_origin_longitude)
{
  if (billboards.empty() || viewport_height <= 0)
    return;
  if (!ensure_billboard_resources(state))
    return;

  const glm::dvec3 camera_eye = camera_pose.eye;
  const glm::dvec3 camera_forward =
    safe_normalize(camera_pose.center - camera_pose.eye, glm::dvec3(0.0, -0.3, -1.0));
  const glm::dvec3 camera_up =
    safe_normalize(camera_pose.up, glm::dvec3(0.0, 1.0, 0.0));
  glm::dvec3 camera_right =
    safe_normalize(glm::cross(camera_forward, camera_up), glm::dvec3(1.0, 0.0, 0.0));
  const glm::dvec3 billboard_up =
    safe_normalize(glm::cross(camera_right, camera_forward), camera_up);

  glUseProgram(state->billboard_program);
  glUniformMatrix4fv(glGetUniformLocation(state->billboard_program, "mvp"),
                     1,
                     GL_FALSE,
                     glm::value_ptr(mvp));
  glUniform1i(glGetUniformLocation(state->billboard_program, "billboard_texture"), 6);
  glUniform1f(glGetUniformLocation(state->billboard_program, "opacity"), 1.0f);
  glBindVertexArray(state->billboard_vao);
  glBindBuffer(GL_ARRAY_BUFFER, state->billboard_vbo);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);

  constexpr double vertical_fov_rad = 45.0 * kPi / 180.0;
  for (const BillboardRenderItem &billboard : billboards) {
    SceneImageTexture *texture = ensure_scene_image_texture(state, billboard.image_path);
    if (texture == nullptr || texture->texture == 0 || texture->width <= 0 || texture->height <= 0)
      continue;

    const glm::dvec3 center = geodetic_to_enu(billboard.latitude,
                                              billboard.longitude,
                                              billboard.altitude_amsl,
                                              mesh_origin_latitude,
                                              mesh_origin_longitude,
                                              0.0);
    const double distance = glm::length(center - camera_eye);
    if (distance <= 0.001)
      continue;
    if (billboard.max_visible_distance_m > 0.0 &&
        distance > billboard.max_visible_distance_m)
      continue;

    const double target_width_px =
      std::clamp(billboard.reference_size_px * billboard.reference_distance_m / std::max(distance, 1.0),
                 billboard.min_px,
                 billboard.max_px);
    const double aspect =
      static_cast<double>(texture->width) / static_cast<double>(std::max(1, texture->height));
    const double target_height_px = target_width_px / std::max(aspect, 0.001);
    const double world_per_pixel =
      2.0 * distance * std::tan(vertical_fov_rad * 0.5) / static_cast<double>(viewport_height);
    const double half_width_m = target_width_px * world_per_pixel * 0.5;
    const double half_height_m = target_height_px * world_per_pixel * 0.5;

    const glm::dvec3 left = camera_right * half_width_m;
    const glm::dvec3 up = billboard_up * half_height_m;
    const glm::dvec3 top_left = center - left + up;
    const glm::dvec3 bottom_left = center - left - up;
    const glm::dvec3 bottom_right = center + left - up;
    const glm::dvec3 top_right = center + left + up;

    float vertices[30];
    int offset = 0;
    write_billboard_vertex(vertices, offset, top_left, 0.0f, 0.0f);
    write_billboard_vertex(vertices, offset, bottom_left, 0.0f, 1.0f);
    write_billboard_vertex(vertices, offset, bottom_right, 1.0f, 1.0f);
    write_billboard_vertex(vertices, offset, top_left, 0.0f, 0.0f);
    write_billboard_vertex(vertices, offset, bottom_right, 1.0f, 1.0f);
    write_billboard_vertex(vertices, offset, top_right, 1.0f, 0.0f);

    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, texture->texture);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof vertices, vertices);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }

  glBindTexture(GL_TEXTURE_2D, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  glUseProgram(0);
}

void
render_ground_overlays(GWorldSceneViewState *state,
                       const std::vector<GroundOverlayRenderItem> &ground_overlays,
                       const glm::mat4 &mvp)
{
  if (ground_overlays.empty())
    return;
  if (!ensure_ground_overlay_resources(state))
    return;

  glUseProgram(state->billboard_program);
  glUniformMatrix4fv(glGetUniformLocation(state->billboard_program, "mvp"),
                     1,
                     GL_FALSE,
                     glm::value_ptr(mvp));
  glUniform1i(glGetUniformLocation(state->billboard_program, "billboard_texture"), 6);
  glBindVertexArray(state->ground_overlay_vao);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glEnable(GL_POLYGON_OFFSET_FILL);
  glPolygonOffset(-1.0f, -1.0f);

  GLint previous_depth_func = GL_LESS;
  glGetIntegerv(GL_DEPTH_FUNC, &previous_depth_func);
  glDepthFunc(GL_LEQUAL);

  for (const GroundOverlayRenderItem &overlay : ground_overlays) {
    if (overlay.vertices.empty() || overlay.indices.empty())
      continue;

    SceneImageTexture *texture = ensure_scene_image_texture(state, overlay.image_path);
    if (texture == nullptr || texture->texture == 0)
      continue;

    glUniform1f(glGetUniformLocation(state->billboard_program, "opacity"), overlay.opacity);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, texture->texture);
    glBindBuffer(GL_ARRAY_BUFFER, state->ground_overlay_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(overlay.vertices.size() * sizeof(float)),
                 overlay.vertices.data(),
                 GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->ground_overlay_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(overlay.indices.size() * sizeof(unsigned int)),
                 overlay.indices.data(),
                 GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES,
                   static_cast<GLsizei>(overlay.indices.size()),
                   GL_UNSIGNED_INT,
                   nullptr);
  }

  glDepthFunc(static_cast<GLenum>(previous_depth_func));
  glDisable(GL_POLYGON_OFFSET_FILL);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glDepthMask(GL_TRUE);
  glUseProgram(0);
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
  if (state->ultra_texture)
    glDeleteTextures(1, &state->ultra_texture);
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
  for (auto &entry : state->scene_image_textures) {
    if (entry.second.texture)
      glDeleteTextures(1, &entry.second.texture);
  }
  state->scene_image_textures.clear();
  if (state->shadow_depth_texture)
    glDeleteTextures(1, &state->shadow_depth_texture);
  if (state->shadow_fbo)
    glDeleteFramebuffers(1, &state->shadow_fbo);
  if (state->billboard_vbo)
    glDeleteBuffers(1, &state->billboard_vbo);
  if (state->billboard_vao)
    glDeleteVertexArrays(1, &state->billboard_vao);
  if (state->sky_vbo)
    glDeleteBuffers(1, &state->sky_vbo);
  if (state->sky_vao)
    glDeleteVertexArrays(1, &state->sky_vao);
  if (state->sun_vbo)
    glDeleteBuffers(1, &state->sun_vbo);
  if (state->sun_vao)
    glDeleteVertexArrays(1, &state->sun_vao);
  if (state->ground_overlay_ebo)
    glDeleteBuffers(1, &state->ground_overlay_ebo);
  if (state->ground_overlay_vbo)
    glDeleteBuffers(1, &state->ground_overlay_vbo);
  if (state->ground_overlay_vao)
    glDeleteVertexArrays(1, &state->ground_overlay_vao);
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
  if (state->billboard_program)
    glDeleteProgram(state->billboard_program);
  if (state->sky_program)
    glDeleteProgram(state->sky_program);
  if (state->sun_program)
    glDeleteProgram(state->sun_program);

  state->ultra_texture = 0;
  state->texture = 0;
  state->mid_texture = 0;
  state->base_texture = 0;
  state->globe_texture = 0;
  state->model_texture = 0;
  state->shadow_depth_texture = 0;
  state->shadow_fbo = 0;
  state->billboard_vbo = 0;
  state->billboard_vao = 0;
  state->sky_vbo = 0;
  state->sky_vao = 0;
  state->sun_vbo = 0;
  state->sun_vao = 0;
  state->ground_overlay_ebo = 0;
  state->ground_overlay_vbo = 0;
  state->ground_overlay_vao = 0;
  state->globe_ebo = 0;
  state->globe_vbo = 0;
  state->globe_vao = 0;
  state->ebo = 0;
  state->vbo = 0;
  state->vao = 0;
  state->program = 0;
  state->shadow_program = 0;
  state->billboard_program = 0;
  state->sky_program = 0;
  state->sun_program = 0;
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
                        2,
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
                        reinterpret_cast<void *>(11 * sizeof(float)));
  glEnableVertexAttribArray(6);
  glVertexAttribPointer(6,
                        3,
                        GL_FLOAT,
                        GL_FALSE,
                        kVertexStride * sizeof(float),
                        reinterpret_cast<void *>(14 * sizeof(float)));
  glEnableVertexAttribArray(7);
  glVertexAttribPointer(7,
                        1,
                        GL_FLOAT,
                        GL_FALSE,
                        kVertexStride * sizeof(float),
                        reinterpret_cast<void *>(17 * sizeof(float)));
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

bool
upload_mesh_if_needed(GWorldSceneViewState *state, PerfBufferUploadStats *stats = nullptr)
{
  if (!state->mesh_dirty)
    return false;

  const gint64 start_us = perf_now_us();
  const std::size_t vertex_bytes = state->vertices.size() * sizeof(float);
  const std::size_t index_bytes = state->indices.size() * sizeof(unsigned int);
  upload_vertex_index_buffers(state->vao,
                              state->vbo,
                              state->ebo,
                              state->vertices,
                              state->indices,
                              state->index_count);
  state->mesh_dirty = false;
  if (stats != nullptr) {
    stats->uploaded = true;
    stats->duration_ms = perf_elapsed_ms(start_us);
    stats->vertex_bytes = vertex_bytes;
    stats->index_bytes = index_bytes;
    stats->indices = state->index_count;
  }
  return true;
}

bool
upload_globe_mesh_if_needed(GWorldSceneViewState *state, PerfBufferUploadStats *stats = nullptr)
{
  if (!state->globe_mesh_dirty)
    return false;

  const gint64 start_us = perf_now_us();
  const std::size_t vertex_bytes = state->globe_vertices.size() * sizeof(float);
  const std::size_t index_bytes = state->globe_indices.size() * sizeof(unsigned int);
  upload_vertex_index_buffers(state->globe_vao,
                              state->globe_vbo,
                              state->globe_ebo,
                              state->globe_vertices,
                              state->globe_indices,
                              state->globe_index_count);
  state->globe_mesh_dirty = false;
  if (stats != nullptr) {
    stats->uploaded = true;
    stats->duration_ms = perf_elapsed_ms(start_us);
    stats->vertex_bytes = vertex_bytes;
    stats->index_bytes = index_bytes;
    stats->indices = state->globe_index_count;
  }
  return true;
}

bool
upload_texture_buffer_if_needed(GLuint &texture,
                                const char *label,
                                const std::vector<unsigned char> &pixels,
                                int width,
                                int height,
                                bool &dirty,
                                PerfTextureUploadStats *stats = nullptr)
{
  if (!dirty)
    return false;

  if (pixels.empty() || width <= 0 || height <= 0) {
    if (texture) {
      glDeleteTextures(1, &texture);
      texture = 0;
      g_debug("Texture upload cleared: layer=%s", label);
    }
    dirty = false;
    return false;
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
    return false;
  }

  if (texture == 0)
    glGenTextures(1, &texture);

  const gint64 start_us = perf_now_us();
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
  const double duration_ms = perf_elapsed_ms(start_us);

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
  if (stats != nullptr) {
    stats->attempted = true;
    stats->uploaded = true;
    stats->layer = label;
    stats->duration_ms = duration_ms;
    stats->width = width;
    stats->height = height;
    stats->bytes = expected_size;
    stats->error = error;
  }
  return true;
}

bool
upload_textures_if_needed(GWorldSceneViewState *state, PerfTextureUploadStats *stats = nullptr)
{
  bool uploaded_this_frame = false;
  bool has_more_uploads = false;

  auto upload_layer = [&](GLuint &texture,
                          const char *label,
                          const std::vector<unsigned char> &pixels,
                          int width,
                          int height,
                          bool &dirty) {
    if (!dirty)
      return;

    if (uploaded_this_frame && !pixels.empty() && width > 0 && height > 0) {
      has_more_uploads = true;
      if (stats != nullptr)
        stats->deferred = true;
      return;
    }

    if (upload_texture_buffer_if_needed(texture, label, pixels, width, height, dirty, stats))
      uploaded_this_frame = true;
  };

  upload_layer(state->base_texture,
               "base",
               state->base_texture_pixels,
               state->base_texture_width,
               state->base_texture_height,
               state->base_texture_dirty);
  upload_layer(state->mid_texture,
               "mid",
               state->mid_texture_pixels,
               state->mid_texture_width,
               state->mid_texture_height,
               state->mid_texture_dirty);
  upload_layer(state->texture,
               "detail",
               state->texture_pixels,
               state->texture_width,
               state->texture_height,
               state->texture_dirty);
  upload_layer(state->ultra_texture,
               "ultra",
               state->ultra_texture_pixels,
               state->ultra_texture_width,
               state->ultra_texture_height,
               state->ultra_texture_dirty);
  upload_layer(state->globe_texture,
               "globe",
               state->globe_texture_pixels,
               state->globe_texture_width,
               state->globe_texture_height,
               state->globe_texture_dirty);
  upload_layer(state->model_texture,
               "model",
               state->model_texture_pixels,
               state->model_texture_width,
               state->model_texture_height,
               state->model_texture_dirty);

  return has_more_uploads;
}

void
set_atlas_uniforms(GLuint program, const char *prefix, const AtlasRange &range)
{
  const std::string base(prefix);
  glUniform1i(glGetUniformLocation(program, (base + "_atlas_valid").c_str()), range.valid());
  glUniform4f(glGetUniformLocation(program, (base + "_atlas_range").c_str()),
              static_cast<float>(range.z),
              static_cast<float>(range.x_min),
              static_cast<float>(range.y_min),
              0.0f);
  glUniform2f(glGetUniformLocation(program, (base + "_atlas_size").c_str()),
              static_cast<float>(range.width_tiles()),
              static_cast<float>(range.height_tiles()));
}

} // namespace

static gboolean scheduled_scene_request_cb(gpointer user_data);
void cancel_all_pending_downloads_locked(GWorldSceneViewState *state);
void cancel_pending_downloads_with_prefix_locked(GWorldSceneViewState *state,
                                                const std::string &prefix);
void remove_texture_build_cancellable_locked(GWorldSceneViewState *state,
                                             const std::string &pending_key);
void cancel_texture_build_locked(GWorldSceneViewState *state,
                                 const std::string &pending_key);
void cancel_all_texture_builds_locked(GWorldSceneViewState *state);
void cancel_world_mesh_build_locked(GWorldSceneViewState *state);

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
  if (state_ != nullptr) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    cancel_all_pending_downloads_locked(state_);
    cancel_all_texture_builds_locked(state_);
    cancel_world_mesh_build_locked(state_);
  }
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

enum {
  SIGNAL_NODE_CLICKED,
  SIGNAL_NODE_DOUBLE_CLICKED,
  SIGNAL_GROUND_CLICKED,
  SIGNAL_GROUND_DOUBLE_CLICKED,
  N_SIGNALS,
};

static guint signals[N_SIGNALS];

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

static ScenePickResult
pick_scene_at_widget_point(GWorldSceneView *self, double widget_x, double widget_y)
{
  ScenePickResult result;
  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    pick_scene_locked(state, GTK_WIDGET(self), widget_x, widget_y, result);
    if (result.node != nullptr)
      g_object_ref(result.node);
  }
  return result;
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

static void
world_mesh_build_job_free(WorldMeshBuildJob *job)
{
  delete job;
}

static void
world_mesh_build_result_free(WorldMeshBuildResult *result)
{
  delete result;
}

static void
world_mesh_build_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  (void)source_object;

  const gint64 build_start_us = perf_now_us();
  auto *job = static_cast<WorldMeshBuildJob *>(task_data);
  if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_CANCELLED,
                            "World mesh build cancelled: %s",
                            job->key.c_str());
    return;
  }

  GWorldSceneViewState build_state;
  build_state.latitude = job->latitude;
  build_state.longitude = job->longitude;
  build_state.altitude_amsl = job->altitude_amsl;
  build_state.mesh_origin_latitude = job->mesh_origin_latitude;
  build_state.mesh_origin_longitude = job->mesh_origin_longitude;
  build_state.terrain_revision = job->terrain_revision;
  build_state.scene_revision = job->scene_revision;
  if (!job->current_mesh_empty)
    build_state.vertices.push_back(0.0f);

  for (const auto &tile : job->terrain_tiles) {
    if (tile != nullptr)
      build_state.terrain_tiles[terrain_key(tile->lat, tile->lon)] = tile;
  }

  rebuild_world_mesh(&build_state,
                     job->ultra_range,
                     job->detail_range,
                     job->mid_range,
                     job->base_range,
                     job->ultra_lateral_radius_m,
                     job->include_terrain);

  if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_CANCELLED,
                            "World mesh build cancelled: %s",
                            job->key.c_str());
    return;
  }

  auto *result = new WorldMeshBuildResult;
  result->key = build_state.mesh_key;
  result->vertices = std::move(build_state.vertices);
  result->indices = std::move(build_state.indices);
  result->include_terrain = job->include_terrain;
  result->ultra_range = job->ultra_range;
  result->detail_range = job->detail_range;
  result->mid_range = job->mid_range;
  result->base_range = job->base_range;
  result->ultra_lateral_radius_m = job->ultra_lateral_radius_m;
  result->mesh_origin_latitude = build_state.mesh_origin_latitude;
  result->mesh_origin_longitude = build_state.mesh_origin_longitude;
  result->mesh_lod_altitude_amsl = build_state.mesh_lod_altitude_amsl;
  result->mesh_radius_m = build_state.mesh_radius_m;
  result->mesh_sample_step = build_state.mesh_sample_step;
  result->build_duration_us = perf_now_us() - build_start_us;

  g_task_return_pointer(task,
                        result,
                        reinterpret_cast<GDestroyNotify>(world_mesh_build_result_free));
}

static void
world_mesh_build_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  (void)user_data;

  auto *self = GWORLD_SCENE_VIEW(source_object);
  auto *state = get_state(self);
  auto *task = G_TASK(result);
  auto *job = static_cast<WorldMeshBuildJob *>(g_task_get_task_data(task));

  g_autoptr(GError) error = nullptr;
  auto *mesh_result = static_cast<WorldMeshBuildResult *>(g_task_propagate_pointer(task, &error));
  if (mesh_result == nullptr) {
    bool should_refresh = false;
    if (job != nullptr) {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->pending_mesh_key == job->key) {
        if (state->pending_mesh_cancellable != nullptr) {
          g_object_unref(state->pending_mesh_cancellable);
          state->pending_mesh_cancellable = nullptr;
        }
        state->pending_mesh_key.clear();
        should_refresh = state->mesh_build_refresh_needed;
        state->mesh_build_refresh_needed = false;
      }
    }
    if (should_refresh)
      schedule_scene_requests(self, 20);
    return;
  }

  bool applied = false;
  bool should_refresh = false;
  bool cleared_pending = false;
  std::size_t vertex_count = 0;
  std::size_t index_count = 0;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->pending_mesh_key == mesh_result->key) {
      if (state->pending_mesh_cancellable != nullptr) {
        g_object_unref(state->pending_mesh_cancellable);
        state->pending_mesh_cancellable = nullptr;
      }
      state->pending_mesh_key.clear();
      cleared_pending = true;
    }

    should_refresh = state->mesh_build_refresh_needed;
    state->mesh_build_refresh_needed = false;

    double wanted_origin_latitude = 0.0;
    double wanted_origin_longitude = 0.0;
    const bool include_terrain = state->altitude_amsl < kTerrainDisableAltitudeM;
    const std::string wanted_key =
      world_mesh_key_for_state_locked(state, include_terrain, wanted_origin_latitude, wanted_origin_longitude);

    if (!should_refresh && cleared_pending && wanted_key == mesh_result->key) {
      const bool origin_changed =
        std::abs(state->mesh_origin_latitude - mesh_result->mesh_origin_latitude) > 0.0000001 ||
        std::abs(state->mesh_origin_longitude - mesh_result->mesh_origin_longitude) > 0.0000001;
      state->vertices = std::move(mesh_result->vertices);
      state->indices = std::move(mesh_result->indices);
      state->mesh_origin_latitude = mesh_result->mesh_origin_latitude;
      state->mesh_origin_longitude = mesh_result->mesh_origin_longitude;
      state->mesh_key = mesh_result->key;
      state->mesh_includes_terrain = mesh_result->include_terrain;
      state->mesh_ultra_atlas_range = mesh_result->ultra_range;
      state->mesh_atlas_range = mesh_result->detail_range;
      state->mesh_mid_atlas_range = mesh_result->mid_range;
      state->mesh_base_atlas_range = mesh_result->base_range;
      state->ultra_texture_lateral_radius_m = mesh_result->ultra_lateral_radius_m;
      state->mesh_lod_altitude_amsl = mesh_result->mesh_lod_altitude_amsl;
      state->mesh_radius_m = mesh_result->mesh_radius_m;
      state->mesh_sample_step = mesh_result->mesh_sample_step;
      append_scene_nodes(state);
      state->mesh_dirty = true;
      if (origin_changed)
        state->globe_mesh_key.clear();
      vertex_count = state->vertices.size() / kVertexStride;
      index_count = state->indices.size();
      applied = true;
    } else {
      should_refresh = true;
    }
  }

  if (perf_enabled() &&
      (perf_verbose() || applied || perf_elapsed_ms(0, mesh_result->build_duration_us) >= perf_scene_threshold_ms())) {
    g_message("GWorldScene perf mesh-build applied=%d key=%s build=%.2fms vertices=%zu indices=%zu refresh=%d",
              applied ? 1 : 0,
              mesh_result->key.c_str(),
              perf_elapsed_ms(0, mesh_result->build_duration_us),
              vertex_count,
              index_count,
              should_refresh ? 1 : 0);
  }

  world_mesh_build_result_free(mesh_result);
  if (applied)
    gtk_widget_queue_draw(GTK_WIDGET(self));
  if (should_refresh)
    schedule_scene_requests(self, 20);
}

static bool
request_world_mesh_build(GWorldSceneView *self,
                         const AtlasRange &ultra_range,
                         const AtlasRange &detail_range,
                         const AtlasRange &mid_range,
                         const AtlasRange &base_range,
                         double ultra_lateral_radius_m,
                         bool include_terrain)
{
  auto *state = get_state(self);
  auto *job = new WorldMeshBuildJob;
  GCancellable *cancellable = nullptr;

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    double wanted_origin_latitude = 0.0;
    double wanted_origin_longitude = 0.0;
    const std::string key =
      world_mesh_key_for_state_locked(state, include_terrain, wanted_origin_latitude, wanted_origin_longitude);

    state->ultra_texture_lateral_radius_m = ultra_lateral_radius_m;
    state->mesh_ultra_atlas_range = ultra_range;
    state->mesh_atlas_range = detail_range;
    state->mesh_mid_atlas_range = mid_range;
    state->mesh_base_atlas_range = base_range;

    if (state->mesh_key == key && !state->vertices.empty()) {
      delete job;
      return false;
    }

    if (!state->pending_mesh_key.empty()) {
      if (state->pending_mesh_key != key)
        state->mesh_build_refresh_needed = true;
      delete job;
      return false;
    }

    job->latitude = state->latitude;
    job->longitude = state->longitude;
    job->altitude_amsl = state->altitude_amsl;
    job->mesh_origin_latitude = state->mesh_origin_latitude;
    job->mesh_origin_longitude = state->mesh_origin_longitude;
    job->current_mesh_empty = state->vertices.empty();
    job->include_terrain = include_terrain;
    job->ultra_range = ultra_range;
    job->detail_range = detail_range;
    job->mid_range = mid_range;
    job->base_range = base_range;
    job->ultra_lateral_radius_m = ultra_lateral_radius_m;
    job->terrain_revision = state->terrain_revision;
    job->scene_revision = state->scene_revision;
    job->key = key;
    job->terrain_tiles.reserve(state->terrain_tiles.size());
    for (const auto &entry : state->terrain_tiles)
      job->terrain_tiles.push_back(entry.second);

    cancellable = g_cancellable_new();
    state->pending_mesh_key = key;
    state->pending_mesh_cancellable = cancellable;
    state->mesh_build_refresh_needed = false;
  }

  GTask *task = g_task_new(self, cancellable, world_mesh_build_done, nullptr);
  g_task_set_task_data(task, job, reinterpret_cast<GDestroyNotify>(world_mesh_build_job_free));
  g_task_run_in_thread(task, world_mesh_build_thread);
  g_object_unref(task);
  return true;
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

  const gint64 build_start_us = perf_now_us();
  auto *job = static_cast<TextureAtlasBuildJob *>(task_data);
  if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_CANCELLED,
                            "Texture atlas build cancelled: %s",
                            job->pending_key.c_str());
    return;
  }

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
      if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
        texture_atlas_build_result_free(result);
        g_task_return_new_error(task,
                                G_IO_ERROR,
                                G_IO_ERROR_CANCELLED,
                                "Texture atlas build cancelled: %s",
                                job->pending_key.c_str());
        return;
      }

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

      if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
        texture_atlas_build_result_free(result);
        g_task_return_new_error(task,
                                G_IO_ERROR,
                                G_IO_ERROR_CANCELLED,
                                "Texture atlas build cancelled: %s",
                                job->pending_key.c_str());
        return;
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

  result->build_duration_us = perf_now_us() - build_start_us;
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
  auto *job = static_cast<TextureAtlasBuildJob *>(g_task_get_task_data(task));

  g_autoptr(GError) error = nullptr;
  auto *atlas_result = static_cast<TextureAtlasBuildResult *>(g_task_propagate_pointer(task, &error));
  if (atlas_result == nullptr) {
    if (job != nullptr) {
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        auto &pending_key = texture_layer_pending_key(state, job->layer);
        if (pending_key == job->pending_key)
          pending_key.clear();
        remove_texture_build_cancellable_locked(state, job->pending_key);
      }

      if (error == nullptr ||
          error->domain != G_IO_ERROR ||
          error->code != G_IO_ERROR_CANCELLED) {
        g_debug("Texture atlas build failed: layer=%s pending=%s error=%s",
                texture_layer_name(job->layer),
                job->pending_key.c_str(),
                error ? error->message : "unknown error");
        ensure_scene_requests_scheduled(self, 20);
      }
    }
    return;
  }

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
    remove_texture_build_cancellable_locked(state, atlas_result->pending_key);
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

  if (perf_enabled() && (perf_verbose() || applied ||
                         perf_elapsed_ms(0, atlas_result->build_duration_us) >= perf_scene_threshold_ms())) {
    g_message("GWorldScene perf atlas layer=%s applied=%d range=%s build=%.2fms loaded=%d missing=%d decode_failed=%d size=%dx%d bytes=%.1fMiB pending_cleared=%d",
              texture_layer_name(atlas_result->layer),
              applied ? 1 : 0,
              range_key.c_str(),
              perf_elapsed_ms(0, atlas_result->build_duration_us),
              atlas_result->loaded_count,
              atlas_result->missing_count,
              atlas_result->decode_failed_count,
              atlas_result->width,
              atlas_result->height,
              bytes_to_mib(static_cast<std::size_t>(std::max(0, atlas_result->width)) *
                           static_cast<std::size_t>(std::max(0, atlas_result->height)) * 4),
              cleared_pending ? 1 : 0);
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
    if (!pending_key.empty())
      cancel_texture_build_locked(state, pending_key);
    pixels.clear();
    width = 0;
    height = 0;
    dirty = true;
    loaded_key.clear();
    wanted_key.clear();
    pending_key.clear();
    active_range = AtlasRange();
    return;
  }

  const std::string range_key = range.key();
  const std::string pending_key = std::string(texture_layer_name(layer)) + ":" + range_key;
  std::string cache_dir;
  std::string texture_template;
  guint64 source_revision = 0;
  GCancellable *cancellable = nullptr;

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
      if (pending_layer_key == pending_key) {
        g_debug("Texture atlas build already pending: layer=%s wanted=%s pending=%s",
                texture_layer_name(layer),
                range_key.c_str(),
                pending_layer_key.c_str());
        return;
      }

      g_debug("Texture atlas build cancelled for newer range: layer=%s wanted=%s old_pending=%s",
              texture_layer_name(layer),
              range_key.c_str(),
              pending_layer_key.c_str());
      cancel_texture_build_locked(state, pending_layer_key);
      pending_layer_key.clear();
    }

    cancellable = g_cancellable_new();
    pending_layer_key = pending_key;
    state->pending_texture_build_cancellables[pending_key] = cancellable;
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

  GTask *task = g_task_new(self, cancellable, texture_atlas_build_done, nullptr);
  g_task_set_task_data(task, job, reinterpret_cast<GDestroyNotify>(texture_atlas_build_job_free));
  g_task_run_in_thread(task, texture_atlas_build_thread);
  g_object_unref(task);
}

static void
download_job_free(DownloadJob *job)
{
  delete job;
}

static void
terrain_load_job_free(TerrainLoadJob *job)
{
  delete job;
}

static void
terrain_load_result_free(TerrainLoadResult *result)
{
  delete result;
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

int
count_pending_downloads_with_prefix_locked(GWorldSceneViewState *state, const std::string &prefix)
{
  int count = 0;
  for (const std::string &pending_key : state->pending) {
    if (pending_key.rfind(prefix, 0) == 0)
      ++count;
  }
  return count;
}

void
clear_loaded_texture_keys_locked(GWorldSceneViewState *state)
{
  state->loaded_ultra_texture_key.clear();
  state->loaded_texture_key.clear();
  state->loaded_mid_texture_key.clear();
  state->loaded_base_texture_key.clear();
  state->loaded_globe_texture_key.clear();
}

bool
flush_pending_texture_refresh_locked(GWorldSceneViewState *state, gint64 now_us)
{
  if (!state->texture_refresh_pending)
    return false;

  const int pending_textures = count_pending_downloads_with_prefix_locked(state, "texture:");
  const bool cooldown_elapsed =
    state->last_texture_refresh_us == 0 ||
    now_us - state->last_texture_refresh_us >= kTextureAtlasRefreshMinIntervalUs;
  if (pending_textures > 0 && !cooldown_elapsed)
    return false;

  clear_loaded_texture_keys_locked(state);
  state->texture_refresh_pending = false;
  state->last_texture_refresh_us = now_us;
  return true;
}

bool
remove_pending_download_locked(GWorldSceneViewState *state, const std::string &pending_key)
{
  const bool was_pending = state->pending.erase(pending_key) > 0;
  auto cancellable_iter = state->pending_cancellables.find(pending_key);
  if (cancellable_iter != state->pending_cancellables.end()) {
    g_object_unref(cancellable_iter->second);
    state->pending_cancellables.erase(cancellable_iter);
  }
  return was_pending;
}

void
cancel_pending_download_locked(GWorldSceneViewState *state, const std::string &pending_key)
{
  auto cancellable_iter = state->pending_cancellables.find(pending_key);
  if (cancellable_iter != state->pending_cancellables.end()) {
    g_cancellable_cancel(cancellable_iter->second);
    g_object_unref(cancellable_iter->second);
    state->pending_cancellables.erase(cancellable_iter);
  }
  state->pending.erase(pending_key);
}

void
cancel_pending_downloads_not_in_locked(GWorldSceneViewState *state,
                                       const std::string &prefix,
                                       const std::unordered_set<std::string> &wanted_pending_keys)
{
  std::vector<std::string> stale_keys;
  for (const std::string &pending_key : state->pending) {
    if (pending_key.rfind(prefix, 0) == 0 &&
        wanted_pending_keys.find(pending_key) == wanted_pending_keys.end()) {
      stale_keys.push_back(pending_key);
    }
  }

  for (const std::string &pending_key : stale_keys)
    cancel_pending_download_locked(state, pending_key);
}

void
cancel_pending_downloads_with_prefix_locked(GWorldSceneViewState *state,
                                            const std::string &prefix)
{
  std::vector<std::string> pending_keys;
  for (const std::string &pending_key : state->pending) {
    if (pending_key.rfind(prefix, 0) == 0)
      pending_keys.push_back(pending_key);
  }

  for (const std::string &pending_key : pending_keys)
    cancel_pending_download_locked(state, pending_key);
}

void
cancel_all_pending_downloads_locked(GWorldSceneViewState *state)
{
  std::vector<std::string> pending_keys(state->pending.begin(), state->pending.end());
  for (const std::string &pending_key : pending_keys)
    cancel_pending_download_locked(state, pending_key);
}

void
remove_texture_build_cancellable_locked(GWorldSceneViewState *state,
                                        const std::string &pending_key)
{
  auto iter = state->pending_texture_build_cancellables.find(pending_key);
  if (iter != state->pending_texture_build_cancellables.end()) {
    g_object_unref(iter->second);
    state->pending_texture_build_cancellables.erase(iter);
  }
}

void
cancel_texture_build_locked(GWorldSceneViewState *state, const std::string &pending_key)
{
  auto iter = state->pending_texture_build_cancellables.find(pending_key);
  if (iter != state->pending_texture_build_cancellables.end()) {
    g_cancellable_cancel(iter->second);
    g_object_unref(iter->second);
    state->pending_texture_build_cancellables.erase(iter);
  }
}

void
cancel_texture_build_for_layer_locked(GWorldSceneViewState *state, TextureLayer layer)
{
  auto &pending_key = texture_layer_pending_key(state, layer);
  if (!pending_key.empty()) {
    cancel_texture_build_locked(state, pending_key);
    pending_key.clear();
  }
}

void
cancel_all_texture_builds_locked(GWorldSceneViewState *state)
{
  cancel_texture_build_for_layer_locked(state, TextureLayer::Ultra);
  cancel_texture_build_for_layer_locked(state, TextureLayer::Detail);
  cancel_texture_build_for_layer_locked(state, TextureLayer::Mid);
  cancel_texture_build_for_layer_locked(state, TextureLayer::Base);
  cancel_texture_build_for_layer_locked(state, TextureLayer::Globe);

  for (auto &entry : state->pending_texture_build_cancellables) {
    g_cancellable_cancel(entry.second);
    g_object_unref(entry.second);
  }
  state->pending_texture_build_cancellables.clear();
}

void
cancel_world_mesh_build_locked(GWorldSceneViewState *state)
{
  if (state->pending_mesh_cancellable != nullptr) {
    g_cancellable_cancel(state->pending_mesh_cancellable);
    g_object_unref(state->pending_mesh_cancellable);
    state->pending_mesh_cancellable = nullptr;
  }
  state->pending_mesh_key.clear();
  state->mesh_build_refresh_needed = false;
}

static void
terrain_load_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  (void)source_object;

  auto *job = static_cast<TerrainLoadJob *>(task_data);
  if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_CANCELLED,
                            "Terrain load cancelled: %s",
                            job->pending_key.c_str());
    return;
  }

  auto *result = new TerrainLoadResult;
  result->path = job->path;
  result->pending_key = job->pending_key;
  result->source = job->source;
  result->tile.lat = job->terrain_lat;
  result->tile.lon = job->terrain_lon;

  const gint64 parse_start_us = perf_now_us();
  if (!read_hgt_file(job->path, result->tile.heights, result->tile.dimension)) {
    terrain_load_result_free(result);
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_FAILED,
                            "Failed to parse terrain tile: %s",
                            job->path.c_str());
    return;
  }
  result->parse_duration_us = perf_now_us() - parse_start_us;

  if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
    terrain_load_result_free(result);
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_CANCELLED,
                            "Terrain load cancelled: %s",
                            job->pending_key.c_str());
    return;
  }

  g_task_return_pointer(task,
                        result,
                        reinterpret_cast<GDestroyNotify>(terrain_load_result_free));
}

static void
terrain_load_done(GObject *source_object, GAsyncResult *result, gpointer user_data)
{
  (void)user_data;

  auto *self = GWORLD_SCENE_VIEW(source_object);
  auto *task = G_TASK(result);
  auto *job = static_cast<TerrainLoadJob *>(g_task_get_task_data(task));
  auto *state = get_state(self);

  g_autoptr(GError) error = nullptr;
  auto *terrain_result = static_cast<TerrainLoadResult *>(g_task_propagate_pointer(task, &error));
  if (terrain_result == nullptr) {
    if (job != nullptr) {
      std::lock_guard<std::mutex> lock(state->mutex);
      remove_pending_download_locked(state, job->pending_key);
      if (job->source == "download") {
        const std::string key = terrain_key(job->terrain_lat, job->terrain_lon);
        note_terrain_download_failure_locked(state, key, false, g_get_monotonic_time());
      }
    }
    if (error == nullptr ||
        error->domain != G_IO_ERROR ||
        error->code != G_IO_ERROR_CANCELLED) {
      g_warning("Terrain parse failed: key=%s path=%s error=%s",
                job ? job->pending_key.c_str() : "(unknown)",
                job ? job->path.c_str() : "(unknown)",
                error ? error->message : "unknown error");
    }
    return;
  }

  bool applied = false;
  bool was_pending = false;
  const std::string key = terrain_key(terrain_result->tile.lat, terrain_result->tile.lon);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    was_pending = remove_pending_download_locked(state, terrain_result->pending_key);
    const bool still_wanted =
      state->wanted_terrain_keys.empty() ||
      state->wanted_terrain_keys.find(key) != state->wanted_terrain_keys.end();
    if (was_pending && still_wanted) {
      state->terrain_tiles[key] =
        std::make_shared<TerrainTile>(std::move(terrain_result->tile));
      note_terrain_download_success_locked(state, key);
      ++state->terrain_revision;
      state->mesh_key.clear();
      applied = true;
    }
  }

  const double parse_ms = perf_elapsed_ms(0, terrain_result->parse_duration_us);
  if (perf_enabled() && (perf_verbose() || parse_ms >= perf_scene_threshold_ms())) {
    g_message("GWorldScene perf terrain-parse source=%s applied=%d key=%s parse=%.2fms path=%s",
              terrain_result->source.c_str(),
              applied ? 1 : 0,
              key.c_str(),
              parse_ms,
              terrain_result->path.c_str());
  }

  terrain_load_result_free(terrain_result);
  if (applied) {
    schedule_scene_requests(self, 120);
    gtk_widget_queue_draw(GTK_WIDGET(self));
  }
}

static bool
start_terrain_load(GWorldSceneView *self,
                   const std::string &source,
                   const std::string &pending_key,
                   const std::string &path,
                   int terrain_lat,
                   int terrain_lon)
{
  auto *state = get_state(self);
  auto *cancellable = g_cancellable_new();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->terrain_tiles.find(terrain_key(terrain_lat, terrain_lon)) != state->terrain_tiles.end() ||
        state->pending.find(pending_key) != state->pending.end()) {
      g_object_unref(cancellable);
      return false;
    }

    state->pending.insert(pending_key);
    state->pending_cancellables[pending_key] = cancellable;
  }

  auto *job = new TerrainLoadJob;
  job->source = source;
  job->pending_key = pending_key;
  job->path = path;
  job->terrain_lat = terrain_lat;
  job->terrain_lon = terrain_lon;

  GTask *task = g_task_new(self, cancellable, terrain_load_done, nullptr);
  g_task_set_task_data(task, job, reinterpret_cast<GDestroyNotify>(terrain_load_job_free));
  g_task_run_in_thread(task, terrain_load_thread);
  g_object_unref(task);
  return true;
}

static void
download_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
  (void)source_object;
  auto *job = static_cast<DownloadJob *>(task_data);

  if (cancellable != nullptr && g_cancellable_is_cancelled(cancellable)) {
    g_task_return_new_error(task,
                            G_IO_ERROR,
                            G_IO_ERROR_CANCELLED,
                            "Download cancelled: %s",
                            job->pending_key.c_str());
    return;
  }

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
  const bool cancelled = error != nullptr &&
                         error->domain == G_IO_ERROR &&
                         error->code == G_IO_ERROR_CANCELLED;
  bool was_pending = false;

  {
    std::lock_guard<std::mutex> lock(state->mutex);
    was_pending = remove_pending_download_locked(state, job->pending_key);
  }

  if (!was_pending) {
    if (!cancelled) {
      g_debug("Stale download ignored: key=%s uri=%s cache=%s ok=%s",
              job->pending_key.c_str(),
              job->uri.c_str(),
              job->path.c_str(),
              ok ? "yes" : "no");
    }
    return;
  }

  if (!ok) {
    if (cancelled) {
      g_debug("Download cancelled: key=%s uri=%s cache=%s",
              job->pending_key.c_str(),
              job->uri.c_str(),
              job->path.c_str());
      return;
    }

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
    start_terrain_load(self,
                       "download",
                       job->pending_key,
                       job->path,
                       job->terrain_lat,
                       job->terrain_lon);
    return;
  } else if (job->kind == "texture") {
    g_debug("Texture download finished: key=%s uri=%s cache=%s",
            job->pending_key.c_str(),
            job->uri.c_str(),
            job->path.c_str());
    bool refreshed = false;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->texture_refresh_pending = true;
      refreshed = flush_pending_texture_refresh_locked(state, g_get_monotonic_time());
    }
    schedule_scene_requests(self, refreshed ? 120 : 250);
    gtk_widget_queue_draw(GTK_WIDGET(self));
    return;
  }

  schedule_scene_requests(self, 120);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

static bool
start_download(GWorldSceneView *self,
               const std::string &kind,
               const std::string &pending_key,
               const std::string &uri,
               const std::string &path,
               int terrain_lat,
               int terrain_lon)
{
  auto *state = get_state(self);
  auto *cancellable = g_cancellable_new();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->pending.find(pending_key) != state->pending.end()) {
      g_object_unref(cancellable);
      if (kind == "texture") {
        g_debug("Texture download already pending: key=%s uri=%s cache=%s",
                pending_key.c_str(),
                uri.c_str(),
                path.c_str());
      }
      return false;
    }
    state->pending.insert(pending_key);
    state->pending_cancellables[pending_key] = cancellable;
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

  GTask *task = g_task_new(self, cancellable, download_done, nullptr);
  g_task_set_task_data(task, job, reinterpret_cast<GDestroyNotify>(download_job_free));
  g_task_run_in_thread(task, download_thread);
  g_object_unref(task);
  return true;
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
  const std::string pending_key = "terrain:" + key;
  std::string cache_dir;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->terrain_tiles.find(key) != state->terrain_tiles.end() ||
        state->pending.find(pending_key) != state->pending.end())
      return true;
    cache_dir = state->cache_directory;
  }

  std::string load_path = path;
  const std::string raw_path = join_path(join_path(cache_dir, "terrain"), tile_name + ".hgt");
  const std::string zip_path = join_path(join_path(cache_dir, "terrain"), tile_name + ".hgt.zip");
  if (g_file_test(raw_path.c_str(), G_FILE_TEST_EXISTS))
    load_path = raw_path;
  else if (g_file_test(zip_path.c_str(), G_FILE_TEST_EXISTS))
    load_path = zip_path;
  else if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
    return false;

  return start_terrain_load(self, "cache", pending_key, load_path, tile_lat, tile_lon);
}

static void
queue_scene_requests(GWorldSceneView *self)
{
  const gint64 request_start_us = perf_now_us();
  auto *state = get_state(self);

  double latitude = 0.0;
  double longitude = 0.0;
  double altitude = 0.0;
  std::string cache_dir;
  std::string terrain_template;
  std::string texture_template;
  std::vector<LatLonBounds> overlay_terrain_bounds;
  bool cache_enabled = true;
  bool has_ground_height = false;
  double ground_height_amsl = 0.0;
  double initial_state_ms = 0.0;
  double refresh_ms = 0.0;
  double terrain_queue_ms = 0.0;
  double texture_queue_ms = 0.0;
  double mesh_rebuild_ms = 0.0;
  double atlas_request_ms = 0.0;
  int terrain_candidates_count = 0;
  int terrain_loads_count = 0;
  int terrain_downloads_count = 0;
  int pending_terrain_downloads_count = 0;
  int texture_candidates_count = 0;
  int texture_downloads_count = 0;
  int pending_texture_downloads_count = 0;
  bool texture_refresh_waiting = false;
  bool texture_refresh_flushed = false;
  bool mesh_rebuilt = false;
  bool mesh_build_queued = false;
  bool mesh_build_deferred = false;
  bool globe_mesh_rebuilt = false;
  std::size_t mesh_vertices_count = 0;
  std::size_t mesh_indices_count = 0;

  {
    const gint64 start_us = perf_now_us();
    std::lock_guard<std::mutex> lock(state->mutex);
    latitude = state->latitude;
    longitude = state->longitude;
    altitude = state->altitude_amsl;
    cache_dir = state->cache_directory;
    terrain_template = state->terrain_server;
    texture_template = state->map_tile_template;
    cache_enabled = state->cache_enabled;
    collect_ground_overlay_terrain_bounds_locked(state, overlay_terrain_bounds);
    has_ground_height = terrain_height_at_lat_lon_locked(state,
                                                        latitude,
                                                        longitude,
                                                        ground_height_amsl);
    initial_state_ms = perf_elapsed_ms(start_us);
  }

  {
    const gint64 start_us = perf_now_us();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->texture_refresh_pending) {
      texture_refresh_flushed = flush_pending_texture_refresh_locked(state, g_get_monotonic_time());
      texture_refresh_waiting = !texture_refresh_flushed;
    }
    refresh_ms = perf_elapsed_ms(start_us);
  }
  if (texture_refresh_waiting)
    ensure_scene_requests_scheduled(self, 250);

  const double radius_m = terrain_build_radius_for_altitude(altitude);
  const bool render_globe = altitude >= kGlobeRenderAltitudeM;
  const bool include_terrain = altitude < kTerrainDisableAltitudeM;
  const double altitude_agl = has_ground_height ? std::max(0.0, altitude - ground_height_amsl) : altitude;
  const double texture_lod_altitude = has_ground_height ? altitude_agl : altitude;
  const double ultra_bubble_lateral_radius_m = include_terrain
                                                 ? ultra_texture_lateral_radius_for_agl(altitude_agl)
                                                 : 0.0;
  const int ultra_texture_zoom = ultra_texture_zoom_for_altitude(texture_lod_altitude);
  double ultra_lateral_radius_m =
    ultra_texture_atlas_radius(latitude, ultra_texture_zoom, ultra_bubble_lateral_radius_m);
  const LatLonBounds terrain_bounds = bounds_around_camera(latitude, longitude, radius_m);
  const LatLonBounds texture_bounds =
    bounds_around_camera(latitude, longitude, texture_radius_for_altitude(texture_lod_altitude));
  const LatLonBounds mid_texture_bounds =
    bounds_around_camera(latitude, longitude, mid_texture_radius_for_altitude(texture_lod_altitude, radius_m));
  const AtlasRange atlas_range =
    select_atlas_range(texture_bounds, detail_texture_zoom_for_altitude(texture_lod_altitude));
  const AtlasRange ultra_atlas_range = ultra_lateral_radius_m > 0.0
                                         ? select_ultra_atlas_range(latitude,
                                                                    longitude,
                                                                    ultra_texture_zoom,
                                                                    ultra_lateral_radius_m)
                                         : AtlasRange();
  const AtlasRange mid_atlas_range =
    select_atlas_range(mid_texture_bounds, mid_texture_zoom_for_altitude(texture_lod_altitude));
  const AtlasRange base_atlas_range =
    select_atlas_range(terrain_bounds, base_texture_zoom_for_altitude(texture_lod_altitude));
  const AtlasRange globe_range = globe_atlas_range(latitude, longitude, altitude);

  if (cache_enabled) {
    const gint64 terrain_start_us = perf_now_us();
    if (include_terrain) {
      std::unordered_set<std::string> keep_keys;
      std::unordered_set<std::string> keep_pending_keys;
      std::unordered_set<std::string> candidate_keys;
      std::vector<TerrainCandidate> candidates;

      auto add_terrain_candidates_for_bounds = [&](const LatLonBounds &bounds) {
        const int lat_min = static_cast<int>(std::floor(bounds.min_lat));
        const int lat_max = static_cast<int>(std::floor(bounds.max_lat));
        const int lon_min = static_cast<int>(std::floor(bounds.min_lon));
        const int lon_max = static_cast<int>(std::floor(bounds.max_lon));

        for (int lat = lat_min; lat <= lat_max; ++lat) {
          for (int lon = lon_min; lon <= lon_max; ++lon) {
            if (lat < -90 || lat > 89 || lon < -180 || lon > 179)
              continue;

            const std::string key = terrain_key(lat, lon);
            keep_keys.insert(key);
            keep_pending_keys.insert("terrain:" + key);
            if (!candidate_keys.insert(key).second)
              continue;

            const glm::dvec3 center = geodetic_to_enu(static_cast<double>(lat) + 0.5,
                                                      static_cast<double>(lon) + 0.5,
                                                      0.0,
                                                      latitude,
                                                      longitude,
                                                      0.0);
            candidates.push_back({lat, lon, std::hypot(center.x, center.z)});
          }
        }
      };

      add_terrain_candidates_for_bounds(terrain_bounds);
      for (const LatLonBounds &bounds : overlay_terrain_bounds)
        add_terrain_candidates_for_bounds(bounds);

      std::sort(candidates.begin(), candidates.end(), [](const TerrainCandidate &a, const TerrainCandidate &b) {
        return a.distance < b.distance;
      });
      terrain_candidates_count = static_cast<int>(candidates.size());

      int terrain_loads = 0;
      int terrain_downloads = 0;
      int pending_terrain_downloads = 0;
      bool has_more_terrain_work = false;
      guint terrain_reschedule_ms = 60000;

      {
        std::lock_guard<std::mutex> lock(state->mutex);
        cancel_pending_downloads_not_in_locked(state, "terrain:", keep_pending_keys);
        pending_terrain_downloads = count_pending_downloads_with_prefix_locked(state, "terrain:");
      }
      pending_terrain_downloads_count = pending_terrain_downloads;

      for (const TerrainCandidate &candidate : candidates) {
        const int lat = candidate.lat;
        const int lon = candidate.lon;
        const std::string key = terrain_key(lat, lon);
        const std::string pending_key = "terrain:" + key;
        {
          std::lock_guard<std::mutex> lock(state->mutex);
          if (state->terrain_tiles.find(key) != state->terrain_tiles.end())
            continue;
          if (state->pending.find(pending_key) != state->pending.end())
            continue;
        }

        const std::string name = hgt_tile_name(lat, lon);
        const std::string uri = terrain_uri_from_template(terrain_template, name);
        const std::string path = terrain_cache_path_for_uri(cache_dir, uri, name);

        if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS) ||
            g_file_test(join_path(join_path(cache_dir, "terrain"), name + ".hgt").c_str(), G_FILE_TEST_EXISTS) ||
            g_file_test(join_path(join_path(cache_dir, "terrain"), name + ".hgt.zip").c_str(), G_FILE_TEST_EXISTS)) {
          if (terrain_loads >= kMaxTerrainTileLoadsPerUpdate ||
              pending_terrain_downloads + terrain_loads >= kMaxPendingTerrainDownloads) {
            has_more_terrain_work = true;
            terrain_reschedule_ms = 350;
            continue;
          }
          if (load_cached_terrain_tile(self, lat, lon, name, path))
            ++terrain_loads;
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

        if (terrain_downloads >= kMaxTerrainDownloadsPerUpdate ||
            pending_terrain_downloads + terrain_downloads >= kMaxPendingTerrainDownloads) {
          has_more_terrain_work = true;
          terrain_reschedule_ms = 350;
          continue;
        }

        if (start_download(self, "terrain", pending_key, uri, path, lat, lon))
          ++terrain_downloads;
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
        bool removed_terrain = false;
        for (auto it = state->terrain_tiles.begin(); it != state->terrain_tiles.end();) {
          if (keep_keys.find(it->first) == keep_keys.end()) {
            it = state->terrain_tiles.erase(it);
            removed_terrain = true;
          } else {
            ++it;
          }
        }
        if (removed_terrain) {
          ++state->terrain_revision;
          state->mesh_key.clear();
        }
      }
      terrain_loads_count = terrain_loads;
      terrain_downloads_count = terrain_downloads;
    } else {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->wanted_terrain_keys.clear();
      if (!state->terrain_tiles.empty()) {
        state->terrain_tiles.clear();
        ++state->terrain_revision;
        state->mesh_key.clear();
      }
    }
    terrain_queue_ms = perf_elapsed_ms(terrain_start_us);

    const gint64 texture_start_us = perf_now_us();
    std::unordered_set<std::string> wanted_texture_pending_keys;
    std::unordered_set<std::string> candidate_texture_keys;
    std::vector<TextureCandidate> texture_candidates;
    auto add_texture_range_candidates = [&](const AtlasRange &range, double layer_priority) {
      if (!range.valid())
        return;

      const double center_x = lon_to_tile_x(longitude, range.z);
      const double center_y = lat_to_tile_y(latitude, range.z);
      for (int ty = range.y_min; ty <= range.y_max; ++ty) {
        for (int tx = range.x_min; tx <= range.x_max; ++tx) {
          TileCoord tile{range.z, tx, ty};
          const std::string key = texture_download_key(tile);
          wanted_texture_pending_keys.insert(key);
          const std::string uri = texture_uri_from_template(texture_template, tile);
          const std::string path = texture_cache_path(cache_dir, uri, texture_template, tile);
          if (g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
            continue;
          if (!candidate_texture_keys.insert(key).second)
            continue;
          const double distance = std::hypot((static_cast<double>(tx) + 0.5) - center_x,
                                             (static_cast<double>(ty) + 0.5) - center_y);
          texture_candidates.push_back({tile, layer_priority + distance});
        }
      }
    };

    if (include_terrain) {
      add_texture_range_candidates(base_atlas_range, 0.0);
      add_texture_range_candidates(ultra_atlas_range, 0.04);
      add_texture_range_candidates(mid_atlas_range, 0.12);
      add_texture_range_candidates(atlas_range, 0.24);
    }
    if (render_globe)
      add_texture_range_candidates(globe_range, 0.0);

    int texture_downloads = 0;
    int pending_texture_downloads = 0;
    bool has_more_texture_work = false;

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      cancel_pending_downloads_not_in_locked(state, "texture:", wanted_texture_pending_keys);
      pending_texture_downloads = count_pending_downloads_with_prefix_locked(state, "texture:");
    }
    pending_texture_downloads_count = pending_texture_downloads;

    std::sort(texture_candidates.begin(),
              texture_candidates.end(),
              [](const TextureCandidate &a, const TextureCandidate &b) {
                return a.priority < b.priority;
              });
    texture_candidates_count = static_cast<int>(texture_candidates.size());

    for (const TextureCandidate &candidate : texture_candidates) {
      const std::string key = texture_download_key(candidate.tile);
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->pending.find(key) != state->pending.end())
          continue;
      }

      if (texture_downloads >= kMaxTextureDownloadsPerUpdate ||
          pending_texture_downloads + texture_downloads >= kMaxPendingTextureDownloads) {
        has_more_texture_work = true;
        continue;
      }

      const std::string uri = texture_uri_from_template(texture_template, candidate.tile);
      const std::string path = texture_cache_path(cache_dir, uri, texture_template, candidate.tile);
      if (start_download(self, "texture", key, uri, path, 0, 0))
        ++texture_downloads;
    }
    texture_downloads_count = texture_downloads;

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
    texture_queue_ms = perf_elapsed_ms(texture_start_us);
  }

  {
    const gint64 mesh_start_us = perf_now_us();
    mesh_build_deferred = include_terrain && terrain_loads_count > 0;
    if (!mesh_build_deferred) {
      mesh_build_queued = request_world_mesh_build(self,
                                                   ultra_atlas_range,
                                                   atlas_range,
                                                   mid_atlas_range,
                                                   base_atlas_range,
                                                   ultra_lateral_radius_m,
                                                   include_terrain);
    }
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      const AtlasRange mesh_globe_atlas_range =
        state->globe_texture_atlas_range.valid() ? state->globe_texture_atlas_range : globe_range;
      if (render_globe)
        globe_mesh_rebuilt = rebuild_globe_mesh(state, mesh_globe_atlas_range);
      mesh_vertices_count = state->vertices.size() / kVertexStride;
      mesh_indices_count = state->indices.size();
    }
    mesh_rebuild_ms = perf_elapsed_ms(mesh_start_us);
  }

  const gint64 atlas_start_us = perf_now_us();
  if (include_terrain) {
    request_texture_atlas_build(self, ultra_atlas_range, TextureLayer::Ultra);
    request_texture_atlas_build(self, base_atlas_range, TextureLayer::Base);
    request_texture_atlas_build(self, mid_atlas_range, TextureLayer::Mid);
    request_texture_atlas_build(self, atlas_range, TextureLayer::Detail);
  } else {
    request_texture_atlas_build(self, AtlasRange(), TextureLayer::Ultra);
  }
  if (render_globe)
    request_texture_atlas_build(self, globe_range, TextureLayer::Globe);
  atlas_request_ms = perf_elapsed_ms(atlas_start_us);
  const double total_ms = perf_elapsed_ms(request_start_us);
  if (perf_enabled() &&
      (perf_verbose() || total_ms >= perf_scene_threshold_ms() || mesh_rebuilt || mesh_build_queued || mesh_build_deferred || globe_mesh_rebuilt)) {
    g_message("GWorldScene perf scene total=%.2fms state=%.2f refresh=%.2f terrain=%.2f(candidates=%d cached_loads=%d downloads=%d pending=%d) texture=%.2f(candidates=%d downloads=%d pending=%d refresh_wait=%d refresh_flush=%d) mesh=%.2f(rebuilt=%d queued=%d deferred=%d globe=%d vertices=%zu indices=%zu) atlas_req=%.2f ranges ultra=%s detail=%s mid=%s base=%s",
              total_ms,
              initial_state_ms,
              refresh_ms,
              terrain_queue_ms,
              terrain_candidates_count,
              terrain_loads_count,
              terrain_downloads_count,
              pending_terrain_downloads_count,
              texture_queue_ms,
              texture_candidates_count,
              texture_downloads_count,
              pending_texture_downloads_count,
              texture_refresh_waiting ? 1 : 0,
              texture_refresh_flushed ? 1 : 0,
              mesh_rebuild_ms,
              mesh_rebuilt ? 1 : 0,
              mesh_build_queued ? 1 : 0,
              mesh_build_deferred ? 1 : 0,
              globe_mesh_rebuilt ? 1 : 0,
              mesh_vertices_count,
              mesh_indices_count,
              atlas_request_ms,
              ultra_atlas_range.key().c_str(),
              atlas_range.key().c_str(),
              mid_atlas_range.key().c_str(),
              base_atlas_range.key().c_str());
  }
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
  const gint64 frame_start_us = perf_now_us();

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
  GWorldSceneCameraMode camera_mode = GWORLD_SCENE_CAMERA_MODE_DEFAULT;
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
  double ultra_texture_lateral_radius = 0.0;
  AtlasRange ultra_texture_atlas_range;
  AtlasRange texture_atlas_range;
  AtlasRange mid_texture_atlas_range;
  AtlasRange base_texture_atlas_range;
  AtlasRange globe_texture_atlas_range;
  bool more_texture_uploads_pending = false;
  bool moving = false;
  bool mesh_dirty_before = false;
  bool globe_mesh_dirty_before = false;
  bool texture_dirty_before = false;
  bool ultra_texture_dirty_before = false;
  bool mid_texture_dirty_before = false;
  bool base_texture_dirty_before = false;
  bool globe_texture_dirty_before = false;
  bool model_texture_dirty_before = false;
  int pending_terrain_downloads = 0;
  int pending_texture_downloads = 0;
  int pending_texture_builds = 0;
  guint64 frame_number = 0;
  double frame_gap_ms = 0.0;
  double state_lock_wait_ms = 0.0;
  double state_copy_upload_ms = 0.0;
  PerfBufferUploadStats mesh_upload_stats;
  PerfBufferUploadStats globe_upload_stats;
  PerfTextureUploadStats texture_upload_stats;
  std::vector<BillboardRenderItem> billboards;
  std::vector<GroundOverlayRenderItem> ground_overlays;
  {
    const gint64 lock_wait_start_us = perf_now_us();
    std::unique_lock<std::mutex> lock(state->mutex);
    state_lock_wait_ms = perf_elapsed_ms(lock_wait_start_us);
    const gint64 state_start_us = perf_now_us();
    if (state->last_render_start_us != 0)
      frame_gap_ms = perf_elapsed_ms(state->last_render_start_us, frame_start_us);
    state->last_render_start_us = frame_start_us;
    frame_number = ++state->perf_frame_counter;
    latitude = state->latitude;
    longitude = state->longitude;
    altitude = std::max(25.0, state->altitude_amsl);
    heading = state->heading_deg;
    pitch = std::clamp(state->pitch_deg, kMinCameraPitchDeg, kMaxCameraPitchDeg);
    radius_m = terrain_build_radius_for_altitude(state->altitude_amsl);
    mesh_origin_latitude = state->mesh_origin_latitude;
    mesh_origin_longitude = state->mesh_origin_longitude;
    camera_mode = state->camera_mode;
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
    ultra_texture_lateral_radius = state->ultra_texture_lateral_radius_m;
    ultra_texture_atlas_range = state->ultra_texture_atlas_range;
    texture_atlas_range = state->texture_atlas_range;
    mid_texture_atlas_range = state->mid_texture_atlas_range;
    base_texture_atlas_range = state->base_texture_atlas_range;
    globe_texture_atlas_range = state->globe_texture_atlas_range;
    moving = state->move_forward || state->move_backward || state->move_left || state->move_right;
    mesh_dirty_before = state->mesh_dirty;
    globe_mesh_dirty_before = state->globe_mesh_dirty;
    texture_dirty_before = state->texture_dirty;
    ultra_texture_dirty_before = state->ultra_texture_dirty;
    mid_texture_dirty_before = state->mid_texture_dirty;
    base_texture_dirty_before = state->base_texture_dirty;
    globe_texture_dirty_before = state->globe_texture_dirty;
    model_texture_dirty_before = state->model_texture_dirty;
    pending_terrain_downloads = count_pending_downloads_with_prefix_locked(state, "terrain:");
    pending_texture_downloads = count_pending_downloads_with_prefix_locked(state, "texture:");
    pending_texture_builds = static_cast<int>(state->pending_texture_build_cancellables.size());
    collect_billboards_locked(state, billboards);
    collect_ground_overlays_locked(state,
                                   ground_overlays,
                                   state->mesh_origin_latitude,
                                   state->mesh_origin_longitude);
    upload_mesh_if_needed(state, &mesh_upload_stats);
    if (render_globe)
      upload_globe_mesh_if_needed(state, &globe_upload_stats);
    more_texture_uploads_pending = upload_textures_if_needed(state, &texture_upload_stats);
    state_copy_upload_ms = perf_elapsed_ms(state_start_us);
  }

  const gint64 camera_start_us = perf_now_us();
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
  const double sun_height = sun_enu.z;
  const float daylight = static_cast<float>(smoothstep_value(-0.18, 0.12, sun_height));
  const float direct_daylight = static_cast<float>(smoothstep_value(-0.04, 0.14, sun_height));
  const float twilight = static_cast<float>(
    smoothstep_value(-0.28, -0.02, sun_height) *
    (1.0 - smoothstep_value(0.08, 0.40, sun_height)));
  const float sunset_warmth =
    static_cast<float>(1.0 - smoothstep_value(0.015, 0.34, sun_height));
  const float night =
    static_cast<float>(1.0 - smoothstep_value(-0.32, -0.12, sun_height));
  const float horizon_glow_strength = std::clamp(
    static_cast<float>(smoothstep_value(-0.26, -0.07, sun_height) *
                       (1.0 - smoothstep_value(0.10, 0.34, sun_height))),
    0.0f,
    1.0f);
  const float ambient_strength =
    std::clamp(0.3f + daylight * 0.29f + twilight * 0.14f, 0.18f, 0.60f);
  const float sun_strength =
    std::clamp(direct_daylight * (0.56f + sunset_warmth * 0.26f), 0.0f, 0.82f);
  glm::vec3 ambient_color =
    glm::mix(glm::vec3(0.42f, 0.46f, 0.64f),
             glm::vec3(0.72f, 0.80f, 1.00f),
             daylight);
  ambient_color =
    glm::mix(ambient_color,
             glm::vec3(0.58f, 0.42f, 0.54f),
             std::clamp(twilight * 0.55f, 0.0f, 1.0f));
  const glm::vec3 direct_light_color =
    glm::mix(glm::vec3(1.00f, 0.34f, 0.13f),
             glm::vec3(1.00f, 0.96f, 0.84f),
             static_cast<float>(smoothstep_value(0.08, 0.45, sun_height)));
  glm::vec3 effective_fog_color =
    glm::mix(fog_color,
             glm::vec3(0.30f, 0.34f, 0.50f),
             std::clamp(twilight * 0.22f, 0.0f, 0.22f));
  effective_fog_color =
    glm::mix(effective_fog_color,
             glm::vec3(0.130f, 0.155f, 0.250f),
             std::clamp(night * 0.58f, 0.0f, 0.58f));
  effective_fog_range(fog_start, fog_end, altitude, render_globe, fog_start, fog_end);
  const float fog_density =
    fog_enabled ? static_cast<float>(1.0 / std::max(fog_end * 2.8, 1.0)) : 0.0f;

  const gworld_scene::CameraPose camera_pose =
    camera_pose_for_mode(camera_mode,
                         latitude,
                         longitude,
                         altitude,
                         heading,
                         pitch,
                         mesh_origin_latitude,
                         mesh_origin_longitude);
  const gworld_scene::LocalFrame camera_frame =
    gworld_scene::local_frame_at(latitude, longitude, mesh_origin_latitude, mesh_origin_longitude);
  const glm::vec3 ultra_texture_center = glm::vec3(camera_frame.origin);
  const glm::mat4 view = glm::lookAt(glm::vec3(camera_pose.eye),
                                     glm::vec3(camera_pose.center),
                                     glm::vec3(camera_pose.up));
  const glm::mat4 mvp = projection * view;
  const bool render_world_mesh = state->index_count > 0 && (render_terrain || !mesh_includes_terrain);
  const double camera_setup_ms = perf_elapsed_ms(camera_start_us);

  const gint64 shadow_start_us = perf_now_us();
  glm::mat4 light_mvp(1.0f);
  bool shadow_available = false;
  if (shadows_enabled &&
      render_world_mesh &&
      altitude < kShadowMaxAltitudeM &&
      sun_enu.z > 0.04) {
    const double shadow_extent =
      std::clamp(std::max(terrain_near_radius_for_altitude(altitude, radius_m),
                          altitude * 4.0),
                 2500.0,
                 120000.0);
    light_mvp = shadow_light_matrix(camera_frame.origin, sun_direction, shadow_extent);
    shadow_available = render_shadow_map(state, light_mvp);
  }
  const double shadow_ms = perf_elapsed_ms(shadow_start_us);

  const gint64 draw_start_us = perf_now_us();
  glViewport(0, 0, width, height);
  glClearColor(effective_fog_color.r, effective_fog_color.g, effective_fog_color.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  render_sky_background(state,
                        mvp,
                        camera_pose,
                        sun_direction,
                        fog_color,
                        daylight,
                        twilight,
                        horizon_glow_strength);
  render_sun_disc(state,
                  mvp,
                  camera_pose,
                  sun_direction,
                  sun_enu,
                  far_plane_m,
                  width,
                  height);

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
  glUniform1i(glGetUniformLocation(state->program, "ultra_texture"), 5);
  set_atlas_uniforms(state->program, "ultra", ultra_texture_atlas_range);
  set_atlas_uniforms(state->program, "detail", texture_atlas_range);
  set_atlas_uniforms(state->program, "mid", mid_texture_atlas_range);
  set_atlas_uniforms(state->program, "base", base_texture_atlas_range);
  set_atlas_uniforms(state->program, "globe", globe_texture_atlas_range);
  glUniform3fv(glGetUniformLocation(state->program, "sun_direction"),
               1,
               glm::value_ptr(sun_direction));
  glUniform3fv(glGetUniformLocation(state->program, "ambient_color"),
               1,
               glm::value_ptr(ambient_color));
  glUniform3fv(glGetUniformLocation(state->program, "direct_light_color"),
               1,
               glm::value_ptr(direct_light_color));
  glUniform1f(glGetUniformLocation(state->program, "ambient_strength"), ambient_strength);
  glUniform1f(glGetUniformLocation(state->program, "sun_strength"), sun_strength);
  glUniform3fv(glGetUniformLocation(state->program, "camera_position"),
               1,
               glm::value_ptr(glm::vec3(camera_pose.eye)));
  glUniform3fv(glGetUniformLocation(state->program, "ultra_texture_center"),
               1,
               glm::value_ptr(ultra_texture_center));
  glUniform1f(glGetUniformLocation(state->program, "ultra_texture_radius"),
              static_cast<float>(ultra_texture_lateral_radius));
  glUniform1i(glGetUniformLocation(state->program, "fog_enabled"), fog_enabled);
  glUniform3fv(glGetUniformLocation(state->program, "fog_color"),
               1,
               glm::value_ptr(effective_fog_color));
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
    glUniform1i(glGetUniformLocation(state->program, "has_ultra_texture"), FALSE);
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
    glUniform1i(glGetUniformLocation(state->program, "has_ultra_texture"), state->ultra_texture != 0);
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
    if (state->ultra_texture != 0) {
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_2D, state->ultra_texture);
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
  render_ground_overlays(state, ground_overlays, mvp);
  render_billboards(state,
                    billboards,
                    mvp,
                    camera_pose,
                    height,
                    mesh_origin_latitude,
                    mesh_origin_longitude);
  const double draw_ms = perf_elapsed_ms(draw_start_us);
  const double total_ms = perf_elapsed_ms(frame_start_us);

  if (perf_enabled()) {
    const bool slow_frame =
      total_ms >= perf_frame_threshold_ms() ||
      frame_gap_ms >= perf_frame_threshold_ms() ||
      state_lock_wait_ms >= perf_upload_threshold_ms() ||
      state_copy_upload_ms >= perf_upload_threshold_ms() ||
      shadow_ms >= perf_upload_threshold_ms() ||
      texture_upload_stats.duration_ms >= perf_upload_threshold_ms() ||
      mesh_upload_stats.duration_ms >= perf_upload_threshold_ms() ||
      globe_upload_stats.duration_ms >= perf_upload_threshold_ms();
    if (perf_verbose() || slow_frame) {
      g_message("GWorldScene perf frame=%" G_GUINT64_FORMAT " total=%.2fms gap=%.2fms moving=%d lock_wait=%.2f state_upload=%.2f camera=%.2f shadow=%.2f draw=%.2f mesh_upload=%d %.2fms %.1fMiB idx=%zu globe_upload=%d %.2fms %.1fMiB tex_upload=%s %.2fms %.1fMiB %dx%d deferred=%d pending(terrain=%d texture=%d builds=%d) dirty(mesh=%d globe=%d tex=%d ultra=%d mid=%d base=%d globe_tex=%d model=%d)",
                frame_number,
                total_ms,
                frame_gap_ms,
                moving ? 1 : 0,
                state_lock_wait_ms,
                state_copy_upload_ms,
                camera_setup_ms,
                shadow_ms,
                draw_ms,
                mesh_upload_stats.uploaded ? 1 : 0,
                mesh_upload_stats.duration_ms,
                bytes_to_mib(mesh_upload_stats.vertex_bytes + mesh_upload_stats.index_bytes),
                mesh_upload_stats.indices,
                globe_upload_stats.uploaded ? 1 : 0,
                globe_upload_stats.duration_ms,
                bytes_to_mib(globe_upload_stats.vertex_bytes + globe_upload_stats.index_bytes),
                texture_upload_stats.uploaded ? texture_upload_stats.layer : "none",
                texture_upload_stats.duration_ms,
                bytes_to_mib(texture_upload_stats.bytes),
                texture_upload_stats.width,
                texture_upload_stats.height,
                texture_upload_stats.deferred ? 1 : 0,
                pending_terrain_downloads,
                pending_texture_downloads,
                pending_texture_builds,
                mesh_dirty_before ? 1 : 0,
                globe_mesh_dirty_before ? 1 : 0,
                texture_dirty_before ? 1 : 0,
                ultra_texture_dirty_before ? 1 : 0,
                mid_texture_dirty_before ? 1 : 0,
                base_texture_dirty_before ? 1 : 0,
                globe_texture_dirty_before ? 1 : 0,
                model_texture_dirty_before ? 1 : 0);
    }
  }

  if (more_texture_uploads_pending)
    gtk_widget_queue_draw(GTK_WIDGET(area));

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
    set_camera_position_locked(state, latitude, longitude, altitude);
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
    if (std::hypot(offset_x, offset_y) >= kClickDragThresholdPx ||
        std::hypot(dx, dy) > 0.25)
      state->click_dragged = true;
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
  bool free_camera = false;
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
    free_camera = state->camera_mode == GWORLD_SCENE_CAMERA_MODE_FREE;
  }

  if (dt <= 0.0 || (!forward && !backward && !left && !right))
    return G_SOURCE_CONTINUE;

  dt = std::min(dt, 0.05);
  const double movement_altitude = free_camera ? 0.0 : altitude;
  const glm::dvec2 direction =
    gworld_scene::camera_movement_direction_for_input(movement_altitude, heading, forward, backward, left, right);
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
  (void)n_press;
  auto *self = GWORLD_SCENE_VIEW(user_data);
  gtk_widget_grab_focus(GTK_WIDGET(self));

  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  state->click_button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  state->click_press_x = x;
  state->click_press_y = y;
  state->click_press_heading_deg = state->heading_deg;
  state->click_press_pitch_deg = state->pitch_deg;
  state->click_dragged = false;
}

static void
on_click_released(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data)
{
  auto *self = GWORLD_SCENE_VIEW(user_data);
  guint button = 0;
  bool suppress_pick = false;
  {
    auto *state = get_state(self);
    std::lock_guard<std::mutex> lock(state->mutex);
    button = state->click_button;
    const double dx = x - state->click_press_x;
    const double dy = y - state->click_press_y;
    const bool camera_rotated =
      std::abs(state->heading_deg - state->click_press_heading_deg) >= kClickCameraRotationThresholdDeg ||
      std::abs(state->pitch_deg - state->click_press_pitch_deg) >= kClickCameraRotationThresholdDeg;
    suppress_pick = state->click_dragged ||
                    camera_rotated ||
                    std::hypot(dx, dy) >= kClickDragThresholdPx;
    state->click_button = 0;
    state->click_dragged = false;
  }

  if (button == 0)
    button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
  if (suppress_pick)
    return;

  ScenePickResult pick = pick_scene_at_widget_point(self, x, y);
  if (!pick.hit)
    return;

  if (pick.node != nullptr) {
    const guint signal_id =
      n_press == 2 ? signals[SIGNAL_NODE_DOUBLE_CLICKED] : signals[SIGNAL_NODE_CLICKED];
    g_signal_emit(self,
                  signal_id,
                  0,
                  pick.node,
                  pick.latitude,
                  pick.longitude,
                  pick.altitude_amsl,
                  button);
    g_object_unref(pick.node);
    return;
  }

  const guint signal_id =
    n_press == 2 ? signals[SIGNAL_GROUND_DOUBLE_CLICKED] : signals[SIGNAL_GROUND_CLICKED];
  g_signal_emit(self,
                signal_id,
                0,
                pick.latitude,
                pick.longitude,
                pick.altitude_amsl,
                button);
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

  signals[SIGNAL_NODE_CLICKED] =
    g_signal_new("node-clicked",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 nullptr,
                 nullptr,
                 nullptr,
                 G_TYPE_NONE,
                 5,
                 GWORLD_TYPE_SCENE_NODE,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_UINT);
  signals[SIGNAL_NODE_DOUBLE_CLICKED] =
    g_signal_new("node-double-clicked",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 nullptr,
                 nullptr,
                 nullptr,
                 G_TYPE_NONE,
                 5,
                 GWORLD_TYPE_SCENE_NODE,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_UINT);
  signals[SIGNAL_GROUND_CLICKED] =
    g_signal_new("ground-clicked",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 nullptr,
                 nullptr,
                 nullptr,
                 G_TYPE_NONE,
                 4,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_UINT);
  signals[SIGNAL_GROUND_DOUBLE_CLICKED] =
    g_signal_new("ground-double-clicked",
                 G_TYPE_FROM_CLASS(klass),
                 G_SIGNAL_RUN_LAST,
                 0,
                 nullptr,
                 nullptr,
                 nullptr,
                 G_TYPE_NONE,
                 4,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_DOUBLE,
                 G_TYPE_UINT);

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
  g_signal_connect(click, "released", G_CALLBACK(on_click_released), self);
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
    set_camera_position_locked(state, latitude, longitude, altitude_amsl);
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
    set_camera_orientation_locked(state, heading_deg, pitch_deg);
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
gworld_scene_view_set_camera_mode(GWorldSceneView *self, GWorldSceneCameraMode camera_mode)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  if (camera_mode != GWORLD_SCENE_CAMERA_MODE_DEFAULT &&
      camera_mode != GWORLD_SCENE_CAMERA_MODE_FREE) {
    camera_mode = GWORLD_SCENE_CAMERA_MODE_DEFAULT;
  }

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->camera_mode = camera_mode;
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

GWorldSceneCameraMode
gworld_scene_view_get_camera_mode(GWorldSceneView *self)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), GWORLD_SCENE_CAMERA_MODE_DEFAULT);

  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  return state->camera_mode;
}

void
gworld_scene_view_set_free_camera_position(GWorldSceneView *self,
                                           double latitude,
                                           double longitude,
                                           double altitude_amsl)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->camera_mode = GWORLD_SCENE_CAMERA_MODE_FREE;
    set_camera_position_locked(state, latitude, longitude, altitude_amsl);
  }

  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_LATITUDE]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_LONGITUDE]);
  g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ALTITUDE_AMSL]);

  schedule_scene_requests(self);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_get_free_camera_position(GWorldSceneView *self,
                                           double *latitude,
                                           double *longitude,
                                           double *altitude_amsl)
{
  gworld_scene_view_get_camera(self, latitude, longitude, altitude_amsl);
}

void
gworld_scene_view_set_free_camera_orientation(GWorldSceneView *self,
                                              double azimuth_deg,
                                              double pitch_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->camera_mode = GWORLD_SCENE_CAMERA_MODE_FREE;
    set_camera_orientation_locked(state, azimuth_deg, pitch_deg);
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_get_free_camera_orientation(GWorldSceneView *self,
                                              double *azimuth_deg,
                                              double *pitch_deg)
{
  gworld_scene_view_get_camera_orientation(self, azimuth_deg, pitch_deg);
}

void
gworld_scene_view_set_free_camera_azimuth(GWorldSceneView *self, double azimuth_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->camera_mode = GWORLD_SCENE_CAMERA_MODE_FREE;
    set_camera_orientation_locked(state, azimuth_deg, state->pitch_deg);
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_set_free_camera_pitch(GWorldSceneView *self, double pitch_deg)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->camera_mode = GWORLD_SCENE_CAMERA_MODE_FREE;
    set_camera_orientation_locked(state, state->heading_deg, pitch_deg);
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_look_at_location(GWorldSceneView *self,
                                   double latitude,
                                   double longitude,
                                   double altitude_amsl)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->camera_mode = GWORLD_SCENE_CAMERA_MODE_FREE;
    look_at_location_locked(state,
                            std::clamp(latitude, -90.0, 90.0),
                            std::clamp(longitude, -180.0, 180.0),
                            altitude_amsl);
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_look_at_node(GWorldSceneView *self, GWorldSceneNode *node)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));
  g_return_if_fail(GWORLD_IS_SCENE_NODE(node));

  double latitude = 0.0;
  double longitude = 0.0;
  double altitude_amsl = 0.0;
  gworld_scene_node_get_position(node, &latitude, &longitude, &altitude_amsl);
  const bool billboard_agl =
    GWORLD_IS_SCENE_BILLBOARD_NODE(node) &&
    gworld_scene_billboard_node_get_altitude_mode(GWORLD_SCENE_BILLBOARD_NODE(node)) ==
      GWORLD_SCENE_ALTITUDE_AGL;

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (billboard_agl) {
      double terrain_height = 0.0;
      if (terrain_height_at_lat_lon_locked(state, latitude, longitude, terrain_height))
        altitude_amsl += terrain_height;
    }
    state->camera_mode = GWORLD_SCENE_CAMERA_MODE_FREE;
    look_at_location_locked(state, latitude, longitude, altitude_amsl);
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void
gworld_scene_view_set_terrain_server(GWorldSceneView *self, const char *terrain_server)
{
  g_return_if_fail(GWORLD_IS_SCENE_VIEW(self));

  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->terrain_server = terrain_server ? terrain_server : kDefaultTerrainServer;
    if (!state->terrain_tiles.empty()) {
      state->terrain_tiles.clear();
      ++state->terrain_revision;
    }
    clear_terrain_download_state_locked(state);
    cancel_pending_downloads_with_prefix_locked(state, "terrain:");
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
    cancel_pending_downloads_with_prefix_locked(state, "texture:");
    cancel_all_texture_builds_locked(state);
    state->ultra_texture_pixels.clear();
    state->ultra_texture_width = 0;
    state->ultra_texture_height = 0;
    state->ultra_texture_dirty = true;
    state->loaded_ultra_texture_key.clear();
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
    state->wanted_ultra_texture_key.clear();
    state->wanted_texture_key.clear();
    state->wanted_mid_texture_key.clear();
    state->wanted_base_texture_key.clear();
    state->wanted_globe_texture_key.clear();
    state->pending_ultra_texture_build_key.clear();
    state->pending_texture_build_key.clear();
    state->pending_mid_texture_build_key.clear();
    state->pending_base_texture_build_key.clear();
    state->pending_globe_texture_build_key.clear();
    state->texture_refresh_pending = false;
    state->last_texture_refresh_us = 0;
    state->ultra_texture_atlas_range = AtlasRange();
    state->texture_atlas_range = AtlasRange();
    state->mid_texture_atlas_range = AtlasRange();
    state->base_texture_atlas_range = AtlasRange();
    state->globe_texture_atlas_range = AtlasRange();
    ++state->texture_source_revision;
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
    cancel_all_pending_downloads_locked(state);
    cancel_all_texture_builds_locked(state);
    if (!state->terrain_tiles.empty()) {
      state->terrain_tiles.clear();
      ++state->terrain_revision;
    }
    clear_terrain_download_state_locked(state);
    state->ultra_texture_pixels.clear();
    state->ultra_texture_width = 0;
    state->ultra_texture_height = 0;
    state->ultra_texture_dirty = true;
    state->loaded_ultra_texture_key.clear();
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
    state->wanted_ultra_texture_key.clear();
    state->wanted_texture_key.clear();
    state->wanted_mid_texture_key.clear();
    state->wanted_base_texture_key.clear();
    state->wanted_globe_texture_key.clear();
    state->pending_ultra_texture_build_key.clear();
    state->pending_texture_build_key.clear();
    state->pending_mid_texture_build_key.clear();
    state->pending_base_texture_build_key.clear();
    state->pending_globe_texture_build_key.clear();
    state->texture_refresh_pending = false;
    state->last_texture_refresh_us = 0;
    state->ultra_texture_atlas_range = AtlasRange();
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
    if (!state->cache_enabled) {
      cancel_all_pending_downloads_locked(state);
      cancel_all_texture_builds_locked(state);
      state->texture_refresh_pending = false;
      state->last_texture_refresh_us = 0;
    }
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

gboolean
gworld_scene_view_sample_terrain_altitude(GWorldSceneView *self,
                                          double latitude,
                                          double longitude,
                                          double *altitude_amsl)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), FALSE);
  g_return_val_if_fail(altitude_amsl != nullptr, FALSE);

  auto *state = get_state(self);
  std::lock_guard<std::mutex> lock(state->mutex);
  double terrain_height = 0.0;
  if (!terrain_height_at_lat_lon_locked(state, latitude, longitude, terrain_height))
    return FALSE;

  *altitude_amsl = terrain_height;
  return TRUE;
}

static void
mark_scene_nodes_changed(GWorldSceneView *self, GWorldSceneViewState *state)
{
  (void)self;
  ++state->scene_revision;
  state->mesh_key.clear();
}

static bool
scene_node_requires_mesh_rebuild(GWorldSceneNode *node)
{
  return !GWORLD_IS_SCENE_BILLBOARD_NODE(node) &&
         !GWORLD_IS_SCENE_GROUND_OVERLAY_NODE(node);
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
  auto *self = GWORLD_SCENE_VIEW(user_data);
  if (!scene_node_requires_mesh_rebuild(node)) {
    gtk_widget_queue_draw(GTK_WIDGET(self));
    return;
  }

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
    if (scene_node_requires_mesh_rebuild(node))
      mark_scene_nodes_changed(self, state);
  }
  if (scene_node_requires_mesh_rebuild(node))
    schedule_scene_nodes_changed(self);
  else
    gtk_widget_queue_draw(GTK_WIDGET(self));
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

GWorldSceneBillboardNode *
gworld_scene_view_add_billboard(GWorldSceneView *self,
                                const char *image_path,
                                double latitude,
                                double longitude,
                                double altitude)
{
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);
  g_return_val_if_fail(image_path != nullptr && image_path[0] != '\0', nullptr);

  GWorldSceneBillboardNode *node = nullptr;
  {
    auto *state = get_state(self);
    std::lock_guard<std::mutex> lock(state->mutex);
    node = _gworld_scene_billboard_node_new(state->next_node_id++,
                                            image_path,
                                            latitude,
                                            longitude,
                                            altitude);
  }
  add_scene_node(self, GWORLD_SCENE_NODE(node));
  return node;
}

GWorldSceneGroundOverlayNode *
gworld_scene_view_add_ground_overlay(GWorldSceneView *self,
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
  g_return_val_if_fail(GWORLD_IS_SCENE_VIEW(self), nullptr);
  g_return_val_if_fail(image_path != nullptr && image_path[0] != '\0', nullptr);

  GWorldSceneGroundOverlayNode *node = nullptr;
  {
    auto *state = get_state(self);
    std::lock_guard<std::mutex> lock(state->mutex);
    node = _gworld_scene_ground_overlay_node_new(state->next_node_id++,
                                                 image_path,
                                                 top_left_latitude,
                                                 top_left_longitude,
                                                 top_right_latitude,
                                                 top_right_longitude,
                                                 bottom_right_latitude,
                                                 bottom_right_longitude,
                                                 bottom_left_latitude,
                                                 bottom_left_longitude);
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
  const bool requires_mesh_rebuild = scene_node_requires_mesh_rebuild(iter->second);
  g_signal_handlers_disconnect_by_func(iter->second,
                                       reinterpret_cast<gpointer>(on_scene_node_changed),
                                       self);
  g_object_unref(iter->second);
  state->scene_nodes.erase(iter);
  if (requires_mesh_rebuild)
    mark_scene_nodes_changed(self, state);
  lock.unlock();
  if (requires_mesh_rebuild)
    schedule_scene_nodes_changed(self);
  else
    gtk_widget_queue_draw(GTK_WIDGET(self));
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
  bool requires_mesh_rebuild = false;
  for (auto &entry : state->scene_nodes) {
    requires_mesh_rebuild = requires_mesh_rebuild || scene_node_requires_mesh_rebuild(entry.second);
    g_signal_handlers_disconnect_by_func(entry.second,
                                         reinterpret_cast<gpointer>(on_scene_node_changed),
                                         self);
    g_object_unref(entry.second);
  }
  state->scene_nodes.clear();
  if (requires_mesh_rebuild)
    mark_scene_nodes_changed(self, state);
  lock.unlock();
  if (requires_mesh_rebuild)
    schedule_scene_nodes_changed(self);
  else
    gtk_widget_queue_draw(GTK_WIDGET(self));
}
