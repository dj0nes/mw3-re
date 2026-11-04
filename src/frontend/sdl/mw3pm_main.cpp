// mw3pm: Pirate's Moon (ours; docs/games-layout.md). The second program over
// the shared engine: PM's table set before anything loads, PM's own install
// mounted, its own write dir. For now it stops at "data not loadable yet",
// unless --fly starts a world for checking the port.
//
//   mw3pm [install-dir] [--write-dir DIR] [--hidden] [--nosound] [--version]
//         [--fly c1 [--variant BLOCK] [--pos X Y Z] [--look YAW PITCH] [--shot FILE.bmp [--shot-frame N]] [--frames N]]
//
//   install-dir      the Pirate's Moon folder (default: mw3pm.cfg's install=,
//                    else the development copy, game/pm)
//   --write-dir DIR  saves and settings (default: the user's app data, apart
//                    from MechWarrior 3's)
//   --hidden         no message box (the tests)
//   --nosound        accepted, as mw3's; nothing plays yet
//   --fly NAME       debug only: world NAME: W/S/A/D move, Q/E down/up,
//                    arrows turn, Shift fast, P prints the view, Escape quits
//   --variant BLOCK  debug only, with --fly: world.zrd's block over `defaults` (c1: m2, m5, sa4 are night)
//   --map-light X Y Z RANGE  debug only, with --fly: one light map light there
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "gamez/zgame/zgame_table.h"
#include "gamez/zutil/zutl_path.h"
#include "frontend/pm_debug_world.h"
#include "frontend/sdl/sdl_platform.h"
#include "pm/pm_game.h"

namespace {

struct Args {
  std::string install, write_dir;
  bool hidden = false;
  std::string fly, variant, shot;
  bool pos_given = false;
  float pos[3] = {};
  float yaw_deg = 0, pitch_deg = 30;
  float map_light[4] = {};  // x, y, z, range
  bool map_light_on = false;
  int shot_frame = 10, frames = 0;
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
    else if (s == "--fly" && i + 1 < argc) a.fly = argv[++i];
    else if (s == "--variant" && i + 1 < argc) a.variant = argv[++i];
    else if (s == "--pos" && i + 3 < argc) {
      for (float& p : a.pos) p = static_cast<float>(std::atof(argv[++i]));
      a.pos_given = true;
    } else if (s == "--map-light" && i + 4 < argc) {
      for (float& p : a.map_light) p = static_cast<float>(std::atof(argv[++i]));
      a.map_light_on = true;
    } else if (s == "--look" && i + 2 < argc) {
      a.yaw_deg = static_cast<float>(std::atof(argv[++i]));
      a.pitch_deg = static_cast<float>(std::atof(argv[++i]));
    } else if (s == "--shot" && i + 1 < argc) a.shot = argv[++i];
    else if (s == "--shot-frame" && i + 1 < argc) a.shot_frame = std::atoi(argv[++i]);
    else if (s == "--frames" && i + 1 < argc) a.frames = std::atoi(argv[++i]);
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

// --fly (ours): the world through the real walk and draw table, no game.
int fly(const Args& a) {
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    std::fprintf(stderr, "mw3pm: SDL_InitSubSystem: %s\n", SDL_GetError());
    return 1;
  }
  constexpr int kW = 1280, kH = 960;
  sdlfe::SdlWindow window;
  if (!window.init(a.hidden)) return 1;
  window.create("Pirate's Moon (debug)", kW, kH);
  window.frame_size(kW, kH);
  window.show();
  float eye[3];
  const std::string err = frontend::pm_world_load(a.fly, a.variant, window.render(), kW, kH, eye);
  if (!err.empty()) {
    std::fprintf(stderr, "mw3pm: --fly %s: %s\n", a.fly.c_str(), err.c_str());
    return 1;
  }
  if (a.pos_given) std::memcpy(eye, a.pos, sizeof eye);
  if (a.map_light_on) frontend::pm_world_map_light(a.map_light, a.map_light[3]);
  float yaw = a.yaw_deg * 0.0174533f, pitch = a.pitch_deg * 0.0174533f;
  for (int frame = 1;; ++frame) {
    SDL_Event e;
    while (SDL_PollEvent(&e))
      if (e.type == SDL_EVENT_QUIT) return 0;
    const bool* k = SDL_GetKeyboardState(nullptr);
    if (k[SDL_SCANCODE_ESCAPE]) return 0;
    const float step = k[SDL_SCANCODE_LSHIFT] ? 20.0f : 3.0f;
    yaw += (k[SDL_SCANCODE_RIGHT] - k[SDL_SCANCODE_LEFT]) * 0.03f;
    pitch += (k[SDL_SCANCODE_UP] - k[SDL_SCANCODE_DOWN]) * 0.02f;
    const float fwd[3] = {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const float right[3] = {-std::cos(yaw), 0, std::sin(yaw)};
    for (int i = 0; i < 3; ++i) {
      eye[i] += (k[SDL_SCANCODE_W] - k[SDL_SCANCODE_S]) * fwd[i] * step;
      eye[i] += (k[SDL_SCANCODE_D] - k[SDL_SCANCODE_A]) * right[i] * step;
    }
    eye[1] += (k[SDL_SCANCODE_E] - k[SDL_SCANCODE_Q]) * step;
    if (k[SDL_SCANCODE_P] && frame % 15 == 0)
      std::printf("mw3pm: --pos %.1f %.1f %.1f --look %.1f %.1f\n", eye[0], eye[1], eye[2], yaw / 0.0174533f, pitch / 0.0174533f);
    const bool shot = !a.shot.empty() && frame == a.shot_frame;
    if (shot) window.render()->capture_next(a.shot.c_str());
    frontend::pm_world_frame(eye, yaw, pitch, window.render());
    if (shot) {
      std::printf("mw3pm: wrote %s (frame %d)\n", a.shot.c_str(), frame);
      return 0;
    }
    if (a.frames > 0 && frame >= a.frames) return 0;
  }
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
  if (!a.fly.empty()) {
    const int result = fly(a);
    SDL_Quit();
    return result;
  }
  tell(a, "Pirate's Moon's data is not loadable yet: its readers are being ported (docs/pm-map.md, step 1).");
  SDL_Quit();
  return 0;
}
