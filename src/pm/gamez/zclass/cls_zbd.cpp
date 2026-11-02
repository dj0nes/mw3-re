#include "pm/gamez/zclass/cls_zbd.h"

#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_texture.h"

namespace pm::zcls {

namespace {
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zClass\\cls_zbd.c";
ZbdState g_state;
}  // namespace

ZbdState& zbd_state() { return g_state; }

// Pirate's Moon 1.0 @ 0x00516ab0
zimg::TextureEntry* texture_for(int32_t index) {
  if (g_state.own_textures) return ::zimg::texture_at(index);  // 0x00531840
  return file_texture(index);
}

// Pirate's Moon 1.0 @ 0x00516ae0. The name is the entry's +0xc on, as the exe
// passes it (a 20-letter name runs on into the state's bytes).
zimg::TextureEntry* file_texture(int32_t index) {
  if (!g_state.file || g_state.current < 0 || index < 0) return nullptr;
  const ZbdFile& z = g_state.files[static_cast<size_t>(g_state.current)];
  if (index >= z.get(0xc)) return nullptr;
  const long at = std::ftell(g_state.file);
  std::fseek(g_state.file, z.get(0x10) + index * 0x2c, SEEK_SET);
  uint8_t raw[0x2c + 1] = {};
  const bool ok = std::fread(raw, 0x2c, 1, g_state.file) == 1;
  std::fseek(g_state.file, at, SEEK_SET);
  if (!ok) {
    zerr::report(0x200, kFile, 0x3b8, "Error reading GameZ Texture directory data.");
    return nullptr;
  }
  return ::zimg::add_texture(reinterpret_cast<const char*>(raw + 0xc));
}

}  // namespace pm::zcls
