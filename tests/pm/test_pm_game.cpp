// Pirate's Moon's side of the engine (src/pm/pm_game, docs/games-layout.md):
// the game table and telling PM's install from MW3's.
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <fstream>
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
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/list.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmodel/gmod_matl.h"
#include "pm/gamez/zclass/cls_zbd.h"
#include "pm/gamez/zmodel/gmod_const.h"
#include "pm/gamez/zutil/zutl_zar.h"
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
  // A node of c2 links to c2's nodes and models, not c1's.
  int linked = 0, wrong = 0;
  for (int32_t i = nodes; i < ::zcls::nodes().capacity; ++i) {
    const ::zcls::Node* n = ::zcls::node_at(i);
    if (!(n->link & ::zcls::kNodeLive)) continue;
    for (int32_t k = 0; k < n->parent_count(); ++k) {
      ++linked;
      const ::zcls::Node* q = n->parents[k];
      wrong += q < ::zcls::node_at(nodes) && q >= ::zcls::node_at(0);
    }
    if (n->pm_model) wrong += n->pm_model < &pm::zmod::models().at(3000);
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
