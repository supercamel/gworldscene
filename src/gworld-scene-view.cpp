#include "gworld-scene-view.h"
#include "gworld-scene-view-private.h"

#include <assimp/Importer.hpp>
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
constexpr double kEarthMetersPerDegree = 111320.0;
constexpr double kWgs84A = 6378137.0;
constexpr double kWgs84F = 1.0 / 298.257223563;
constexpr double kWgs84B = kWgs84A * (1.0 - kWgs84F);
constexpr double kWgs84E2 = 1.0 - (kWgs84B * kWgs84B) / (kWgs84A * kWgs84A);
constexpr int kAtlasTilePixels = 256;
constexpr int kMaxAtlasTiles = 64;
constexpr int kMaxAtlasPixels = 4096;
constexpr guint kSceneUpdateDelayMs = 180;
constexpr int kMaxTerrainTileLoadsPerUpdate = 10;
constexpr int kMaxTerrainDownloadsPerUpdate = 8;
constexpr int kMaxTextureDownloadsPerUpdate = 36;
constexpr double kMinCameraPitchDeg = -89.0;
constexpr double kMaxCameraPitchDeg = 89.0;

constexpr const char *kDefaultTerrainServer = "https://flightops.silvertone.com.au/terrain/data/";
constexpr const char *kDefaultMapTileTemplate = "https://tile.openstreetmap.org/{z}/{x}/{y}.png";

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
};

} // namespace

struct GWorldSceneViewState {
  std::mutex mutex;

  double latitude = -35.0024;
  double longitude = 147.4648;
  double altitude_amsl = 5200.0;
  double heading_deg = 35.0;
  double pitch_deg = -26.0;
  double mesh_origin_latitude = -35.0024;
  double mesh_origin_longitude = 147.4648;

  std::string terrain_server = kDefaultTerrainServer;
  std::string map_tile_template = kDefaultMapTileTemplate;
  std::string cache_directory;
  bool cache_enabled = true;

  std::unordered_map<std::string, TerrainTile> terrain_tiles;
  std::unordered_set<std::string> wanted_terrain_keys;
  std::unordered_set<std::string> pending;

  std::vector<float> vertices;
  std::vector<unsigned int> indices;
  bool mesh_dirty = true;
  std::string mesh_key;
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
  guint64 texture_source_revision = 0;

  GLuint program = 0;
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint ebo = 0;
  GLuint texture = 0;
  GLuint mid_texture = 0;
  GLuint base_texture = 0;
  std::size_t index_count = 0;

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
  }
  return state->texture_atlas_range;
}

double
deg_to_rad(double degrees)
{
  return degrees * kPi / 180.0;
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
  const double horizon_m = std::sqrt(std::max(0.0, 2.0 * kWgs84A * altitude_amsl));
  return std::clamp(std::max(altitude_amsl * 70.0, horizon_m * 1.35), 90000.0, 650000.0);
}

double
texture_radius_for_altitude(double altitude_amsl)
{
  return std::clamp(altitude_amsl * 1.4, 3500.0, 18000.0);
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
texture_cache_path(const std::string &cache_dir,
                   const std::string &uri,
                   TileCoord tile)
{
  return join_path(join_path(join_path(cache_dir, "textures"), std::to_string(tile.z)),
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
geodetic_to_ecef(double lat_deg, double lon_deg, double h)
{
  const double lat = deg_to_rad(lat_deg);
  const double lon = deg_to_rad(lon_deg);
  const double sin_lat = std::sin(lat);
  const double cos_lat = std::cos(lat);
  const double sin_lon = std::sin(lon);
  const double cos_lon = std::cos(lon);
  const double n = kWgs84A / std::sqrt(1.0 - kWgs84E2 * sin_lat * sin_lat);

  return glm::dvec3((n + h) * cos_lat * cos_lon,
                    (n + h) * cos_lat * sin_lon,
                    (n * (1.0 - kWgs84E2) + h) * sin_lat);
}

glm::dvec3
geodetic_to_enu(double lat_deg,
                double lon_deg,
                double h,
                double ref_lat_deg,
                double ref_lon_deg,
                double ref_h)
{
  const glm::dvec3 ref_ecef = geodetic_to_ecef(ref_lat_deg, ref_lon_deg, ref_h);
  const glm::dvec3 ecef = geodetic_to_ecef(lat_deg, lon_deg, h);
  const glm::dvec3 d = ecef - ref_ecef;

  const double lat0 = deg_to_rad(ref_lat_deg);
  const double lon0 = deg_to_rad(ref_lon_deg);
  const double sin_lat = std::sin(lat0);
  const double cos_lat = std::cos(lat0);
  const double sin_lon = std::sin(lon0);
  const double cos_lon = std::cos(lon0);

  const double east = -sin_lon * d.x + cos_lon * d.y;
  const double north = -sin_lat * cos_lon * d.x - sin_lat * sin_lon * d.y + cos_lat * d.z;
  const double up = cos_lat * cos_lon * d.x + cos_lat * sin_lon * d.y + sin_lat * d.z;

  return glm::dvec3(east, up, -north);
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
              const glm::vec3 &normal)
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
  const unsigned int base = static_cast<unsigned int>(vertices.size() / 12);
  append_vertex(vertices, p0, uv0, normal);
  append_vertex(vertices, p1, uv1, normal);
  append_vertex(vertices, p2, uv2, normal);
  indices.push_back(base);
  indices.push_back(base + 1);
  indices.push_back(base + 2);
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
                   const AtlasRange &base_range)
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
                          ":" + std::to_string(state->terrain_tiles.size());

  if (state->mesh_key == key && !state->vertices.empty())
    return;

  state->mesh_origin_latitude = state->latitude;
  state->mesh_origin_longitude = state->longitude;
  state->vertices.clear();
  state->indices.clear();

  for (const auto &entry : state->terrain_tiles)
    append_terrain_tile_mesh(state, entry.second, detail_range, mid_range, base_range, radius_m, sample_step);

  if (state->vertices.empty())
    append_flat_mesh(state, detail_range, mid_range, base_range);

  state->mesh_key = key;
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
uniform mat4 mvp;
out vec2 v_detail_texcoord;
out vec2 v_mid_texcoord;
out vec2 v_base_texcoord;
out vec3 v_normal;
out float v_height;
void main() {
  gl_Position = mvp * vec4(position, 1.0);
  v_detail_texcoord = detail_texcoord;
  v_mid_texcoord = mid_texcoord;
  v_base_texcoord = base_texcoord;
  v_normal = normal;
  v_height = position.y;
}
)GLSL";

  static const char *fragment_source = R"GLSL(
#version 330 core
in vec2 v_detail_texcoord;
in vec2 v_mid_texcoord;
in vec2 v_base_texcoord;
in vec3 v_normal;
in float v_height;
uniform sampler2D detail_texture;
uniform sampler2D mid_texture;
uniform sampler2D base_texture;
uniform bool has_detail_texture;
uniform bool has_mid_texture;
uniform bool has_base_texture;
out vec4 color;
void main() {
  vec3 normal = normalize(v_normal);
  vec3 light_dir = normalize(vec3(-0.45, 0.76, 0.38));
  float diffuse = clamp(dot(normal, light_dir), 0.0, 1.0);
  float shade = 0.58 + diffuse * 0.42;
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
  vec3 base = mix(terrain_tint, texel.rgb, texel.a * 0.88);
  color = vec4(base * shade, 1.0);
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

void
delete_gl_resources(GWorldSceneViewState *state)
{
  if (state->texture)
    glDeleteTextures(1, &state->texture);
  if (state->mid_texture)
    glDeleteTextures(1, &state->mid_texture);
  if (state->base_texture)
    glDeleteTextures(1, &state->base_texture);
  if (state->ebo)
    glDeleteBuffers(1, &state->ebo);
  if (state->vbo)
    glDeleteBuffers(1, &state->vbo);
  if (state->vao)
    glDeleteVertexArrays(1, &state->vao);
  if (state->program)
    glDeleteProgram(state->program);

  state->texture = 0;
  state->mid_texture = 0;
  state->base_texture = 0;
  state->ebo = 0;
  state->vbo = 0;
  state->vao = 0;
  state->program = 0;
  state->index_count = 0;
}

void
upload_mesh_if_needed(GWorldSceneViewState *state)
{
  if (!state->mesh_dirty)
    return;

  if (state->vao == 0)
    glGenVertexArrays(1, &state->vao);
  if (state->vbo == 0)
    glGenBuffers(1, &state->vbo);
  if (state->ebo == 0)
    glGenBuffers(1, &state->ebo);

  glBindVertexArray(state->vao);
  glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
  glBufferData(GL_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(state->vertices.size() * sizeof(float)),
               state->vertices.data(),
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(state->indices.size() * sizeof(unsigned int)),
               state->indices.data(),
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12 * sizeof(float), reinterpret_cast<void *>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        12 * sizeof(float),
                        reinterpret_cast<void *>(3 * sizeof(float)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        12 * sizeof(float),
                        reinterpret_cast<void *>(5 * sizeof(float)));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(3,
                        2,
                        GL_FLOAT,
                        GL_FALSE,
                        12 * sizeof(float),
                        reinterpret_cast<void *>(7 * sizeof(float)));
  glEnableVertexAttribArray(4);
  glVertexAttribPointer(4,
                        3,
                        GL_FLOAT,
                        GL_FALSE,
                        12 * sizeof(float),
                        reinterpret_cast<void *>(9 * sizeof(float)));

  glBindVertexArray(0);
  state->index_count = state->indices.size();
  state->mesh_dirty = false;
}

void
upload_texture_buffer_if_needed(GLuint &texture,
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
    }
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
  glBindTexture(GL_TEXTURE_2D, 0);

  dirty = false;
}

void
upload_textures_if_needed(GWorldSceneViewState *state)
{
  upload_texture_buffer_if_needed(state->texture,
                                  state->texture_pixels,
                                  state->texture_width,
                                  state->texture_height,
                                  state->texture_dirty);
  upload_texture_buffer_if_needed(state->mid_texture,
                                  state->mid_texture_pixels,
                                  state->mid_texture_width,
                                  state->mid_texture_height,
                                  state->mid_texture_dirty);
  upload_texture_buffer_if_needed(state->base_texture,
                                  state->base_texture_pixels,
                                  state->base_texture_width,
                                  state->base_texture_height,
                                  state->base_texture_dirty);
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
load_image_rgba_256(const std::string &path, std::vector<unsigned char> &pixels)
{
  g_autoptr(GError) error = nullptr;
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path.c_str(), &error);
  if (pixbuf == nullptr)
    return false;

  const int width = gdk_pixbuf_get_width(pixbuf);
  const int height = gdk_pixbuf_get_height(pixbuf);
  const int rowstride = gdk_pixbuf_get_rowstride(pixbuf);
  const int channels = gdk_pixbuf_get_n_channels(pixbuf);
  const bool has_alpha = gdk_pixbuf_get_has_alpha(pixbuf);
  const auto *source = gdk_pixbuf_get_pixels(pixbuf);
  if (width <= 0 || height <= 0 || source == nullptr || channels < 3) {
    g_object_unref(pixbuf);
    return false;
  }

  pixels.assign(static_cast<std::size_t>(kAtlasTilePixels * kAtlasTilePixels * 4), 0);
  for (int y = 0; y < kAtlasTilePixels; ++y) {
    const int sy = std::clamp(static_cast<int>((static_cast<double>(y) / kAtlasTilePixels) * height),
                              0,
                              height - 1);
    for (int x = 0; x < kAtlasTilePixels; ++x) {
      const int sx = std::clamp(static_cast<int>((static_cast<double>(x) / kAtlasTilePixels) * width),
                                0,
                                width - 1);
      const std::size_t src = static_cast<std::size_t>(sy * rowstride + sx * channels);
      const std::size_t dst = static_cast<std::size_t>((y * kAtlasTilePixels + x) * 4);
      pixels[dst + 0] = source[src + 0];
      pixels[dst + 1] = source[src + 1];
      pixels[dst + 2] = source[src + 2];
      pixels[dst + 3] = has_alpha && channels >= 4 ? source[src + 3] : 255;
    }
  }

  g_object_unref(pixbuf);
  return true;
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
      const std::string path = texture_cache_path(job->cache_dir, uri, tile);
      if (!g_file_test(path.c_str(), G_FILE_TEST_EXISTS))
        continue;

      std::vector<unsigned char> tile_pixels;
      if (!load_image_rgba_256(path, tile_pixels))
        continue;

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
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    auto &pending_key = texture_layer_pending_key(state, atlas_result->layer);
    if (pending_key == atlas_result->pending_key)
      pending_key.clear();
    const std::string &wanted_key = texture_layer_wanted_key(state, atlas_result->layer);

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

    if (loaded_key == range_key || !pending_layer_key.empty())
      return;

    pending_layer_key = pending_key;
  }

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
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "HTTP %u for %s", status, job->uri.c_str());
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
    g_debug("GWorldScene request failed: %s", error ? error->message : "unknown error");
    return;
  }

  if (job->kind == "terrain") {
    TerrainTile tile;
    tile.lat = job->terrain_lat;
    tile.lon = job->terrain_lon;
    if (read_hgt_file(job->path, tile.heights, tile.dimension)) {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->terrain_tiles[terrain_key(tile.lat, tile.lon)] = std::move(tile);
      state->mesh_key.clear();
    }
  } else if (job->kind == "texture") {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->loaded_texture_key.clear();
    state->loaded_mid_texture_key.clear();
    state->loaded_base_texture_key.clear();
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
    if (state->pending.find(pending_key) != state->pending.end())
      return;
    state->pending.insert(pending_key);
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

  if (cache_enabled) {
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
          continue;
        }
        ++terrain_loads;
        load_cached_terrain_tile(self, lat, lon, name, path);
        continue;
      }

      if (terrain_downloads >= kMaxTerrainDownloadsPerUpdate) {
        has_more_terrain_work = true;
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
          break;
        }
      }
    }

    if (has_more_terrain_work)
      schedule_scene_requests(self, 350);

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

    int texture_downloads = 0;
    bool has_more_texture_work = false;
    auto request_texture_range = [&](const AtlasRange &range) {
      if (!range.valid())
        return;

      for (int ty = range.y_min; ty <= range.y_max; ++ty) {
        for (int tx = range.x_min; tx <= range.x_max; ++tx) {
          TileCoord tile{range.z, tx, ty};
          const std::string uri = texture_uri_from_template(texture_template, tile);
          const std::string path = texture_cache_path(cache_dir, uri, tile);
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

    request_texture_range(base_atlas_range);
    request_texture_range(mid_atlas_range);
    request_texture_range(atlas_range);

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
    rebuild_world_mesh(state, mesh_atlas_range, mesh_mid_atlas_range, mesh_base_atlas_range);
  }

  request_texture_atlas_build(self, base_atlas_range, TextureLayer::Base);
  request_texture_atlas_build(self, mid_atlas_range, TextureLayer::Mid);
  request_texture_atlas_build(self, atlas_range, TextureLayer::Detail);
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
  glClearColor(0.60f, 0.75f, 0.95f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  double altitude = 0.0;
  double heading = 0.0;
  double pitch = 0.0;
  double radius_m = 0.0;
  double latitude = 0.0;
  double longitude = 0.0;
  double mesh_origin_latitude = 0.0;
  double mesh_origin_longitude = 0.0;
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
    upload_mesh_if_needed(state);
    upload_textures_if_needed(state);
  }

  const int width = std::max(1, gtk_widget_get_width(GTK_WIDGET(area)));
  const int height = std::max(1, gtk_widget_get_height(GTK_WIDGET(area)));
  glViewport(0, 0, width, height);

  const float far_plane = static_cast<float>(std::max(100000.0, radius_m * 3.0 + altitude * 4.0));
  const glm::mat4 projection = glm::perspective(glm::radians(45.0f),
                                                static_cast<float>(width) / static_cast<float>(height),
                                                2.0f,
                                                far_plane);

  const double heading_rad = deg_to_rad(heading);
  const double pitch_rad = deg_to_rad(pitch);
  const glm::dvec3 camera_offset = geodetic_to_enu(latitude,
                                                   longitude,
                                                   0.0,
                                                   mesh_origin_latitude,
                                                   mesh_origin_longitude,
                                                   0.0);
  const glm::dvec3 eye(camera_offset.x, camera_offset.y + altitude, camera_offset.z);
  const glm::dvec3 forward(std::sin(heading_rad) * std::cos(pitch_rad),
                           std::sin(pitch_rad),
                           -std::cos(heading_rad) * std::cos(pitch_rad));
  const glm::dvec3 center = eye + forward * std::max(altitude * 2.5, 1000.0);
  const glm::mat4 view = glm::lookAt(glm::vec3(eye), glm::vec3(center), glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 mvp = projection * view;

  glUseProgram(state->program);
  glUniformMatrix4fv(glGetUniformLocation(state->program, "mvp"),
                     1,
                     GL_FALSE,
                     glm::value_ptr(mvp));
  glUniform1i(glGetUniformLocation(state->program, "detail_texture"), 0);
  glUniform1i(glGetUniformLocation(state->program, "mid_texture"), 1);
  glUniform1i(glGetUniformLocation(state->program, "base_texture"), 2);
  glUniform1i(glGetUniformLocation(state->program, "has_detail_texture"), state->texture != 0);
  glUniform1i(glGetUniformLocation(state->program, "has_mid_texture"), state->mid_texture != 0);
  glUniform1i(glGetUniformLocation(state->program, "has_base_texture"), state->base_texture != 0);

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

  glBindVertexArray(state->vao);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(state->index_count), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
  glUseProgram(0);

  return TRUE;
}

static void
move_camera_by_enu(GWorldSceneView *self, double east_m, double north_m, double up_m)
{
  auto *state = get_state(self);
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    const double lon_scale = std::max(0.05, std::cos(deg_to_rad(state->latitude)));
    state->latitude = std::clamp(state->latitude + north_m / kEarthMetersPerDegree, -90.0, 90.0);
    state->longitude = std::clamp(state->longitude + east_m / (kEarthMetersPerDegree * lon_scale), -180.0, 180.0);
    state->altitude_amsl = std::clamp(state->altitude_amsl + up_m, 25.0, 10000000.0);
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
  std::lock_guard<std::mutex> lock(state->mutex);
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
  const double heading_rad = deg_to_rad(heading);
  const double forward_east = std::sin(heading_rad);
  const double forward_north = std::cos(heading_rad);
  const double right_east = std::cos(heading_rad);
  const double right_north = -std::sin(heading_rad);

  double east = 0.0;
  double north = 0.0;
  if (forward) {
    east += forward_east;
    north += forward_north;
  }
  if (backward) {
    east -= forward_east;
    north -= forward_north;
  }
  if (right) {
    east += right_east;
    north += right_north;
  }
  if (left) {
    east -= right_east;
    north -= right_north;
  }

  const double length = std::hypot(east, north);
  if (length <= 0.000001)
    return G_SOURCE_CONTINUE;

  east /= length;
  north /= length;
  const double speed_mps = std::clamp(altitude * 0.65, 35.0, 4000.0) * (fast ? 3.0 : 1.0);
  move_camera_by_enu(self, east * speed_mps * dt, north * speed_mps * dt, 0.0);

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
  std::lock_guard<std::mutex> lock(state->mutex);
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
    state->wanted_texture_key.clear();
    state->wanted_mid_texture_key.clear();
    state->wanted_base_texture_key.clear();
    state->pending_texture_build_key.clear();
    state->pending_mid_texture_build_key.clear();
    state->pending_base_texture_build_key.clear();
    state->texture_atlas_range = AtlasRange();
    state->mid_texture_atlas_range = AtlasRange();
    state->base_texture_atlas_range = AtlasRange();
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
    state->wanted_texture_key.clear();
    state->wanted_mid_texture_key.clear();
    state->wanted_base_texture_key.clear();
    state->pending_texture_build_key.clear();
    state->pending_mid_texture_build_key.clear();
    state->pending_base_texture_build_key.clear();
    state->texture_atlas_range = AtlasRange();
    state->mid_texture_atlas_range = AtlasRange();
    state->base_texture_atlas_range = AtlasRange();
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
