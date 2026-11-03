// The joystick (docs/input.md section 4). Original file unknown: it follows
// the command map (0x0051d520..0x0051ddb0).
#pragma STDC FP_CONTRACT OFF
#include <cstring>

#include "gamez/zinput/zinput.h"

namespace zin {

// Mech3.exe 1.2 @ 0x0051d520
// FIX (A): EnumDevices(DIDEVTYPE_JOYSTICK, attached only) taking the first
// stick, its IDirectInputDevice2 and c_dfDIJoystick2 are the platform's
// open(). Exclusive, foreground with force feedback (5), background without (9).
uint32_t joy_init(uint32_t) {
  State& s = state();
  if (!s.di) return 0;
  s.joy_open = false;
  if (!s.di->open(platform::Input::kJoystick)) return 0;
  s.joy_open = true;
  uint32_t flags = 0, axes = 0;
  s.di->joystick_caps(&flags, &axes);
  s.joy_axes = axes;
  s.ff = flags & 0x100;
  s.ff_attack = flags & 0x200;
  s.ff_fade = flags & 0x400;
  s.di->set_cooperative_level(platform::Input::kJoystick, s.ff ? 5 : 9);
  // The shipped setup: every axis -1000..1000; dead zones 10% on X and Y,
  // none on Z (the throttle), 25% on the rotations.
  JoyAxis config[6];
  for (JoyAxis& a : config) a.min = -1000, a.max = 1000;
  config[0].deadzone = config[1].deadzone = 1000;
  config[3].deadzone = config[4].deadzone = config[5].deadzone = 2500;
  joy_setup(config);
  joy_acquire();
  s.joy_ready = 1;
  return 1;
}

// Mech3.exe 1.2 @ 0x0051d6e0
bool joy_acquire() {
  State& s = state();
  return s.joy_open && s.di->acquire(platform::Input::kJoystick) == 0;
}

// Mech3.exe 1.2 @ 0x0051d700
// Each axis the stick has (its name found, 0x0051db80) gets the range
// (or keeps the device's, read back, 0x0051db30) and the dead zone; then
// every axis's centre and scale.
uint32_t joy_setup(const JoyAxis* config) {
  if (!config) return 0;
  State& s = state();
  JoyAxis a[6];
  std::memcpy(a, config, sizeof a);
  for (uint32_t i = 0; i < 6; ++i) {
    const uint32_t ofs = 4 * i;
    if (s.di->joystick_object_name(ofs, a[i].name, 0x104) != 0) a[i].name[0] = 0;
    a[i].name[0x103] = 0;
    if (a[i].name[0] == 0) continue;
    if (s.di->joystick_set_range(ofs, a[i].min, a[i].max) < 0) s.di->joystick_get_range(ofs, &a[i].min, &a[i].max);
    s.di->joystick_set_deadzone(ofs, static_cast<uint32_t>(a[i].deadzone));
  }
  for (JoyAxis& x : a) {
    x.centre = static_cast<float>(x.min + x.max) * 0.5f;
    x.scale = 2.0f / static_cast<float>(x.max - x.min);
  }
  std::memcpy(s.axes, a, sizeof a);
  return 1;
}

// Mech3.exe 1.2 @ 0x0051da90
JoyAxis* joy_config() { return state().axes; }

// Mech3.exe 1.2 @ 0x0051dbe0
uint32_t joy_close() {
  State& s = state();
  if (s.joy_open) {
    s.di->unacquire(platform::Input::kJoystick);
    s.di->close(platform::Input::kJoystick);  // Release
    s.joy_open = false;
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x0051dc10
bool joy_present() { return state().joy_ready == 1; }
bool joy_gamepad() { return state().di && joy_present() && state().di->joystick_gamepad(); }

// Mech3.exe 1.2 @ 0x0051dc20
// Rotations the stick does not have read as 0.
platform::JoyState* joy_read(bool callbacks) {
  State& s = state();
  if (s.joy_ready == 0) return nullptr;
  const int32_t hr = s.di->joystick_state(&s.joy_read);
  if (s.axes[3].name[0] == 0) s.joy_read.rx = 0;
  if (s.axes[4].name[0] == 0) s.joy_read.ry = 0;
  if (s.axes[5].name[0] == 0) s.joy_read.rz = 0;
  if (hr == platform::Input::kInputLost) {
    joy_acquire();
    return nullptr;
  }
  if (hr != 0) return nullptr;
  s.joy_prev = s.joy;
  s.joy = s.joy_read;
  if (callbacks) joy_callbacks();
  return &s.joy;
}

// Mech3.exe 1.2 @ 0x0051dce0
platform::JoyState* joy_state() { return &state().joy; }

// Mech3.exe 1.2 @ 0x0051dcf0
uint32_t joy_button(int32_t b) {
  const State& s = state();
  const bool now = s.joy.buttons[b - 1] != 0, before = s.joy_prev.buttons[b - 1] != 0;
  if (now) return before ? kHeld : kPressed;
  return before ? kReleased : 0;
}

// Mech3.exe 1.2 @ 0x0051dd20
// The first button pressed this read, 1..10; with wait, reads until one is.
int32_t joy_wait_button(bool wait) {
  int32_t found = 0;
  do {
    if (wait) joy_read(false);
    for (int32_t b = 1; b < 0xb; ++b) {
      if (joy_button(b) == kPressed) {
        found = b;
        break;
      }
    }
    joy_reset();
  } while (found == 0 && wait);
  return found;
}

// Mech3.exe 1.2 @ 0x0051dd60
// The buttons up in both reads, the hats 0xffff (centred: DirectInput
// looks at the low word). Quirk kept: it clears the bytes of buttons 2..11,
// one past the ones joy_button reads, so button 1 (the trigger) keeps its state.
void joy_reset() {
  State& s = state();
  if (s.joy_ready == 0) return;
  for (int b = 1; b <= 10; ++b) s.joy.buttons[b] = s.joy_prev.buttons[b] = 0;
  for (int i = 0; i < 4; ++i) s.joy.pov[i] = s.joy_prev.pov[i] = 0xffff;
}

// Mech3.exe 1.2 @ 0x0051ddb0
// The stick has force feedback: the engine start then makes the game's
// force-feedback object (0x00433090, likely).
uint32_t force_feedback() { return state().ff; }

}  // namespace zin
