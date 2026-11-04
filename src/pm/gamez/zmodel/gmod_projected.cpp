#include "pm/gamez/zmodel/gmod_projected.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "crt/msvc_math.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zutil/zfix.h"
#include "pm/gamez/zmodel/gmod_cull.h"

#pragma STDC FP_CONTRACT OFF

namespace pm::zmod {
namespace {

// 0xec bytes in the exe, at 0x006a2d48.
struct Vert {
  float pos[3];        // +0x00
  float screen[3];     // +0x0c
  float uv[8][2];      // +0x18  the texture's, then each set's
  float xyz[7][3];     // +0x54..  a light map set's colours
  float fog;           // +0xc0
  float light[3];      // +0xc4
  float color[3];      // +0xd0
  float out[3];        // +0xdc  the light, finished
  int32_t outline;     // +0xe8
};
struct Tri {
  int32_t v[3];
  int32_t across[3];
  int32_t active;
};

constexpr int kMaxTriangles = 2048, kMaxVertices = 6144;  // FIX (bug): as MW3's, checked (fixes.md)
std::vector<Vert> g_vert(kMaxVertices);
std::vector<Tri> g_tri(kMaxTriangles);
int32_t g_verts = 0, g_tris = 0;            // 0x0068a8b8, 0x0068a8c0
const ::zcls::Light* g_lights[128];         // 0x0068a8c4
int32_t g_light_count = 0;                  // 0x0068a8bc
float g_split = 0;                          // 0x00612e3c

float lp(const ::zcls::Light* l, int off) {
  if (off < 0xb0) return l->get<float>(off);
  if (off < 0xe0) return l->pm_colors[(off - 0xb0) / 4];
  return l->get<float>(off - 0x30);
}
uint32_t lflags(const ::zcls::Light* l) { return l->get<uint32_t>(0xb0); }
const float* cam_pos(const ::zcls::Light* l) { return reinterpret_cast<const float*>(l->raw + 0x78); }
float dist2(const float* a, const float* b) {  // 0x00537a00
  const float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
  const float xx = x * x, yy = y * y, zz = z * z;
  const float s = xx + yy;  // in this order: at 24 bits an edge's length then matches the exe's
  return s + zz;
}

void list_lights(const Record& r) {
  const ::zmod::LightList& list = ::zmod::light_list();
  g_light_count = 0;
  for (int32_t i = 0; i < list.count; ++i) {
    const ::zmod::LightEntry& e = list.list[i];
    if (!e.reach) continue;
    const uint32_t f = lflags(e.data);
    if ((r.flags & 0x400) && (f & 0x80)) continue;
    if (!(f & 0x10) || !(f & 0x40)) continue;
    g_lights[g_light_count++] = e.data;
  }
}

// The polygon's mean position, at vertex n; the largest squared distance to it.
float centre_of(int32_t n) {
  float* c = g_vert[static_cast<size_t>(n)].pos;
  for (int k = 0; k < 3; ++k) c[k] = g_vert[0].pos[k] + g_vert[1].pos[k];
  for (int32_t i = 2; i < n; ++i)
    for (int k = 0; k < 3; ++k) c[k] = c[k] + g_vert[static_cast<size_t>(i)].pos[k];
  const float inv = 1.0f / static_cast<float>(n);
  for (int k = 0; k < 3; ++k) c[k] = inv * c[k];
  float most = 0.0f;
  for (int32_t i = 0; i < n; ++i) {
    const float d = dist2(g_vert[static_cast<size_t>(i)].pos, c);
    if (most < d) most = d;
  }
  return most;
}

// Slot s of a vertex's UVs: s = 0 the texture's, s = n + 1 set n's. FIX (bug): the exe's
// test on the set's kind starts one set late, so a first set that is a light map keeps
// old colours at the centre and every split, whatever the last polygon left (fixes.md).
bool mean_colour(const Record& r, int32_t s) {
  if (s < 1 || r.shadows->kind[s - 1] != 2) return false;
  return s >= 2 || zfix::options().bugs;
}

// Pirate's Moon 1.0 @ 0x00561fb0
void vertex_light(int32_t i) {
  Vert& v = g_vert[static_cast<size_t>(i)];
  std::memcpy(v.out, v.light, 12);
  for (int32_t k = 0; k < g_light_count; ++k) {
    const ::zcls::Light* l = g_lights[k];
    const float d2 = dist2(cam_pos(l), v.pos);
    if (!(d2 < lp(l, 0xf0))) continue;
    float f = 1.0f;
    if (lp(l, 0xec) < d2) {
      f = (lp(l, 0xe8) - std::sqrt(d2)) * lp(l, 0xf4);
      f = f * f;
    }
    if (lflags(l) & 0x400) {
      for (int c = 0; c < 3; ++c) v.out[c] = lp(l, 0xd4 + c * 4) * v.color[c] * f + v.out[c];
    } else {
      f = (lp(l, 0xa0) + lp(l, 0x9c)) * f;
      for (int c = 0; c < 3; ++c) v.out[c] = v.color[c] * lp(l, 0xa4 + c * 4) * f + v.out[c];
    }
  }
}

// Pirate's Moon 1.0 @ 0x00562120, as MW3's split with the sets' colours.
void split(const Record& r, int32_t t, int32_t edge) {
  const int32_t next = edge == 2 ? 0 : edge + 1;
  const int32_t prev = edge == 0 ? 2 : edge - 1;
  Tri& T = g_tri[static_cast<size_t>(t)];
  const int32_t n = g_verts;
  Vert& m = g_vert[static_cast<size_t>(n)];
  const Vert& a = g_vert[static_cast<size_t>(T.v[next])];
  const Vert& b = g_vert[static_cast<size_t>(T.v[edge])];
  const auto mid = [](float x, float y) { return (x + y) * 0.5f; };
  for (int k = 0; k < 3; ++k) m.pos[k] = mid(b.pos[k], a.pos[k]);
  const int32_t sets = r.shadows ? r.shadows->count : 0;
  for (int32_t s = 0; s <= sets; ++s) {
    for (int k = 0; k < 2; ++k) m.uv[s][k] = mid(a.uv[s][k], b.uv[s][k]);
    if (mean_colour(r, s))
      for (int k = 0; k < 3; ++k) m.xyz[s - 1][k] = mid(a.xyz[s - 1][k], b.xyz[s - 1][k]);
  }
  m.fog = mid(a.fog, b.fog);
  for (int k = 0; k < 3; ++k) m.light[k] = mid(a.light[k], b.light[k]);
  for (int k = 0; k < 3; ++k) m.color[k] = mid(a.color[k], b.color[k]);
  m.outline = 0;
  vertex_light(n);
  const auto relink = [&](int32_t nb, int32_t from, int32_t to) {
    if (nb < 0) return;
    for (int j = 0; j < 3; ++j) {
      if (g_tri[static_cast<size_t>(nb)].across[j] == from) {
        g_tri[static_cast<size_t>(nb)].across[j] = to;
        return;
      }
    }
  };
  const int32_t k = g_tris++;
  Tri& N = g_tri[static_cast<size_t>(k)];
  N.v[0] = n, N.v[1] = T.v[next], N.v[2] = T.v[prev];
  T.v[next] = n;
  N.across[1] = T.across[next];
  relink(N.across[1], t, k);
  N.across[2] = t;
  T.across[next] = k;
  N.active = 1;
  const int32_t o = T.across[edge];
  if (o < 0) {
    N.across[0] = -1;
    m.outline = 1;
    ++g_verts;
    return;
  }
  Tri& O = g_tri[static_cast<size_t>(o)];
  int32_t j = 0;
  while (j < 3 && O.v[j] != N.v[1]) ++j;
  // The edge is always shared, so j < 3. FIX (bug): as MW3's, stop there.
  if (j == 3) {
    N.across[0] = -1;
    ++g_verts;
    return;
  }
  const int32_t jn = j == 2 ? 0 : j + 1, jp = j == 0 ? 2 : j - 1;
  const int32_t q = g_tris++;
  Tri& P = g_tri[static_cast<size_t>(q)];
  P.v[0] = n, P.v[1] = O.v[jn], P.v[2] = O.v[jp];
  O.v[jn] = n;
  P.across[1] = O.across[jn];
  relink(P.across[1], o, q);
  P.across[2] = o;
  O.across[jn] = q;
  P.active = O.active;
  T.across[edge] = q;
  N.across[0] = o;
  P.across[0] = t;
  O.across[j] = k;
  ++g_verts;
}
}  // namespace

// Pirate's Moon 1.0 @ 0x00562620
int projected_reach(const Record& r) {
  list_lights(r);
  if (g_light_count == 0) return 0;
  const int32_t n = r.count;
  for (int32_t i = 0; i < n; ++i) std::memcpy(g_vert[static_cast<size_t>(i)].pos, r.pos + i * 3, 12);
  const float radius = std::sqrt(centre_of(n));
  const float* c = g_vert[static_cast<size_t>(n)].pos;
  for (int32_t i = 0; i < g_light_count; ++i) {
    const float reach = radius + lp(g_lights[i], 0xe8);
    if (dist2(cam_pos(g_lights[i]), c) < reach * reach) return 1;
  }
  return 0;
}

// Pirate's Moon 1.0 @ 0x00562840
int draw_projected(const Record& r, void (*draw)(const ProjectedTriangle&, const ::zmod::Material&)) {
  list_lights(r);
  if (g_light_count == 0) return 0;
  const int32_t n = r.count;
  for (int32_t i = 0; i < n; ++i) {
    // Pirate's Moon 1.0 @ 0x0053cf40: MW3's 0x00523590 with z before the inverse scale.
    const zmth::Projection& pr = zmth::projection();
    float* v = g_vert[static_cast<size_t>(i)].pos;
    const float z = pr.near_z / r.tl[i * 8 + 2];
    v[2] = z;
    v[0] = (r.tl[i * 8] - pr.cx) * z * pr.inv_fx;
    v[1] = (r.tl[i * 8 + 1] - pr.cy) * v[2] * pr.inv_fy;
  }
  const float radius = std::sqrt(centre_of(n));
  Vert& c = g_vert[static_cast<size_t>(n)];
  float nearest = 3.4028235e+38f;
  for (int32_t i = 0; i < g_light_count; ++i) {
    const ::zcls::Light* l = g_lights[i];
    const float reach = radius + lp(l, 0xe8);
    if (dist2(cam_pos(l), c.pos) < reach * reach) {
      if (lp(l, 0xe8) < nearest) nearest = lp(l, 0xe8);
    } else {
      --g_light_count;
      if (i != g_light_count) {
        g_lights[i] = g_lights[g_light_count];
        --i;
      }
    }
  }
  g_split = nearest * nearest * 0.2f;
  if (g_split < 30.0f) g_split = 30.0f;
  if (g_light_count == 0) return 0;
  const int32_t sets = r.shadows ? r.shadows->count : 0;
  const auto is_map = [&](int32_t s) { return r.shadows->kind[s] == 2; };
  for (int32_t i = 0; i < n; ++i) {
    Vert& v = g_vert[static_cast<size_t>(i)];
    std::memcpy(v.screen, r.tl + i * 8, 12);
    std::memcpy(v.uv[0], r.tl + i * 8 + 6, 8);
    v.fog = r.rec[i * 8 + 4];
    for (int32_t s = 0; s < sets; ++s) {
      std::memcpy(v.uv[s + 1], &r.shadows->uv[s][static_cast<size_t>(i) * 2], 8);
      if (is_map(s)) std::memcpy(v.xyz[s], &r.shadows->xyz[s][static_cast<size_t>(i) * 3], 12);
    }
    std::memcpy(v.color, r.rec + i * 8 + 5, 12);
    std::memcpy(v.light, (r.flags & 0x10) ? r.rec + i * 8 : r.rec + i * 8 + 5, 12);
    v.outline = 0;
    vertex_light(i);
  }
  // The centre takes the mean of the corners'.
  const Vert& v0 = g_vert[0];
  for (int32_t s = 0; s <= sets; ++s) {
    std::memcpy(c.uv[s], v0.uv[s], 8);
    if (mean_colour(r, s)) std::memcpy(c.xyz[s - 1], v0.xyz[s - 1], 12);
  }
  c.fog = v0.fog;
  std::memcpy(c.light, v0.light, 12), std::memcpy(c.color, v0.color, 12);
  for (int32_t i = 1; i < n; ++i) {
    const Vert& v = g_vert[static_cast<size_t>(i)];
    for (int32_t s = 0; s <= sets; ++s) {
      for (int k = 0; k < 2; ++k) c.uv[s][k] = c.uv[s][k] + v.uv[s][k];
      if (mean_colour(r, s))
        for (int k = 0; k < 3; ++k) c.xyz[s - 1][k] = c.xyz[s - 1][k] + v.xyz[s - 1][k];
    }
    c.fog = v.fog + c.fog;
    for (int k = 0; k < 3; ++k) c.light[k] = v.light[k] + c.light[k], c.color[k] = v.color[k] + c.color[k];
  }
  const float inv = 1.0f / static_cast<float>(n);
  for (int32_t s = 0; s <= sets; ++s) {
    for (int k = 0; k < 2; ++k) c.uv[s][k] = inv * c.uv[s][k];
    if (mean_colour(r, s))
      for (int k = 0; k < 3; ++k) c.xyz[s - 1][k] = inv * c.xyz[s - 1][k];
  }
  c.outline = 0;
  c.fog = inv * c.fog;
  for (int k = 0; k < 3; ++k) c.light[k] = inv * c.light[k], c.color[k] = inv * c.color[k];
  vertex_light(n);
  g_tris = n;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t nx = i + 1 == n ? 0 : i + 1;
    Tri& t = g_tri[static_cast<size_t>(i)];
    t.v[0] = i, t.v[1] = nx, t.v[2] = n;
    t.across[0] = -1, t.across[1] = nx, t.across[2] = (i == 0 ? n : i) - 1;
    t.active = 1;
  }
  g_verts = n + 1;
  for (int pass = 0; pass < 6; ++pass) {
    const int32_t now = g_tris;
    for (int32_t ti = 0; ti < now; ++ti) {
      Tri& T = g_tri[static_cast<size_t>(ti)];
      if (!T.active) continue;
      const float* a = g_vert[static_cast<size_t>(T.v[0])].pos;
      const float* b = g_vert[static_cast<size_t>(T.v[1])].pos;
      const float* d = g_vert[static_cast<size_t>(T.v[2])].pos;
      float mid[3];
      for (int k = 0; k < 3; ++k) mid[k] = (a[k] + b[k] + d[k]) * 0.33333331f;
      float most = dist2(mid, a);
      float e = dist2(mid, b);
      if (most < e) most = e;
      e = dist2(mid, d);
      if (most < e) most = e;
      if (!(g_split <= most)) {
        T.active = 0;
        continue;
      }
      const float tr = std::sqrt(most);
      int32_t li = 0;
      for (; li < g_light_count; ++li) {
        const ::zcls::Light* l = g_lights[li];
        const float d2 = dist2(cam_pos(l), mid);
        const float reach = tr + lp(l, 0xe8);
        if (!(d2 < reach * reach)) continue;
        const float inner = lp(l, 0xe4) - tr;
        if (inner < 0.0f || inner * inner < d2) break;
      }
      if (li >= g_light_count) {
        T.active = 0;
        continue;
      }
      const float e01 = dist2(a, b), e12 = dist2(b, d);
      float longest = e01;
      int32_t edge = 0;
      if (longest < e12) longest = e12, edge = 1;
      if (longest < dist2(d, a)) edge = 2;
      // FIX (bug): the exe has room for 2,048 triangles and 6,144 vertices, unchecked.
      const int32_t adds = T.across[edge] < 0 ? 1 : 2;
      if (g_tris + adds > kMaxTriangles || g_verts + 1 > kMaxVertices) {
        T.active = 0;
        continue;
      }
      split(r, ti, edge);
    }
  }
  for (int32_t k = n; k < g_verts; ++k)
    project_point(g_vert[static_cast<size_t>(k)].pos, g_vert[static_cast<size_t>(k)].screen);
  const float scale = static_cast<float>(r.polygon->priority) * ::zmod::draw_state().priority_scale + 1.0f;
  for (int32_t k = 0; k < g_verts; ++k) g_vert[static_cast<size_t>(k)].screen[2] = scale * g_vert[static_cast<size_t>(k)].screen[2];
  for (int32_t k = n + 1; k < g_verts; ++k) {
    Vert& v = g_vert[static_cast<size_t>(k)];
    if (!v.outline) continue;
    const float dx = v.screen[0] - c.screen[0], dy = v.screen[1] - c.screen[1];
    const float len2 = dx * dx + dy * dy;
    if (len2 == 0.0f) continue;
    const float k2 = 1.0f / std::sqrt(len2);
    v.screen[0] = k2 * dx + v.screen[0];
    v.screen[1] = k2 * dy + v.screen[1];
  }
  if (!draw) return 1;
  const ::zmod::Material& mat = *r.polygon->materials[0];
  ClipShadows out_sets;
  if (r.shadows) {
    out_sets.count = sets;
    for (int32_t s = 0; s < sets; ++s) {
      out_sets.texture[s] = r.shadows->texture[s], out_sets.kind[s] = r.shadows->kind[s], out_sets.fade[s] = r.shadows->fade[s];
      out_sets.uv[s].resize(6), out_sets.xyz[s].resize(9);
    }
  }
  for (int32_t ti = 0; ti < g_tris; ++ti) {
    ProjectedTriangle t;
    t.flags = r.flags;
    t.sets = r.shadows ? &out_sets : nullptr;
    for (int j = 0; j < 3; ++j) {
      const Vert& v = g_vert[static_cast<size_t>(g_tri[static_cast<size_t>(ti)].v[j])];
      ProjectedTriangle::Vertex& o = t.v[j];
      o.sx = v.screen[0], o.sy = v.screen[1], o.sz = v.screen[2], o.rhw = v.screen[2];
      o.tu = v.uv[0][0], o.tv = v.uv[0][1];
      o.fog = v.fog;
      o.specular = (r.flags & 8) ? static_cast<uint32_t>(crt::msvc_ftol(255.5f - v.fog * 255.0f)) << 24 : 0xff000000u;
      const auto byte = [](float x) {
        const int32_t w = static_cast<int32_t>(rounded_sum(x, 0.5f));
        return static_cast<uint32_t>(w >= 0x100 ? 0xff : w < 0 ? 0 : w);
      };
      o.diffuse = 0xff000000u | byte(v.out[0]) << 16 | byte(v.out[1]) << 8 | byte(v.out[2]);
      for (int32_t s = 0; s < sets; ++s) {
        std::memcpy(&out_sets.uv[s][static_cast<size_t>(j) * 2], v.uv[s + 1], 8);
        if (is_map(s)) std::memcpy(&out_sets.xyz[s][static_cast<size_t>(j) * 3], v.xyz[s], 12);
      }
    }
    draw(t, mat);
  }
  return 1;
}

}  // namespace pm::zmod
