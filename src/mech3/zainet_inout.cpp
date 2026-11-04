#include "mech3/zainet_inout.h"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_const.h"
#include "crt/strcase.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3::ai {

namespace {
constexpr const char* kFile = "C:\\zipper\\Mech3\\ZAiNetMechInOut.cpp";
constexpr float kWidth = 3.0f;  // 0x005e8a18
// 0x005e8a5c (1) and 0x0060b394 / 0x005e8a1c (0 / 1) are constants in the
// exe: the corners go on the ground, red and yellow keep the model's
// winding, blue and cyan are reversed.
constexpr bool kGroundCorners = true;
constexpr bool kReverse[4] = {false, false, true, true};

InOutHooks g_hooks;
InOut g_inout;

// 0x004c1030 on (x, -, z): normalized in x and z unless of no length; the length.
float normalize_xz(float v[3]) {
  const float len = std::sqrt(v[0] * v[0] + v[2] * v[2]);  // 0x004c10e0
  if (len != 0.0f) {
    const float k = 1.0f / len;
    v[0] = v[0] * k;
    v[2] = v[2] * k;
  }
  return len;
}

// 0x004c1160 -> 0x004c5210 over 0x004c11f0: the corners walked in order with
// the two before each, from (n-2, n-1); each one's box, the corner before it
// given its pushed-out place and its side's length.
void make_sides(InOutPolygon& p) {
  const size_t n = p.verts.size();
  InOutVertex* prev = &p.verts[n - 2];
  InOutVertex* cur = &p.verts[n - 1];
  for (size_t i = 0; i < n; ++i) {
    InOutVertex* v = &p.verts[i];
    for (int k = 0; k < 3; ++k) {  // 0x004c51f0, 0x004c5200: the box keeps its value on a NaN
      if (!(p.min[k] < v->pos[k])) p.min[k] = v->pos[k];
      if (!(v->pos[k] < p.max[k])) p.max[k] = v->pos[k];
    }
    float a[3] = {prev->pos[2] - cur->pos[2], 0.0f, cur->pos[0] - prev->pos[0]};
    float b[3] = {cur->pos[2] - v->pos[2], 0.0f, v->pos[0] - cur->pos[0]};
    normalize_xz(a);
    cur->length = normalize_xz(b);
    p.perimeter = p.perimeter + cur->length;
    if (cur->length <= 0.0f) zerr::report(0x100, kFile, 0x1bc, "zero length segment in an inside/outside net");
    float c[3] = {a[0] + b[0], 0.0f, a[2] + b[2]};
    normalize_xz(c);
    c[0] = c[0] * p.width;
    c[2] = c[2] * p.width;
    cur->offset[0] = cur->pos[0] + c[0];
    cur->offset[2] = cur->pos[2] + c[2];
    cur->offset[1] = 0.0f;
    prev = cur;
    cur = v;
  }
}

// x87 compares: true when below or unordered (C0).
bool lt(float a, float b) { return !(a >= b); }

float dist_xz(const float* a, const float* b) {  // 0x004c1090
  const float dx = b[0] - a[0], dz = b[2] - a[2];
  return std::sqrt(dx * dx + dz * dz);
}
float dist3(const float* a, const float* b) {  // 0x0051e000
  const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
  return std::sqrt((dx * dx + dy * dy) + dz * dz);
}
float dot3(const float* a, const float* b) { return (a[0] * b[0] + a[1] * b[1]) + a[2] * b[2]; }  // 0x004c2430
// 0x004c0fe0 (ECX out, EDX a): a + (b - a) t in x and z, a's height.
void lerp(float out[3], const float* a, float t, const float* b) {
  out[0] = (b[0] - a[0]) * t + a[0];
  out[1] = a[1];
  out[2] = (b[2] - a[2]) * t + a[2];
}
// 0x004020d0 in play: floats.
float normalize3(float v[3]) {
  const float len = std::sqrt((v[0] * v[0] + v[2] * v[2]) + v[1] * v[1]);
  if (len != 0.0f) {
    const float k = 1.0f / len;
    v[0] = k * v[0], v[1] = k * v[1], v[2] = k * v[2];
  }
  return len;
}
// The side of `p` from the line a -> b (0x004c1410 / 0x004c1770's sums).
float side(const float* a, const float* b, const float* p) {
  return (((a[2] - b[2]) * p[0] - (a[0] - b[0]) * p[2]) + b[2] * a[0]) - b[0] * a[2];
}
bool opposite(float s1, float s2) { return (lt(s1, 0.0f) && lt(0.0f, s2)) || (s1 > 0.0f && 0.0f > s2); }
// From the side line p -> v: whether the ray from `a` through `b` meets it,
// and the ray from `b` back through `a`.
bool ray_from_a(float sa, float sb) { return (lt(sa, 0.0f) && lt(sa, sb)) || (sa > 0.0f && sa > sb); }
bool ray_from_b(float sa, float sb) { return (lt(sa, sb) && lt(0.0f, sb)) || (sa > sb && 0.0f > sb); }

// Mech3.exe 1.2 @ 0x004c1770 (ECX {a, b, prev}): segment a -> b against the side prev -> v.
bool crosses(const float* a, const float* b, const float* p, const float* v) {
  if (!opposite(side(a, b, p), side(a, b, v))) return false;
  const float sa = side(p, v, a), sb = side(p, v, b);
  return ray_from_a(sa, sb) && ray_from_b(sa, sb);
}

// Mech3.exe 1.2 @ 0x004c1410 (ECX {query, prev}): the query's segment
// against the side prev -> v; a crossing's place along it kept when nearest
// or farthest.
void edge_count(InOutQuery& c, const InOutVertex* prev, const InOutVertex* v) {
  const float* a = c.from;
  const float* b = c.to;
  if (!opposite(side(a, b, prev->pos), side(a, b, v->pos))) return;
  const float sa = side(prev->pos, v->pos, a), sb = side(prev->pos, v->pos, b);
  const bool ra = ray_from_a(sa, sb), rb = ray_from_b(sa, sb);
  if (ra) ++c.from_rays;
  if (rb) ++c.to_rays;
  if (!(ra && rb)) return;
  ++c.crossings;
  const float t = -sa / (sb - sa);
  if (lt(t, c.t_near)) c.t_near = t, c.near_a = prev, c.near_b = v;
  if (lt(c.t_far, t)) c.t_far = t, c.far_a = prev, c.far_b = v;
}

bool colour_is(const zmod::Material* m, float r, float g, float b) { return m->r == r && m->g == g && m->b == b; }

// Mech3.exe 1.2 @ 0x004c32c0: polygons of three corners or more, not
// morphing (flag 0x400), into the area.
void add_polygon(InOutArea& area, const char* name, const zmod::Polygon& poly, const float* verts) {
  if ((poly.flags >> 10) & 1) return;
  if ((poly.flags & 0xff) <= 2) return;
  area.polygons.push_back(std::make_unique<InOutPolygon>(name, poly, verts, kWidth));
}

// Mech3.exe 1.2 @ 0x004c3380 (ECX area, the node): its model's vertices
// through the top of the matrix stack (0x004c3520), on the ground, then its
// polygons. The name kept is the node's past its first seven characters when
// it starts "inout" and is longer than that.
void read_model(InOutArea& area, zcls::Node* node) {
  zmod::Model* m = node->model;
  const int32_t nv = static_cast<int32_t>(m->count(0x14));
  if (nv <= 0) return;
  const char* name = node->name();
  if (std::strncmp(name, "inout", 5) == 0 && std::strlen(name) > 7) name += 7;
  std::vector<float> buf(static_cast<size_t>(nv) * 3);
  zmth::MatrixStack& st = zmth::matrix_stack();
  if (st.is_identity()) {
    std::memcpy(buf.data(), m->vertices, buf.size() * 4);
  } else {
    const float* t = st.matrix().m;
    for (int32_t i = 0; i < nv; ++i) {
      const float* s = m->vertices + 3 * i;
      float* d = buf.data() + 3 * i;
      d[0] = ((s[0] * t[0] + s[1] * t[3]) + s[2] * t[6]) + t[9];
      d[1] = ((s[0] * t[1] + s[1] * t[4]) + s[2] * t[7]) + t[10];
      d[2] = ((s[0] * t[2] + s[1] * t[5]) + s[2] * t[8]) + t[11];
    }
  }
  if (kGroundCorners && g_hooks.ground)
    for (int32_t i = 0; i < nv; ++i) g_hooks.ground(buf.data() + 3 * i);  // 0x004c5440 over 0x004c4240
  const int32_t np = static_cast<int32_t>(m->count(0x10));
  for (int32_t i = 0; i < np; ++i) add_polygon(area, name, m->polygons[i], buf.data());  // 0x004c5470
}

void walk(zcls::Node* n, InOutArea& area);

void walk_children(zcls::Node* n, InOutArea& area) {  // 0x004c3f70
  for (int32_t i = 0; i < n->child_count(); ++i) walk(n->children[i], area);
}

// Mech3.exe 1.2 @ 0x004c3ed0: object3ds (5) through their matrices, LODs (6)
// in range, the other grouping classes their children only.
void walk(zcls::Node* n, InOutArea& area) {
  switch (n->cls()) {
    case 1: case 7: case 8: case 9: case 10: case 11:
      walk_children(n, area);
      break;
    case 5: {  // 0x004c3fb0
      if (!(n->flags() & 4)) return;
      n->set_flags(n->flags() & 0xfdffffff);
      auto* d = static_cast<uint8_t*>(n->data);
      uint32_t df;
      std::memcpy(&df, d, 4);
      auto* local = reinterpret_cast<zmth::Mat34*>(d + 0x30);
      auto* world = reinterpret_cast<zmth::Mat34*>(d + 0x60);
      const bool pushed = (df & 8) == 0;
      if (pushed) {
        if (!(n->flags() & 0x80000)) {
          zmth::push_copy(world);
          zmth::multiply(*local, 3);
        } else if (df & 0x20) {
          zmth::push_copy(world);
          zmth::multiply(*local, 3);
          df &= ~0x20u;
          std::memcpy(d, &df, 4);
        } else {
          zmth::push(world);
        }
      }
      if (n->model) read_model(area, n);  // 0x004c3600 through [0x0060b39c]
      walk_children(n, area);
      if (pushed) zmth::pop();
      break;
    }
    case 6: {  // 0x004c40e0
      float range;
      std::memcpy(&range, static_cast<uint8_t*>(n->data) + 4, 4);
      if (!(n->flags() & 4) || !(range <= 0.0f)) return;
      if (n->model) read_model(area, n);
      walk_children(n, area);
      break;
    }
    default:
      zerr::reportf(0x100, kFile, 0x845, "Unknown class type %d for node %s", n->cls(), n->name());
  }
}
}  // namespace

InOutHooks& inout_hooks() { return g_hooks; }
InOut& inout() { return g_inout; }

// Mech3.exe 1.2 @ 0x004c1ab0
InOutPolygon::InOutPolygon(const char* name_, const zmod::Polygon& poly, const float* v, float width_)
    : width(width_) {
  min[0] = min[1] = min[2] = FLT_MAX;     // 0x0060b3b8
  max[0] = max[1] = max[2] = -FLT_MAX;    // 0x0060b3a8
  if (name_) name = strdup(name_);
  const zmod::Material* m = poly.material;
  if (colour_is(m, 0, 0, 255)) kind = 2;         // 0x005e8a20
  else if (colour_is(m, 0, 255, 255)) kind = 3;  // 0x005e8a40
  else if (colour_is(m, 255, 0, 0)) kind = 0;    // 0x005e8a30
  else if (colour_is(m, 255, 255, 0)) kind = 1;  // 0x005e8a50
  tested = kind == 0 || kind == 2;
  inverted = kind >= 2;
  const uint32_t n = poly.flags & 0xff;
  verts.reserve(n);
  for (uint32_t k = 0; k < n; ++k) {
    const uint32_t i = kReverse[kind] ? n - 1 - k : k;
    const float* p = v + 3 * poly.vertex_indices[i];
    InOutVertex c;  // 0x004c1110
    std::memcpy(c.pos, p, 12);
    std::memcpy(c.offset, p, 12);
    verts.push_back(c);
  }
  make_sides(*this);
}

InOutPolygon::~InOutPolygon() { std::free(name); }

// Mech3.exe 1.2 @ 0x004c26b0 (the query's template copied)
InOutArea::InOutArea() {
  min[0] = min[1] = min[2] = FLT_MAX;
  max[0] = max[1] = max[2] = -FLT_MAX;
}

// Mech3.exe 1.2 @ 0x004c2080
void InOutPolygon::cross(InOutArea& area) const {
  if (!tested) return;
  InOutQuery c;
  c.from = area.q.from, c.to = area.q.to;
  c.poly = const_cast<InOutPolygon*>(this);
  c.length = area.q.length;
  const InOutVertex* prev = &verts.back();  // 0x004c5250 over 0x004c1410
  for (const InOutVertex& v : verts) {
    edge_count(c, prev, &v);
    prev = &v;
  }
  area.q.crossings += c.crossings;
  area.q.from_rays += c.from_rays;
  area.q.to_rays += c.to_rays;
  const bool from_in = ((c.from_rays & 1) != 0) != (inverted != 0);
  const bool to_in = ((c.to_rays & 1) != 0) != (inverted != 0);
  auto keep = [&] {
    area.q.poly = c.poly;
    area.q.near_a = c.near_a, area.q.near_b = c.near_b;
    area.q.far_a = c.far_a, area.q.far_b = c.far_b;
    area.q.t_near = c.t_near, area.q.t_far = c.t_far;
    area.from_in = from_in, area.to_in = to_in;
  };
  if (c.crossings == 0 && from_in && to_in) keep();
  if (c.crossings > 0 && lt(c.t_near, area.q.t_near)) keep();
}

// Mech3.exe 1.2 @ 0x004c25f0 (over 0x004c1770, from the last corner)
bool InOutPolygon::blocks(const float* from, const float* p) const {
  const InOutVertex* prev = &verts.back();
  for (const InOutVertex& v : verts) {
    if (crosses(from, p, prev->pos, v.pos)) return true;
    prev = &v;
  }
  return false;
}

// Mech3.exe 1.2 @ 0x004c2f20. The sides' lengths from near_b and from far_a
// to the end (0x004c2e90), the arc between them, then the two ends' gaps.
const InOutVertex* InOutPolygon::way_round(const InOutQuery& q) const {
  const InOutVertex* got = q.near_a;
  float arc = 0, rest = 0;
  for (const InOutVertex& v : verts) {
    if (&v == q.near_b) arc = v.length;
    else if (arc < 0.0f || arc > 0.0f) arc = arc + v.length;
    if (&v == q.far_a) rest = v.length;
    else if (rest < 0.0f || rest > 0.0f) rest = rest + v.length;
  }
  if (lt(arc, rest)) arc = (perimeter - rest) + arc;
  else arc = arc - rest;
  const float d1 = dist_xz(q.near_point, q.near_b->pos);  // 0x004c1090
  const float d2 = dist_xz(q.far_point, q.far_a->pos);
  arc = (d2 + d1) + arc;
  if (lt(2.0f * arc, perimeter)) got = q.near_b;  // 0x005cac60
  return got;
}

// Mech3.exe 1.2 @ 0x004c2470 / 0x004c5290 / 0x004c22c0: the sides from (last,
// first) on, each one's nearest point to `from` (its projection in x and z
// when it falls inside, else its first corner), by the distance in 3D
// (0x0051e000, the outline at height 0).
void InOutPolygon::nearest(const float* from, float out[3], const InOutVertex** a, const InOutVertex** b) const {
  float best = FLT_MAX;
  const InOutVertex* p = &verts.back();
  for (const InOutVertex& v : verts) {
    const float e[3] = {v.offset[0] - p->offset[0], 0.0f, v.offset[2] - p->offset[2]};  // 0x0041fb30, y 0
    const float u[3] = {p->offset[0] - from[0], 0.0f, p->offset[2] - from[2]};
    const float w[3] = {v.offset[0] - from[0], 0.0f, v.offset[2] - from[2]};
    const float d1 = dot3(e, u), d2 = dot3(e, w);  // 0x004c2430
    float at[3];
    const float* q = p->offset;
    if ((!(d1 > 0.0f) && lt(0.0f, d2)) || (lt(d2, 0.0f) && !(0.0f > d1))) {
      lerp(at, p->offset, d1 / (d1 - d2), v.offset);
      q = at;
    }
    const float d = dist3(q, from);
    if (lt(d, best)) {
      best = d;
      std::memcpy(out, q, 12);
      *a = p, *b = &v;
    }
    p = &v;
  }
}

// Mech3.exe 1.2 @ 0x004c2e30 (0x004c53d0 over 0x004c2940): every polygon,
// tested or not.
bool InOutArea::clear(const float* from, const float* p) const {
  for (const auto& poly : polygons)
    if (poly->blocks(from, p)) return false;
  return true;
}

// Mech3.exe 1.2 @ 0x004c2b00 (0x004c2a80: the state reset, each polygon's
// pass). Nothing to do unless the way crosses a side or both ends are in the
// same polygon. From inside one: the nearest point of its pushed-out
// outline. Into one, crossing once: the width short of it. Through one: the
// width short of the first crossing, then the pushed-out corners round the
// shorter way (0x004c2f20) up to the last crossing's side, the last one in
// a clear line from `from` taken.
const float* InOutArea::query(const float* from, const float* to) {
  const float* got = to;
  q = InOutQuery{};
  q.from = from, q.to = to;
  from_in = to_in = 0;
  for (const auto& poly : polygons) poly->cross(*this);  // 0x004c53a0 over 0x004c2920
  if (!(q.crossings > 0 || (from_in && to_in))) return got;
  const float d[2] = {to[0] - from[0], to[2] - from[2]};
  q.length = std::sqrt(d[0] * d[0] + d[1] * d[1]);  // 0x004c2e00
  if (from_in) {
    const InOutVertex *a = nullptr, *b = nullptr;
    q.poly->nearest(from, q.near_point, &a, &b);
    got = q.near_point;
    // FIX (bug): with every distance NaN no side is kept and the exe reads
    // the stack's leftovers; the normal is left as it was.
    if (a && b) {
      normal[0] = a->pos[2] - b->pos[2];
      normal[1] = 0.0f;
      normal[2] = b->pos[0] - a->pos[0];
      normalize3(normal);  // 0x004020d0
    }
  } else if (to_in && q.crossings == 1) {
    got = q.far_point;
    lerp(q.far_point, from, q.t_far - q.poly->width / q.length, to);
  } else if (q.near_a && q.far_a) {
    lerp(q.near_point, from, q.t_near - q.poly->width / q.length, to);
    lerp(q.far_point, from, q.poly->width / q.length + q.t_far, to);
    const InOutVertex* cur = q.poly->way_round(q);
    if (cur) {
      got = q.near_point;
      const InOutVertex* first = q.poly->verts.data();
      const InOutVertex* end = first + q.poly->verts.size();
      if (cur == q.near_a) {
        while (cur != q.far_a) {  // back round
          if (clear(from, cur->offset)) got = cur->offset;
          if (cur == first) cur = end;
          --cur;
        }
      } else if (cur == q.near_b) {
        while (cur != q.far_b) {  // forward round
          if (clear(from, cur->offset)) got = cur->offset;
          if (++cur == end) cur = first;
        }
      }
    }
  }
  return got;
}

// Mech3.exe 1.2 @ 0x004c36c0
InOutVariant inout_variant() {
  InOutVariant v;
  if (!g_hooks.have_game || !g_hooks.have_game()) return v;
  const char* suffix = nullptr;
  v.operation = g_hooks.operation ? g_hooks.operation() : 0;
  v.game = g_hooks.game_type ? g_hooks.game_type() : -1;
  if (v.game == 1) {
    v.numbered = 1;
    v.index = (g_hooks.mission_number ? g_hooks.mission_number() : 0) - 1;
    switch (v.operation) {  // 0x00484840
      case 1: case 2: v.count = 4; break;
      case 3: case 4: v.count = 6; break;
      default: v.count = 0;
    }
    suffix = "m";  // 0x005e8acc
  } else if (v.game == 2) {
    v.count = 1;
    suffix = "ia";  // 0x005e8ac8
  } else if (v.game == 4) {
    v.count = 1;
    suffix = "t";  // 0x005e8ac4
  }
  if (suffix) {
    v.valid = 1;
    v.prefix = std::snprintf(v.name, sizeof v.name, "%s%s", "inout", suffix);
  }
  return v;
}

// Mech3.exe 1.2 @ 0x004c3850
int32_t InOutVariant::match(const char* child) const {
  long n = 0;
  if (valid) {
    if (!numbered) {
      if (strcasecmp(name, child) == 0) n = 1;
    } else {
      char* end = nullptr;
      if (strncasecmp(name, child, static_cast<size_t>(prefix)) == 0) n = std::strtol(child + prefix, &end, 0);
      if (!end || *end != 0) n = 0;
    }
  }
  return static_cast<int32_t>(n - 1);
}

// Mech3.exe 1.2 @ 0x004c3e50 (ECX node, EDX area, the callback 0x004c3600):
// from the node's place in the world (0x004ed280 mode 3 on a fresh top).
void inout_read_tree(zcls::Node* node, InOutArea& area) {
  if (!node) return;
  zmth::Mat34 top;
  zmth::push(&top);
  zmth::load_identity();
  if (zcls::node_to_world(node, 3) == 0) walk(node, area);
  zmth::pop();
}

// Mech3.exe 1.2 @ 0x004c3930
void inout_mission_load() {
  g_inout.areas.reset();
  zcls::Node* root = zcls::list_find_by_name(6, "inout");
  if (!root) return;
  const InOutVariant v = inout_variant();
  if (g_hooks.have_game && g_hooks.have_game() && v.valid) {
    // 0x004c3630: count copies of a fresh area (a negative count is none).
    g_inout.areas = std::make_unique<std::vector<InOutArea>>(static_cast<size_t>(v.count > 0 ? v.count : 0));
    for (int32_t i = 0; i < root->child_count(); ++i) {
      zcls::Node* c = root->children[i];
      const int32_t at = v.match(c->name());
      // FIX (bug): the exe writes past the vector for a child numbered beyond
      // the operation's missions; none is shipped (docs/ai.md section 11).
      if (at >= 0 && static_cast<size_t>(at) < g_inout.areas->size()) inout_read_tree(c, (*g_inout.areas)[size_t(at)]);
    }
  }
  while (root->parent_count() > 0) zcls::node_remove_child(root->parents[0], root);  // 0x004ebb40
  zcls::tree_destroy(root);  // 0x004f9050
}

// Mech3.exe 1.2 @ 0x004c3b80
void inout_select() {
  if (!g_inout.areas) return;
  const int32_t i = inout_variant().index;
  // FIX (bug): unchecked in the exe (0x004c50e0); a campaign mission past the
  // operation's count would point past the vector.
  if (i >= 0 && static_cast<size_t>(i) < g_inout.areas->size()) g_inout.current = &(*g_inout.areas)[size_t(i)];
}

// Mech3.exe 1.2 @ 0x004c3000 (the vehicle's record left out, see the header)
bool inout_pull(const float pos[3], const float in[3], float out[3]) {
  InOutArea* a = g_inout.current;
  if (!a) return false;
  const float* r = a->query(pos, in);  // 0x004c2670
  if (r == in) return false;
  out[0] = r[0], out[1] = r[1], out[2] = r[2];
  out[1] = in[1];  // after the copy: with `in` as `out`, the pulled point's own height
  return true;
}

// Mech3.exe 1.2 @ 0x004c3bd0 -> 0x004c2a30 over 0x004c29a0. Any other
// message tests the red ones (kind 0).
namespace {
int32_t g_inout_changes = 0;
}
int32_t inout_changes() { return g_inout_changes; }

void inout_set_state(const char* name, int32_t message) {
  if (!name || !g_inout.current) return;
  ++g_inout_changes;
  int32_t kind = 0;
  switch (message) {
    case 0x39: kind = 1; break;
    case 0x3a: kind = 0; break;
    case 0x3b: kind = 3; break;
    case 0x3c: kind = 2; break;
  }
  for (const auto& p : g_inout.current->polygons)
    if (p->name && std::strcmp(name, p->name) == 0) p->tested = p->kind == kind;
}

// Mech3.exe 1.2 @ 0x004c3dd0
void inout_mission_end() {
  g_inout.areas.reset();
  ++g_inout_changes;
  // FIX (bug): the exe leaves [0x0060b344] pointing at the freed area; a next
  // mission without one (multiplayer, or a world without `inout`) then pulls
  // the AI's destinations with freed memory (docs/ai.md section 11).
  g_inout.current = nullptr;
}

}  // namespace mech3::ai
