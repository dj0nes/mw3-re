#include "gamez/zclass/cls_world.h"
#include "gamez/zgame/zgame_table.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#pragma STDC FP_CONTRACT OFF

#include "crt/msvc_math.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zclass/list.h"
#include "gamez/zmodel/gmod_fog.h"
#include "gamez/zrender/zrndr_fog.h"

namespace zcls {

namespace {
int area_update_of_game(World* w, uint32_t* entry) { return zgame::game().zcls_area_update(w, entry); }
AreaUpdate g_area_update = area_update_of_game;

World* world_of(Node* n) { return static_cast<World*>(n->data); }
void mark(World* w, uint32_t bit) { w->put<uint32_t>(0, w->get<uint32_t>(0) | bit); }
}  // namespace

void set_area_update(AreaUpdate fn) { g_area_update = fn ? fn : area_update_of_game; }

// Mech3.exe 1.2 @ 0x004f23e0
int world_set_fog_type(Node* world, uint32_t type) {
  World* w = world_of(world);
  w->put(0x10, type);
  mark(w, kFogType);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f23f0
int world_set_fog_color(Node* world, float r, float g, float b) {
  World* w = world_of(world);
  w->put(0x14, r), w->put(0x18, g), w->put(0x1c, b);
  mark(w, kFogColor);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f2420
int world_set_fog_altitude(Node* world, float at_2c, float at_28) {
  World* w = world_of(world);
  w->put(0x28, at_28), w->put(0x2c, at_2c);
  mark(w, kFogAltitude);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f2440
int world_set_fog_range(Node* world, float near_range, float far_range) {
  World* w = world_of(world);
  w->put(0x20, near_range), w->put(0x24, far_range);
  mark(w, kFogRange);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f2460
int world_set_fog_density(Node* world, float density) {
  World* w = world_of(world);
  w->put(0x30, density);
  mark(w, kFogDensity);
  return 0;
}

// The getters copy the fog back out, in the setters' layout.
// Mech3.exe 1.2 @ 0x004f24a0
void world_get_fog_color(Node* world, float* r, float* g, float* b) {
  *r = world_of(world)->get<float>(0x14);
  *g = world_of(world)->get<float>(0x18);
  *b = world_of(world)->get<float>(0x1c);
}
// Mech3.exe 1.2 @ 0x004f24c0
void world_get_fog_range(Node* world, float* near_range, float* far_range) {
  *near_range = world_of(world)->get<float>(0x20);
  *far_range = world_of(world)->get<float>(0x24);
}
// Mech3.exe 1.2 @ 0x004f24e0
void world_get_fog_altitude(Node* world, float* at_2c, float* at_28) {
  *at_28 = world_of(world)->get<float>(0x28);
  *at_2c = world_of(world)->get<float>(0x2c);
}

// Mech3.exe 1.2 @ 0x004f1df0. With more than one world every setting is
// pushed. Fog type 0 is off, 1 linear, anything else on but not linear.
int world_push_fog(Node* world) {
  World* w = world_of(world);
  if (list_count(kWorlds) > 1) w->put<uint32_t>(0, 0x2f);
  const uint32_t changed = w->get<uint32_t>(0);
  if (changed == 0) return 0;
  bool any = false;
  if (changed & kFogType) {
    any = true;
    const uint32_t type = w->get<uint32_t>(0x10);
    if (type == 0) {
      zmod::fog_set_on(0), zmod::fog_set_linear(0), zmod::fog_set_density(0);
    } else {
      zmod::fog_set_on(1), zmod::fog_set_linear(type == 1);
    }
  }
  if (changed & kFogColor) {
    any = true;
    zrndr::set_fog_color(w->floats(0x14));
    zmod::fog_set_color(w->floats(0x14));
  }
  if (changed & kFogRange) {
    any = true;
    zmod::fog_set_near(w->get<float>(0x20)), zmod::fog_set_far(w->get<float>(0x24));
  }
  if (changed & kFogAltitude) {
    any = true;
    zmod::fog_set_alt_high(w->get<float>(0x28)), zmod::fog_set_alt_low(w->get<float>(0x2c));
  }
  if (changed & kFogDensity) {
    any = true;
    zmod::fog_set_density(w->get<float>(0x30));
  }
  if (any) zmod::fog_apply();  // after 0x005680b0, which does nothing
  if (w->get<int32_t>(4) > 0) {
    uint32_t** e = w->area_pending;
    int32_t n;
    do {
      uint32_t* entry = *e++;
      if (g_area_update) g_area_update(w, entry);
      *entry &= ~1u;
      n = w->get<int32_t>(4) - 1;
      w->put(4, n);
    } while (n > 0);
  }
  w->put<uint32_t>(0, 0);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f1f10. Left of the grid the cell counts back from the
// block's right edge (the block start +0x5c is not added there); right of
// it, forward from the start. Rows the same with +0x58 / +0x8c / +0x60.
int world_wrap_cell(Node* world, int32_t x, int32_t z, int32_t* out_x, int32_t* out_z,
                    int32_t* inside) {
  const World* w = world_of(world);
  *inside = 1;
  if (x < 0) {
    *inside = 0;
    *out_x = w->get<int32_t>(0x54) - (-1 - x) % w->get<int32_t>(0x54) - 1;
  } else if (x >= w->get<int32_t>(0x88)) {
    *inside = 0;
    *out_x = (x - w->get<int32_t>(0x88)) % w->get<int32_t>(0x54) + w->get<int32_t>(0x5c);
  } else {
    *out_x = x;
  }
  if (z < 0) {
    *inside = 0;
    *out_z = w->get<int32_t>(0x58) - (-1 - z) % w->get<int32_t>(0x58) - 1;
  } else if (z >= w->get<int32_t>(0x8c)) {
    *inside = 0;
    *out_z = (z - w->get<int32_t>(0x8c)) % w->get<int32_t>(0x58) + w->get<int32_t>(0x60);
  } else {
    *out_z = z;
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004f1fe0. Grid origin +0x34 / +0x38, inverse cell size
// +0x74 / +0x78 (z runs negative).
int world_cell_wrapped(Node* world, int32_t* cell_x, float x, float z, int32_t* cell_z,
                       int32_t* out_x, int32_t* out_z, int32_t* inside) {
  const World* w = world_of(world);
  const float dz = z - w->get<float>(0x38);
  const float fx = (x - w->get<float>(0x34)) * w->get<float>(0x74);
  *cell_x = static_cast<int32_t>(std::floor(static_cast<double>(fx)));
  const float fz = w->get<float>(0x78) * dz;
  *cell_z = static_cast<int32_t>(std::floor(static_cast<double>(fz)));
  return world_wrap_cell(world, *cell_x, *cell_z, out_x, out_z, inside);
}

// Mech3.exe 1.2 @ 0x004f2060. x is kept 0.1 inside [+0x34, +0x44], z inside
// [+0x48, +0x38]; z at or below +0x48 + 0.1 takes the edge.
int world_cell_clamped(Node* world, int32_t* cell_x, float x, float z, int32_t* cell_z) {
  const World* w = world_of(world);
  float cx;
  if (!(static_cast<float>(w->get<float>(0x34) - -0.1) <= x)) {  // 0x005cdb18, a double
    cx = w->get<float>(0x34) - -0.1f;
  } else {
    cx = w->get<float>(0x44) - 0.1f;
    if (x < cx) cx = x;
  }
  float cz = w->get<float>(0x38) - 0.1f;
  if (!(z > cz)) {
    cz = w->get<float>(0x48) - -0.1f;
    if (!(z <= cz)) cz = z;
  }
  *cell_x = static_cast<int32_t>((cx - w->get<float>(0x34)) * w->get<float>(0x74));
  *cell_z = static_cast<int32_t>(w->get<float>(0x78) * (cz - w->get<float>(0x38)));
  return 0;
}

namespace {
const char kWorldFile[] = "C:\\zipper\\GameZMech3\\zClass\\cls_world.c";

// FIX (A): the exe reallocs these arrays by one; here a new array each time.
template <typename T>
T* grown(T* array, int32_t count, T value) {
  T* out = new T[count + 1];
  for (int32_t i = 0; i < count; ++i) out[i] = array[i];
  out[count] = value;
  delete[] array;
  return out;
}

// Removes entry `at` of an array of `count` by moving the rest down.
template <typename T>
void erase_at(T* array, int32_t count, int32_t at) {
  for (int32_t i = at; i < count - 1; ++i) array[i] = array[i + 1];
}

// 0x004f1810 (ECX world node, EDX its data, stack the partition): the
// partition joins the pending list (grown by one when full) and is flagged,
// the world is marked changed.
int area_pending_push(Node* world, World* w, Partition* p) {
  const int32_t count = w->get<int32_t>(4), cap = w->get<int32_t>(8);
  uint32_t* entry = reinterpret_cast<uint32_t*>(p->raw);
  if (count == cap) {
    auto** bigger = new uint32_t*[cap + 1];
    for (int32_t i = 0; i < count; ++i) bigger[i] = w->area_pending[i];
    delete[] w->area_pending;
    w->area_pending = bigger;
    w->put<int32_t>(8, cap + 1);
  }
  w->area_pending[count] = entry;
  w->put<int32_t>(4, count + 1);
  p->put<uint32_t>(0, p->get<uint32_t>(0) | 1);
  if ((world->flags() & 1) == 0 && push_back(kChanged, world) == 0) world->set_flags(world->flags() | 1);
  world->set_flags(world->flags() | 2);
  if (world->changed) world->changed->removable = 0;
  mark(w, 0x10);
  return 0;
}

}  // namespace

// Mech3.exe 1.2 @ 0x004f2110 (ECX world node, EDX &cell x, stack min x, max x,
// min z, max z, &cell z): the one cell a box fits in, with the world's margins (+0x80 x,
// +0x84 z), else (-1, -1). Cells count from +0x34 / +0x38 by +0x74 / +0x78.
void box_cell(Node* world, int32_t* cx, float min_x, float max_x, float min_z, float max_z, int32_t* cz) {
  const World* w = world_of(world);
  *cz = -1, *cx = -1;
  const float mx = w->get<float>(0x80), mz = w->get<float>(0x84);
  if (!(w->get<float>(0x34) - mx <= min_x)) return;
  if (!(max_x < w->get<float>(0x44) + mx)) return;
  if (!(max_z <= w->get<float>(0x38) + mz)) return;
  if (!(min_z > w->get<float>(0x48) - mz)) return;
  const float mid_x = (max_x + min_x) * 0.5f, mid_z = (max_z + min_z) * 0.5f;
  *cx = crt::msvc_ftol((mid_x - w->get<float>(0x34)) * w->get<float>(0x74));
  *cz = crt::msvc_ftol(w->get<float>(0x78) * (mid_z - w->get<float>(0x38)));
  if (*cx < 0) *cx = 0;
  else if (*cx >= w->get<int32_t>(0x88)) *cx = w->get<int32_t>(0x88) - 1;
  if (*cz < 0) *cz = 0;
  else if (*cz >= w->get<int32_t>(0x8c)) *cz = w->get<int32_t>(0x8c) - 1;
  const Partition& p = w->rows[*cz][*cx];
  const float px = p.get<float>(0x8), pz = p.get<float>(0xc);
  const float right = w->get<float>(0x64) + px, bottom = w->get<float>(0x68) + pz;
  const bool fits = (!(min_x < px) || px - min_x <= mx) && (!(max_x > right) || max_x - right <= mx) &&
                    (!(min_z < bottom) || bottom - min_z <= mz) && (max_z <= pz || max_z - pz <= mz);
  if (!fits) *cz = -1, *cx = -1;
}

// Mech3.exe 1.2 @ 0x004f28c0 (ECX world, EDX node, stack cell x, cell z): into the cell's
// node list (a full one, 0x7fff, sends it to the world's own children).
int world_link(Node* world, Node* node, int32_t x, int32_t z) {
  World* w = world_of(world);
  if (x >= 0 && z >= 0 && w->rows[z][x].get<int16_t>(0x3a) >= 0x7fff) x = z = -1;
  if (x >= 0 && z >= 0) {
    Partition& p = w->rows[z][x];
    const int16_t n = p.get<int16_t>(0x3a);
    p.nodes = grown(p.nodes, n, node);
    p.put<int16_t>(0x3a, static_cast<int16_t>(n + 1));
    node->put<int32_t>(0x50, z), node->put<int32_t>(0x4c, x);
    node->parents = grown(node->parents, node->parent_count(), world);
    node->put<int32_t>(0x54, node->parent_count() + 1);
    if (node->parent_count() > 1) node_set_tree_valid(node, 0);
    if (p.get<uint32_t>(0) & 1) return 0;
    return area_pending_push(world, w, &p);
  }
  world->children = grown(world->children, world->child_count(), node);
  world->put<int32_t>(0x5c, world->child_count() + 1);
  node->put<int32_t>(0x4c, -1), node->put<int32_t>(0x50, -1);
  node->parents = grown(node->parents, node->parent_count(), world);
  node->put<int32_t>(0x54, node->parent_count() + 1);
  if (node->parent_count() > 1) node_set_tree_valid(node, 0);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f2a40. A node with a box (flag 0x100) goes in the
// cell its world box fits; without one its box is the whole world (so no
// cell); flag 0x80 keeps it out of the cells. The box is the node's own
// through its matrix: an object3d's unless it is still the identity (data
// flag 8), a camera's (+0x80), a display's (class 8) when active and set.
int world_add_child(Node* world, Node* node) {
  const World* w = world_of(world);
  const uint32_t flags = node->flags();
  int32_t cx = -1, cz = -1;
  if ((flags & 0x80) == 0) {
    float box[6];
    if ((flags & 0x100) == 0) {
      box[0] = w->get<float>(0x34), box[1] = 0.0f, box[2] = w->get<float>(0x38);
      box[3] = w->get<float>(0x3c) + w->get<float>(0x34), box[4] = 1.0f;
      box[5] = w->get<float>(0x40) + w->get<float>(0x38);
    } else {
      const float* m = nullptr;
      auto* d = static_cast<uint8_t*>(node->data);
      uint32_t dflags = 0;
      if (d) std::memcpy(&dflags, d, 4);
      if (node->cls() == 5) {
        if ((dflags & 8) == 0) m = reinterpret_cast<const float*>(d + 0x30);
      } else if (node->cls() == 1) {
        m = reinterpret_cast<const float*>(d + 0x80);
      } else if (node->cls() == 8 && (flags & 4)) {
        uint32_t f4;
        std::memcpy(&f4, d + 4, 4);
        if (f4 & 4) m = reinterpret_cast<const float*>(d + 8);
      }
      if (m) zmth::transform_box(m, reinterpret_cast<const float*>(node->raw + 0x74), box);
      else std::memcpy(box, node->raw + 0x74, sizeof box);
    }
    box_cell(world, &cx, box[0], box[3], box[2], box[5], &cz);
  }
  return world_link(world, node, cx, cz);
}

// Mech3.exe 1.2 @ 0x004f2c30. A node in no cell is unlinked as a child; in a
// cell it leaves the cell's list and its parents (the cell is flagged for
// the area update).
int world_remove_child(Node* world, Node* node) {
  const int32_t x = node->get<int32_t>(0x4c), z = node->get<int32_t>(0x50);
  if (x == -1 && z == -1) return node_unlink(world, node);
  World* w = world_of(world);
  Partition& p = w->rows[z][x];
  const int16_t n = p.get<int16_t>(0x3a);
  int32_t at = -1;
  for (int32_t i = 0; i < n; ++i)
    if (p.nodes[i] == node) {
      at = i;
      break;
    }
  if (at < 0) {
    char msg[120];
    std::snprintf(msg, sizeof msg, "ERROR deleting child node %s from parent node %s", node->name(), world->name());
    return zerr::report(0x200, kWorldFile, 0x11ce, msg), 1;
  }
  erase_at(p.nodes, n, at);
  p.put<int16_t>(0x3a, static_cast<int16_t>(n - 1));
  node->put<int32_t>(0x4c, -1), node->put<int32_t>(0x50, -1);
  at = -1;
  for (int32_t i = 0; i < node->parent_count(); ++i)
    if (node->parents[i] == world) {
      at = i;
      break;
    }
  if (at >= 0) {
    erase_at(node->parents, node->parent_count(), at);
    node->put<int32_t>(0x54, node->parent_count() - 1);
  }
  if (p.get<uint32_t>(0) & 1) return 0;
  return area_pending_push(world, w, &p);
}

// Mech3.exe 1.2 @ 0x004f2770. With +0xb8 set the rows are not the world's
// to free; the grid is emptied either way.
int world_free_partitions(Node* world) {
  World* w = world_of(world);
  if (!w->rows) return 0;
  const bool own = w->get<int32_t>(0xb8) == 0;
  for (int32_t z = 0; z < w->get<int32_t>(0x8c); ++z) {
    for (int32_t x = 0; x < w->get<int32_t>(0x88); ++x) {
      Partition& p = w->rows[z][x];
      delete[] p.nodes;
      p.nodes = nullptr;
      delete[] p.fades;
      p.fades = nullptr;
    }
    if (own) delete[] w->rows[z];
  }
  if (own) delete[] w->rows;
  w->rows = nullptr;
  for (int off : {0x90, 0x68, 0x64, 0x8c, 0x88}) w->put<int32_t>(off, 0);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f1bd0: a world node (class 2) with no grid, fog type
// 1, unit fog factors and one virtual block (+0x54 / +0x58), on list 13.
Node* world_create() {
  Node* node = node_alloc();
  node->put<int32_t>(0x34, 2);
  auto* w = new World{};
  std::memset(w->raw, 0, sizeof w->raw);
  node->data = w;
  w->put<float>(0x94, 1.0f), w->put<float>(0x98, 1.0f), w->put<float>(0x9c, 1.0f);
  w->put<uint32_t>(0, 1);
  w->raw[0x4c] = 0x10;
  w->put<int32_t>(0x58, 1), w->put<int32_t>(0x54, 1);
  push_front(kWorlds, node);
  return node;
}

// Mech3.exe 1.2 @ 0x004f2500: the area's corner (+0x34, +0x38); its far
// corner (+0x44, +0x48) follows the size.
int world_set_origin(Node* world, float x, float z) {
  World* w = world_of(world);
  const float fx = w->get<float>(0x3c) + x, fz = w->get<float>(0x40) + z;
  w->put(0x34, x), w->put(0x38, z), w->put(0x44, fx), w->put(0x48, fz);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f2530: the area's size (+0x3c, +0x40), the far corner
// with it.
int world_set_size(Node* world, float x, float z) {
  World* w = world_of(world);
  const float fx = w->get<float>(0x34) + x, fz = w->get<float>(0x38) + z;
  w->put(0x3c, x), w->put(0x40, z), w->put(0x44, fx), w->put(0x48, fz);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f2560: the partition grid made afresh for cells of
// (x, z): the cell sizes and their factors, enough cells to cover the area
// (one more when the truncated count falls short), each cell's box from its
// corner, its sphere (0x004f9bc0), no node list yet (+0x04 -1, flag 0x100).
int world_make_grid(Node* world, float x, float z) {
  World* w = world_of(world);
  if (w->rows) world_free_partitions(world);
  w->put(0x64, x), w->put(0x68, z);
  w->put(0x80, x * 0.125f), w->put(0x84, z * -0.125f);   // [0x005cdb2c], [0x005cdb30]
  w->put(0x6c, x * 0.5f), w->put(0x70, z * 0.5f);         // [0x005cdb28]
  w->put(0x74, 1.0f / x), w->put(0x78, 1.0f / z);
  w->put(0x7c, approx_sqrt(x * x + z * z) * -0.5f);       // [0x005cdb38]
  int32_t cols = crt::msvc_ftol(w->get<float>(0x3c) / x);
  if (static_cast<float>(cols) * x < w->get<float>(0x3c)) ++cols;
  int32_t rows = crt::msvc_ftol(w->get<float>(0x40) / z);
  if (static_cast<float>(rows) * z > w->get<float>(0x40)) ++rows;
  w->put<int32_t>(0x88, cols), w->put<int32_t>(0x8c, rows);
  w->rows = new Partition*[rows > 0 ? rows : 0]();
  for (int32_t r = 0; r < rows; ++r) w->rows[r] = new Partition[cols > 0 ? cols : 0]();
  for (int32_t r = 0; r < rows; ++r) {
    for (int32_t c = 0; c < cols; ++c) {
      Partition& p = w->rows[r][c];
      p.put<uint32_t>(0, p.get<uint32_t>(0) | 0x100);
      const float x0 = static_cast<float>(c) * x + w->get<float>(0x34);
      const float z0 = static_cast<float>(r) * z + w->get<float>(0x38);
      p.put(0x08, x0), p.put(0x0c, z0);
      // The box (+0x10..+0x24): x from the corner, z from the far side.
      p.put(0x10, x0), p.put(0x24, z0), p.put(0x1c, x0 + x), p.put(0x18, z0 + z);
      float box[6], centre[3], radius;
      std::memcpy(box, p.raw + 0x10, sizeof box);
      box_sphere(box, centre, &radius);
      std::memcpy(p.raw + 0x28, centre, sizeof centre);
      p.put(0x34, radius);
      p.put<int32_t>(0x04, -1);
    }
  }
  return 0;
}

// Mech3.exe 1.2 @ 0x004f1c60
int world_delete(Node* world) {
  if (world_free_partitions(world) != 0) return 0;
  // The exe frees the light, sound and pending lists but leaves their
  // pointers and counts. A world that then stays (it still has children or
  // a model, as when 0x004eaea0 deletes it) keeps them in the port (FIX (A):
  // nothing dangles) until its slot is freed.
  World* w = world_of(world);
  if (world->child_count() == 0 && world->parent_count() == 0 && !node_model_header(world)) {
    delete[] w->lights, delete[] w->light_data, delete[] w->sounds, delete[] w->sound_data;
    delete[] w->area_pending;
    w->lights = nullptr, w->light_data = nullptr, w->sounds = nullptr, w->sound_data = nullptr;
    w->area_pending = nullptr;
  }
  return node_release(world);
}

// Mech3.exe 1.2 @ 0x004f2d50. Both sides' lists, no duplicate check.
int world_add_light(Node* world, Node* light) {
  World* w = world_of(world);
  auto* l = static_cast<Light*>(light->data);
  const int32_t n = w->get<int32_t>(0xa0);
  w->lights = grown(w->lights, n, light);
  w->light_data = grown(w->light_data, n, static_cast<void*>(l));
  w->put<int32_t>(0xa0, n + 1);
  l->nodes = grown(l->nodes, l->get<int32_t>(0xc8), world);
  l->put<int32_t>(0xc8, l->get<int32_t>(0xc8) + 1);
  return 0;
}

namespace {
// 0x004f2e00 / 0x004f3040: a light or sound out of the world's lists and the
// world out of its node's. A miss is printed and reported through 0x0050da70
// (level 5, line 0x23 of zerr_old.c).
int remove_from_world(Node* world, Node* node, int32_t* count, Node** nodes, void** data, int32_t* back_count,
                      Node** back, int line, const char* what) {
  int32_t at = -1;
  for (int32_t i = 0; i < *count; ++i)
    if (nodes[i] == node) {
      at = i;
      break;
    }
  if (at == -1) {
    char msg[160];
    std::snprintf(msg, sizeof msg, "%s: Line %d: ERROR deleting %s; not found in world list.", kWorldFile, line, what);
    return zerr::report(5, "C:\\zipper\\GameZMech3\\zError\\zerr_old.c", 0x23, msg), 5;
  }
  for (int32_t i = at; i < *count - 1; ++i) nodes[i] = nodes[i + 1], data[i] = data[i + 1];
  --*count;
  at = -1;
  for (int32_t i = 0; i < *back_count; ++i)
    if (back[i] == world) {
      at = i;
      break;
    }
  if (at == -1) {
    char msg[160];
    std::snprintf(msg, sizeof msg, "%s: Line %d: ERROR deleting %s; world not found.", kWorldFile, line + 0x27, what);
    return zerr::report(5, "C:\\zipper\\GameZMech3\\zError\\zerr_old.c", 0x23, msg), 5;
  }
  erase_at(back, *back_count, at);
  --*back_count;
  return 0;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004f2e00
int world_remove_light(Node* world, Node* light) {
  World* w = world_of(world);
  auto* l = static_cast<Light*>(light->data);
  int32_t n = w->get<int32_t>(0xa0), back = l->get<int32_t>(0xc8);
  const int r = remove_from_world(world, light, &n, w->lights, w->light_data, &back, l->nodes, 0x12ac, "light");
  w->put<int32_t>(0xa0, n), l->put<int32_t>(0xc8, back);
  return r;
}

// Mech3.exe 1.2 @ 0x004f2f90
int world_add_sound(Node* world, Node* sound) {
  World* w = world_of(world);
  auto* s = static_cast<Sound*>(sound->data);
  const int32_t n = w->get<int32_t>(0xac);
  w->sounds = grown(w->sounds, n, sound);
  w->sound_data = grown(w->sound_data, n, static_cast<void*>(s));
  w->put<int32_t>(0xac, n + 1);
  s->nodes = grown(s->nodes, s->get<int32_t>(0x8c), world);
  s->put<int32_t>(0x8c, s->get<int32_t>(0x8c) + 1);
  return 0;
}

// Mech3.exe 1.2 @ 0x004f3040
int world_remove_sound(Node* world, Node* sound) {
  World* w = world_of(world);
  auto* s = static_cast<Sound*>(sound->data);
  int32_t n = w->get<int32_t>(0xac), back = s->get<int32_t>(0x8c);
  const int r = remove_from_world(world, sound, &n, w->sounds, w->sound_data, &back, s->nodes, 0x13eb, "sound");
  w->put<int32_t>(0xac, n), s->put<int32_t>(0x8c, back);
  return r;
}

// Mech3.exe 1.2 @ 0x004f2360 (ECX world, EDX cell x, stack cell z): the
// cell joins the pending list unless it is on it.
int world_touch_cell(Node* world, int32_t x, int32_t z) {
  if (!world) return zerr::report(0x400, kWorldFile, 0x8ee, "Null node pointer."), 5;
  World* w = world_of(world);
  if (!w) return zerr::report(0x400, kWorldFile, 0x8ef, "Null class data pointer"), 5;
  Partition& p = w->rows[z][x];
  if (p.get<uint32_t>(0) & 1) return 0;
  return area_pending_push(world, w, &p);
}

// Mech3.exe 1.2 @ 0x004f2b70 (ECX matrix, EDX box, stack out): the y range
// of a box through a matrix.
void box_y_range(const float* m, const float* box, float out[2]) {
  if (!(m[1] >= 0.0f)) out[0] = box[3] * m[1], out[1] = box[0] * m[1];
  else out[0] = box[0] * m[1], out[1] = box[3] * m[1];
  if (!(m[4] >= 0.0f)) out[0] = box[4] * m[4] + out[0], out[1] = box[1] * m[4] + out[1];
  else out[0] = box[1] * m[4] + out[0], out[1] = box[4] * m[4] + out[1];
  if (!(m[7] >= 0.0f)) out[0] = box[5] * m[7] + out[0], out[1] = box[2] * m[7] + out[1];
  else out[0] = box[2] * m[7] + out[0], out[1] = box[5] * m[7] + out[1];
  out[0] = out[0] + m[10];
  out[1] = out[1] + m[10];
}

namespace {
// A node's local matrix for its box, as 0x004f2a40 picks it: none for an
// object3d still at the identity (data flag 8) or a stopped animate.
const float* box_matrix(Node* n) {
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

// A node's y range into lo/hi; a node without data leaves them as they were
// (the exe's stack locals, stale from the last node).
void node_y_range(Node* n, float& lo, float& hi) {
  if (!n->data || !(n->flags() & 0x100)) return;
  if (const float* m = box_matrix(n)) {
    float r[2];
    box_y_range(m, reinterpret_cast<const float*>(n->raw + 0x74), r);
    lo = r[0], hi = r[1];
  } else {
    lo = n->get<float>(0x78), hi = n->get<float>(0x84);
  }
}

}  // namespace

// Mech3.exe 1.2 @ 0x004f18a0 (EDX the partition): the cell's height range
// (+0x14 min y, +0x20 max y) from its nodes with boxes, else its fade
// entries, then its sphere (+0x28 centre, +0x34 radius). Flag 0x100: the
// range is set.
int mw3_area_update(World*, uint32_t* entry) {
  auto* p = reinterpret_cast<Partition*>(entry);  // the pending list holds &raw, at +0
  uint32_t flags = p->get<uint32_t>(0) & ~0x100u;
  p->put<uint32_t>(0, flags);
  const int16_t n = p->get<int16_t>(0x3a);
  const int32_t nf = p->get<int32_t>(0x40);
  if (n == 0 && nf == 0) return 0;
  float lo = 0.0f, hi = 0.0f;  // uninitialised in the exe
  int32_t i = 0;
  for (; i < p->get<int16_t>(0x3a); ++i) {
    Node* node = p->nodes[i];
    if (!(node->flags() & 0x100)) continue;
    p->put<uint32_t>(0, flags | 0x100);
    node_y_range(node, lo, hi);
    p->put<float>(0x14, lo), p->put<float>(0x20, hi);
    ++i;
    break;
  }
  for (; i < p->get<int16_t>(0x3a); ++i) {
    Node* node = p->nodes[i];
    if (!(node->flags() & 0x100)) continue;
    node_y_range(node, lo, hi);
    if (!(lo >= p->get<float>(0x14))) p->put<float>(0x14, lo);
    if (hi > p->get<float>(0x20)) p->put<float>(0x20, hi);
  }
  auto fade_node = [&](int32_t k) {
    Node* f;
    std::memcpy(&f, p->fades + 0x44 * k, sizeof f);  // FIX (A): a 32-bit pointer in the exe
    return f;
  };
  auto fade_matrix = [&](int32_t k, float m[12]) { std::memcpy(m, p->fades + 0x44 * k + 4, 48); };
  i = 0;
  if (!(p->get<uint32_t>(0) & 0x100)) {
    for (; i < nf; ++i) {
      Node* f = fade_node(i);
      if (!(f->flags() & 0x100)) continue;
      p->put<uint32_t>(0, p->get<uint32_t>(0) | 0x100);
      float m[12];
      fade_matrix(i, m);
      float r[2];
      box_y_range(m, reinterpret_cast<const float*>(f->raw + 0x74), r);  // inlined there, the same sums
      lo = r[0], hi = r[1];
      p->put<float>(0x14, lo), p->put<float>(0x20, hi);
      ++i;
      break;
    }
  }
  for (; i < p->get<int32_t>(0x40); ++i) {
    Node* f = fade_node(i);
    if (!(f->flags() & 0x100)) continue;
    float m[12], r[2];
    fade_matrix(i, m);
    box_y_range(m, reinterpret_cast<const float*>(f->raw + 0x74), r);
    if (!(r[0] >= p->get<float>(0x14))) p->put<float>(0x14, r[0]);
    if (r[1] > p->get<float>(0x20)) p->put<float>(0x20, r[1]);
  }
  if (p->get<uint32_t>(0) & 0x100) {
    float c[3], r;
    box_sphere(p->floats(0x10), c, &r);
    std::memcpy(p->raw + 0x28, c, 12);
    p->put<float>(0x34, r);
  }
  return 0;
}

}  // namespace zcls
