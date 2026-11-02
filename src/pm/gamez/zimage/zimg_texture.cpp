#include "pm/gamez/zimage/zimg_texture.h"

#include <cstdint>
#include <cstring>

#include "gamez/zerror/zerr.h"

namespace pm::zimg {

namespace {
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zImage\\zimg_texture.cpp";
}  // namespace

// Pirate's Moon 1.0 @ 0x00531920. MW3's 0x00518160 kept the file's state and
// made a pointer of the mip index; PM's sets both.
int read_texture_directory(int count, std::FILE* f) {
  using ::zimg::kMaxTextures;
  if (count == 0) return 0;
  if (count > kMaxTextures) {
    zerr::report(0x100, kFile, 0x139, "Too many textures for texture array size.");
    return -1;
  }
  static uint8_t buf[kMaxTextures * 0x2c];
  if (std::fread(buf, static_cast<size_t>(count) * 0x2c, 1, f) != 1) {
    zerr::report(0x200, kFile, 0x147, "Error reading GameZ Texture directory data.");
    return -1;
  }
  ::zimg::TextureTable& t = ::zimg::textures();
  t.count = count;
  for (int i = 0; i < count; ++i) {
    ::zimg::TextureEntry& e = t.entries[i];
    std::memcpy(e.name, buf + i * 0x2c + 0xc, 20);
    e.image = e.surface = nullptr;
    e.state = 2;
    e.group = t.group;
    e.mip = nullptr;
  }
  return count;
}

}  // namespace pm::zimg
