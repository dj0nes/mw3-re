#pragma once
#include <cstdint>

#include "mech3/menu_states.h"
#include "mech3/menu_widgets.h"

// The pilot screen (docs/menus.md, The pilot screen): the Pilot State
// (0x00488910, [0x00609858]) and its screen (0x00486f40), where the player
// picks, makes, renames or deletes a pilot before a campaign, Instant Action,
// multiplayer or training; and the four states the main menu's buttons push,
// which open it first. Original file unknown: 0x00486f40..0x00488ad0 and the
// states 0x0048a6c0..0x0048b170, 0x0049c900.., between mech3\mech_waynet.cpp
// and mech3\mission.cpp.
namespace mech3 {

class Pilot;
class PilotScreen;

// PILOTLIST (vtable 0x005c51d0): choosing a row chooses that pilot.
class PilotList : public FileList {
 public:
  explicit PilotList(Engine& e) : FileList(e, -1) {}
  void select(int32_t index) override;  // +0x98  0x00487e80
  uint8_t x200 = 0;                  // +0x200  (cleared by the screen's reset, read by nothing)
};

// GAMELIST (vtable 0x005c5128): the campaign's saves, or the Instant Action
// and multiplayer records as lines.
class GameList : public FileList {
 public:
  explicit GameList(Engine& e) : FileList(e, -1) {}
  void on_left_up() override;        // +0x30  0x00488040: not in Instant Action
  void select(int32_t index) override;  // +0x98  0x00487f90: a campaign's save chosen
};

// SCENARIO_LIST (vtable 0x005c5080): the training missions; the other two
// cycles follow it, and the pilot's training mission is the one shown.
class ScenarioCycle : public CycleButton {
 public:
  explicit ScenarioCycle(Engine& e) : CycleButton(e, -1) {}
  void select(int32_t i) override;   // +0xa0  0x00487440
};

// Ours (tvOS, docs/tvos.md): the name Enter gives a new pilot whose name was
// left empty, for a player with no keyboard; null (the original): none.
extern const char* g_default_pilot_name;

// EDITBAR (vtable 0x005c5310): a new pilot's name; Enter makes it.
class NewPilotBar : public ShellEditBar {
 public:
  explicit NewPilotBar(Engine& e);
  void on_left_up() override;        // +0x30  0x00487dc0
  void on_return() override;         // +0x90  0x00487cf0
  void on_cancel() override;         // +0x94  0x00487d40
  uint8_t active = 0;                // +0x38c
};

// RENAMEBAR (vtable 0x005c5278): over the chosen row while renaming.
class RenameBar : public ShellEditBar {
 public:
  explicit RenameBar(Engine& e);
  void on_left_up() override;        // +0x30  0x00487dc0
  void on_return() override;         // +0x90  0x00487de0
  void on_cancel() override;         // +0x94  0x00487e10
  uint8_t active = 0;                // +0x38c
};

// The screen (vtable 0x005c5060), 0x13184 bytes, PILOTDIALOG of the main
// menu's dialog.zrd.
class PilotScreen : public MenuScreen {
 public:
  PilotScreen(Engine& e, Handle state);  // 0x00486f40
  ~PilotScreen() override;           // 0x004878b0 (deleting 0x00487840): the other pilots deleted first
  void update(float dt) override;    // +0x00  0x00488170: Delete and Ctrl+R
  void on_button(int32_t id, int32_t a, int32_t b) override;  // +0x14  0x004880a0

  void reset();                      // 0x00487b30: on entering
  bool finish_edits();               // 0x00487b90: a rename or a new name ended (Enter)
  void rename(bool start, bool cancel);  // 0x00487bd0
  void delete_chosen();              // 0x00487e30
  void cancel();                     // 0x00488060: no pilot, back (3)
  void choose(Pilot* p);             // 0x004881e0
  void fill_games();                 // 0x004882b0
  void refresh(bool focus_new, Pilot* p);  // 0x00488770: the list again, p chosen (0: the chosen one)
  void show_lists();                 // inline in 0x004889a0 and 0x00488ad0: the game list or the training cycles
  bool editing() const { return rename_bar.active || new_bar.active; }

  SlideButton cancel_btn;            // +0x111ec  CANCEL (0x17)
  SlideButton accept;                // +0x113d8  ACCEPT (0x18)
  ShellButton delete_btn;            // +0x115c4  DELETE (0x19)
  ShellButton rename_btn;            // +0x1171c  RENAME (0x1a)
  RenameBar rename_bar;              // +0x11874  RENAMEBAR
  NewPilotBar new_bar;               // +0x11c04  EDITBAR
  PilotList pilots;                  // +0x11f94  PILOTLIST
  ztwod::Image lines{0};             // +0x121a0  LINES
  ztwod::Image lines2{0};            // +0x1225c  LINES2
  GameList games;                    // +0x12318  GAMELIST
  ScenarioCycle scenarios;           // +0x12518  SCENARIO_LIST
  CycleButton descriptions;          // +0x1293c  SCENARIO_DESC
  CycleButton completed;             // +0x12d60  SCENARIO_COMPLETE
  int32_t mode = 0;                  // +0x12198  the game the pilot is for (GameType)
  Pilot* chosen = nullptr;           // +0x1219c
};

// "Pilot State" (0x00488910, vtable 0x005c54e8), 0x14 bytes: its screen
// made with it (the engine owns it, as the controls' popups).
class PilotState : public MenuState {
 public:
  PilotState(Engine& e, Handle addr);  // 0x00488910
  ~PilotState() override;
  bool enter() override;             // +0x0c  0x004889a0
  void prepare();                    // 0x004889a0 before the shell's enter
  void escape() override;            // +0x28  0x00488990
  void set_mode(int32_t m);          // 0x00488ad0

  PilotScreen* screen = nullptr;     // +0x0c's object
  int32_t mode = -1;                 // +0x10

 private:
  Engine& e_;
};

// The main menu's INSTANT ACTION (0x0048a6c0, vtable 0x005c5b10), NEW
// CAMPAIGN (0x0048aef0, 0x005c5b40), TRAINING (0x0048b120, 0x005c5b70) and
// MULTIPLAYER (0x0049c900, 0x005c7fe8) states: each sets the pilot screen's
// game and pushes it; what follows a pilot is a hook where not ported.
class GameMenuState : public MenuState {
 public:
  enum Kind { kAction, kCampaign, kTraining, kMultiplayer };
  GameMenuState(Kind k, Handle addr);
  bool enter() override;             // 0x0048a750, 0x0048afd0, 0x0048b140, 0x0049cab0
  bool update() override;            // 0x0048af20 (the campaign), else the shell's
  void leave() override;             // 0x0048afa0 (the campaign), else the shell's
  void resume(uint32_t arg) override;  // 0x0048a7b0, 0x0048b000, 0x0048b170, 0x0049cbb0
  void escape() override;            // 0x00438c80; the campaign's does nothing (0x005ac5c0)

 protected:
  // The shell's halves these call (0x0048ebf0, 0x0048ecc0, 0x0048ed30), a
  // virtual step of their own so the tests can stand in for them.
  virtual bool shell_enter() { return ShellState::enter(); }
  virtual void shell_resume(uint32_t arg) { ShellState::resume(arg); }
  virtual bool shell_update() { return ShellState::update(); }

 public:

  Kind kind;
  Handle screens[3] = {};            // +0x1c..+0x24  multiplayer's (not ported)
  Handle movie = 0;                  // +0x10  the campaign's opening movie
  int32_t page = 0;                  // +0x10  multiplayer: the screen shown (0x0049cb20 moves to `want`)
  int32_t want = 0;                  // +0x14  multiplayer: 1, or 2 from a lobby
  uint8_t x19 = 0;                   // +0x19  multiplayer
};

}  // namespace mech3
