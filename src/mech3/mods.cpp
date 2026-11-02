#include "mech3/mods.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "gamez/zutil/zutl_path.h"

namespace mech3 {

namespace fs = std::filesystem;

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

std::vector<std::string> mods_mount(const std::string& write_dir, const std::vector<std::string>& enabled,
                                    const std::vector<std::string>& later) {
  std::vector<std::string> log;
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

}  // namespace mech3
