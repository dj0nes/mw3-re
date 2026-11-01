#pragma once
#include <cstdint>
#include <vector>

#include "gamez/zgame/zgameapp.h"
#include "mech3/app_hooks.h"

// The game states the app's stack runs (docs/game-loop.md). The static objects
// at 0x00608e70..0x00608f58 are picked by Mech3App::state(n) (0x00482970).
// Attract, Intro, Exit, Pre Lab, Core and the key list sit next to the app
// (original file unknown, between mech3\mech_waynet.cpp and mech3\mission.cpp);
// the menu states share a base, the "shell" state (0x0048eb10, vtable
// 0x005c6038), whose screens are the 2D front end's.
namespace mech3 {

class Mech3App;

class State : public zgame::State {
 public:
  State(const char* name, uint32_t addr) : name_(name), addr_(addr) {}
  const char* name() const { return name_; }  // +0x04, _strdup'd in the original
  // The static object's address in the original: how hooks name the state.
  uint32_t addr() const { return addr_; }

 protected:
  Engine& e();
  Mech3App& a();

 private:
  const char* name_;
  uint32_t addr_;
};

// 0 "Attract State" (0x00608e90, vtable 0x005c4078): the ATTRACT movie, shown
// when the main menu sits idle for 60 seconds.
class AttractState : public State {
 public:
  AttractState() : State("Attract State", 0x608e90) {}
  bool enter() override;              // 0x00482330
  bool update() override;             // 0x00482430
  void leave() override;              // 0x00482470
  uint32_t message(uint32_t, uint32_t) override { return 1; }  // 0x004c93c0
  Handle movie_ = 0;                  // +0x08
};

// "Win Movie State" (0x00608be8, constructor 0x0047a960, vtable 0x005c3958):
// the campaign's ending, fmv.zrd's c4win, then the main menu with its credits
// (the debriefing's CONTINUE after Op 4 Mission 6, campaign.md).
class WinMovieState : public State {
 public:
  WinMovieState() : State("Win Movie State", 0x608be8) {}
  bool enter() override;              // 0x0047a9b0
  bool update() override;             // 0x0047aa50
  void leave() override;              // 0x0047aac0
  uint32_t message(uint32_t, uint32_t) override { return 1; }  // 0x0042a130
  Handle movie_ = 0;                  // +0x08
};

// "IA Dummy State" (0x00608bd0, constructor 0x0047ce40, vtable 0x005c3c60):
// after Instant Action's debriefing (ABORT, through a fade): the mechs'
// kills and Attack's or Defend's result into the pilot's record, the pilot
// saved, the mission unloaded, then the main menu, which opens Instant
// Action again (app +0x1bc, +0x1bf).
class IaDummyState : public State {
 public:
  IaDummyState() : State("IA Dummy State", 0x608bd0) {}
  bool enter() override;              // 0x0047ce80
  bool update() override { return false; }  // 0x0040bae0
  uint32_t message(uint32_t, uint32_t) override { return 1; }  // 0x0042a130
};

// "TR Dummy State" (0x006098c8, constructor 0x00491a70, vtable 0x005c60e0):
// after a training mission's end screen (through a fade): won, the mission
// marked passed and the pilot saved; the mission unloaded, then the main
// menu, which opens training again (app +0x1be).
class TrDummyState : public State {
 public:
  TrDummyState() : State("TR Dummy State", 0x6098c8) {}
  bool enter() override;              // 0x00491ab0
  bool update() override { return false; }  // 0x0040bae0
  uint32_t message(uint32_t, uint32_t) override { return 1; }  // 0x0042a130
};

// "Multi-Exit State" (0x00605fa8, constructor 0x00470650, vtable 0x005c3680):
// after a network game's end (0x004707c0, through a fade): the mission
// unloaded, then the main menu.
class MultiExitState : public State {
 public:
  MultiExitState() : State("Multi-Exit State", 0x605fa8) {}
  bool enter() override;              // 0x00470710
  bool update() override { return false; }  // 0x0040bae0
  uint32_t message(uint32_t, uint32_t) override { return 1; }  // 0x004a5bc0
};

// 1 "Intro State" (0x00608f58, vtable 0x005c40c8): INTRO, then the SPLASH logos.
class IntroState : public State {
 public:
  IntroState() : State("Intro State", 0x608f58) {}
  bool enter() override;              // 0x00482070
  bool update() override;             // 0x00482170
  void leave() override;              // 0x004822d0
  uint32_t message(uint32_t, uint32_t) override { return 1; }  // 0x004c93c0
  Handle intro_ = 0;                  // +0x08
  Handle splash_ = 0;                 // +0x0c
};

// 4 "Exit State" (0x00608ef8, vtable 0x005c4178): stops the game, then quits.
class ExitState : public State {
 public:
  ExitState() : State("Exit State", 0x608ef8) {}
  bool enter() override;              // 0x00482060
  bool update() override { return true; }  // 0x00499270
};

// 7 "Pre Lab State" (0x00608f38, vtable 0x005c4120): the debug menu's Mech Lab.
class PreLabState : public State {
 public:
  PreLabState() : State("Pre Lab State", 0x608f38) {}
  bool enter() override;              // 0x00480b30
  bool update() override;             // 0x00480b60
  void leave() override {}            // 0x005ac5c0
};

// 5 "Core State" (0x00608ec0, vtable 0x005c4040): the mission. Its frame picks
// one of three drawing paths (the objects at vtables 0x005c4068/6c/70).
class CoreState : public State {
 public:
  enum Renderer : int { kNone = -1, kSoftware = 0, kDoubled = 1, kHardware = 2 };
  CoreState() : State("Core State", 0x608ec0) {}
  void on_activate(uint32_t active) override;  // 0x00481280
  bool enter() override;              // 0x00480c70
  bool update() override;             // 0x00481f70
  void leave() override;              // 0x00481fc0
  void suspend(uint32_t arg) override;  // 0x00481250
  void resume(uint32_t arg) override;   // 0x00481170

  bool frame();                       // 0x00481e70
  void pick_renderer();               // 0x00480bf0
  void start_mission();               // 0x00480df0
  bool draw_software(Handle veh, bool flag);  // 0x00481290
  bool draw_doubled(Handle veh, bool flag);   // 0x00481a10
  bool draw_hardware(Handle veh, bool flag);  // 0x00481790

  Handle window_rect_ = 0;            // +0x08
  Handle display_rect_ = 0;           // +0x0c
  Handle render_rect_ = 0;            // +0x10
  float elapsed_ = 0;                 // +0x14
  uint8_t paused_ = 0;                // +0x18
  uint8_t active_ = 0;                // +0x19
  int renderer_ = kNone;              // +0x1c
  Handle message_box_ = 0;            // +0x20
};

// 10, unnamed (0x00608ea0, vtable 0x005c40a0): a list of the 0xa7 input
// commands and their keys, likely a debug screen.
class KeyListState : public State {
 public:
  KeyListState() : State("", 0x608ea0) {}
  bool enter() override;              // 0x00482570
  bool update() override;             // 0x00482520
  void leave() override;              // 0x00482550
  Handle list_ = 0;                   // +0x18
};

// The menu states' base (0x0048eb10): a 2D screen at +0x0c.
class ShellState : public State {
 public:
  ShellState(const char* name, uint32_t addr) : State(name, addr) {}
  bool enter() override;              // 0x0048ebf0
  bool update() override;             // 0x0048ed30
  void leave() override;              // 0x0048ec40
  void suspend(uint32_t arg) override;  // 0x0048ec80
  void resume(uint32_t arg) override;   // 0x0048ecc0
  virtual void escape() {}            // +0x28, command 1 (Escape)
  void refresh_target();              // 0x0048edc0
  void clear_screen();                // 0x0048ee00

  uint32_t cursor_ = 0;               // +0x08  the cursor state before entering
  Handle screen_ = 0;                 // +0x0c
};

// 2 "Main State" (0x00608f00, vtable 0x005c5030): the main menu.
class MainState : public ShellState {
 public:
  MainState() : ShellState("Main State", 0x608f00) {}
  bool enter() override;              // 0x00486990
  bool update() override;             // 0x00486d40
  void leave() override;              // 0x00486c50
  void suspend(uint32_t arg) override;  // 0x0048e400
  void resume(uint32_t arg) override;   // 0x00486e80
  void escape() override;             // 0x00486ec0
  void reset_idle() { idle_ = 60.0f; }  // 0x00486980
  uint32_t save_pilots_ = 0;          // +0x10  (never set by this file)
  float idle_ = 0;                    // +0x14  seconds left before the attract movie
};

// 3 "Pause State" (0x00608f48, vtable 0x005c40f0).
class PauseState : public ShellState {
 public:
  PauseState() : ShellState("Pause State", 0x608f48) {}
  bool enter() override;              // 0x00493040
  void leave() override;              // 0x004931e0
  void escape() override;             // 0x00493250
  void resume(uint32_t arg) override;  // ShellState's; ours first: the mission's mode back (W36)
};

// 6 "Load State" (0x00608f18, vtable 0x005c4148): the briefing (briefing.cpp).
class LoadState : public ShellState {
 public:
  LoadState() : ShellState("Load State", 0x608f18) {}
  bool enter() override;              // 0x004091e0
  bool update() override;             // 0x00409440
  void leave() override;              // 0x004093e0
  void suspend(uint32_t arg) override;  // 0x004092c0
  void resume(uint32_t arg) override;   // 0x004092e0
  void escape() override {}           // 0x0049bfd0
  uint8_t build_ = 0;                 // +0x10
  Handle movie_ = 0;                  // +0x18
};

// 9 "Win State" (0x00608e70, vtable 0x005c3980): the debriefing.
class WinState : public ShellState {
 public:
  // The state table's (state 9, 0x00608e70) and the missions object's
  // +0xd0 (0x006099a0), the end screen's fade's next (campaign.md section 1).
  explicit WinState(Handle addr = 0x608e70) : ShellState("Win State", addr) {}
  bool enter() override;              // 0x0047ac20
  bool update() override;             // 0x0047aed0
  void leave() override;              // 0x0047af20
  void escape() override {}           // 0x0049bfd0
  uint8_t campaign_won_ = 0;          // +0x10
  Handle movie_ = 0;                  // +0x14
  char voice_[32] = {};               // +0x18  CString "c%dm%dwin"; +0x1c its sound
                                      // (set by 0x0047abe0, so kept in the original's memory)
};

// 8 "Salvage State" (0x00608ee8, vtable 0x005c3ee8; both made by 0x0047f220):
// the state table's, and 0x00608bd8 the briefing's SALVAGE pushes (0x0047d2f0).
class SalvageState : public ShellState {
 public:
  explicit SalvageState(Handle addr = 0x608ee8) : ShellState("Salvage State", addr) {}
  bool enter() override;              // 0x0047d310
  void leave() override;              // 0x0047d390
  void escape() override {}           // 0x0049bfd0
};

// "Lab State" (0x006059f8, vtable 0x005c0c90; mech_lab.cpp, docs/mechlab.md):
// the mech lab, over the shell base but with its own screen (+0x60) and a 3D
// view of the mech. Its file's globals [0x00605a28..0x00605a88] are its fields.
class LabState : public ShellState {
 public:
  LabState() : ShellState("Lab State", 0x6059f8) {}  // 0x004488b0
  ~LabState() override;               // 0x00448930
  bool enter() override;              // 0x00448960
  bool update() override;             // 0x00448a80
  void leave() override;              // 0x00448a20
  void suspend(uint32_t) override {}  // 0x004bf620, empty
  void resume(uint32_t) override {}   // 0x004bf620
  uint32_t message(uint32_t, uint32_t) override { return 1; }  // 0x0042a130
  void escape() override {}           // 0x0049bfd0
  bool accept();                      // 0x00448870: ACCEPT, the slots let go and the state popped
  bool model_valid(int32_t i) const;  // 0x00448e20
  int32_t model_index(int32_t mech);  // 0x004507c0: the entry of `mech`, or -1
  bool show_slot(int32_t slot);       // 0x00448f80: the lance's slot shown, its mech out of the store
  bool set_mech(int32_t mech);        // 0x00449010: the shown slot's chassis
  void make_screen();                 // 0x00448e70
  void delete_screen();               // 0x00448ef0
  void draw(float dt);                // 0x00448f10

  static constexpr int32_t kNoMech = 0x13;  // +0x38 when no mech is waiting (the 19 mechs)

  uint8_t own_world_ = 0;             // +0x10  the 3D world's load loaded a mission world (none was up)
  std::vector<Handle> models_;        // +0x20  the 3D view's mechs (each +0x08 its mech)
  Handle model_ = 0;                  // +0x2c  the one shown
  int32_t slot_ = -1;                 // +0x30  the slot shown [0x00605a28]
  int32_t slots_ = 0;                 // +0x34  the game's slot count
  int32_t mech_ = 0;                  // +0x38  the mech to show next; kNoMech none (from enter on)
  Handle lab_screen_ = 0;             // +0x60  [0x00605a58]
  uint8_t x40_ = 0;                   // +0x40  set by 0x0044ffc0, never read
  float cam_pos_[3] = {};             // +0x48  the 3D view's camera, from [0x005e4648]
  float cam_rot_[3] = {};             // +0x54  and [0x005e4678]
  int32_t clear_ = 0;                 // +0x64  the frame clear's flag to put back (0x0057a3c0), kept by 0x0044ffc0
  uint32_t cursor_ = 0;               // +0x68  the cursor's state, put back by 0x00450390
  uint32_t world_ = 0;                // +0x6c  the 3D world is up (set by 0x0044ffc0)
  Handle sounds_[4] = {};             // +0x70  select, move, rollover, error
  Handle pilot_ = 0;                  // +0x84  [0x00605a7c]
  Handle store_ = 0;                  // +0x88  [0x00605a80]
  uint8_t flag_ = 0;                  // +0x8c  [0x00605a84] 0 from the briefing
  uint32_t result_ = 0;               // +0x90  [0x00605a88] update's result, never set
  // ARMOR_TITLES' read (0x004510a0): its toggles' columns and first row,
  // each set once from the first panel read (bits 1, 2, 4 of [0x00605ab4]).
  uint8_t armor_set_ = 0;             // [0x00605ab4]
  int32_t armor_front_x_ = 0;         // [0x00605ab0]
  int32_t armor_rear_x_ = 0;          // [0x00605aac]
  int32_t armor_y_ = 0;               // [0x00605ab8]
};

// Mech3.exe 1.2 @ 0x00448830: the lab opened on the pilot's game with `store`;
// false without either.
bool mech_lab_open(Handle pilot, Handle store, uint8_t flag);

// "Fade State" (0x00609888, vtable 0x005c6088; constructor 0x0048f1b0): a still
// of the screen grabbed when it is queued, faded from or to a colour over a
// time, then the stack moves on by mode (docs/game-loop.md, The fade state).
class FadeState : public State {
 public:
  enum Mode : int32_t { kPush = 0, kPopTwice = 1, kReplace = 2 };
  FadeState() : State("Fade State", 0x609888) {}
  void on_queued() override;          // 0x0048ef60
  bool enter() override;              // 0x0048ef70
  bool update() override;             // 0x0048ef90
  void leave() override;              // 0x0048f030
  // 0x0048f060: direction -1 fades the colour out (in from it), else in.
  void setup(int32_t direction, float seconds, uint8_t r, uint8_t g, uint8_t b);
  bool step(float dt);                // 0x0048f0b0: one frame drawn; false when done

  uint32_t arg_ = 0;                  // +0x08 the code the state below gets
  int32_t mode_ = kReplace;           // +0x0c
  zgame::State* next_ = nullptr;      // +0x10
  uint32_t cursor_ = 0;               // +0x14 the cursor's state before
  Handle image_ = 0;                  // +0x18 the grab
  int32_t direction_ = -1;            // +0x1c
  uint16_t colour_ = 0;               // +0x20 16-bit
  float seconds_ = 2.0f;              // +0x24
  float t_ = 0.0f;                    // +0x28 0..1
  float one_ = 0.0f;                  // +0x2c set to 1, never read
  float rate_ = 1.0f;                 // +0x30 1 / seconds
  int32_t buffer_ = 0;                // +0x34 0x00579500's: 0 back, 1 the copy, 2 front
};

// [0x005e7274], the fades' time in seconds.
constexpr float kFadeSeconds = 2.0f;

// Mech3.exe 1.2 @ 0x0048eea0 (with its wrapper 0x0048ee70, next = 0): the
// fade pushed on top; at its end the state below is popped to with `arg`,
// then `next` pushed when there is one.
void fade_push(zgame::State* next, uint32_t arg, int32_t direction, int32_t buffer, float seconds,
               uint8_t r, uint8_t g, uint8_t b);
// Mech3.exe 1.2 @ 0x0048ef00: the fade replaces the current state; at its end
// it is replaced by `next`, or popped with `arg` when there is none.
void fade_replace(zgame::State* next, uint32_t arg, int32_t direction, int32_t buffer, float seconds,
                  uint8_t r, uint8_t g, uint8_t b);

struct States {
  AttractState attract;
  IntroState intro;
  MainState main;
  PauseState pause;
  ExitState exit;
  CoreState core;
  LoadState load;
  PreLabState pre_lab;
  SalvageState salvage;
  SalvageState briefing_salvage{0x608bd8};
  WinState win;
  WinState end_win{0x6099a0};  // the missions object's (0x006098d0 +0xd0)
  KeyListState key_list;
  FadeState fade;
  LabState lab;
  WinMovieState win_movie;
  IaDummyState ia_dummy;
  TrDummyState tr_dummy;
  MultiExitState multi_exit;
};
States& states();
void reset_states();

// Mech3.exe 1.2 @ 0x004814e0: the Direct3D frame's first call, the mechs'
// projected shadows (docs/render-shadows.md).
void update_shadows(Engine& en);

}  // namespace mech3
