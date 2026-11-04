#include "pm/gamez/zclass/cls_world.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"

namespace pm::zcls {

namespace {
const char* const kClassFile = "C:\\zipper\\branchep1\\gamez\\zClass\\Class.c";
const char* const kWorldFile = "C:\\zipper\\branchep1\\gamez\\zClass\\cls_world.c";

using ::zcls::Node;
using ::zcls::Partition;
using ::zcls::World;

template <typename T>
T* grown(T* array, int32_t count, T value) {
  T* bigger = new T[static_cast<size_t>(count) + 1];
  for (int32_t i = 0; i < count; ++i) bigger[i] = array[i];
  bigger[count] = value;
  delete[] array;
  return bigger;
}

World* world_of(Node* n) { return static_cast<World*>(n->data); }
int16_t floored(float v) { return static_cast<int16_t>(static_cast<int32_t>(std::floor(static_cast<double>(v)))); }

// A parent marked changed for its own boxes, held on list 7.
void mark_changed(Node* p) {
  p->put<uint32_t>(0x2c, p->get<uint32_t>(0x2c) | 2);
  if (!(p->flags() & 1)) {
    ::zcls::push_back(::zcls::kChanged, p);
    p->set_flags(p->flags() | 1);
  }
  p->set_flags(p->flags() | 2);
  if (p->changed) p->changed->removable = 0;
}

void store_range(Node* node, const int16_t range[4]) {
  node->put<int32_t>(0x4c, range[0]), node->put<int32_t>(0x50, range[1]);
  node->pm_virtual[0] = range[2], node->pm_virtual[1] = range[3];
}

void add_parent(Node* node, Node* parent) {
  node->parents = grown(node->parents, node->parent_count(), parent);
  node->put<int32_t>(0x54, node->parent_count() + 1);
  if (node->parent_count() > 1) pm::zcls::node_set_tree_valid(node, 0);
}

// The node's world box: through its local matrix, or its own.
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
}  // namespace

void node_set_tree_valid(Node* node, int on) {
  if (!node) return;
  uint32_t flags;
  if (on == 0) {
    flags = node->flags() & ~0x80000u;
  } else {
    if (node->parent_count() > 1) return;
    flags = node->flags() | 0x80000;
  }
  node->set_flags(flags);
  for (int i = 0; i < node->child_count(); ++i) pm::zcls::node_set_tree_valid(node->children[i], on);
}

void world_box_range(const World* w, const float b[6], int16_t r[4]) {
  const float margin_x = w->get<float>(0x80), margin_z = w->get<float>(0x84);
  const float x0 = w->get<float>(0x34), z0 = w->get<float>(0x38), sx = w->get<float>(0x74), sz = w->get<float>(0x78);
  const int32_t cols = w->get<int32_t>(0x88), rows = w->get<int32_t>(0x8c);
  r[0] = floored(((margin_x + b[0]) - x0) * sx);
  r[2] = floored(((b[3] - margin_x) - x0) * sx);
  r[1] = floored(((b[5] - margin_z) - z0) * sz);
  r[3] = floored(((margin_z + b[2]) - z0) * sz);
  const int16_t s2 = r[2], s3 = r[3];
  if (s2 < r[0]) r[0] = s2;
  if (s3 < r[1]) r[1] = s3;
  // Past the second area it can never be one cell: a range across it is widened to two.
  const float* area = w->pm_area2;
  if (!(area[0] <= b[0]) || !(b[3] < area[2])) {
    if (r[0] == s2) r[2] = static_cast<int16_t>(s2 + 1);
    return;
  }
  if (!(b[5] <= area[1]) || !(area[3] < b[2])) {
    if (r[1] == s3) r[3] = static_cast<int16_t>(s3 + 1);
    return;
  }
  if (r[0] < 0) r[0] = 0;
  else if (cols <= r[0]) r[0] = static_cast<int16_t>(cols - 1);
  if (r[1] < 0) r[1] = 0;
  else if (rows <= r[1]) r[1] = static_cast<int16_t>(rows - 1);
  if (s2 < 0) r[2] = 0;
  else if (cols <= s2) r[2] = static_cast<int16_t>(cols - 1);
  if (s3 < 0) r[3] = 0;
  else if (rows <= s3) r[3] = static_cast<int16_t>(rows - 1);
}

int world_touch_node(Node* world, Node* node, const float box[6]) {
  if (!world) return zerr::report(0x400, kWorldFile, 0x904, "Null node pointer."), 5;
  World* w = world_of(world);
  if (!w) return zerr::report(0x400, kWorldFile, 0x905, "Null class data pointer"), 5;
  const int32_t x = node->get<int32_t>(0x4c), z = node->get<int32_t>(0x50);
  Partition& p = w->rows[z][x];
  for (int i = 0; i < p.get<int16_t>(0x3a); ++i) {
    if (p.nodes[i] == node) {
      p.pm_y[i][0] = box[1], p.pm_y[i][1] = box[4];
      break;
    }
  }
  return ::zcls::world_touch_cell(world, x, z);
}

int node_remove_child(Node* parent, Node* child) {
  int count = parent->child_count();
  int at = 0;
  for (; at < count; ++at) {
    if (parent->children[at] == child) break;
  }
  if (at == count) {
    char msg[160];
    std::snprintf(msg, sizeof msg, "ERROR deleting child node %s from %s", child->name(), parent->name());
    return zerr::report(0x200, kClassFile, 0x99e, msg), 1;
  }
  --count;
  parent->put<int32_t>(0x5c, count);
  if (at < count) parent->children[at] = parent->children[count];
  int parents = child->parent_count();
  for (int i = 0; i < parents; ++i) {
    if (child->parents[i] == parent) {
      --parents;
      child->put<int32_t>(0x54, parents);
      if (i < parents) child->parents[i] = child->parents[parents];
      if (parents == 1 && (parent->flags() & 0x80000)) pm::zcls::node_set_tree_valid(child, 1);
      break;
    }
  }
  mark_changed(parent);
  return 0;
}

int world_remove_node(Node* world, Node* node) {
  const int32_t x = node->get<int32_t>(0x4c), z = node->get<int32_t>(0x50);
  if (x != node->pm_virtual[0] || z != node->pm_virtual[1]) return pm::zcls::node_remove_child(world, node);
  World* w = world_of(world);
  Partition& p = w->rows[z][x];
  int count = p.get<int16_t>(0x3a);
  int at = 0;
  for (; at < count; ++at) {
    if (p.nodes[at] == node) break;
  }
  if (at == count) {
    char msg[160];
    std::snprintf(msg, sizeof msg, "ERROR deleting child node %s from %s", node->name(), world->name());
    return zerr::report(0x200, kWorldFile, 0x1205, msg), 1;
  }
  --count;
  p.put<int16_t>(0x3a, static_cast<int16_t>(count));
  if (at < count) {
    p.nodes[at] = p.nodes[count];
    p.pm_y[at][0] = p.pm_y[count][0], p.pm_y[at][1] = p.pm_y[count][1];
  }
  const int16_t none[4] = {-1, -1, static_cast<int16_t>(w->get<int32_t>(0x88)), static_cast<int16_t>(w->get<int32_t>(0x8c))};
  store_range(node, none);
  int parents = node->parent_count();
  for (int i = 0; i < parents; ++i) {
    if (node->parents[i] == world) {
      --parents;
      node->put<int32_t>(0x54, parents);
      if (i < parents) node->parents[i] = node->parents[parents];
      break;
    }
  }
  return ::zcls::world_touch_cell(world, x, z);
}

int world_link_node(Node* world, Node* node, const float box[6], const int16_t range[4]) {
  World* w = world_of(world);
  if (range[0] != range[2] || range[1] != range[3]) {
    world->children = grown(world->children, world->child_count(), node);
    world->put<int32_t>(0x5c, world->child_count() + 1);
    store_range(node, range);
    add_parent(node, world);
    return 0;
  }
  const int32_t x = range[0], z = range[1];
  Partition& p = w->rows[z][x];
  const int count = p.get<int16_t>(0x3a);
  if (count > 0x7ffe) return 1;
  p.nodes = grown(p.nodes, count, node);
  auto* ys = new float[static_cast<size_t>(count) + 1][2];
  if (count) std::memcpy(ys, p.pm_y, static_cast<size_t>(count) * sizeof *ys);
  delete[] p.pm_y;
  p.pm_y = ys;
  p.pm_y[count][0] = box[1], p.pm_y[count][1] = box[4];
  p.put<int16_t>(0x3a, static_cast<int16_t>(count + 1));
  store_range(node, range);
  add_parent(node, world);
  return ::zcls::world_touch_cell(world, x, z);
}

int node_bound_box(Node* node) {
  if (!node) return zerr::report(0x400, kClassFile, 0xe59, "Null node pointer."), 5;
  if (node->cls() == 2) return 0;
  const uint32_t flags = node->flags();
  const auto* model = reinterpret_cast<const float*>(node->raw + 0x8c);
  const auto* kids = reinterpret_cast<const float*>(node->raw + 0xa4);
  auto* both = reinterpret_cast<float*>(node->raw + 0x74);
  if ((flags & 0x200) && (flags & 0x400)) {
    for (int k = 0; k < 3; ++k) both[k] = kids[k] <= model[k] ? kids[k] : model[k];
    for (int k = 3; k < 6; ++k) both[k] = model[k] <= kids[k] ? kids[k] : model[k];
    node->pm_box = both;
  } else if (flags & 0x200) {
    node->pm_box = model;
  } else if (flags & 0x400) {
    node->pm_box = kids;
  } else {
    node->set_flags(flags & ~0x100u);
    node->pm_box = both;
    return 0;
  }
  node->set_flags(flags | 0x100);
  if (node->cls() == 6) {  // The LoD's scale, on the chosen box itself: it grows at every call.
    const auto* lod = static_cast<const ::zcls::Lod*>(node->data);
    auto* box = const_cast<float*>(node->pm_box);
    for (int k = 0; k < 3; ++k) {
      float s;
      std::memcpy(&s, lod->pm_tail + 4 * k, 4);  // PM +0x34..
      if (1.0f < s) box[k] = box[k] * s, box[k + 3] = box[k + 3] * s;
    }
  }
  node->put<uint32_t>(0x2c, node->get<uint32_t>(0x2c) | 1);
  bool have_box = false;
  float box[6] = {};
  for (int i = 0; i < node->parent_count();) {
    Node* p = node->parents[i];
    ++i;
    if (p->cls() != 2) {
      mark_changed(p);
      continue;
    }
    if (!have_box) {
      if (node->data) {
        if (const float* m = local_matrix(node)) zmth::transform_box(m, node->pm_box, box);
        else std::memcpy(box, node->pm_box, 24);
      }
      have_box = true;
    }
    int16_t range[4];
    World* w = world_of(p);
    if (node->flags() & 0x80) {
      range[0] = -1, range[1] = -1;
      range[2] = static_cast<int16_t>(w->get<int32_t>(0x88)), range[3] = static_cast<int16_t>(w->get<int32_t>(0x8c));
    } else {
      world_box_range(w, box, range);
    }
    const int32_t x0 = node->get<int32_t>(0x4c), z0 = node->get<int32_t>(0x50);
    if (x0 == range[0] && z0 == range[1] && node->pm_virtual[0] == range[2] && node->pm_virtual[1] == range[3]) {
      if (x0 == node->pm_virtual[0] && z0 == node->pm_virtual[1]) world_touch_node(p, node, box);
    } else {
      world_remove_node(p, node);
      world_link_node(p, node, box, range);
    }
  }
  return 0;
}

}  // namespace pm::zcls
