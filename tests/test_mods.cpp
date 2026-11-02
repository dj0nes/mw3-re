// Mods (mech3/mods, docs/modding.md): mod.txt, the folders found, the mounts
// in mw3.cfg's order, the player's own files never served from a mod, a
// mod's loose .zrd over its archive's entry, nothing changed without mods.
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "check.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_path.h"
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
  EXPECT(zutl::find("zbd\\a.zbd") == wd + "/mods/two/zbd/a.zbd");     // the first named wins
  EXPECT(zutl::find("zbd\\b.zbd") == wd + "/mods/two/zbd/b.zbd");
  EXPECT(zutl::find("zbd\\new.zbd") == wd + "/mods/one/zbd/new.zbd");  // added
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
