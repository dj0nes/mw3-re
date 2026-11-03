#pragma once
#include <cstdio>

#include "gamez/zclass/cls_mechlib.h"

// Pirate's Moon's mechlib.zbd (version 41; docs/pm-map.md, mechlib 41).
// Original file unknown: it sits between branchep1\gamez\zClass\object3d.c and
// cls_clutter.cpp, as MW3's does. The library object, its materials, find,
// close and the material resolve are MW3's (gamez/zclass/cls_mechlib); PM
// changed the open's version, the tree's nodes (an object3d or a LOD) and
// its models (PM's, pm::zmod), which are resolved and sorted by material.
// Shared code reaches these through the game table (zcls::mechlib_open,
// mechlib_load).
namespace pm::zcls {

// Pirate's Moon 1.0 @ 0x00510910: MW3's 0x004fc6d0 with version 41.
bool mechlib_open(::zcls::MechLib& lib, const char* path);
// Pirate's Moon 1.0 @ 0x00510b90: MW3's 0x004fc8f0 with PM's tree and
// resolve. The renderer conversion after it (0x00510ab0 under Direct3D,
// else 0x00510b00) is left out, as for the world's models (pm-map.md, the
// models' form).
::zcls::Node* mechlib_load(::zcls::MechLib& lib, const char* name);
// Pirate's Moon 1.0 @ 0x00510fd0: each polygon's library indices become
// world materials (no index check), then the model is sorted by material
// (0x0055c470).
void mechlib_resolve(::zcls::MechLib& lib, ::zcls::Node* node);

// Pirate's Moon 1.0 @ 0x005110c0: null for no file; the node, its box
// pointer (+0x70), its model, then its children.
::zcls::Node* read_tree(::zcls::TreeReader& lib, std::FILE* f, ::zcls::Node* parent);
// Pirate's Moon 1.0 @ 0x00511210: the file's 0xd0 bytes over a new object3d
// (class 5, data 0x90) or LOD (6, data 0x5c), but its data, flag bits 0-1,
// list-7 item and links; the file's child count in `children`.
::zcls::Node* read_node(std::FILE* f, ::zcls::Node* parent, int32_t* children);
// Pirate's Moon 1.0 @ 0x00511310: when the node's +0x3c is set, a model from
// PM's pool (0x00558f50); its polygons keep the library's material indices
// (Polygon::library_indices). No lights are read (the count stays), and the
// file's material refs are skipped: the resolve rebuilds them.
void read_model(std::FILE* f, ::zcls::Node* node);

}  // namespace pm::zcls
