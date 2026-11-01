#include "frontend/sdl/launcher.h"
#include "frontend/sdl/launcher_notes.h"
#include "frontend/sdl/text_fit.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "frontend/sdl/sdl_install.h"
#include "gamez/zutil/zutl_zar.h"
#include "gamez/zrender/zrndr_ripple.h"
#include "mech3/menu_ripple.h"
#include "gamez/zrender/zrndr_tint.h"
#include "mech3/game_setup.h"

namespace sdlfe {

namespace {
namespace fs = std::filesystem;
constexpr int W = 640, H = 480;
constexpr int kBadge = 76;  // a game's badge across, on the 640x480 canvas

// ---- art ----

std::string assets_dir() {
  std::error_code ec;
  if (const char* base = SDL_GetBasePath()) {
    const fs::path p = fs::path(base) / "assets" / "launcher";
    if (fs::is_directory(p, ec)) return p.string();
  }
#ifdef MW3_ASSETS_DIR
  const fs::path p = fs::path(MW3_ASSETS_DIR) / "launcher";
  if (fs::is_directory(p, ec)) return p.string();
#endif
  return {};
}
// The font: assets/launcher/font.ttf when shipped, else a condensed system
// face (a placeholder until Owen picks one; launcher.md, Open).
std::string font_path(const std::string& dir) {
  std::error_code ec;
  std::vector<std::string> tries;
  if (!dir.empty()) tries.push_back((fs::path(dir) / "font.ttf").string());
  tries.insert(tries.end(), {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/arialbd.ttf",
                             "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf",
                             "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                             // Linux (docs/linux.md): a narrow face first, at Debian/Ubuntu's,
                             // Fedora's and Arch's places, then a plain bold one.
                             "/usr/share/fonts/truetype/liberation/LiberationSansNarrow-Bold.ttf",
                             "/usr/share/fonts/liberation-narrow/LiberationSansNarrow-Bold.ttf",
                             "/usr/share/fonts/liberation/LiberationSansNarrow-Bold.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-Bold.ttf",
                             "/usr/share/fonts/dejavu-sans-fonts/DejaVuSansCondensed-Bold.ttf",
                             "/usr/share/fonts/TTF/DejaVuSansCondensed-Bold.ttf",
                             "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                             "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans-Bold.ttf",
                             "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                             "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"});
  for (const std::string& t : tries)
    if (fs::exists(t, ec)) return t;
  return {};
}

SDL_Surface* load_png(const std::string& dir, const char* name) {
  if (dir.empty()) return nullptr;
  SDL_Surface* s = SDL_LoadPNG((fs::path(dir) / name).string().c_str());
  if (!s) return nullptr;
  SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
  SDL_DestroySurface(s);
  return c;
}
// The picture's lit part (brighter than `thresh`), cropped; with `alpha`, its
// near-black made see-through (the generated art is on black).
SDL_Surface* cut_out(SDL_Surface* s, int thresh, bool alpha) {
  if (!s) return nullptr;
  int x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
  for (int y = 0; y < s->h; ++y) {
    const uint32_t* row = reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(s->pixels) + y * s->pitch);
    for (int x = 0; x < s->w; ++x) {
      const uint32_t p = row[x];
      const int lum = std::max({int(p >> 16 & 0xff), int(p >> 8 & 0xff), int(p & 0xff)});
      if (lum > thresh) x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    }
  }
  if (x1 < x0) return s;
  SDL_Surface* c = SDL_CreateSurface(x1 - x0 + 1, y1 - y0 + 1, SDL_PIXELFORMAT_ARGB8888);
  const SDL_Rect r{x0, y0, c->w, c->h};
  SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
  SDL_BlitSurface(s, &r, c, nullptr);
  SDL_DestroySurface(s);
  if (alpha) {
    for (int y = 0; y < c->h; ++y) {
      uint32_t* row = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(c->pixels) + y * c->pitch);
      for (int x = 0; x < c->w; ++x) {
        const uint32_t p = row[x];
        const int lum = std::max({int(p >> 16 & 0xff), int(p >> 8 & 0xff), int(p & 0xff)});
        const int a = std::clamp((lum - 18) * 255 / 42, 0, 255);
        row[x] = (p & 0xffffffu) | (static_cast<uint32_t>(a) << 24);
      }
    }
    SDL_SetSurfaceBlendMode(c, SDL_BLENDMODE_BLEND);
  }
  return c;
}
SDL_Surface* scaled(SDL_Surface* s, int w, int h) {
  if (!s) return nullptr;
  SDL_Surface* r = SDL_ScaleSurface(s, w, h, SDL_SCALEMODE_LINEAR);
  if (r) SDL_SetSurfaceBlendMode(r, SDL_BLENDMODE_BLEND);
  return r;
}

struct Art {
  SDL_Surface *stars = nullptr, *metal = nullptr, *plate = nullptr, *title = nullptr, *emblem = nullptr;
  SDL_Surface* badge[3] = {};  // the games' round badges: MechWarrior 3, Pirate's Moon, MechWarrior (1989)
  ~Art() {
    for (SDL_Surface* s : {stars, metal, plate, title, emblem, badge[0], badge[1], badge[2]})
      if (s) SDL_DestroySurface(s);
  }
  void load(const std::string& dir) {
    if (SDL_Surface* s = load_png(dir, "bg_stars.png")) stars = scaled(s, s->w * H / s->h, H), SDL_DestroySurface(s);
    if (SDL_Surface* s = load_png(dir, "bg_metal.png")) metal = scaled(s, s->w * 240 / s->h, 240), SDL_DestroySurface(s);
    if (stars) SDL_SetSurfaceBlendMode(stars, SDL_BLENDMODE_NONE);
    if (metal) SDL_SetSurfaceBlendMode(metal, SDL_BLENDMODE_NONE), SDL_SetSurfaceColorMod(metal, 190, 190, 190);
    plate = cut_out(load_png(dir, "plate.png"), 24, false);
    if (SDL_Surface* t = cut_out(load_png(dir, "title.png"), 24, true)) title = scaled(t, 420, t->h * 420 / t->w), SDL_DestroySurface(t);
    if (SDL_Surface* e = cut_out(load_png(dir, "emblem.png"), 24, true)) emblem = scaled(e, 72, 72), SDL_DestroySurface(e);
    const char* const badges[3] = {"badge_mw3.png", "badge_pm.png", "badge_mw1.png"};
    for (int i = 0; i < 3; ++i)
      if (SDL_Surface* b = cut_out(load_png(dir, badges[i]), 24, true)) badge[i] = scaled(b, kBadge, kBadge), SDL_DestroySurface(b);
  }
};

// ---- text ----

struct Text {
  std::string path;
  std::map<int, TTF_Font*> fonts;
  bool ok = false;
  ~Text() {
    for (auto& [k, f] : fonts) TTF_CloseFont(f);
    if (ok) TTF_Quit();
  }
  void open(const std::string& p) {
    path = p;
    ok = !p.empty() && TTF_Init();
  }
  TTF_Font* font(int size) {
    if (!ok) return nullptr;
    auto it = fonts.find(size);
    if (it != fonts.end()) return it->second;
    return fonts[size] = TTF_OpenFont(path.c_str(), static_cast<float>(size));
  }
  // `s` at (x, y): align -1 left, 0 centred, 1 right of x; y its middle.
  void draw(SDL_Surface* dst, const std::string& s, int size, SDL_Color c, int x, int y, int align = 0) {
    TTF_Font* f = font(size);
    if (!f || s.empty()) return;
    SDL_Surface* t = TTF_RenderText_Blended(f, s.c_str(), 0, c);
    if (!t) return;
    SDL_Rect r{x, y - t->h / 2, t->w, t->h};
    if (align == 0) r.x -= t->w / 2;
    if (align > 0) r.x -= t->w;
    SDL_Surface* shadow = TTF_RenderText_Blended(f, s.c_str(), 0, SDL_Color{0, 0, 0, 255});
    if (shadow) {
      SDL_Rect sr{r.x + 1, r.y + 2, r.w, r.h};
      SDL_BlitSurface(shadow, nullptr, dst, &sr), SDL_DestroySurface(shadow);
    }
    SDL_BlitSurface(t, nullptr, dst, &r);
    SDL_DestroySurface(t);
  }
  // `s` shortened from the middle with "..." until it is `width` across; its
  // first `keep` characters stay whole (a folder's start and end both show).
  std::string fit(const std::string& s, int size, int width, size_t keep = 0) {
    TTF_Font* f = font(size);
    if (!f) return s;
    auto w_of = [&](const std::string& t) {
      int w = 0, h = 0;
      TTF_GetStringSize(f, t.c_str(), 0, &w, &h);
      return w;
    };
    return fit_middle(s, keep, width, w_of);
  }
  // Wrapped to `width`, lines centred on x from y; the height drawn.
  int draw_wrapped(SDL_Surface* dst, const std::string& s, int size, SDL_Color c, int x, int y, int width) {
    TTF_Font* f = font(size);
    if (!f) return 0;
    std::vector<std::string> lines;
    std::string line, word;
    auto fits = [&](const std::string& t) {
      int w = 0, h = 0;
      TTF_GetStringSize(f, t.c_str(), 0, &w, &h);
      return w <= width;
    };
    auto flush_word = [&] {
      if (word.empty()) return;
      const std::string tryl = line.empty() ? word : line + " " + word;
      if (!fits(tryl) && !line.empty()) lines.push_back(line), line = word;
      else line = tryl;
      word.clear();
    };
    for (char ch : s) {
      if (ch == '\n') flush_word(), lines.push_back(line), line.clear();
      else if (ch == ' ') flush_word();
      else word += ch;
    }
    flush_word();
    if (!line.empty()) lines.push_back(line);
    const int lh = TTF_GetFontLineSkip(f);
    for (size_t i = 0; i < lines.size(); ++i) draw(dst, lines[i], size, c, x, y + static_cast<int>(i) * lh);
    return static_cast<int>(lines.size()) * lh;
  }
};

const SDL_Color kWhite{228, 230, 232, 255}, kAmber{255, 176, 64, 255}, kGrey{120, 124, 130, 255}, kDim{190, 190, 180, 255};

// The main menu's own sounds (sounds.zrd's MAINMENU set: menu_rollover,
// sfx_button2.wav at 0.7; menu_select, sfx_button6.wav), read from the
// player's sound archive as zSnd reads it; none without an install (we ship
// no sounds of our own).
struct Sounds {
  enum { kRollover, kSelect };
  SDL_AudioStream* stream[2] = {};
  Uint8* audio[2] = {};
  Uint32 len[2] = {};
  ~Sounds() {
    for (int i = 0; i < 2; ++i) {
      if (stream[i]) SDL_DestroyAudioStream(stream[i]);
      SDL_free(audio[i]);
    }
  }
  bool load(const std::string& install) {
    static const char* const kFiles[2] = {"sfx_button2.wav", "sfx_button6.wav"};
    static const float kGain[2] = {0.7f, 1.0f};
    for (const char* arc : {"soundsL.zbd", "soundsH.zbd"}) {
      zutl_zar::Archive a;
      zutl_zar::init(a);
      const std::string path = (fs::path(install) / "zbd" / arc).string();
      if (!fs::is_regular_file(path) || !zutl_zar::open(a, path.c_str())) {
        zutl_zar::destroy(a);
        continue;
      }
      int got = 0;
      for (int i = 0; i < 2; ++i) {
        const int e = zutl_zar::find(a, kFiles[i]);
        uint32_t size = 0;
        if (e < 0) continue;
        zutl_zar::read(a, e, nullptr, &size);
        std::vector<Uint8> bytes(size);
        if (!size || zutl_zar::read(a, e, bytes.data(), &size) != 0) continue;
        SDL_AudioSpec spec{};
        if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true, &spec, &audio[i], &len[i])) continue;
        stream[i] = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        // MW3_LAUNCHER_MUTE (checks): loaded and played at no volume.
        if (stream[i]) SDL_SetAudioStreamGain(stream[i], SDL_getenv("MW3_LAUNCHER_MUTE") ? 0.0f : kGain[i]), ++got;
      }
      zutl_zar::close(a);
      zutl_zar::destroy(a);
      if (got) {
        std::printf("mw3: launcher: the menu's sounds from %s (%d of 2)\n", path.c_str(), got);
        return true;
      }
    }
    return false;
  }
  void play(int i) {
    if (!stream[i]) return;
    SDL_ClearAudioStream(stream[i]);
    SDL_PutAudioStreamData(stream[i], audio[i], static_cast<int>(len[i]));
    SDL_ResumeAudioStreamDevice(stream[i]);
  }
};

// ---- the screens ----

struct Button {
  int id = 0;
  SDL_Rect r{};
  std::string label, sub;
  bool enabled = true;
  size_t sub_keep = 0;  // the sub's start kept whole when it is shortened to fit
  int badge = -1;       // a game's entry: its round badge (Art::badge), the name beside it
};

enum Id {
  kMw3 = 1, kPm, kSetup, kQuit, kMw1,
  kFind = 10, kInstall, kPatch, kMusic, kBack,
  kGo = 20, kChooseFile, kCancel, kNotesOk,
};

struct Install {
  bool ok = false;
  std::string dir, version, summary;
};

// An operation on a worker thread (mech3::setup's calls run on the caller's).
struct Work {
  std::thread th;
  std::mutex m;
  mech3::setup::Progress progress;
  mech3::setup::Result result;
  std::atomic<bool> cancel{false}, done{false};
  std::string title;
  ~Work() {
    cancel = true;
    if (th.joinable()) th.join();
  }
  void start(std::string t, std::function<mech3::setup::Result(const mech3::setup::ProgressFn&)> op) {
    title = std::move(t);
    th = std::thread([this, op] {
      mech3::setup::Result r = op([this](const mech3::setup::Progress& p) {
        std::lock_guard<std::mutex> g(m);
        progress = p;
        return !cancel.load();
      });
      std::lock_guard<std::mutex> g(m);
      result = std::move(r);
      done = true;
    });
  }
};

// A picked file or folder from SDL's dialogs (their answers come through a
// callback, on another thread on some systems).
struct Pick {
  std::mutex m;
  int what = 0;  // the button that asked
  std::string path;
  bool answered = false, cancelled = false;
  static void SDLCALL done(void* user, const char* const* list, int) {
    auto* p = static_cast<Pick*>(user);
    std::lock_guard<std::mutex> g(p->m);
    p->answered = true;
    if (list && list[0]) p->path = list[0];
    else p->cancelled = true;
  }
};

class Launcher {
 public:
  explicit Launcher(const LauncherOptions& o) : o_(o) {}
  ~Launcher() {
    if (canvas_) SDL_DestroySurface(canvas_);
    if (tex_) SDL_DestroyTexture(tex_);
    if (ren_) SDL_DestroyRenderer(ren_);
    if (win_) SDL_DestroyWindow(win_);
  }
  LauncherChoice run();

 private:
  void find_install();
  void find_mw1();
  void run_mw1();
  void layout();
  void draw(float dt);
  void background(float dt);
  void plate(const Button& b, bool hot);
  void activate(int id);
  void ask_file(int what, const SDL_DialogFileFilter* filters, int n);
  void ask_folder(int what);
  void picked(int what, const std::string& path);
  void finish_work();
  void present();

  LauncherOptions o_;
  SDL_Window* win_ = nullptr;
  SDL_Renderer* ren_ = nullptr;
  SDL_Texture* tex_ = nullptr;
  SDL_Surface* canvas_ = nullptr;
  Art art_;
  Text text_;
  enum Screen { kMain, kSetupScreen, kConfirm, kWorking, kNotes } screen_ = kMain;
  std::vector<std::string> notes_;  // the first start's (launcher_notes)
  std::vector<Button> buttons_;
  int hot_ = -1, focus_ = -1;
  float hot_t_ = 0, phase_ = 0, scroll_ = 0;
  // The ripple (mech3::menu_ripple_step's state) and the button it is for.
  int frames_ = 0;                    // frames shown, and their draw and present times (the log)
  bool vsync_ = false;
  int quit_after_ = SDL_getenv("MW3_LAUNCHER_FRAMES") ? std::atoi(SDL_getenv("MW3_LAUNCHER_FRAMES")) : 0;
  double draw_ms_ = 0, present_ms_ = 0;
  int ring_for_ = -1, ring_radius_ = 0;
  float ring_carry_ = 0, ring_amp_ = 0, ring_phase_ = 0;
  // The ripple's centre: a game's badge, else the plate's middle.
  SDL_Point ring_centre(const Button& b) const {
    if (b.badge >= 0) return {b.r.x + kBadge / 2 + 4, b.r.y + b.r.h / 2};
    return {b.r.x + b.r.w / 2, b.r.y + b.r.h / 2};
  }
  Install install_;
  std::string mw1_exe_, mw1_game_;  // MechWarrior (1989): Elthial's port beside ours, the player's folder
  std::string status_;
  bool status_good_ = true;
  std::unique_ptr<Work> work_;
  Pick pick_;
  bool asking_ = false;
  std::string confirm_text_, pending_source_;
  int confirm_go_ = 0;
  bool after_install_music_ = false;
  LauncherChoice choice_;
  bool done_ = false;
  Sounds sounds_;
  bool sounds_on_ = false, sounds_tried_ = false;  // loaded once there is an install (at the start or after SETUP)
};

void Launcher::find_install() {
  install_ = {};
  std::vector<std::string> tries;
  if (!o_.cfg_install.empty()) tries.push_back(o_.cfg_install);
  if (!o_.write_dir.empty()) tries.push_back((fs::path(o_.write_dir) / "install").string());
  for (const std::string& c : install_candidates()) tries.push_back(c);
  for (const std::string& c : o_.more_installs) tries.push_back(c);
  for (const std::string& d : tries) {
    if (!install_valid(d)) continue;
    const mech3::setup::Result r = mech3::setup::check_install(d, o_.write_dir);
    if (!r.ok) continue;
    install_ = {true, r.install_dir.empty() ? d : r.install_dir, r.version, r.summary};
    std::printf("mw3: launcher: %s\n", r.summary.c_str());
    return;
  }
  std::printf("mw3: launcher: no MechWarrior 3 install found\n");
}

namespace {
// A MechWarrior (1989) folder: BTECH.EXE in it (any case).
bool mw1_game_valid(const std::string& dir) {
  std::error_code ec;
  if (dir.empty() || !fs::is_directory(dir, ec)) return false;
  for (const fs::directory_entry& e : fs::directory_iterator(dir, ec)) {
    std::string n = e.path().filename().string();
    for (char& c : n) c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
    if (n == "BTECH.EXE") return true;
  }
  return false;
}
}  // namespace

// Elthial's MechWarrior (1989) SDL port (docs/launcher.md): its program
// beside ours or in a MechWarrior1989 folder beside it (MW3_MW1_PORT for
// checks); the entry shows only when it is there.
void Launcher::find_mw1() {
#if defined(_WIN32)
  const char* name = "mechwarrior.exe";
#else
  const char* name = "mechwarrior";
#endif
  std::vector<fs::path> tries;
  if (const char* env = SDL_getenv("MW3_MW1_PORT")) tries.emplace_back(env);
  if (const char* base = SDL_GetBasePath()) tries.push_back(fs::path(base) / name), tries.push_back(fs::path(base) / "MechWarrior1989" / name);
  std::error_code ec;
  for (const fs::path& t : tries)
    if (fs::is_regular_file(t, ec)) {
      mw1_exe_ = t.string();
      std::printf("mw3: launcher: MechWarrior (1989) port at %s\n", mw1_exe_.c_str());
      break;
    }
  if (mw1_game_valid(o_.cfg_mw1_game)) mw1_game_ = o_.cfg_mw1_game;
}

// The port started as its own script starts it (`mechwarrior <game dir>
// <capture dir>`, the saves in MECHWARRIOR_SAVE_DIRECTORY), both under the
// write dir's mw1; this window hidden until it exits.
void Launcher::run_mw1() {
  const fs::path base = o_.write_dir.empty() ? fs::path("mw1") : fs::path(o_.write_dir) / "mw1";
  std::error_code ec;
  fs::create_directories(base / "captures", ec);
  fs::create_directories(base / "saves", ec);
  SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "MECHWARRIOR_SAVE_DIRECTORY", (base / "saves").string().c_str(), true);
  const std::string game = mw1_game_, captures = (base / "captures").string();
  const char* args[] = {mw1_exe_.c_str(), game.c_str(), captures.c_str(), nullptr};
  SDL_Process* p = SDL_CreateProcess(args, false);
  if (!p) {
    status_ = std::string("MechWarrior (1989) did not start: ") + SDL_GetError(), status_good_ = false;
    std::printf("mw3: launcher: %s\n", status_.c_str());
    return;
  }
  std::printf("mw3: launcher: MechWarrior (1989) started\n");
  if (win_) SDL_HideWindow(win_);
  int code = 0;
  while (!SDL_WaitProcess(p, false, &code)) {
    SDL_PumpEvents();
    SDL_Delay(50);
  }
  SDL_DestroyProcess(p);
  std::printf("mw3: launcher: MechWarrior (1989) exited (%d)\n", code);
  status_ = code == 0 ? "MechWarrior (1989) closed." : "MechWarrior (1989) closed with an error (" + std::to_string(code) + "): see its log.";
  status_good_ = code == 0;
  if (win_) SDL_ShowWindow(win_), SDL_RaiseWindow(win_);
}

void Launcher::layout() {
  buttons_.clear();
  auto add = [&](int id, SDL_Rect r, std::string label, std::string sub = {}, bool on = true) {
    buttons_.push_back({id, r, std::move(label), std::move(sub), on});
  };
  if (sounds_on_ && install_.ok && !sounds_tried_) {  // any screen, once there is an install
    sounds_tried_ = true;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) sounds_.load(install_.dir);
  }
  switch (screen_) {
    case kMain: {
      // A long folder is shortened to the plate when drawn (Text::fit), the
      // words before it kept.
      const std::string ready = "Ready: version " + install_.version + ", ";
      std::string sub = install_.ok ? ready + install_.dir : "Not set up yet: click to set it up";
      // The games as round badges (Owen: the ripple belongs to a circle),
      // each name and line beside its badge, the ripple from its centre.
      auto game = [&](int id, int row, int badge, const char* name, const std::string& line, bool on) {
        add(id, {130, 118 + row * 84, 380, 80}, name, line, on);
        buttons_.back().badge = badge;
      };
      game(kMw3, 0, 0, "MECHWARRIOR 3", sub, true);
      if (sub.rfind(ready, 0) == 0) buttons_.back().sub_keep = ready.size();
      game(kPm, 1, 1, "PIRATE'S MOON", "Coming later", false);
      if (!mw1_exe_.empty()) {  // Elthial's port, credited on its line (MIT; THIRD-PARTY.txt)
        const std::string by = "Elthial's SDL port: ";
        game(kMw1, 2, 2, "MECHWARRIOR (1989)", by + (mw1_game_.empty() ? "click to pick your MechWarrior folder" : mw1_game_), true);
        buttons_.back().sub_keep = by.size();
      }
      add(kSetup, {196, 384, 120, 34}, "SETUP");
      add(kQuit, {324, 384, 120, 34}, "QUIT");
      break;
    }
    case kSetupScreen: {
      // The patch only for data of an unknown version: 1.0's and a mix load
      // as 1.2 anyway (mech3/patch12.h, docs/patch12.md).
      const bool patchable = install_.ok && install_.version == "unknown";
      int y = 152;
      add(kFind, {130, y, 380, 44}, "FIND MY INSTALLED MECHWARRIOR 3"), y += 52;
      add(kInstall, {130, y, 380, 44}, "INSTALL FROM MY DISC OR DISC IMAGE"), y += 52;
      if (patchable) add(kPatch, {130, y, 380, 44}, "APPLY THE 1.2 PATCH"), y += 52;
      add(kMusic, {130, y, 380, 44}, "ADD THE MUSIC FROM MY DISC IMAGE");
      add(kBack, {250, 372, 140, 34}, "BACK");
      break;
    }
    case kConfirm:
      if (confirm_go_) add(kGo, {170, 300, 140, 40}, "INSTALL");
      if (confirm_go_ == 2) add(kChooseFile, {130, 250, 380, 40}, "CHOOSE A .ISO OR .CUE FILE INSTEAD");
      add(kBack, {330, 300, 140, 40}, "BACK");
      break;
    case kWorking:
      add(kCancel, {250, 372, 140, 34}, "CANCEL");
      break;
    case kNotes:
      add(kNotesOk, {250, 380, 140, 34}, "OK");
      break;
  }
  hot_ = focus_ = -1;
}

void Launcher::background(float dt) {
  scroll_ += dt * 6.0f;  // a few pixels a second
  SDL_FillSurfaceRect(canvas_, nullptr, SDL_MapSurfaceRGB(canvas_, 8, 8, 12));
  if (art_.stars) {
    const int sw = art_.stars->w, x = static_cast<int>(scroll_) % sw;
    SDL_Rect src{x, 0, std::min(W, sw - x), H}, dst{0, 0, src.w, H};
    SDL_BlitSurface(art_.stars, &src, canvas_, &dst);
    if (src.w < W) {
      SDL_Rect s2{0, 0, W - src.w, H}, d2{src.w, 0, W - src.w, H};
      SDL_BlitSurface(art_.stars, &s2, canvas_, &d2);
    }
  }
  if (art_.metal) {
    const int span = art_.metal->w - W - 200, o = span > 0 ? static_cast<int>(scroll_ / 3) % span : 0;
    SDL_Rect top{o, 40, W, 96}, bot{o + 100, 150, W, 48};
    SDL_Rect dt0{0, 0, W, 96}, db{0, H - 48, W, 48};
    SDL_BlitSurface(art_.metal, &top, canvas_, &dt0);
    SDL_BlitSurface(art_.metal, &bot, canvas_, &db);
  }
  const uint32_t amber = SDL_MapSurfaceRGB(canvas_, 255, 176, 64);
  SDL_Rect l1{0, 96, W, 1}, l2{0, H - 49, W, 1};
  SDL_FillSurfaceRect(canvas_, &l1, amber), SDL_FillSurfaceRect(canvas_, &l2, amber);
  if (art_.title) {
    const int x = (W - art_.title->w - 84) / 2;
    if (art_.emblem) {
      SDL_Rect e{x, 12, art_.emblem->w, art_.emblem->h};
      SDL_BlitSurface(art_.emblem, nullptr, canvas_, &e);
    }
    SDL_Rect t{x + 84, 48 - art_.title->h / 2, art_.title->w, art_.title->h};
    SDL_BlitSurface(art_.title, nullptr, canvas_, &t);
  } else {
    text_.draw(canvas_, "MW3 REBUILD", 40, kWhite, W / 2, 48);
  }
}

void Launcher::plate(const Button& b, bool hot) {
  SDL_Rect r = b.r;
  if (b.badge >= 0) {  // a game: its badge, the name and its line beside it
    const SDL_Point c = ring_centre(b);
    SDL_Surface* art = art_.badge[b.badge];
    const uint8_t m = !b.enabled ? 80 : hot ? 255 : 205;
    if (art) {
      SDL_SetSurfaceColorMod(art, m, m, m);
      SDL_Rect d{c.x - kBadge / 2, c.y - kBadge / 2, kBadge, kBadge};
      SDL_BlitSurface(art, nullptr, canvas_, &d);
    } else {  // no art: a plain disc
      for (int y = -kBadge / 2; y <= kBadge / 2; ++y) {
        const int half = static_cast<int>(std::sqrt(static_cast<float>(kBadge * kBadge / 4 - y * y)));
        SDL_Rect row{c.x - half, c.y + y, half * 2, 1};
        SDL_FillSurfaceRect(canvas_, &row, SDL_MapSurfaceRGB(canvas_, m / 3, m / 3, m / 3 + 8));
      }
    }
    const SDL_Color col = !b.enabled ? kGrey : hot ? kAmber : kWhite;
    const int tx = c.x + kBadge / 2 + 18, tw = r.x + r.w - tx;
    text_.draw(canvas_, b.label, 22, col, tx, c.y - 9, -1);
    text_.draw(canvas_, text_.fit(b.sub, 12, tw, b.sub_keep), 12, b.enabled ? kDim : kGrey, tx, c.y + 14, -1);
    return;
  }
  if (art_.plate) {
    const uint8_t m = !b.enabled ? 90 : hot ? 255 : 215;
    SDL_SetSurfaceColorMod(art_.plate, m, m, m);
    SDL_BlitSurfaceScaled(art_.plate, nullptr, canvas_, &r, SDL_SCALEMODE_LINEAR);
  } else {
    SDL_FillSurfaceRect(canvas_, &r, SDL_MapSurfaceRGB(canvas_, b.enabled ? 70 : 40, b.enabled ? 72 : 40, b.enabled ? 78 : 44));
  }
  const SDL_Color c = !b.enabled ? kGrey : hot ? kAmber : kWhite;
  const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
  const int big = r.h >= 50 ? 22 : r.h >= 40 ? 20 : 18;
  if (b.sub.empty()) {
    text_.draw(canvas_, b.label, big, c, cx, cy + 1);
  } else {
    text_.draw(canvas_, b.label, big, c, cx, cy - 8);
    text_.draw(canvas_, text_.fit(b.sub, 12, r.w - 20, b.sub_keep), 12, b.enabled ? kDim : kGrey, cx, cy + 13);
  }
}

void Launcher::draw(float dt) {
  background(dt);
  switch (screen_) {
    case kMain:
      if (!status_.empty()) {
        SDL_Rect box{110, 104, 420, 24};
        SDL_FillSurfaceRect(canvas_, &box, SDL_MapSurfaceRGB(canvas_, status_good_ ? 16 : 30, status_good_ ? 26 : 14, 16));
        text_.draw(canvas_, status_, 12, status_good_ ? SDL_Color{200, 230, 200, 255} : SDL_Color{240, 190, 180, 255}, W / 2, 116);
      }
      text_.draw(canvas_, "Unofficial fan rebuild. MechWarrior is a trademark of Microsoft.", 12, kDim, 16, H - 24, -1);
      break;
    case kSetupScreen: {
      SDL_Rect box{110, 108, 420, 32};
      SDL_FillSurfaceRect(canvas_, &box, SDL_MapSurfaceRGB(canvas_, 16, 18, 22));
      const std::string s = !status_.empty() ? status_
                            : install_.ok   ? install_.summary
                                            : "No MechWarrior 3 install was found on this computer.";
      text_.draw(canvas_, s, 13, kWhite, W / 2, 124);
      text_.draw(canvas_, "Your MechWarrior 3 files are only read, never changed.", 12, kDim, 16, H - 24, -1);
      break;
    }
    case kNotes: {
      text_.draw(canvas_, "BEFORE YOU PLAY", 24, kAmber, W / 2, 128);
      int y = 162;
      for (const std::string& n : notes_) {
        y += text_.draw_wrapped(canvas_, n, 14, kWhite, W / 2, y, 470) + 14;
      }
      break;
    }
    case kConfirm:
      text_.draw_wrapped(canvas_, confirm_text_, 16, kWhite, W / 2, 150, 460);
      break;
    case kWorking: {
      mech3::setup::Progress p;
      std::string title;
      if (work_) {
        std::lock_guard<std::mutex> g(work_->m);
        p = work_->progress, title = work_->title;
      }
      text_.draw(canvas_, title, 26, kAmber, W / 2, 140);
      SDL_Rect bar{120, 200, 400, 26}, in{123, 203, static_cast<int>(394 * std::clamp(p.fraction, 0.0, 1.0)), 20};
      SDL_FillSurfaceRect(canvas_, &bar, SDL_MapSurfaceRGB(canvas_, 140, 100, 50));
      SDL_Rect inner{122, 202, 396, 22};
      SDL_FillSurfaceRect(canvas_, &inner, SDL_MapSurfaceRGB(canvas_, 10, 12, 14));
      SDL_FillSurfaceRect(canvas_, &in, SDL_MapSurfaceRGB(canvas_, 214, 140, 40));
      char pct[16];
      std::snprintf(pct, sizeof pct, "%d%%", static_cast<int>(std::lround(p.fraction * 100)));
      text_.draw(canvas_, pct, 14, kWhite, W / 2, 214);
      text_.draw(canvas_, p.stage, 13, kWhite, W / 2, 246);
      text_.draw(canvas_, p.message, 10, kGrey, W / 2, 266);
      text_.draw(canvas_, "Cancel stops within a file and changes nothing.", 12, kDim, 16, H - 24, -1);
      break;
    }
  }
  const int hot = focus_ >= 0 ? focus_ : hot_;
  for (size_t i = 0; i < buttons_.size(); ++i) plate(buttons_[i], static_cast<int>(i) == hot && buttons_[i].enabled);
  // The main menu's own ripple (its step, mech3::menu_ripple_step, and its
  // draw 0x00558dd0) round the button under the mouse, started afresh on
  // each new one as SlideButton's on_enter does.
  hot_t_ += dt;
  if (hot != ring_for_) {
    ring_for_ = hot, ring_radius_ = mech3::MenuRipple::kStartRadius, ring_carry_ = 0.0f;
    ring_amp_ = mech3::MenuRipple::kStartAmplitude, ring_phase_ = 0.0f;
  }
  if (hot >= 0 && hot < static_cast<int>(buttons_.size()) && buttons_[hot].enabled &&
      mech3::menu_ripple_step(ring_radius_, ring_carry_, ring_amp_, ring_phase_, mech3::MenuRipple::kSpeed,
                              mech3::MenuRipple::kWavelength, mech3::MenuRipple::kMaxRadius, dt)) {
    const SDL_Point c = ring_centre(buttons_[hot]);
    const float wl = mech3::MenuRipple::kWavelength;
    zrndr::set_finish_target(static_cast<uint16_t*>(canvas_->pixels), W, H, canvas_->pitch);
    zrndr::ripple(c.x, c.y, static_cast<int32_t>(static_cast<float>(ring_radius_) - wl * 3.0f), ring_radius_,
                  static_cast<int32_t>(ring_amp_), wl, ring_phase_, nullptr, zrndr::X87::k53);
  }
}

void Launcher::present() {
  SDL_UpdateTexture(tex_, nullptr, canvas_->pixels, canvas_->pitch);
  SDL_SetRenderDrawColor(ren_, 0, 0, 0, 255);
  SDL_RenderClear(ren_);
  SDL_RenderTexture(ren_, tex_, nullptr, nullptr);
  SDL_RenderPresent(ren_);
}

void Launcher::ask_file(int what, const SDL_DialogFileFilter* filters, int n) {
  {
    std::lock_guard<std::mutex> g(pick_.m);
    pick_.what = what, pick_.path.clear(), pick_.answered = pick_.cancelled = false;
  }
  asking_ = true;
  SDL_ShowOpenFileDialog(Pick::done, &pick_, win_, filters, n, nullptr, false);
}
void Launcher::ask_folder(int what) {
  {
    std::lock_guard<std::mutex> g(pick_.m);
    pick_.what = what, pick_.path.clear(), pick_.answered = pick_.cancelled = false;
  }
  asking_ = true;
  SDL_ShowOpenFolderDialog(Pick::done, &pick_, win_, nullptr, false);
}

// A picked path: what the button that asked does with it.
void Launcher::picked(int what, const std::string& path) {
  using namespace mech3::setup;
  if (what == kMw1) {
    const std::string dir = fs::is_regular_file(path) ? fs::path(path).parent_path().string() : path;
    if (!mw1_game_valid(dir)) {
      status_ = "That folder has no BTECH.EXE: it is not MechWarrior (1989).", status_good_ = false;
      return;
    }
    mw1_game_ = dir;
    save_config_value(o_.write_dir, "mw1_game", dir);
    layout();
    run_mw1();
    return;
  }
  if (what == kFind) {
    const std::string dir = fs::is_regular_file(path) ? fs::path(path).parent_path().string() : path;
    const Result r = check_install(dir, o_.write_dir);
    if (r.ok) {
      install_ = {true, r.install_dir.empty() ? dir : r.install_dir, r.version, r.summary};
      save_install(o_.write_dir, install_.dir);
      status_ = r.summary, status_good_ = true, screen_ = kMain;
    } else {
      status_ = r.summary.empty() ? "That folder is not a MechWarrior 3 install." : r.summary, status_good_ = false;
    }
  } else if (what == kInstall || what == kChooseFile) {
    const Result r = probe_source(path);
    confirm_text_ = r.summary;
    for (const std::string& w : r.warnings) confirm_text_ += "\n" + w;
    pending_source_ = path;
    confirm_go_ = r.ok && r.install_dir.empty() ? 1 : 0;
    if (r.ok && !r.install_dir.empty()) {  // an installed copy, not a disc: use it
      install_ = {true, r.install_dir, r.version, r.summary};
      save_install(o_.write_dir, install_.dir);
      status_ = r.summary, status_good_ = true, screen_ = kMain;
      layout();
      return;
    }
    screen_ = kConfirm;
  } else if (what == kPatch) {
    work_ = std::make_unique<Work>();
    const std::string wd = o_.write_dir;
    work_->start("APPLYING THE 1.2 PATCH", [path, wd](const ProgressFn& f) { return apply_patch(path, wd, f); });
    screen_ = kWorking;
  } else if (what == kMusic) {
    work_ = std::make_unique<Work>();
    const std::string music = (fs::path(o_.write_dir) / "music").string();
    work_->start("ADDING THE MUSIC", [path, music](const ProgressFn& f) { return add_music(path, music, f); });
    screen_ = kWorking;
  }
  layout();
}

void Launcher::finish_work() {
  mech3::setup::Result r;
  {
    std::lock_guard<std::mutex> g(work_->m);
    r = work_->result;
  }
  if (work_->th.joinable()) work_->th.join();
  std::printf("mw3: launcher: %s%s%s\n", r.summary.c_str(), r.detail.empty() ? "" : " | ", r.detail.c_str());
  status_ = r.summary, status_good_ = r.ok;
  if (r.ok && !r.install_dir.empty()) {
    save_install(o_.write_dir, r.install_dir);
    after_install_music_ = r.music_tracks == 0 && fs::path(pending_source_).extension() == ".iso";
  }
  find_install();
  work_.reset();
  screen_ = install_.ok && r.ok ? kMain : kSetupScreen;
  layout();
}

void Launcher::activate(int id) {
  sounds_.play(Sounds::kSelect);
  static const SDL_DialogFileFilter kDisc[] = {{"Disc images (.iso, .cue)", "iso;cue"}, {"All files", "*"}};
  static const SDL_DialogFileFilter kCue[] = {{"Cue sheets (.cue)", "cue"}, {"All files", "*"}};
  static const SDL_DialogFileFilter kZip[] = {{"The 1.2 patch (.zip, .exe)", "zip;exe"}, {"All files", "*"}};
  switch (id) {
    case kMw3:
      if (install_.ok) {
        choice_ = {LauncherChoice::kPlay, install_.dir};
        done_ = true;
      } else {
        screen_ = kSetupScreen, status_.clear(), layout();
      }
      break;
    case kSetup: screen_ = kSetupScreen, status_.clear(), layout(); break;
    case kQuit: done_ = true; break;
    case kMw1:
      if (mw1_game_.empty()) {
        status_ = "Pick your MechWarrior (1989) folder: the one with BTECH.EXE.", status_good_ = true;
        ask_folder(kMw1);
      } else {
        run_mw1();
      }
      break;
    case kBack:
      screen_ = screen_ == kConfirm ? kSetupScreen : kMain, layout();
      break;
    case kNotesOk:
      screen_ = kMain, layout();
      break;
    case kFind:
      find_install();
      if (install_.ok) {
        save_install(o_.write_dir, install_.dir);
        status_ = install_.summary, status_good_ = true, screen_ = kMain, layout();
      } else {
        ask_folder(kFind);
      }
      break;
    case kInstall: {
      const std::vector<mech3::setup::DiscSource> discs = mech3::setup::find_discs();
      if (!discs.empty()) {
        picked(kInstall, discs.front().path);
        if (screen_ == kConfirm) confirm_go_ = confirm_go_ ? 2 : 0, layout();
      } else {
        ask_file(kInstall, kDisc, 2);
      }
      break;
    }
    case kChooseFile: ask_file(kChooseFile, kDisc, 2); break;
    case kPatch: ask_file(kPatch, kZip, 2); break;
    case kMusic: ask_file(kMusic, kCue, 2); break;
    case kGo: {
      work_ = std::make_unique<Work>();
      const std::string src = pending_source_;
      const std::string out = (fs::path(o_.write_dir) / "install").string();
      const std::string music = (fs::path(o_.write_dir) / "music").string();
      work_->start("INSTALLING MECHWARRIOR 3",
                   [src, out, music](const mech3::setup::ProgressFn& f) { return mech3::setup::install_from_disc(src, out, "", music, f); });
      screen_ = kWorking, layout();
      break;
    }
    case kCancel:
      if (work_) work_->cancel = true;
      break;
    default: break;
  }
}

LauncherChoice Launcher::run() {
  const bool shot = !o_.shot.empty();
  // Where the start's time goes (Owen: "pretty slow"), in the log.
  const Uint64 t0 = SDL_GetTicksNS();
  auto ms = [](Uint64 a, Uint64 b) { return static_cast<double>(b - a) / 1e6; };
  const std::string dir = assets_dir();
  art_.load(dir);
  const Uint64 t_art = SDL_GetTicksNS();
  text_.open(font_path(dir));
  canvas_ = SDL_CreateSurface(W, H, SDL_PIXELFORMAT_RGB565);
  const Uint64 t_font = SDL_GetTicksNS();
  find_install();
  const Uint64 t_install = SDL_GetTicksNS();
  find_mw1();
  std::printf("mw3: launcher: art %.0f ms, font %.0f ms, the install search %.0f ms\n", ms(t0, t_art), ms(t_art, t_font),
              ms(t_font, t_install));
  sounds_on_ = o_.sound && !shot;
  // The first start's notes (no install= in mw3.cfg yet: PLAY saves one),
  // on their own screen before the main one; logged on every start.
  if (install_.ok) {
    const fs::path music = o_.write_dir.empty() ? fs::path("music") : fs::path(o_.write_dir) / "music";
    const bool have = have_music({music.string(), (fs::path(install_.dir) / "music").string()});
    const std::string dir = fs::path(music).make_preferred().string();
    for (const std::string& n : launcher_notes(install_.version, have, dir)) std::printf("mw3: launcher: note: %s\n", n.c_str());
    notes_ = launcher_notes(install_.version, have, text_.fit(dir, 14, 440));  // shown: a long folder shortened (one word)
  }
  const bool first_start = o_.cfg_install.empty() && !notes_.empty();
  if (shot) {
    if (o_.screen == "setup") screen_ = kSetupScreen;
    if (o_.screen == "notes") screen_ = kNotes;
    if (o_.screen == "work") {
      screen_ = kWorking;
      work_ = std::make_unique<Work>();
      work_->title = "INSTALLING MECHWARRIOR 3";
      work_->progress = {"Copying the movies", 0.62, "VIDEO\\C1M3.AVI"};
    }
    layout();
    if (!buttons_.empty()) hot_ = 0, hot_t_ = 0.4f;
    scroll_ = 120.0f;
    const Uint64 td = SDL_GetTicksNS();
    draw(0.0f);
    for (int i = 0; i < 6; ++i) draw(1.0f / 30.0f);  // 0.2 s on: the hovered entry's ripple spreading
    std::printf("mw3: launcher: first frame drawn %.0f ms after the start (its draw %.1f ms)\n", ms(t0, SDL_GetTicksNS()),
                ms(td, SDL_GetTicksNS()));
    SDL_SaveBMP(canvas_, o_.shot.c_str());
    work_.reset();
    return {};
  }
  if (!install_.ok) screen_ = kSetupScreen;
  else if (first_start) screen_ = kNotes;
  layout();
  // Not high pixel density: the game takes this window (one window into the
  // game), and its sizes are the window's points (SdlWindow, SdlInput).
  SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
  if (o_.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
  win_ = SDL_CreateWindow("MechWarrior 3", o_.win_w, o_.win_h, flags);
  if (!win_ || !(ren_ = SDL_CreateRenderer(win_, nullptr))) {
    std::fprintf(stderr, "mw3: launcher: no window (%s): straight to the game\n", SDL_GetError());
    return install_.ok ? LauncherChoice{LauncherChoice::kPlay, install_.dir} : LauncherChoice{};
  }
  SDL_SetRenderLogicalPresentation(ren_, W, H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
  // Paced by the display (Owen: "pretty slow"): a fixed SDL_Delay(15) was
  // 16 to 31 ms on Windows' 15.6 ms timer, and no vsync let frames tear.
  // MW3_LAUNCHER_NOVSYNC (checks): the timed sleep instead, to compare.
  vsync_ = !SDL_getenv("MW3_LAUNCHER_NOVSYNC") && SDL_SetRenderVSync(ren_, 1);
  tex_ = SDL_CreateTexture(ren_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, W, H);
  SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_LINEAR);
  Uint64 last = SDL_GetTicks();
  if (o_.play) activate(kMw3);
  if (o_.play_mw1) activate(kMw1), done_ = !asking_;
  while (!done_) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      SDL_ConvertEventToRenderCoordinates(ren_, &e);
      if (e.type == SDL_EVENT_QUIT) done_ = true;
      if (asking_ || screen_ == kWorking) {
        if (screen_ == kWorking && e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
          for (const Button& b : buttons_)
            if (e.button.x >= b.r.x && e.button.x < b.r.x + b.r.w && e.button.y >= b.r.y && e.button.y < b.r.y + b.r.h) activate(b.id);
        continue;
      }
      if (e.type == SDL_EVENT_MOUSE_MOTION) {
        int h = -1;
        for (size_t i = 0; i < buttons_.size(); ++i) {
          const SDL_Rect& r = buttons_[i].r;
          if (e.motion.x >= r.x && e.motion.x < r.x + r.w && e.motion.y >= r.y && e.motion.y < r.y + r.h) h = static_cast<int>(i);
        }
        if (h != hot_) {
          hot_ = h, hot_t_ = 0, focus_ = -1;
          if (h >= 0 && buttons_[h].enabled) sounds_.play(Sounds::kRollover);
        }
      } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        if (hot_ >= 0 && buttons_[hot_].enabled) activate(buttons_[hot_].id);
      } else if (e.type == SDL_EVENT_KEY_DOWN) {
        const SDL_Keycode k = e.key.key;
        const int n = static_cast<int>(buttons_.size());
        auto step = [&](int d) {
          int f = focus_ >= 0 ? focus_ : hot_;
          for (int i = 0; i < n; ++i) {
            f = ((f < 0 ? (d > 0 ? -1 : 0) : f) + d + n) % n;
            if (buttons_[f].enabled) break;
          }
          focus_ = f, hot_t_ = 0;
          if (f >= 0) sounds_.play(Sounds::kRollover);
        };
        if (k == SDLK_DOWN || k == SDLK_TAB) step(1);
        else if (k == SDLK_UP) step(-1);
        else if ((k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_SPACE) && (focus_ >= 0 ? focus_ : hot_) >= 0)
          activate(buttons_[focus_ >= 0 ? focus_ : hot_].id);
        else if (k == SDLK_ESCAPE) activate(screen_ == kMain ? kQuit : kBack);
      }
    }
    if (asking_) {
      int what = 0;
      std::string path;
      bool answered = false, cancelled = false;
      {
        std::lock_guard<std::mutex> g(pick_.m);
        answered = pick_.answered, cancelled = pick_.cancelled, what = pick_.what, path = pick_.path;
      }
      if (answered) {
        asking_ = false;
        if (!cancelled) picked(what, path);
      }
    }
    if (work_ && work_->done) finish_work();
    const Uint64 now = SDL_GetTicks();
    const float dt = std::min(0.1f, static_cast<float>(now - last) / 1000.0f);
    last = now;
    const Uint64 ta = SDL_GetTicksNS();
    draw(dt);
    const Uint64 tb = SDL_GetTicksNS();
    present();
    const Uint64 tc = SDL_GetTicksNS();
    if (!frames_) std::printf("mw3: launcher: first frame shown %.0f ms after the start\n", ms(t0, tc));
    ++frames_, draw_ms_ += ms(ta, tb), present_ms_ += ms(tb, tc);
    if (quit_after_ > 0 && frames_ >= quit_after_) done_ = true;  // MW3_LAUNCHER_FRAMES (checks)
    if (!vsync_) {  // no vsync: the rest of a 60th of a second, to the nanosecond
      const Uint64 spent = SDL_GetTicksNS() - ta, frame = 1000000000ull / 60;
      if (spent < frame) SDL_DelayPrecise(frame - spent);
    }
  }
  if (frames_) {
    const double secs = ms(t0, SDL_GetTicksNS()) / 1000.0;
    std::printf("mw3: launcher: %d frames, %.1f a second; the draw %.2f ms, the present %.2f ms a frame (%s)\n", frames_,
                frames_ / std::max(secs, 0.001), draw_ms_ / frames_, present_ms_ / frames_, ren_ ? SDL_GetRendererName(ren_) : "-");
    std::printf("mw3: launcher: paced by %s\n", vsync_ ? "vsync" : "a timed sleep to 60 a second");
  }
  if (work_) work_->cancel = true;
  if (choice_.kind == LauncherChoice::kPlay) {
    save_install(o_.write_dir, choice_.install);
    // The window goes on to the game, its renderer freed first (a window
    // drawn by an SDL renderer cannot be the GPU device's too).
    if (tex_) SDL_DestroyTexture(tex_), tex_ = nullptr;
    if (ren_) SDL_DestroyRenderer(ren_), ren_ = nullptr;
    // Its window's events so far are the launcher's: a focus lost to another
    // app would reach the game as a deactivation and leave it waiting.
    SDL_FlushEvents(SDL_EVENT_WINDOW_FIRST, SDL_EVENT_WINDOW_LAST);
    choice_.window = win_;
    win_ = nullptr;
    if (choice_.window) std::printf("mw3: launcher: its window goes on to the game\n");
  }
  return choice_;
}

}  // namespace

LauncherChoice run_launcher(const LauncherOptions& o) {
  Launcher l(o);
  return l.run();
}

}  // namespace sdlfe
