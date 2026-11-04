#include "pm/gamez/zmodel/gmod_draw.h"

#include <cmath>
#include <cstring>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/camera.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_draw.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zmodel/gmod_fogcalc.h"
#include "gamez/zmodel/gmod_light.h"
#include "gamez/zmodel/gmod_matl.h"
#include "gamez/zrender/zrndr_fog.h"
#include "pm/gamez/zimage/zimg_texture.h"
#include "pm/gamez/zmodel/gmod_clip3d.h"
#include "pm/gamez/zmodel/gmod_cull.h"

#pragma STDC FP_CONTRACT OFF

namespace pm::zmod {
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
    const zmth::Vec3& v = *reinterpret_cast<const zmth::Vec3*>(&cam[i * 3]);
    if (!(v.z > near_z)) continue;
    zmth::Vec3 sc;
    zmth::project_d3d(&v, &sc, 1);
    if (below(sc.x, s.point_rect[0]) || below(sc.y, s.point_rect[1])) continue;
    if (sc.x > s.point_rect[2] || sc.y > s.point_rect[3]) continue;
    const float screen[3] = {sc.x, sc.y, sc.z};
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
constexpr int32_t kMaxVerts = ::zmod::FogCalc::kMax;  // the shared factor array; the exe has no limit

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
float face(const float* p, float* n) {
  const float e2x = p[6] - p[3], e2y = p[7] - p[4], e2z = p[8] - p[5];
  const float e0x = p[0] - p[3], e0y = p[1] - p[4], e0z = p[2] - p[5];
  float a = e0z * e2y, b = e0y * e2z;
  n[0] = a - b;
  a = e0x * e2z, b = e0z * e2x;
  n[1] = a - b;
  a = e0y * e2x, b = e0x * e2y;
  n[2] = a - b;
  const float x = n[0] * p[0], y = n[1] * p[1], z = n[2] * p[2];
  const float s = z + y;
  return s + x;
}
}  // namespace

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
      d = std::sqrt(z * z + y * y + x * x);  // 0x00537a50
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
      if (g_state.bake) g_state.bake(m, node);
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
  size_t verts = 0, sets = 0;
  for (int32_t i = 0; i < ref.usage_count; ++i) {
    verts += (ref.polygon[i].flags & 0x3ff) + 8;
    if (ref.polygon[i].material_count > 1) ++sets;
  }
  out.pos.resize(verts * 3), out.normals.resize(verts * 3);
  out.rec.resize(verts * 8), out.tl.resize(verts * 8);
  out.faces.resize(static_cast<size_t>(ref.usage_count) * 3);
  out.extra.assign(verts, 0);
  out.shadows.reserve(sets);
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
  return out.records.empty() ? 0 : 1;
}

}  // namespace pm::zmod
