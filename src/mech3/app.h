#pragma once
#include <cstdint>
#include <string>

#include "gamez/zgame/zgameapp.h"
#include "mech3/app_hooks.h"

// MechWarrior 3's application object (the MFC CWinApp at 0x00608c88, vtable
// 0x005c3f80) and its start-up. Original file unknown: it sits between
// mech3\mech_waynet.cpp and mech3\mission.cpp with the game states it drives
// (app_states.cpp). docs/startup.md, docs/game-loop.md.
//
// Much of the game reads the app's fields by absolute address: 0x00608e14..
// 0x00608e64 are the app's +0x18c..+0x1dc. Each field below gives both.
namespace mech3 {

class State;

// The true globals of this file.
struct AppGlobals {
  uint8_t class_registered = 0;  // 0x00608f6c  window class registered (ExitInstance)
  uint8_t tampered = 0;          // 0x00608f70  the exe self-check's result: never set (FIX (trap))
  Handle debug_view = 0;         // 0x00608f74  an optional overlay drawn instead of 0x0041ab90
  int32_t last_vmode = 0;        // 0x00608f78
  Handle main_hwnd = 0;          // 0x00608f80
  Handle script = 0;             // 0x006040b0  the interp script object (0x00410700)
};
AppGlobals& globals();

class Mech3App;

// The command-line switches (in 0x00482d10): space-separated, prefix-matched,
// the program name included. -nofmv, -seasick (FastMenu), -cd / -cdN.
void parse_command_line(Engine& e, Mech3App& app, const char* line);

class Mech3App : public zgame::App {
 public:
  explicit Mech3App(Engine* engine);                       // 0x0047f7a0

  State* state(int n);                                     // 0x00482970
  void set_defaults();                                     // 0x0047fb80
  void pick_post_intro_state(bool lobby);                  // 0x0047fed0
  void restore_video(bool hw, bool pick);                  // 0x0047fe10
  void gpu_only();  // ours, FIX (A): a saved software renderer choice becomes the GPU's
  void set_reopen_instant_action(uint8_t a, uint8_t b);    // 0x0047fc20
  void set_reopen_training(uint8_t v);                     // 0x0047fe00
  void restart_in_lab();                                   // 0x0047ff60
  bool confirm_quit();                                     // 0x00482a20
  bool pre_translate_message(uint32_t msg);                // 0x004803f0, +0x60
  bool init_instance();                                    // 0x00480000, +0x58
  int exit_instance() override;                            // 0x0047fa10, +0x70
  void destroy();                                          // 0x0047fab0, the destructor's work
  bool start_game(uint32_t window) override;               // 0x00480570, +0xac
  void stop_game() override;                               // 0x004808a0, +0xb0
  uint32_t main_window() override { return engine_->frame_hwnd(frame_); }  // [0x0047fff0()+0x20]
  Handle create_main_window();                             // 0x0047ff90 -> 0x00482d10, +0xbc
  bool open_video(uint32_t window);                        // 0x00480970
  bool check_video_memory(bool hw3d, int32_t* message);    // 0x004804f0

  Engine& engine() { return *engine_; }

  // Fields (offset in the object, absolute address).
  Handle frame_ = 0;              // +0x020  the main frame window (CWnd)
  uint32_t cmd_show_ = 0;         // +0x06c  passed to the input setup
  uint8_t mission_started_ = 0;   // +0x18c 0x00608e14
  uint8_t lobby_launch_ = 0;      // +0x18d 0x00608e15
  uint8_t lobby_flow_ = 0;        // +0x18e 0x00608e16  pick_post_intro_state(1): no Intro movies
  int32_t input_mode_ = 0;        // +0x190 0x00608e18  5; bits 2-3 == 2: the mouse follows the cursor
  uint8_t field_194_ = 0;         // +0x194 0x00608e1c
  std::string mech_ = "";         // +0x198 0x00608e20  default mech, "bushwacker"
  uint8_t lobby_ = 0;             // +0x19c 0x00608e24  started from a DirectPlay lobby
  uint8_t field_1a4_ = 0;         // +0x1a4 0x00608e2c
  // +0x1ac..+0x1db (0x00608e34..0x00608e63), set by set_defaults, by offset:
  //   +0x1ac..+0x1b1 1, +0x1b2 1 (0x00608e3a), +0x1b4 the -cd track (dword,
  //   0x00608e3c), +0x1bd a network game is on (options().multiplayer),
  //   +0x1c1 movies on (-nofmv), +0x1c3 no briefing voices, +0x1c4/+0x1c5 and
  //   +0x1cc..+0x1d4 GameIntensity's values (in options()), +0x1c7 full
  //   screen, +0x1d8 lock the system keys. The main menu presses a button on
  //   entry to reopen the menu a game came from: +0x1bc Instant Action (with
  //   +0x1bf), +0x1be Training, +0x1c6 Credits (after the campaign's end).
  uint8_t block_[0x30] = {};
  State* post_intro_ = nullptr;   // +0x1dc 0x00608e64  where the Intro state goes
  Handle field_1e0_ = 0, field_1e4_ = 0;  // +0x1e0 +0x1e4

  uint8_t& byte_at(int off) { return block_[off - 0x1ac]; }
  int32_t cd_track() const;
  void set_cd_track(int32_t track);
  uint8_t& reopen_instant_action() { return byte_at(0x1bc); }
  uint8_t& multiplayer();
  uint8_t& reopen_training() { return byte_at(0x1be); }
  uint8_t& instant_action_arg() { return byte_at(0x1bf); }
  uint8_t& fmv() { return byte_at(0x1c1); }
  uint8_t& no_voices() { return byte_at(0x1c3); }
  uint8_t& reopen_credits() { return byte_at(0x1c6); }
  uint8_t& fullscreen() { return byte_at(0x1c7); }
  uint8_t& lock_system_keys() { return byte_at(0x1d8); }

 private:
  Engine* engine_;
};

Mech3App& app();             // the one at 0x00608c88
Mech3App* app_if_any();      // null before set_app
void set_app(Mech3App* app);

}  // namespace mech3
