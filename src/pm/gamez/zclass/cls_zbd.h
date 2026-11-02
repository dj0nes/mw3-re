#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace zimg { struct TextureEntry; }

// C:\zipper\branchep1\gamez\zClass\cls_zbd.c: Pirate's Moon's gamez.zbd
// (version 41) stacks: each file read appends to the material, model and
// node pools, and the loader keeps a list of the files read (docs/pm-map.md,
// gamez.zbd 41).
namespace pm::zcls {

struct ZbdFile {             // 0x2c bytes at 0x00639c28
  std::string path;          // +0x00
  uint8_t header[0x28] = {}; // +0x04  signature, version, time stamp, the texture count +0xc, offsets
  int32_t get(int off) const {
    int32_t v;
    std::memcpy(&v, header + off, 4);
    return v;
  }
};

struct ZbdState {
  std::vector<ZbdFile> files;  // 0x00639c28, count 0x00639c2c
  int32_t current = -1;        // 0x0060ea00  the file being read
  std::FILE* file = nullptr;   // 0x00639c30  open while it is
  bool own_textures = false;   // 0x00639c34  its texture directory is the table's (the first file)
};
ZbdState& zbd_state();

// Pirate's Moon 1.0 @ 0x00516ab0: a texture index of the file being read.
// The first file's are the table's (0x00531840); a later file's directory was
// never read, so its entry's name is looked up (0x00516ae0).
zimg::TextureEntry* texture_for(int32_t index);
// Pirate's Moon 1.0 @ 0x00516ae0: entry `index` of the current file's own
// directory, read from the file, found or added by name (0x00532450). Null
// outside the directory or with no file open.
zimg::TextureEntry* file_texture(int32_t index);

}  // namespace pm::zcls
