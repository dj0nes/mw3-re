#include "pm/gamez/zclass/cls_mechlib.h"

#include <cstdlib>
#include <cstring>
#include <vector>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/lod.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zutil/zutl_zar.h"
#include "pm/gamez/zclass/cls_zbd.h"
#include "pm/gamez/zmodel/gmod_const.h"

// The exe's trace lines (0x00511640, to stderr: "reading node", "read node:
// %s type %s", "read model: ..." and "read complete") are left out.
namespace pm::zcls {

using ::zcls::MechLib;
using ::zcls::Node;

namespace {
// Ours (docs/modding.md, M3), as MW3's reader: an entry read with its end
// known. A count the bytes left cannot hold, a node of another class, a tree
// deeper than 64, a polygon with no material or an index past its array mark
// the read broken; the load refuses it. Without an end (-1) the reads are
// the original's.
long g_end = -1;
bool g_broken = false;
bool fits(std::FILE* f, uint64_t bytes) {
  if (g_end < 0) return true;
  if (g_broken || static_cast<uint64_t>(g_end - std::ftell(f)) < bytes || std::ftell(f) > g_end) g_broken = true;
  return !g_broken;
}
bool bounded() { return g_end >= 0; }

// malloc and an unchecked ReadFile, as the exe; zeroed, so a short read
// leaves zeros. Past a bounded entry's end, nothing is read.
template <typename T>
T* read_array(std::FILE* f, int64_t count, size_t size) {
  const size_t bytes = count > 0 ? static_cast<size_t>(count) * size : 0;
  if (bytes && !fits(f, bytes)) return static_cast<T*>(std::calloc(1, size));  // bounded: before the allocation
  T* p = static_cast<T*>(std::calloc(bytes ? bytes : 1, 1));
  if (bytes) std::fread(p, bytes, 1, f);
  return p;
}

uint32_t u32_at(const uint8_t* p, int off) {
  uint32_t v;
  std::memcpy(&v, p + off, 4);
  return v;
}

// Ours: every polygon's library indices within the library's list.
bool materials_fit(const MechLib& lib, const Node* node) {
  if (!node) return true;
  if (const pm::zmod::Model* m = node->pm_model)
    for (int32_t i = 0; i < m->count(0x10); ++i) {
      const pm::zmod::Polygon& q = m->polygons[i];
      for (int32_t j = 0; j < q.material_count; ++j)
        if (static_cast<uint32_t>(q.library_indices[j]) >= lib.materials.size()) return false;
    }
  for (int32_t i = 0; i < node->child_count(); ++i)
    if (node->children && !materials_fit(lib, node->children[i])) return false;
  return true;
}
}  // namespace

// Pirate's Moon 1.0 @ 0x00510910
bool mechlib_open(MechLib& lib, const char* path) {
  const bool ok = zutl_zar::open(lib.archive, path);
  uint32_t version = 0, format = 0, size = 4, size2 = 4;
  zutl_zar::read(lib.archive, zutl_zar::find(lib.archive, "version"), &version, &size);
  zutl_zar::read(lib.archive, zutl_zar::find(lib.archive, "format"), &format, &size2);
  if (version != 41 || format != 1) {  // [0x0060e61c], [0x0060e620]
    zutl_zar::close(lib.archive);
    return false;
  }
  if (ok) ::zcls::mechlib_read_materials(lib);  // 0x00510c80: MW3's
  return ok;
}

// Pirate's Moon 1.0 @ 0x00510b90. Ours: bounded by the entry's span; a
// broken tree is refused, as MW3's.
Node* mechlib_load(MechLib& lib, const char* name) {
  if (Node* hit = ::zcls::mechlib_find(lib, name)) return hit;
  const int index = zutl_zar::find(lib.archive, name);
  uint32_t length = 0;
  std::FILE* f = zutl_zar::seek(lib.archive, index, &length);
  if (f) g_end = std::ftell(f) + static_cast<long>(zutl_zar::span(lib.archive, index)), g_broken = false;
  Node* root = pm::zcls::read_tree(lib.reader, f, nullptr);
  g_end = -1;
  if (root && !g_broken && !materials_fit(lib, root)) g_broken = true;
  if (g_broken) {
    g_broken = false;
    std::printf("mw3pm: %s: its model %s is broken (a count, class, depth or index it cannot hold), not read\n",
                lib.archive.path ? lib.archive.path : "mechlib", name);
    return nullptr;
  }
  pm::zcls::mechlib_resolve(lib, root);
  if (root) lib.trees.push_back(root);
  return root;
}

// Pirate's Moon 1.0 @ 0x00510fd0. Its second loop, over the model's refs,
// never runs: the read left none.
void mechlib_resolve(MechLib& lib, Node* node) {
  if (!node) return;
  if (pm::zmod::Model* m = node->pm_model) {
    for (int32_t i = 0; i < m->count(0x10); ++i) {
      pm::zmod::Polygon& q = m->polygons[i];
      const size_t n = q.material_count > 0 ? static_cast<size_t>(q.material_count) : 0;
      q.materials = static_cast<::zmod::Material**>(std::calloc(n ? n : 1, sizeof(void*)));
      for (size_t j = 0; j < n; ++j) q.materials[j] = ::zcls::material_resolve(*lib.materials[q.library_indices[j]]);
      std::free(q.library_indices);
      q.library_indices = nullptr;
    }
    pm::zmod::sort_by_material(*m);
  }
  for (int32_t i = 0; i < node->child_count(); ++i) pm::zcls::mechlib_resolve(lib, node->children[i]);
}

// Pirate's Moon 1.0 @ 0x005110c0
Node* read_tree(::zcls::TreeReader& lib, std::FILE* f, Node* parent) {
  if (!f) return nullptr;
  ++lib.depth;  // +0x40 of PM's library object
  int32_t children = 0;
  Node* node = read_node(f, parent, &children);
  if (node && bounded() && lib.depth > 64) g_broken = true;  // ours: a broken file's
  if (node && !g_broken) {
    read_model(f, node);
    if (bounded() && (children < 0 || !fits(f, static_cast<uint64_t>(children) * 0xd0))) children = 0;
    for (int32_t i = 0; i < children && !(bounded() && g_broken); ++i) pm::zcls::read_tree(lib, f, node);
  }
  --lib.depth;
  return node;
}

// Pirate's Moon 1.0 @ 0x00511210. The exe dereferences null for a class
// other than 5 or 6.
Node* read_node(std::FILE* f, Node* parent, int32_t* children) {
  uint8_t file_node[0xd0] = {};
  if (!fits(f, sizeof file_node)) return nullptr;
  std::fread(file_node, sizeof file_node, 1, f);
  const int32_t cls = static_cast<int32_t>(u32_at(file_node, 0x34));
  Node* node = nullptr;
  if (cls == 5) {
    if (!fits(f, 0x90)) return nullptr;
    node = ::zcls::object3d_create();  // 0x0050f960
    std::fread(static_cast<::zcls::Object3d*>(node->data)->raw, 0x90, 1, f);
  } else if (cls == 6) {
    if (!fits(f, 0x5c)) return nullptr;
    node = ::zcls::lod_create();  // 0x00515b30: PM's 0x5c bytes
    auto* l = static_cast<::zcls::Lod*>(node->data);
    uint8_t pm[0x5c] = {};
    std::fread(pm, sizeof pm, 1, f);
    translate_lod(*l, pm);
  } else {
    if (bounded()) g_broken = true;  // ours: the exe would dereference null
    return nullptr;
  }
  const uint32_t flags = node->flags();
  const int32_t box = translate_node(*node, file_node);
  node->set_flags((node->flags() & ~3u) | (flags & 3));
  node->put<int32_t>(0x54, 0), node->put<int32_t>(0x58, 0);  // PM's int16 counts at +0x54, +0x56
  node->put<int32_t>(0x5c, 0), node->put<int32_t>(0x60, 0);
  node->parents = nullptr, node->children = nullptr;
  int16_t count;
  std::memcpy(&count, file_node + 0x56, 2);
  *children = count;
  // In the tree reader (0x005110c0) in the exe: the box index made a pointer.
  node->pm_box = box_pointer(*node, box);
  if (parent) ::zcls::node_add_child(parent, node);  // 0x004fd5e0
  return node;
}

// Pirate's Moon 1.0 @ 0x00511310. The file's pointer fields only say whether
// an array follows.
void read_model(std::FILE* f, Node* node) {
  if (node->get<int32_t>(0x3c) == 0) return;
  uint8_t header[0x64] = {};
  if (fits(f, sizeof header)) std::fread(header, sizeof header, 1, f);
  pm::zmod::Model* m = pm::zmod::model_alloc();
  std::memcpy(m->header, header, sizeof header);
  node->pm_model = m;
  // Ours: once broken nothing more is read and the model is left with no
  // counts, so what stays in the pool of a refused tree is empty.
  auto refused = [&] {
    if (!bounded() || !g_broken) return false;
    std::memset(m->header + 0x10, 0, 16), std::memset(m->header + 0x5c, 0, 8);
    return true;
  };
  if (refused()) return;
  if (m->count(0x14) > 0) m->vertices = read_array<float>(f, m->count(0x14), 12);
  if (m->count(0x18) > 0) m->normals = read_array<float>(f, m->count(0x18), 12);
  if (m->count(0x1c) > 0) m->morphs = read_array<float>(f, m->count(0x1c), 12);
  if (refused()) return;  // a count past the end, polygons or not
  const int32_t polys = m->count(0x10);
  if (polys > 0) {
    if (bounded() && !fits(f, static_cast<uint64_t>(polys) * 0x28)) {  // ours: none of them read
      refused();
      return;
    }
    std::vector<uint8_t> raw(static_cast<size_t>(polys) * 0x28);
    std::fread(raw.data(), raw.size(), 1, f);
    m->polygons = new pm::zmod::Polygon[static_cast<size_t>(polys)]{};
    for (int32_t i = 0; i < polys; ++i) {
      const uint8_t* r = raw.data() + i * 0x28;
      pm::zmod::Polygon& q = m->polygons[i];
      q.flags = u32_at(r, 0);
      q.priority = static_cast<int32_t>(u32_at(r, 4));
      q.material_count = static_cast<int32_t>(u32_at(r, 0x10));
      q.file_uvs = u32_at(r, 0x18);
      q.zone_set = u32_at(r, 0x24);
      const uint32_t k = q.flags & 0x3ff;
      if (k) q.vertex_indices = read_array<uint32_t>(f, k, 4);
      if ((q.flags & 0x1000) && u32_at(r, 0xc)) q.normal_indices = read_array<uint32_t>(f, k, 4);
      q.library_indices = read_array<int32_t>(f, q.material_count, 4);
      if (refused()) return;  // before a broken count's UV arrays
      if (q.file_uvs) {
        const size_t n = q.material_count > 0 ? static_cast<size_t>(q.material_count) : 0;
        q.uvs = static_cast<float**>(std::calloc(n ? n : 1, sizeof(float*)));
        for (size_t j = 0; j < n; ++j) q.uvs[j] = read_array<float>(f, k, 8);
      }
      if (u32_at(r, 0x1c)) q.colors = read_array<float>(f, k, 12);
      q.scratch = static_cast<uint32_t*>(std::calloc(k ? k : 1, 4));
      // Ours: a material and indices within the model's arrays (the exe
      // sorts by the first material; a broken file's indices the renderer
      // would read past).
      if (bounded() && !g_broken) {
        if (q.material_count < 1) g_broken = true;
        for (uint32_t j = 0; j < k && !g_broken; ++j)
          if (q.vertex_indices[j] >= static_cast<uint32_t>(m->count(0x14))) g_broken = true;
        for (uint32_t j = 0; q.normal_indices && j < k && !g_broken; ++j)
          if (q.normal_indices[j] >= static_cast<uint32_t>(m->count(0x18))) g_broken = true;
      }
      if (refused()) return;
    }
  }
  if (const int32_t refs = m->count(0x5c); refs > 0) {  // read and dropped
    std::free(read_array<uint8_t>(f, refs, 12));
    std::memset(m->header + 0x5c, 0, 8);
  }
}

}  // namespace pm::zcls
