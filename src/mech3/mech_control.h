#pragma once
#include <cstdint>
#include <memory>

#include "mech3/controls_cfg.h"
#include "mech3/zai_brain.h"

// The shared part of a mech's controls (docs/controls.md): the commanded
// throttle, turn and jump, and their smoothing toward the targets
// (docs/movement.md, Throttle and turn). Original file unknown: the code sits
// between mech_config and mech_data (named here mech_control after the
// alphabetical link order). The player, AI and network classes derive from it.
namespace platform { struct JoyState; }
namespace zin { struct MouseInfo; }

namespace mech3 {

struct Mech;
class CameraMode;

// Mech3.exe 1.2 @ 0x0042fea0: a source's reading (1..6 the stick's axes, 7, 8
// its sliders, 9..11 the mouse); null devices read 0.
float source_value(int32_t src, const platform::JoyState* j, const zin::MouseInfo* m);

// exp(x) the game's way (ftol(x * 2^23 / ln 2) + 0x3f800000 as float bits; 0x00426960 and others).
float fast_exp(float x);

struct MechControls {
  virtual ~MechControls() = default;
  // vtable +0x14: where the reticle sits, (x, y) in -1..1 of the display's
  // half size; none but the player's (0x0040bae0; 0x00428800 below).
  virtual const float* look_position() { return nullptr; }
  uint32_t flags = 0;          // +0x08  bit 2 doubles the turn rate
  int32_t view = 0;            // +0x0c  the player's view state 0..3 (0x00426780)
  float throttle = 0;          // +0x10  -1..1, smoothed toward throttle_target
  float turn = 0;              // +0x14  smoothed toward turn_target
  float pitch = 0;             // +0x18
  float twist = 0;             // +0x1c
  float jump = 0;              // +0x20  0..1, smoothed toward jump_target
  float jj_strafe = 0;         // +0x24
  float jj_forward = 0;        // +0x28
  float axis_2c = 0, axis_30 = 0;  // +0x2c, +0x30  free-look pitch and yaw (the torso's look inputs)
  uint8_t fire = 0;            // +0x34  handed to the weapons each frame (IControls +0x10)
  int32_t wrapped = 0;         // +0x38  set: an order wrapper to take off (primary vt +0x04)
  bool joy_throttle_set = false;  // +0x3c  the joystick's throttle sets the target outright (the throttle keys clear it)
  bool jump_held = false;      // +0x3d  a jump set to hold (0x00426b20(v, 1)): the player's zero leaves it
  bool throttle_held = false;  // +0x3e  cleared by the throttle keys; set, the throttle does not return to zero
  bool throttle_returns = false;  // +0x3f  ThrottleConst off: with no throttle key the throttle eases to zero
  float joy_throttle = 0;      // +0x40  the joystick throttle's last reading
  bool lever_seen = false;     // ours (W56): the lever's first reading taken as the last (init clears it)
  float throttle_target = 0;   // +0x44  -1..1
  float turn_target = 0;       // +0x48
  float jump_target = 0;       // +0x4c
  bool reverse = false;        // +0x50  the last non-zero throttle command was < 0
  uint32_t last_pov = 1;       // +0x54  the joystick hat's last reading
  bool moves[2] = {true, true};  // +0x58, +0x59  either set: the gait or the actions run
  bool x5a = false;            // +0x5a
  ai::HitRecord hit;           // +0x64  (IControls +0x04)
  Mech* owner = nullptr;       // +0x70  (IControls +0x10)

  // IControls vt+0x94 (from Mech::take_damage 0x0041ac60): a hit from
  // `attacker` (the base's 0x004aec90; the AI's 0x004ea520). vt+0x98: it hit
  // an ally, `victim` (nothing but the AI's 0x004ea500). vt+0xa0
  // (0x004aed40): hit within the last second.
  virtual void on_hit(void* attacker);
  virtual void hit_an_ally(void* victim) { (void)victim; }  // 0x004bf620
  bool hit_lately() const;
  // vt+0xa4: the pilot's tick (the base's is _purecall; the player's
  // 0x004d8990, the AI's 0x004e94f0). vt+0xa8 (Mech::die): the base's
  // 0x004aed50 runs the tick once more; the AI's 0x004d8a00. vt+0xb4 (the
  // kill credit 0x0041b420): it killed `victim` (nothing but the AI's 0x004ea630).
  virtual void tick() {}
  virtual void on_death() { tick(); }
  virtual void set_owner(Mech* m) { owner = m; }  // IControls vt+0x08 0x00428750
  // The primary vtable's: +0x04 the controls an order wrapper leaves (the
  // player's 0x004d2a70: themselves), +0x08 the pocket commander they are
  // (none but its 0x004d2a70), +0x18 whether the mech that goes deletes them
  // (the player's 0x004d7290: no), +0x1c a command (the player's 0x004274d0).
  virtual MechControls* unwrap() { return this; }
  virtual class PocketControls* pocket() { return nullptr; }
  virtual bool release() { return false; }
  virtual void command(int32_t cmd) { (void)cmd; }
  virtual void on_kill(void* victim) { (void)victim; }  // 0x004bf620
  // IControls vt+0x70, "on a path": the player's and the base's 0x0040bae0
  // (no); the AI's yes. A mech that hits a wall reverses its throttle when
  // it answers yes (0x004231c0, the mech's vt+0x140 its +0x37c).
  virtual bool on_path() { return false; }

  virtual int update(float dt);  // IControls +0x0c: 0x00426960 for the base and network class
  int smooth(float dt) { return MechControls::update(dt); }  // 0x00426960 whatever the class
  void set_throttle(float v);    // 0x00426b60 (IControls +0x20)
  // 0x00426780: the player's view state (+0x0c: 0 the torso, 1 free look in
  // the cockpit, 2 / 3 torso mode; docs/look.md).
  void set_view(int32_t v);
  void set_turn(float v) { turn_target = v; }  // 0x00426b10 (IControls +0x28)
  void set_jump(float v, bool hold = false) {  // 0x00426b20 (IControls +0x34: 0x00426b40, no hold)
    jump_target = v;
    jump_held = hold;
  }
  void zero();                   // 0x00426c80
  void stop();                   // 0x00428490 (IControls +0x14): throttle, turn, jump and both jump-jet axes 0
  // 0x00426c10 (IControls +0x4c, Stop; its argument unused): the turn, pitch
  // and jump-jet axes zeroed at once, the throttle, turn and jump targets 0,
  // the joystick throttle and the trigger off. Mech::power calls it.
  void halt();
  void init();                   // 0x004265a0's fields (most derived 0): at rest, view state 2
  // 0x00426650: the control mode (+0x08). While the axis table is ready, bit
  // 4 puts the joystick's axes in it and bit 1 the mouse's.
  void set_mode(uint32_t mode);
};

// The player's controls (ctor 0x00427320, 0x160 bytes; controls.md): the
// keyboard, joystick and mouse through zInput's commands and devices. The
// IControls base sits at +0x14c here (its owner at +0x15c); +0x5c is a base
// of the AI's (0x004d9840), not ported.
struct PlayerControls : MechControls {
  float look_dx = 0, look_dy = 0;  // +0x138, +0x13c  the mouse look's motion (view 2)
  float look_x = 0, look_y = 0;    // +0x140, +0x144  its position
  // +0x5c: the pilot's AI settings (0x004d9840: the defaults, vtable
  // 0x005cd348), the launch's aiv.zrd "player" record (IControls vt+0x88);
  // only its vlist (+0x12c) is read, by the tick.
  ai::AiParams settings;
  void set_record(const ai::AiParams& r) { settings = r; }  // IControls vt+0x88 0x00428440 (0x004d99e0)
  // 0x004d8990 (IControls vt+0xa4): the settings' vlist triggers run on the
  // player's vehicle (0x004d9290), nothing picked yet (docs/ai.md 17.5).
  void pilot_tick();
  void tick() override { pilot_tick(); }  // IControls vt+0xa4 0x00428720 -> 0x004d8990
  void construct(uint32_t mode);   // 0x00427320 with the base 0x004265a0
  int update(float dt) override;   // 0x00427d60
  // 0x004274d0 (primary vt+0x1c): the throttle keys. Backspace reverses the
  // target (or, at 0, the direction); 1 stops; 2..0 set 0.2, 0.3 .. 0.9, 1.0
  // (2 is labelled 10%), negative in reverse, and hold it. Not while
  // powered down or without a mech.
  void command(int32_t cmd) override;
  const float* look_position() override { return &look_x; }  // 0x00428800
  // 0x00427870: the joystick and mouse axes into the frame's inputs.
  void read_axes(float dt, float* throttle, float* turn, float* pitch, float* twist, float* look_pitch,
                 float* look_yaw);
};

// The AI's controls (ctor 0x004d8750 over 0x004d8670, 0x320 bytes; docs/ai.md,
// The AI controls): MechControls, the pilot's brain at +0x5c (a base in the
// exe, ai::Brain), the IControls virtual base at +0x30c (its owner +0x31c).
// Each frame the brain (0x004e9710, not ported) sets the targets and runs the
// smoothing. Made without a record, it has no brain (the smoothing's tests).
// 0x0049e540 -> 0x0049e4e0 (the AI controls' death): a dead pilot's region
// triggers go to the player's settings when it was on his side.
void player_takes_triggers(const ai::AiParams& dead);
// Ours, for the tests: the AiControls alive.
int32_t ai_mech_controls_alive();
void take_triggers(ai::AiParams& to, const ai::AiParams& dead);  // 0x0049e4e0

struct AiControls : MechControls, ai::BrainControls {
  AiControls();
  AiControls(Mech* owner, ai::AiNet* net, const ai::AiParams& rec);  // 0x004d8750(owner, net, rec, 1)
  ~AiControls() override;          // IControls vt+0x00: 0x004d8bf0 -> 0x004d8c00
  int update(float dt) override;   // 0x004e96f0
  bool on_path() override { return true; }  // IControls vt+0x70 0x004e7790 -> 0x004a5270
  std::unique_ptr<ai::Brain> brain;  // +0x5c

  // What the brain asks of them (ai::BrainControls).
  zgame::Handle vehicle() override;
  void stop() override { MechControls::stop(); }
  void smooth(float dt) override { MechControls::smooth(dt); }  // 0x004d89f0
  bool hit_lately() override { return MechControls::hit_lately(); }  // IControls vt+0xa0 0x004aed40
  zgame::Handle attacker() override { return hit.by; }
  void on_hit(void* attacker) override;       // IControls vt+0x94 0x004ea520
  void hit_an_ally(void* victim) override;    // IControls vt+0x98 0x004ea500
  void tick() override;                       // IControls vt+0xa4 0x004e94f0
  void on_death() override;                   // IControls vt+0xa8 0x004d8d00 -> 0x004d8a00
  void on_kill(void* victim) override;        // IControls vt+0xb4 0x004ea630
  void fire(bool on) override { MechControls::fire = on; }  // IControls vt+0x10 0x00428580
  bool reversing() override { return reverse; }  // IControls vt+0x18 0x00428500
  float throttle_now() override { return MechControls::throttle; }  // IControls vt+0x1c 0x00428510
  void set_throttle(float v) override { MechControls::set_throttle(v); }
  void set_turn(float v) override { MechControls::set_turn(v); }
  void set_jump(float v) override { MechControls::set_jump(v); }  // IControls vt+0x34 0x00426b40
  void set_jet_forward(float v) override { jj_forward = v; }      // IControls vt+0x3c 0x00428560
  float turn_goal() override { return turn_target; }  // IControls vt+0x30 0x00426b00
  void aim_started() override { brain->scan_aim(); }  // IControls vt+0x80 0x004d8930
};

// Mech3.exe 1.2 @ 0x00427280: the player's controls, made once in `mode`
// ([0x006056ec], with the commands' callback registered), the mode set again
// on later calls (the launch, 0x00491150).
PlayerControls* player_controls(uint32_t mode);
PlayerControls*& player_controls_instance();  // [0x006056ec]
// Mech3.exe 1.2 @ 0x00427300 (the mission's end): the player controls'
// pilot record's vlist freed (its +0x5c, 0x004d9cc0) and +0x50 cleared.
void player_controls_mission_end();
void register_player_commands();               // 0x00426ce0
void player_command(int32_t cmd);              // 0x00426cb0: to the controls, then the viewed vehicle

// The axis table's four bindings (0x006040d0: throttle, twist, pitch, turn;
// controls_cfg.h), set by the controls file, the options and set_mode.
inline AxisBinding* axis_bindings() { return axis_table().axes; }

// What the controls call outside: zInput (unset: its port), the HUD, the
// pilot and the AI brain (unset: nothing, 0).
struct ControlsCalls {
  uint32_t (*command)(int cmd) = nullptr;        // 0x0051d300; unset: zin::command
  const void* (*joy_state)() = nullptr;          // 0x0051dce0 (a DIJOYSTATE2); unset: zin::joy_state
  const void* (*joy_config)() = nullptr;         // 0x0051da90 (six zin::JoyAxis); unset: zin::joy_config
  const void* (*mouse_info)() = nullptr;         // 0x0051b7b0 (a zin::MouseInfo); unset: zin::mouse_info
  bool (*joy_present)() = nullptr;               // 0x0051dc10; unset: zin::joy_present
  bool (*joy_gamepad)() = nullptr;               // ours (W56); unset: zin::joy_gamepad
  void (*run_command)(int cmd) = nullptr;        // 0x0051c440; unset: zin::run_callback
  void (*mouse_set)(float x, float y) = nullptr; // 0x0051b440; unset: zin::mouse_set
  int (*camera_view)() = nullptr;                // [0x005f4c8c] +0x58 +0x40
  bool (*mw2_jets)() = nullptr;                  // 0x0040d1e0: the MW2JJets option ([0x005f4d04] bit 7)
  void (*pilot_tick)(MechControls&) = nullptr;   // IControls +0xa4: unset, PlayerControls::pilot_tick 0x004d8990
  uint8_t (*screen_busy)(void* screen) = nullptr;  // the HUD's +0x167c
  void (*zoom)(void* screen, bool in) = nullptr; // the HUD's +0x1508: vt +0x60(-1) held, vt +0x64 let go
  // The view (0x00426780): the mech's camera `id` (vt+0x10c), the view's
  // target ([0x005f4c8c] +0x64) and its mode (+0x58); unset: none.
  CameraMode* (*mech_camera)(Mech&, int32_t id) = nullptr;
  Mech* (*view_target)() = nullptr;
  CameraMode* (*view_mode)() = nullptr;
  int (*ai_brain)(MechControls&, float dt) = nullptr;  // AI controls without a brain (the tests'); unset: the smoothing alone
  void (*joy_use)() = nullptr;                   // 0x0051b2c0; unset: zin::joy_use
};
ControlsCalls& controls_calls();

// Ours (W56, FIX (bug) behind `bugs` and zfix's throttle_reverse; fixes.md):
// a stick's throttle lever over the mech's whole range, full reverse at one
// end to full ahead at the other. Stop sits at kLeverStop of the travel, as
// reverse runs at 0.6 of the speed (Gait 0x004181c0): a kph is the same
// travel both ways; kLeverStopZone either side of it reads stop. The lever's
// end that gave full ahead before still does (the binding's scale's sign);
// its offset is not used. Stick sources only (1..8), not a gamepad's
// triggers, which keep half at rest.
constexpr float kLeverStop = 0.375f, kLeverStopZone = 0.05f;
bool lever_reverse_on(const AxisBinding& b);
float lever_throttle(const AxisBinding& b, const void* joy);

}  // namespace mech3
