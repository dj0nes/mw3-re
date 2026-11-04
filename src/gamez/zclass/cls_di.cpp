#include "gamez/zclass/cls_di.h"
#include "gamez/zgame/zgame_table.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "crt/msvc_math.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zmodel/gmod_hit.h"

#pragma STDC FP_CONTRACT OFF

namespace zcls {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zClass\\cls_di.c";
constexpr int32_t kMaxProbes = 24;
constexpr uint32_t kActive = 4, kAltitude = 8, kFade = 0x10, kHasBox = 0x100, kZoned = 0x1000000;

// The query in progress (0x0060bab0..0x0060bac0).
struct Query {
  GroundHits* out = nullptr;   // 0x0060bab0 (0x0060bab4: its first hit)
  float top = 0;               // 0x0060bab8
  float* probes = nullptr;     // 0x0060babc
  int32_t count = 0;           // 0x0060bac0
};
Query g_q;

// The segment query in progress: the segments' boxes (0x0060b990, min xyz
// then max xyz) and the records ([0x0060bab0]).
struct SegmentQuery {
  float box[12][6];
  SegmentHits* out = nullptr;
  float* segs = nullptr;           // [0x0060babc]
  int32_t count = 0;               // [0x0060bac0], segments
};
SegmentQuery g_sq;
SegmentOptions g_opt;

bool stop_early() { return g_opt.first_hit && g_sq.out[0].count > 0; }

template <typename T> T at(const void* base, int off) {
  T v;
  std::memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof v);
  return v;
}
template <typename T> void put_at(void* base, int off, T v) { std::memcpy(static_cast<uint8_t*>(base) + off, &v, sizeof v); }

bool tested(const Node* n) { return (n->flags() & kActive) && (n->flags() & kAltitude); }

zmth::Mat34* mat(uint8_t* d, int off) { return reinterpret_cast<zmth::Mat34*>(d + off); }

int ground_node(Node* node, int32_t siblings, const int32_t* mask);

void model_test(Node* node, const int32_t* mask) {
  if (node->model) zmod::model_ground(node, node->model, g_q.probes, mask, g_q.count, g_q.top, g_q.out);
}

void ground_children(Node* node, const int32_t* mask, bool all) {
  for (int32_t i = 0; i < node->child_count(); ++i)
    if (all || tested(node->children[i])) ground_node(node->children[i], node->child_count(), mask);
}

// Mech3.exe 1.2 @ 0x004f1350. Masks out the probes outside the node's box in
// x and z; 1 when none is left inside (the node is skipped), also for a node
// without a box.
int box_test(Node* node, int32_t* mask) {
  if (!(node->flags() & kHasBox)) return 1;
  float c[8][2];
  node_box_xz(node, c);
  float minx = c[0][0], maxx = c[0][0], minz = c[0][1], maxz = c[0][1];
  for (int i = 1; i < 8; ++i) {
    if (c[i][0] < minx) minx = c[i][0];
    else if (c[i][0] > maxx) maxx = c[i][0];
    if (c[i][1] < minz) minz = c[i][1];
    else if (maxz < c[i][1]) maxz = c[i][1];
  }
  int none = 1;
  for (int32_t k = 0; k < g_q.count; ++k) {
    if (!mask[k]) continue;
    const float* p = g_q.probes + 3 * k;
    if (p[0] < minx || p[0] > maxx || p[2] < minz || maxz < p[2]) mask[k] = 0;
    else none = 0;
  }
  return none;
}

// Mech3.exe 1.2 @ 0x004eeb50 (an animated node, class 8)
int ground_animate(Node* node, int32_t siblings, const int32_t* in) {
  int32_t mask[kMaxProbes];
  std::memcpy(mask, in, 4 * g_q.count);
  if (siblings > 1) {
    if (int r = box_test(node, mask)) return r;
  }
  auto* d = static_cast<uint8_t*>(node->data);
  const bool pushed = node->raw[0x24] & kActive;
  if (pushed) {
    zmth::push_copy(mat(d, 0x38));
    zmth::multiply(*mat(d, 8), 1);
  }
  model_test(node, mask);
  ground_children(node, mask, true);
  if (pushed) zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004eec10 (a light, class 9: its position and angles)
int ground_light(Node* node, int32_t siblings, const int32_t* in) {
  int32_t mask[kMaxProbes];
  std::memcpy(mask, in, 4 * g_q.count);
  if (siblings > 1) {
    if (int r = box_test(node, mask)) return r;
  }
  auto* d = static_cast<uint8_t*>(node->data);
  zmth::push_copy(mat(d, 0x30));
  zmth::translate(at<float>(d, 0xc), at<float>(d, 0x10), at<float>(d, 0x14));
  zmth::rotate_y(at<float>(d, 4));
  zmth::rotate_x(at<float>(d, 0));
  zmth::rotate_z(at<float>(d, 8));
  model_test(node, mask);
  ground_children(node, mask, true);
  zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004ee790. `siblings` (EDX) is the count of the list the
// node came from: alone, it skips the box test. The return is ignored by
// every caller; class 10 returns a stack slot never written (0 here).
int ground_node(Node* node, int32_t siblings, const int32_t* in) {
  const uint32_t flags = node->flags();
  if (!(flags & kActive) || !(flags & kAltitude)) return 1;
  if ((flags & kZoned) && !zmod::zone_visible(node->raw[0x30])) return 1;
  const uint32_t kept = node->flags() & ~0x2000000u;
  node->set_flags(kept);
  int32_t mask[kMaxProbes];
  auto* d = static_cast<uint8_t*>(node->data);
  switch (node->cls()) {
    case 1: {  // camera: its pose at unit scale, in the slot at +0x44
      std::memcpy(mask, in, 4 * g_q.count);
      const float scale[3] = {1.0f, 1.0f, 1.0f};
      bool pushed = false;
      if (kept & kActive) {
        pushed = true;
        zmth::push_copy(mat(d, 0x44));
        zmth::euler(reinterpret_cast<float*>(d + 0x20), reinterpret_cast<float*>(d + 0x14), scale);
      }
      model_test(node, mask);
      ground_children(node, mask, false);
      if (pushed) zmth::pop();
      return 0;
    }
    case 5: {
      std::memcpy(mask, in, 4 * g_q.count);
      if (siblings > 1) {
        if (int r = box_test(node, mask)) return r;
      }
      auto* o = static_cast<Object3d*>(node->data);
      uint32_t of = o->get<uint32_t>(0);
      bool pushed = false;
      if (!(of & 8)) {  // not the identity
        pushed = true;
        if (!(node->flags() & 0x80000)) {
          zmth::push_copy(mat(d, 0x60));
          zmth::multiply(*mat(d, 0x30), 1);
        } else if (!(of & 0x20)) {
          zmth::push(mat(d, 0x60));  // the cached world matrix
        } else {
          zmth::push_copy(mat(d, 0x60));
          zmth::multiply(*mat(d, 0x30), 1);
          of = o->get<uint32_t>(0);
          if (!(of & 1)) o->put<uint32_t>(0, of & ~0x20u);
        }
      }
      model_test(node, mask);
      ground_children(node, mask, false);
      if (pushed) zmth::pop();
      return 0;
    }
    case 6:  // lod: past 5 it is not tested
      if (at<float>(d, 4) > 5.0) return 1;  // 0x005cdaf8
      std::memcpy(mask, in, 4 * g_q.count);
      if (siblings > 1) {
        if (int r = box_test(node, mask)) return r;
      }
      ground_children(node, mask, true);
      return 0;
    case 7: {  // sequence: only its current child
      if (at<int32_t>(d, 0) == 0) return 1;
      std::memcpy(mask, in, 4 * g_q.count);
      if (siblings > 1) {
        if (int r = box_test(node, mask)) return r;
      }
      Node* child;
      std::memcpy(&child, d + 0x20 + 8 * at<int32_t>(d, 0x14), sizeof child);  // FIX (A): a 32-bit pointer in the exe
      return ground_node(child, node->child_count(), mask);
    }
    case 8: return ground_animate(node, siblings, in);
    case 9: return ground_light(node, siblings, in);
    case 10: return 0;
    default: {
      char msg[96];
      std::snprintf(msg, sizeof msg, "Unrecognized node class type:  node = %s class_type = %d", node->name(),
                    node->cls());
      zerr::report(0x200, kFile, 0x626, msg);
      return 3;
    }
  }
}

// Mech3.exe 1.2 @ 0x004f1640. Masks out the segments whose box misses the
// node's (both boxes in world space); 1 when none is left. With
// INTERSECT_BBOX the box's faces are tested too and decide.
int box_segments_test(Node* node, int32_t* mask) {
  if (!(node->flags() & kHasBox)) return 1;
  float c[8][3];
  node_box_corners(node, c);
  float lo[3] = {c[0][0], c[0][1], c[0][2]}, hi[3] = {c[0][0], c[0][1], c[0][2]};
  for (int i = 1; i < 8; ++i) {
    if (c[i][0] < lo[0]) lo[0] = c[i][0];
    else if (c[i][0] > hi[0]) hi[0] = c[i][0];
    if (c[i][1] < lo[1]) lo[1] = c[i][1];
    else if (c[i][1] > hi[1]) hi[1] = c[i][1];
    if (c[i][2] < lo[2]) lo[2] = c[i][2];
    else if (hi[2] < c[i][2]) hi[2] = c[i][2];
  }
  int32_t any = 0;
  for (int32_t k = 0; k < g_sq.count; ++k) {
    if (!mask[k]) continue;
    const float* b = g_sq.box[k];
    if (!(b[3] > lo[0]) || !(b[0] < hi[0]) || !(b[4] > lo[1]) || !(b[1] < hi[1]) || !(b[5] > lo[2]) ||
        !(hi[2] > b[2]))
      mask[k] = 0;
    else
      any = 1;
  }
  if (any && (node->flags() & 0x20)) any = zmod::box_segments(node, g_sq.out, g_sq.segs, mask, g_sq.count, c);
  return any == 0;
}

int segment_node(Node* node, int32_t siblings, const int32_t* mask);

void model_segments_of(Node* node, const int32_t* mask) {
  if (node->model) zmod::model_segments(node, node->model, g_sq.segs, mask, g_sq.count, g_sq.out);
}

void segment_children(Node* node, const int32_t* mask, bool all) {
  for (int32_t i = 0; i < node->child_count(); ++i) {
    Node* c = node->children[i];
    if (!all && !((c->flags() & kActive) && (c->flags() & kFade))) continue;
    segment_node(c, node->child_count(), mask);
    if (stop_early()) return;
  }
}

// The box test the segment visitors share: alone and without INTERSECT_BBOX
// a node skips it; a node that is its box ends there. -1 goes on.
int segment_box(Node* node, int32_t siblings, int32_t* mask) {
  if (siblings > 1 || (node->flags() & 0x20)) {
    if (int r = box_segments_test(node, mask)) return r;
    if (node->flags() & 0x20) return 0;
  }
  return -1;
}

// Mech3.exe 1.2 @ 0x004f08c0 (class 8)
int segment_animate(Node* node, int32_t siblings, const int32_t* in) {
  int32_t mask[kMaxProbes];
  std::memcpy(mask, in, 4 * g_sq.count);
  const int b = segment_box(node, siblings, mask);
  if (b >= 0) return b;
  auto* d = static_cast<uint8_t*>(node->data);
  const bool pushed = node->flags() & kActive;
  if (pushed) {
    zmth::push_copy(mat(d, 0x38));
    zmth::multiply(*mat(d, 8), 1);
  }
  model_segments_of(node, mask);
  if (!stop_early()) segment_children(node, mask, true);
  if (pushed) zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004f09b0 (class 9)
int segment_light(Node* node, int32_t siblings, const int32_t* in) {
  int32_t mask[kMaxProbes];
  std::memcpy(mask, in, 4 * g_sq.count);
  const int b = segment_box(node, siblings, mask);
  if (b >= 0) return b;
  auto* d = static_cast<uint8_t*>(node->data);
  zmth::push_copy(mat(d, 0x30));
  zmth::translate(at<float>(d, 0xc), at<float>(d, 0x10), at<float>(d, 0x14));
  zmth::rotate_y(at<float>(d, 4));
  zmth::rotate_x(at<float>(d, 0));
  zmth::rotate_z(at<float>(d, 8));
  model_segments_of(node, mask);
  if (!stop_early()) segment_children(node, mask, false);
  zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004f0480. Unlike the ground walk, the zone is always
// tested, and the object3d's matrix is multiplied in mode 3 (all of it, as 1).
// Class 10 returns its mask pointer in the exe (non-zero); 1 here.
int segment_node(Node* node, int32_t siblings, const int32_t* in) {
  const uint32_t flags = node->flags();
  if (!(flags & kActive) || !(flags & kFade)) return 1;
  if (g_opt.exclude & flags) return 1;
  if (!zmod::zone_visible(node->raw[0x30])) return 1;
  const uint32_t kept = node->flags() & ~0x2000000u;
  node->set_flags(kept);
  int32_t mask[kMaxProbes];
  auto* d = static_cast<uint8_t*>(node->data);
  switch (node->cls()) {
    case 1: {
      const float scale[3] = {1.0f, 1.0f, 1.0f};
      bool pushed = false;
      if (kept & kActive) {
        pushed = true;
        zmth::push_copy(mat(d, 0x44));
        zmth::euler(reinterpret_cast<float*>(d + 0x20), reinterpret_cast<float*>(d + 0x14), scale);
      }
      model_segments_of(node, in);
      if (!stop_early()) segment_children(node, in, true);
      if (pushed) zmth::pop();
      return 0;
    }
    case 5: {
      std::memcpy(mask, in, 4 * g_sq.count);
      const int b = segment_box(node, siblings, mask);
      if (b >= 0) return b;
      auto* o = static_cast<Object3d*>(node->data);
      uint32_t of = o->get<uint32_t>(0);
      bool pushed = false;
      if (!(of & 8)) {
        pushed = true;
        if (!(node->flags() & 0x80000)) {
          zmth::push_copy(mat(d, 0x60));
          zmth::multiply(*mat(d, 0x30), 3);
        } else if (!(of & 0x20)) {
          zmth::push(mat(d, 0x60));
        } else {
          zmth::push_copy(mat(d, 0x60));
          zmth::multiply(*mat(d, 0x30), 1);
          of = o->get<uint32_t>(0);
          if (!(of & 1)) o->put<uint32_t>(0, of & ~0x20u);
        }
      }
      model_segments_of(node, mask);
      if (!stop_early()) segment_children(node, mask, false);
      if (pushed) zmth::pop();
      return 0;
    }
    case 6: {
      if (at<float>(d, 4) > 5.0) return 1;  // 0x005cdaf8
      std::memcpy(mask, in, 4 * g_sq.count);
      const int b = segment_box(node, siblings, mask);
      if (b >= 0) return b;
      segment_children(node, mask, true);
      return 0;
    }
    case 7: {
      if (at<int32_t>(d, 0) == 0) return 1;
      std::memcpy(mask, in, 4 * g_sq.count);
      const int b = segment_box(node, siblings, mask);
      if (b >= 0) return b;
      Node* child;
      std::memcpy(&child, d + 0x20 + 8 * at<int32_t>(d, 0x14), sizeof child);  // FIX (A): a 32-bit pointer in the exe
      return segment_node(child, node->child_count(), mask);
    }
    case 8: return segment_animate(node, siblings, in);
    case 9: return segment_light(node, siblings, in);
    case 10: return 1;
    default: {
      char msg[96];
      std::snprintf(msg, sizeof msg, "Unrecognized node class type:  node = %s class_type = %d", node->name(),
                    node->cls());
      zerr::report(0x200, kFile, 0xe43, msg);
      return 3;
    }
  }
}

int32_t cell_of(float v, float origin, float inv) {
  return crt::msvc_ftol(std::floor(static_cast<double>((v - origin) * inv)));
}
// Mech3.exe 1.2 @ 0x004efef0: the cells under the segments' boxes (all of
// them, dropped ones too), each cell's nodes and fades.
void segment_cells(Node* world, const int32_t* valid) {
  auto* w = static_cast<World*>(world->data);
  int32_t x_lo = 0, x_hi = 0, z_lo = 0, z_hi = 0;
  for (int32_t k = 0; k < g_sq.count; ++k) {
    const float* b = g_sq.box[k];
    const int32_t a = cell_of(b[0], w->get<float>(0x34), w->get<float>(0x74));
    const int32_t zb = cell_of(b[5], w->get<float>(0x38), w->get<float>(0x78));
    const int32_t c = cell_of(b[3], w->get<float>(0x34), w->get<float>(0x74));
    const int32_t zd = cell_of(b[2], w->get<float>(0x38), w->get<float>(0x78));
    if (k == 0) {
      x_lo = a, z_lo = zb, x_hi = c, z_hi = zd;
      continue;
    }
    if (a < x_lo) x_lo = a;
    if (zb < z_lo) z_lo = zb;
    if (c > x_hi) x_hi = c;
    if (zd > z_hi) z_hi = zd;
  }
  for (int32_t z = z_lo; z <= z_hi; ++z) {
    for (int32_t x = x_lo; x <= x_hi; ++x) {
      bool wrapped = false;
      int32_t sx = x, sz = z;
      float dx = 0, dz = 0;
      uint8_t saved[12];
      if (!(x >= 0 && x < w->get<int32_t>(0x88) && z >= 0 && z < w->get<int32_t>(0x8c))) {
        if (w->get<int32_t>(0x50) == 0) continue;
        wrapped = true;
        for (int32_t k = 0; k < g_sq.count; ++k) saved[k] = g_sq.out[k].count;
        int32_t inside;
        world_wrap_cell(world, x, z, &sx, &sz, &inside);
        dx = static_cast<float>(sx - x) * w->get<float>(0x64);
        dz = static_cast<float>(sz - z) * w->get<float>(0x68);
        for (int32_t k = 0; k < g_sq.count; ++k) {
          float* s = g_sq.segs + 6 * k;
          s[0] = dx + s[0], s[2] = dz + s[2], s[3] = dx + s[3], s[5] = dz + s[5];
          float* b = g_sq.box[k];
          b[0] = dx + b[0], b[2] = dz + b[2], b[3] = dx + b[3], b[5] = dz + b[5];
        }
      }
      Partition& p = w->rows[sz][sx];
      for (int32_t i = 0; i < p.get<int16_t>(0x3a); ++i) {
        Node* n = p.nodes[i];
        if (!(n->flags() & kActive) || !(n->flags() & kFade)) continue;
        segment_node(n, p.get<int16_t>(0x3a) + 1, valid);
        if (stop_early()) break;
      }
      // Pirate's Moon: +0x40 is the cell's clutter records; its queries (0x004ffb40 and siblings) are still to port.
      for (int32_t i = 0; zgame::game().cell_fades && i < p.get<int32_t>(0x40); ++i) {
        uint8_t* e = p.fades + 0x44 * i;
        Node* n;
        std::memcpy(&n, e, sizeof n);  // FIX (A): a 32-bit pointer in the exe
        if (!(n->flags() & kActive) || !(n->flags() & kFade)) continue;
        std::memcpy(n->raw + 0x64, e + 0x28, 12);
        if (n->cls() == 5) {
          auto* o = static_cast<Object3d*>(n->data);
          o->put<uint32_t>(0, o->get<uint32_t>(0) & ~8u);
          std::memcpy(o->raw + 0x60, e + 4, 48);
          std::memcpy(o->raw + 0x30, e + 4, 48);
        }
        n->raw[0x30] = e[0x40];
        segment_node(n, p.get<int32_t>(0x40) + 1, valid);
        if (stop_early()) return;  // the shifted segments are not moved back
      }
      if (!wrapped) continue;
      for (int32_t k = 0; k < g_sq.count; ++k) {
        float* s = g_sq.segs + 6 * k;
        s[0] = s[0] - dx, s[2] = s[2] - dz, s[3] = s[3] - dx, s[5] = s[5] - dz;
        float* b = g_sq.box[k];
        b[0] = b[0] - dx, b[2] = b[2] - dz, b[3] = b[3] - dx, b[5] = b[5] - dz;
      }
      for (int32_t k = 0; k < g_sq.count; ++k) {
        SegmentHits& r = g_sq.out[k];
        for (int32_t j = saved[k]; j < r.count; ++j) {
          r.hit[j].point[0] = r.hit[j].point[0] - dx;
          r.hit[j].point[2] = r.hit[j].point[2] - dz;
        }
      }
    }
  }
}
}  // namespace

SegmentOptions& segment_options() { return g_opt; }

// Mech3.exe 1.2 @ 0x004efcd0
void segment_query(Node* world, float* segs, int32_t points, SegmentHits* out) {
  if (points > kMaxProbes) {
    char msg[64];
    std::snprintf(msg, sizeof msg, "More test pnts than space for: %d", points);
    zerr::report(0x200, kFile, 0xc83, msg);
    points = kMaxProbes;
  }
  if (update_hooks().pending) update_hooks().pending(update_hooks().ctx);
  const int32_t n = points >> 1;
  int32_t valid[12];
  for (int32_t k = 0; k < n; ++k) valid[k] = 1, out[k].count = 0;
  auto* w = static_cast<World*>(world->data);
  int32_t any = 0;
  for (int32_t k = 0; k < n; ++k) {
    const float* s = segs + 6 * k;
    float* b = g_sq.box[k];
    for (int c = 0; c < 3; ++c) {
      if (s[c] < s[3 + c]) b[c] = s[c], b[3 + c] = s[3 + c];
      else b[c] = s[3 + c], b[3 + c] = s[c];
    }
    // Quirk: the bounds test reads the first segment's box (fixed addresses
    // 0x0060b990..), so every segment stands or falls with segment 0.
    const float* b0 = g_sq.box[0];
    if (w->get<int32_t>(0x50) != 0) {
      any = 1;
    } else if (!(b0[0] < w->get<float>(0x44)) || b0[3] < w->get<float>(0x34) || b0[2] > w->get<float>(0x38) ||
               !(b0[5] > w->get<float>(0x48))) {
      valid[k] = 0;
    } else {
      any = 1;
    }
  }
  if (any) {
    zmth::Mat34 m;
    zmth::push(&m);
    zmth::load_identity();
    g_sq.out = out;
    g_sq.segs = segs;
    g_sq.count = n;
    segment_cells(world, valid);
    for (int32_t i = 0; i < world->child_count(); ++i) {
      Node* c = world->children[i];
      if (!(c->flags() & kActive) || !(c->flags() & kFade) || !zmod::zone_visible(c->raw[0x30])) continue;
      segment_node(c, world->child_count() + 1, valid);
      if (stop_early()) break;
    }
    zmth::pop();
  }
  g_opt.exclude = 0;
}

namespace {
// The point query in progress: the point (0x0060b978, moved into the
// stored cell while it is tested) and the record (0x0060bab0; the next
// slot, 0x0060bab4, is its count's).
struct PointQuery {
  float p[3] = {};
  GroundHits* out = nullptr;
};
PointQuery g_pq;

int point_node(Node* node, int32_t siblings);

// The node's model under the point: a hit takes the record's next slot.
// FIX (A): the exe's record has no room past 32 (only the node walk's own
// entry checks); the port stops there.
void point_model(Node* node) {
  GroundHits& r = *g_pq.out;
  if (!node->model || r.count >= 32) return;
  if (!zmod::model_ground_point(node->model, &r.hit[r.count], g_pq.p)) return;
  r.hit[r.count].node = node;
  ++r.count;
}

void point_children(Node* node, bool all) {
  for (int32_t i = 0; i < node->child_count(); ++i)
    if (all || tested(node->children[i])) point_node(node->children[i], node->child_count());
}

int point_result() { return g_pq.out->count == 0; }

// Mech3.exe 1.2 @ 0x004f12e0. 0 when the point is inside the node's box
// in x and z; 1 outside, or without a box.
int box_point_test(Node* node) {
  if (!(node->flags() & kHasBox)) return 1;
  float b[6];
  node_box_extent_xz(node, b);
  const float x = g_pq.p[0], z = g_pq.p[2];
  if (!(x >= b[0]) || x > b[3] || !(z >= b[2]) || z > b[5]) return 1;
  return 0;
}

// Mech3.exe 1.2 @ 0x004ee140 (class 8). Its own model is tested before its
// matrix goes on the stack, then its box.
int point_animate(Node* node, int32_t siblings) {
  point_model(node);
  auto* d = static_cast<uint8_t*>(node->data);
  const bool pushed = node->raw[0x24] & kActive;
  if (pushed) {
    zmth::push_copy(mat(d, 0x38));
    zmth::multiply(*mat(d, 8), 1);
  }
  if (siblings > 1) {
    if (int r = box_point_test(node)) {
      if (pushed) zmth::pop();
      return r;
    }
  }
  point_children(node, true);
  if (pushed) zmth::pop();
  return point_result();
}

// Mech3.exe 1.2 @ 0x004ee220 (class 9)
int point_light(Node* node, int32_t siblings) {
  if (siblings > 1) {
    if (int r = box_point_test(node)) return r;
  }
  auto* d = static_cast<uint8_t*>(node->data);
  zmth::push_copy(mat(d, 0x30));
  zmth::translate(at<float>(d, 0xc), at<float>(d, 0x10), at<float>(d, 0x14));
  zmth::rotate_y(at<float>(d, 4));
  zmth::rotate_x(at<float>(d, 0));
  zmth::rotate_z(at<float>(d, 8));
  point_model(node);
  point_children(node, true);
  zmth::pop();
  return point_result();
}

// Mech3.exe 1.2 @ 0x004eddb0. As ground_node, for one point; class 10
// returns a stack slot never written (0 here; no caller reads it).
int point_node(Node* node, int32_t siblings) {
  const uint32_t flags = node->flags();
  if (!(flags & kActive) || !(flags & kAltitude)) return 1;
  if ((flags & kZoned) && !zmod::zone_visible(node->raw[0x30])) return 1;
  const uint32_t kept = node->flags() & ~0x2000000u;
  node->set_flags(kept);
  if (g_pq.out->count > 0x1f) {
    zerr::report(0x200, kFile, 0x2e3, "Database intersections array is full");
    return 1;
  }
  auto* d = static_cast<uint8_t*>(node->data);
  switch (node->cls()) {
    case 1: {  // camera
      const float scale[3] = {1.0f, 1.0f, 1.0f};
      const bool pushed = kept & kActive;
      if (pushed) {
        zmth::push_copy(mat(d, 0x44));
        zmth::euler(reinterpret_cast<float*>(d + 0x20), reinterpret_cast<float*>(d + 0x14), scale);
      }
      point_model(node);
      point_children(node, false);
      if (pushed) zmth::pop();
      return point_result();
    }
    case 5: {
      if (siblings > 1) {
        if (int r = box_point_test(node)) return r;
      }
      auto* o = static_cast<Object3d*>(node->data);
      uint32_t of = o->get<uint32_t>(0);
      bool pushed = false;
      if (!(of & 8)) {
        pushed = true;
        if (!(node->flags() & 0x80000)) {
          zmth::push_copy(mat(d, 0x60));
          zmth::multiply(*mat(d, 0x30), 1);
        } else if (!(of & 0x20)) {
          zmth::push(mat(d, 0x60));
        } else {
          zmth::push_copy(mat(d, 0x60));
          zmth::multiply(*mat(d, 0x30), 1);
          of = o->get<uint32_t>(0);
          if (!(of & 1)) o->put<uint32_t>(0, of & ~0x20u);
        }
      }
      point_model(node);
      point_children(node, false);
      if (pushed) zmth::pop();
      return point_result();
    }
    case 6:  // lod: past 5 it is not tested
      if (at<float>(d, 4) > 5.0) return 1;  // 0x005cdaf8
      if (siblings > 1) {
        if (int r = box_point_test(node)) return r;
      }
      point_children(node, true);
      return point_result();
    case 7: {  // sequence: only its current child
      if (at<int32_t>(d, 0) == 0) return 1;
      if (siblings > 1) {
        if (int r = box_point_test(node)) return r;
      }
      Node* child;
      std::memcpy(&child, d + 0x20 + 8 * at<int32_t>(d, 0x14), sizeof child);  // FIX (A): a 32-bit pointer in the exe
      return point_node(child, node->child_count());
    }
    case 8: return point_animate(node, siblings);
    case 9: return point_light(node, siblings);
    case 10: return 0;
    default: {
      char msg[96];
      std::snprintf(msg, sizeof msg, "Unrecognized node class type:  node = %s class_type = %d", node->name(),
                    node->cls());
      zerr::report(0x200, kFile, 0x30d, msg);
      return 3;
    }
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x004edab0. The stored cell's nodes, then its fading
// ones (each given its stored pose and zone first), then the world's own
// children at the point as given.
int ground_point(Node* world, float x, float y, float z, GroundHits* out) {
  out->count = 0;
  if (update_hooks().pending) update_hooks().pending(update_hooks().ctx);  // 0x004f7e50 when [0x0060bea0]
  g_pq.p[0] = x, g_pq.p[1] = y, g_pq.p[2] = z;
  g_pq.out = out;
  auto* w = static_cast<World*>(world->data);
  zmth::Mat34 m;
  zmth::push(&m);
  zmth::load_identity();
  const int32_t cx = cell_of(x, w->get<float>(0x34), w->get<float>(0x74));
  const int32_t cz = cell_of(z, w->get<float>(0x38), w->get<float>(0x78));
  Partition* cell = nullptr;
  bool wrapped = false;
  float shift[2] = {};
  if (cx >= 0 && cx < w->get<int32_t>(0x88) && cz >= 0 && cz < w->get<int32_t>(0x8c)) {
    cell = &w->rows[cz][cx];
  } else if (w->get<int32_t>(0x50) != 0) {
    int32_t sx, sz, inside;
    world_wrap_cell(world, cx, cz, &sx, &sz, &inside);
    wrapped = true;
    shift[0] = static_cast<float>(sx - cx) * w->get<float>(0x64);
    shift[1] = static_cast<float>(sz - cz) * w->get<float>(0x68);
    cell = &w->rows[sz][sx];
    g_pq.p[0] = shift[0] + g_pq.p[0];
    g_pq.p[2] = shift[1] + g_pq.p[2];
  }
  if (cell) {
    Partition& p = *cell;
    for (int32_t i = 0; i < p.get<int16_t>(0x3a); ++i) {
      Node* n = p.nodes[i];
      if (!tested(n)) continue;
      if ((n->flags() & kZoned) && !zmod::zone_visible(n->raw[0x30])) continue;
      point_node(n, p.get<int16_t>(0x3a) + 1);
    }
    // Pirate's Moon: +0x40 is the cell's clutter records; its queries (0x004ffb40 and siblings) are still to port.
    for (int32_t i = 0; zgame::game().cell_fades && i < p.get<int32_t>(0x40); ++i) {
      uint8_t* e = p.fades + 0x44 * i;
      Node* n;
      std::memcpy(&n, e, sizeof n);  // FIX (A): a 32-bit pointer in the exe
      if (!(n->flags() & kActive) || !(n->flags() & kFade)) continue;
      n->raw[0x30] = e[0x40];
      if ((n->flags() & kZoned) && !zmod::zone_visible(n->raw[0x30])) continue;
      std::memcpy(n->raw + 0x64, e + 0x28, 12);
      if (n->cls() == 5) {
        auto* o = static_cast<Object3d*>(n->data);
        o->put<uint32_t>(0, o->get<uint32_t>(0) & ~8u);
        std::memcpy(o->raw + 0x60, e + 4, 48);
        std::memcpy(o->raw + 0x30, e + 4, 48);
      }
      point_node(n, p.get<int32_t>(0x40) + 1);
    }
    if (wrapped) {
      g_pq.p[0] = g_pq.p[0] - shift[0];
      g_pq.p[2] = g_pq.p[2] - shift[1];
    }
  }
  for (int32_t i = 0; i < world->child_count(); ++i) {
    Node* n = world->children[i];
    if (!tested(n)) continue;
    if ((n->flags() & kZoned) && !zmod::zone_visible(n->raw[0x30])) continue;
    point_node(n, world->child_count() + 1);
  }
  zmth::pop();
  return out->count == 0;
}

// Mech3.exe 1.2 @ 0x004eda10. Of the hits, the highest whose height is at or
// below at[1]; on a tie the later one unless the kept one's zone byte is set.
int ground_below(Node* world, const float at[3], GroundHits* out) {
  if (ground_point(world, at[0], at[1], at[2], out) != 0) {
    out->count = 0;
    out->hit[0].zone[0] = 0xffffff00u;  // 0x0053cc40 on it: no zone
    return 0;
  }
  ground_keep_below(out, at[1]);
  return 0;
}

// 0x004eda10's choice, as its x87 compares go: a NaN height counts as below
// the point and as equal to the kept one.
void ground_keep_below(GroundHits* out, float y) {
  int best = 0;
  for (int i = 1; i < out->count; ++i) {
    const GroundHit& n = out->hit[i];
    const GroundHit& b = out->hit[best];
    if (n.height > y) continue;
    const bool equal = !(n.height < b.height) && !(n.height > b.height);
    if (b.height > y || n.height > b.height || (equal && (b.zone[0] & 0xff) == 0)) best = i;
  }
  out->hit[0] = out->hit[best];
  out->count = 1;
}

// Mech3.exe 1.2 @ 0x004ee2f0
int ground_query(Node* world, float* probes, int32_t count, float top, GroundHits* out) {
  if (count > kMaxProbes) {
    char msg[64];
    std::snprintf(msg, sizeof msg, "More test pnts than space for: %d", count);
    zerr::report(0x200, kFile, 0x50d, msg);
    count = kMaxProbes;
  }
  if (update_hooks().pending) update_hooks().pending(update_hooks().ctx);  // 0x004f7e50 when [0x0060bea0]
  auto* w = static_cast<World*>(world->data);
  int32_t valid[kMaxProbes], wrapped[kMaxProbes];
  Partition* cell[kMaxProbes];
  float shift[kMaxProbes][2];
  for (int32_t i = 0; i < count; ++i) {
    float* p = probes + 3 * i;
    wrapped[i] = 0;
    valid[i] = 1;
    out[i].count = 0;
    const int32_t cx = cell_of(p[0], w->get<float>(0x34), w->get<float>(0x74));
    const int32_t cz = cell_of(p[2], w->get<float>(0x38), w->get<float>(0x78));
    if (cx >= 0 && cx < w->get<int32_t>(0x88) && cz >= 0 && cz < w->get<int32_t>(0x8c)) {
      cell[i] = &w->rows[cz][cx];
    } else if (w->get<int32_t>(0x50) == 0) {
      wrapped[i] = 0;
      valid[i] = 0;
    } else {
      int32_t sx, sz, inside;
      world_wrap_cell(world, cx, cz, &sx, &sz, &inside);
      wrapped[i] = 1;
      cell[i] = &w->rows[sz][sx];
      shift[i][0] = static_cast<float>(sx - cx) * w->get<float>(0x64);
      shift[i][1] = static_cast<float>(sz - cz) * w->get<float>(0x68);
      p[0] = shift[i][0] + p[0];
      p[2] = shift[i][1] + p[2];
    }
  }
  Partition* cells[kMaxProbes];
  int32_t ncells = 0;
  for (int32_t i = 0; i < count; ++i) {
    if (!valid[i]) continue;
    bool fresh = true;
    for (int32_t k = 0; k < ncells; ++k)
      if (cell[i] == cells[k]) fresh = false;
    if (fresh) cells[ncells++] = cell[i];
  }
  zmth::Mat34 m;
  zmth::push(&m);
  zmth::load_identity();
  g_q = {out, top, probes, count};
  for (int32_t c = 0; c < ncells; ++c) {
    Partition& p = *cells[c];
    int32_t mask[kMaxProbes];
    for (int32_t i = 0; i < count; ++i) mask[i] = valid[i] && cell[i] == &p;
    for (int32_t i = 0; i < p.get<int16_t>(0x3a); ++i)
      if (tested(p.nodes[i])) ground_node(p.nodes[i], p.get<int16_t>(0x3a) + 1, mask);
    // Pirate's Moon: +0x40 is the cell's clutter records; its queries (0x004ffb40 and siblings) are still to port.
    for (int32_t i = 0; zgame::game().cell_fades && i < p.get<int32_t>(0x40); ++i) {
      uint8_t* e = p.fades + 0x44 * i;
      Node* n;
      std::memcpy(&n, e, sizeof n);  // FIX (A): a 32-bit pointer in the exe
      if (!(n->flags() & kActive) || !(n->flags() & kFade)) continue;
      std::memcpy(n->raw + 0x64, e + 0x28, 12);
      if (n->cls() == 5) {
        auto* o = static_cast<Object3d*>(n->data);
        o->put<uint32_t>(0, o->get<uint32_t>(0) & ~8u);
        std::memcpy(o->raw + 0x60, e + 4, 48);
        std::memcpy(o->raw + 0x30, e + 4, 48);
      }
      n->raw[0x30] = e[0x40];
      ground_node(n, p.get<int32_t>(0x40) + 1, mask);
    }
  }
  for (int32_t i = 0; i < count; ++i) {
    if (!wrapped[i]) continue;
    probes[3 * i] = probes[3 * i] - shift[i][0];
    probes[3 * i + 2] = probes[3 * i + 2] - shift[i][1];
  }
  for (int32_t i = 0; i < world->child_count(); ++i) {
    Node* n = world->children[i];
    if (!tested(n)) continue;
    if ((n->flags() & kZoned) && !zmod::zone_visible(n->raw[0x30])) continue;
    ground_node(n, world->child_count() + 1, valid);
  }
  zmth::pop();
  return 0;
}

namespace {
// The shot queries' segment (0x0060b978, 0x0060b984), its box (0x0060b990,
// min xyz then max xyz; both moved into a wrapped cell while it is walked)
// and the record (0x0060bab0; the next slot is its count's).
struct LineQuery {
  float a[3] = {}, b[3] = {};
  float box[6] = {};
  LineHits* out = nullptr;
};
LineQuery g_lq;

constexpr uint32_t kIntersect = 0x10, kBoxHit = 0x20;

bool line_done() { return g_opt.first_hit && g_lq.out->count != 0; }
bool line_static_ok(const Node* n) { return !g_opt.static_only || n->get<uint32_t>(0xc4) <= frames().frame; }
int line_result() { return g_lq.out->count == 0; }

// The node itself as the hit (INTERSECT_BBOX, its box crossed).
void line_take(Node* node) {
  LineHits& r = *g_lq.out;
  r.hit[r.count].node = node;
  ++r.count;
}

// The node's model crossed: its slot filled, the node recorded.
void line_model(Node* node) {
  LineHits& r = *g_lq.out;
  if (!node->model || !(g_opt.since < node->get<int32_t>(0xc4))) return;
  if (r.count >= 32) return;  // FIX (A): the exe's record has no room past 32
  if (zmod::model_segment(node->model, &r.hit[r.count], g_lq.a, g_lq.b)) line_take(node);
}

int line_node(Node* node, int32_t siblings);

void line_children(Node* node, bool filtered) {
  if (line_done()) return;
  for (int32_t i = 0; i < node->child_count(); ++i) {
    Node* c = node->children[i];
    if (filtered && !((c->flags() & kActive) && (c->flags() & kIntersect) && line_static_ok(c))) continue;
    line_node(c, node->child_count());
    if (line_done()) break;
  }
}

// Mech3.exe 1.2 @ 0x004f1470. 0 when the segment's box meets the node's
// (through its matrices, 0x004ec6b0, or with INTERSECT_BBOX its corners'
// extent, 0x004ebdb0) and, with INTERSECT_BBOX, a face of the box is
// crossed (0x0054d2f0, into the record's next slot).
int line_box(Node* node) {
  if (!(node->flags() & kHasBox)) return 1;
  float lo[3], hi[3];
  float c[8][3];
  if (!(node->flags() & kBoxHit)) {
    float b[6];
    node_box_extent(node, b);
    lo[0] = b[0], lo[1] = b[1], lo[2] = b[2], hi[0] = b[3], hi[1] = b[4], hi[2] = b[5];
  } else {
    node_box_corners(node, c);
    for (int k = 0; k < 3; ++k) lo[k] = hi[k] = c[0][k];
    for (int i = 1; i < 8; ++i)
      for (int k = 0; k < 3; ++k) {
        if (!(lo[k] <= c[i][k])) lo[k] = c[i][k];
        else if (hi[k] < c[i][k]) hi[k] = c[i][k];
      }
  }
  const float* q = g_lq.box;
  if (q[3] <= lo[0] || hi[0] <= q[0] || q[4] <= lo[1] || hi[1] <= q[1] || q[5] <= lo[2] || hi[2] <= q[2]) return 1;
  if (node->flags() & kBoxHit) {
    LineHits& r = *g_lq.out;
    if (!zmod::box_segment(c, &r.hit[r.count], g_lq.a, g_lq.b)) return 1;
  }
  return 0;
}

// The box gate of the walk's classes: 0 to go on, else the result.
int line_gate(Node* node, int32_t siblings, bool* taken) {
  *taken = false;
  if (siblings > 1 || (node->flags() & kBoxHit)) {
    if (int r = line_box(node)) return r;
    if (node->flags() & kBoxHit) {
      line_take(node);
      *taken = true;
    }
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004ef980 (class 8): the box first, then its matrix.
int line_animate(Node* node, int32_t siblings) {
  bool taken;
  if (int r = line_gate(node, siblings, &taken)) return r;
  if (taken) return 0;
  auto* d = static_cast<uint8_t*>(node->data);
  const bool pushed = node->flags() & kActive;
  if (pushed) {
    zmth::push_copy(mat(d, 0x38));
    zmth::multiply(*mat(d, 8), 1);
  }
  line_model(node);
  line_children(node, false);
  if (pushed) zmth::pop();
  return line_result();
}

// Mech3.exe 1.2 @ 0x004efaa0 (class 1, a camera): no box test.
int line_camera(Node* node) {
  auto* d = static_cast<uint8_t*>(node->data);
  const float scale[3] = {1.0f, 1.0f, 1.0f};
  const bool pushed = node->flags() & kActive;
  if (pushed) {
    zmth::push_copy(mat(d, 0x44));
    zmth::euler(reinterpret_cast<float*>(d + 0x20), reinterpret_cast<float*>(d + 0x14), scale);
  }
  line_model(node);
  line_children(node, false);
  if (pushed) zmth::pop();
  return line_result();
}

// Mech3.exe 1.2 @ 0x004efba0 (class 9, a light)
int line_light(Node* node, int32_t siblings) {
  bool taken;
  if (int r = line_gate(node, siblings, &taken)) return r;
  if (taken) return 0;
  auto* d = static_cast<uint8_t*>(node->data);
  zmth::push_copy(mat(d, 0x30));
  zmth::translate(at<float>(d, 0xc), at<float>(d, 0x10), at<float>(d, 0x14));
  zmth::rotate_y(at<float>(d, 4));
  zmth::rotate_x(at<float>(d, 0));
  zmth::rotate_z(at<float>(d, 8));
  line_model(node);
  line_children(node, false);
  zmth::pop();
  return line_result();
}

// Mech3.exe 1.2 @ 0x004ef570. Class 10 returns the node's address in the
// exe (non-zero); 1 here. No caller reads the result but the sequence's.
int line_node(Node* node, int32_t siblings) {
  const uint32_t flags = node->flags();
  if (!(flags & kActive) || !(flags & kIntersect)) return 1;
  if (g_opt.exclude & flags) return 1;
  if (line_done()) return 0;
  if (!zmod::zone_visible(node->raw[0x30])) return 1;
  if (g_opt.static_only && frames().frame < node->get<uint32_t>(0xc4)) return 1;
  node->set_flags(node->flags() & ~0x2000000u);
  if (g_lq.out->count > 0x1f) {
    zerr::report(0x200, kFile, 0xa1c, "Database intersections array is full");
    return 1;
  }
  auto* d = static_cast<uint8_t*>(node->data);
  bool taken;
  switch (node->cls()) {
    case 1: return line_camera(node);
    case 5: {
      if (int r = line_gate(node, siblings, &taken)) return r;
      if (taken) return 0;
      auto* o = static_cast<Object3d*>(node->data);
      uint32_t of = o->get<uint32_t>(0);
      bool pushed = false;
      if (!(of & 8)) {
        pushed = true;
        if (!(node->flags() & 0x80000)) {
          zmth::push_copy(mat(d, 0x60));
          zmth::multiply(*mat(d, 0x30), 1);
        } else if (!(of & 0x20)) {
          zmth::push(mat(d, 0x60));
        } else {
          zmth::push_copy(mat(d, 0x60));
          zmth::multiply(*mat(d, 0x30), 1);
          of = o->get<uint32_t>(0);
          if (!(of & 1)) o->put<uint32_t>(0, of & ~0x20u);
        }
      }
      line_model(node);
      line_children(node, true);
      if (pushed) zmth::pop();
      return line_result();
    }
    case 6:  // lod: past 5 it is not tested
      if (at<float>(d, 4) > 5.0) return 1;  // 0x005cdaf8
      if (int r = line_gate(node, siblings, &taken)) return r;
      if (taken) return 0;
      for (int32_t i = 0; i < node->child_count(); ++i) {
        line_node(node->children[i], node->child_count());
        if (line_done()) break;
      }
      return line_result();
    case 7: {  // sequence: only its current child
      if (at<int32_t>(d, 0) == 0) return 1;
      if (int r = line_gate(node, siblings, &taken)) return r;
      if (taken) return 0;
      Node* child;
      std::memcpy(&child, d + 0x20 + 8 * at<int32_t>(d, 0x14), sizeof child);  // FIX (A): a 32-bit pointer in the exe
      return line_node(child, node->child_count());
    }
    case 8: return line_animate(node, siblings);
    case 9: return line_light(node, siblings);
    case 10: return 1;
    default: {
      char msg[96];
      std::snprintf(msg, sizeof msg, "Unrecognized node class type:  node = %s class_type = %d", node->name(),
                    node->cls());
      zerr::report(0x200, kFile, 0xa4a, msg);
      return 3;
    }
  }
}

void line_shift(float dx, float dz) {
  g_lq.a[0] = dx + g_lq.a[0], g_lq.a[2] = dz + g_lq.a[2];
  g_lq.b[0] = dx + g_lq.b[0], g_lq.b[2] = dz + g_lq.b[2];
  g_lq.box[0] = dx + g_lq.box[0], g_lq.box[2] = dz + g_lq.box[2];
  g_lq.box[3] = dx + g_lq.box[3], g_lq.box[5] = dz + g_lq.box[5];
}

// Mech3.exe 1.2 @ 0x004eefe0. The cells the segment passes over, stepped
// along it in x and z (a grid walk); a cell off the grid of a wrapping world
// is the stored cell its nodes are, the segment moved there meanwhile and
// the crossings found there moved back. With first-hit on, a crossing ends
// the walk at once: in a wrapped cell its point stays moved (and the
// segment too).
void line_cells(Node* world) {
  auto* w = static_cast<World*>(world->data);
  int32_t cx = cell_of(g_lq.a[0], w->get<float>(0x34), w->get<float>(0x74));
  int32_t cz = cell_of(g_lq.a[2], w->get<float>(0x38), w->get<float>(0x78));
  const float dx = g_lq.b[0] - g_lq.a[0];
  const float dz = g_lq.b[2] - g_lq.a[2];
  int32_t step_x = 0, step_z = 0;
  float inv_x = 0, inv_z = 0;
  auto sign_differs = [](float a, float b) {
    uint32_t x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return ((x ^ y) & 0x80000000u) != 0;
  };
  if (dx != 0.0f) {
    inv_x = 1.0f / dx;
    step_x = sign_differs(dx, w->get<float>(0x74)) ? -1 : 1;
  }
  if (dz != 0.0f) {
    inv_z = 1.0f / dz;
    step_z = sign_differs(dz, w->get<float>(0x78)) ? -1 : 1;
  }
  float tx = 2.0f, tz = 2.0f;
  for (;;) {
    Partition* cell = nullptr;
    bool wrapped = false;
    float sx = 0, sz = 0;
    uint32_t had = 0;
    if (cx >= 0 && cx < w->get<int32_t>(0x88) && cz >= 0 && cz < w->get<int32_t>(0x8c)) {
      cell = &w->rows[cz][cx];
    } else if (w->get<int32_t>(0x50) != 0) {
      had = g_lq.out->count;
      int32_t wx, wz, inside;
      world_wrap_cell(world, cx, cz, &wx, &wz, &inside);
      sx = static_cast<float>(wx - cx) * w->get<float>(0x64);
      sz = static_cast<float>(wz - cz) * w->get<float>(0x68);
      wrapped = true;
      line_shift(sx, sz);
      cell = &w->rows[wz][wx];
    }
    if (cell) {
      Partition& p = *cell;
      for (int32_t i = 0; i < p.get<int16_t>(0x3a); ++i) {
        Node* n = p.nodes[i];
        if (!(n->flags() & kActive) || !(n->flags() & kIntersect) || !line_static_ok(n)) continue;
        line_node(n, p.get<int16_t>(0x3a) + 1);
        if (line_done()) return;
      }
      // Pirate's Moon: +0x40 is the cell's clutter records; its queries (0x004ffb40 and siblings) are still to port.
      for (int32_t i = 0; zgame::game().cell_fades && i < p.get<int32_t>(0x40); ++i) {
        uint8_t* e = p.fades + 0x44 * i;
        Node* n;
        std::memcpy(&n, e, sizeof n);  // FIX (A): a 32-bit pointer in the exe
        if (!(n->flags() & kActive) || !(n->flags() & kIntersect) || !line_static_ok(n)) continue;
        std::memcpy(n->raw + 0x64, e + 0x28, 12);
        if (n->cls() == 5) {
          auto* o = static_cast<Object3d*>(n->data);
          o->put<uint32_t>(0, o->get<uint32_t>(0) & ~8u);
          std::memcpy(o->raw + 0x60, e + 4, 48);
          std::memcpy(o->raw + 0x30, e + 4, 48);
        }
        n->raw[0x30] = e[0x40];
        line_node(n, p.get<int32_t>(0x40) + 1);
        if (line_done()) return;
      }
      if (wrapped) {
        line_shift(-sx, -sz);
        LineHits& r = *g_lq.out;
        for (uint32_t k = had; k < r.count; ++k) {
          r.hit[k].point[0] = r.hit[k].point[0] - sx;
          r.hit[k].point[2] = r.hit[k].point[2] - sz;
        }
      }
    }
    if (step_x != 0) {
      const int32_t edge = step_x == 1 ? cx + 1 : cx;
      tx = ((static_cast<float>(edge) * w->get<float>(0x64) + w->get<float>(0x34)) - g_lq.a[0]) * inv_x;
    }
    if (step_z != 0) {
      const int32_t edge = step_z == 1 ? cz + 1 : cz;
      tz = ((static_cast<float>(edge) * w->get<float>(0x68) + w->get<float>(0x38)) - g_lq.a[2]) * inv_z;
    }
    // FIX (bug): a NaN end (an entity at a NaN place) never passes 1, so the
    // exe walks on forever (x87: NaN is "not past", and it steps both ways);
    // the walk stops instead. fixes.md.
    if (tx != tx || tz != tz) break;
    if (tx > 1.0f && tz > 1.0f) break;
    const bool x_first = tx <= tz, z_first = tz <= tx;
    if (x_first) cx += step_x;
    if (z_first) cz += step_z;
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x004eed90
int segment_list(Node* world, const float from[3], const float to[3], LineHits* out) {
  out->count = 0;
  if (!world) return zerr::report(0x400, kFile, 0x872, "Null node pointer."), 5;
  if (!world->data) return zerr::report(0x400, kFile, 0x873, "Null class data pointer"), 5;
  if (update_hooks().pending) update_hooks().pending(update_hooks().ctx);  // 0x004f7e50 when [0x0060bea0]
  auto* w = static_cast<World*>(world->data);
  for (int k = 0; k < 3; ++k) {
    if (to[k] <= from[k]) g_lq.box[k] = to[k], g_lq.box[3 + k] = from[k];
    else g_lq.box[k] = from[k], g_lq.box[3 + k] = to[k];
  }
  std::memcpy(g_lq.a, from, 12);
  std::memcpy(g_lq.b, to, 12);
  g_lq.out = out;
  zmth::Mat34 m;
  zmth::push(&m);
  zmth::load_identity();
  if (w->get<int32_t>(0x50) != 0 ||
      (g_lq.box[0] < w->get<float>(0x44) && w->get<float>(0x34) <= g_lq.box[3] &&
       g_lq.box[2] <= w->get<float>(0x38) && w->get<float>(0x48) < g_lq.box[5]))
    line_cells(world);
  if (!line_done()) {
    for (int32_t i = 0; i < world->child_count(); ++i) {
      Node* n = world->children[i];
      if (!(n->flags() & kActive) || !(n->flags() & kIntersect)) continue;
      if (!zmod::zone_visible(n->raw[0x30])) continue;
      if (g_opt.static_only && !(n->get<uint32_t>(0xc4) <= frames().frame)) continue;
      line_node(n, world->child_count() + 1);
      if (line_done()) break;
    }
  }
  zmth::pop();
  g_opt.exclude = 0;
  return out->count == 0;
}

// Mech3.exe 1.2 @ 0x004eece0. With one crossing its distance is not
// written.
int segment_nearest(Node* world, const float from[3], const float to[3], LineHits* out) {
  if (segment_list(world, from, to, out)) return 1;
  out->total = out->count;
  if (out->count < 2) {
    out->count = 0;
    return 0;
  }
  const zmth::Vec3 f{from[0], from[1], from[2]};
  auto dist2 = [&](int k) {
    const LineHit& h = out->hit[k];
    return zmth::distance_sq(f, zmth::Vec3{h.point[0], h.point[1], h.point[2]});
  };
  float best = dist2(0);
  out->hit[0].dist2 = best;
  uint8_t pick = 0;
  for (uint8_t k = 1; k < out->total; ++k) {
    const float d = dist2(k);
    out->hit[k].dist2 = d;
    if (d < best) best = d, pick = k;
  }
  out->count = pick;
  return 0;
}

namespace {
// The sphere query in progress (0x0060b950..0x0060b964).
struct SphereQuery {
  const char* prefix = nullptr;   // 0x0060b950
  const float* centre = nullptr;  // 0x0060b954
  float radius2 = 0;              // 0x0060b958
  int32_t ranged = 0;             // 0x0060b95c
  Node* sight_world = nullptr;    // 0x0060b960
  LineHits* out = nullptr;        // 0x0060b964
};
SphereQuery g_spq;

bool name_matches(const Node* n) {
  return !g_spq.prefix || std::strncmp(n->name(), g_spq.prefix, std::strlen(g_spq.prefix)) == 0;
}

// The node's box (0x004ebd50) as its eight corners (0x004f0f00), through
// its world matrix (0x004ed280, 0x005227a0), as a sphere (0x004f9c40),
// tested and recorded; 0 recorded, else why not.
int sphere_leaf(Node* node) {
  const float* b = reinterpret_cast<const float*>(node->raw + 0x74);
  float c[8][3] = {{b[0], b[1], b[5]}, {b[3], b[1], b[5]}, {b[3], b[1], b[2]}, {b[0], b[1], b[2]},
                   {b[0], b[4], b[5]}, {b[3], b[4], b[5]}, {b[3], b[4], b[2]}, {b[0], b[4], b[2]}};
  zmth::Mat34 buf;
  zmth::push(&buf);
  zmth::load_identity();
  if (int r = node_to_world(node, 1)) {
    zmth::pop();
    return r;
  }
  zmth::transform_points(reinterpret_cast<zmth::Vec3*>(c), 8);
  zmth::pop();
  float centre[3], radius;
  corners_sphere(c, centre, &radius);
  float d2 = 0.0f;
  if (g_spq.ranged) {
    const float d = zmth::distance(zmth::Vec3{g_spq.centre[0], g_spq.centre[1], g_spq.centre[2]},
                                   zmth::Vec3{centre[0], centre[1], centre[2]}) -
                    radius;
    d2 = 0.0f <= d ? d * d : 0.0f;
    if (g_spq.radius2 < d2) return 1;
  }
  if (g_spq.sight_world && (node->flags() & 0x400000)) {
    static LineHits seen;  // the exe's stack buffer
    g_opt.first_hit = 1;   // 0x004ed9c0
    g_opt.exclude = 0x40000;  // 0x004eda00
    node_set_intersect(node, 0);
    const int none = segment_list(g_spq.sight_world, g_spq.centre, centre, &seen);
    node_set_intersect(node, 1);
    g_opt.first_hit = 0;
    if (none == 0 && seen.count != 0) return 1;
  }
  LineHits& r = *g_spq.out;
  LineHit& h = r.hit[r.count];
  h.node = node;
  std::memcpy(h.point, centre, 12);
  h.material = nullptr;
  h.dist2 = d2;
  ++r.count;
  return 0;
}

// Mech3.exe 1.2 @ 0x004f0f90
int sphere_node(Node* node) {
  if (g_spq.out->count > 0x1f) {
    zerr::report(0x200, kFile, 0x10f5, "Database intersections array is full");
    return 1;
  }
  if (!(node->flags() & kActive)) return 1;
  if (!zmod::zone_visible(node->raw[0x30])) return 1;
  if (!name_matches(node)) return 1;
  if (!(node->flags() & 0x40)) {
    int r = 1;
    for (int32_t i = 0; i < node->child_count(); ++i)
      if (sphere_node(node->children[i]) == 0) r = 0;
    return r;
  }
  if (!(node->flags() & kHasBox)) return 1;
  return sphere_leaf(node);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004f0ac0
int sphere_query(Node* world, const float centre[3], float radius, const char* prefix, int32_t ranged,
                 int32_t sighted, LineHits* out) {
  if (!world) return zerr::report(0x400, kFile, 0x108c, "Null node pointer."), 5;
  if (!world->data) return zerr::report(0x400, kFile, 0x108d, "Null class data pointer"), 5;
  if (update_hooks().pending) update_hooks().pending(update_hooks().ctx);  // 0x004f7e50 when [0x0060bea0]
  out->count = 0;
  int32_t x0, z0, x1, z1;
  if (int r = world_cell_clamped(world, &x0, centre[0] - radius, radius + centre[2], &z0)) return r;
  if (int r = world_cell_clamped(world, &x1, radius + centre[0], centre[2] - radius, &z1)) return r;
  g_spq = {prefix, centre, radius * radius, ranged, sighted ? world : nullptr, out};
  auto* w = static_cast<World*>(world->data);
  for (int32_t z = z0; z <= z1; ++z) {
    for (int32_t x = x0; x <= x1; ++x) {
      Partition& p = w->rows[z][x];
      for (int32_t i = 0; i < p.get<int16_t>(0x3a); ++i) {
        Node* n = p.nodes[i];
        if (out->count >= 0x20) {
          zerr::report(0x200, kFile, 0x10f5, "Database intersections array is full");
          continue;
        }
        if (!(n->flags() & kActive) || !zmod::zone_visible(n->raw[0x30])) continue;
        if (!name_matches(n)) continue;
        if (!(n->flags() & 0x40)) {
          for (int32_t k = 0; k < n->child_count(); ++k) sphere_node(n->children[k]);
        } else if (n->flags() & kHasBox) {
          sphere_leaf(n);
        }
      }
    }
  }
  for (int32_t i = 0; i < world->child_count(); ++i) sphere_node(world->children[i]);
  return out->count == 0;
}

}  // namespace zcls
