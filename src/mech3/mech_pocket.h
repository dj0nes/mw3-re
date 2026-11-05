#pragma once
#include <cstdint>

#include "mech3/mech_control.h"
#include "mech3/zai_target.h"

// The pocket commander (mech_pocket.cpp, docs/ai.md 17.6a): the player's
// lance command mode, the lance order 2 (key 0x53). His controls are wrapped
// by these, which keep the old ones and give them back; while it is up the
// number keys pick lancemates, the panel (item L4c2) shows the lance and the
// target, and the next lance order goes to the lancemates picked.
namespace mech3 {

struct Mech;
class PocketControls;

// What the mode reaches outside (unset: logged through `unported`, if set,
// and nothing). The view object is [0x005f4c8c]; the HUD the viewed mech's
// (0x00430aa0 on its +0x684), only when the mode's mech is the view's
// target (its +0x64).
struct PocketCalls {
  uint32_t (*unported)(const char* key, uint32_t arg) = nullptr;
  void (*view_pocket)(bool on) = nullptr;            // 0x004b5290: the view's +0x10, 0x0059fbe0 / 0x0059fc80 on 0x0060b270
  int32_t (*camera_mode)() = nullptr;                // the view's +0x58 +0x04, -1 without one
  void (*set_camera_mode)(int32_t mode) = nullptr;   // 0x0040ae70
  // The HUD's +0x144c (the lance part): +0x90c the mode's flag; on, 0x0059bb50
  // on its +0x920 too.
  void (*hud_mode)(Mech& m, bool on) = nullptr;
  void (*hud_hold)(Mech& m, bool hold) = nullptr;    // 0x004484b0 on the HUD (+0x6520, +0x188c0: vt+0x60(-1) / vt+0x64)
  int32_t (*display_width)() = nullptr;              // 0x0040d760: the panel's x is it less 0x8c
  // The tests' stand-ins for the controls' own (unset: theirs): the view
  // state (0x00426780) and the player's keys (0x004274d0).
  void (*view_state)(PocketControls& p, int32_t v) = nullptr;
  void (*player_keys)(PocketControls& p, int32_t cmd) = nullptr;
  uint32_t (*net_element)() = nullptr;               // [0x00609980]'s 0x0058b060 by its +0x20: 0 outside a network game
  // The mode's keys (0x0046b0a0): the HUD (0x00430aa0) when the mech is the
  // view's (+0x64), for its zoom box (ControlsCalls::screen_busy, zoom).
  void* (*view_screen)(Mech& m) = nullptr;
  // The panel (0x548 bytes, item L4c2): made by the mode's set_owner
  // (0x00468250 on the mode, its +0x178 / +0x17c, 0x0c), freed (0x00468720),
  // its frame (0x00469d00, then 0x0046b0a0), a lancemate picked or not
  // (0x00469060 on its +0x53c list: vt+0x44 / +0x48); the orders on the ones
  // picked (0x00468f00 / 0x00468fa0, item L4c3: 1 taken, 0 refused, 2 none).
  void* (*panel_make)(PocketControls& p) = nullptr;
  void (*panel_free)(void* panel) = nullptr;
  void (*panel_frame)(PocketControls& p, float dt) = nullptr;
  bool (*panel_pick)(PocketControls& p, int32_t unit) = nullptr;
  // The panel's units (its +0x53c list, the first +0x70): each picked or
  // not (its +0x4b0 bit 1) and its mech (vt+0xb4, then vt+0x2c, vt+0x40).
  // Unset: the mode's PocketPanel's (pocket_panel_units).
  int32_t (*panel_units)(PocketControls& p, struct PocketUnit* out, int32_t cap) = nullptr;
};
PocketCalls& pocket_calls();

struct PocketUnit;
// The mode's panel's units: its first +0x70 lines (none for an empty place).
int32_t pocket_panel_units(PocketControls& p, PocketUnit* out, int32_t cap);

struct PocketUnit {
  bool picked = false;
  Mech* mech = nullptr;
};

// The mode's globals: [0x00605f64] the mode while it is up, [0x00605f5c]
// the panel's cursor (an entity), [0x00605f58] the command target marker
// (an AiTarget on `cmd_target.flt`, made at the mission's start by
// 0x0046aaa0 / 0x00468100; the panel puts it where the cursor meets the
// ground),
// [0x00605f50] / [0x00605f60] (cleared by the mode's start).
struct PocketGlobals {
  PocketControls* active = nullptr;  // 0x00605f64
  uint32_t cursor = 0;               // 0x00605f5c
  uint32_t marker = 0;               // 0x00605f58
  uint32_t x50 = 0, x60 = 0;         // 0x00605f50, 0x00605f60
  // The mode's sounds (0x0046a930, zSound's by name; [0x00605f2c] once).
  bool sounds_loaded = false;        // 0x00605f2c
  int32_t sure_sir = 0, right_away = 0, so_long = 0, no_way = 0, cannot = 0;  // 0x00605f30..0x00605f40
  int32_t select = 0;                // 0x00605f48  pckt_select
  int32_t rollover = 0;              // 0x00605f44  pckt_rollover
};
PocketGlobals& pocket_globals();

// 0x004690d0 (0x1dc bytes, vtables 0x005c2fb0 / 0x005c2fa8 / IControls
// 0x005c2ef0, over the player's controls 0x00427320): made over the
// player's controls (`prev`) and the player controls' instance [0x006056ec]
// it stands in for, with the mode's lance order (2) and its input mode.
class PocketControls : public PlayerControls {
 public:
  PocketControls(MechControls* prev, PlayerControls* prev_instance, int32_t order, uint32_t mode);
  ~PocketControls() override;            // 0x00469310 (and 0x0046b580, the deleting one)
  int update(float dt) override;         // IControls vt+0x0c 0x0046a360
  void set_owner(Mech* m) override;      // IControls vt+0x08 0x0046a440: the panel made again
  void on_death() override;              // IControls vt+0xa8 0x0046abb0
  MechControls* unwrap() override;       // primary vt+0x04 0x00469cf0: the dispatcher on +0x160
  PocketControls* pocket() override { return this; }  // primary vt+0x08 0x004d2a70
  bool release() override;               // primary vt+0x18 0x0046ad80
  void command(int32_t cmd) override;    // primary vt+0x1c 0x0046a860

  // The mode's way out (the dispatcher's, 0x0046ad80's and the death's
  // shared block): the old controls given back to the player's mech, the
  // instance, view, camera and HUD restored, the inputs copied back. The
  // controls to use; this one deleted when `free_self`.
  MechControls* leave(bool free_self);
  // Mech3.exe 1.2 @ 0x0046b0a0 (the update's second half): the mode's own
  // driving, 0x00427d60's with the mouse look for the stick.
  void keys(float dt);
  // Mech3.exe 1.2 @ 0x0046af30: with the mouse on (+0x08 bit 0) its motion
  // into the pitch and twist by the view state (+0x0c).
  void mouse_look(float dt, float* pitch, float* twist);

  int32_t x148 = 0, x14c = 0;            // +0x148, +0x14c
  int32_t saved_camera = -1;             // +0x150  the camera mode at the start
  uint32_t x154 = 0;                     // +0x154  bit 0: the jumps and the trigger left alone
  MechControls* prev;                    // +0x158
  PlayerControls* prev_instance;         // +0x15c  [0x006056ec] before
  int32_t order;                         // +0x160  2 (unwrap's)
  uint32_t x164 = 0;                     // +0x164  the cursor's node
  const float* cursor_pos = nullptr;     // +0x168  its place: an entity's, the net's or +0x16c
  float place[3] = {};                   // +0x16c  the spot picked
  int32_t panel_x = 0, panel_y = 0x5f;   // +0x178 (the display's width less 0x8c), +0x17c
  void* panel = nullptr;                 // +0x180
  uint32_t x184 = 0;                     // +0x184
  ai::Contact aim;                       // +0x188  (the template 0x005e9d10)
  uint32_t x1b8 = 0, net_element = 0;    // +0x1b8 the mission map when the HUD shows it, +0x1bc
  uint8_t x1c0 = 0, x1c1 = 0;            // +0x1c0 the cursor the net's, +0x1c1 the player's target's

 private:
  void copy_inputs(const MechControls& from);  // 0x004690d0's and the way out's (the other way)
};

// Mech3.exe 1.2 @ 0x00468f00 (no target: on each lancemate's contact on the
// player) / 0x00468fa0 (on a contact from it on `target`, an entity, with
// 0x00468ca0 first): the lance order to the panel's units picked that have a
// brain (lance_orders' give). 1 when one took it, else 0 when one refused,
// else 2.
int32_t pocket_order(PocketControls& p, int32_t order, uint32_t target, bool on_target);

// Mech3.exe 1.2 @ 0x00469940's order 2 outside the mode: the mode started
// over `c` (the player's controls); the controls to use.
MechControls* pocket_enter(MechControls* c, uint32_t mode);

}  // namespace mech3
