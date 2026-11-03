#pragma once
#include <cstdint>
#include <cstdio>

// The model library reader for mechlib.zbd (docs/mechlib.md). Original file
// unknown: it sits between zClass\object3d.c and zClass\switch.c.
#include <list>
#include <vector>

#include "gamez/zutil/zutl_zar.h"

namespace zimg {
struct TextureEntry;
}
namespace zmod {
struct Model;
struct Material;
}

namespace zcls {

struct Material {                // 0xc bytes
  uint8_t* raw = nullptr;        // +0x00  the 0x28 file bytes, freed once resolved
  bool resolved = false;         // +0x04
  char* texture = nullptr;       // +0x08  the texture's file name
  // +0x00 once resolved: the world material it became. FIX (A): its own
  // field, and the texture (the raw bytes' +0x10 in the exe) too.
  zmod::Material* world = nullptr;
  zimg::TextureEntry* entry = nullptr;
};

struct Node;

struct TreeReader {
  int32_t depth = 0;             // +0x3c of the library object
};

// The library object (0x40 bytes, [0x00609844], made by 0x004fc5d0).
struct MechLib {
  zutl_zar::Archive archive;     // +0x00
  bool materials_changed = false;  // +0x18  never set: the writer never runs
  std::vector<Material*> materials;  // +0x20  the shared list
  std::list<Node*> trees;        // +0x30  the loaded trees, count +0x34
  TreeReader reader;             // +0x3c  its depth
};
// The running game's (zgame::Table): MW3's 0x004fc6d0, 0x004fc8f0.
bool mechlib_open(MechLib& lib, const char* path);
Node* mechlib_load(MechLib& lib, const char* name);
// 0x004fc6d0: `version` 27 and `format` 1 are read even when the open failed
// (then both are 0 and the archive is closed again); the materials when open.
bool mw3_mechlib_open(MechLib& lib, const char* path);
bool mechlib_read_materials(MechLib& lib);              // 0x004fc9e0
Node* mechlib_find(MechLib& lib, const char* name);     // 0x004fc650: by root node name
// 0x004fc8f0: a tree already loaded is handed out again, else read from its
// entry and its materials resolved. The two arguments after the name are
// unused in the exe.
Node* mw3_mechlib_load(MechLib& lib, const char* name);
void mechlib_resolve(MechLib& lib, Node* node);         // 0x004fcd40: no index check
// 0x004fc520: the texture by name (0x00518c60), then the world's equal
// material or a new one (0x00536ad0); null while that fails.
zmod::Material* material_resolve(Material& m);
void material_free(Material& m);                        // 0x004fc490
// 0x004fc790: the trees stay (the world's nodes), the materials are freed,
// the archive closed.
void mechlib_close(MechLib& lib);
// 0x00553450 ([0x0072be74], set in one video mode by zvid_init.c 0x00579e79):
// mechlib_load then runs 0x004fc8b0 over every model, which is not ported.
bool& mechlib_post_process();

bool read_material(std::FILE* f, Material& m);   // 0x004fc570
Node* read_node(std::FILE* f, Node* parent);     // 0x004fce50
Node* read_tree(TreeReader& lib, std::FILE* f, Node* parent);  // 0x004fcdc0
// When node +0x3c != 0: a model from the world pool (0x00538fe0), its
// polygons' materials left as mechlib's indices (Polygon::material_index).
zmod::Model* read_model(std::FILE* f);           // 0x004fcf20

}  // namespace zcls
