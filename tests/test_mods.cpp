// Mods (mech3/mods, docs/modding.md): mod.txt, the folders found, the mounts
// in mw3.cfg's order, the player's own files never served from a mod, a
// mod's loose .zrd over its archive's entry, nothing changed without mods.
#include <filesystem>
#include <cstdint>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/mission.h"
#include "mech3/mods.h"

namespace {
namespace fs = std::filesystem;
struct SavedMounts {
  zutl::Mounts saved = zutl::mounts();
  ~SavedMounts() { zutl::mounts() = saved; }
};
void put(const fs::path& p, const std::string& text) {
  std::error_code ec;
  fs::create_directories(p.parent_path(), ec);
  std::ofstream(p) << text;
}
// The same file, whatever the separators (Windows' test folder has \\).
bool same(const std::string& a, const std::string& b) {
  std::error_code ec;
  return !a.empty() && fs::equivalent(a, b, ec);
}
std::string tmp(const char* name) {
  const std::string d = std::string(MW3_TEST_TMP) + "/" + name;
  std::error_code ec;
  fs::remove_all(d, ec);
  fs::create_directories(d, ec);
  return d;
}
}  // namespace

TEST(mods_mod_txt) {
  mech3::ModInfo a;
  EXPECT(mech3::mod_parse("# a comment\nname = Hard Rain \nversion=2\r\nauthor=Me\nfor=mw3 1.2\nunknown=1\nnoequals\n", &a));
  EXPECT(a.name == "Hard Rain" && a.version == "2" && a.author == "Me" && a.game == "mw3 1.2" && a.error.empty());
  mech3::ModInfo b;
  EXPECT(!mech3::mod_parse("version=1\n", &b) && !b.error.empty());
  mech3::ModInfo c;
  EXPECT(!mech3::mod_parse("name=X\nfor=pm\n", &c) && c.error.find("pm") != std::string::npos);
  mech3::ModInfo d;
  EXPECT(mech3::mod_parse("name=X\nfor=MW3\n", &d));
}

TEST(mods_mount_in_order_and_game_data_only) {
  SavedMounts keep;
  const std::string root = tmp("mods_mount");
  const std::string wd = root + "/wd", install = root + "/install", patch = wd + "/patch12";
  put(install + "/zbd/a.zbd", "install");
  put(install + "/zbd/b.zbd", "install");
  put(wd + "/mods/one/mod.txt", "name=One\n");
  put(wd + "/mods/one/zbd/a.zbd", "one");
  put(wd + "/mods/one/zbd/new.zbd", "one");
  put(wd + "/mods/one/pilots/me.pil", "one");
  put(wd + "/mods/one/mw3.cfg", "install=/elsewhere\n");
  put(wd + "/mods/two/mod.txt", "name=Two\nversion=3\n");
  put(wd + "/mods/two/zbd/a.zbd", "two");
  put(wd + "/mods/two/zbd/b.zbd", "two");
  put(wd + "/mods/bad/readme.txt", "no mod.txt");
  put(wd + "/pilots/keep.pil", "player");
  const auto found = mech3::mods_found(wd);
  EXPECT(found.size() == 3 && found[0].folder == "bad" && !found[0].error.empty() && found[1].name == "One" &&
         found[2].version == "3");
  // Without mods: nothing mounted beyond the install.
  zutl::mounts() = zutl::Mounts{};
  zutl::set_write_dir(wd);
  EXPECT(mech3::mods_mount(wd, {}, {patch, install}).empty());
  zutl::mount(install);
  EXPECT(zutl::mounts().mods.empty() && zutl::mounts().read.size() == 1);
  EXPECT(zutl::find("zbd\\a.zbd") == install + "/zbd/a.zbd");
  // two, then one (mw3.cfg's order), the unusable and unknown left out.
  zutl::mounts() = zutl::Mounts{};
  zutl::set_write_dir(wd);
  const auto log = mech3::mods_mount(wd, {"two", "one", "bad", "none", "two"}, {patch, install});
  zutl::mount(install);
  std::string text;
  for (const std::string& l : log) text += l + "\n";
  std::printf("%s", text.c_str());
  EXPECT(zutl::mounts().mods.size() == 2 && zutl::mounts().read.size() == 3);
  EXPECT(same(zutl::find("zbd\\a.zbd"), wd + "/mods/two/zbd/a.zbd"));     // the first named wins
  EXPECT(same(zutl::find("zbd\\b.zbd"), wd + "/mods/two/zbd/b.zbd"));
  EXPECT(same(zutl::find("zbd\\new.zbd"), wd + "/mods/one/zbd/new.zbd"));  // added
  EXPECT(zutl::find("pilots\\me.pil").empty());                        // a mod serves no pilot
  EXPECT(zutl::find("pilots\\keep.pil") == wd + "/pilots/keep.pil");   // the player's own
  EXPECT(zutl::find("mw3.cfg").empty() || zutl::find("mw3.cfg").find("/mods/") == std::string::npos);
  EXPECT(zutl::list("pilots\\*.pil") == std::vector<std::string>{"keep.pil"});
  EXPECT(text.find("zbd/a.zbd replaces the game's") != std::string::npos && text.find("zbd/new.zbd added") != std::string::npos &&
         text.find("pilots/me.pil: the player's own kind of file, ignored") != std::string::npos &&
         text.find("mw3.cfg: the player's own") != std::string::npos && text.find("mod bad: no mod.txt") != std::string::npos &&
         text.find("mod none: not in") != std::string::npos);
  // Writes go to the write dir, never into a mod or the install.
  EXPECT(zutl::write_path("zbd\\a.zbd") == wd + "/zbd/a.zbd");
  EXPECT(mech3::mods_found(wd).size() == 3);
}

TEST(mods_loose_zrd_beats_the_archives_entry) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  if (!fs::exists(install + "/zbd/reader.zbd", ec)) return;
  SavedMounts keep;
  const std::string wd = tmp("mods_loose");
  put(wd + "/mods/fmvmod/mod.txt", "name=FMV\n");
  put(wd + "/mods/fmvmod/zbd/fmv.zrd", "(MODDED 1)\n");
  auto marked = [&]() {
    zrdr::init();
    EXPECT(zrdr::open_archive("zbd\\reader.zbd", true));
    zrdr::Node* n = zrdr::load("fmv.zrd");
    const bool mod = n && zrdr::find(n, "MODDED") != nullptr;
    const bool intro = n && zrdr::find(n, "INTRO") != nullptr;
    if (n) zrdr::free_node(n);
    zrdr::close_archives(true);
    return mod ? 1 : intro ? 0 : -1;
  };
  zutl::mounts() = zutl::Mounts{};
  zutl::set_write_dir(wd);
  zutl::mount(install);
  EXPECT(marked() == 0);  // no mods: the archive's own
  zutl::mounts() = zutl::Mounts{};
  zutl::set_write_dir(wd);
  mech3::mods_mount(wd, {"fmvmod"}, {install});
  zutl::mount(install);
  EXPECT(marked() == 1);  // the mod's loose copy instead
  // A broken loose copy: the archive's entry after all.
  put(wd + "/mods/fmvmod/zbd/fmv.zrd", "(BROKEN\n");
  EXPECT(marked() == 0);
}

TEST(mods_gameplay_hash_ignores_looks_and_words) {
  const std::string wd = tmp("mods_hash");
  put(wd + "/mods/tex/mod.txt", "name=Textures\n");
  put(wd + "/mods/tex/zbd/rmechtex.zbd", "pixels");
  put(wd + "/mods/tex/zbd/sounds.zbd", "noise");
  put(wd + "/mods/tex/strings.txt", "1234=Hallo\n");
  put(wd + "/mods/tex/music/track02.ogg", "music");
  put(wd + "/mods/world/mod.txt", "name=World\n");
  put(wd + "/mods/world/zbd/c1/gamez.zbd", "world");
  put(wd + "/mods/world2/mod.txt", "name=World 2\n");
  put(wd + "/mods/world2/zbd/c1/gamez.zbd", "other world");
  EXPECT(mech3::mods_gameplay_hash(wd, {}) == 0);
  EXPECT(mech3::mods_gameplay_hash(wd, {"tex"}) == 0);  // looks and words only
  const uint64_t a = mech3::mods_gameplay_hash(wd, {"world"}), b = mech3::mods_gameplay_hash(wd, {"world2"});
  EXPECT(a != 0 && b != 0 && a != b);
  EXPECT(mech3::mods_gameplay_hash(wd, {"tex", "world"}) == a);
  EXPECT(mech3::mods_gameplay_hash(wd, {"world", "world2"}) != mech3::mods_gameplay_hash(wd, {"world2", "world"}));
  EXPECT(mech3::mod_file_is_gameplay("zbd/c1/reader.zbd") && mech3::mod_file_is_gameplay("zbd/fmv.zrd") &&
         !mech3::mod_file_is_gameplay("zbd/rtexture.zbd") && !mech3::mod_file_is_gameplay("pilots/x.pil") &&
         !mech3::mod_file_is_gameplay("video/intro.m1v"));
}

TEST(mods_strings_over_the_message_table) {
  SavedMounts keep;
  const std::string wd = tmp("mods_strings");
  put(wd + "/mods/fr/mod.txt", "name=French\n");
  put(wd + "/mods/fr/strings.txt",
      "\xef\xbb\xbf# French\n100=Campagne\n0x65=D\xc3\xa9marrer\nID_QUIT=Quitter\nnot a line\n999999999999=x\n"
      "102=Two\\nlines \\\\ here\n103=\xe2\x98\x83 snowman\n");
  put(wd + "/mods/en/mod.txt", "name=English fixes\n");
  put(wd + "/mods/en/strings.txt", "100=Campaign!\n104=Only here\n");
  zutl::mounts() = zutl::Mounts{};
  zutl::set_write_dir(wd);
  mech3::mods_mount(wd, {"fr", "en"}, {});
  std::map<int32_t, std::string> text = {{100, "CAMPAIGN"}, {101, "START"}, {105, "QUIT"}, {106, "kept"}};
  const std::map<std::string, int32_t> ids = {{"ID_QUIT", 105}};
  std::string log;
  for (const std::string& l : mech3::mods_apply_strings(text, ids)) log += l + "\n";
  std::printf("%s", log.c_str());
  EXPECT(text[100] == "Campagne");                   // the first mod's wins
  EXPECT(text[101] == "D\xe9marrer");                // hex id, UTF-8 to Windows-1252
  EXPECT(text[105] == "Quitter");                    // by the message's name
  EXPECT(text[102] == "Two\r\nlines \\ here");       // \n and \\ .
  EXPECT(text.count(103) == 0);                      // a snowman the game cannot show: left out
  EXPECT(text[104] == "Only here" && text[106] == "kept");
  EXPECT(log.find("line 5: no id= here") != std::string::npos && log.find("line 6: no id= here") != std::string::npos &&
         log.find("line 8: a character the game cannot show") != std::string::npos);
  // No mods: nothing changes.
  zutl::mounts() = zutl::Mounts{};
  std::map<int32_t, std::string> same = {{100, "CAMPAIGN"}};
  EXPECT(mech3::mods_apply_strings(same, ids).empty() && same[100] == "CAMPAIGN");
}

TEST(mods_worlds_declared) {
  mech3::ModInfo m;
  EXPECT(mech3::mod_parse("name=W\nworld=mymap\nworld=other\nia_world=mymap  My  Map \nia_world=c2\n", &m));
  EXPECT((m.worlds == std::vector<std::string>{"mymap", "other"}) && m.ia_worlds.size() == 2 &&
         m.ia_worlds[0].first == "mymap" && m.ia_worlds[0].second == "My  Map" && m.ia_worlds[1].second.empty());
  SavedMounts keep;
  const std::string wd = tmp("mods_worlds");
  put(wd + "/mods/w/mod.txt", "name=W\nworld=good\nworld=nofiles\nworld=../evil\nworld=T1\nia_world=good Good\n"
                              "ia_world=c3\nia_world=nowhere Lost\n");
  put(wd + "/mods/w/zbd/good/gamez.zbd", "x");
  zutl::mounts() = zutl::Mounts{};
  zutl::set_write_dir(wd);
  mech3::mod_worlds().clear(), mech3::mod_ia_worlds().clear();
  std::string log;
  for (const std::string& l : mech3::mods_mount(wd, {"w"}, {})) log += l + "\n";
  EXPECT(mech3::mod_worlds() == std::vector<std::string>{"good"} && mech3::mission_dir_count() == 10 &&
         std::string(mech3::mission_dir(9)) == "good" && std::string(mech3::mission_dir(10)) == "none");
  EXPECT(mech3::mod_ia_worlds().size() == 2 && mech3::mod_ia_worlds()[0].name == "Good" && mech3::mod_ia_worlds()[1].name == "c3");
  EXPECT(log.find("world nofiles: no zbd/nofiles/gamez.zbd") != std::string::npos &&
         log.find("world ../evil: no zbd") != std::string::npos && log.find("world T1: there is a world T1 already") != std::string::npos &&
         log.find("Instant Action world nowhere is no world") != std::string::npos);
  mech3::mod_worlds().clear(), mech3::mod_ia_worlds().clear();
  EXPECT(mech3::mission_dir_count() == 9 && std::string(mech3::mission_dir(9)) == "none");
}

// M3: a reader archive's broken entries are refused, not read on into the
// next ones or allocated as their counts say; good ones read as before.
TEST(mods_broken_reader_entries_are_refused) {
  SavedMounts keep;
  const std::string dir = tmp("mods_broken_zrd");
  const std::string path = dir + "/broken.zbd";
  auto le = [](std::vector<uint8_t>& v, int32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
  };
  // (list of 3: int 7, string "ok")
  std::vector<uint8_t> good;
  le(good, 4), le(good, 3), le(good, 1), le(good, 7), le(good, 3), le(good, 2), good.push_back('o'), good.push_back('k');
  std::vector<uint8_t> huge;  // a list claiming a billion children
  le(huge, 4), le(huge, 1000000000), le(huge, 1), le(huge, 7);
  std::vector<uint8_t> type;  // a node of type 9
  le(type, 4), le(type, 2), le(type, 9), le(type, 0);
  std::vector<uint8_t> deep;  // 100 nested lists of 2
  for (int i = 0; i < 100; ++i) le(deep, 4), le(deep, 2);
  le(deep, 1), le(deep, 0);
  std::vector<uint8_t> past;  // a list of 3 holding one int: the rest is the next entry's
  le(past, 4), le(past, 3), le(past, 1), le(past, 5);
  zutl_zar::Archive a;
  zutl_zar::init(a);
  EXPECT(zutl_zar::create(a, path.c_str()));
  zutl_zar::add(a, "good.zrd", good.data(), static_cast<uint32_t>(good.size()));
  zutl_zar::add(a, "huge.zrd", huge.data(), static_cast<uint32_t>(huge.size()));
  zutl_zar::add(a, "type.zrd", type.data(), static_cast<uint32_t>(type.size()));
  zutl_zar::add(a, "deep.zrd", deep.data(), static_cast<uint32_t>(deep.size()));
  zutl_zar::add(a, "past.zrd", past.data(), static_cast<uint32_t>(past.size()));
  zutl_zar::add(a, "after.zrd", good.data(), static_cast<uint32_t>(good.size()));
  EXPECT(zutl_zar::close(a));
  zutl_zar::destroy(a);
  zutl::mounts() = zutl::Mounts{};
  zrdr::init();
  EXPECT(zrdr::open_archive(path.c_str(), true));
  zrdr::Node* g = zrdr::load("good.zrd");
  EXPECT(g && zrdr::list_count(g) == 3 && g->list[1].i == 7 && std::string(g->list[2].s) == "ok");
  if (g) zrdr::free_node(g);
  for (const char* bad : {"huge.zrd", "type.zrd", "deep.zrd", "past.zrd"}) {
    zrdr::Node* n = zrdr::load(bad);
    std::printf("  %s: %s\n", bad, n ? "read" : "refused");
    EXPECT(n == nullptr);
    if (n) zrdr::free_node(n);
  }
  zrdr::Node* after = zrdr::load("after.zrd");  // the reader is fine after them
  EXPECT(after && zrdr::list_count(after) == 3);
  if (after) zrdr::free_node(after);
  zrdr::close_archives(true);
}

// M3: mw3 --check-mod's report on a good mod and on one with every kind of
// fault the game would refuse.
TEST(mods_check_mod_reports_what_the_game_would_refuse) {
  const std::string install = MW3_GAME_DIR;
  std::error_code ec;
  SavedMounts keep;
  const std::string root = tmp("mods_check");
  // Good: a world reader copied from the install, a loose .zrd, strings.
  const std::string good = root + "/good";
  put(good + "/mod.txt", "name=Good\nversion=1\nia_world=c1 Again\n");
  put(good + "/zbd/fmv.zrd", "(INTRO (PLAYAVI intro))\n");
  put(good + "/strings.txt", "100=Hello\n");
  if (fs::exists(install + "/zbd/c1/readerm1.zbd", ec)) {
    fs::create_directories(good + "/zbd/c1", ec);
    fs::copy_file(install + "/zbd/c1/readerm1.zbd", good + "/zbd/c1/readerm1.zbd", ec);
  }
  int errors = -1;
  std::string text;
  for (const std::string& l : mech3::mod_check(good, install, &errors)) text += l + "\n";
  std::printf("%s", text.c_str());
  EXPECT(errors == 0 && text.find("mod.txt: Good 1") != std::string::npos && text.find("zbd/fmv.zrd: added") != std::string::npos);
  if (fs::exists(install + "/zbd/c1/readerm1.zbd", ec))
    EXPECT(text.find("zbd/c1/readerm1.zbd: replaces the game's") != std::string::npos && text.find("0 errors") != std::string::npos);
  // Bad: no name, a world without files, an unknown Instant Action world, a
  // garbage archive, a reader archive with a broken entry, a broken .zrd.
  const std::string bad = root + "/bad";
  put(bad + "/mod.txt", "version=1\nworld=nofiles\nia_world=nowhere\n");
  put(bad + "/zbd/rimage.zbd", "not an archive at all");
  put(bad + "/zbd/broken.zrd", "(UNCLOSED\n");
  put(bad + "/pilots/x.pil", "x");
  {
    std::vector<uint8_t> huge = {4, 0, 0, 0, 0, 0xca, 0x9a, 0x3b};  // a list of a billion
    zutl_zar::Archive a;
    zutl_zar::init(a);
    fs::create_directories(bad + "/zbd/c1", ec);
    EXPECT(zutl_zar::create(a, (bad + "/zbd/c1/readerm1.zbd").c_str()));
    zutl_zar::add(a, "mission.zrd", huge.data(), static_cast<uint32_t>(huge.size()));
    EXPECT(zutl_zar::close(a));
    zutl_zar::destroy(a);
  }
  {  // a mechlib whose materials table is cut short, a motion of 4 bytes
    zutl_zar::Archive a;
    zutl_zar::init(a);
    EXPECT(zutl_zar::create(a, (bad + "/zbd/mechlib.zbd").c_str()));
    const uint32_t version = 27, format = 1, count = 5;
    zutl_zar::add(a, "version", &version, 4);
    zutl_zar::add(a, "format", &format, 4);
    zutl_zar::add(a, "materials", &count, 4);
    EXPECT(zutl_zar::close(a));
    zutl_zar::destroy(a);
    zutl_zar::init(a);
    EXPECT(zutl_zar::create(a, (bad + "/zbd/motion.zbd").c_str()));
    zutl_zar::add(a, "walk", &count, 4);
    EXPECT(zutl_zar::close(a));
    zutl_zar::destroy(a);
  }
  text.clear();
  for (const std::string& l : mech3::mod_check(bad, install, &errors)) text += l + "\n";
  std::printf("%s", text.c_str());
  EXPECT(errors == 8 && text.find("error: zbd/mechlib.zbd: its version, format or materials cannot be read") != std::string::npos &&
         text.find("error: zbd/motion.zbd: motion walk cannot be read") != std::string::npos);
  EXPECT(text.find("error: mod.txt: mod.txt has no name= line") != std::string::npos &&
         text.find("error: world nofiles: no zbd/nofiles/gamez.zbd") != std::string::npos &&
         text.find("error: ia_world nowhere: no such world") != std::string::npos &&
         text.find("error: zbd/rimage.zbd: not a texture library the game can read") != std::string::npos &&
         text.find("error: zbd/c1/readerm1.zbd: entry mission.zrd cannot be read") != std::string::npos &&
         text.find("error: zbd/broken.zrd: not a text .zrd the reader can read") != std::string::npos &&
         text.find("pilots/x.pil: the player's own kind of file") != std::string::npos);
  EXPECT(mech3::mod_check(root + "/none", install, &errors).size() == 1 && errors == 1);
}
