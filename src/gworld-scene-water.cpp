#include "gworld-scene-water-private.h"
#include "gworld-scene-geo-private.h"

#include <gdal_priv.h>
#include <gdal_alg.h>
#include <ogrsf_frmts.h>
#include <cpl_vsi.h>
#include <glib/gstdio.h>
#include <libsoup/soup.h>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <tuple>

namespace gworld_scene {
namespace {
constexpr std::size_t kMaxWaterTileBytes = 8u * 1024u * 1024u;
constexpr gint64 kWaterCacheLifetimeSeconds = 7 * 24 * 60 * 60;
using TileKey = std::tuple<int, int, int>;
TileKey tile_key(int z, int x, int y) { return {z, wrap_tile_x(x, z), y}; }

std::string tile_url(std::string value, int z, int x, int y)
{
  for (const auto &item : {std::make_pair("{z}", z), std::make_pair("{x}", wrap_tile_x(x, z)), std::make_pair("{y}", y)}) {
    std::size_t position = 0;
    const std::string replacement = std::to_string(item.second);
    while ((position = value.find(item.first, position)) != std::string::npos) {
      value.replace(position, 3, replacement);
      position += replacement.size();
    }
  }
  return value;
}

TileRange centered_range(double latitude, double longitude, int zoom)
{
  const int count = std::min(4, 1 << zoom);
  const int x = static_cast<int>(std::floor(slippy_tile_x_for_longitude(longitude, zoom)));
  const int y = static_cast<int>(std::floor(slippy_tile_y_for_latitude(latitude, zoom)));
  TileRange range;
  range.z = zoom;
  range.x_min = x - count / 2;
  range.x_max = range.x_min + count - 1;
  range.y_min = std::clamp(y - count / 2, 0, (1 << zoom) - count);
  range.y_max = range.y_min + count - 1;
  return range;
}

struct TileEntry {
  std::vector<unsigned char> mask;
  bool pending = false;
  unsigned failures = 0;
  gint64 retry_after = 0;
};
}

struct WaterTileStore {
  GCancellable *cancellable = g_cancellable_new();
  std::map<TileKey, TileEntry> tiles;
  std::set<TileKey> wanted;
  unsigned pending = 0;
  ~WaterTileStore() { g_object_unref(cancellable); }
};

std::string water_cache_path(const std::string &directory, const std::string &source, int z, int x, int y)
{
  g_autofree char *digest = g_compute_checksum_for_string(G_CHECKSUM_SHA256, source.c_str(), -1);
  return directory + "/water/" + digest + "/" + std::to_string(z) + "/" +
         std::to_string(wrap_tile_x(x, z)) + "/" + std::to_string(y) + ".pbf";
}

bool decode_water_mask(const std::vector<unsigned char> &bytes, int z, int x, int y,
                        std::vector<unsigned char> &mask, std::string &error)
{
  mask.clear();
  if (bytes.empty() || bytes.size() > kMaxWaterTileBytes || z < 0 || z > 14 || y < 0 || y >= (1 << z)) {
    error = "Invalid water tile size or zoom";
    return false;
  }
  // Some providers store gzip inside the PBF response rather than using HTTP
  // Content-Encoding. Bound the inflated size before handing data to GDAL.
  if (bytes.size() >= 2 && bytes[0] == 0x1f && bytes[1] == 0x8b) {
    std::vector<unsigned char> inflated(kMaxWaterTileBytes);
    z_stream stream = {};
    stream.next_in = const_cast<Bytef *>(bytes.data());
    stream.avail_in = bytes.size();
    stream.next_out = inflated.data();
    stream.avail_out = inflated.size();
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
      error = "Unable to initialize water tile decompression";
      return false;
    }
    const int status = inflate(&stream, Z_FINISH);
    const auto size = stream.total_out;
    inflateEnd(&stream);
    if (status != Z_STREAM_END || (size >= 2 && inflated[0] == 0x1f && inflated[1] == 0x8b)) {
      error = "Invalid or oversized compressed water tile";
      return false;
    }
    inflated.resize(size);
    return decode_water_mask(inflated, z, x, y, mask, error);
  }
  g_autofree char *uuid = g_uuid_string_random();
  const std::string name = "/vsimem/gworldscene-water-" + std::string(uuid) + ".pbf";
  VSILFILE *file = VSIFileFromMemBuffer(name.c_str(), const_cast<GByte *>(bytes.data()), bytes.size(), FALSE);
  if (!file) { error = "Unable to open water tile bytes"; return false; }
  VSIFCloseL(file);
  const std::string option_x = "X=" + std::to_string(wrap_tile_x(x, z));
  const std::string option_y = "Y=" + std::to_string(y);
  const std::string option_z = "Z=" + std::to_string(z);
  const char *options[] = {option_x.c_str(), option_y.c_str(), option_z.c_str(), "CLIP=YES", "METADATA_FILE=", nullptr};
  const char *drivers[] = {"MVT", nullptr};
  CPLPushErrorHandler(CPLQuietErrorHandler);
  CPLErrorReset();
  auto *source = static_cast<GDALDataset *>(GDALOpenEx(name.c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, drivers, options, nullptr));
  bool success = source != nullptr;
  GDALDataset *raster = nullptr;
  if (source) {
    auto *driver = GetGDALDriverManager()->GetDriverByName("MEM");
    raster = driver ? driver->Create("", kWaterTilePixels, kWaterTilePixels, 1, GDT_Byte, nullptr) : nullptr;
    success = raster != nullptr;
  }
  if (raster) {
    const double half_world = 20037508.342789244;
    const double tile_span = half_world * 2.0 / (1 << z);
    double transform[] = {-half_world + wrap_tile_x(x, z) * tile_span, tile_span / kWaterTilePixels, 0,
                           half_world - y * tile_span, 0, -tile_span / kWaterTilePixels};
    raster->SetGeoTransform(transform);
    raster->GetRasterBand(1)->Fill(0);
    if (auto *layer = source->GetLayerByName("water")) {
      unsigned features = 0;
      while (OGRFeature *feature = layer->GetNextFeature()) {
        const int intermittent = feature->GetFieldIndex("intermittent");
        const int brunnel = feature->GetFieldIndex("brunnel");
        const bool hidden = (intermittent >= 0 && feature->GetFieldAsInteger(intermittent) != 0) ||
          (brunnel >= 0 && std::strcmp(feature->GetFieldAsString(brunnel), "tunnel") == 0);
        OGRGeometry *geometry = feature->GetGeometryRef();
        if (!hidden && geometry && (wkbFlatten(geometry->getGeometryType()) == wkbPolygon ||
                                    wkbFlatten(geometry->getGeometryType()) == wkbMultiPolygon)) {
          int band = 1;
          OGRGeometryH handle = OGRGeometry::ToHandle(geometry);
          double burn = 255.0;
          success = GDALRasterizeGeometries(raster, 1, &band, 1, &handle, nullptr, nullptr,
                                             &burn, nullptr, nullptr, nullptr) == CE_None;
        }
        OGRFeature::DestroyFeature(feature);
        if (!success || ++features > 100000) { success = false; break; }
      }
    }
    if (CPLGetLastErrorType() >= CE_Failure) success = false;
    if (success) {
      mask.resize(kWaterTilePixels * kWaterTilePixels);
      success = raster->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, kWaterTilePixels, kWaterTilePixels,
        mask.data(), kWaterTilePixels, kWaterTilePixels, GDT_Byte, 0, 0) == CE_None;
    }
  }
  if (!success) error = "Unable to decode the MVT water layer";
  if (raster) GDALClose(raster);
  if (source) GDALClose(source);
  VSIUnlink(name.c_str());
  CPLPopErrorHandler();
  if (!success) mask.clear();
  return success;
}

namespace {
struct TileJob {
  std::shared_ptr<WaterTileStore> store;
  TileKey key;
  std::string url, path, error;
  bool cache_enabled = false;
  std::vector<unsigned char> mask;
};

bool read_cached_tile(const std::string &path, std::vector<unsigned char> &bytes, bool &fresh)
{
  GStatBuf file_info;
  if (g_stat(path.c_str(), &file_info) != 0 || file_info.st_size <= 0 ||
      static_cast<std::size_t>(file_info.st_size) > kMaxWaterTileBytes) return false;
  fresh = g_get_real_time() / G_USEC_PER_SEC - file_info.st_mtime < kWaterCacheLifetimeSeconds;
  g_autofree char *contents = nullptr;
  gsize size = 0;
  if (!g_file_get_contents(path.c_str(), &contents, &size, nullptr)) return false;
  bytes.assign(contents, contents + size);
  return true;
}

void fetch_tile(GTask *task, gpointer, gpointer data, GCancellable *cancellable)
{
  auto &job = *static_cast<TileJob *>(data);
  auto [z, x, y] = job.key;
  bool fresh = false;
  std::vector<unsigned char> bytes;
  bool cached = job.cache_enabled && read_cached_tile(job.path, bytes, fresh);
  if (cached && fresh && decode_water_mask(bytes, z, x, y, job.mask, job.error)) {
    g_task_return_boolean(task, TRUE);
    return;
  }
  std::vector<unsigned char> stale = std::move(bytes);
  bytes.clear();
  g_autoptr(SoupSession) session = soup_session_new_with_options(
    "timeout", 15u, "user-agent", "GWorldScene/0.1 (+https://github.com/supercamel/gworldscene)", nullptr);
  g_autoptr(SoupMessage) message = soup_message_new("GET", job.url.c_str());
  g_autoptr(GError) error = nullptr;
  g_autoptr(GInputStream) stream = message ? soup_session_send(session, message, cancellable, &error) : nullptr;
  if (stream && soup_message_get_status(message) == SOUP_STATUS_OK) {
    unsigned char chunk[16384];
    gssize count = 0;
    while ((count = g_input_stream_read(stream, chunk, sizeof chunk, cancellable, &error)) > 0) {
      if (bytes.size() + count > kMaxWaterTileBytes) { bytes.clear(); break; }
      bytes.insert(bytes.end(), chunk, chunk + count);
    }
    if (error) bytes.clear();
  }
  bool decoded = !g_cancellable_is_cancelled(cancellable) && decode_water_mask(bytes, z, x, y, job.mask, job.error);
  if (decoded && job.cache_enabled) {
    g_autofree char *directory = g_path_get_dirname(job.path.c_str());
    if (g_mkdir_with_parents(directory, 0755) == 0)
      g_file_set_contents(job.path.c_str(), reinterpret_cast<const char *>(bytes.data()), bytes.size(), nullptr);
  }
  // An expired but valid cached tile remains usable during a network outage.
  if (!decoded && cached && !g_cancellable_is_cancelled(cancellable))
    decoded = decode_water_mask(stale, z, x, y, job.mask, job.error);
  g_task_return_boolean(task, decoded);
}

void tile_ready(GObject *widget, GAsyncResult *result, gpointer)
{
  auto *task = G_TASK(result);
  auto &job = *static_cast<TileJob *>(g_task_get_task_data(task));
  g_autoptr(GError) error = nullptr;
  const bool success = g_task_propagate_boolean(task, &error);
  auto &store = *job.store;
  if (g_cancellable_is_cancelled(store.cancellable)) return;
  auto &entry = store.tiles[job.key];
  entry.pending = false;
  --store.pending;
  if (success) {
    entry.mask = std::move(job.mask);
    entry.retry_after = 0;
  } else {
    ++entry.failures;
    entry.retry_after = g_get_monotonic_time() +
      std::min<gint64>(300, 5LL << std::min(entry.failures - 1, 6u)) * G_USEC_PER_SEC;
    // Avoid logging URLs: custom providers may include credentials.
    g_debug("Water tile unavailable; retry scheduled (%d/%d/%d)", std::get<0>(job.key), std::get<1>(job.key), std::get<2>(job.key));
  }
  gtk_widget_queue_draw(GTK_WIDGET(widget));
}

void upload_atlas(WaterAtlas &atlas, const TileRange &range, const WaterTileStore &store)
{
  unsigned long revision = 1;
  for (int y = range.y_min; y <= range.y_max; ++y)
    for (int x = range.x_min; x <= range.x_max; ++x) {
      const auto it = store.tiles.find(tile_key(range.z, x, y));
      if (it != store.tiles.end() && !it->second.mask.empty()) ++revision;
    }
  // Masks are immutable within a provider's store. A completion outside this
  // atlas must not re-upload its pixels or regenerate its mip chain.
  if (atlas.texture && atlas.range.key() == range.key() && atlas.revision == revision) return;
  const int width = range.width_tiles() * kWaterTilePixels;
  const int height = range.height_tiles() * kWaterTilePixels;
  std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 2, 0);
  atlas.has_water = false;
  for (int y = range.y_min; y <= range.y_max; ++y) {
    for (int x = range.x_min; x <= range.x_max; ++x) {
      auto it = store.tiles.find(tile_key(range.z, x, y));
      if (it == store.tiles.end() || it->second.mask.empty()) continue;
      const auto &mask = it->second.mask;
      atlas.has_water = atlas.has_water || std::any_of(mask.begin(), mask.end(), [](unsigned char v) { return v != 0; });
      for (int row = 0; row < kWaterTilePixels; ++row) {
        const std::size_t destination = (static_cast<std::size_t>((y - range.y_min) * kWaterTilePixels + row) * width +
                                          (x - range.x_min) * kWaterTilePixels) * 2;
        for (int column = 0; column < kWaterTilePixels; ++column) {
          pixels[destination + column * 2] = mask[row * kWaterTilePixels + column];
          // Known land must override a coarse water mask; missing tiles must not.
          pixels[destination + column * 2 + 1] = 255;
        }
      }
    }
  }
  if (!atlas.texture) glGenTextures(1, &atlas.texture);
  glBindTexture(GL_TEXTURE_2D, atlas.texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width, height, 0, GL_RG, GL_UNSIGNED_BYTE, pixels.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenerateMipmap(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);
  atlas.range = range;
  atlas.revision = revision;
}
}

WaterTiles::~WaterTiles() { cancel(); }
void WaterTiles::cancel()
{
  if (store) g_cancellable_cancel(store->cancellable);
  store.reset();
  configuration.clear();
  near_atlas.range = mid_atlas.range = far_atlas.range = TileRange();
  near_atlas.has_water = mid_atlas.has_water = far_atlas.has_water = false;
}

void WaterTiles::destroy_gl()
{
  cancel();
  if (near_atlas.texture) glDeleteTextures(1, &near_atlas.texture);
  if (mid_atlas.texture) glDeleteTextures(1, &mid_atlas.texture);
  if (far_atlas.texture) glDeleteTextures(1, &far_atlas.texture);
  near_atlas = WaterAtlas();
  mid_atlas = WaterAtlas();
  far_atlas = WaterAtlas();
}

void WaterTiles::update(GtkWidget *widget, const std::string &source, const std::string &directory,
                        bool cache_enabled, double latitude, double longitude, double altitude, double radius_m)
{
  const std::string key = source + "\n" + directory + (cache_enabled ? "\n1" : "\n0");
  if (key != configuration) { cancel(); configuration = key; store = std::make_shared<WaterTileStore>(); }
  if (!store) return;
  const double circumference = 40075016.68557849 * std::max(0.02, std::cos(deg_to_rad(latitude)));
  const int near_zoom = std::clamp(static_cast<int>(std::floor(std::log2(circumference / std::max(2500.0, altitude * 2.0)))), 0, 14);
  const int far_zoom = std::clamp(static_cast<int>(std::floor(std::log2(circumference / std::max(5000.0, radius_m)))), 0, near_zoom);
  const TileRange near = centered_range(latitude, longitude, near_zoom);
  const TileRange mid = centered_range(latitude, longitude, std::max(far_zoom, near_zoom - 2));
  const TileRange far = centered_range(latitude, longitude, far_zoom);
  store->wanted.clear();
  std::vector<std::pair<double, TileKey>> candidates;
  for (const TileRange &range : {near, mid, far}) {
    const double cx = slippy_tile_x_for_longitude(longitude, range.z), cy = slippy_tile_y_for_latitude(latitude, range.z);
    for (int y = range.y_min; y <= range.y_max; ++y)
      for (int x = range.x_min; x <= range.x_max; ++x) {
        auto tile = tile_key(range.z, x, y);
        if (!store->wanted.insert(tile).second) continue;
        auto &entry = store->tiles[tile];
        if (entry.mask.empty() && !entry.pending && entry.retry_after <= g_get_monotonic_time())
          candidates.emplace_back(std::hypot(x + 0.5 - cx, y + 0.5 - cy) + (range.z == near.z ? 0.0 : 0.5), tile);
      }
  }
  std::sort(candidates.begin(), candidates.end());
  for (const auto &candidate : candidates) {
    if (store->pending >= 4) break;
    const auto [z, x, y] = candidate.second;
    auto *job = new TileJob{store, candidate.second, tile_url(source, z, x, y),
                            water_cache_path(directory, source, z, x, y), {}, cache_enabled, {}};
    store->tiles[candidate.second].pending = true;
    ++store->pending;
    GTask *task = g_task_new(widget, store->cancellable, tile_ready, nullptr);
    g_task_set_task_data(task, job, [](gpointer p) { delete static_cast<TileJob *>(p); });
    g_task_run_in_thread(task, fetch_tile);
    g_object_unref(task);
  }
  for (auto it = store->tiles.begin(); it != store->tiles.end() && store->tiles.size() > 96;) {
    if (!it->second.pending && !store->wanted.count(it->first)) it = store->tiles.erase(it);
    else ++it;
  }
  upload_atlas(near_atlas, near, *store);
  upload_atlas(mid_atlas, mid, *store);
  upload_atlas(far_atlas, far, *store);
}

void WaterTiles::bind(GLuint program) const
{
  const double span = 40075016.68557849 / std::exp2(near_atlas.range.z);
  glUniform1f(glGetUniformLocation(program, "water_tile_meters"), span);
  // An integer number of periods around the world keeps waves continuous
  // when the camera and the atlas cross the dateline.
  glUniform2f(glGetUniformLocation(program, "water_wave_origin"),
               std::fmod(near_atlas.range.x_min * span, 40075016.68557849 / 9784.0),
               std::fmod(near_atlas.range.y_min * span, 40075016.68557849 / 9784.0));
  int unit = 7;
  for (const auto *atlas : {&near_atlas, &mid_atlas, &far_atlas}) {
    const std::string name = unit == 7 ? "water_near" : (unit == 8 ? "water_mid" : "water_far");
    glUniform1i(glGetUniformLocation(program, (name + "_valid").c_str()), atlas->texture && atlas->range.valid());
    glUniform1i(glGetUniformLocation(program, (name + "_texture").c_str()), unit);
    glUniform4f(glGetUniformLocation(program, (name + "_range").c_str()), atlas->range.z, atlas->range.x_min, atlas->range.y_min, 0);
    glUniform2f(glGetUniformLocation(program, (name + "_size").c_str()), atlas->range.width_tiles(), atlas->range.height_tiles());
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, atlas->texture);
    ++unit;
  }
}

bool WaterTiles::ready() const
{
  if (!store || store->wanted.empty()) return false;
  for (const auto &key : store->wanted) {
    auto it = store->tiles.find(key);
    if (it == store->tiles.end() || it->second.mask.empty()) return false;
  }
  return true;
}

bool WaterTiles::wants_frame(bool animate) const
{
  if (animate && (near_atlas.has_water || mid_atlas.has_water || far_atlas.has_water)) return true;
  if (!store) return false;
  for (const auto &key : store->wanted) {
    auto it = store->tiles.find(key);
    if (it != store->tiles.end() && !it->second.pending && it->second.mask.empty() &&
        it->second.retry_after <= g_get_monotonic_time()) return true;
  }
  return false;
}
} // namespace gworld_scene
