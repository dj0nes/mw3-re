#include "pm/gamez/zmodel/gmod_draw.h"

#include <cmath>
#include <cstring>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/camera.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zimage/zimg_library.h"
#include "crt/msvc_math.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zmodel/gmod_bake.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zrender/zrndr_fog.h"
#include "pm/gamez/zimage/zimg_texture.h"
#include "pm/gamez/zmodel/gmod_clip3d.h"
#include "pm/gamez/zmodel/gmod_cull.h"
#include "pm/gamez/zmodel/gmod_projected.h"

#pragma STDC FP_CONTRACT OFF

namespace pm::zmod {
void project_point(const float cam[3], float screen[3]) {
  const zmth::Projection& p = zmth::projection();
  const float w = p.near_z / cam[2];
  screen[2] = w;
  screen[0] = w * cam[0] * p.fx_near + p.cx;
  screen[1] = p.fy_near * cam[1] * screen[2] + p.cy;
}

namespace {
DrawState g_state;

// As MW3's (gmod_draw.cpp): written out here so the sums do not fuse.
void transform_point(const float* v, const float* t, float* out) {
  const float x0 = v[0] * t[0], x1 = v[0] * t[1], x2 = v[0] * t[2];
  const float y0 = v[1] * t[3], y1 = v[1] * t[4], y2 = v[1] * t[5];
  const float z0 = v[2] * t[6], z1 = v[2] * t[7], z2 = v[2] * t[8];
  const float a0 = x0 + y0, a1 = x1 + y1, a2 = x2 + y2;
  const float b0 = a0 + z0, b1 = a1 + z1, b2 = a2 + z2;
  out[0] = b0 + t[9], out[1] = b1 + t[10], out[2] = b2 + t[11];
}

float factor_of(const Model& m) {
  float f;
  std::memcpy(&f, m.header + 0x24, 4);
  return f;
}

// Model flag 8 with a morph factor and count (a NaN factor counts as zero).
bool morphing(const Model& m) {
  const float f = factor_of(m);
  return (m.count(8) & 8) && (f < 0.0f || f > 0.0f) && m.count(0x1c) != 0;
}

// 0x0053bba0, 0x0053c280: blended, then transformed in place.
void morph_vertices(const Model& m) {
  std::vector<float>& cam = g_state.cam;
  const int32_t k = m.count(0x1c), n = m.count(0x14);
  const float f = factor_of(m);
  for (int32_t i = 0; i < k * 3; ++i) {
    const float d = m.morphs[i] * f;
    cam[i] = m.vertices[i] + d;
  }
  if (zmth::matrix_stack().is_identity()) return;
  const float* t = zmth::matrix_stack().matrix().m;
  for (int32_t i = 0; i < n; ++i) {
    const float v[3] = {cam[i * 3], cam[i * 3 + 1], cam[i * 3 + 2]};
    transform_point(v, t, &cam[i * 3]);
  }
}

void draw_points(Model& m) {
  std::vector<float>& cam = g_state.cam;
  const int32_t n = m.count(0x14);
  if (n > 0 && cam.size() < static_cast<size_t>(n) * 3) cam.resize(static_cast<size_t>(n) * 3);
  if (m.count(0x1c) > n && cam.size() < static_cast<size_t>(m.count(0x1c)) * 3) cam.resize(static_cast<size_t>(m.count(0x1c)) * 3);
  zmth::load_model_view();
  if (morphing(m)) {
    morph_vertices(m);
  } else if (n > 0) {  // FIX (bug): a do-while in the exe (0 runs 2^32 times)
    if (zmth::matrix_stack().is_identity()) {
      std::memcpy(cam.data(), m.vertices, static_cast<size_t>(n) * 12);
    } else {
      const float* t = zmth::matrix_stack().matrix().m;
      for (int32_t i = 0; i < n; ++i) transform_point(m.vertices + i * 3, t, &cam[i * 3]);
    }
  }
  // Polygon 0's first material's colour. FIX (bug): no polygons or material draws
  // nothing here (the exe reads through a null pointer).
  if (!m.polygons || !m.polygons[0].materials || !m.polygons[0].materials[0]) return;
  const uint32_t color = m.polygons[0].materials[0]->rgb;
  const ::zmod::DrawState& s = ::zmod::draw_state();
  ::zmod::DrawTable& table = ::zmod::draw_table();
  const float near_z = clip3d_state().near_z;
  const auto below = [](float a, float b) { return !(a >= b); };
  for (int32_t i = 0; i < n; ++i) {
    if (!(cam[i * 3 + 2] > near_z)) continue;
    float screen[3];
    project_point(&cam[i * 3], screen);
    if (below(screen[0], s.point_rect[0]) || below(screen[1], s.point_rect[1])) continue;
    if (screen[0] > s.point_rect[2] || screen[1] > s.point_rect[3]) continue;
    if (table.point) table.point(table.ctx, screen, color, 0xff);
  }
}
}  // namespace

DrawState& draw_state() { return g_state; }

// Pirate's Moon 1.0 @ 0x005416e0, MW3's 0x0053dcf0 on PM's polygons: polygons
// last to first, morphed.
void facade_centre(const Model& m, float out[3]) {
  out[0] = out[1] = out[2] = 0.0f;
  const int32_t polys = m.count(0x10);
  if (polys == 0) return;
  int32_t total = 0;
  const bool morph = m.count(8) & 8;
  const float f = factor_of(m);
  for (int32_t i = polys - 1; i >= 0; --i) {
    const Polygon& p = m.polygons[i];
    const int32_t n = static_cast<int32_t>(p.flags & 0x3ff);
    total += n;
    for (int32_t j = n - 1; j >= 0; --j) {
      const uint32_t at = p.vertex_indices[j];
      const float* v = m.vertices + 3 * at;
      out[0] = out[0] + v[0], out[1] = out[1] + v[1], out[2] = out[2] + v[2];
      if (morph && m.count(0x1c) > static_cast<int32_t>(at)) {
        const float* d = m.morphs + 3 * at;
        const float dx = f * d[0], dy = d[1] * f, dz = d[2] * f;
        out[0] = out[0] + dx, out[1] = out[1] + dy, out[2] = out[2] + dz;
      }
    }
  }
  const float r = 1.0f / static_cast<float>(total);
  out[0] = r * out[0], out[1] = r * out[1], out[2] = r * out[2];
}

// Pirate's Moon 1.0 @ 0x00542a70
int prepare_model(Model& m, float centre[3]) {
  switch (m.count(0)) {
    case 0:
      zmth::load_model_view();
      g_state.default_model = 1;
      return 1;
    case 1: {
      const bool about = m.count(8) & 0x40;
      if (about) facade_centre(m, centre);
      ::zmod::facade_turn(m.count(4), about, centre);
      g_state.default_model = 0;
      return 1;
    }
    case 2:  // the only case that pops the draw's push: it returns 0, with no draw after
      draw_points(m);
      zmth::pop();  // 0x005384a0
      g_state.default_model = 0;
      return 0;
    default:
      zmth::load_model_view();
      g_state.default_model = 0;
      return 1;
  }
}

namespace {
constexpr int32_t kMaxVerts = 1024;  // the plane codes' array; the largest shipped model has 500

template <typename T> T hdr(const Model& m, int off) {
  T v;
  std::memcpy(&v, m.header + off, sizeof v);
  return v;
}

// As MW3's texture_alpha: the image's flag 2, 0x00543b10 reads through without a test.
bool has_alpha(const ::zmod::Material& mat) {
  if (!mat.texture || !mat.texture->image) return false;
  return (static_cast<const ::zimg::Texture*>(mat.texture->image)->flags & 2) != 0;
}

// The device's fog colour, as the shared fog_vertices (the same lines at 0x005443f0).
void push_fog_color() {
  zrndr::DeviceFog& d = zrndr::device_fog();
  const ::zmod::Fog& f = ::zmod::fog();
  for (int i = 0; i < 3; ++i) d.color[i] = f.color[i] * 255.0f;
  if (d.sent[0] != d.color[0] || d.sent[1] != d.color[1] || d.sent[2] != d.color[2]) {
    for (int i = 0; i < 3; ++i) d.sent[i] = d.color[i];
    if (d.send) d.send();
  }
}

// The polygon's first three positions: (p2 - p1) x (p0 - p1), and n . p0 (MW3's is
// the other way round, and its test too).
template <typename R>  // what the x87 keeps between stores: float at 24 bits, double at 53
float face_at(const float* p, float* n) {
  const R e2x = R{p[6]} - R{p[3]}, e2y = R{p[7]} - R{p[4]}, e2z = R{p[8]} - R{p[5]};
  const float e0x = p[0] - p[3], e0y = p[1] - p[4];  // stored; the rest stay on the x87's stack
  const R e0z = R{p[2]} - R{p[5]};
  n[0] = static_cast<float>(e0z * e2y - R{e0y} * e2z);
  n[1] = static_cast<float>(R{e0x} * e2z - e0z * e2x);
  n[2] = static_cast<float>(R{e0y} * e2x - R{e0x} * e2y);
  const R x = R{n[0]} * R{p[0]}, y = R{n[1]} * R{p[1]}, z = R{n[2]} * R{p[2]};
  const R s = x + y;
  return static_cast<float>(s + z);
}
}  // namespace
float face(const float* p, float* n) { return zfix::options().fpu53 ? face_at<double>(p, n) : face_at<float>(p, n); }

// Pirate's Moon 1.0 @ 0x0055ebf0. MW3's lights_reaching (0x00543d90) without its
// light values and fog mix: PM works those out by polygon (0x005602e0).
int32_t lights_reaching(const float centre[3], float radius) {
  ::zmod::LightList& list = ::zmod::light_list();
  const bool d3d = zrndr::device_fog().active != 0;
  int32_t reached = 0;
  for (int32_t i = 0; i < list.count; ++i) {
    ::zmod::LightEntry& e = list.list[i];
    const uint32_t f = e.data->get<uint32_t>(0xb0);  // PM +0xe0
    if ((d3d && (f & 8)) || !(f & 2)) {
      e.full = 1, e.reach = 1;
      ++reached;
      continue;
    }
    e.full = 0, e.reach = 0;
    if (!(f & 0x800)) continue;
    float d;
    if (f & 8) {
      d = centre[2];
      list.directed = i;
    } else {
      const float* p = e.data->floats(0x78);
      const float x = p[0] - centre[0], y = p[1] - centre[1], z = p[2] - centre[2];
      d = std::sqrt(x * x + y * y + z * z);  // 0x00537a50
    }
    if (!(d - radius < e.data->get<float>(0xb8)) && !(f & 8)) continue;  // PM +0xe8
    e.reach = 1;
    if (d + radius < e.data->get<float>(0xb4)) e.full = 1;  // PM +0xe4
    ++reached;
  }
  return reached;
}

int begin_model(::zcls::Node& node, Model& m, uint32_t mask, ModelPass& pass) {
  pass = ModelPass{};
  const int32_t nv = m.count(0x14), nn = m.count(0x18);
  if (!m.polygons || m.count(0x10) == 0) return 0;
  // FIX (bug): the exe's buffers are unchecked; a larger model is refused (fixes.md).
  if (nv > kMaxVerts || hdr<int32_t>(m, 0x1c) > kMaxVerts) return 0;
  pass.mask = mask;
  cull_state().planes = mask;
  const uint32_t flags = hdr<uint32_t>(m, 8);
  const float* centre = reinterpret_cast<const float*>(m.header + 0x48);
  float c[3];
  if (zmth::matrix_stack().is_identity()) std::memcpy(c, centre, 12);
  else transform_point(centre, zmth::matrix_stack().matrix().m, c);
  const float radius = hdr<float>(m, 0x54);
  uint32_t needs = 0;
  const int32_t fog = ::zmod::fog().on;
  if (fog != 0 && (fog == 2 || (flags & 2)) && double{::zmod::fog_sphere(c, radius)} > 0.003921569) needs = 1;
  const ::zmod::Lighting& lit = ::zmod::lighting();
  if ((*lit.mask & 1) && ::zmod::light_list().any && (flags & 1) && lights_reaching(c, radius) > 0) needs |= 2;
  if (lit.tint_on) needs |= 4;

  std::vector<float>& cam = g_state.cam;
  const int32_t morphs = hdr<int32_t>(m, 0x1c);
  const size_t want = static_cast<size_t>(nv > morphs ? nv : morphs) * 3;
  if (cam.size() < want) cam.resize(want);
  if ((flags & 8) && !(hdr<float>(m, 0x24) == 0.0f) && morphs != 0) {
    morph_vertices(m);
  } else if (nv > 0) {
    if (zmth::matrix_stack().is_identity()) {
      std::memcpy(cam.data(), m.vertices, static_cast<size_t>(nv) * 12);
    } else {
      const float* t = zmth::matrix_stack().matrix().m;
      for (int32_t i = 0; i < nv; ++i) transform_point(m.vertices + i * 3, t, &cam[i * 3]);
    }
  }
  if (mask != 0 && plane_codes(cam.data(), nv, cull_state().codes) == 0) return 0;
  if (needs & 1) {
    ::zmod::fog_calc().verts = reinterpret_cast<const float(*)[3]>(cam.data());
    if (!::zmod::fog_vertices(nv)) needs &= ~1u;
  }
  if (::zmod::draw_state().vertex_shading && nn != 0) {
    g_state.normals.resize(static_cast<size_t>(nn) * 3);
    zmth::rotate_points_to(reinterpret_cast<const zmth::Vec3*>(m.normals), reinterpret_cast<zmth::Vec3*>(g_state.normals.data()),
                           static_cast<uint32_t>(nn));
    for (int32_t i = 0; i < nn; ++i) ::zmod::normalize(&g_state.normals[static_cast<size_t>(i) * 3]);
  }
  pass.needs = needs;
  const float fade = ::zmod::draw_state().opacity;
  if (!(fade < 1.0f) && (flags & 1)) {
    pass.shadows = ::zmod::draw_state().shadows != 0;
    if (node.get<uint32_t>(0xbc) < 2) {
      pass.light_maps = pm::zimg::light_map() != nullptr && g_state.light_map_option != -1;
      const ::zmod::LightList& list = ::zmod::light_list();
      for (int32_t i = 0; i < list.count; ++i) {
        const uint32_t f = list.list[i].data->get<uint32_t>(0xb0);
        if (list.list[i].reach && (f & 0x10) && (f & 0x40)) {
          pass.projected = true;
          break;
        }
      }
    }
  }
  // The static bake: a type-0 model under one parent lit this frame.
  if (::zmod::draw_state().static_lights && m.count(0) == 0 && m.count(0xc) <= 1 && (needs & 2)) {
    if (node.get<uint32_t>(0xc0) >= ::zcls::frames().update_frame) {
      pass.bake = true;
    } else if (node.get<uint32_t>(0xc4) <= ::zcls::frames().frame) {
      bake_static_light(m);
      node.put<uint32_t>(0xc0, ::zcls::frames().update_frame);  // 0x004ff770
      pass.bake = true;
    }
  }
  pass.cull = !(m.count(0) == 1 && (m.count(4) == 0 || m.count(4) == 1));
  return 1;
}

int build_records(Model& m, const MaterialRef& ref, const ModelPass& pass, RefPass& out) {
  out.records.clear(), out.shadows.clear();
  out.strips = 0;
  ::zmod::Material& mat = *ref.polygon->materials[0];
  const bool textured = (mat.flags & 1) != 0;
  const bool opaque = textured && !has_alpha(mat);
  out.projected = pass.needs != 0 && pass.projected && opaque;
  out.light_maps = pass.light_maps && opaque;
  if (mat.alpha == 0 || ::zmod::draw_state().opacity == 0.0f) return 0;
  const float tolerance = ::zmod::draw_state().backface;
  const int32_t fog = ::zmod::fog().on;
  const float* factor = ::zmod::fog_calc().factor;
  // The arenas: each polygon's vertices, with room for what the clippers add.
  size_t verts = 0, tris = 0;
  for (int32_t i = 0; i < ref.usage_count; ++i) {
    const size_t n = ref.polygon[i].flags & 0x3ff;
    verts += n + 8;
    if (n > 2) tris += n - 2;
  }
  verts += tris * (3 + 8);  // the strips' and fans' triangles, each with the clippers' room
  const size_t records = static_cast<size_t>(ref.usage_count) + tris;
  out.pos.resize(verts * 3), out.normals.resize(verts * 3);
  out.rec.resize(verts * 8), out.tl.resize(verts * 8);
  out.faces.resize(records * 3);
  out.extra.assign(verts, 0);
  out.records.reserve(records), out.shadows.reserve(records);
  size_t at = 0, normal_at = 0, face_at = 0;
  for (int32_t pi = 0; pi < ref.usage_count; ++pi) {
    Polygon& p = ref.polygon[pi];
    const int32_t n = static_cast<int32_t>(p.flags & 0x3ff);
    float* pos = &out.pos[at * 3];
    for (int32_t i = 0; i < n; ++i) std::memcpy(pos + i * 3, &g_state.cam[static_cast<size_t>(p.vertex_indices[i]) * 3], 12);
    const bool strip = (p.flags & 0x2000) != 0;
    float* normal = strip ? nullptr : &out.faces[face_at];
    if (pass.cull && !strip) {
      const float d = face(pos, normal);
      if (!(d < -tolerance)) {
        if (!(p.flags & 0x400) || !(tolerance < d)) continue;
        normal[0] = -normal[0], normal[1] = -normal[1], normal[2] = -normal[2];
      }
    }
    uint32_t codes = pass.mask;
    if (pass.mask != 0 && polygon_codes(n, reinterpret_cast<const int32_t*>(p.vertex_indices), &codes) == 0) continue;
    Record r;
    r.polygon = &p, r.pos = pos, r.rec = &out.rec[at * 8], r.tl = &out.tl[at * 8];
    r.count = n, r.codes = codes;
    r.extra = &out.extra[at];
    if (pass.needs & 1) {
      for (int32_t i = 0; i < n; ++i) r.rec[i * 8 + 4] = factor[p.vertex_indices[i]];
      for (int32_t i = 0; i < n; ++i) {
        if (double{r.rec[i * 8 + 4]} > 0.003921569) {
          r.flags |= 8;
          push_fog_color();
          break;
        }
      }
      if (r.flags & 8) {
        for (int32_t i = 1; i < n; ++i) {
          if (std::fabs(double{r.rec[i * 8 + 4]} - double{r.rec[4]}) > 0.003921569) {
            r.flags |= 0x20;
            break;
          }
        }
        if (!(r.flags & 0x20) && fog != 2 && r.rec[4] == 1.0f) r.count = 0;  // all fog
      }
      if (r.count == 0 && fog != 2) continue;
    }
    if (mat.flags & 1) {
      const float* uv = p.uvs ? p.uvs[0] : nullptr;
      if (mat.flags & 4) ::zmod::material_cycle(mat);
      for (int32_t i = 0; i < n; ++i) {
        if (uv) std::memcpy(r.tl + i * 8 + 6, uv + i * 2, 8);
        std::memcpy(r.rec + i * 8 + 5, p.colors + i * 3, 12);
      }
    }
    if (::zmod::draw_state().vertex_shading && (p.flags & 0x1000)) {
      float* into = &out.normals[normal_at * 3];
      for (int32_t i = 0; i < n; ++i) std::memcpy(into + i * 3, &g_state.normals[static_cast<size_t>(p.normal_indices[i]) * 3], 12);
      r.normals = into;
      normal_at += static_cast<size_t>(n);
    }
    if (p.material_count > 1) {
      out.shadows.emplace_back();
      ClipShadows& s = out.shadows.back();
      s.count = p.material_count - 1;
      // FIX (bug): the exe's record holds seven sets, unchecked.
      const int32_t sets_here = s.count < 7 ? s.count : 7;
      s.count = sets_here;
      for (int32_t k = 0; k < sets_here; ++k) {
        ::zmod::Material* other = p.materials[k + 1];
        s.texture[k] = other && other->texture ? other->texture->surface : nullptr;
        s.kind[k] = 0;
        if (p.uvs) s.uv[k].assign(p.uvs[k + 1], p.uvs[k + 1] + static_cast<size_t>(n) * 2);
      }
      r.shadows = &s;
    }
    if (strip) ++out.strips;
    r.normal = normal;
    out.records.push_back(r);
    at += static_cast<size_t>(n) + 8;
    if (!strip) face_at += 3;
  }
  out.at = at, out.normal_at = normal_at, out.face_at = face_at;
  return out.records.empty() ? 0 : 1;
}

namespace {
constexpr double kVisible = 0.003921569;  // 0x005f0a60

// A field of PM's light data by PM's own offset: below +0xb0 the shared block, then
// PM's colours (`pm_colors`), then the shared block's +0xb0.. (cls_zbd, read_node).
float lp(const ::zcls::Light* l, int off) {
  if (off < 0xb0) return l->get<float>(off);
  if (off < 0xe0) return l->pm_colors[(off - 0xb0) / 4];
  return l->get<float>(off - 0x30);
}
uint32_t lflags(const ::zcls::Light* l) { return l->get<uint32_t>(0xb0); }
float dot(const float* a, const ::zcls::Light* l) {
  const float x = a[0] * lp(l, 0x90), y = a[1] * lp(l, 0x94), z = a[2] * lp(l, 0x98);
  const float s = x + y;
  return s + z;
}
float clamp255(float v) {
  if (v <= 255.0f) return v < 0.0f ? 0.0f : v;
  return 255.0f;
}
float range(const ::zcls::Light* l, float d) {  // over the light's range
  if (!(d < lp(l, 0xe8))) return 0.0f;
  if (!(lp(l, 0xe4) < d)) return 1.0f;
  return (lp(l, 0xe8) - d) * lp(l, 0xf4);
}
}  // namespace

// Pirate's Moon 1.0 @ 0x005602e0
void sort_lights(RefPass& ref) {
  g_state.directed.clear(), g_state.point.clear(), g_state.spot.clear();
  ::zmod::LightList& list = ::zmod::light_list();
  for (int32_t i = 0; i < list.count; ++i) {
    ::zmod::LightEntry& e = list.list[i];
    if (!e.reach) continue;
    const uint32_t f = lflags(e.data);
    if (f & 8) g_state.directed.push_back(&e);
    else if (f & 4) g_state.spot.push_back(&e);
    else if (!(lp(e.data, 0xa0) == 0.0f) || !(lp(e.data, 0x9c) == 0.0f)) g_state.point.push_back(&e);
  }
  for (Record& r : ref.records) {
    for (int32_t i = 0; i < r.count; ++i) {
      r.rec[i * 8] = r.rec[i * 8 + 1] = r.rec[i * 8 + 2] = 1.0f;
      if (!(r.flags & 8)) r.rec[i * 8 + 4] = 0.0f;
    }
  }
}

// Pirate's Moon 1.0 @ 0x005603f0. Vertices last to first, as the exe.
int shade_polygon(Record& r, const float* baked, uint32_t needs) {
  float* rec = r.rec;
  const float* pos = r.pos;
  const int32_t n = r.count;
  const bool no_shading = (r.flags & 0x80) != 0;
  // FIX (bug): a strip with no vertex normals has no normal at all; the exe reads
  // through null there (fixes.md).
  static const float kNoNormal[3] = {};
  const float* normal = r.normal ? r.normal : kNoNormal;
  const float* normals = r.normals;
  int32_t bright = 0, lit = 0;
  uint32_t fogged = 0;
  const ::zcls::Light* fog_light = nullptr;
  const bool has_baked = baked != nullptr;
  if (needs & 2) {
    if (baked) {
      float sum = 0.0f;
      for (int32_t i = 0; i < n; ++i) {
        float v = baked[i];
        if (!no_shading) v = ::zmod::clouds_at_camera(pos + i * 3) * v;
        v = v - 1.0f;
        rec[i * 8] = v + rec[i * 8], rec[i * 8 + 1] = v + rec[i * 8 + 1], rec[i * 8 + 2] = v + rec[i * 8 + 2];
        sum = std::fabs(v) + sum;
      }
      if (double{sum} > kVisible) bright = 1;
    }
    if (!g_state.directed.empty() || !g_state.spot.empty()) {
      if (r.normal) ::zmod::normalize(r.normal);
    }
    for (const ::zmod::LightEntry* e : g_state.directed) {
      const ::zcls::Light* l = e->data;
      const uint32_t f = lflags(l);
      if ((f & 0x200) && has_baked) continue;  // PM's static light: in the bake
      const bool each = !no_shading && normals;
      float shade = 0.0f;
      if (!no_shading && !normals) {
        const float d = dot(normal, l);
        shade = d > 0.0f ? ((f & 0x400) ? d : d * lp(l, 0x9c)) : 0.0f;
      }
      for (int32_t i = n - 1; i >= 0; --i) {
        if (each) {
          const float d = dot(normals + i * 3, l);
          shade = d > 0.0f ? ((f & 0x400) ? d : d * lp(l, 0x9c)) : 0.0f;
        }
        float* v = rec + i * 8;
        if (f & 0x400) {  // PM's own colours
          const float c = no_shading ? 1.0f : ::zmod::clouds_at_camera(pos + i * 3);
          const float s = no_shading ? shade : c * shade;
          const float a0 = c * lp(l, 0xc8), a1 = c * lp(l, 0xcc), a2 = c * lp(l, 0xd0);
          const float d0 = s * lp(l, 0xbc), d1 = s * lp(l, 0xc0), d2 = s * lp(l, 0xc4);
          const float t0 = a0 + d0, t1 = a1 + d1, t2 = a2 + d2;
          v[0] = (t0 - 1.0f) + v[0], v[1] = (t1 - 1.0f) + v[1], v[2] = (t2 - 1.0f) + v[2];
        } else {
          float k = shade + lp(l, 0xa0);
          if (!no_shading) k = ::zmod::clouds_at_camera(pos + i * 3) * k;
          v[0] = (k * lp(l, 0xa4) - 1.0f) + v[0];
          v[1] = (k * lp(l, 0xa8) - 1.0f) + v[1];
          v[2] = (k * lp(l, 0xac) - 1.0f) + v[2];
        }
      }
      ++bright;
    }
    for (const ::zmod::LightEntry* e : g_state.point) {
      const ::zcls::Light* l = e->data;
      const uint32_t f = lflags(l);
      float sum = 0.0f;
      if ((f & 0x20) && (r.flags & 8)) continue;
      if ((f & 0x200) && has_baked) continue;
      if ((r.flags & 0x400) && (f & 0x80)) continue;
      if ((r.flags & 0x200) && (f & 0x40)) continue;
      if (e->full != 0 || e->reach != 0) {
        const float base = (f & 0x400) ? 1.0f : lp(l, 0xa0) + lp(l, 0x9c);
        for (int32_t i = n - 1; i >= 0; --i) {
          float k = base;
          if (e->full == 0) {
            const float* p = pos + i * 3;
            const float x = lp(l, 0x78) - p[0], y = lp(l, 0x7c) - p[1], z = lp(l, 0x80) - p[2];
            const float d2 = x * x + y * y + z * z;
            if (!(d2 < lp(l, 0xf0))) continue;
            if (lp(l, 0xec) <= d2) k = range(l, std::sqrt(d2)) * base;
          }
          float* v = rec + i * 8;
          if (f & 0x20) {
            v[4] = k + v[4];
            sum = k + sum;
          } else {
            const int at = (f & 0x400) ? 0xd4 : 0xa4;
            const float a = k * lp(l, at), b = k * lp(l, at + 4), c = k * lp(l, at + 8);
            v[0] = a + v[0], v[1] = b + v[1], v[2] = c + v[2];
            sum = std::fabs(a) + std::fabs(b) + std::fabs(c) + sum;
          }
        }
      }
      if (double{sum} > kVisible) {
        ++lit;
        if (f & 0x20) fogged |= 1, fog_light = l;
      }
    }
    for (const ::zmod::LightEntry* e : g_state.spot) {
      const ::zcls::Light* l = e->data;
      const uint32_t f = lflags(l);
      if ((f & 0x20) && (r.flags & 8)) continue;
      if ((f & 0x200) && has_baked) continue;
      if ((r.flags & 0x400) && (f & 0x80)) continue;
      if ((r.flags & 0x200) && (f & 0x40)) continue;
      float facing = 1.0f;
      if (!normals) {
        facing = dot(normal, l);
        if (facing <= 0.0f && lp(l, 0xa0) == 0.0f) continue;
      }
      float sum = 0.0f;
      for (int32_t i = n - 1; i >= 0; --i) {
        const float* p = pos + i * 3;
        float to[3] = {lp(l, 0x78) - p[0], lp(l, 0x7c) - p[1], lp(l, 0x80) - p[2]};
        float dist = 0.0f;
        if (e->full == 0) {
          const float d2 = to[0] * to[0] + to[1] * to[1] + to[2] * to[2];
          if (!(d2 < lp(l, 0xf0))) continue;
          dist = std::sqrt(d2);
          const float inv = 1.0f / dist;  // 0x00537b60
          to[0] = inv * to[0], to[1] = inv * to[1], to[2] = inv * to[2];
        }
        if (normals) facing = dot(normals + i * 3, l);
        float cone = 0.0f;
        if (facing > 0.0f) {
          if (e->full != 0) ::zmod::normalize(to);
          const float d = dot(to, l);
          if (d < 0.0f) cone = 0.0f;
          else cone = d;
        }
        if (cone <= 0.0f && lp(l, 0xa0) == 0.0f) continue;
        float* v = rec + i * 8;
        if (!(f & 0x400) || (f & 0x20)) {
          float k = cone > 0.0f ? cone * lp(l, 0x9c) + lp(l, 0xa0) : lp(l, 0xa0);
          if (e->full == 0) k = range(l, dist) * k;
          if (f & 0x20) {
            v[4] = k + v[4];
            sum = k + sum;
            continue;
          }
          const float a = k * lp(l, 0xa4), b = k * lp(l, 0xa8), c = k * lp(l, 0xac);
          v[0] = a + v[0], v[1] = b + v[1], v[2] = c + v[2];
          sum = std::fabs(a) + std::fabs(b) + std::fabs(c) + sum;
        } else {
          const float g = cone > 0.0f ? cone : 0.0f;
          const float rr = e->full == 0 ? range(l, dist) : 1.0f;
          const float d = e->full == 0 ? g * rr : g;
          const float a = rr * lp(l, 0xc8) + d * lp(l, 0xbc);
          const float b = rr * lp(l, 0xcc) + d * lp(l, 0xc0);
          const float c = rr * lp(l, 0xd0) + d * lp(l, 0xc4);
          v[0] = a + v[0], v[1] = b + v[1], v[2] = c + v[2];
          sum = std::fabs(a) + std::fabs(b) + std::fabs(c) + sum;
        }
      }
      if (double{sum} > kVisible) {
        ++lit;
        if (f & 0x20) fogged |= 1, fog_light = l;
      }
    }
  }
  if (needs & 4) {
    ++lit;
    const float w = ::zmod::lighting().tint_weight;
    for (int32_t i = n - 1; i >= 0; --i) rec[i * 8 + 4] = w + rec[i * 8 + 4];
    fogged |= 1;
  }
  if (lit == 0 && bright == 0) return 0;
  int ret = 0;
  const auto apart = [](float a, float b) { return double{std::fabs(a - b)} > kVisible; };
  bool white = !apart(rec[(n - 1) * 8], 1.0f);
  for (int32_t i = n - 1; white && i >= 0; --i)
    white = !apart(rec[i * 8], 1.0f) && !apart(rec[i * 8 + 1], 1.0f) && !apart(rec[i * 8 + 2], 1.0f);
  if (!white) {
    r.flags |= 0x10;
    ret = 1;
    bool alike = true;
    for (int32_t i = n - 1; alike && i >= 1; --i)
      alike = !apart(rec[i * 8], rec[0]) && !apart(rec[i * 8 + 1], rec[1]) && !apart(rec[i * 8 + 2], rec[2]);
    if (!alike) r.flags |= 0x40;
  }
  if (fogged) {
    fogged = 0;
    for (int32_t i = n - 1; i >= 0; --i) {
      float& v = rec[i * 8 + 4];
      if (v > 1.0f) v = 1.0f;
      else if (v < 0.0f) v = 0.0f;
      if (double{v} > kVisible) fogged = 1;
    }
    if (!(r.flags & 0x60)) {
      for (int32_t i = n - 1; i >= 1; --i) {
        if (apart(rec[i * 8 + 4], rec[4])) {
          r.flags |= 0x20;
          break;
        }
      }
    }
    if (fogged) {
      const bool was = (r.flags & 8) != 0;
      r.flags |= 0x18;
      ret = 1;
      const float* rgb = fog_light ? nullptr : ::zmod::lighting().tint;
      float own[3];
      if (fog_light) {
        for (int k = 0; k < 3; ++k) own[k] = lp(fog_light, 0xa4 + k * 4);
        rgb = own;
      }
      if (was) {
        if (g_state.mix_fog) g_state.mix_fog(rgb);
        return 1;
      }
      zrndr::DeviceFog& d = zrndr::device_fog();
      for (int k = 0; k < 3; ++k) d.color[k] = rgb[k] * 255.0f;
      if (d.sent[0] != d.color[0] || d.sent[1] != d.color[1] || d.sent[2] != d.color[2]) {
        for (int k = 0; k < 3; ++k) d.sent[k] = d.color[k];
        if (d.send) d.send();
      }
      return 1;
    }
  }
  return ret;
}

// 0x00544797..0x00544ad6
void light_records(const Model& m, const ModelPass& pass, RefPass& ref) {
  if (pass.needs == 0) return;
  sort_lights(ref);
  const ::zmod::Material& mat = *ref.records.front().polygon->materials[0];
  // The exe tests +0x10, which on a coloured material holds the file's index.
  const bool textured = mat.texture != nullptr || mat.texture_index != 0;
  const uint32_t shading = pass.needs & 6;
  const bool shade_off = (hdr<uint8_t>(m, 8) & 0x20) != 0;
  for (Record& r : ref.records) {
    if (ref.light_maps) {
      const ::zmod::LightList& list = ::zmod::light_list();  // 0x005625e0
      for (int32_t i = 0; i < list.count; ++i) {
        const uint32_t f = lflags(list.list[i].data);
        if (list.list[i].reach && (f & 0x10) && (f & 0x80)) {
          r.flags |= 0x400;
          break;
        }
      }
    }
    if (ref.projected && projected_reach(r)) r.flags |= 0x200;
    if (shading) {
      if (shade_off) r.flags |= 0x80;
      const float* baked = pass.bake && !(r.polygon->flags & 0x400) ? reinterpret_cast<const float*>(r.polygon->scratch) : nullptr;
      if (shade_polygon(r, baked, pass.needs) == 0) {
        r.flags &= ~0x80u;
      } else {
        std::memcpy(r.lit, r.rec, 12);
        for (int32_t i = r.count - 1; i >= 0; --i) {
          float* v = r.rec + i * 8;
          if (!textured) {
            v[0] = clamp255(v[0] * mat.r), v[1] = clamp255(v[1] * mat.g), v[2] = clamp255(v[2] * mat.b);
          } else {
            v[0] = clamp255(v[5] * v[0]), v[1] = clamp255(v[6] * v[1]), v[2] = clamp255(v[7] * v[2]);
          }
        }
        r.flags &= ~0x80u;
      }
    }
    if ((r.flags & 0x18) && !(r.flags & 0x10)) {
      r.lit[0] = r.lit[1] = r.lit[2] = 1.0f;
      if (!textured) {  // the rest of the vertices only with a polygon-wide flag
        const int32_t last = (r.flags & 0xe0) ? r.count - 1 : 0;
        for (int32_t i = last; i >= 0; --i) {
          float* v = r.rec + i * 8;
          v[0] = mat.r, v[1] = mat.g, v[2] = mat.b;
        }
      } else {
        for (int32_t i = r.count - 1; i >= 0; --i) {
          float* v = r.rec + i * 8;
          v[0] = v[5], v[1] = v[6], v[2] = v[7];
        }
      }
    }
  }
}

namespace {
// Pirate's Moon 1.0 @ 0x00547dd0, the Direct3D path (the software one is not ported).
void model_light(const float* vertex, uint32_t color, ::zmod::Light& l) {
  const auto field = [&](int off) {
    float v;
    std::memcpy(&v, l.raw + off, 4);
    return v;
  };
  zmth::MatrixStack& st = zmth::matrix_stack();
  float c[3] = {vertex[0], vertex[1], vertex[2]};
  if (!st.is_identity()) transform_point(vertex, st.matrix().m, c);
  if (!(c[2] > clip3d_state().near_z)) return;
  float sc[3];
  project_point(c, sc);
  const ::zmod::DrawState& s = ::zmod::draw_state();
  const auto below = [](float a, float b) { return !(a >= b); };
  if (below(sc[0], s.point_rect[0]) || below(sc[1], s.point_rect[1])) return;
  if (sc[0] > s.point_rect[2] || sc[1] > s.point_rect[3]) return;
  int16_t priority;
  std::memcpy(&priority, l.raw + 0x18, 2);
  float z = sc[2];
  if (priority != 0) z = (static_cast<float>(priority) * s.priority_scale - -1.0f) * z;
  const float screen[3] = {sc[0], sc[1], z};
  int32_t alpha = 0xff;
  const float near_ = field(0x30), far_ = field(0x34);
  if (far_ != 0.0f) {
    if (!(c[2] < far_)) alpha = 0;
    else if (c[2] > near_) {
      const int32_t a = crt::msvc_ftol((far_ - c[2]) * field(0x38));
      if (a < 0xff) alpha = a;
    }
  }
  ::zmod::DrawTable& t = ::zmod::draw_table();
  if (t.point) t.point(t.ctx, screen, color & 0xffff, alpha);
  int32_t flare;
  std::memcpy(&flare, l.raw + 0x3c, 4);
  if (flare != 0 && g_state.flare) {
    float world[3];
    transform_point(c, zmth::view().camera.m, world);
    g_state.flare(world, screen, color & 0xffff, l.raw + 0x30);
  }
}
}  // namespace

void draw_model_lights(Model& m) {
  const int32_t n = m.count(0x20);
  const float dt = ztimer::state().dt;
  const auto gi = [](const ::zmod::Light& l, int off) {
    int32_t v;
    std::memcpy(&v, l.raw + off, 4);
    return v;
  };
  const auto gf = [](const ::zmod::Light& l, int off) {
    float v;
    std::memcpy(&v, l.raw + off, 4);
    return v;
  };
  const auto pf = [](::zmod::Light& l, int off, float v) { std::memcpy(l.raw + off, &v, 4); };
  const auto pu = [](::zmod::Light& l, int off, uint32_t v) { std::memcpy(l.raw + off, &v, 4); };
  for (int32_t i = 0; i < n; ++i) {
    ::zmod::Light& l = m.lights[i];
    const int32_t type = gi(l, 0);
    if (type == 0) {
      if (gi(l, 4) == 1) {
        const float t = dt + gf(l, 0x10);
        pf(l, 0x10, t);
        if (gf(l, 8) < t) {
          const uint32_t pair = static_cast<uint32_t>(gi(l, 0x14));
          pf(l, 0x10, 0.0f);
          pu(l, 0x14, (pair & 0xffff) | (static_cast<uint32_t>(l.color) << 16));
          l.color = static_cast<uint16_t>(pair >> 16);
        }
      }
      model_light(l.vertices, l.color, l);
    } else if (type == 1) {
      const float t = dt + gf(l, 0x10);
      pf(l, 0x10, t);
      if (gf(l, 8) < t) {
        uint32_t k = static_cast<uint32_t>(gi(l, 0x14)) + 1;  // the whole dword: 0xffff carries into the colour
        if (static_cast<int32_t>(k & 0xffff) >= gi(l, 0xc)) k &= 0xffff0000u;
        pu(l, 0x14, k);
        pf(l, 0x10, 0.0f);
      }
      const uint32_t k = static_cast<uint32_t>(gi(l, 0x14));
      const uint32_t at = k & 0xffff;
      model_light(l.vertices + 3 * at, l.color, l);
      if (at != 0) model_light(l.vertices + 3 * (at - 1), k >> 16, l);
    }
  }
}

// Pirate's Moon 1.0 @ 0x0055fb30. Below near^2 the range is tested with the squared
// distance, not the distance, as the exe: `dist` stays d2 there (MW3 has the same).
void bake_static_light(Model& m) {
  float* to = ::zmod::bake_state().stale;
  const bool no_sun = (hdr<uint32_t>(m, 8) >> 5) & 1;
  const ::zmod::LightList& list = ::zmod::light_list();
  const auto dotted = [](const float* v, const ::zcls::Light* l) {
    const float z = v[2] * lp(l, 0x98), y = v[1] * lp(l, 0x94), x = v[0] * lp(l, 0x90);
    return x + y + z;
  };
  const auto term = [](float k, const ::zcls::Light* l) {
    return k > 0.0f ? k * lp(l, 0x9c) + lp(l, 0xa0) : lp(l, 0xa0);
  };
  static std::vector<float> pos, normals;
  if (g_state.normals.size() < static_cast<size_t>(m.count(0x18)) * 3) g_state.normals.resize(static_cast<size_t>(m.count(0x18)) * 3);
  for (int32_t pi = 0; pi < m.count(0x10); ++pi) {
    Polygon& p = m.polygons[pi];
    if (p.flags & 0x400) continue;
    const int32_t n = static_cast<int32_t>(p.flags & 0x3ff);
    // At least 9 floats: poly_normal reads three vertices, and past a smaller polygon's
    // the exe reads old stack.
    pos.assign(static_cast<size_t>(n > 3 ? n : 3) * 3, 0.0f);
    for (int32_t i = 0; i < n; ++i) std::memcpy(&pos[static_cast<size_t>(i) * 3], &g_state.cam[static_cast<size_t>(p.vertex_indices[i]) * 3], 12);
    const bool vn = !no_sun && (p.flags & 0x1000);
    if (vn) {
      normals.resize(static_cast<size_t>(n) * 3);
      for (int32_t i = 0; i < n; ++i) std::memcpy(&normals[static_cast<size_t>(i) * 3], &g_state.normals[static_cast<size_t>(p.normal_indices[i]) * 3], 12);
    }
    float* out = reinterpret_cast<float*>(p.scratch);
    for (int32_t i = n - 1; i >= 0; --i) out[i] = 1.0f;
    bool have_normal = false, directed = false;
    float normal[3] = {};
    float lit = 0.0f;  // [ebp-8]: left from the last light where it is not set
    float dist = 0.0f;
    for (int32_t li = 0; li < list.count; ++li) {
      const ::zmod::LightEntry& e = list.list[li];
      const ::zcls::Light* l = e.data;
      const uint32_t f = lflags(l);
      if (!(f & 0x200)) continue;
      if (f & 8) {
        if (no_sun) {
          lit = lp(l, 0xa0);
        } else if (!vn) {
          if (!have_normal) zmth::poly_normal(&pos[3], &pos[6], &pos[0], normal), have_normal = true;
          lit = term(dotted(normal, l), l);
        }
        directed = true;
        for (int32_t i = n - 1; i >= 0; --i) {
          if (vn) lit = term(dotted(&normals[static_cast<size_t>(i) * 3], l), l);
          out[i] = lit + out[i];
        }
        continue;
      }
      if (!e.reach) continue;
      const bool spot = (f & 4) != 0;
      float facing = 1.0f, factor = 1.0f;
      if (spot) {
        if (!have_normal) zmth::poly_normal(&pos[3], &pos[6], &pos[0], normal), have_normal = true;
        facing = dotted(normal, l);
        if (!(double{facing} > kVisible)) {
          facing = 0.0f, factor = 0.0f;
        } else {
          const float s = dotted(to, l);  // the previous light's or polygon's vector
          factor = double{s} < kVisible ? 0.0f : s;
        }
      }
      if (!(facing > 0.0f) && !(lp(l, 0xa0) > 0.0f)) continue;
      lit = term(factor, l);
      const auto measure = [&](const float* v) {
        to[0] = lp(l, 0x78) - v[0], to[1] = lp(l, 0x7c) - v[1], to[2] = lp(l, 0x80) - v[2];
        const float d2 = to[0] * to[0] + to[1] * to[1] + to[2] * to[2];
        dist = d2;
        if (!(d2 < lp(l, 0xf0))) return false;
        if (spot) {
          dist = std::sqrt(d2);
          const float inv = 1.0f / dist;
          to[0] = inv * to[0], to[1] = inv * to[1], to[2] = inv * to[2];
        } else if (lp(l, 0xec) <= d2) {
          dist = std::sqrt(d2);
        }
        return true;
      };
      if (e.full || measure(&pos[0])) {
        const float k = e.full ? lit : range(l, dist) * lit;
        out[0] = k + out[0];
      }
      for (int32_t i = n - 1; i >= 1; --i) {
        const float* v = &pos[static_cast<size_t>(i) * 3];
        if (!e.full && !measure(v)) continue;
        float g = facing > 0.0f ? facing : 0.0f;
        if (facing > 0.0f && spot) {
          if (e.full) to[0] = lp(l, 0x78) - v[0], to[1] = lp(l, 0x7c) - v[1], to[2] = lp(l, 0x80) - v[2];
          const float s = dotted(to, l);
          g = double{s} < kVisible ? 0.0f : s;
        }
        if (!(double{g} > kVisible) && !(lp(l, 0xa0) > 0.0f)) continue;
        if (spot) lit = term(g, l);
        const float k = e.full ? lit : range(l, dist) * lit;
        out[i] = k + out[i];
      }
    }
    if (directed) {
      for (int32_t i = n - 1; i >= 0; --i) out[i] = out[i] - 1.0f;
    }
  }
}

namespace {
// One triangle of `src`, vertices a, b, c, appended to the arenas; its positions and
// face normal are already at the arenas' next free.
Record& add_triangle(RefPass& ref, const Record& src, int a, int b, int c, uint32_t codes, float* normal) {
  const int v[3] = {a, b, c};
  Record r;
  r.polygon = src.polygon, r.flags = src.flags, r.count = 3, r.codes = codes;
  std::memcpy(r.lit, src.lit, 12);
  r.pos = &ref.pos[ref.at * 3], r.rec = &ref.rec[ref.at * 8], r.tl = &ref.tl[ref.at * 8];
  r.extra = &ref.extra[ref.at];
  r.normal = normal;
  for (int k = 0; k < 3; ++k) {
    std::memcpy(r.rec + k * 8, src.rec + v[k] * 8, 32);
    std::memcpy(r.tl + k * 8 + 6, src.tl + v[k] * 8 + 6, 8);
  }
  if (src.normals) {
    r.normals = &ref.normals[ref.normal_at * 3];
    for (int k = 0; k < 3; ++k) std::memcpy(r.normals + k * 3, src.normals + v[k] * 3, 12);
    ref.normal_at += 3;
  }
  if (src.shadows) {
    ref.shadows.emplace_back();
    ClipShadows& s = ref.shadows.back();
    s.count = src.shadows->count;
    for (int32_t k = 0; k < s.count; ++k) {
      s.texture[k] = src.shadows->texture[k];
      s.kind[k] = src.shadows->kind[k];
      s.uv[k].resize(6);
      for (int i = 0; i < 3; ++i) std::memcpy(&s.uv[k][static_cast<size_t>(i) * 2], &src.shadows->uv[k][static_cast<size_t>(v[i]) * 2], 8);
    }
    r.shadows = &s;
  }
  ref.at += 3 + 8;
  ref.records.push_back(r);
  return ref.records.back();
}
}  // namespace

// 0x00544b00..0x005451cd: taken apart when clipped at the camera, seen through, shadowed
// or lit by a map or projected light. A strip's flip is in its index pairs.
void split_strips(const ::zmod::Material& mat, const ModelPass& pass, RefPass& ref) {
  if (ref.strips == 0) return;
  const bool near_ = (pass.mask & 0x10) != 0;
  const bool shadowed = pass.shadows && ::zmod::shadow_casters().count > 0 && mat.texture && !has_alpha(mat);
  const bool seen_through = shadowed || ::zmod::draw_state().opacity < 1.0f || (mat.texture && has_alpha(mat));
  if (!near_ && !seen_through && !ref.projected && !ref.light_maps) return;
  const float tolerance = ::zmod::draw_state().backface;
  const size_t end = ref.records.size();
  for (size_t ri = 0; ri < end; ++ri) {
    if (ref.records[ri].count == 0 || !(ref.records[ri].polygon->flags & 0x2000)) continue;
    uint32_t f = ref.records[ri].flags;
    if (!seen_through) {
      if (!near_) {
        if (!((ref.light_maps && (f & 0x400)) || (ref.projected && (f & 0x200)))) continue;
      } else if (!ref.projected && !ref.light_maps && ref.records[ri].codes == 0) {
        continue;
      }
    }
    ref.records[ri].flags |= 0x100;
    const int32_t n = ref.records[ri].count;
    const uint32_t* idx = ref.records[ri].polygon->vertex_indices;
    for (int32_t k = 2; k < n; ++k) {
      const int a = k - 2, b = (k - 1) | 1, c = k & ~1;
      if (idx[a] == idx[b] || idx[a] == idx[c] || idx[b] == idx[c]) continue;
      const Record& src = ref.records[ri];
      float* pos = &ref.pos[ref.at * 3];
      std::memcpy(pos, src.pos + a * 3, 12), std::memcpy(pos + 3, src.pos + b * 3, 12), std::memcpy(pos + 6, src.pos + c * 3, 12);
      float* normal = &ref.faces[ref.face_at];
      if (!(face(pos, normal) < -tolerance)) continue;
      uint32_t codes = src.codes;
      if (src.codes != 0) {
        const int32_t tri[3] = {static_cast<int32_t>(idx[a]), static_cast<int32_t>(idx[b]), static_cast<int32_t>(idx[c])};
        if (polygon_codes(3, tri, &codes) == 0) continue;
      }
      ref.face_at += 3;
      add_triangle(ref, src, a, b, c, codes, normal);
    }
    ref.records[ri].count = 0;
  }
}

// 0x005451ce..0x005456e8
void split_fans(const ModelPass& pass, RefPass& ref) {
  if (!(pass.mask & 0x1f)) return;
  const size_t end = ref.records.size();
  for (size_t ri = 0; ri < end; ++ri) {
    if (ref.records[ri].count <= 3 || ref.records[ri].codes == 0 || (ref.records[ri].polygon->flags & 0x2000)) continue;
    const int32_t n = ref.records[ri].count;
    const uint32_t* idx = ref.records[ri].polygon->vertex_indices;
    for (int32_t k = 2; k < n; ++k) {
      const Record& src = ref.records[ri];
      const int32_t tri[3] = {static_cast<int32_t>(idx[k]), static_cast<int32_t>(idx[0]), static_cast<int32_t>(idx[k - 1])};
      uint32_t codes = 0;
      if (polygon_codes(3, tri, &codes) == 0) continue;
      float* pos = &ref.pos[ref.at * 3];
      std::memcpy(pos, src.pos + k * 3, 12), std::memcpy(pos + 3, src.pos, 12), std::memcpy(pos + 6, src.pos + (k - 1) * 3, 12);
      float* normal = &ref.faces[ref.face_at];
      if (src.normal) std::memcpy(normal, src.normal, 12);
      ref.face_at += 3;
      add_triangle(ref, src, k, 0, k - 1, codes, normal);
    }
    ref.records[ri].count = 0;
  }
}

// 0x005456f8..0x00545ab0, as MW3's collect_shadows; the sets go on the polygon's own.
void shadow_sets(const ::zmod::Material& mat, const ModelPass& pass, RefPass& ref) {
  const ::zmod::ShadowCasters& casters = ::zmod::shadow_casters();
  if (!pass.shadows || casters.count == 0 || !mat.texture || has_alpha(mat)) return;
  const float* m = zmth::view().camera.m;
  const float* off = ::zmod::lighting().camera_offset;
  static std::vector<float> world, ground;
  for (Record& r : ref.records) {
    if (r.count == 0 || (r.shadows && r.shadows->count >= 7)) continue;
    const int32_t n = r.count;
    world.resize(static_cast<size_t>(n) * 3), ground.resize(static_cast<size_t>(n) * 2);
    float lowest = 3.4028235e+38f;
    for (int32_t i = n - 1; i >= 0; --i) {
      transform_point(r.pos + i * 3, m, &world[static_cast<size_t>(i) * 3]);
      if (lowest > world[static_cast<size_t>(i) * 3 + 1]) lowest = world[static_cast<size_t>(i) * 3 + 1];
    }
    for (int32_t c = casters.count - 1; c >= 0; --c) {
      const ::zmod::ShadowCaster& s = *casters.list[c];
      if (lowest > s.box_max[1]) continue;
      const float* nv = r.normal;
      const float f2 = nv[2] * s.slope_cam[2], f1 = nv[1] * s.slope_cam[1], f0 = nv[0] * s.slope_cam[0];
      const float facing = (f2 + f1) + f0;
      if (!(facing > 0.0f)) continue;
      const float lx = (off[0] + s.position[0]) * s.slope[0], lz = (off[2] + s.position[2]) * s.slope[1];
      const float limit = (s.slope_len + s.slope_len) + lz + lx;
      bool slide_all = false;
      for (int32_t i = n - 1; i >= 0; --i) {
        const float a = s.slope[0] * world[static_cast<size_t>(i) * 3], b = world[static_cast<size_t>(i) * 3 + 2] * s.slope[1];
        if (a + b <= limit) {
          slide_all = true;
          break;
        }
      }
      float max_x = -3.4028235e+38f, min_x = 3.4028235e+38f, min_z = 3.4028235e+38f, max_z = -3.4028235e+38f;
      for (int32_t i = n - 1; i >= 0; --i) {
        const float* w = &world[static_cast<size_t>(i) * 3];
        float* g = &ground[static_cast<size_t>(i) * 2];
        g[0] = w[0], g[1] = w[2];
        if (slide_all || w[1] < s.ground_y) {
          const float t = s.ground_y - w[1];
          g[0] = t * s.slope[0] + g[0];
          g[1] = t * s.slope[1] + g[1];
        }
        if (g[0] < min_x) min_x = g[0];
        if (max_x < g[0]) max_x = g[0];
        if (g[1] < min_z) min_z = g[1];
        if (max_z < g[1]) max_z = g[1];
      }
      const float x0 = off[0] + s.box_min[0], z0 = off[2] + s.box_min[2];
      if (!(x0 < max_x) || !(z0 < max_z)) continue;
      if (!(min_x < off[0] + s.box_max[0]) || !(min_z < off[2] + s.box_max[2])) continue;
      if (!r.shadows) {
        ref.shadows.emplace_back();
        r.shadows = &ref.shadows.back();
      }
      ClipShadows& set = *r.shadows;
      std::vector<float>& uv = set.uv[set.count];
      uv.resize(static_cast<size_t>(n) * 2);
      for (int32_t i = 0; i < n; ++i) {
        uv[static_cast<size_t>(i) * 2] = (ground[static_cast<size_t>(i) * 2] - x0) * s.scale[0];
        uv[static_cast<size_t>(i) * 2 + 1] = (ground[static_cast<size_t>(i) * 2 + 1] - z0) * s.scale[1];
      }
      set.texture[set.count] = s.texture;
      set.kind[set.count] = 1;
      if (++set.count > 6) break;
    }
  }
}

// 0x00545ab1..0x00546009: each polygon flagged 0x400, each map light (flags 0x10, 0x80)
// whose square across its range covers the polygon: UVs in that square, the light's
// colour by the vertex colour, the fade with distance.
void light_map_sets(RefPass& ref) {
  if (!ref.light_maps) return;
  const ::zmod::LightList& list = ::zmod::light_list();
  const ::zmod::Material* first = nullptr;
  static std::vector<float> to;
  for (Record& r : ref.records) {
    if (r.count == 0 || !(r.flags & 0x400) || (r.shadows && r.shadows->count >= 7)) continue;
    if (!r.normal) continue;  // the exe reads through null; a strip is split before it comes here
    first = r.polygon->materials[0];
    ::zmod::normalize(r.normal);
    const int32_t n = r.count;
    to.resize(static_cast<size_t>(n) * 3);
    for (int32_t li = 0; li < list.count; ++li) {
      const ::zmod::LightEntry& e = list.list[li];
      if (!e.reach) continue;
      const ::zcls::Light* l = e.data;
      const uint32_t f = lflags(l);
      if (!(f & 0x10) || !(f & 0x80)) continue;
      const float lx = lp(l, 0x78), ly = lp(l, 0x7c), lz = lp(l, 0x80);
      const float* n3 = r.normal;
      const float d0 = lz - r.pos[2], d1 = ly - r.pos[1], d2 = lx - r.pos[0];
      to[0] = d2, to[1] = d1, to[2] = d0;
      const float along = n3[0] * d2 + n3[1] * d1 + n3[2] * d0;
      const float dist = along < 0.0f ? -along : along;
      if (!(dist < lp(l, 0xe8))) continue;
      const float k = 0.5f / std::sqrt(lp(l, 0xf0) - dist * dist);
      for (int32_t i = 1; i < n; ++i) {
        to[static_cast<size_t>(i) * 3] = lx - r.pos[i * 3];
        to[static_cast<size_t>(i) * 3 + 1] = ly - r.pos[i * 3 + 1];
        to[static_cast<size_t>(i) * 3 + 2] = lz - r.pos[i * 3 + 2];
      }
      const zmth::Vec3 nv{n3[0], n3[1], n3[2]};
      zmth::Vec3 uax;
      zmth::perpendicular(nv, &uax);
      uax.x = uax.x * k, uax.y = uax.y * k, uax.z = uax.z * k;
      const float vax[3] = {n3[1] * uax.z - n3[2] * uax.y, n3[2] * uax.x - n3[0] * uax.z, n3[0] * uax.y - n3[1] * uax.x};
      uint32_t seen = 0;
      for (int32_t i = 0; i < n; ++i) {
        float* t = &to[static_cast<size_t>(i) * 3];
        const float u = t[0] * uax.x + t[1] * uax.y + t[2] * uax.z;
        const float v = t[0] * vax[0] + t[1] * vax[1] + t[2] * vax[2];
        t[0] = u, t[1] = v;
        if (double{u} >= -0.5) seen |= 1;
        if (double{u} <= 0.5) seen |= 2;
        if (double{v} >= -0.5) seen |= 4;
        if (double{v} <= 0.5) seen |= 8;
      }
      if (seen != 0xf) continue;
      if (!r.shadows) {
        ref.shadows.emplace_back();
        r.shadows = &ref.shadows.back();
      }
      ClipShadows& set = *r.shadows;
      const int32_t at = set.count;
      set.uv[at].resize(static_cast<size_t>(n) * 2), set.xyz[at].resize(static_cast<size_t>(n) * 3);
      for (int32_t i = 0; i < n; ++i) {
        set.uv[at][static_cast<size_t>(i) * 2] = to[static_cast<size_t>(i) * 3] + 0.5f;
        set.uv[at][static_cast<size_t>(i) * 2 + 1] = to[static_cast<size_t>(i) * 3 + 1] + 0.5f;
        float* c = &set.xyz[at][static_cast<size_t>(i) * 3];
        c[0] = lp(l, 0xd4), c[1] = lp(l, 0xd8), c[2] = lp(l, 0xdc);
        if (first->flags & 1) {
          c[0] = r.rec[i * 8 + 5] * c[0], c[1] = r.rec[i * 8 + 6] * c[1], c[2] = r.rec[i * 8 + 7] * c[2];
        }
        for (int q = 0; q < 3; ++q) c[q] = clamp255(c[q]);
      }
      ::zimg::TextureEntry* map = pm::zimg::light_map();
      set.texture[at] = map ? map->surface : nullptr;
      set.kind[at] = 2;
      uint32_t fade = 0xff000000u;
      if (lp(l, 0xe4) < dist) {
        int32_t w = static_cast<int32_t>(rounded_product((lp(l, 0xe8) - dist) * lp(l, 0xf4), 255.0f));
        if (w >= 0x100) w = 0xff;
        else if (w < 0) w = 0;
        fade = static_cast<uint32_t>(w) << 24;
      }
      set.fade[at] = fade;
      if (++set.count > 6) break;
    }
  }
}

namespace {
// 0x00546030..0x005460a2: at the camera, by what the record carries.
void clip_near(const ::zmod::Material& mat, RefPass& ref) {
  const bool textured = (mat.flags & 1) != 0;
  for (Record& r : ref.records) {
    if (r.count == 0 || !(r.codes & 0x10)) continue;
    int ok;
    if (!textured) ok = (r.flags & 0x18) ? clip3d_lit(r) : clip3d_plain(r);
    else if ((r.flags & 0x18) && (r.flags & 0xe0)) ok = r.shadows ? clip3d_textured_lit_shadow(r) : clip3d_textured_lit(r);
    else ok = r.shadows ? clip3d_textured_shadow(r) : clip3d_textured(r);
    if (!ok) r.count = 0;
  }
}

void project_records(RefPass& ref) {  // 0x005460a2..0x005460f0
  const zmth::Projection& p = zmth::projection();
  for (Record& r : ref.records) {
    for (int32_t i = 0; i < r.count; ++i) {
      const float* c = r.pos + i * 3;
      float* t = r.tl + i * 8;
      const float w = p.near_z / c[2];
      t[2] = w;
      const float x = w * c[0];
      t[0] = x * p.fx_near + p.cx;
      const float y = c[1] * t[2];
      t[1] = y * p.fy_near + p.cy;
    }
  }
}

// 0x005460f8..0x005461ab: at the screen's sides.
void clip_sides(const ::zmod::Material& mat, RefPass& ref) {
  const bool textured = (mat.flags & 1) != 0;
  for (Record& r : ref.records) {
    if (r.count == 0 || !(r.codes & 0xf)) continue;
    if ((r.polygon->flags & 0x2000) && !(r.flags & 0x100)) continue;
    int ok;
    if (!textured) ok = (r.flags & 0x18) ? clip2d_lit(r) : clip2d_plain(r);
    else if (r.flags & 0xe0) ok = r.shadows ? clip2d_textured_lit_shadow(r) : clip2d_textured_lit(r);
    else ok = r.shadows ? clip2d_textured_lit_shadow(r) : clip2d_textured(r);
    if (!ok) r.count = 0;
  }
}

// 0x005461b1..0x005462b8: the plain and the one-colour polygons' colours.
void fill_colours(const ::zmod::Material& mat, RefPass& ref) {
  const bool textured = (mat.flags & 1) != 0;
  for (Record& r : ref.records) {
    const bool lit = (r.flags & 0x18) != 0;
    if (!lit) r.lit[0] = r.lit[1] = r.lit[2] = 1.0f;
    else if (r.flags & 0xe0) continue;
    for (int32_t i = 0; i < r.count; ++i) {
      float* v = r.rec + i * 8;
      for (int k = 0; k < 3; ++k) {
        const float c = textured ? v[5 + k] : (&mat.r)[k];
        v[k] = lit ? c * r.lit[k] : c;
      }
    }
  }
}

// Pirate's Moon 1.0 @ 0x005984e0: a colour to the device's word, each channel rounded.
uint32_t pack_material(const ::zmod::Material& mat) {
  const uint32_t r = static_cast<uint32_t>(crt::msvc_ftol(mat.r + 0.5f)) & 0xff, g = static_cast<uint32_t>(crt::msvc_ftol(mat.g + 0.5f)) & 0xff,
                 b = static_cast<uint32_t>(crt::msvc_ftol(mat.b + 0.5f)) & 0xff;
  return b | (r << 16) | (g << 8);
}

// 0x005462c0..0x00546690: z's priority, rhw, fog in the specular, the diffuse.
int32_t finish_tl(const ::zmod::Material& mat, RefPass& ref) {
  const bool textured = (mat.flags & 1) != 0;
  const float fade = ::zmod::draw_state().opacity;
  cull_state().fade = fade;
  const float scale = ::zmod::draw_state().priority_scale;
  int32_t shade = 1;
  for (Record& r : ref.records) {
    if (r.count == 0) continue;
    const int32_t priority = r.polygon->priority;
    if (priority != 0) {
      const float k = static_cast<float>(priority) * scale + 1.0f;
      for (int32_t i = r.count - 1; i >= 0; --i) r.tl[i * 8 + 2] = k * r.tl[i * 8 + 2];
    }
    for (int32_t i = 0; i < r.count; ++i) {
      float* t = r.tl + i * 8;
      t[3] = t[2];
      uint32_t spec = 0xff000000u;
      if (r.flags & 8) {
        const int32_t w = static_cast<int32_t>(rounded_sum(255.5f, r.rec[i * 8 + 4] * -255.0f));
        spec = static_cast<uint32_t>(w >= 0x100 ? 0xff : w < 0 ? 0 : w) << 24;
      }
      std::memcpy(t + 5, &spec, 4);
    }
    if (r.flags & 0x10) {
      shade = 2;
      tl_colours(r);
      continue;
    }
    if (!textured) {
      const uint32_t c = pack_material(mat);
      shade = 1;
      for (int32_t i = 0; i < r.count; ++i) std::memcpy(r.tl + i * 8 + 4, &c, 4);
      continue;
    }
    uint32_t alpha = 0xff000000u;
    if (fade < 1.0f) {
      const int32_t w = static_cast<int32_t>(rounded_product(fade, 255.0f));
      alpha = static_cast<uint32_t>(w >= 0x100 ? 0xff : w < 0 ? 0 : w) << 24;
    }
    shade = 2;
    for (int32_t i = 0; i < r.count; ++i) {
      const float* v = r.rec + i * 8;
      const auto channel = [](float x) {
        const int32_t w = static_cast<int32_t>(rounded_sum(x, 0.5f));
        return static_cast<uint32_t>(w >= 0x100 ? 0xff : w < 0 ? 0 : w);
      };
      const uint32_t c = (channel(v[0]) << 16) | (channel(v[1]) << 8) | channel(v[2]) | alpha;
      std::memcpy(r.tl + i * 8 + 4, &c, 4);
    }
  }
  return shade;
}

// 0x00546690..0x005469b9
void submit(const ::zmod::Material& mat, RefPass& ref, int32_t shade, void (*device)(const DeviceCall&)) {
  const bool textured = (mat.flags & 1) != 0;
  const float fade = ::zmod::draw_state().opacity;
  const int32_t overlay = ::zmod::draw_state().overlay;
  const bool apart = overlay != 0 || fade < 1.0f || (textured && has_alpha(mat));
  for (Record& r : ref.records) {
    if (r.count == 0) continue;
    if (!apart && !r.shadows && !((r.polygon->flags & 0x2000) && !(r.flags & 0x100))) continue;
    DeviceCall d;
    const bool strip = (r.polygon->flags & 0x2000) && !(r.flags & 0x100);
    d.cull = strip ? 2 : 1;
    d.primitive = strip ? 5 : 6;
    d.tl = r.tl, d.count = r.count, d.material = &mat, d.fade = fade, d.overlay = overlay;
    d.sets = r.shadows;
    if (!textured) d.kind = (r.flags & 0x18) ? DeviceCall::kLit : DeviceCall::kFlat;
    else if (r.shadows) d.kind = DeviceCall::kSets;
    else if (r.flags & 0xe0) d.kind = DeviceCall::kTexturedVertex;
    else d.kind = (r.flags & 0x18) ? DeviceCall::kTexturedLit : DeviceCall::kTextured;
    if (device) device(d);
    r.count = 0;
  }
  if (apart) return;
  static std::vector<float> tris;
  tris.clear();
  for (const Record& r : ref.records) {
    for (int32_t k = 2; k < r.count; ++k) {
      tris.insert(tris.end(), r.tl, r.tl + 8);
      tris.insert(tris.end(), r.tl + (k - 1) * 8, r.tl + k * 8);
      tris.insert(tris.end(), r.tl + k * 8, r.tl + (k + 1) * 8);
    }
  }
  if (tris.empty()) return;
  DeviceCall d;
  d.kind = textured ? DeviceCall::kIndexedTextured : DeviceCall::kIndexedFlat;
  d.tl = tris.data(), d.count = static_cast<int32_t>(tris.size() / 8), d.material = &mat;
  d.primitive = 4, d.shade = shade, d.fade = fade, d.overlay = overlay;
  if (device) device(d);
}
}  // namespace

namespace {
void (*g_projected_device)(const DeviceCall&) = nullptr;
float g_projected_tl[3 * 8];
ClipShadows g_projected_sets;

// 0x00562840's two calls, [0x008c4bf8] and [0x008c4bfc], as device calls.
void projected_triangle(const ProjectedTriangle& t, const ::zmod::Material& mat) {
  for (int j = 0; j < 3; ++j) {
    float* o = g_projected_tl + j * 8;
    o[0] = t.v[j].sx, o[1] = t.v[j].sy, o[2] = t.v[j].sz, o[3] = t.v[j].rhw;
    std::memcpy(o + 4, &t.v[j].diffuse, 4), std::memcpy(o + 5, &t.v[j].specular, 4);
    o[6] = t.v[j].tu, o[7] = t.v[j].tv;
  }
  DeviceCall d;
  d.kind = t.sets ? DeviceCall::kSets : DeviceCall::kProjected;
  d.tl = g_projected_tl, d.count = 3, d.sets = t.sets, d.material = &mat;
  d.fade = ::zmod::draw_state().opacity, d.overlay = ::zmod::draw_state().overlay;
  d.flags = t.flags;
  if (g_projected_device) g_projected_device(d);
}
}  // namespace

// Pirate's Moon 1.0 @ 0x00543b10
int32_t draw_model(::zcls::Node* node, uint32_t mask, void (*device)(const DeviceCall&)) {
  Model& m = *node->pm_model;
  zmth::Mat34 local;
  zmth::push(&local);
  float centre[3] = {};
  if (!prepare_model(m, centre)) return 0;  // it popped
  draw_model_lights(m);
  ModelPass pass;
  if (begin_model(*node, m, mask, pass)) {
    static RefPass ref;
    for (int32_t i = 0; i < m.count(0x5c); ++i) {
      const MaterialRef& mr = m.refs[i];
      if (!build_records(m, mr, pass, ref)) continue;
      ::zmod::Material& mat = *mr.polygon->materials[0];
      light_records(m, pass, ref);
      split_strips(mat, pass, ref);
      split_fans(pass, ref);
      shadow_sets(mat, pass, ref);
      light_map_sets(ref);
      // Where a polygon that grows goes, for the near clip and the sides alike.
      Clip3dState& arena = clip3d_state();
      arena.arena_pos = &ref.pos[ref.at * 3], arena.arena_rec = &ref.rec[ref.at * 8], arena.arena_tl = &ref.tl[ref.at * 8];
      arena.arena_extra = &ref.extra[ref.at];
      if (mask & 0x10) clip_near(mat, ref);
      project_records(ref);
      if (mask & 0xf) clip_sides(mat, ref);
      fill_colours(mat, ref);
      if (ref.projected) {
        for (Record& r : ref.records) {
          g_projected_device = device;
          if (r.count && (r.flags & 0x200) && draw_projected(r, projected_triangle)) r.count = 0;
        }
      }
      const int32_t shade = finish_tl(mat, ref);
      submit(mat, ref, shade, device);
    }
  }
  zmth::pop();
  return 0;
}

}  // namespace pm::zmod
