#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Tiny, deterministic MVT fixture: square water polygon, optional island.
// Coordinates use the MVT screen convention (clockwise outer, opposite hole).
namespace water_fixture {
using Bytes = std::vector<unsigned char>;
inline void varint(Bytes &out, unsigned value) {
  while (value >= 128) { out.push_back((value & 127) | 128); value >>= 7; }
  out.push_back(value);
}
inline void field(Bytes &out, unsigned id, const Bytes &value) {
  varint(out, id * 8 + 2); varint(out, value.size());
  out.insert(out.end(), value.begin(), value.end());
}
inline void text(Bytes &out, unsigned id, const std::string &value) {
  field(out, id, Bytes(value.begin(), value.end()));
}
inline Bytes tile(bool island = true, bool intermittent = false, bool tunnel = false,
                   const std::string &name = "water", bool full = false) {
  Bytes geometry;
  int x = 0, y = 0;
  auto ring = [&](std::initializer_list<std::pair<int,int>> points) {
    bool first = true;
    for (auto [px, py] : points) {
      if (first) varint(geometry, 9);
      int dx = px - x, dy = py - y;
      varint(geometry, dx < 0 ? -2 * dx - 1 : 2 * dx);
      varint(geometry, dy < 0 ? -2 * dy - 1 : 2 * dy);
      x = px; y = py;
      if (first) varint(geometry, 26);
      first = false;
    }
    varint(geometry, 15);
  };
  const int lo = full ? 0 : 512, hi = full ? 4096 : 3584;
  ring({{lo,lo},{hi,lo},{hi,hi},{lo,hi}});
  if (island) ring({{1536,1536},{1536,2560},{2560,2560},{2560,1536}});
  Bytes feature = {24, 3}; // polygon type
  field(feature, 4, geometry);
  Bytes tags = {0, 0, 1, 1};
  field(feature, 2, tags);
  Bytes layer = {120, 2}; // version 2
  text(layer, 1, name);
  field(layer, 2, feature);
  text(layer, 3, "intermittent"); text(layer, 3, "brunnel");
  field(layer, 4, Bytes{40, static_cast<unsigned char>(intermittent)});
  Bytes value; text(value, 1, tunnel ? "tunnel" : ""); field(layer, 4, value);
  varint(layer, 40); varint(layer, 4096);
  Bytes result; field(result, 3, layer); return result;
}
} // namespace water_fixture
