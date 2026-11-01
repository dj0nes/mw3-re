#pragma once
// The launcher and the first-start setup (ours, not original code;
// docs/launcher.md): the first screen of a player's start, before any
// movie. It finds the MechWarrior 3 install quietly, offers the games, and
// when there is no install a setup screen with buttons (find it, install from
// the disc or an image, the 1.2 patch, the music) over mech3::setup. Its art
// is ours (assets/launcher); the ripple under the mouse is the main menu's
// (zrndr::ripple, 0x00558dd0).
#include <string>
#include <vector>

struct SDL_Window;

namespace sdlfe {

struct LauncherOptions {
  std::string write_dir;
  std::string cfg_install;                // mw3.cfg's install=
  std::string cfg_mw1_game;               // mw3.cfg's mw1_game= (MechWarrior 1989's folder)
  std::vector<std::string> more_installs;  // a development build's copy
  bool fullscreen = true;
  bool sound = true;  // the main menu's rollover and select sounds from the install (off with --nosound)
  int win_w = 1280, win_h = 960;
  // For checking (--launcher-shot FILE [--launcher-screen main|setup|work]):
  // the screen drawn once into FILE (a BMP of the 640x480 canvas), no window shown.
  std::string shot, screen = "main";
  bool play = false;  // --launcher-play: MECHWARRIOR 3 pressed once the window is up (checks)
  bool play_mw1 = false;  // --launcher-mw1: MECHWARRIOR (1989) pressed once, then quit when it returns (checks)
};

struct LauncherChoice {
  enum Kind { kQuit, kPlay } kind = kQuit;
  std::string install;  // kPlay: MechWarrior 3's folder (saved to mw3.cfg)
  // kPlay: the launcher's window, its renderer gone, for the game to take
  // (SdlWindow::init): one window from the launcher into the game.
  SDL_Window* window = nullptr;
};

LauncherChoice run_launcher(const LauncherOptions& o);

}  // namespace sdlfe
