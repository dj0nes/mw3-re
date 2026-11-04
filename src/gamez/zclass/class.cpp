#include "gamez/zclass/class.h"

#include <cstdio>
#include <cstring>

#pragma STDC FP_CONTRACT OFF

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/sound.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zgame/zgame_table.h"
#include "gamez/zmodel/gmod_const.h"

namespace zcls {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zClass\\Class.c";
UpdateHooks g_hooks;

const float kOnes[3] = {1.0f, 1.0f, 1.0f};  // 0x005e9e20
const float kZeros[3] = {};                 // 0x0060b940

zmth::Mat34* mat(void* p) { return static_cast<zmth::Mat34*>(p); }

// 0x004ebf72..: local * top. The x87 sums the 3x3 terms as (0 + 2) + 1 and
// the translation in order.
void product(const float* l, const float* t, float* r) {
  for (int row = 0; row < 3; ++row) {
    const float* a = l + 3 * row;
    for (int c = 0; c < 3; ++c) {
      const float p0 = a[0] * t[c], p1 = a[1] * t[3 + c], p2 = a[2] * t[6 + c];
      r[3 * row + c] = (p0 + p2) + p1;
    }
  }
  for (int c = 0; c < 3; ++c) {
    const float p0 = l[9] * t[c], p1 = l[10] * t[3 + c], p2 = l[11] * t[6 + c];
    r[9 + c] = ((p0 + p1) + p2) + t[9 + c];
  }
}

void box_through(const float* m, const float* box, float out[8][3]) { zmth::box_corners(m, box, out); }
}  // namespace

UpdateHooks& update_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x004ed280 (gwNodeBuildNodeToAncestorMatrix). A tree-valid
// object3d with a clean cache loads its cached world matrix (+0x60). Else the
// single-parent chain is applied root first; the first changed node on it
// (list 7) is brought up to date before.
int node_to_world(Node* node, int mode) {
  if (!node) return zerr::report(0x400, kFile, 0xef1, "Null node pointer."), 5;
  if (node->cls() == 5 && (node->flags() & 0x80000)) {
    auto* d = static_cast<Object3d*>(node->data);
    if (!(d->get<uint32_t>(0) & 0x20)) {
      zmth::load(*mat(d->floats(0x60)));
      return 0;
    }
  }
  Node* chain[64];  // the exe's stack array holds 13; no bound check there
  int n = 1;
  chain[0] = node;
  Node* p = node;
  if (p->parent_count() > 1) {
    char msg[160];
    std::snprintf(msg, sizeof msg, "node has multiple parents; count = %d.\n  node = %s class_type = %d\n",
                  p->parent_count(), p->name(), p->cls());
    return zerr::report(0x800, kFile, 0xf0b, msg), 1;
  }
  while (p->parent_count() == 1 && (p = p->parents[0]) != nullptr) {
    chain[n++] = p;
    if (p->parent_count() > 1) {
      char msg[160];
      std::snprintf(msg, sizeof msg, "node has multiple parents; count = %d.\n  node = %s class_type = %d\n",
                    p->parent_count(), p->name(), p->cls());
      return zerr::report(0x800, kFile, 0xf0b, msg), 1;
    }
  }
  for (int i = 0; i < n; ++i) {
    if (chain[i]->cls() != 2 && (chain[i]->flags() & 1)) {
      if (g_hooks.update_node) g_hooks.update_node(g_hooks.ctx, chain[i]);
      break;
    }
  }
  for (int i = n - 1; i >= 0; --i) {
    Node* q = chain[i];
    const uint32_t flags = q->flags() & ~0x2000000u;
    q->set_flags(flags);
    switch (q->cls()) {
      case 1: {
        auto* d = static_cast<Camera*>(q->data);
        if (d->get<uint32_t>(0x10) & 2)
          zmth::multiply(*mat(d->floats(0x80)), 1);
        else
          zmth::euler(d->floats(0x20), d->floats(0x14), kOnes);
        break;
      }
      case 2:
      case 6:
        break;
      case 5: {
        auto* d = static_cast<Object3d*>(q->data);
        const uint32_t df = d->get<uint32_t>(0);
        if (!(df & 8)) {
          if (!(flags & 0x80000)) {
            zmth::multiply(*mat(d->floats(0x30)), mode);
          } else if (df & 0x20) {
            zmth::multiply(*mat(d->floats(0x30)), mode);
            *mat(d->floats(0x60)) = zmth::matrix_stack().matrix();
            d->put<uint32_t>(0, d->get<uint32_t>(0) & ~0x20u);
          } else {
            zmth::load(*mat(d->floats(0x60)));
          }
        } else if ((flags & 0x80000) && (df & 0x20)) {
          *mat(d->floats(0x60)) = zmth::matrix_stack().matrix();
          d->put<uint32_t>(0, df & ~0x20u);
        }
        break;
      }
      case 8: {  // animate: data +4 bit 2, its matrix at +8 (class not ported)
        auto* d = static_cast<uint8_t*>(q->data);
        if ((flags & 4) && (d[4] & 4)) zmth::multiply(*mat(d + 8), mode);
        break;
      }
      case 9: {
        auto* d = static_cast<Light*>(q->data);
        zmth::euler(d->floats(0), d->floats(0xc), kOnes);
        break;
      }
      case 10: {
        auto* d = static_cast<Sound*>(q->data);
        zmth::euler(kZeros, d->floats(0x30), kOnes);
        break;
      }
      default: {
        char msg[160];
        std::snprintf(msg, sizeof msg,
                      "gwNodeBuildNodeToAncestorMatrix(): Unrecognized node class type: node [%s] type [%d]",
                      q->name(), q->cls());
        return zerr::report(0x200, kFile, 0xf83, msg), 3;
      }
    }
  }
  return 0;
}

namespace {
// The cached world matrix of an object3d in the world tree, when still valid.
const zmth::Mat34* cached_world(Node* node) {
  if (node->cls() != 5 || !(node->flags() & 0x80000)) return nullptr;
  auto* d = static_cast<Object3d*>(node->data);
  return d->get<uint32_t>(0) & 0x20 ? nullptr : mat(d->floats(0x60));
}

// node_to_world into a matrix of its own, from the identity.
void world_matrix(Node* node, zmth::Mat34* m) {
  zmth::push(m);
  zmth::load_identity();
  node_to_world(node, 1);
  zmth::pop();
}
}  // namespace

// Mech3.exe 1.2 @ 0x004ed5d0
int node_world_euler(Node* node, float out[3]) {
  if (!node) return 1;
  if (const zmth::Mat34* c = cached_world(node)) return zmth::matrix_to_euler(*c, out), 0;
  zmth::Mat34 m;
  world_matrix(node, &m);
  zmth::matrix_to_euler(m, out);
  return 0;
}

// Mech3.exe 1.2 @ 0x004ed530
int node_world_position(Node* node, float out[3]) {
  if (!node) return 1;
  if (const zmth::Mat34* c = cached_world(node)) return std::memcpy(out, c->m + 9, 12), 0;
  out[0] = out[1] = out[2] = 0.0f;
  zmth::Mat34 m;
  world_matrix(node, &m);
  std::memcpy(out, m.m + 9, 12);
  return 0;
}

namespace {
bool zero3(const float* p) {
  const auto z = [](float v) { return !(v < 0.0f || v > 0.0f); };  // x87 "equal": NaN too
  return z(p[0]) && z(p[1]) && z(p[2]);
}
}  // namespace

// Mech3.exe 1.2 @ 0x004ec9c0 (ECX node, EDX where the box's address goes; 0,
// 1 without a box, 5 and a report for a null node).
const float* node_box(const Node* node) {
  if (!node || !(node->flags() & 0x100)) return nullptr;
  // PM's box is wherever its writer pointed (0x004fe740); MW3's always here.
  return node->pm_box ? node->pm_box : reinterpret_cast<const float*>(node->raw + 0x74);
}

// Mech3.exe 1.2 @ 0x004ed650. 1 for a null node.
int node_world_point(Node* node, float p[3]) {
  if (!node) return 1;
  if (zero3(p)) {
    node_world_position(node, p);
    return 0;
  }
  zmth::Mat34 m;
  zmth::push(&m);
  zmth::load_identity();
  node_to_world(node, 1);
  zmth::transform_points(reinterpret_cast<zmth::Vec3*>(p), 1);
  zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004ed6e0 (ECX node, EDX point, stack out). The point as
// node_world_point (zero: the node's origin), and the angles of the node's
// -z axis seen from its origin (pitch, yaw), with the pitch toward its +x as
// the roll.
int node_world_frame(Node* node, float p[3], float out[3]) {
  if (!node) return 1;
  zmth::Vec3 axes[2] = {{0.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 0.0f}};
  zmth::Mat34 m;
  zmth::push(&m);
  zmth::load_identity();
  node_to_world(node, 1);
  if (zero3(p)) std::memcpy(p, m.m + 9, 12);
  else zmth::transform_points(reinterpret_cast<zmth::Vec3*>(p), 1);
  const float at[3] = {m.m[9], m.m[10], m.m[11]};  // the node's origin, not the point
  zmth::transform_points(axes, 2);
  zmth::load_identity();
  zmth::direction_angles(at, &axes[0].x, out);
  out[2] = zmth::direction_pitch(at, &axes[1].x);
  zmth::pop();
  return 0;
}

// Mech3.exe 1.2 @ 0x004ebdb0. Box min at +0x74, max at +0x80. The local
// matrix: camera +0x80, object3d +0x30 (none with data flag 8), animate +8
// when active; worlds, lods, sequences, lights and sounds have none.
int node_box_corners(Node* node, float out[8][3]) {
  if (!node) return zerr::report(0x400, kFile, 0x9cc, "Null node pointer."), 5;
  if (!node->data) return zerr::report(0x400, kFile, 0x9cd, "Null class data pointer"), 5;
  const uint32_t flags = node->flags();
  if (!(flags & 0x100)) return 1;
  int status = 0;
  const float* local = nullptr;
  bool local_identity = false;
  auto* raw = static_cast<uint8_t*>(node->data);
  switch (node->cls()) {
    case 1: local = reinterpret_cast<float*>(raw + 0x80); break;
    case 2: case 6: case 7: local_identity = true; break;
    case 3: case 4: status = 3; break;
    case 5:
      local_identity = (static_cast<Object3d*>(node->data)->get<uint32_t>(0) >> 3) & 1;
      local = reinterpret_cast<float*>(raw + 0x30);
      break;
    case 8:
      if (!(flags & 4) || !(raw[4] & 4)) local_identity = true;
      local = reinterpret_cast<float*>(raw + 8);
      break;
    case 9: case 10: break;
    default: status = 3; break;
  }
  if (!local) local_identity = true;
  zmth::MatrixStack& s = zmth::matrix_stack();
  const bool top_identity = s.slot[s.top] ? s.is_identity() != 0 : true;
  float box[6];
  // PM's goes through the node's box pointer (0x004fda40, its +0x70: pm_box).
  std::memcpy(box, node->pm_box ? node->pm_box : reinterpret_cast<const float*>(node->raw + 0x74), sizeof box);
  if (top_identity) {
    if (local_identity) {
      static const int kPick[8][3] = {{0, 1, 5}, {3, 1, 5}, {3, 1, 2}, {0, 1, 2},
                                      {0, 4, 5}, {3, 4, 5}, {3, 4, 2}, {0, 4, 2}};
      for (int i = 0; i < 8; ++i)
        for (int c = 0; c < 3; ++c) out[i][c] = box[kPick[i][c]];
      return status;
    }
    box_through(local, box, out);
    return status;
  }
  const float* top = s.matrix().m;
  if (local_identity) {
    box_through(top, box, out);
    return status;
  }
  float m[12];
  product(local, top, m);
  box_through(m, box, out);
  return status;
}

// Mech3.exe 1.2 @ 0x004ec150. 0x004ebdb0 in x and z only; the combined
// matrix fills just the entries the corner transform reads.
int node_box_xz(Node* node, float out[8][2]) {
  if (!node) return zerr::report(0x400, kFile, 0xa71, "Null node pointer."), 5;
  if (!node->data) return zerr::report(0x400, kFile, 0xa72, "Null class data pointer"), 5;
  const uint32_t flags = node->flags();
  if (!(flags & 0x100)) return 1;
  int status = 0;
  const float* local = nullptr;
  bool local_identity = false;
  auto* raw = static_cast<uint8_t*>(node->data);
  switch (node->cls()) {
    case 1: local = reinterpret_cast<float*>(raw + 0x80); break;
    case 2: case 6: case 7: local_identity = true; break;
    case 5:
      local_identity = (static_cast<Object3d*>(node->data)->get<uint32_t>(0) >> 3) & 1;
      local = reinterpret_cast<float*>(raw + 0x30);
      break;
    case 8:
      if (!(flags & 4) || !(raw[4] & 4)) local_identity = true;
      local = reinterpret_cast<float*>(raw + 8);
      break;
    case 9: case 10: break;
    default: status = 3; break;
  }
  if (!local) local_identity = true;
  zmth::MatrixStack& s = zmth::matrix_stack();
  const bool top_identity = s.slot[s.top] ? s.is_identity() != 0 : true;
  float box[6];
  std::memcpy(box, node->raw + 0x74, sizeof box);
  if (top_identity && local_identity) {
    static const int kPick[8][2] = {{0, 5}, {3, 5}, {3, 2}, {0, 2}, {0, 5}, {3, 5}, {3, 2}, {0, 2}};
    for (int i = 0; i < 8; ++i) out[i][0] = box[kPick[i][0]], out[i][1] = box[kPick[i][1]];
    return status;
  }
  const float* m = top_identity ? local : s.matrix().m;
  float c[12] = {};
  if (!top_identity && !local_identity) {
    const float* l = local;
    const float* t = s.matrix().m;
    c[0] = (l[1] * t[3] + l[2] * t[6]) + l[0] * t[0];
    c[3] = (l[3] * t[0] + l[4] * t[3]) + l[5] * t[6];
    c[6] = (l[6] * t[0] + l[7] * t[3]) + l[8] * t[6];
    c[2] = (l[0] * t[2] + l[2] * t[8]) + l[1] * t[5];
    c[5] = (l[5] * t[8] + l[3] * t[2]) + l[4] * t[5];
    c[8] = (l[6] * t[2] + l[7] * t[5]) + l[8] * t[8];
    c[9] = ((l[9] * t[0] + l[11] * t[6]) + l[10] * t[3]) + t[9];
    c[11] = ((l[9] * t[2] + l[10] * t[5]) + l[11] * t[8]) + t[11];
    m = c;
  }
  zmth::box_corners_xz(m, box, out);
  return status;
}

// Mech3.exe 1.2 @ 0x004ec430. Picks its matrix as 0x004ec150 does; the
// combined one's [8] sums in another order.
int node_box_extent_xz(Node* node, float out[6]) {
  if (!node) return zerr::report(0x400, kFile, 0xb90, "Null node pointer."), 5;
  if (!node->data) return zerr::report(0x400, kFile, 0xb91, "Null class data pointer"), 5;
  const uint32_t flags = node->flags();
  if (!(flags & 0x100)) return 1;
  int status = 0;
  const float* local = nullptr;
  bool local_identity = false;
  auto* raw = static_cast<uint8_t*>(node->data);
  switch (node->cls()) {
    case 1: local = reinterpret_cast<float*>(raw + 0x80); break;
    case 2: case 6: case 7: local_identity = true; break;
    case 5:
      local_identity = (static_cast<Object3d*>(node->data)->get<uint32_t>(0) >> 3) & 1;
      local = reinterpret_cast<float*>(raw + 0x30);
      break;
    case 8:
      if (!(flags & 4) || !(raw[4] & 4)) local_identity = true;
      local = reinterpret_cast<float*>(raw + 8);
      break;
    case 9: case 10: break;
    default: status = 3; break;
  }
  if (!local) local_identity = true;
  zmth::MatrixStack& s = zmth::matrix_stack();
  const bool top_identity = s.slot[s.top] ? s.is_identity() != 0 : true;
  const float* box = reinterpret_cast<const float*>(node->raw + 0x74);
  if (top_identity && local_identity) {
    std::memcpy(&out[0], box + 0, 4);
    std::memcpy(&out[3], box + 3, 4);
    std::memcpy(&out[2], box + 2, 4);
    std::memcpy(&out[5], box + 5, 4);
    return status;
  }
  const float* m = top_identity ? local : s.matrix().m;
  float c[12] = {};
  if (!top_identity && !local_identity) {
    const float* l = local;
    const float* t = s.matrix().m;
    c[0] = (l[1] * t[3] + l[2] * t[6]) + l[0] * t[0];
    c[3] = (l[3] * t[0] + l[4] * t[3]) + l[5] * t[6];
    c[6] = (l[6] * t[0] + l[7] * t[3]) + l[8] * t[6];
    c[2] = (l[0] * t[2] + l[2] * t[8]) + l[1] * t[5];
    c[5] = (l[5] * t[8] + l[3] * t[2]) + l[4] * t[5];
    c[8] = (l[7] * t[5] + l[8] * t[8]) + l[6] * t[2];
    c[9] = ((l[9] * t[0] + l[11] * t[6]) + l[10] * t[3]) + t[9];
    c[11] = ((l[9] * t[2] + l[10] * t[5]) + l[11] * t[8]) + t[11];
    m = c;
  }
  float b[6];
  std::memcpy(b, box, sizeof b);
  zmth::transform_box_xz(m, b, out);
  return status;
}

// Mech3.exe 1.2 @ 0x004ec6b0. 0x004ec430 in three axes, with its own sum
// orders for the combined matrix.
int node_box_extent(Node* node, float out[6]) {
  if (!node) return zerr::report(0x400, kFile, 0xc28, "Null node pointer."), 5;
  if (!node->data) return zerr::report(0x400, kFile, 0xc29, "Null class data pointer"), 5;
  const uint32_t flags = node->flags();
  if (!(flags & 0x100)) return 1;
  int status = 0;
  const float* local = nullptr;
  bool local_identity = false;
  auto* raw = static_cast<uint8_t*>(node->data);
  switch (node->cls()) {
    case 1: local = reinterpret_cast<float*>(raw + 0x80); break;
    case 2: case 6: case 7: local_identity = true; break;
    case 5:
      local_identity = (static_cast<Object3d*>(node->data)->get<uint32_t>(0) >> 3) & 1;
      local = reinterpret_cast<float*>(raw + 0x30);
      break;
    case 8:
      if (!(flags & 4) || !(raw[4] & 4)) local_identity = true;
      local = reinterpret_cast<float*>(raw + 8);
      break;
    case 9: case 10: break;
    default: status = 3; break;
  }
  if (!local) local_identity = true;
  zmth::MatrixStack& s = zmth::matrix_stack();
  const bool top_identity = s.slot[s.top] ? s.is_identity() != 0 : true;
  const float* box = reinterpret_cast<const float*>(node->raw + 0x74);
  if (top_identity && local_identity) {
    std::memcpy(out, box, 24);
    return status;
  }
  const float* m = top_identity ? local : s.matrix().m;
  float c[12] = {};
  if (!top_identity && !local_identity) {
    const float* l = local;
    const float* t = s.matrix().m;
    c[0] = (t[6] * l[2] + t[3] * l[1]) + l[0] * t[0];
    c[1] = (l[0] * t[1] + l[1] * t[4]) + l[2] * t[7];
    c[2] = (l[0] * t[2] + t[8] * l[2]) + t[5] * l[1];
    c[3] = (t[0] * l[3] + l[4] * t[3]) + l[5] * t[6];
    c[4] = (l[5] * t[7] + l[4] * t[4]) + t[1] * l[3];
    c[5] = (t[2] * l[3] + t[5] * l[4]) + t[8] * l[5];
    c[6] = (t[0] * l[6] + t[6] * l[8]) + t[3] * l[7];
    c[7] = (t[4] * l[7] + t[7] * l[8]) + t[1] * l[6];
    c[8] = (t[2] * l[6] + t[8] * l[8]) + t[5] * l[7];
    c[9] = ((t[0] * l[9] + l[10] * t[3]) + l[11] * t[6]) + t[9];
    c[10] = ((l[10] * t[4] + l[11] * t[7]) + l[9] * t[1]) + t[10];
    c[11] = ((t[2] * l[9] + t[5] * l[10]) + t[8] * l[11]) + t[11];
    m = c;
  }
  float b[6];
  std::memcpy(b, box, sizeof b);
  zmth::transform_box(m, b, out);
  return status;
}

namespace {
// The local matrix a node's box goes through (the pick of 0x004f2a40,
// inlined in 0x004ecc00 and 0x004ecf70): none for an object3d still at the
// identity (data flag 8), a stopped animate, or a class without one.
const float* local_matrix(Node* n) {
  auto* d = static_cast<uint8_t*>(n->data);
  uint32_t f0, f4;
  std::memcpy(&f0, d, 4);
  std::memcpy(&f4, d + 4, 4);
  switch (n->cls()) {
    case 5: return f0 & 8 ? nullptr : reinterpret_cast<const float*>(d + 0x30);
    case 1: return reinterpret_cast<const float*>(d + 0x80);
    case 8: return (n->flags() & 4) && (f4 & 4) ? reinterpret_cast<const float*>(d + 8) : nullptr;
    default: return nullptr;
  }
}

// The node's box (+0x74) in its parent's frame; a node without data leaves
// `out` as it was (the exe's stack locals).
void parent_box(Node* n, float out[6]) {
  if (!n->data || !(n->flags() & 0x100)) return;
  if (const float* m = local_matrix(n)) zmth::transform_box(m, reinterpret_cast<const float*>(n->raw + 0x74), out);
  else std::memcpy(out, n->raw + 0x74, 24);
}

// A parent marked changed for its own boxes (+0x2c bit 2), held on list 7.
void mark_parent(Node* p) {
  p->put<uint32_t>(0x2c, p->get<uint32_t>(0x2c) | 2);
  if (!(p->flags() & 1)) {
    push_back(kChanged, p);
    p->set_flags(p->flags() | 1);
  }
  p->set_flags(p->flags() | 2);
  if (p->changed) p->changed->removable = 0;
}

// Euler angles, position and scale into a matrix (push, identity, 0x00521670, pop).
void build(float* m, const float* rot, const float* pos, const float* scale) {
  zmth::push(reinterpret_cast<zmth::Mat34*>(m));
  zmth::load_identity();
  zmth::euler(rot, pos, scale);
  zmth::pop();
}
}  // namespace

int node_model_box(Node* node) { return zgame::game().zcls_node_model_box(node); }

// Mech3.exe 1.2 @ 0x004ed220
int mw3_node_model_box(Node* node) {
  if (!node) return zerr::report(0x400, kFile, 0xebc, "Null node pointer."), 5;
  float* box = reinterpret_cast<float*>(node->raw + 0x8c);
  if (node->model) {
    zmod::model_box(node->model, box);
  } else {
    node->set_flags(node->flags() & ~0x200u);
    return 0;
  }
  node->set_flags(node->flags() | 0x200);
  return 0;
}

// Mech3.exe 1.2 @ 0x004ecf70. The children with boxes, each through its own
// matrix, merged; worlds keep none (their cells hold the boxes).
int node_children_box(Node* node) {
  if (!node) return zerr::report(0x400, kFile, 0xe5b, "Null node pointer."), 5;
  node->set_flags(node->flags() & ~0x400u);
  const int32_t n = node->child_count();
  if (n == 0 || node->cls() == 2) return 0;
  float* acc = reinterpret_cast<float*>(node->raw + 0xa4);
  int32_t i = 0;
  for (; i < n; ++i) {
    Node* c = node->children[i];
    if (!(c->flags() & 0x100)) continue;
    parent_box(c, acc);
    node->set_flags(node->flags() | 0x400);
    break;
  }
  if (!(node->flags() & 0x400)) return 0;
  float b[6] = {};  // stale from the last child in the exe
  for (++i; i < node->child_count(); ++i) {
    Node* c = node->children[i];
    if (!(c->flags() & 0x100)) continue;
    parent_box(c, b);
    if (!(b[0] >= acc[0])) acc[0] = b[0];
    if (b[3] > acc[3]) acc[3] = b[3];
    if (!(b[1] >= acc[1])) acc[1] = b[1];
    if (b[4] > acc[4]) acc[4] = b[4];
    if (!(b[2] >= acc[2])) acc[2] = b[2];
    if (b[5] > acc[5]) acc[5] = b[5];
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004ecc00. The bounds from the model box and the
// children's; then each world parent refiles the node in the cell its box
// fits now (or flags the cell it stays in), and other parents are marked
// changed. FIX (A): the exe printf()s "haha" for a node named
// "blackhawk_01" here, a debug leftover with no console to show it; dropped.
int node_bound_box(Node* node) { return zgame::game().zcls_node_bound_box(node); }

int mw3_node_bound_box(Node* node) {
  if (!node) return zerr::report(0x400, kFile, 0xda2, "Null node pointer."), 5;
  if (node->cls() == 2) return 0;
  const uint32_t flags = node->flags();
  const float* a = reinterpret_cast<const float*>(node->raw + 0x8c);
  const float* c = reinterpret_cast<const float*>(node->raw + 0xa4);
  float both[6];
  const float* src;
  if ((flags & 0x200) && (flags & 0x400)) {
    for (int k = 0; k < 3; ++k) both[k] = !(a[k] >= c[k]) ? a[k] : c[k];
    for (int k = 3; k < 6; ++k) both[k] = !(a[k] > c[k]) ? c[k] : a[k];
    src = both;
  } else if (flags & 0x200) {
    src = a;
  } else if (flags & 0x400) {
    src = c;
  } else {
    node->set_flags(flags & ~0x100u);
    return 0;
  }
  node->set_flags(flags | 0x100);
  std::memcpy(node->raw + 0x74, src, 24);
  node->put<uint32_t>(0x2c, node->get<uint32_t>(0x2c) | 1);
  bool have_box = false;
  float box[6] = {};  // stack garbage in the exe when the node has no data
  for (int32_t i = 0; i < node->parent_count(); ++i) {
    Node* p = node->parents[i];
    if (p->cls() != 2) {
      mark_parent(p);
      continue;
    }
    if (!have_box) {
      parent_box(node, box);
      have_box = true;
    }
    int32_t cx = -1, cz = -1;
    if (!(node->flags() & 0x80)) box_cell(p, &cx, box[0], box[3], box[2], box[5], &cz);
    const int32_t ox = node->get<int32_t>(0x4c), oz = node->get<int32_t>(0x50);
    if (cx == ox && cz == oz) {
      if (ox >= 0 && oz >= 0) world_touch_cell(p, ox, oz);
    } else {
      world_remove_child(p, node);
      world_link(p, node, cx, cz);
    }
  }
  return 0;
}

NodeObserver& node_update_observer() {
  static NodeObserver observer = nullptr;
  return observer;
}

// Mech3.exe 1.2 @ 0x004eca00 (gwNodeUpdate)
int node_update(Node* node) {
  if (NodeObserver o = node_update_observer()) o(node);
  int status = 0;
  bool bounds = false;
  if (node->get<uint32_t>(0x2c) & 4) node_model_box(node), bounds = true;
  if (node->get<uint32_t>(0x2c) & 2) node_children_box(node), bounds = true;
  node->put<uint32_t>(0x2c, node->get<uint32_t>(0x2c) & ~6u);
  auto* d = static_cast<uint8_t*>(node->data);
  switch (node->cls()) {
    case 1: {
      auto* cam = static_cast<Camera*>(node->data);
      const uint32_t f = cam->get<uint32_t>(0x10);
      if (!(f & 4)) break;
      if (!(f & 2)) build(cam->floats(0x80), cam->floats(0x20), cam->floats(0x14), kOnes);
      node_bound_box(node);
      bounds = false;
      cam->put<uint32_t>(0x10, cam->get<uint32_t>(0x10) & ~4u);
      break;
    }
    case 2:
      world_push_fog(node);
      break;
    case 5: {
      auto* o = static_cast<Object3d*>(node->data);
      const uint32_t f = o->get<uint32_t>(0);
      if (!(f & 1)) break;
      if (!(f & 0x10)) {
        float pos[3];
        std::memcpy(pos, o->raw + 0x54, 12);
        build(o->floats(0x30), o->floats(0x18), pos, o->floats(0x24));
      }
      node_bound_box(node);
      bounds = false;
      o->put<uint32_t>(0, o->get<uint32_t>(0) & ~1u);
      break;
    }
    case 6:
    case 7:
      break;
    case 8: {
      uint32_t f0, f4;
      std::memcpy(&f0, d, 4);
      std::memcpy(&f4, d + 4, 4);
      if (!(node->flags() & 4) || !(f4 & 4) || f0 == 0) break;
      if (f0 & 1) {
        auto* fl = reinterpret_cast<float*>(d);
        build(fl + 2, fl + 0x1c, fl + 0x1f, fl + 0x22);  // +0x08 from +0x70, +0x7c, +0x88
        node_bound_box(node);
        bounds = false;
      }
      std::memset(d, 0, 4);
      break;
    }
    case 10:
      sound_update(node);
      break;
    default: {
      char msg[160];
      std::snprintf(msg, sizeof msg, "gwNodeUpdate(): Unrecognized node class type:\n  node = %s class_type = %d\n",
                    node->name(), node->cls());
      zerr::report(0x200, kFile, 0xd6c, msg);
      status = 3;
      break;
    }
  }
  if (bounds) node_bound_box(node);
  node->set_flags(node->flags() & ~2u);
  if (node->changed) node->changed->removable = 1;
  return status;
}

}  // namespace zcls
