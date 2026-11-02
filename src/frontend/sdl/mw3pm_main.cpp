// mw3pm: Pirate's Moon (ours; docs/games-layout.md). The second program over
// the shared engine: PM's table set before anything loads, PM's own install
// mounted, its own write dir. For now it stops at "data not loadable yet":
// PM's readers come first (docs/pm-map.md, A porting order, step 1).
//
//   mw3pm [install-dir] [--write-dir DIR] [--hidden] [--nosound] [--version]
//
//   install-dir      the Pirate's Moon folder (default: mw3pm.cfg's install=,
//                    else the development copy, game/pm)
//   --write-dir DIR  saves and settings (default: the user's app data, apart
//                    from MechWarrior 3's)
//   --hidden         no message box (the tests)
//   --nosound        accepted, as mw3's; nothing plays yet
#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "gamez/zgame/zgame_table.h"
#include "gamez/zutil/zutl_path.h"
#include "pm/pm_game.h"

namespace {

struct Args {
  std::string install, write_dir;
  bool hidden = false;
};

bool parse(int argc, char** argv, Args& a) {
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    if (s == "--version") {
      std::printf("mw3pm (Pirate's Moon rebuild) %s\n", MW3_VERSION);
      std::exit(0);
    } else if (s == "--write-dir" && i + 1 < argc) a.write_dir = argv[++i];
    else if (s == "--hidden") a.hidden = true;
    else if (s == "--nosound") {}
    else if (!s.empty() && s[0] != '-' && a.install.empty()) a.install = s;
    else {
      std::fprintf(stderr, "mw3pm: unknown argument %s\nusage: mw3pm [install-dir] [--write-dir DIR] [--hidden] [--nosound] [--version]\n", s.c_str());
      return false;
    }
  }
  return true;
}

// mw3pm.cfg in the write dir: install=DIR.
std::string cfg_install(const std::string& write_dir) {
  std::ifstream in(std::filesystem::path(write_dir) / "mw3pm.cfg");
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.rfind("install=", 0) == 0) return line.substr(8);
  }
  return {};
}
void save_install(const std::string& write_dir, const std::string& install) {
  std::ofstream out(std::filesystem::path(write_dir) / "mw3pm.cfg", std::ios::trunc);
  out << "install=" << install << "\n";
}

void tell(const Args& a, const char* text) {
  std::fprintf(stderr, "mw3pm: %s\n", text);
  if (!a.hidden) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Pirate's Moon", text, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
  Args a;
  if (!parse(argc, argv, a)) return 2;
  if (!SDL_Init(0)) {
    std::fprintf(stderr, "mw3pm: SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  zgame::set_game(pm::table());
  if (a.write_dir.empty()) {
    if (char* p = SDL_GetPrefPath("MicroProse", "Pirate's Moon")) a.write_dir = p, SDL_free(p);
  }
  while (a.write_dir.size() > 1 && a.write_dir.back() == '/') a.write_dir.pop_back();
  if (!a.write_dir.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(a.write_dir, ec);
  }
  const bool given = !a.install.empty();
  if (a.install.empty() && !a.write_dir.empty()) a.install = cfg_install(a.write_dir);
#ifdef MW3PM_GAME_DIR
  if (!given && (a.install.empty() || pm::install_kind(a.install) != pm::Install::kPm)) a.install = MW3PM_GAME_DIR;
#endif
  const pm::Install kind = a.install.empty() ? pm::Install::kNone : pm::install_kind(a.install);
  if (kind != pm::Install::kPm) {
    const std::string text = "No Pirate's Moon install: " + (a.install.empty() ? std::string("none given") : a.install + " is " + pm::install_words(kind)) +
                             ". Start mw3pm with the Pirate's Moon folder.";
    tell(a, text.c_str());
    SDL_Quit();
    return 1;
  }
  if (given && !a.write_dir.empty()) save_install(a.write_dir, std::filesystem::absolute(a.install).string());
  zutl::mount(a.install);
  zutl::set_write_dir(a.write_dir);
  std::printf("mw3pm: %s (%s), install %s, write dir %s\n", zgame::game().name, zgame::game().exe, a.install.c_str(),
              a.write_dir.c_str());
  tell(a, "Pirate's Moon's data is not loadable yet: its readers are being ported (docs/pm-map.md, step 1).");
  SDL_Quit();
  return 0;
}
