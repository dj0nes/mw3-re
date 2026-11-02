// M3 (docs/modding.md): the readers a broken mod file could crash, bounded
// by their archive entry. The shipped files read whole; truncated and
// garbled copies are refused or read, never a crash.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "check.h"
#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zsiscript/si_script.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/mech_anim.h"
#include "mech3/mods.h"
#include "gamez/zutil/zutl_path.h"

namespace {
namespace fs = std::filesystem;
struct Entry {
  std::string name;
  std::vector<uint8_t> data;
};
std::vector<Entry> read_all(const std::string& path) {
  std::vector<Entry> out;
  zutl_zar::Archive a;
  zutl_zar::init(a);
  if (!zutl_zar::open(a, path.c_str())) return out;
  for (int i = 0; i < a.count; ++i) {
    Entry e;
    e.name = a.entries[i].name;
    uint32_t size = a.entries[i].length;
    e.data.resize(size);
    if (zutl_zar::read(a, i, e.data.data(), &size) == 0) out.push_back(std::move(e));
  }
  zutl_zar::destroy(a);
  return out;
}
void write_all(const std::string& path, const std::vector<Entry>& entries) {
  zutl_zar::Archive a;
  zutl_zar::init(a);
  zutl_zar::create(a, path.c_str());
  for (const Entry& e : entries) zutl_zar::add(a, e.name.c_str(), e.data.data(), static_cast<uint32_t>(e.data.size()));
  zutl_zar::close(a);
  zutl_zar::destroy(a);
}
bool is_tree(const std::string& n) { return n != "version" && n != "format" && n != "materials"; }
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
      std::memcpy(&d[at], &v, 4);
    }
  }
}
}  // namespace

TEST(bounded_mechlib_reads_the_shipped_trees_whole) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/mechlib.zbd";
  std::error_code ec;
  if (!fs::exists(path, ec)) return;
  zmod::models() = zmod::ModelPool{};
  zmod::reset_pool();
  zcls::MechLib lib;
  zutl_zar::init(lib.archive);
  EXPECT(zcls::mechlib_open(lib, path.c_str()));
  EXPECT(lib.materials.size() == 442);
  int trees = 0, loaded = 0;
  for (const Entry& e : read_all(path)) {
    if (!is_tree(e.name)) continue;
    ++trees;
    loaded += zcls::mechlib_load(lib, e.name.c_str()) != nullptr;
  }
  std::printf("  mechlib: %d of %d trees read whole\n", loaded, trees);
  EXPECT(trees == 57 && loaded == 57);
  zcls::mechlib_close(lib);
}

TEST(bounded_mechlib_fuzz_never_crashes) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/mechlib.zbd";
  std::error_code ec;
  if (!fs::exists(path, ec)) return;
  const std::vector<Entry> all = read_all(path);
  std::vector<size_t> trees;
  for (size_t i = 0; i < all.size(); ++i)
    if (is_tree(all[i].name)) trees.push_back(i);
  const std::string dir = std::string(MW3_TEST_TMP) + "/bounded_mechlib";
  fs::create_directories(dir, ec);
  const std::string copy = dir + "/mechlib.zbd";
  std::mt19937 r(1234);
  int runs = 0, refused = 0, read = 0, cut = 0, cut_refused = 0, materials_refused = 0;
  for (int seed = 0; seed < 240; ++seed) {
    std::vector<Entry> m = all;
    const int kind = seed % 3;
    const bool materials = seed % 8 == 7;
    const std::string want = materials ? std::string("materials") : all[trees[r() % trees.size()]].name;
    Entry* target = nullptr;
    for (Entry& e : m)
      if (e.name == want) target = &e;
    if (!target) continue;
    const size_t before = target->data.size();
    mutate(target->data, r, kind);
    write_all(copy, m);
    zmod::models() = zmod::ModelPool{};
    zmod::reset_pool();
    zcls::MechLib lib;
    zutl_zar::init(lib.archive);
    const bool open = zcls::mechlib_open(lib, copy.c_str());
    if (materials) materials_refused += !open || lib.materials.empty();
    zcls::Node* t = open && !materials ? zcls::mechlib_load(lib, target->name.c_str()) : nullptr;
    if (!materials) {
      ++runs;
      t ? ++read : ++refused;
      if (kind == 0 && target->data.size() < before) ++cut, cut_refused += t == nullptr;
    }
    zcls::mechlib_close(lib);
  }
  std::printf("  mechlib fuzz: %d trees, %d refused, %d read; cut short %d, refused %d; materials refused %d\n", runs,
              refused, read, cut, cut_refused, materials_refused);
  EXPECT(runs > 150 && refused > 0 && cut_refused == cut);
}

namespace {
// Every motion in the archive at `path` loaded: how many read, how many refused.
void load_motions(const std::string& path, const std::vector<std::string>& names, int* read, int* refused) {
  *read = *refused = 0;
  if (!mech3::open_motions(path.c_str())) return;
  for (const std::string& n : names) {
    zsis::Script s;
    if (mech3::load_motion(s, n.c_str())) ++*read;
    else ++*refused;
    mech3::script_free_parts(s);
  }
  mech3::close_motions();
}
}  // namespace

TEST(bounded_motion_reads_the_shipped_motions_whole) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/motion.zbd";
  std::error_code ec;
  if (!fs::exists(path, ec)) return;
  std::vector<std::string> names;
  for (const Entry& e : read_all(path)) names.push_back(e.name);
  int read = 0, refused = 0;
  load_motions(path, names, &read, &refused);
  std::printf("  motion: %d of %zu read whole\n", read, names.size());
  EXPECT(names.size() == 258 && read == 258 && refused == 0);
}

TEST(bounded_motion_fuzz_never_crashes) {
  const std::string path = std::string(MW3_GAME_DIR) + "/zbd/motion.zbd";
  std::error_code ec;
  if (!fs::exists(path, ec)) return;
  const std::vector<Entry> all = read_all(path);
  const std::string dir = std::string(MW3_TEST_TMP) + "/bounded_motion";
  fs::create_directories(dir, ec);
  const std::string copy = dir + "/motion.zbd";
  std::mt19937 r(4321);
  int runs = 0, refused = 0, cut = 0, cut_refused = 0;
  for (int seed = 0; seed < 300; ++seed) {
    std::vector<Entry> m = all;
    Entry& e = m[r() % m.size()];
    const size_t before = e.data.size();
    const int kind = seed % 3;
    mutate(e.data, r, kind);
    write_all(copy, m);
    int ok = 0, bad = 0;
    load_motions(copy, {e.name}, &ok, &bad);
    ++runs, refused += bad;
    if (kind == 0 && e.data.size() < before) ++cut, cut_refused += bad;
  }
  std::printf("  motion fuzz: %d motions, %d refused; cut short %d, refused %d\n", runs, refused, cut, cut_refused);
  EXPECT(runs == 300 && refused > 0 && cut_refused == cut);
}

// anim.zbd (a whole file, not an archive): copies of c1's cut short, bytes
// flipped or words made huge, read with no world. Each read refuses the
// file (no defs, its counts 0) or reads every def and fails at the first
// lookup, the original's way; none crash or allocate what a count says.
TEST(bounded_anim_fuzz_never_crashes) {
  const std::string src = std::string(MW3_GAME_DIR) + "/zbd/c1/anim.zbd";
  std::error_code ec;
  if (!fs::exists(src, ec)) return;
  std::vector<uint8_t> all(fs::file_size(src, ec));
  if (std::FILE* f = std::fopen(src.c_str(), "rb")) {
    const size_t n = std::fread(all.data(), 1, all.size(), f);
    all.resize(n);
    std::fclose(f);
  }
  const std::string dir = std::string(MW3_TEST_TMP) + "/bounded_anim";
  fs::create_directories(dir, ec);
  const std::string copy = dir + "/anim.zbd";
  std::mt19937 r(777);
  int runs = 0, refused = 0, cut = 0, cut_refused = 0;
  for (int seed = 0; seed < 150; ++seed) {
    std::vector<uint8_t> d = all;
    const int kind = seed % 3;
    // Past the 12-byte header and the source list, so most changes land in the defs.
    std::vector<uint8_t> head(d.begin(), d.begin() + 12), body(d.begin() + 12, d.end());
    mutate(body, r, kind);
    d = head;
    d.insert(d.end(), body.begin(), body.end());
    if (std::FILE* f = std::fopen(copy.c_str(), "wb")) {
      std::fwrite(d.data(), 1, d.size(), f);
      std::fclose(f);
    }
    zeff::anim() = zeff::AnimState{};
    zeff::anim_allow_loading(1);
    zeff::anim_set_zbd_path(copy.c_str());
    zeff::anim_read_zbd();
    const bool was_refused = zeff::anim().defs == nullptr && zeff::anim().def_count() == 0;
    ++runs, refused += was_refused;
    if (kind == 0 && d.size() < all.size()) ++cut, cut_refused += was_refused;
    zeff::anim_free_all();
  }
  std::printf("  anim fuzz: %d reads, %d refused; cut short %d, refused %d\n", runs, refused, cut, cut_refused);
  EXPECT(runs == 150 && refused > 0 && cut_refused == cut);
  // A source count of 0xfffffff0 in the header: refused before anything is
  // allocated (unbounded, the list alone would be some 360 GB).
  std::vector<uint8_t> d = all;
  const uint32_t huge = 0xfffffff0u;
  std::memcpy(&d[8], &huge, 4);
  if (std::FILE* f = std::fopen(copy.c_str(), "wb")) {
    std::fwrite(d.data(), 1, d.size(), f);
    std::fclose(f);
  }
  zeff::anim() = zeff::AnimState{};
  zeff::anim_allow_loading(1);
  zeff::anim_set_zbd_path(copy.c_str());
  EXPECT(zeff::anim_read_zbd() == -1 && zeff::anim().defs == nullptr);
  zeff::anim_free_all();
}

// A mod's motion.zbd with one motion cut short (docs/modding.md, M3): the
// motion comes from the next copy (the install's), read whole; with no
// other copy it is refused. Every other motion is the mod's own.
TEST(bounded_motion_falls_back_to_the_next_copy) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!fs::exists(install + "/zbd/motion.zbd", ec)) return;
  const zutl::Mounts saved = zutl::mounts();
  const std::string wd = std::string(MW3_TEST_TMP) + "/motion_fallback";
  fs::remove_all(wd, ec);
  fs::create_directories(wd + "/mods/brokenwalk/zbd", ec);
  std::ofstream(wd + "/mods/brokenwalk/mod.txt") << "name=Broken walk\n";
  std::vector<Entry> all = read_all(install + "/zbd/motion.zbd");
  size_t walk_frames = 0;
  for (Entry& e : all)
    if (e.name == "madcat_walk") e.data.resize(e.data.size() / 2);  // cut short: refused
  write_all(wd + "/mods/brokenwalk/zbd/motion.zbd", all);
  auto load = [&](bool with_install, const char* name, uint32_t* frames) {
    zutl::mounts() = zutl::Mounts{};
    zutl::set_write_dir(wd);
    mech3::mods_mount(wd, {"brokenwalk"}, {install});
    if (with_install) zutl::mount(install);
    bool ok = false;
    if (mech3::open_motions("zbd\\motion.zbd")) {
      zsis::Script s;
      ok = mech3::load_motion(s, name);
      *frames = ok ? s.frame_count : 0;
      mech3::script_free_parts(s);
      mech3::close_motions();
    }
    return ok;
  };
  uint32_t frames = 0, whole = 0;
  {  // the install's own, for the frame count
    zutl::mounts() = zutl::Mounts{};
    zutl::mount(install);
    if (mech3::open_motions("zbd\\motion.zbd")) {
      zsis::Script s;
      if (mech3::load_motion(s, "madcat_walk")) whole = s.frame_count;
      mech3::script_free_parts(s);
      mech3::close_motions();
    }
  }
  (void)walk_frames;
  EXPECT(whole > 0);
  EXPECT(load(true, "madcat_walk", &frames) && frames == whole);   // the install's copy
  EXPECT(!mech3::motion_broken("madcat_walk"));
  EXPECT(!load(false, "madcat_walk", &frames));                    // no other copy: refused
  EXPECT(mech3::motion_broken("madcat_walk"));                     // broken, so the mech is refused
  EXPECT(load(false, "madcat_run", &frames) && frames > 0);        // the mod's own others
  // Missing from every copy (the shipped data has no elemental_limp): not
  // broken, so the Elemental keeps the exe's null limp and is not refused.
  EXPECT(!load(true, "elemental_limp", &frames) && !mech3::motion_broken("elemental_limp"));
  zutl::mounts() = saved;
}
