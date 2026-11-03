#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "gamez/zmodel/gmod_const.h"
#include "gamez/zmodel/gmod_matl.h"

// C:\zipper\branchep1\gamez\zModel\gmod_const.c: Pirate's Moon's world models
// (docs/pm-map.md, gamez.zbd 41). PM's own types, not MW3's: a polygon has a
// list of materials (UVs per material), a model a list of the materials it
// uses. Read as the file has them; the Direct3D strip conversion PM ran next
// (0x00570b70) is left out (pm-map.md, the models' form).
namespace pm::zmod {

struct Polygon {                       // 0x28 bytes
  uint32_t flags;                      // +0x00  bits 0-9 vertex count, 0x400 back face, 0x1000 normals, 0x2000 strip
  int32_t priority;                    // +0x04
  uint32_t* vertex_indices = nullptr;  // +0x08
  uint32_t* normal_indices = nullptr;  // +0x0c  with 0x1000
  int32_t material_count;              // +0x10
  ::zmod::Material** materials = nullptr;  // +0x14  indices in the file, offset by the file's material base
  float** uvs = nullptr;               // +0x18  one array per material, when the file's +0x18 is set
  float* colors = nullptr;             // +0x1c  always
  uint32_t* scratch = nullptr;         // +0x20
  uint32_t zone_set;                   // +0x24
  uint32_t file_uvs;                   // the file's +0x18 (0: no UVs)
  // mechlib's: the +0x14 array as read, the library's material indices, until
  // mechlib_resolve makes them `materials` (the exe overwrites them in place).
  int32_t* library_indices = nullptr;
};

struct MaterialRef {                   // 0x0c bytes
  ::zmod::Material* material;          // +0x00  an index in the file
  int32_t usage_count;                 // +0x04  polygons
  Polygon* polygon;                    // +0x08  the first of them
};

struct Model {                         // 0x68-byte slot
  uint8_t header[0x64] = {};           // as read: flags +0x08, counts +0x0c..+0x20, material refs +0x5c
  int32_t link = 0;                    // +0x64  the free list
  Polygon* polygons = nullptr;         // +0x34
  float* vertices = nullptr;           // +0x38
  float* normals = nullptr;            // +0x3c
  ::zmod::Light* lights = nullptr;     // +0x40
  float* morphs = nullptr;             // +0x44
  MaterialRef* refs = nullptr;         // +0x60
  int32_t count(int off) const {
    int32_t v;
    std::memcpy(&v, header + off, 4);
    return v;
  }
};

struct ModelPool {
  // FIX (A): owned blocks for the exe's realloc'd array of malloc'd ones; a
  // model keeps its address as the pool grows.
  std::vector<std::unique_ptr<Model[]>> blocks;  // 0x008d4af4, count 0x008d4aec
  int32_t block = 750;                 // 0x008d4ae8  slots a block (0x0053fe30)
  int32_t capacity = 0;                // 0x008d4af0
  int32_t count = 0;                   // 0x008d4af8
  int32_t free_head = -1;              // 0x008d4afc
  Model& at(int32_t i) { return blocks[static_cast<size_t>(i / block)][i % block]; }
};
ModelPool& models();

// Pirate's Moon 1.0 @ 0x00557ba0: the slot of an index, null below 0.
Model* model_at(int32_t index);
// Pirate's Moon 1.0 @ 0x00558f50: the free head, zeroed but its link, flags
// 3; a full pool grows by a block first (0x00557be0; MW3's aborted). The exe
// returns null before the pool's set-up ([0x008d4ae4], always done in play).
Model* model_alloc();
// Pirate's Moon 1.0 @ 0x0055c470: the material refs rebuilt from each
// polygon's first material, the polygons regrouped to match: a cycled
// material's polygons one ref each, first; then one ref per other material,
// in the order first used. One ref keeps the polygons as they are.
void sort_by_material(Model& m);
// Pirate's Moon 1.0 @ 0x00558ba0: a gamez.zbd's models appended in new blocks
// (the count and free head taken on the first load only), each one's data
// read (0x005587e0) with the file's material base. The first one's index, 0
// for none, -1 on a read error.
int read_models(std::FILE* f, int32_t material_base);
// Pirate's Moon 1.0 @ 0x005587e0: one model's arrays, its material indices
// offset by `material_base`. The exe checks no read.
void read_model_data(std::FILE* f, Model& m, int32_t material_base);

}  // namespace pm::zmod
