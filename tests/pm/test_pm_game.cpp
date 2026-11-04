#include <cmath>
// Pirate's Moon's side of the engine (src/pm/pm_game, docs/games-layout.md):
// the game table and telling PM's install from MW3's.
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <fstream>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "check.h"
#include "gamez/zgame/zgame_table.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsiscript/si_script.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/mech_anim.h"
#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_matl.h"
#include "pm/gamez/zclass/cls_zbd.h"
#include "pm/gamez/zmodel/gmod_const.h"
#include "pm/gamez/zutil/zutl_zar.h"
#include "gamez/zinterp/zinterp.h"
#include "pm/gamez/zimage/zimg_texture.h"
#include "pm/gamez/zmodel/gmod_clip3d.h"
#include "pm/pm_game.h"

namespace {
bool have(const char* dir) { return std::filesystem::exists(std::filesystem::path(dir) / "zbd" / "reader.zbd"); }
}  // namespace

TEST(pm_table_is_picked_at_run_time) {
  EXPECT(&zgame::game() == &zgame::mw3_table());
  zgame::set_game(pm::table());
  EXPECT(std::string(zgame::game().name) == "Pirate's Moon");
  EXPECT(std::string(zgame::game().exe) == "Pirate's Moon 1.0");
  zgame::set_game(zgame::mw3_table());
  EXPECT(std::string(zgame::game().name) == "MechWarrior 3");
}

TEST(pm_install_told_from_mw3s) {
  if (!have(MW3PM_GAME_DIR) || !have(MW3_GAME_DIR)) {
    std::printf("  skipped: no PM or MW3 install\n");
    return;
  }
  EXPECT(pm::install_kind(MW3PM_GAME_DIR) == pm::Install::kPm);
  EXPECT(pm::install_kind(MW3_GAME_DIR) == pm::Install::kMw3);
  const std::string tmp = MW3_TEST_TMP;
  EXPECT(pm::install_kind(tmp) == pm::Install::kNone);
  std::filesystem::create_directories(tmp + "/zbd");
  { std::ofstream(tmp + "/zbd/reader.zbd", std::ios::binary) << "short"; }
  EXPECT(pm::install_kind(tmp) == pm::Install::kUnknown);
  { std::ofstream(tmp + "/zbd/reader.zbd", std::ios::binary) << std::string(12, '\0'); }
  EXPECT(pm::install_kind(tmp) == pm::Install::kUnknown);
}

// zArchive 2 (pm/gamez/zutil/zutl_zar): every motion of PM's motion.zbd loads
// through the bounded reader, its span reaching the next entry (the entries
// store a length of 1; mech3ax backfills them the same way), and MW3's
// table refuses the archive.
TEST(pm_motions_load_through_their_spans) {
  const std::string path = std::string(MW3PM_GAME_DIR) + "/zbd/motion.zbd";
  if (!std::filesystem::exists(path)) {
    std::printf("  skipped: no PM install\n");
    return;
  }
  zutl::mounts() = {};
  zutl::mount(MW3PM_GAME_DIR);
  EXPECT(!mech3::open_motions("zbd/motion.zbd"));  // MW3's table: version 1 only
  zgame::set_game(pm::table());
  zutl_zar::Archive a;
  zutl_zar::init(a);
  EXPECT(zutl_zar::open(a, "zbd/motion.zbd"));
  int ones = 0, loaded = 0, spans_ok = 0;
  const long table = static_cast<long>(std::filesystem::file_size(path)) - 12 - a.count * 0x94;
  std::vector<std::string> names;
  for (int i = 0; i < a.count; ++i) {
    ones += a.entries[i].length == 1;
    const uint32_t next = i + 1 < a.count ? a.entries[i + 1].start : static_cast<uint32_t>(table);
    spans_ok += zutl_zar::span(a, i) == next - a.entries[i].start;
    names.emplace_back(a.entries[i].name);
  }
  EXPECT(a.count == 364 && ones == 364 && spans_ok == 364);
  EXPECT(zutl_zar::span(a, -1) == 0 && zutl_zar::span(a, a.count) == 0);
  zutl_zar::close(a);
  zutl_zar::destroy(a);
  EXPECT(mech3::open_motions("zbd/motion.zbd"));
  for (const std::string& n : names) {
    zsis::Script s;
    if (mech3::load_motion(s, n.c_str())) ++loaded;
    mech3::script_free_parts(s);
  }
  mech3::close_motions();
  std::printf("  %d motions, %d loaded\n", static_cast<int>(names.size()), loaded);
  EXPECT(loaded == 364);
  zgame::set_game(zgame::mw3_table());
  zutl::mounts() = {};
}

// A truly broken PM motion is still refused (the bounded reader's rule,
// docs/modding.md): a copy of motion.zbd's first two motions, PM's way (each
// entry's length 1), the first cut to half its bytes. The second loads.
TEST(pm_broken_motion_is_refused) {
  const std::string path = std::string(MW3PM_GAME_DIR) + "/zbd/motion.zbd";
  if (!std::filesystem::exists(path)) {
    std::printf("  skipped: no PM install\n");
    return;
  }
  zgame::set_game(pm::table());
  zutl::mounts() = {};
  zutl::mount(MW3PM_GAME_DIR);
  zutl_zar::Archive src;
  zutl_zar::init(src);
  EXPECT(zutl_zar::open(src, "zbd/motion.zbd") && src.count >= 2);
  std::vector<std::vector<uint8_t>> body(2);
  std::string names[2];
  for (int i = 0; i < 2 && i < src.count; ++i) {
    body[i].resize(zutl_zar::span(src, i));
    std::FILE* f = zutl_zar::seek(src, i, nullptr);
    EXPECT(f && std::fread(body[i].data(), 1, body[i].size(), f) == body[i].size());
    names[i] = src.entries[i].name;
  }
  zutl_zar::close(src);
  zutl_zar::destroy(src);
  body[0].resize(body[0].size() / 2);
  const std::string dir = std::string(MW3_TEST_TMP) + "/pm_motion";
  std::filesystem::create_directories(dir + "/zbd");
  {
    std::ofstream out(dir + "/zbd/motion.zbd", std::ios::binary);
    std::vector<zutl_zar::Entry> table(2);
    uint32_t at = 0;
    for (int i = 0; i < 2; ++i) {
      std::memset(&table[i], 0, sizeof table[i]);
      table[i].start = at, table[i].length = 1;
      std::strncpy(table[i].name, names[i].c_str(), sizeof table[i].name - 1);
      out.write(reinterpret_cast<const char*>(body[i].data()), static_cast<std::streamsize>(body[i].size()));
      at += static_cast<uint32_t>(body[i].size());
    }
    out.write(reinterpret_cast<const char*>(table.data()), sizeof(zutl_zar::Entry) * 2);
    const uint32_t tail[3] = {2, 2, 0};
    out.write(reinterpret_cast<const char*>(tail), sizeof tail);
  }
  zutl::mounts() = {};
  zutl::mount(dir);
  EXPECT(mech3::open_motions("zbd/motion.zbd"));
  zsis::Script cut, whole;
  EXPECT(!mech3::load_motion(cut, names[0].c_str()));
  EXPECT(mech3::motion_broken(names[0].c_str()));
  EXPECT(mech3::load_motion(whole, names[1].c_str()));
  EXPECT(!mech3::motion_broken(names[1].c_str()));
  mech3::script_free_parts(cut);
  mech3::script_free_parts(whole);
  mech3::close_motions();
  zgame::set_game(zgame::mw3_table());
  zutl::mounts() = {};
}

// The loose text files through zrdr::load: PM's table reads an #include in
// place (pm/gamez/zreader/zrdr_main), MW3's skips the word as an unknown
// directive.
TEST(pm_text_files_take_include) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/pm_include";
  std::filesystem::create_directories(dir);
  { std::ofstream(dir + "/top.zrd") << "(A 1\n#include part.zrd\nC 3)\n"; }
  { std::ofstream(dir + "/part.zrd") << "B 2 (D 4)"; }
  zutl::mounts() = {};
  zutl::mount(dir);
  zutl::set_write_dir(dir);
  int32_t v = 0;
  for (const bool pm_game : {true, false}) {
    zgame::set_game(pm_game ? pm::table() : zgame::mw3_table());
    zrdr::state().if_depth = 0;
    zrdr::Node* root = zrdr::load("top.zrd");
    EXPECT(root != nullptr);
    if (!root) continue;
    EXPECT(zrdr::get_int(root, "A", &v) && v == 1);
    EXPECT(zrdr::get_int(root, "C", &v) && v == 3);
    EXPECT(zrdr::get_int(root, "B", &v) == pm_game && (!pm_game || v == 2));
    EXPECT(zrdr::get_int(root, "D", &v) == pm_game && (!pm_game || v == 4));
    zrdr::free_node(root);
  }
}

// An #include reaches only the mounts: a file beside them, named by "..", by
// its absolute path or with backslashes, is not read (zrdr_main.cpp inside_mounts).
TEST(pm_include_stays_inside_the_mounts) {
  const std::string base = std::string(MW3_TEST_TMP) + "/pm_include_out";
  const std::string dir = base + "/game";
  std::filesystem::create_directories(dir + "/sub");
  { std::ofstream(base + "/secret.zrd") << "S 9\n"; }
  { std::ofstream(dir + "/sub/ok.zrd") << "K 5\n"; }
  const std::string abs = std::filesystem::absolute(base + "/secret.zrd").string();
  { std::ofstream(dir + "/top.zrd") << "(A 1\n#include ..\\secret.zrd\n#include sub/../../secret.zrd\n#include "
                                    << abs << "\n#include sub\\ok.zrd\nC 3)\n"; }
  zutl::mounts() = {};
  zutl::mount(dir);
  zutl::set_write_dir(dir);
  zgame::set_game(pm::table());
  zrdr::state().if_depth = 0;
  zrdr::Node* root = zrdr::load("top.zrd");
  EXPECT(root != nullptr);
  if (!root) return;
  int32_t v = 0;
  EXPECT(zrdr::get_int(root, "A", &v) && v == 1);
  EXPECT(zrdr::get_int(root, "C", &v) && v == 3);
  EXPECT(zrdr::get_int(root, "K", &v) && v == 5);
  EXPECT(!zrdr::get_int(root, "S", &v));
  zrdr::free_node(root);
  zgame::set_game(zgame::mw3_table());
}

// The world loader 0x00516bc0 through the game table: c1 read, then c2
// stacked on it (each pool grows by the file's; the file list keeps both
// paths only while reading); a file of the wrong version or signature, or
// MW3's version 27, is refused and leaves the list and the pools alone.
TEST(pm_worlds_stack_through_the_table) {
  const std::string c1 = std::string(MW3PM_GAME_DIR) + "/zbd/c1/gamez.zbd";
  if (!std::filesystem::exists(c1)) {
    std::printf("  skipped: no PM install\n");
    return;
  }
  zutl::mounts() = {};
  zutl::mount(MW3PM_GAME_DIR);
  zgame::set_game(pm::table());
  ::zimg::textures().count = 0;
  ::zmod::materials() = ::zmod::MaterialTable{};
  pm::zmod::models() = pm::zmod::ModelPool{};
  ::zcls::nodes() = ::zcls::NodePool{};
  ::zcls::lists() = ::zcls::Lists{};
  pm::zcls::zbd_state() = pm::zcls::ZbdState{};
  EXPECT(zgame::game().zcls_read_zbd("zbd/c1/gamez.zbd") == 0);
  const int32_t textures = ::zimg::textures().count, materials = ::zmod::materials().capacity;
  const int32_t models = pm::zmod::models().capacity, nodes = ::zcls::nodes().capacity;
  EXPECT(textures == 437 && materials == 1000 && models == 3000 && nodes == 7500);
  EXPECT(pm::zcls::zbd_state().files.size() == 1 && !pm::zcls::zbd_state().file);
  EXPECT(zgame::game().zcls_read_zbd("zbd/c2/gamez.zbd") == 0);
  EXPECT(::zimg::textures().count >= textures);  // c2's found by name, added past c1's
  EXPECT(::zmod::materials().capacity == 2000 && pm::zmod::models().capacity == 3000 + 2250);
  EXPECT(::zcls::nodes().capacity > nodes);
  EXPECT(pm::zcls::zbd_state().files.size() == 2);
  // A node of c2 links to c2's nodes and models, not c1's. By block: a pool's
  // blocks are separate allocations, in no address order.
  auto in_first = [](const auto& blocks, int32_t per_block, int32_t slots, const auto* p) {
    for (int32_t k = 0; k < slots / per_block; ++k)
      if (p >= blocks[k].get() && p < blocks[k].get() + per_block) return true;
    return false;
  };
  int linked = 0, wrong = 0;
  for (int32_t i = nodes; i < ::zcls::nodes().capacity; ++i) {
    const ::zcls::Node* n = ::zcls::node_at(i);
    if (!(n->link & ::zcls::kNodeLive)) continue;
    for (int32_t k = 0; k < n->parent_count(); ++k) {
      ++linked;
      const ::zcls::Node* q = n->parents[k];
      wrong += in_first(::zcls::nodes().blocks, ::zcls::nodes().block_size, nodes, q);
    }
    if (n->pm_model) wrong += in_first(pm::zmod::models().blocks, pm::zmod::models().block, 3000, n->pm_model);
  }
  EXPECT(linked > 100 && wrong == 0);
  // Refused files: the pools and the list stay as they are.
  const std::string dir = std::string(MW3_TEST_TMP) + "/pm_worlds";
  std::filesystem::create_directories(dir + "/zbd");
  std::vector<char> bytes(static_cast<size_t>(std::filesystem::file_size(c1)));  // whole: only the header refuses it
  {
    std::ifstream in(c1, std::ios::binary);
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  }
  for (const auto& [name, off, value] : {std::tuple<const char*, int, int32_t>{"version27.zbd", 4, 27},
                                         {"version40.zbd", 4, 40}, {"signature.zbd", 0, 0x12345678}}) {
    std::vector<char> b = bytes;
    std::memcpy(b.data() + off, &value, 4);
    std::ofstream(dir + "/zbd/" + name, std::ios::binary).write(b.data(), static_cast<std::streamsize>(b.size()));
    EXPECT(zgame::game().zcls_read_zbd((dir + "/zbd/" + name).c_str()) == -1);
  }
  EXPECT(zgame::game().zcls_read_zbd("") == -1);
  EXPECT(zgame::game().zcls_read_zbd("zbd/none/gamez.zbd") == -1);
  EXPECT(pm::zcls::zbd_state().files.size() == 2 && ::zmod::materials().capacity == 2000);
  zgame::set_game(zgame::mw3_table());
}

namespace {
struct ZarEntry {
  std::string name;
  std::vector<uint8_t> data;
};
std::vector<ZarEntry> read_zar(const std::string& path) {
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
void write_zar(const std::string& path, const std::vector<ZarEntry>& entries) {
  zutl_zar::Archive a;
  zutl_zar::init(a);
  zutl_zar::create(a, path.c_str());
  for (const ZarEntry& e : entries) zutl_zar::add(a, e.name.c_str(), e.data.data(), static_cast<uint32_t>(e.data.size()));
  zutl_zar::close(a);
  zutl_zar::destroy(a);
}
bool is_mechlib_tree(const std::string& n) { return n != "version" && n != "format" && n != "materials"; }
void reset_pm_pools() {
  ::zimg::textures().count = 0;
  ::zmod::materials() = ::zmod::MaterialTable{};
  pm::zmod::models() = pm::zmod::ModelPool{};
  ::zcls::nodes() = ::zcls::NodePool{};
  ::zcls::lists() = ::zcls::Lists{};
}
}  // namespace

// PM's mechlib.zbd 41 through the table, each tree bounded by its entry
// (docs/pm-map.md, mechlib 41): all 76 read whole; MW3's file refused.
TEST(pm_mechlib_reads_the_shipped_trees_whole) {
  const std::string path = std::string(MW3PM_GAME_DIR) + "/zbd/mechlib.zbd";
  if (!std::filesystem::exists(path)) {
    std::printf("  skipped: no PM install\n");
    return;
  }
  zgame::set_game(pm::table());
  reset_pm_pools();
  ::zcls::MechLib lib;
  zutl_zar::init(lib.archive);
  EXPECT(zgame::game().zcls_mechlib_open(lib, path.c_str()));
  EXPECT(lib.materials.size() == 550);
  int trees = 0, loaded = 0, lods = 0;
  for (const ZarEntry& e : read_zar(path)) {
    if (!is_mechlib_tree(e.name)) continue;
    ++trees;
    loaded += zgame::game().zcls_mechlib_load(lib, e.name.c_str()) != nullptr;
  }
  for (int32_t i = 0; i < ::zcls::nodes().capacity; ++i)
    lods += ::zcls::node_at(i)->cls() == 6;  // a free slot's class is 0
  std::printf("  pm mechlib: %d trees, %d loaded, %zu kept, %d LODs\n", trees, loaded, lib.trees.size(), lods);
  EXPECT(trees == 76 && loaded == 76 && lib.trees.size() == 76 && lods == 2);
  ::zcls::mechlib_close(lib);
  const std::string mw3 = std::string(MW3_GAME_DIR) + "/zbd/mechlib.zbd";
  if (std::filesystem::exists(mw3)) {
    ::zcls::MechLib other;
    zutl_zar::init(other.archive);
    EXPECT(!zgame::game().zcls_mechlib_open(other, mw3.c_str()));  // version 27
    zutl_zar::destroy(other.archive);
  }
  zutl_zar::destroy(lib.archive);
  zgame::set_game(zgame::mw3_table());
}

// Cut, flipped and garbled copies of PM's trees: refused or read, never a
// crash; a tree cut short is always refused.
TEST(pm_mechlib_fuzz_never_crashes) {
  const std::string path = std::string(MW3PM_GAME_DIR) + "/zbd/mechlib.zbd";
  if (!std::filesystem::exists(path)) {
    std::printf("  skipped: no PM install\n");
    return;
  }
  zgame::set_game(pm::table());
  const std::vector<ZarEntry> all = read_zar(path);
  std::vector<size_t> trees;
  for (size_t i = 0; i < all.size(); ++i)
    if (is_mechlib_tree(all[i].name)) trees.push_back(i);
  const std::string dir = std::string(MW3_TEST_TMP) + "/pm_mechlib";
  std::filesystem::create_directories(dir);
  const std::string copy = dir + "/mechlib.zbd";
  std::mt19937 r(41);
  int runs = 0, refused = 0, read = 0, cut = 0, cut_refused = 0, bad_polys = 0;
  for (int seed = 0; seed < 180; ++seed) {
    std::vector<ZarEntry> m = all;
    ZarEntry& target = m[trees[r() % trees.size()]];
    std::vector<uint8_t>& d = target.data;
    const size_t before = d.size();
    const int kind = seed % 3;
    if (kind == 0) {
      d.resize(r() % d.size());
    } else if (kind == 1) {
      for (int n = 1 + r() % 8; n > 0; --n) d[r() % d.size()] ^= static_cast<uint8_t>(1 + r() % 255);
    } else {
      for (int n = 1 + r() % 4; n > 0 && d.size() >= 4; --n) {
        const size_t at = (r() % (d.size() / 4)) * 4;
        const uint32_t v = r() % 2 ? 0xffffffffu - r() % 16 : 0x10000u + r() % 0x7fffffffu;
        std::memcpy(&d[at], &v, 4);
      }
    }
    write_zar(copy, m);
    reset_pm_pools();
    ::zcls::MechLib lib;
    zutl_zar::init(lib.archive);
    EXPECT(zgame::game().zcls_mechlib_open(lib, copy.c_str()));
    ::zcls::Node* t = zgame::game().zcls_mechlib_load(lib, target.name.c_str());
    ++runs;
    t ? ++read : ++refused;
    if (kind == 0 && d.size() < before) ++cut, cut_refused += t == nullptr;
    // A refused tree stays in the pool: what is left of it must be whole
    // (its model empty once broken), every polygon's indices in its arrays.
    pm::zmod::ModelPool& pool = pm::zmod::models();
    for (int32_t i = 0; i < pool.capacity; ++i) {
      const pm::zmod::Model& model = pool.at(i);
      for (int32_t k = 0; k < model.count(0x10); ++k) {
        const pm::zmod::Polygon& q = model.polygons[k];
        const uint32_t n = q.flags & 0x3ff;
        bad_polys += q.material_count < 1 || (n && !q.vertex_indices);
        for (uint32_t j = 0; j < n && q.vertex_indices; ++j)
          bad_polys += q.vertex_indices[j] >= static_cast<uint32_t>(model.count(0x14));
      }
    }
    ::zcls::mechlib_close(lib);
    zutl_zar::destroy(lib.archive);
  }
  std::printf("  pm mechlib fuzz: %d trees, %d refused, %d read; cut short %d, refused %d\n", runs, refused, read, cut,
              cut_refused);
  EXPECT(runs == 180 && refused > 0 && cut == cut_refused && bad_polys == 0);
  zgame::set_game(zgame::mw3_table());
}

// LightMapTexture (PM's 0x005b2d10): a word of PM's table only; MW3's counts
// it unknown and stores nothing. The textures' free (0x00530810) clears it.
TEST(pm_light_map_texture_is_pms_alone) {
  static ::zimg::TextureEntry entry;
  ::zint::Engine keep = ::zint::engine();
  ::zint::engine().texture_add = [](const char*) { return &entry; };
  auto run = [] {
    char line[] = "LightMapTexture\0lightmap";
    ::zint::Interp in;
    in.argc = 2, in.argv[0] = line, in.argv[1] = line + 16, in.cursor = 1;
    ::zint::dispatch(in, in.argv[0]);
    return in.misses;
  };
  pm::zimg::set_light_map(nullptr);
  zgame::set_game(zgame::mw3_table());
  EXPECT(run() == 1 && pm::zimg::light_map() == nullptr);
  zgame::set_game(pm::table());
  EXPECT(run() == 0 && pm::zimg::light_map() == &entry);
  pm::zimg::textures_free();
  EXPECT(pm::zimg::light_map() == nullptr);
  ::zint::engine() = keep;
  zgame::set_game(zgame::mw3_table());
}

// FIX (bug), the port alone (the exe overruns its 64-vertex scratch here): the
// worst polygon, 1,023 vertices zigzagging across the near plane, comes out
// whole at 1,534 with every vertex in place, on the clipper carrying the most.
TEST(pm_clip3d_keeps_the_largest_polygon_whole) {
  constexpr int kIn = 1023, kOut = 1534;
  std::vector<float> pos(kIn * 3), rec(kIn * 8), tl(kIn * 8), a_pos(kOut * 3), a_rec(kOut * 8), a_tl(kOut * 8);
  std::vector<uint8_t> extra(kIn), a_extra(kOut);
  pm::zmod::ClipShadows sets;
  sets.count = 7;
  for (int s = 0; s < 7; ++s) {
    sets.kind[s] = 2;
    sets.uv[s].assign(kIn * 2, 0.5f);
    sets.xyz[s].assign(kIn * 3, 2.0f);
  }
  for (int i = 0; i < kIn; ++i) {
    pos[i * 3] = static_cast<float>(i), pos[i * 3 + 2] = i % 2 ? -2.0f : 2.0f;  // odd: behind
    for (int k = 0; k < 8; ++k) rec[i * 8 + k] = 7.0f;
    tl[i * 8 + 6] = tl[i * 8 + 7] = 3.0f;
  }
  pm::zmod::ClipPoly p;
  p.pos = pos.data(), p.rec = rec.data(), p.tl = tl.data(), p.extra = extra.data(), p.shadows = &sets, p.count = kIn;
  pm::zmod::Clip3dState& s = pm::zmod::clip3d_state();
  s.near_z = 0, s.arena_pos = a_pos.data(), s.arena_rec = a_rec.data(), s.arena_tl = a_tl.data(), s.arena_extra = a_extra.data();
  EXPECT(pm::zmod::clip3d_textured_lit_shadow(p) == 1);
  EXPECT(p.count == kOut && p.pos == a_pos.data() && s.arena_pos == a_pos.data() + kOut * 3);
  int on_plane = 0, in_front = 0, wrong = 0;
  for (int i = 0; i < kOut; ++i) {
    on_plane += a_pos[i * 3 + 2] == 0.0f, in_front += a_pos[i * 3 + 2] == 2.0f;
    wrong += a_rec[i * 8] != 7.0f || a_tl[i * 8 + 7] != 3.0f;
    for (int k = 0; k < 7; ++k) wrong += sets.uv[k][i * 2 + 1] != 0.5f || sets.xyz[k][i * 3 + 2] != 2.0f;
  }
  EXPECT(on_plane == 1022 && in_front == 512 && wrong == 0);
  EXPECT(sets.uv[6].size() == kOut * 2 && sets.xyz[6].size() == kOut * 3);
  std::printf("  pm clip3d: %d in, %d out: %d cuts, %d in front\n", kIn, p.count, on_plane, in_front);
}

// The same for the side clipper, the port alone: a zigzag past the left side,
// then a star across all four sides, on the path carrying the most.
TEST(pm_clip2d_keeps_large_polygons_whole) {
  constexpr int kIn = 1023, kRoom = 8000;
  std::vector<float> rec(kIn * 8, 7.0f), tl(kIn * 8), a_rec(kRoom * 8), a_tl(kRoom * 8);
  std::vector<uint8_t> extra(kIn), a_extra(kRoom);
  pm::zmod::ClipShadows sets;
  sets.count = 7;
  for (int s = 0; s < 7; ++s) {
    sets.kind[s] = 2;
    sets.uv[s].assign(kIn * 2, 0.5f);
    sets.xyz[s].assign(kIn * 3, 2.0f);
  }
  pm::zmod::Clip3dState& st = pm::zmod::clip3d_state();
  st.left = 0, st.right = 640, st.top = 0, st.bottom = 480;
  auto run = [&](uint32_t codes, auto place) {
    for (int i = 0; i < kIn; ++i) {
      place(i, &tl[i * 8]);
      tl[i * 8 + 2] = 0.5f, tl[i * 8 + 6] = tl[i * 8 + 7] = 3.0f;
    }
    st.arena_rec = a_rec.data(), st.arena_tl = a_tl.data(), st.arena_extra = a_extra.data();
    pm::zmod::ClipPoly p;
    p.rec = rec.data(), p.tl = tl.data(), p.extra = extra.data(), p.shadows = &sets, p.count = kIn, p.codes = codes;
    EXPECT(pm::zmod::clip2d_textured_lit_shadow(p) == 1);
    int wrong = 0;
    for (int i = 0; i < p.count; ++i) {
      auto off = [](float v, float want) { return !(std::fabs(v - want) < 1e-3f); };  // blended: within rounding
      wrong += off(p.rec[i * 8], 7.0f) || off(p.tl[i * 8 + 7], 3.0f);
      wrong += off(sets.uv[6][i * 2 + 1], 0.5f) || off(sets.xyz[6][i * 3 + 2], 2.0f);
    }
    EXPECT(wrong == 0 && p.tl == a_tl.data() && sets.uv[6].size() >= static_cast<size_t>(p.count) * 2);
    return p.count;
  };
  const int zigzag = run(1, [](int i, float* v) { v[0] = i % 2 ? -10.0f : 10.0f, v[1] = 240.0f; });
  EXPECT(zigzag == 1534);
  const int star = run(0xf, [](int i, float* v) {
    v[0] = i % 2 ? -10.0f : 650.0f, v[1] = i % 4 < 2 ? -10.0f : 490.0f;
  });
  EXPECT(star >= 3);
  std::printf("  pm clip2d: %d in, zigzag %d out, star %d out\n", kIn, zigzag, star);
}

// A model with no polygons and a vertex count past the tree's end: refused, and
// what stays in the pool has no counts (the arrays are a single element each).
TEST(pm_mechlib_refused_model_keeps_no_counts) {
  const std::string path = std::string(MW3PM_GAME_DIR) + "/zbd/mechlib.zbd";
  if (!std::filesystem::exists(path)) {
    std::printf("  skipped: no PM install\n");
    return;
  }
  zgame::set_game(pm::table());
  std::vector<ZarEntry> all = read_zar(path);
  size_t tree = 0;
  while (!is_mechlib_tree(all[tree].name)) ++tree;
  auto try_count = [&](int off) {
    std::vector<uint8_t> d = all[tree].data;
    d.resize(0xd0 + 0x90 + 0x64);
    std::memset(d.data() + 0xd0 + 0x90, 0, 0x64);
    const int32_t cls = 5, has_model = 1;
    const int16_t children = 0;
    std::memcpy(d.data() + 0x34, &cls, 4);
    std::memcpy(d.data() + 0x3c, &has_model, 4);
    std::memcpy(d.data() + 0x56, &children, 2);
    const int32_t huge = 0x1000000;
    std::memcpy(d.data() + 0xd0 + 0x90 + off, &huge, 4);
    std::vector<ZarEntry> m = all;
    m[tree].data = d;
    const std::string dir = std::string(MW3_TEST_TMP) + "/pm_mechlib_counts";
    std::filesystem::create_directories(dir);
    write_zar(dir + "/mechlib.zbd", m);
    reset_pm_pools();
    ::zcls::MechLib lib;
    zutl_zar::init(lib.archive);
    EXPECT(zgame::game().zcls_mechlib_open(lib, (dir + "/mechlib.zbd").c_str()));
    EXPECT(zgame::game().zcls_mechlib_load(lib, m[tree].name.c_str()) == nullptr);
    int models = 0, counted = 0;
    pm::zmod::ModelPool& pool = pm::zmod::models();
    // Fresh pool: the one allocated slot is the first.
    models = pool.count;
    if (pool.capacity > 0)
      for (int c : {0x10, 0x14, 0x18, 0x1c, 0x5c}) counted += pool.at(0).count(c) != 0;
    EXPECT(models == 1 && counted == 0);
    ::zcls::mechlib_close(lib);
    zutl_zar::destroy(lib.archive);
  };
  for (int off : {0x14, 0x18, 0x1c}) try_count(off);
  zgame::set_game(zgame::mw3_table());
}

// 0x00558f50's fresh model: flags 3, the rest clear. The only ported caller
// replaces the header; the others are to check as they are ported (pm-map.md).
TEST(pm_model_alloc_starts_with_flags_3) {
  pm::zmod::models() = pm::zmod::ModelPool{};
  const pm::zmod::Model* m = pm::zmod::model_alloc();
  EXPECT(m != nullptr);
  uint32_t flags = 0;
  std::memcpy(&flags, m->header + 8, 4);
  EXPECT(flags == 3 && m->count(0x10) == 0 && m->count(0x14) == 0);
  pm::zmod::models() = pm::zmod::ModelPool{};
}
