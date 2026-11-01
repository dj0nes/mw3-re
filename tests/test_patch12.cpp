// The 1.2 data layer (mech3/patch12, docs/patch12.md): 1.0's files, with the
// layer, parse as 1.2's.
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>
#include <vector>
#include <string>

#include "check.h"
#include "gamez/zclass/cls_zbd.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/patch12.h"
#include "mech3/patch12_tree.h"
#include "platform/silent_sound.h"

namespace {

const std::string kRoot = std::string(MW3_SOURCE_DIR) + "/game";

bool have_both() {
  return std::filesystem::exists(kRoot + "/mw3-1.0/zbd/reader.zbd") &&
         std::filesystem::exists(kRoot + "/mw3-1.2/zbd/reader.zbd");
}

// One entry of an archive file, read as zrdr::load reads it; null if absent.
zrdr::Node* read_entry(const std::string& archive, const char* entry) {
  zutl_zar::Archive a;
  zutl_zar::init(a);
  zrdr::Node* root = nullptr;
  if (zutl_zar::open(a, archive.c_str()))
    if (std::FILE* f = zutl_zar::seek(a, zutl_zar::find(a, entry), nullptr)) {
      root = zrdr::new_nodes(zrdr::List, 1);
      zrdr::read_node(f, root);
    }
  zutl_zar::close(a);
  zutl_zar::destroy(a);
  return root;
}

// 1.0's entry with the layer against 1.2's; true when they parse equal.
bool same_as_12(const char* archive, const char* entry, bool* changed) {
  zrdr::Node* old = read_entry(kRoot + "/mw3-1.0/" + archive, entry);
  zrdr::Node* want = read_entry(kRoot + "/mw3-1.2/" + archive, entry);
  bool ok = old && want;
  if (ok) {
    *changed = mech3::patch12::apply(archive, entry, old);
    ok = mech3::patch12::tree::equal(*old, *want);
    if (!ok) std::printf("  %s %s differs:\n    got  %.300s\n    want %.300s\n", archive, entry,
                         mech3::patch12::tree::text(*old).c_str(), mech3::patch12::tree::text(*want).c_str());
  }
  zrdr::free_node(old), zrdr::free_node(want);
  return ok;
}

struct SavedMounts {
  zutl::Mounts saved = zutl::mounts();
  ~SavedMounts() { zutl::mounts() = saved; }
};

}  // namespace

// The message table: every key 1.2 has, with 1.2's text, except where the
// wording is ours (listed); the version line 1.2 dropped is gone.
TEST(patch12_messages_make_10s_table_12s) {
  if (!have_both()) return;
  zutl::MessageDll v10, v12;
  EXPECT(zutl::read_message_dll(kRoot + "/mw3-1.0/Mech3Msg.dll", v10));
  EXPECT(zutl::read_message_dll(kRoot + "/mw3-1.2/Mech3Msg.dll", v12));
  EXPECT(v10.ids != v12.ids);
  mech3::patch12::apply_messages(v10);
  EXPECT(v10.ids == v12.ids);
  int ours = 0, differ = 0;
  for (const auto& [id, text] : v12.text) {
    auto it = v10.text.find(id);
    if (it != v10.text.end() && it->second == text) continue;
    if (mech3::patch12::message_is_ours(id)) {
      ++ours;
      continue;
    }
    ++differ;
    std::printf("  message %#x: %s, want %s\n", id, it == v10.text.end() ? "missing" : it->second.c_str(), text.c_str());
  }
  EXPECT(differ == 0);
  EXPECT(v10.text.size() == v12.text.size());
  std::printf("  %zu messages as 1.2's; %d in our own words\n", v12.text.size() - ours, ours);
}

// The reader entries this sitting covers parse as 1.2's.
TEST(patch12_reader_entries_parse_as_12s) {
  if (!have_both()) return;
  struct {
    const char *archive, *entry;
  } const kCases[] = {
      {"zbd/reader.zbd", "hud.zrd"},
      {"zbd/reader.zbd", "weapons.zrd"},
      {"zbd/reader.zbd", "dfn_annihilator.zrd"}, {"zbd/reader.zbd", "dfn_avatar.zrd"},
      {"zbd/reader.zbd", "dfn_blackhawk.zrd"},   {"zbd/reader.zbd", "dfn_bushwacker.zrd"},
      {"zbd/reader.zbd", "dfn_cauldron.zrd"},    {"zbd/reader.zbd", "dfn_champion.zrd"},
      {"zbd/reader.zbd", "dfn_daishi.zrd"},      {"zbd/reader.zbd", "dfn_madcat.zrd"},
      {"zbd/reader.zbd", "dfn_orion.zrd"},       {"zbd/reader.zbd", "dfn_owens.zrd"},
      {"zbd/reader.zbd", "dfn_puma.zrd"},        {"zbd/reader.zbd", "dfn_shadowcat.zrd"},
      {"zbd/reader.zbd", "dfn_strider.zrd"},     {"zbd/reader.zbd", "dfn_sunder.zrd"},
      {"zbd/reader.zbd", "dfn_supernova.zrd"},   {"zbd/reader.zbd", "dfn_thor.zrd"},
      {"zbd/reader.zbd", "dfn_vulture.zrd"},
      {"zbd/reader.zbd", "briefing.zrd"},        {"zbd/reader.zbd", "dialog.zrd"},
      {"zbd/reader.zbd", "sounds.zrd"},          {"zbd/reader.zbd", "mechsoilfx.zrd"},
      {"zbd/c1/reader.zbd", "objectives.zrd"},   {"zbd/c2/reader.zbd", "start_multi_c2.zrd"},
      {"zbd/c2/reader.zbd", "world.zrd"},        {"zbd/c2/readerm3.zbd", "aiv.zrd"},
      {"zbd/c2/readerm3.zbd", "targets.zrd"},    {"zbd/c3/readerm2.zbd", "targets.zrd"},
      {"zbd/c3/readerm3.zbd", "reg_inside_c3m3.zrd"}, {"zbd/c3/readerm3.zbd", "reg_outside_c3m3.zrd"},
      {"zbd/c3/readerm4.zbd", "targets.zrd"},    {"zbd/c3/readerm5.zbd", "targets.zrd"},
      {"zbd/c3/readerm6.zbd", "targets.zrd"},    {"zbd/c4b/readerm4.zbd", "aiv.zrd"},
      {"zbd/c4b/readerm4.zbd", "targets.zrd"},   {"zbd/c4b/readerm4.zbd", "net_09.zrd"},
      {"zbd/c4b/readerm5.zbd", "targets.zrd"},
  };
  for (const auto& c : kCases) {
    bool changed = false;
    EXPECT(same_as_12(c.archive, c.entry, &changed));
    EXPECT(changed);
  }
  // 1.2's own entries are left alone: the layer acts only on 1.0's files
  // (by CRC), so with 1.2 mounted the hook changes none of them.
  SavedMounts keep;
  zutl::mounts() = zutl::Mounts{};
  zutl::mount(kRoot + "/mw3-1.2");
  mech3::patch12::forget_files();
  for (const auto& c : kCases) {
    zrdr::Node* e12 = read_entry(kRoot + "/mw3-1.2/" + c.archive, c.entry);
    std::string opened = c.archive;
    for (char& ch : opened) ch = ch == '/' ? '\\' : ch;
    EXPECT(!mech3::patch12::on_load(opened.c_str(), c.entry, e12));
    zrdr::free_node(e12);
  }
  mech3::patch12::forget_files();
}

// The layer acts on 1.0's files only: by size and CRC, through the mounts.
TEST(patch12_acts_on_10s_files_only) {
  if (!have_both()) return;
  SavedMounts keep;
  for (const char* v : {"mw3-1.0", "mw3-1.2"}) {
    zutl::mounts() = zutl::Mounts{};
    zutl::mount(kRoot + "/" + v);
    mech3::patch12::forget_files();
    const bool ten = std::string(v) == "mw3-1.0";
    EXPECT(mech3::patch12::is_10("zbd\\reader.zbd") == ten);
    EXPECT(mech3::patch12::is_10("Mech3Msg.dll") == ten);
    EXPECT(!mech3::patch12::is_10("zbd\\c1\\readerm1.zbd"));  // not one the patch changed
    zrdr::Node* hud = read_entry(kRoot + "/" + v + "/zbd/reader.zbd", "hud.zrd");
    EXPECT(mech3::patch12::on_load("zbd\\reader.zbd", "HUD.ZRD", hud) == ten);
    zrdr::free_node(hud);
    // The game's own path: the archive opened as the game opens it, the
    // entry through zrdr::load and the installed hook.
    mech3::patch12::install();
    zrdr::init();
    zrdr::close_archives(true);
    EXPECT(zrdr::open_archive("zbd\\reader.zbd", true));
    zrdr::Node* loaded = zrdr::load("hud.zrd");
    EXPECT(loaded && zrdr::find(loaded, "V1600x1200"));
    zrdr::free_node(loaded);
    zrdr::close_archives(true);
    mech3::patch12::uninstall();
  }
  mech3::patch12::forget_files();
}

// force_eff.ifr: 1.0's with the layer has 1.2's effects, record for record
// (names, types and values; the IDs are ours, so a compound's parts are
// compared by name), and the file's own format reads back.
TEST(patch12_force_effects_make_10s_12s) {
  if (!have_both()) return;
  auto slurp = [](const std::string& path) {
    std::vector<uint8_t> d;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
      uint8_t buf[4096];
      for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
      std::fclose(f);
    }
    return d;
  };
  std::vector<uint8_t> v10 = slurp(kRoot + "/mw3-1.0/force_eff.ifr"), v12 = slurp(kRoot + "/mw3-1.2/force_eff.ifr");
  std::vector<mech3::patch12::IfrRecord> a, b;
  EXPECT(mech3::patch12::read_ifr(v12, &b));
  EXPECT(mech3::patch12::write_ifr(b) == v12);  // the writer writes the format back byte for byte
  std::vector<uint8_t> got = v10;
  EXPECT(mech3::patch12::apply_force(got));
  EXPECT(!mech3::patch12::apply_force(v12));  // 1.2's left alone
  EXPECT(mech3::patch12::read_ifr(got, &a));
  auto canon = [](const std::vector<mech3::patch12::IfrRecord>& rs) {
    std::map<std::string, std::string> name_of;
    for (const auto& r : rs)
      for (const auto& kv : r.values)
        if (kv.first == "ID") name_of[kv.second] = r.name;
    std::vector<std::string> out;
    for (const auto& r : rs) {
      std::map<std::string, std::string> v(r.values.begin(), r.values.end());
      v.erase("ID");
      std::string parts, s = v["ContainedObjects"];
      for (size_t i = 0; !s.empty() && i <= s.size();) {
        const size_t j = std::min(s.find(';', i), s.size());
        parts += name_of[s.substr(i, j - i)] + ";";
        i = j + 1;
      }
      if (!parts.empty()) v["ContainedObjects"] = parts;
      std::string line = r.name + "|" + r.type;
      for (const auto& kv : v) line += "|" + kv.first + "=" + kv.second;
      out.push_back(line);
    }
    return out;
  };
  const std::vector<std::string> ca = canon(a), cb = canon(b);
  EXPECT(ca == cb);
  for (size_t i = 0; i < std::max(ca.size(), cb.size()); ++i)
    if (i >= ca.size() || i >= cb.size() || ca[i] != cb[i])
      std::printf("  record %zu:\n    got  %s\n    want %s\n", i, i < ca.size() ? ca[i].c_str() : "-",
                  i < cb.size() ? cb[i].c_str() : "-");
  std::printf("  %zu effects as 1.2's\n", ca.size());
}

// The sounds 1.2's sounds.zrd names, on 1.0's data: the soil and footstep
// sounds load from 1.0's own (soundsH and soundsL), and the engine loop is
// 1.0's less 176 samples at 22 kHz (88 at 11 kHz).
TEST(patch12_sounds_come_from_10s_own) {
  if (!have_both()) return;
  SavedMounts keep;
  zutl::mounts() = zutl::Mounts{};
  zutl::mount(kRoot + "/mw3-1.0");
  mech3::patch12::install();
  for (int32_t lod : {0, 2}) {
    platform::SilentSoundOutput out;
    zsnd::state() = zsnd::State{};
    zsnd::state().archives_on = 1;
    zsnd::hooks() = zsnd::Hooks{};
    zsnd::hooks().output = &out;
    zrdr::init();
    zrdr::close_archives(true);
    EXPECT(zrdr::open_archive("zbd\\reader.zbd", true));
    EXPECT(zsnd::init(1));
    zsnd::state().lod_default = lod;  // init sets it to 0 (soundsH)
    EXPECT(zsnd::load(1, "sounds.zrd", nullptr, 0));
    zsnd::Set* mech = zsnd::find_set("mech");
    EXPECT(mech && zsnd::load_set(mech));
    auto bytes = [&](const char* name) -> int64_t {
      auto* s = mech ? zsnd::find_in_set(mech, name) : nullptr;
      if (!s || !(s->flags & zsnd::kLoaded)) return -1;
      void *p1 = nullptr, *p2 = nullptr;
      uint32_t n1 = 0, n2 = 0;
      out.lock(s->voice.buffer, 0, 1u << 30, &p1, &n1, &p2, &n2);
      return n1;
    };
    for (const char* n : {"soilDefault", "soilConcrete", "soilDirt", "soilGrass", "soilMud", "soilWater",
                          "soilWaterDeep", "soilSnow"})
      EXPECT(bytes(n) > 0);
    EXPECT(bytes("mech_engine") == (lod == 0 ? 54016 - 176 : 27008 - 88));
    zsnd::shutdown();
    zrdr::close_archives(true);
  }
  zsnd::hooks() = zsnd::Hooks{};
  mech3::patch12::uninstall();
  mech3::patch12::forget_files();
}

// c4b mission 5: the 22 path and region files 1.2 adds come from 1.0's own
// missions 6 and 4 (through the reader, as the mission loads them), each
// equal to 1.2's; nothing comes while mission 5's archive is not open.
TEST(patch12_c4b_m5_paths_come_from_10s_own_missions) {
  if (!have_both()) return;
  SavedMounts keep;
  zutl::mounts() = zutl::Mounts{};
  zutl::mount(kRoot + "/mw3-1.0");
  mech3::patch12::install();
  zrdr::init();
  zrdr::close_archives(true);
  EXPECT(zrdr::open_archive("zbd\\c4b\\reader.zbd", true));
  EXPECT(!mech3::patch12::provide("net_07.zrd"));  // mission 5's archive not open
  EXPECT(zrdr::open_archive("zbd\\c4b\\readerm5.zbd", false));
  const char* const kNew[] = {"net_07.zrd", "net_08.zrd", "net_09.zrd", "net_10.zrd", "net_11.zrd",
                              "net_12.zrd", "net_13.zrd", "net_14.zrd", "net_15.zrd", "net_16.zrd",
                              "net_17.zrd", "net_18.zrd", "net_19.zrd", "net_20.zrd", "reg_inside_c4bm6.zrd",
                              "reg_lava.zrd", "reg_outside_c4bm6.zrd", "reg_p2.zrd", "reg_base_interior.zrd",
                              "reg_inside_c4bm5.zrd", "reg_outside_c4bm5.zrd", "reg_warehouse.zrd"};
  int same = 0;
  for (const char* e : kNew) {
    zrdr::Node* got = zrdr::load(e);
    zrdr::Node* want = read_entry(kRoot + "/mw3-1.2/zbd/c4b/readerm5.zbd", e);
    const bool ok = got && want && mech3::patch12::tree::equal(*got, *want);
    if (!ok) std::printf("  %s: %s\n", e, got ? "differs" : "missing");
    same += ok;
    zrdr::free_node(got), zrdr::free_node(want);
  }
  EXPECT(same == 22);
  EXPECT(!zrdr::load("net_21.zrd"));  // not one of them
  zrdr::close_archives(true);
  mech3::patch12::uninstall();
  mech3::patch12::forget_files();
  std::printf("  %d of mission 5's new files from 1.0's own, as 1.2's\n", same);
}

// The entries 1.2 adds that 1.0 has nowhere (new AI paths, c1's animation
// sources, c2's mobile repair list): through the reader while their archive
// is open, equal to 1.2's.
TEST(patch12_added_entries_parse_as_12s) {
  if (!have_both()) return;
  struct {
    const char *archive, *entry;
  } const kAdded[] = {{"zbd/c1/reader.zbd", "envlights.zrd"}, {"zbd/c1/reader.zbd", "intro.zrd"},
                      {"zbd/c2/reader.zbd", "mobilerepair.zrd"}, {"zbd/c2/readerm3.zbd", "net_14.zrd"},
                      {"zbd/c4b/readerm4.zbd", "net_02.zrd"},  {"zbd/c4b/readerm4.zbd", "net_05.zrd"},
                      {"zbd/c4b/readerm4.zbd", "net_06.zrd"},  {"zbd/c4b/readerm4.zbd", "net_07.zrd"},
                      {"zbd/c4b/readerm4.zbd", "net_08.zrd"}};
  SavedMounts keep;
  zutl::mounts() = zutl::Mounts{};
  zutl::mount(kRoot + "/mw3-1.0");
  mech3::patch12::install();
  int same = 0;
  for (const auto& c : kAdded) {
    std::string opened = c.archive;
    for (char& ch : opened) ch = ch == '/' ? '\\' : ch;
    zrdr::init();
    zrdr::close_archives(true);
    EXPECT(!zrdr::load(c.entry));  // nothing while its archive is closed
    EXPECT(zrdr::open_archive(opened.c_str(), true));
    zrdr::Node* got = zrdr::load(c.entry);
    zrdr::Node* want = read_entry(kRoot + "/mw3-1.2/" + c.archive, c.entry);
    const bool ok = got && want && mech3::patch12::tree::equal(*got, *want);
    if (!ok) std::printf("  %s %s: %s\n", c.archive, c.entry, got ? "differs" : "missing");
    same += ok;
    zrdr::free_node(got), zrdr::free_node(want);
    zrdr::close_archives(true);
  }
  EXPECT(same == 9);
  mech3::patch12::uninstall();
  mech3::patch12::forget_files();
}

namespace {

std::vector<uint8_t> slurp_file(const std::string& path) {
  std::vector<uint8_t> d;
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    uint8_t buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
  }
  return d;
}

// What the game gets from a c3 world: each node's record and data (stale
// heap pointers and the model index aside) and, for objects, the model as a
// set of faces, each its corners (point, normal, UV, colour) from the lowest
// point round, its flags and its material (texture name or colour, soil).
struct WorldView {
  const std::vector<uint8_t>& b;
  mech3::patch12::ParsedWorld w;
  explicit WorldView(const std::vector<uint8_t>& data) : b(data) { mech3::patch12::parse_world(b, &w); }
  int32_t i32(size_t at) const {
    int32_t v;
    std::memcpy(&v, b.data() + at, 4);
    return v;
  }
  float f32(size_t at) const {  // -0 and values that print as 0.000 are 0
    float v;
    std::memcpy(&v, b.data() + at, 4);
    return v > -0.0005f && v < 0.0005f ? 0.0f : v;
  }
  std::string material(int32_t i) const {
    const size_t m = w.materials + 16 + static_cast<size_t>(i) * 0x2c;
    char buf[96];
    if (b[m + 1] & 1) {
      const size_t t = static_cast<size_t>(i32(0xc)) + static_cast<size_t>(i32(m + 0x10)) * 0x28 + 8;
      std::snprintf(buf, sizeof buf, "tex %.20s a%u f%u soil %d", reinterpret_cast<const char*>(b.data() + t),
                    b[m], b[m + 1] & ~0x10u, i32(m + 0x20));
    } else {
      std::snprintf(buf, sizeof buf, "rgb %g %g %g a%u f%u soil %d", f32(m + 4), f32(m + 8), f32(m + 12), b[m],
                    b[m + 1] & ~0x10u, i32(m + 0x20));
    }
    return buf;
  }
  std::set<std::string> faces(int32_t mi) const {
    std::set<std::string> out;
    if (mi < 0 || static_cast<size_t>(mi) >= w.models.size()) return out;
    const auto& m = w.models[static_cast<size_t>(mi)];
    const int32_t vert = i32(m.header + 0x14), nrm = i32(m.header + 0x18);
    for (const auto& p : m.polys) {
      const uint32_t fl = static_cast<uint32_t>(i32(p.record));
      const int32_t k = static_cast<int32_t>(fl & 0xff), mat = i32(p.record + 0x1c);
      const bool tex = b[w.materials + 16 + static_cast<size_t>(mat) * 0x2c + 1] & 1;
      size_t at = p.data;
      const size_t vi = at;
      at += 4 * static_cast<size_t>(k);
      const size_t ni = at;
      if (fl & 0x200) at += 4 * static_cast<size_t>(k);
      const size_t uv = at;
      if (tex) at += 8 * static_cast<size_t>(k);
      const size_t col = at;
      std::vector<std::string> corner;
      for (int32_t c = 0; c < k; ++c) {
        char buf[200];
        const int32_t v = i32(vi + 4 * static_cast<size_t>(c));
        const size_t pv = m.start + 12 * static_cast<size_t>(v < 0 || v >= vert ? 0 : v);
        std::string s;
        std::snprintf(buf, sizeof buf, "(%.3f %.3f %.3f)", f32(pv), f32(pv + 4), f32(pv + 8));
        s = buf;
        if (fl & 0x200) {
          const int32_t n = i32(ni + 4 * static_cast<size_t>(c));
          const size_t pn = m.start + 12 * static_cast<size_t>(vert) + 12 * static_cast<size_t>(n < 0 || n >= nrm ? 0 : n);
          std::snprintf(buf, sizeof buf, " n(%.3f %.3f %.3f)", f32(pn), f32(pn + 4), f32(pn + 8));
          s += buf;
        }
        if (tex) std::snprintf(buf, sizeof buf, " uv(%.4f %.4f)", f32(uv + 8 * c), f32(uv + 8 * c + 4)), s += buf;
        std::snprintf(buf, sizeof buf, " c(%.3f %.3f %.3f)", f32(col + 12 * c), f32(col + 12 * c + 4),
                      f32(col + 12 * c + 8));
        corner.push_back(s + buf);
      }
      size_t lo = 0;
      for (size_t c = 1; c < corner.size(); ++c)
        if (corner[c].substr(0, corner[c].find(')')) < corner[lo].substr(0, corner[lo].find(')'))) lo = c;
      std::string face = material(mat) + " f" + std::to_string(fl);
      for (size_t c = 0; c < corner.size(); ++c) face += " " + corner[(lo + c) % corner.size()];
      out.insert(face);
    }
    return out;
  }
};

bool heap(uint32_t v) { return v >= 0x00800000 && v < 0x08000000; }

}  // namespace

// c3's world: 1.0's file rebuilt by the layer gives the game what 1.2's does
// (docs/patch12.md, sitting 6), and the world loader reads it through the
// hook.
TEST(patch12_c3_world_reads_as_12s) {
  if (!have_both()) return;
  std::vector<uint8_t> v10 = slurp_file(kRoot + "/mw3-1.0/zbd/c3/gamez.zbd");
  const std::vector<uint8_t> v12 = slurp_file(kRoot + "/mw3-1.2/zbd/c3/gamez.zbd");
  std::vector<uint8_t> kept = v12;
  EXPECT(!mech3::patch12::rebuild_c3_world(kept));  // 1.2's own: an edit's old value does not match
  EXPECT(mech3::patch12::rebuild_c3_world(v10));
  const WorldView a(v10), b(v12);
  EXPECT(a.w.nodes.size() == b.w.nodes.size());
  int records = 0, datas = 0, models = 0, objects = 0;
  for (size_t i = 0; i < a.w.nodes.size() && i < b.w.nodes.size(); ++i) {
    const auto &na = a.w.nodes[i], &nb = b.w.nodes[i];
    bool rec = true, dat = na.end - na.start == nb.end - nb.start;
    for (size_t j = 0; j < 0xd4; j += 4) {
      const uint32_t x = static_cast<uint32_t>(a.i32(na.record + j)), y = static_cast<uint32_t>(b.i32(nb.record + j));
      if (x != y && !(heap(x) && heap(y)) && (j != 0x3c || x == 0xffffffffu || y == 0xffffffffu)) rec = false;
    }
    for (size_t j = 0; dat && j < na.end - na.start; j += 4) {
      const uint32_t x = static_cast<uint32_t>(a.i32(na.start + j)), y = static_cast<uint32_t>(b.i32(nb.start + j));
      if (x != y && !(heap(x) && heap(y))) dat = false;
    }
    if (!rec && records++ < 3) std::printf("  node %zu: record differs\n", i);
    if (!dat && datas++ < 3) std::printf("  node %zu: data differs\n", i);
    if (a.i32(na.record + 0x34) == 5) {
      ++objects;
      const auto fa = a.faces(a.i32(na.record + 0x3c)), fb = b.faces(b.i32(nb.record + 0x3c));
      if (fa != fb && models++ < 3) {
        std::printf("  node %zu %.36s: model differs\n", i, reinterpret_cast<const char*>(v10.data() + na.record));
        for (const auto& x : fa)
          if (!fb.count(x)) { std::printf("    got  %.300s\n", x.c_str()); break; }
        for (const auto& x : fb)
          if (!fa.count(x)) { std::printf("    want %.300s\n", x.c_str()); break; }
      }
    }
  }
  EXPECT(records == 0 && datas == 0 && models == 0);
  std::printf("  %zu nodes, %d objects' models as 1.2's (%d, %d, %d differ)\n", a.w.nodes.size(), objects, records,
              datas, models);
  // Through the loader: the hook hands it the rebuilt file.
  SavedMounts keep;
  zutl::mounts() = zutl::Mounts{};
  zutl::mount(kRoot + "/mw3-1.0");
  mech3::patch12::forget_files();
  zcls::ZbdHeader h{};
  std::FILE* plain = zcls::open_zbd("zbd\\c3\\gamez.zbd", &h);
  const int32_t nodes_10 = h.nodes_offset;
  if (plain) std::fclose(plain);
  mech3::patch12::install();
  std::FILE* f = zcls::open_zbd("zbd\\c3\\gamez.zbd", &h);
  EXPECT(f && h.nodes_offset != nodes_10 && h.nodes_offset == a.i32(0x20));
  if (f) std::fclose(f);
  EXPECT(zcls::read_zbd("zbd\\c3\\gamez.zbd") == 0);  // the whole world read from the rebuilt file
  mech3::patch12::uninstall();
  mech3::patch12::forget_files();
}
