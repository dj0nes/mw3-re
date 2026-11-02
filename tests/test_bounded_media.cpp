// M3 (docs/modding.md): the texture and image libraries, the sounds archives
// and interp.zbd's scripts, bounded by their files and entries. The shipped
// files read whole and as before; truncated and garbled copies are refused
// or read, never a crash.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "check.h"
#include "gamez/zimage/zimg_library.h"

namespace {
namespace fs = std::filesystem;

std::vector<uint8_t> slurp(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}
void spit(const std::string& path, const std::vector<uint8_t>& d) {
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}
std::string scratch(const char* name) {
  const std::string d = std::string(MW3_TEST_TMP) + "/" + name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}
uint32_t u32(const std::vector<uint8_t>& d, size_t at) {
  uint32_t v = 0;
  if (at + 4 <= d.size()) std::memcpy(&v, &d[at], 4);
  return v;
}
void put32(std::vector<uint8_t>& d, size_t at, uint32_t v) { std::memcpy(&d[at], &v, 4); }

// A copy: cut short, bytes flipped, or words made huge.
void mutate(std::vector<uint8_t>& d, std::mt19937& r, int kind) {
  if (d.empty()) return;
  if (kind == 0) {
    d.resize(r() % d.size());
  } else if (kind == 1) {
    for (int n = 1 + r() % 16; n > 0; --n) d[r() % d.size()] ^= static_cast<uint8_t>(1 + r() % 255);
  } else {
    for (int n = 1 + r() % 4; n > 0 && d.size() >= 4; --n) {
      const size_t at = (r() % (d.size() / 4)) * 4;
      const uint32_t v = r() % 2 ? 0xffffffffu - r() % 16 : 0x10000u + r() % 0x7fffffffu;
      put32(d, at, v);
    }
  }
}

// ---- the texture and image libraries

void free_palettes() {
  for (uint16_t* p : zimg::state().palettes) std::free(p);
  zimg::state().palettes.clear();
}
// A library on a file of its own (no resolving, no mounts).
struct OneLibrary {
  zimg::Library lib{};
  OneLibrary(const std::string& dir, const std::string& file) {
    zimg::state() = zimg::State{};
    static std::string keep;
    keep = dir + "/";
    zimg::state().dir = keep.c_str();
    std::snprintf(lib.name, sizeof lib.name, "%s", file.c_str());
  }
  ~OneLibrary() {
    zimg::close(lib);
    std::free(lib.entries);
    free_palettes();
    zimg::state() = zimg::State{};
  }
};
// A texture's bytes from its info: info, pixels, alpha, its own palette.
size_t texture_bytes(const std::vector<uint8_t>& d, size_t at) {
  const uint8_t flags = d[at];
  int16_t w, h, pc;
  std::memcpy(&w, &d[at + 4], 2), std::memcpy(&h, &d[at + 6], 2), std::memcpy(&pc, &d[at + 12], 2);
  const size_t px = static_cast<size_t>(w) * h, bpp = (flags & zimg::kTwoBytes) ? 2 : 1;
  return 0x10 + (pc != 0 ? px : bpp * px) + ((flags & zimg::kFullAlpha) ? px : 0) +
         (!(flags & zimg::kGlobalPalette) && pc != 0 ? bpp * static_cast<size_t>(pc) : 0);
}
}  // namespace

// Every texture of every shipped library through the bounded load, the same
// pixels as the exe's read at its offset.
TEST(bounded_textures_read_the_shipped_libraries_whole) {
  const fs::path zbd = fs::path(MW3_GAME_DIR) / "zbd";
  std::error_code ec;
  if (!fs::exists(zbd / "rimage.zbd", ec)) return;
  std::vector<fs::path> files;
  for (fs::recursive_directory_iterator it(zbd, ec), end; !ec && it != end; it.increment(ec)) {
    const std::string n = it->path().filename().string();
    if (n == "rimage.zbd" || n.rfind("rmechtex", 0) == 0 || n.rfind("rtexture", 0) == 0) files.push_back(it->path());
  }
  int libraries = 0, textures = 0, whole = 0, same = 0;
  for (const fs::path& p : files) {
    OneLibrary one(p.parent_path().string(), p.filename().string());
    EXPECT(zimg::open(one.lib));
    if (!one.lib.entries) continue;
    ++libraries;
    std::FILE* raw = std::fopen(p.string().c_str(), "rb");
    for (int i = 0; i < one.lib.header.texture_count; ++i) {
      ++textures;
      const zimg::Entry e = one.lib.entries[i];
      zimg::Texture* t = zimg::load(one.lib, e.name);
      if (!t) continue;
      ++whole;
      std::fseek(raw, static_cast<long>(e.offset), SEEK_SET);
      zimg::Texture* u = zimg::read_texture(raw);
      const size_t n = static_cast<size_t>(t->palette_count != 0 ? t->pixel_count : zimg::bytes_per_pixel(*t) * t->pixel_count);
      if (u && u->width == t->width && u->height == t->height && u->flags == t->flags && std::memcmp(u->pixels, t->pixels, n) == 0)
        ++same;
      zimg::free_texture(u);
      zimg::free_texture(t);
    }
    std::fclose(raw);
  }
  std::printf("  %d libraries, %d textures: %d read whole, %d the same as unbounded\n", libraries, textures, whole, same);
  EXPECT(libraries == 22 && textures > 10000 && whole == textures && same == textures);
}

// A small library (the first textures of c1's rtexture2.zbd, its palettes)
// cut short, flipped and given huge words: no crash, and a cut-short copy
// keeps exactly the textures whose bytes it still has.
TEST(bounded_textures_survive_broken_copies) {
  const std::string src = std::string(MW3_GAME_DIR) + "/zbd/c1/rtexture2.zbd";
  std::error_code ec;
  if (!fs::exists(src, ec)) return;
  const std::vector<uint8_t> full = slurp(src);
  const int32_t palettes = static_cast<int32_t>(u32(full, 8)), count = 12;
  EXPECT(static_cast<int32_t>(u32(full, 12)) > count);
  // Rebuilt: the header, 12 entries, the global palettes, their textures.
  std::vector<uint8_t> small(0x18 + count * 0x28 + palettes * 0x200);
  std::memcpy(small.data(), full.data(), 0x18);
  put32(small, 12, count);
  std::memcpy(&small[0x18 + count * 0x28], &full[0x18 + u32(full, 12) * 0x28], palettes * 0x200);
  std::vector<std::pair<size_t, size_t>> spans;  // where each texture went, its bytes
  for (int32_t i = 0; i < count; ++i) {
    const size_t e = 0x18 + i * 0x28, at = u32(full, e + 32), n = texture_bytes(full, at);
    std::memcpy(&small[e], &full[e], 0x28);
    put32(small, e + 32, static_cast<uint32_t>(small.size()));
    spans.push_back({small.size(), n});
    small.insert(small.end(), full.begin() + static_cast<long>(at), full.begin() + static_cast<long>(at + n));
  }
  const std::string dir = scratch("bounded_tex");
  std::mt19937 r(1999);
  int copies = 0, refused_whole = 0, cut_right = 0, cuts = 0, loaded = 0;
  for (int k = 0; k < 300; ++k) {
    std::vector<uint8_t> d = small;
    const int kind = k % 3;
    mutate(d, r, kind);
    spit(dir + "/lib.zbd", d);
    OneLibrary one(dir, "lib.zbd");
    ++copies;
    if (!zimg::open(one.lib)) {
      ++refused_whole;
      if (kind == 0) ++cuts, ++cut_right;  // the table itself cut
      continue;
    }
    int got = 0, want = 0;
    for (int32_t i = 0; i < one.lib.header.texture_count && i < 64; ++i) {
      char name[32];
      std::memcpy(name, one.lib.entries[i].name, sizeof name);
      if (zimg::Texture* t = zimg::load(one.lib, name)) {
        ++got, ++loaded;
        zimg::free_texture(t);
      }
    }
    if (kind == 0) {
      for (const auto& [at, n] : spans) want += at + n <= d.size();
      ++cuts;
      cut_right += got == want;
    }
  }
  // The table's own counts: past the file, below 0; an entry's offset and
  // palette out of range.
  int tables = 0, entries = 0;
  for (const auto& [at, v] : std::vector<std::pair<size_t, uint32_t>>{{12, 0x7fffffff}, {12, 0xffffffff}, {8, 0x40000}, {8, 0xfffffffe}}) {
    std::vector<uint8_t> d = small;
    put32(d, at, v);
    spit(dir + "/lib.zbd", d);
    OneLibrary one(dir, "lib.zbd");
    tables += !zimg::open(one.lib);
  }
  for (const auto& [at, v] : std::vector<std::pair<size_t, uint32_t>>{{0x18 + 32, 0x7ffffff0}, {0x18 + 32, 4}, {0x18 + 36, 0x1000}, {0x18 + 36, 0xfffffff0}}) {
    std::vector<uint8_t> d = small;
    put32(d, at, v);
    spit(dir + "/lib.zbd", d);
    OneLibrary one(dir, "lib.zbd");
    char name[32];
    std::memcpy(name, &small[0x18], sizeof name);
    entries += zimg::open(one.lib) && zimg::load(one.lib, name) == nullptr;
  }
  EXPECT(tables == 4 && entries == 4);
  std::printf("  %d broken copies: %d refused whole, %d textures read; %d of %d cut copies kept exactly what they have\n",
              copies, refused_whole, loaded, cut_right, cuts);
  EXPECT(copies == 300 && cuts == 100 && cut_right == cuts);
}
