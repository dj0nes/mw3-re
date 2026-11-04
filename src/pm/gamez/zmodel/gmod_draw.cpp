#include "pm/gamez/zmodel/gmod_draw.h"

#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_draw.h"
#include "pm/gamez/zmodel/gmod_clip3d.h"

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

}  // namespace pm::zmod
