#pragma once
#include <cstdint>
#include <cstdio>

#include "gamez/zclass/cls_node.h"

// zClass\cls_zbd.c: reading gamez.zbd and its node part (docs/gamez.md).
namespace zcls {

int read_node_list(Node** out, int32_t count, std::FILE* f);  // 0x004fea80
int read_node(Node* node, std::FILE* f);                      // 0x004feaf0
int read_nodes(int32_t size, std::FILE* f);                   // 0x004ff200

struct ZbdHeader {                 // 0x24 bytes
  uint32_t signature;              // 0x02971222
  uint32_t version;                // 27
  int32_t texture_count;
  int32_t textures_offset, materials_offset, models_offset;
  int32_t node_array_size, node_last_free, nodes_offset;
};
std::FILE* open_zbd(const char* path, ZbdHeader* header);     // 0x004ff6a0
// Ours, not the original's: given the opened file, may return another stream
// with the file's contents changed (the 1.2 data layer, mech3/patch12.h),
// closing the first. Null: none.
using OpenHook = std::FILE* (*)(const char* path, std::FILE* f);
void set_open_hook(OpenHook hook);
char* zbd_path();                                             // 0x0060bf50, 0x30 bytes
int read_zbd(const char* path);                               // 0x004ff520
int32_t node_index_of(const Node* node);                      // 0x004fe100
// A node's model (and with `recurse` its tree's) read again from the file
// the last read_zbd remembered: 0, or 1 without a file or the node in it.
int reread_node(std::FILE* f, const ZbdHeader& h, Node* node, int32_t recurse);  // 0x004ff7a0
int reread(Node* node, int32_t recurse);                      // 0x004ff730

}  // namespace zcls
