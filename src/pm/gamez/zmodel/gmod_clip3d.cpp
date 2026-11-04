#include "pm/gamez/zmodel/gmod_clip3d.h"

#include <cstring>

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
constexpr int kVerts = 64;  // the scratch: 0x006884d0, 0x00688860, 0x00689048..

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
      for (int i = 0; i < 2; ++i) g_shadow_uv[s][n][i] = lerp(p.shadows->uv[s][a][i], p.shadows->uv[s][b][i], t);
      if (p.shadows->kind[s] == 2)
        for (int i = 0; i < 3; ++i) g_shadow_xyz[s][n][i] = lerp(p.shadows->xyz[s][a][i], p.shadows->xyz[s][b][i], t);
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
      std::memcpy(g_shadow_uv[s][n], p.shadows->uv[s][v], 8);
      if (p.shadows->kind[s] == 2) std::memcpy(g_shadow_xyz[s][n], p.shadows->xyz[s][v], 12);
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
      std::memcpy(p.shadows->uv[s], g_shadow_uv[s], static_cast<size_t>(n) * 8);
      if (p.shadows->kind[s] == 2) std::memcpy(p.shadows->xyz[s], g_shadow_xyz[s], static_cast<size_t>(n) * 12);
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
      if (n >= kVerts) break;  // ours: the exe runs on past its scratch
      const float t = (near_z - a[2]) / (b[2] - a[2]);
      g_pos[n][2] = near_z;
      g_pos[n][0] = (b[0] - a[0]) * t + a[0];
      g_pos[n][1] = (b[1] - a[1]) * t + a[1];
      carry_cut(p, bits, prev, cur, t, n);
      ++n;
    }
    if (inside) {
      if (n >= kVerts) break;
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
int clip3d_textured_shadow(ClipPoly& p) { return clip(p, kPlain | kShadows | kRecColour | kUv); }
// Pirate's Moon 1.0 @ 0x0054aa50
int clip3d_textured_lit_shadow(ClipPoly& p) {
  return clip(p, kPlain | kRecLight | kShadows | kRecColour | kRec10 | kUv);
}

}  // namespace pm::zmod
