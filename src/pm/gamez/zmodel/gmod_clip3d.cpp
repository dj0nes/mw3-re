#include "pm/gamez/zmodel/gmod_clip3d.h"

#include <cstring>

#include "gamez/zutil/zfix.h"

#pragma STDC FP_CONTRACT OFF

// How these differ from MW3's: docs/pm-map.md, gmod_matl.c.
namespace pm::zmod {
namespace {

// The carried values, 0x0054fea0's and 0x00550100's mask (bits 1, 2 and 8
// without 0x40 are the software clippers', not ported).
enum : uint32_t {
  kRec10 = 0x04,    // the record's +0x10
  kUv = 0x08,       // the TL vertex's tu, tv
  kRecColour = 0x10,  // the record's +0x14..+0x1c
  kShadows = 0x20,
  kPlain = 0x40,    // always set
  kRecLight = 0x80,  // the record's +0..+8
};
constexpr uint32_t kRecAny = kRec10 | kRecColour | kRecLight;
// FIX (bug): the exe's scratch (0x006884d0, 0x00688860, 0x00689048..) holds 64
// vertices and nothing checks it; polygons allow 1,023 and shipped ones reach
// 313. One plane cuts a polygon to at most 1.5n vertices (a zigzag): every one
// fits here (fixes.md).
constexpr int kVerts = 1536;

float g_pos[kVerts][3];      // 0x006884d0
float g_rec[kVerts][8];      // 0x00689048
float g_uv[kVerts][2];       // 0x00688860 (0x20 apart)
float g_shadow_uv[7][kVerts][2];    // 0x006866d0
float g_shadow_xyz[7][kVerts][3];   // 0x006851c0

Clip3dState g_state;

float lerp(float a, float b, float t) { return (b - a) * t + a; }

// Pirate's Moon 1.0 @ 0x0054fea0: vertex n of the scratch, on the edge from a to b.
void carry_cut(const ClipPoly& p, uint32_t bits, int a, int b, float t, int n) {
  const float* ra = p.rec ? p.rec + a * 8 : nullptr;
  const float* rb = p.rec ? p.rec + b * 8 : nullptr;
  if (bits & kRecLight)
    for (int i = 0; i < 3; ++i) g_rec[n][i] = lerp(ra[i], rb[i], t);
  if (bits & kRec10) g_rec[n][4] = lerp(ra[4], rb[4], t);
  if (bits & kRecColour)
    for (int i = 5; i < 8; ++i) g_rec[n][i] = lerp(ra[i], rb[i], t);
  if (bits & kUv)
    for (int i = 0; i < 2; ++i) g_uv[n][i] = lerp(p.tl[a * 8 + 6 + i], p.tl[b * 8 + 6 + i], t);
  if (bits & kShadows)
    for (int s = 0; s < p.shadows->count; ++s) {
      for (int i = 0; i < 2; ++i) g_shadow_uv[s][n][i] = lerp(p.shadows->uv[s][a * 2 + i], p.shadows->uv[s][b * 2 + i], t);
      if (p.shadows->kind[s] == 2)
        for (int i = 0; i < 3; ++i) g_shadow_xyz[s][n][i] = lerp(p.shadows->xyz[s][a * 3 + i], p.shadows->xyz[s][b * 3 + i], t);
    }
}

// A vertex inside the plane, unchanged.
void carry_vertex(const ClipPoly& p, uint32_t bits, int v, int n) {
  if (bits & kRecLight) std::memcpy(g_rec[n], p.rec + v * 8, 12);
  if (bits & kRec10) g_rec[n][4] = p.rec[v * 8 + 4];
  if (bits & kRecColour) std::memcpy(g_rec[n] + 5, p.rec + v * 8 + 5, 12);
  if (bits & kUv) std::memcpy(g_uv[n], p.tl + v * 8 + 6, 8);
  if (bits & kShadows)
    for (int s = 0; s < p.shadows->count; ++s) {
      std::memcpy(g_shadow_uv[s][n], p.shadows->uv[s].data() + v * 2, 8);
      if (p.shadows->kind[s] == 2) std::memcpy(g_shadow_xyz[s][n], p.shadows->xyz[s].data() + v * 3, 12);
    }
}

// Pirate's Moon 1.0 @ 0x00550100: the scratch over the polygon's buffers.
void write_back(ClipPoly& p, uint32_t bits, int n) {
  for (int v = 0; v < n; ++v) {
    if (bits & kRecLight) std::memcpy(p.rec + v * 8, g_rec[v], 12);
    if (bits & kRec10) p.rec[v * 8 + 4] = g_rec[v][4];
    if (bits & kRecColour) std::memcpy(p.rec + v * 8 + 5, g_rec[v] + 5, 12);
    if (bits & kUv) std::memcpy(p.tl + v * 8 + 6, g_uv[v], 8);
  }
  if (bits & kShadows)
    for (int s = 0; s < p.shadows->count; ++s) {
      auto& uv = p.shadows->uv[s];
      if (uv.size() < static_cast<size_t>(n) * 2) uv.resize(static_cast<size_t>(n) * 2);
      std::memcpy(uv.data(), g_shadow_uv[s], static_cast<size_t>(n) * 8);
      if (p.shadows->kind[s] != 2) continue;
      auto& xyz = p.shadows->xyz[s];
      if (xyz.size() < static_cast<size_t>(n) * 3) xyz.resize(static_cast<size_t>(n) * 3);
      std::memcpy(xyz.data(), g_shadow_xyz[s], static_cast<size_t>(n) * 12);
    }
}

// The six's one loop: Sutherland-Hodgman against the near plane from the edge
// last to first. A vertex is inside when near <= z (a NaN is not). The cut's z
// is the near value's own bits. A polygon that grew moves to the arenas.
int clip(ClipPoly& p, uint32_t bits) {
  Clip3dState& s = g_state;
  const float near_z = s.near_z;
  int n = 0;
  int prev = p.count - 1;
  for (int cur = 0; cur < p.count; prev = cur++) {
    const float* a = p.pos + prev * 3;
    const float* b = p.pos + cur * 3;
    const bool inside = near_z <= b[2];
    if ((near_z <= a[2]) != inside) {
      const float t = (near_z - a[2]) / (b[2] - a[2]);
      g_pos[n][2] = near_z;
      g_pos[n][0] = (b[0] - a[0]) * t + a[0];
      g_pos[n][1] = (b[1] - a[1]) * t + a[1];
      carry_cut(p, bits, prev, cur, t, n);
      ++n;
    }
    if (inside) {
      std::memcpy(g_pos[n], b, 12);
      carry_vertex(p, bits, cur, n);
      ++n;
    }
  }
  if (p.count < n) {
    p.pos = s.arena_pos;
    p.tl = s.arena_tl;
    p.extra = s.arena_extra;
    s.arena_pos += n * 3;
    s.arena_tl += n * 8;
    s.arena_extra += n;
    if (bits & kRecAny) {  // 0x00549440 leaves the record where it is
      p.rec = s.arena_rec;
      s.arena_rec += n * 8;
    }
  }
  if (n < 3) {
    p.count = 0;
    return 0;
  }
  p.count = n;
  std::memcpy(p.pos, g_pos, static_cast<size_t>(n) * 12);
  write_back(p, bits, n);
  return 1;
}

// The side clippers' two scratch sets, a pass reading one and writing the
// other: 0x00688848 / 0x00689868 (TL), 0x00689048 / 0x0068a068 (records),
// 0x006866d0 / 0x006874d0 (shadow UVs), 0x006851c0 / 0x00683cc0 (shadow xyz).
// FIX (bug): the exe's hold 64 vertices. Four passes at 1.5n from the near
// clip's 1,534 fit in these (fixes.md).
constexpr int kSideVerts = 7768;
struct SideSet {
  float tl[kSideVerts][8];
  float rec[kSideVerts][8];
  float uv[7][kSideVerts][2];
  float xyz[7][kSideVerts][3];
};
SideSet g_side[2];

// What a pass reads: the polygon's buffers for the first, a set after that.
struct SideSource {
  const float* tl;
  const float* rec;
  const float* uv[7];
  const float* xyz[7];
};

template <typename Inside>
int side_pass(const ClipPoly& p, uint32_t bits, const SideSource& from, SideSet& to, int count, int axis, float plane,
              Inside inside) {
  const int other = 1 - axis;
  const int sets = bits & kShadows ? p.shadows->count : 0;
  int n = 0;
  int prev = count - 1;
  for (int cur = 0; cur < count; prev = cur++) {
    const float* a = from.tl + prev * 8;
    const float* b = from.tl + cur * 8;
    const bool in = inside(b[axis]);
    if (inside(a[axis]) != in) {
      const float t = (plane - a[axis]) / (b[axis] - a[axis]);
      float* o = to.tl[n];
      o[axis] = plane;
      o[other] = (b[other] - a[other]) * t + a[other];
      o[2] = (b[2] - a[2]) * t + a[2];
      float wa = 0, wb = 0;  // perspective-correct, by rhw
      if (bits != kPlain) {
        const float w = 1.0f / o[2];
        const float t1 = t * w;
        wa = (w - t1) * a[2];
        wb = t1 * b[2];
      }
      auto mix = [&](float va, float vb) { return wa * va + wb * vb; };
      const float* ra = from.rec ? from.rec + prev * 8 : nullptr;
      const float* rb = from.rec ? from.rec + cur * 8 : nullptr;
      if (bits & kRecLight)
        for (int i = 0; i < 3; ++i) to.rec[n][i] = mix(ra[i], rb[i]);
      if (bits & kRec10) to.rec[n][4] = mix(ra[4], rb[4]);
      if (bits & kRecColour)
        for (int i = 5; i < 8; ++i) to.rec[n][i] = mix(ra[i], rb[i]);
      if (bits & kUv)
        for (int i = 6; i < 8; ++i) o[i] = mix(a[i], b[i]);
      for (int s = 0; s < sets; ++s) {
        for (int i = 0; i < 2; ++i) to.uv[s][n][i] = mix(from.uv[s][prev * 2 + i], from.uv[s][cur * 2 + i]);
        if (p.shadows->kind[s] == 2)
          for (int i = 0; i < 3; ++i) to.xyz[s][n][i] = mix(from.xyz[s][prev * 3 + i], from.xyz[s][cur * 3 + i]);
      }
      ++n;
    }
    if (in) {
      float* o = to.tl[n];
      std::memcpy(o, b, 12);
      const float* r = from.rec ? from.rec + cur * 8 : nullptr;
      if (bits & kRecLight) std::memcpy(to.rec[n], r, 12);
      if (bits & kRec10) to.rec[n][4] = r[4];
      if (bits & kRecColour) std::memcpy(to.rec[n] + 5, r + 5, 12);
      if (bits & kUv) std::memcpy(o + 6, b + 6, 8);
      for (int s = 0; s < sets; ++s) {
        std::memcpy(to.uv[s][n], from.uv[s] + cur * 2, 8);
        if (p.shadows->kind[s] == 2) std::memcpy(to.xyz[s][n], from.xyz[s] + cur * 3, 12);
      }
      ++n;
    }
  }
  return n;
}

// Pirate's Moon 1.0 @ 0x00552e50
int clip2d(ClipPoly& p, uint32_t bits) {
  const uint32_t codes = p.codes;
  if (!codes) return 1;
  const Clip3dState& st = g_state;
  const int original = p.count;
  SideSource from{p.tl, p.rec, {}, {}};
  const int sets = bits & kShadows ? p.shadows->count : 0;
  for (int s = 0; s < sets; ++s) from.uv[s] = p.shadows->uv[s].data(), from.xyz[s] = p.shadows->xyz[s].data();
  int which = 0;  // the set the next pass writes
  int n = p.count;
  auto pass = [&](uint32_t code, int axis, float plane, auto inside) {
    if (!(codes & code)) return true;
    SideSet& to = g_side[which];
    n = side_pass(p, bits, from, to, n, axis, plane, inside);
    p.count = n;
    from.tl = to.tl[0], from.rec = to.rec[0];
    for (int s = 0; s < sets; ++s) from.uv[s] = to.uv[s][0], from.xyz[s] = to.xyz[s][0];
    which ^= 1;
    return n >= 3;
  };
  // A NaN is outside left and top, inside right and bottom (the x87's compares).
  if (!pass(1, 0, st.left, [&](float v) { return st.left <= v; })) return 0;
  if (!pass(2, 0, st.right, [&](float v) { return !(v >= st.right); })) return 0;
  if (!pass(4, 1, st.top, [&](float v) { return st.top <= v; })) return 0;
  if (!pass(8, 1, st.bottom, [&](float v) { return !(v >= st.bottom); })) return 0;
  if (p.count < 3) return 0;
  if (!(codes & 0xf)) return 1;
  if (original < n) {
    p.tl = g_state.arena_tl;
    p.extra = g_state.arena_extra;
    g_state.arena_tl += n * 8;
    g_state.arena_extra += n;
    if (bits & kRecAny) {
      p.rec = g_state.arena_rec;
      g_state.arena_rec += n * 8;
    }
  }
  for (int v = 0; v < n; ++v) std::memcpy(p.tl + v * 8, from.tl + v * 8, 12);
  for (int v = 0; v < n; ++v) {
    const float* r = from.rec + v * 8;
    float* d = p.rec ? p.rec + v * 8 : nullptr;
    if (bits & kRecLight) std::memcpy(d, r, 12);
    if (bits & kRec10) d[4] = r[4];
    if (bits & kRecColour) std::memcpy(d + 5, r + 5, 12);
    if (bits & kUv) std::memcpy(p.tl + v * 8 + 6, from.tl + v * 8 + 6, 8);
  }
  for (int s = 0; s < sets; ++s) {
    auto& uv = p.shadows->uv[s];
    if (uv.size() < static_cast<size_t>(n) * 2) uv.resize(static_cast<size_t>(n) * 2);
    std::memcpy(uv.data(), from.uv[s], static_cast<size_t>(n) * 8);
    if (p.shadows->kind[s] != 2) continue;
    auto& xyz = p.shadows->xyz[s];
    if (xyz.size() < static_cast<size_t>(n) * 3) xyz.resize(static_cast<size_t>(n) * 3);
    std::memcpy(xyz.data(), from.xyz[s], static_cast<size_t>(n) * 12);
  }
  return 1;
}

}  // namespace

Clip3dState& clip3d_state() { return g_state; }

// Pirate's Moon 1.0 @ 0x00549440
int clip3d_plain(ClipPoly& p) { return clip(p, kPlain); }
// Pirate's Moon 1.0 @ 0x00549630
int clip3d_lit(ClipPoly& p) { return clip(p, kPlain | kRecLight | (p.flags & 8 ? kRec10 : 0)); }
// Pirate's Moon 1.0 @ 0x00549ab0
int clip3d_textured(ClipPoly& p) { return clip(p, kPlain | kUv | kRecColour | (p.flags & 8 ? kRec10 : 0)); }
// Pirate's Moon 1.0 @ 0x0054a450
int clip3d_textured_lit(ClipPoly& p) { return clip(p, kPlain | kRecLight | kRec10 | kRecColour | kUv); }
// Pirate's Moon 1.0 @ 0x0054a6f0
// FIX (bug): the exe does not carry the fog (+0x10), so a cut vertex fogs from what its arena held.
int clip3d_textured_shadow(ClipPoly& p) {
  return clip(p, kPlain | kShadows | kRecColour | kUv | (p.flags & 8 && zfix::options().bugs ? kRec10 : 0));
}
// Pirate's Moon 1.0 @ 0x0054aa50
int clip3d_textured_lit_shadow(ClipPoly& p) {
  return clip(p, kPlain | kRecLight | kShadows | kRecColour | kRec10 | kUv);
}

// Pirate's Moon 1.0 @ 0x0054ade0
int clip2d_plain(ClipPoly& p) { return clip2d(p, kPlain); }
// Pirate's Moon 1.0 @ 0x0054ae00
int clip2d_lit(ClipPoly& p) { return clip2d(p, kPlain | kRecLight | kRec10); }
// Pirate's Moon 1.0 @ 0x0054ae20
int clip2d_textured(ClipPoly& p) { return clip2d(p, kPlain | kRecColour | kUv | kRec10); }
// Pirate's Moon 1.0 @ 0x0054ae40
int clip2d_textured_lit(ClipPoly& p) { return clip2d(p, kPlain | kRecLight | kRecColour | kUv | kRec10); }
// Pirate's Moon 1.0 @ 0x0054ae60, 0x0054ae80
int clip2d_textured_lit_shadow(ClipPoly& p) {
  return clip2d(p, kPlain | kRecLight | kShadows | kRecColour | kUv | kRec10);
}

}  // namespace pm::zmod
