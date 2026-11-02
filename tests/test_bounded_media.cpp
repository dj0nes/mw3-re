// M3 (docs/modding.md): the texture and image libraries, the sounds archives
// and interp.zbd's scripts, bounded by their files and entries. The shipped
// files read whole and as before; truncated and garbled copies are refused
// or read, never a crash.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "check.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zinterp/zinterp.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/mods.h"
#include "platform/silent_sound.h"

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

namespace {
// ---- the sounds archives

struct ZarEntry {
  std::string name;
  std::vector<uint8_t> data;
};
std::vector<ZarEntry> zar_entries(const std::string& path) {
  std::vector<ZarEntry> out;
  zutl_zar::Archive a;
  zutl_zar::init(a);
  if (!zutl_zar::open(a, path.c_str())) return out;
  for (int i = 0; i < a.count; ++i) {
    ZarEntry e{a.entries[i].name, std::vector<uint8_t>(a.entries[i].length)};
    uint32_t size = a.entries[i].length;
    if (zutl_zar::read(a, i, e.data.data(), &size) == 0) out.push_back(std::move(e));
  }
  zutl_zar::destroy(a);
  return out;
}
void zar_write(const std::string& path, const std::vector<ZarEntry>& entries) {
  zutl_zar::Archive a;
  zutl_zar::init(a);
  zutl_zar::create(a, path.c_str());
  for (const ZarEntry& e : entries) zutl_zar::add(a, e.name.c_str(), e.data.data(), static_cast<uint32_t>(e.data.size()));
  zutl_zar::close(a);
  zutl_zar::destroy(a);
}
// A wave from the archive into a silent buffer: 1 read and made, 0 refused or
// not made; `same` its buffer the wave's own samples.
int sound_through(zutl_zar::Archive& a, platform::SilentSoundOutput& out, const char* name, bool* same) {
  zsnd::Wave w;
  w.path = name;
  if (!zsnd::wave_read_archive(w, a)) return 0;
  zsnd::Sound s{};
  if (!zsnd::create_buffer(&s, w)) return 0;
  void *p1 = nullptr, *p2 = nullptr;
  uint32_t n1 = 0, n2 = 0;
  out.lock(s.voice.buffer, 0, 1u << 30, &p1, &n1, &p2, &n2);
  *same = n1 == w.data_size && (!w.data || n1 == 0 || std::memcmp(p1, w.bytes.data() + w.data, n1) == 0);
  zsnd::release_sound(&s);
  return 1;
}
}  // namespace

// Every sound of both shipped archives and the loose WAVs: read, unrefused,
// its buffer its own samples.
TEST(bounded_sounds_read_the_shipped_archives_whole) {
  const fs::path zbd = fs::path(MW3_GAME_DIR) / "zbd";
  std::error_code ec;
  if (!fs::exists(zbd / "soundsL.zbd", ec)) return;
  platform::SilentSoundOutput out;
  zsnd::hooks() = zsnd::Hooks{};
  zsnd::hooks().output = &out;
  int sounds = 0, made = 0, same = 0;
  for (const char* file : {"soundsL.zbd", "soundsH.zbd"}) {
    const std::string path = (zbd / file).string();
    zutl_zar::Archive a;
    zutl_zar::init(a);
    EXPECT(zutl_zar::open(a, path.c_str()));
    for (int i = 0; i < a.count; ++i) {
      ++sounds;
      bool ok = false;
      made += sound_through(a, out, a.entries[i].name, &ok);
      same += ok;
    }
    zutl_zar::destroy(a);
  }
  int loose = 0, loose_ok = 0;
  for (fs::directory_iterator it(zbd, ec), end; !ec && it != end; it.increment(ec)) {
    if (it->path().extension() != ".wav") continue;
    ++loose;
    zsnd::Wave w;
    w.path = it->path().string();
    loose_ok += zsnd::wave_read_file(w) == 1 && zsnd::wave_broken(w) == nullptr && w.has_format;
  }
  std::printf("  %d archived sounds: %d made, %d their own samples; %d loose WAVs, %d read\n", sounds, made, same, loose, loose_ok);
  EXPECT(sounds > 500 && made == sounds && same == sounds && loose == 10 && loose_ok == loose);
  zsnd::hooks() = zsnd::Hooks{};
}

// Twelve shipped sounds in an archive, each cut short, flipped or given huge
// words: no crash; a cut one whose samples it lost is refused, and every one
// made holds exactly its wave's samples.
TEST(bounded_sounds_survive_broken_copies) {
  const std::string src = std::string(MW3_GAME_DIR) + "/zbd/soundsL.zbd";
  std::error_code ec;
  if (!fs::exists(src, ec)) return;
  std::vector<ZarEntry> all = zar_entries(src), pick;
  for (size_t i = 0; i < all.size() && pick.size() < 12; i += all.size() / 12) pick.push_back(all[i]);
  EXPECT(pick.size() == 12);
  platform::SilentSoundOutput out;
  zsnd::hooks() = zsnd::Hooks{};
  zsnd::hooks().output = &out;
  const std::string dir = scratch("bounded_snd");
  std::mt19937 r(1999);
  int copies = 0, made = 0, refused = 0, cut = 0, cut_refused = 0, wrong = 0;
  for (int k = 0; k < 25; ++k) {
    std::vector<ZarEntry> broken = pick;
    std::vector<bool> lost(broken.size());
    for (size_t i = 0; i < broken.size(); ++i) {
      const int kind = (k + static_cast<int>(i)) % 3;
      const size_t before = broken[i].data.size();
      mutate(broken[i].data, r, kind);
      // Cut inside the samples: the RIFF's data chunk ends past the bytes.
      lost[i] = kind == 0 && broken[i].data.size() < before && broken[i].data.size() > 0x2c;
    }
    zar_write(dir + "/s.zbd", broken);
    zutl_zar::Archive a;
    zutl_zar::init(a);
    EXPECT(zutl_zar::open(a, (dir + "/s.zbd").c_str()));
    for (size_t i = 0; i < broken.size(); ++i) {
      ++copies;
      bool ok = false;
      const int got = sound_through(a, out, broken[i].name.c_str(), &ok);
      made += got, refused += !got, wrong += got && !ok;
      if (lost[i]) ++cut, cut_refused += !got;
    }
    zutl_zar::destroy(a);
  }
  std::printf("  %d broken sounds: %d made, %d refused; %d of %d cut inside their samples refused\n", copies, made, refused,
              cut_refused, cut);
  EXPECT(copies == 300 && wrong == 0 && cut > 50 && cut_refused == cut);
  zsnd::hooks() = zsnd::Hooks{};
}

namespace {
// ---- interp.zbd's scripts

// Each archived script's lines read as the interpreter does (binary), with
// its bound or (bounded false) without; -1 when the archive is refused.
std::vector<int> script_lines(const std::string& dir, bool bounded, std::vector<std::string>* names = nullptr) {
  zint::Interp in;
  in.search_path = dir;
  std::vector<int> out;
  if (!zint::open_archive(in, "interp.zbd")) return {-1};
  std::vector<char> buf(0x1000);
  for (int32_t i = 0; i < in.entry_count; ++i) {
    const std::string name = in.dir[i].name;
    if (names) names->push_back(name);
    zint::ScriptFile* f = zint::archive_find(in, name.c_str());
    if (!f) {
      out.push_back(-1);
      continue;
    }
    if (!bounded) f->end = -1;
    in.binary = 1;
    int n = 0;
    while (n < 100000 && zint::read_line(in, f, buf.data())) {
      ++n;
      std::free(in.line);
      in.line = nullptr;
    }
    out.push_back(n);
  }
  std::fclose(in.archive.f);
  in.archive.f = nullptr;
  return out;
}
}  // namespace

// Every shipped script: as many lines bounded as the exe's read, its end
// marker inside its bytes.
TEST(bounded_interp_reads_the_shipped_scripts_whole) {
  const std::string zbd = std::string(MW3_GAME_DIR) + "/zbd";
  std::error_code ec;
  if (!fs::exists(zbd + "/interp.zbd", ec)) return;
  const std::vector<int> bounded = script_lines(zbd, true), plain = script_lines(zbd, false);
  int lines = 0, same = 0;
  for (size_t i = 0; i < bounded.size() && i < plain.size(); ++i) lines += bounded[i], same += bounded[i] == plain[i] && bounded[i] > 0;
  std::printf("  %zu scripts, %d lines: %d scripts the same bounded\n", bounded.size(), lines, same);
  EXPECT(bounded.size() == 44 && lines == 2504 && bounded.size() == plain.size() && same == static_cast<int>(bounded.size()));
}

// The first ten scripts in an archive of their own, cut short, flipped and
// given huge words: no crash, and in a cut copy every script whose bytes are
// all there reads as before.
TEST(bounded_interp_survives_broken_copies) {
  const std::string zbd = std::string(MW3_GAME_DIR) + "/zbd";
  std::error_code ec;
  if (!fs::exists(zbd + "/interp.zbd", ec)) return;
  const std::vector<uint8_t> full = slurp(zbd + "/interp.zbd");
  const uint32_t count = u32(full, 8), keep = 10;
  EXPECT(count > keep);
  std::vector<uint32_t> starts;
  for (uint32_t i = 0; i < count; ++i) starts.push_back(u32(full, 12 + 0x80 * i + 0x7c));
  auto span_end = [&](uint32_t at) {
    uint32_t e = static_cast<uint32_t>(full.size());
    for (uint32_t s : starts)
      if (s > at && s < e) e = s;
    return e;
  };
  std::vector<uint8_t> small(12 + 0x80 * keep);
  std::memcpy(small.data(), full.data(), 12);
  put32(small, 8, keep);
  std::vector<std::pair<size_t, size_t>> spans;
  for (uint32_t i = 0; i < keep; ++i) {
    std::memcpy(&small[12 + 0x80 * i], &full[12 + 0x80 * i], 0x80);
    // A loose copy's time would reject the archive: none here, in our folder.
    put32(small, 12 + 0x80 * i + 0x7c, static_cast<uint32_t>(small.size()));
    const uint32_t at = starts[i], end = span_end(at);
    spans.push_back({small.size(), end - at});
    small.insert(small.end(), full.begin() + at, full.begin() + end);
  }
  const std::string dir = scratch("bounded_interp");
  spit(dir + "/interp.zbd", small);
  const std::vector<int> whole = script_lines(dir, true);
  EXPECT(whole.size() == keep);
  {
    // Script 0's first line as long as script 0 and 100 bytes of script 1:
    // its line refused, script 0 ended there, the rest as before.
    std::vector<uint8_t> d = small;
    put32(d, spans[0].first, static_cast<uint32_t>(spans[0].second + 100));
    spit(dir + "/interp.zbd", d);
    const std::vector<int> got = script_lines(dir, true);
    EXPECT(got.size() == keep && got[0] == 0 && std::equal(got.begin() + 1, got.end(), whole.begin() + 1));
  }
  {
    // Script 2's offset past the file: not the archive's (the loose file's
    // turn), the others as before.
    std::vector<uint8_t> d = small;
    put32(d, 12 + 0x80 * 2 + 0x7c, 0x7fffff00);
    spit(dir + "/interp.zbd", d);
    const std::vector<int> got = script_lines(dir, true);
    EXPECT(got.size() == keep && got[2] == -1 && got[1] == whole[1] && got[3] == whole[3]);
  }
  std::mt19937 r(1999);
  int copies = 0, refused = 0, cut = 0, cut_right = 0;
  for (int k = 0; k < 300; ++k) {
    std::vector<uint8_t> d = small;
    const int kind = k % 3;
    mutate(d, r, kind);
    spit(dir + "/interp.zbd", d);
    ++copies;
    const std::vector<int> got = script_lines(dir, true);
    if (got.size() == 1 && got[0] == -1 && keep != 1) {
      ++refused;
      if (kind == 0) ++cut, cut_right += d.size() < 12 + 0x80 * keep;
      continue;
    }
    if (kind != 0) continue;
    ++cut;
    bool right = got.size() == keep;
    for (size_t i = 0; right && i < keep; ++i)
      if (spans[i].first + spans[i].second <= d.size()) right = got[i] == whole[i];
    cut_right += right;
  }
  std::printf("  %d broken copies: %d refused whole; %d of %d cut copies read their whole scripts as before\n", copies, refused,
              cut_right, cut);
  EXPECT(copies == 300 && cut == 100 && cut_right == cut);
}

// --check-mod looks inside the libraries, the sounds and the scripts: the
// shipped copies pass, broken ones are named.
TEST(bounded_check_mod_reads_textures_sounds_and_scripts) {
  const std::string zbd = std::string(MW3_GAME_DIR) + "/zbd";
  std::error_code ec;
  if (!fs::exists(zbd + "/interp.zbd", ec)) return;
  const std::string mod = scratch("bounded_check_mod") + "/m";
  fs::create_directories(mod + "/zbd/c1", ec);
  fs::create_directories(mod + "/zbd/c2", ec);
  std::ofstream(mod + "/mod.txt") << "name=Checked\n";
  spit(mod + "/zbd/c1/rtexture2.zbd", slurp(zbd + "/c1/rtexture2.zbd"));
  std::vector<uint8_t> tex = slurp(zbd + "/c2/rtexture2.zbd");
  tex.resize(tex.size() - 5000);  // its last texture cut
  spit(mod + "/zbd/c2/rtexture2.zbd", tex);
  spit(mod + "/zbd/interp.zbd", slurp(zbd + "/interp.zbd"));
  std::vector<uint8_t> scripts = slurp(zbd + "/interp.zbd");
  put32(scripts, 12 + 0x7c, 0x7fffff00);  // the first script's offset past the file
  spit(mod + "/zbd/c1/interp.zbd", scripts);
  std::vector<ZarEntry> sounds = zar_entries(zbd + "/soundsL.zbd");
  sounds.resize(6);
  zar_write(mod + "/zbd/soundsL.zbd", sounds);
  sounds[3].data.resize(sounds[3].data.size() / 2);  // its samples cut
  zar_write(mod + "/zbd/soundsH.zbd", sounds);
  int errors = 0;
  std::string out;
  for (const std::string& l : mech3::mod_check(mod, "", &errors)) out += l + "\n";
  std::printf("%s", out.c_str());
  const std::string first_script = [&] {
    char n[0x79] = {};
    std::memcpy(n, &scripts[12], 0x78);
    return std::string(n);
  }();
  EXPECT(errors == 6);  // the cut library: its last four textures
  EXPECT(out.find("error: zbd/c2/rtexture2.zbd: texture ") != std::string::npos);
  EXPECT(out.find("error: zbd/c1/interp.zbd: script " + first_script + " cannot be read") != std::string::npos);
  EXPECT(out.find("error: zbd/soundsH.zbd: sound " + sounds[3].name + " cannot be read") != std::string::npos);
  EXPECT(out.find("error: zbd/c1/rtexture2.zbd") == std::string::npos && out.find("error: zbd/interp.zbd") == std::string::npos &&
         out.find("error: zbd/soundsL.zbd") == std::string::npos);
}

// gamez.zbd's texture directory (c1's 553 entries): mip indices made huge,
// below -1 or one past the table, and bytes flipped: no crash; every
// directory with an index outside the table refused, the table as it was;
// every one read has each mip null or a row of the table.
TEST(bounded_gamez_texture_directory_mips) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/c1/gamez.zbd";
  std::error_code ec;
  if (!fs::exists(path, ec)) return;
  const std::vector<uint8_t> full = slurp(path);
  const int32_t count = static_cast<int32_t>(u32(full, 8)), offset = static_cast<int32_t>(u32(full, 12));
  const std::vector<uint8_t> dir(full.begin() + offset, full.begin() + offset + count * 0x28);
  const std::string tmp = scratch("bounded_texdir") + "/dir";
  auto read = [&](const std::vector<uint8_t>& d) {
    spit(tmp, d);
    std::FILE* f = std::fopen(tmp.c_str(), "rb");
    zimg::textures().count = 7;  // a marker: a refused directory leaves it
    const int r = zimg::read_texture_directory(count, f, "test");
    std::fclose(f);
    return r;
  };
  EXPECT(read(dir) == count);
  std::mt19937 r(1999);
  int refused = 0, out_of_range = 0, read_ok = 0, inside = 0, kept = 0;
  for (int k = 0; k < 300; ++k) {
    std::vector<uint8_t> d = dir;
    bool bad = false;
    for (int n = 1 + r() % 3; n > 0; --n) {
      const size_t at = (r() % count) * 0x28 + (k % 2 ? 0x24 : r() % 0x24);
      const uint32_t v = k % 2 ? std::vector<uint32_t>{0x7fffffffu, 0xfffffffeu, static_cast<uint32_t>(count), r() % count,
                                                       0xffffffffu}[r() % 5]
                               : r();
      put32(d, at - at % 4, v);
    }
    for (int32_t i = 0; i < count; ++i) {
      const int32_t m = static_cast<int32_t>(u32(d, i * 0x28 + 0x24));
      bad = bad || m < -1 || m >= count;
    }
    out_of_range += bad;
    const int got = read(d);
    if (got < 0) {
      ++refused;
      kept += zimg::textures().count == 7;
      continue;
    }
    ++read_ok;
    bool all = true;
    for (int32_t i = 0; i < count; ++i) {
      const zimg::TextureEntry* m = zimg::textures().entries[i].mip;
      all = all && (!m || (m >= &zimg::textures().entries[0] && m < &zimg::textures().entries[count]));
    }
    inside += all;
  }
  std::printf("  300 garbled directories: %d with an index outside, %d refused (%d left the table), %d read, %d with every mip inside\n",
              out_of_range, refused, kept, read_ok, inside);
  EXPECT(out_of_range > 100 && refused == out_of_range && kept == refused && inside == read_ok);
  zimg::textures().count = 0;
}
