#include "mech3/mods.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsiscript/si_script.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/mission.h"

namespace mech3 {

namespace fs = std::filesystem;

// mech_anim.h's motion archive (its Handle is not mission.h's).
bool open_motions(const char* path);
void close_motions();
bool load_motion(zsis::Script& s, const char* name);
void script_free_parts(zsis::Script& s);

namespace {
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}
std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
}  // namespace

bool mod_parse(const std::string& text, ModInfo* out) {
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const size_t eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string key = lower(trim(line.substr(0, eq))), value = trim(line.substr(eq + 1));
    if (key == "name") out->name = value;
    else if (key == "version") out->version = value;
    else if (key == "author") out->author = value;
    else if (key == "for") out->game = value;
    else if (key == "description") out->description = value;
    else if (key == "world" && !value.empty()) out->worlds.push_back(value);
    else if (key == "ia_world" && !value.empty()) {
      const size_t sp = value.find_first_of(" \t");
      out->ia_worlds.emplace_back(value.substr(0, sp), sp == std::string::npos ? std::string() : trim(value.substr(sp)));
    }
  }
  if (out->name.empty()) out->error = "mod.txt has no name= line";
  else if (!out->game.empty() && lower(out->game).rfind("mw3", 0) != 0) out->error = "it is for " + out->game + ", not MechWarrior 3";
  return out->error.empty();
}

std::vector<ModInfo> mods_found(const std::string& write_dir) {
  std::vector<ModInfo> out;
  std::error_code ec;
  if (write_dir.empty()) return out;
  for (fs::directory_iterator it(fs::path(write_dir) / "mods", ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    ModInfo m;
    m.folder = it->path().filename().string();
    m.path = it->path().string();
    std::ifstream f(it->path() / "mod.txt");
    if (!f) {
      m.error = "no mod.txt";
    } else {
      std::stringstream ss;
      ss << f.rdbuf();
      mod_parse(ss.str(), &m);
    }
    out.push_back(m);
  }
  std::sort(out.begin(), out.end(), [](const ModInfo& a, const ModInfo& b) { return a.folder < b.folder; });
  return out;
}

std::vector<ModIaWorld>& mod_ia_worlds() {
  static std::vector<ModIaWorld> w;
  return w;
}

std::vector<std::string> mods_mount(const std::string& write_dir, const std::vector<std::string>& enabled,
                                    const std::vector<std::string>& later) {
  std::vector<std::string> log;
  std::vector<std::pair<std::string, ModIaWorld>> ia;  // the mod's name, the entry
  const std::vector<ModInfo> found = mods_found(write_dir);
  std::vector<std::string> done;
  for (const std::string& want : enabled) {
    if (std::find(done.begin(), done.end(), want) != done.end()) continue;  // named twice: the first place counts
    done.push_back(want);
    const auto it = std::find_if(found.begin(), found.end(), [&](const ModInfo& m) { return m.folder == want; });
    if (it == found.end()) {
      log.push_back("mod " + want + ": not in " + (fs::path(write_dir) / "mods").string() + ", left out");
      continue;
    }
    if (!it->error.empty()) {
      log.push_back("mod " + want + ": " + it->error + ", left out");
      continue;
    }
    zutl::mount_mod(it->path);
    log.push_back("mod " + want + " (" + it->name + (it->version.empty() ? "" : " " + it->version) + ") mounted");
    // Its new worlds: a folder no world has yet, with its files in the mod.
    for (const std::string& w : it->worlds) {
      std::error_code wec;
      if (mission_dir_index(w.c_str()) >= 0) {
        log.push_back("  world " + w + ": there is a world " + w + " already, left out");
      } else if (w.find_first_of("/\\. ") != std::string::npos || !fs::is_regular_file(fs::path(it->path) / "zbd" / w / "gamez.zbd", wec)) {
        log.push_back("  world " + w + ": no zbd/" + w + "/gamez.zbd in the mod, left out");
      } else {
        mod_worlds().push_back(w);
        log.push_back("  world " + w + " added (world " + std::to_string(mission_dir_count() - 1) + ")");
      }
    }
    for (const auto& [folder, name] : it->ia_worlds) ia.push_back({want, ModIaWorld{folder, name.empty() ? folder : name}});
    // Each file: what it replaces (a copy later in the mounts) or adds.
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator f(it->path, ec), end; !ec && f != end; f.increment(ec))
      if (f->is_regular_file(ec)) files.push_back(fs::relative(f->path(), it->path, ec).generic_string());
    std::sort(files.begin(), files.end());
    int replaced = 0, added = 0, ignored = 0;
    for (const std::string& rel : files) {
      if (lower(rel) == "mod.txt") continue;
      if (!zutl::mod_may_serve(rel)) {
        log.push_back("  " + rel + ": the player's own kind of file, ignored");
        ++ignored;
        continue;
      }
      bool over = false;
      for (const std::string& root : later) over = over || fs::is_regular_file(fs::path(root) / rel, ec);
      log.push_back("  " + rel + (over ? " replaces the game's" : " added"));
      ++(over ? replaced : added);
    }
    log.push_back("  " + std::to_string(replaced) + " replaced, " + std::to_string(added) + " added, " +
                  std::to_string(ignored) + " ignored");
  }
  // Instant Action's new entries, once every mod's worlds are known.
  for (const auto& [mod, w] : ia) {
    if (mission_dir_index(w.folder.c_str()) < 0) {
      log.push_back("mod " + mod + ": Instant Action world " + w.folder + " is no world, left out");
      continue;
    }
    mod_ia_worlds().push_back(w);
    log.push_back("mod " + mod + ": Instant Action world \"" + w.name + "\" on " + w.folder);
  }
  return log;
}

bool mod_file_is_gameplay(const std::string& rel) {
  const std::string p = lower(rel);
  const std::string name = p.substr(p.rfind('/') == std::string::npos ? 0 : p.rfind('/') + 1);
  auto ends = [&](const char* e) { return name.size() >= std::strlen(e) && name.compare(name.size() - std::strlen(e), std::string::npos, e) == 0; };
  auto starts = [&](const char* b) { return name.rfind(b, 0) == 0; };
  if (name == "mod.txt" || name == "strings.txt" || !zutl::mod_may_serve(rel)) return false;
  if (p.rfind("music/", 0) == 0 || p.rfind("video/", 0) == 0) return false;
  if (ends(".png") || ends(".wav") || ends(".ogg") || ends(".txt") || ends(".avi")) return false;
  if (ends(".zbd") && (starts("rimage") || starts("rmechtex") || starts("rtexture") || starts("texture") || starts("mechtex") ||
                       starts("sounds")))
    return false;
  return true;
}

uint64_t mods_gameplay_hash(const std::string& write_dir, const std::vector<std::string>& enabled) {
  uint64_t h = 1469598103934665603ull;  // FNV-1a
  auto mix = [&](const char* d, size_t n) {
    for (size_t i = 0; i < n; ++i) h = (h ^ static_cast<uint8_t>(d[i])) * 1099511628211ull;
  };
  bool any = false;
  const std::vector<ModInfo> found = mods_found(write_dir);
  std::vector<std::string> done;
  for (const std::string& want : enabled) {
    if (std::find(done.begin(), done.end(), want) != done.end()) continue;
    done.push_back(want);
    const auto it = std::find_if(found.begin(), found.end(), [&](const ModInfo& m) { return m.folder == want; });
    if (it == found.end() || !it->error.empty()) continue;
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::recursive_directory_iterator f(it->path, ec), end; !ec && f != end; f.increment(ec))
      if (f->is_regular_file(ec)) files.push_back(fs::relative(f->path(), it->path, ec).generic_string());
    std::sort(files.begin(), files.end());
    for (const std::string& rel : files) {
      if (!mod_file_is_gameplay(rel)) continue;
      any = true;
      const std::string key = lower(rel);
      mix(key.data(), key.size() + 1);
      std::ifstream in(fs::path(it->path) / rel, std::ios::binary);
      char buf[65536];
      while (in.read(buf, sizeof buf) || in.gcount() > 0) mix(buf, static_cast<size_t>(in.gcount()));
    }
  }
  return any ? h : 0;
}

std::vector<std::string> mods_apply_strings(std::map<int32_t, std::string>& text,
                                            const std::map<std::string, int32_t>& ids) {
  std::vector<std::string> log;
  const std::vector<std::string>& mods = zutl::mounts().mods;
  // The last mod first, so the first one's lines are the ones left.
  for (auto m = mods.rbegin(); m != mods.rend(); ++m) {
    std::ifstream f(fs::path(*m) / "strings.txt");
    if (!f) continue;
    int used = 0, n = 0;
    for (std::string line; std::getline(f, line);) {
      ++n;
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (n == 1 && line.rfind("\xef\xbb\xbf", 0) == 0) line.erase(0, 3);  // a UTF-8 mark
      if (trim(line).empty() || trim(line)[0] == '#') continue;
      const size_t eq = line.find('=');
      const std::string key = trim(line.substr(0, eq == std::string::npos ? 0 : eq));
      int32_t id = -1;
      if (!key.empty() && std::isdigit(static_cast<unsigned char>(key[0]))) {
        char* end = nullptr;
        const long v = std::strtol(key.c_str(), &end, 0);
        if (end && *end == 0 && v >= 0 && v <= 0x7fffffff) id = static_cast<int32_t>(v);
      } else if (const auto it = ids.find(key); it != ids.end()) {
        id = it->second;
      }
      std::string value, game;
      if (eq != std::string::npos) {
        const std::string raw = line.substr(eq + 1);
        for (size_t i = 0; i < raw.size(); ++i) {
          if (raw[i] == '\\' && i + 1 < raw.size() && (raw[i + 1] == 'n' || raw[i + 1] == '\\')) {
            value += raw[i + 1] == 'n' ? "\r\n" : "\\";
            ++i;
          } else {
            value += raw[i];
          }
        }
      }
      if (eq == std::string::npos || id < 0) {
        log.push_back("  " + (fs::path(*m) / "strings.txt").string() + " line " + std::to_string(n) + ": no id= here, left out");
        continue;
      }
      if (!zutl::game_text(value, &game)) {
        log.push_back("  " + (fs::path(*m) / "strings.txt").string() + " line " + std::to_string(n) +
                      ": a character the game cannot show, left out");
        continue;
      }
      text[id] = game.substr(0, 0x1ff);  // the original's 0x200-byte buffers
      ++used;
    }
    log.insert(log.end() - 0, "mod strings: " + std::to_string(used) + " from " + (fs::path(*m) / "strings.txt").string());
  }
  return log;
}

std::vector<std::string> mod_check(const std::string& dir, const std::string& install, int* errors) {
  std::vector<std::string> out;
  *errors = 0;
  auto error = [&](const std::string& what) { out.push_back("error: " + what), ++*errors; };
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    error(dir + " is no folder");
    return out;
  }
  ModInfo m;
  std::ifstream f(fs::path(dir) / "mod.txt");
  if (!f) {
    error("no mod.txt");
  } else {
    std::stringstream ss;
    ss << f.rdbuf();
    if (mod_parse(ss.str(), &m)) out.push_back("mod.txt: " + m.name + (m.version.empty() ? "" : " " + m.version));
    else error("mod.txt: " + m.error);
  }
  for (const std::string& w : m.worlds) {
    if (mission_dir_index(w.c_str()) >= 0) error("world " + w + ": there is a world " + w + " already");
    else if (w.find_first_of("/\\. ") != std::string::npos || !fs::is_regular_file(fs::path(dir) / "zbd" / w / "gamez.zbd", ec))
      error("world " + w + ": no zbd/" + w + "/gamez.zbd");
    else if (!fs::is_regular_file(fs::path(dir) / (w + ".gs"), ec))
      error("world " + w + ": no " + w + ".gs (its build script) at the mod's root");
    else out.push_back("world " + w + ": ok");
  }
  for (const auto& [folder, name] : m.ia_worlds) {
    const bool own = std::find(m.worlds.begin(), m.worlds.end(), folder) != m.worlds.end();
    if (!own && mission_dir_index(folder.c_str()) < 0) error("ia_world " + folder + ": no such world");
    else out.push_back("ia_world " + folder + " \"" + (name.empty() ? folder : name) + "\": ok");
  }
  // Every file.
  std::vector<std::string> files;
  for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    if (it->is_regular_file(ec)) files.push_back(fs::relative(it->path(), dir, ec).generic_string());
  std::sort(files.begin(), files.end());
  int archives = 0, entries = 0;
  for (const std::string& rel : files) {
    const std::string low = lower(rel);
    if (low == "mod.txt") continue;
    if (!zutl::mod_may_serve(rel)) {
      out.push_back(rel + ": the player's own kind of file, which a mod cannot replace (ignored)");
      continue;
    }
    const bool over = !install.empty() && fs::is_regular_file(fs::path(install) / rel, ec);
    out.push_back(rel + (install.empty() ? "" : over ? ": replaces the game's" : ": added"));
    const std::string host = (fs::path(dir) / rel).string();
    const std::string name = low.substr(low.rfind('/') == std::string::npos ? 0 : low.rfind('/') + 1);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".zbd") == 0) {
      zutl_zar::Archive a;
      zutl_zar::init(a);
      if (!zutl_zar::open(a, host.c_str())) {
        error(rel + ": not an archive the game can read (its table)");
        zutl_zar::destroy(a);
        continue;
      }
      ++archives;
      std::vector<std::string> names;
      for (int i = 0; i < a.count; ++i) names.push_back(a.entries[i].name);
      zutl_zar::destroy(a);
      if (name == "mechlib.zbd") {  // every tree through the bounded reader
        zmod::models() = zmod::ModelPool{};
        zmod::reset_pool();
        zcls::MechLib lib;
        zutl_zar::init(lib.archive);
        if (!zcls::mechlib_open(lib, host.c_str()) || lib.materials.empty()) {
          error(rel + ": its version, format or materials cannot be read");
        } else {
          for (const std::string& e : names) {
            if (e == "version" || e == "format" || e == "materials") continue;
            ++entries;
            if (!zcls::mechlib_load(lib, e.c_str())) error(rel + ": model " + e + " cannot be read");
          }
        }
        zcls::mechlib_close(lib);
        continue;
      }
      if (name == "motion.zbd") {  // every motion through the bounded reader
        if (open_motions(host.c_str())) {
          for (const std::string& e : names) {
            ++entries;
            zsis::Script sc;
            if (!load_motion(sc, e.c_str())) error(rel + ": motion " + e + " cannot be read");
            script_free_parts(sc);
          }
          close_motions();
        } else {
          error(rel + ": the motion archive cannot be opened");
        }
        continue;
      }
      if (name == "anim.zbd") {  // a world's animations: the read (its binding needs the world)
        zeff::anim() = zeff::AnimState{};
        zeff::anim_allow_loading(1);
        zeff::anim_set_zbd_path(host.c_str());
        zeff::anim_read_zbd();
        if (zeff::anim().defs == nullptr && zeff::anim().def_count() == 0) error(rel + ": its animations cannot be read");
        else out.push_back("  " + std::to_string(zeff::anim().def_count()) + " animations read (bound to the world in play)");
        zeff::anim_free_all();
        zeff::anim() = zeff::AnimState{};
        continue;
      }
      if (name.rfind("reader", 0) != 0) {  // the reader archives' entries: through the bounded reader
        out.push_back("  its table read; what its " + std::to_string(names.size()) +
                      " entries hold is not checked yet (docs/modding.md, M3)");
        continue;
      }
      zrdr::init();
      if (!zrdr::open_archive(host.c_str(), true)) {
        error(rel + ": the reader cannot open it");
        continue;
      }
      for (const std::string& e : names) {
        ++entries;
        if (zrdr::Node* n = zrdr::load(e.c_str())) zrdr::free_node(n);
        else error(rel + ": entry " + e + " cannot be read");
      }
      zrdr::close_archives(true);
    } else if (name.size() > 4 && name.compare(name.size() - 4, 4, ".zrd") == 0) {
      zrdr::init();
      if (zrdr::Node* n = zrdr::load(host.c_str())) zrdr::free_node(n);
      else error(rel + ": not a text .zrd the reader can read");
    }
  }
  // strings.txt, as the game would take it.
  if (fs::is_regular_file(fs::path(dir) / "strings.txt", ec)) {
    zutl::Mounts saved = zutl::mounts();
    zutl::mounts() = zutl::Mounts{};
    zutl::mount_mod(dir);
    std::map<int32_t, std::string> text;
    for (const std::string& l : mods_apply_strings(text, {})) out.push_back(l.rfind("  ", 0) == 0 ? "warning:" + l.substr(1) : l);
    zutl::mounts() = saved;
  }
  out.push_back(std::to_string(files.size()) + " files, " + std::to_string(archives) + " archives, " + std::to_string(entries) +
                " reader entries checked; " + std::to_string(*errors) + " errors");
  return out;
}

}  // namespace mech3
