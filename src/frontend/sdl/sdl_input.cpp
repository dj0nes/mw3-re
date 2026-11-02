#include "frontend/sdl/sdl_input.h"

#include "gamez/zutil/zutl_reg.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>

namespace sdlfe {

uint8_t dik_from_scancode(SDL_Scancode s) {
  // Letters by position on the US layout, as the PC's scan codes are.
  static const uint8_t kLetters[26] = {0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
                                       0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
                                       0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
  if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return kLetters[s - SDL_SCANCODE_A];
  if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_0) return static_cast<uint8_t>(0x02 + (s - SDL_SCANCODE_1));
  if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return static_cast<uint8_t>(0x3b + (s - SDL_SCANCODE_F1));
  switch (s) {
    case SDL_SCANCODE_ESCAPE: return 0x01;           // DIK_ESCAPE
    case SDL_SCANCODE_MINUS: return 0x0c;
    case SDL_SCANCODE_EQUALS: return 0x0d;
    case SDL_SCANCODE_BACKSPACE: return 0x0e;        // DIK_BACK
    case SDL_SCANCODE_TAB: return 0x0f;
    case SDL_SCANCODE_LEFTBRACKET: return 0x1a;
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1b;
    case SDL_SCANCODE_RETURN: return 0x1c;
    case SDL_SCANCODE_LCTRL: return 0x1d;
    case SDL_SCANCODE_SEMICOLON: return 0x27;
    case SDL_SCANCODE_APOSTROPHE: return 0x28;
    case SDL_SCANCODE_GRAVE: return 0x29;
    case SDL_SCANCODE_LSHIFT: return 0x2a;
    case SDL_SCANCODE_BACKSLASH: return 0x2b;
    case SDL_SCANCODE_NONUSHASH: return 0x2b;
    case SDL_SCANCODE_COMMA: return 0x33;
    case SDL_SCANCODE_PERIOD: return 0x34;
    case SDL_SCANCODE_SLASH: return 0x35;
    case SDL_SCANCODE_RSHIFT: return 0x36;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;      // DIK_MULTIPLY
    case SDL_SCANCODE_LALT: return 0x38;             // DIK_LMENU
    case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_CAPSLOCK: return 0x3a;         // DIK_CAPITAL
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
    case SDL_SCANCODE_SCROLLLOCK: return 0x46;
    case SDL_SCANCODE_KP_7: return 0x47;
    case SDL_SCANCODE_KP_8: return 0x48;
    case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_KP_MINUS: return 0x4a;         // DIK_SUBTRACT
    case SDL_SCANCODE_KP_4: return 0x4b;
    case SDL_SCANCODE_KP_5: return 0x4c;
    case SDL_SCANCODE_KP_6: return 0x4d;
    case SDL_SCANCODE_KP_PLUS: return 0x4e;          // DIK_ADD
    case SDL_SCANCODE_KP_1: return 0x4f;
    case SDL_SCANCODE_KP_2: return 0x50;
    case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_KP_PERIOD: return 0x53;        // DIK_DECIMAL
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;   // DIK_OEM_102
    case SDL_SCANCODE_F11: return 0x57;
    case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_F13: return 0x64;
    case SDL_SCANCODE_F14: return 0x65;
    case SDL_SCANCODE_F15: return 0x66;
    case SDL_SCANCODE_KP_EQUALS: return 0x8d;        // DIK_NUMPADEQUALS
    case SDL_SCANCODE_KP_ENTER: return 0x9c;         // DIK_NUMPADENTER
    case SDL_SCANCODE_RCTRL: return 0x9d;
    case SDL_SCANCODE_KP_DIVIDE: return 0xb5;
    case SDL_SCANCODE_PRINTSCREEN: return 0xb7;      // DIK_SYSRQ
    case SDL_SCANCODE_RALT: return 0xb8;             // DIK_RMENU
    case SDL_SCANCODE_PAUSE: return 0xc5;
    case SDL_SCANCODE_HOME: return 0xc7;
    case SDL_SCANCODE_UP: return 0xc8;
    case SDL_SCANCODE_PAGEUP: return 0xc9;           // DIK_PRIOR
    case SDL_SCANCODE_LEFT: return 0xcb;
    case SDL_SCANCODE_RIGHT: return 0xcd;
    case SDL_SCANCODE_END: return 0xcf;
    case SDL_SCANCODE_DOWN: return 0xd0;
    case SDL_SCANCODE_PAGEDOWN: return 0xd1;         // DIK_NEXT
    case SDL_SCANCODE_INSERT: return 0xd2;
    case SDL_SCANCODE_DELETE: return 0xd3;
    case SDL_SCANCODE_LGUI: return 0xdb;             // DIK_LWIN
    case SDL_SCANCODE_RGUI: return 0xdc;
    case SDL_SCANCODE_APPLICATION: return 0xdd;      // DIK_APPS
    default: return 0;
  }
}

SdlInput::~SdlInput() {
  if (pad_) SDL_CloseGamepad(pad_);
  else if (joy_) SDL_CloseJoystick(joy_);
}

bool SdlInput::open(Device d) {
  if (d == kJoystick) {
    if (!joy_) {
      int n = 0;
      SDL_JoystickID* ids = SDL_GetJoysticks(&n);
      if (ids && n > 0) {  // EnumDevices(DIDEVTYPE_JOYSTICK, attached only): the first
        if (SDL_IsGamepad(ids[0])) pad_ = SDL_OpenGamepad(ids[0]);
        joy_ = pad_ ? SDL_GetGamepadJoystick(pad_) : SDL_OpenJoystick(ids[0]);
      }
      SDL_free(ids);
    }
    if (!joy_) return false;
  }
  opened_[d] = true;
  return true;
}

void SdlInput::close(Device d) {
  opened_[d] = acquired_[d] = false;
  if (d == kJoystick && pad_) SDL_CloseGamepad(pad_), pad_ = nullptr, joy_ = nullptr;
  if (d == kJoystick && joy_) SDL_CloseJoystick(joy_), joy_ = nullptr;
  if (d == kKeyboard) keys_.clear();
}

// An exclusive foreground device (DISCL_EXCLUSIVE | DISCL_FOREGROUND) is
// taken only while the window has the focus, and lost when it goes.
int32_t SdlInput::acquire(Device d) {
  if (!opened_[d]) return kNotAcquired;
  if ((coop_[d] & 5) == 5 && !focused_) return kNotAcquired;
  acquired_[d] = true;
  lost_[d] = false;
  if (d == kMouse) refresh();
  return 0;
}
int32_t SdlInput::unacquire(Device d) {
  acquired_[d] = false;
  if (d == kMouse) refresh();
  if (d == kKeyboard) keys_.clear();  // a lost device's buffer is gone
  if (d == kMouse) dx_ = dy_ = dz_ = 0;
  return 0;
}

int32_t SdlInput::keyboard_data(platform::KeyEvent* out, uint32_t* count) {
  if (!opened_[kKeyboard] || !acquired_[kKeyboard]) return *count = 0, kNotAcquired;
  uint32_t n = 0;
  while (n < *count && !keys_.empty()) out[n++] = keys_.front(), keys_.pop_front();
  *count = n;
  const bool over = overflow_;
  overflow_ = false;
  return over ? kBufferOverflow : 0;
}

void SdlInput::capture(bool on) {
  captured_ = on;
  if (window) SDL_SetWindowRelativeMouseMode(window, on);
}

// An exclusive mouse (DISCL_EXCLUSIVE) held while the window has the focus;
// in a window only in a mission and not let go (sdl_input.h).
void SdlInput::refresh() {
  win_ = windowed && windowed();
  const bool mission = in_mission && in_mission();
  if (mission != mission_) {
    mission_ = mission;
    if (mission) released_ = win_ && !focused_;  // into the mission (or out of the pause): held
    // FIX (A), playtest W30: a press from the screen before (LAUNCH, RESUME)
    // is not the mission's fire. The exe's load took seconds, so the click
    // was long let go; ours is at once. A latched press is dropped, and a
    // button still held is kept from the game until it is let go.
    if (mission)
      for (int b = 0; b < 4; ++b) {
        latched_[b] = 0;
        if (buttons_[b]) buttons_[b] = 0, swallow_[b] = true;
      }
  }
  if (win_ && !mission_ && !absolute_) cursor_moved_ = false;
  absolute_ = win_ && !mission_;
  const bool want = acquired_[kMouse] && (coop_[kMouse] & 1) && focused_ && !(win_ && (!mission_ || released_));
  // Playtest W19: SDL's own state, not only ours; a relative mode refused or
  // dropped (a fullscreen switch, the focus) is asked for again, else the
  // system's cursor showed at the centre the game puts it each frame.
  if (want != captured_ || (window && SDL_GetWindowRelativeMouseMode(window) != want)) capture(want);
  // The menus and a held mouse draw the game's own cursor or none; a mouse let
  // go in a window shows the system's.
  const bool hide = absolute_ || captured_;
  if (window && (hide != cursor_hidden_ || SDL_CursorVisible() == hide)) {
    if (hide) SDL_HideCursor();
    else SDL_ShowCursor();
    cursor_hidden_ = hide;
  }
}

// kReleaseKeys pressed together and let go with no other key between them.
void SdlInput::release_chord(SDL_Scancode s, bool down) {
  SDL_Keymod m = SDL_KMOD_NONE;
  switch (s) {
    case SDL_SCANCODE_LCTRL: m = SDL_KMOD_LCTRL; break;
    case SDL_SCANCODE_RCTRL: m = SDL_KMOD_RCTRL; break;
    case SDL_SCANCODE_LALT: m = SDL_KMOD_LALT; break;
    case SDL_SCANCODE_RALT: m = SDL_KMOD_RALT; break;
    case SDL_SCANCODE_LSHIFT: m = SDL_KMOD_LSHIFT; break;
    case SDL_SCANCODE_RSHIFT: m = SDL_KMOD_RSHIFT; break;
    case SDL_SCANCODE_LGUI: m = SDL_KMOD_LGUI; break;
    case SDL_SCANCODE_RGUI: m = SDL_KMOD_RGUI; break;
    default: break;
  }
  if (!(m & kReleaseKeys)) {
    if (down) chord_spoiled_ = true;  // another key between
    return;
  }
  auto all_held = [this] {
    for (const SDL_Keymod g : {SDL_KMOD_CTRL, SDL_KMOD_ALT, SDL_KMOD_SHIFT, SDL_KMOD_GUI})
      if ((kReleaseKeys & g) && !(chord_held_ & g)) return false;
    return true;
  };
  if (down) {
    if (!(chord_held_ & kReleaseKeys)) chord_spoiled_ = false, chord_armed_ = false;  // a new chord
    chord_held_ = static_cast<SDL_Keymod>(chord_held_ | m);
    if (all_held() && !chord_spoiled_) chord_armed_ = true;
    return;
  }
  chord_held_ = static_cast<SDL_Keymod>(chord_held_ & ~m);
  if (chord_armed_ && !chord_spoiled_ && win_ && mission_ && !released_) {
    released_ = true;
    std::fill(std::begin(buttons_), std::end(buttons_), 0);
    std::fill(std::begin(latched_), std::end(latched_), 0);
    dx_ = dy_ = dz_ = 0;
    refresh();
  }
  chord_armed_ = false;
}

void SdlInput::set_cooperative_level(Device d, uint32_t flags) { coop_[d] = flags; }

int32_t SdlInput::mouse_state(platform::MouseState* out) {
  refresh();
  if (!opened_[kMouse] || !acquired_[kMouse]) return lost_[kMouse] ? kInputLost : kNotAcquired;
  if (muted()) {  // let go in a mission: no motion, no buttons
    *out = platform::MouseState{};
    dx_ = dy_ = dz_ = 0;
    return 0;
  }
  if (absolute_ && absolute_cursor && cursor_moved_) {  // the menus in a window: zInput's mouse at the cursor
    absolute_cursor(mouse_x, mouse_y);
    dx_ = dy_ = 0;
  }
  out->x = static_cast<int32_t>(dx_), out->y = static_cast<int32_t>(dy_), out->z = static_cast<int32_t>(dz_);
  dx_ -= out->x, dy_ -= out->y, dz_ -= out->z;  // the fractions carry over
  // FIX (A): DirectInput's device state caught a click shorter than a frame
  // through its buffer; SDL's events can press and release between two reads,
  // so a press stays down for the read after it.
  for (int i = 0; i < 4; ++i) out->buttons[i] = buttons_[i] | latched_[i], latched_[i] = 0;
  return 0;
}

int32_t SdlInput::joystick_state(platform::JoyState* out) {
  if (!joy_ || !opened_[kJoystick]) return kNotAcquired;
  SDL_UpdateJoysticks();  // Poll (IDirectInputDevice2 +0x64)
  *out = platform::JoyState{};
  int32_t* axes[8] = {&out->x, &out->y, &out->z, &out->rx, &out->ry, &out->rz, &out->slider[0], &out->slider[1]};
  const int na = axis_count();
  for (int i = 0; i < 8; ++i) {
    // SDL's -32768..32767 into the range the game set (DIPROP_RANGE), with
    // its dead zone (DIPROP_DEADZONE, 1/10000 of the range) around the centre.
    if (i >= na) continue;  // an axis the device lacks stays 0 in DIJOYSTATE2
    const double c0 = pad_ ? pad_axis(i) : (SDL_GetJoystickAxis(joy_, i) + 32768) / 65535.0 * 2.0 - 1.0;
    double c = c0;
    const double dz = deadzone_[i] / 10000.0;
    c = std::fabs(c) <= dz ? 0.0 : (c - std::copysign(dz, c)) / (1.0 - dz);
    const int32_t lo = range_[i][0], hi = range_[i][1];
    *axes[i] = static_cast<int32_t>(std::lround(lo + (c + 1.0) * 0.5 * (hi - lo)));
  }
  static const int32_t kAngles[16] = {-1, 0, 9000, 4500, 18000, -1, 13500, -1,
                                      27000, 31500, -1, -1, 22500, -1, -1, -1};
  for (int i = 0; i < 4; ++i) out->pov[i] = 0xffffffffu;
  if (pad_) {
    // DirectInput's XInput pad: its buttons in that order, the d-pad the hat.
    static const SDL_GamepadButton kButtons[10] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK};
    for (int i = 0; i < 10; ++i) out->buttons[i] = SDL_GetGamepadButton(pad_, kButtons[i]) ? 0x80 : 0;
    const int hat = (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_UP) ? 1 : 0) |
                    (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ? 2 : 0) |
                    (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_DOWN) ? 4 : 0) |
                    (SDL_GetGamepadButton(pad_, SDL_GAMEPAD_BUTTON_DPAD_LEFT) ? 8 : 0);
    const int32_t a = kAngles[hat];
    out->pov[0] = a < 0 ? 0xffffffffu : static_cast<uint32_t>(a);
  } else {
    const int nh = SDL_GetNumJoystickHats(joy_);
    for (int i = 0; i < 4 && i < nh; ++i) {
      const int32_t a = kAngles[SDL_GetJoystickHat(joy_, i) & 15];  // up 1, right 2, down 4, left 8
      out->pov[i] = a < 0 ? 0xffffffffu : static_cast<uint32_t>(a);
    }
    const int nb = SDL_GetNumJoystickButtons(joy_);
    for (int i = 0; i < 128 && i < nb; ++i) out->buttons[i] = SDL_GetJoystickButton(joy_, i) ? 0x80 : 0;
  }
  return acquired_[kJoystick] ? 0 : kNotAcquired;
}

// The axes DirectInput's layout has: a gamepad's five (X, Y, Z, RX, RY),
// else SDL's count.
int SdlInput::axis_count() const {
  if (pad_) return 5;
  return joy_ ? SDL_GetNumJoystickAxes(joy_) : 0;
}

// A gamepad's axis i of DirectInput's XInput layout, -1..1: the sticks as
// they are, Z the left trigger less the right one (each 0..1).
double SdlInput::pad_axis(int i) const {
  auto ax = [this](SDL_GamepadAxis a) { return (SDL_GetGamepadAxis(pad_, a) + 32768) / 65535.0 * 2.0 - 1.0; };
  auto trig = [this](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad_, a) / 32767.0; };
  switch (i) {
    case 0: return ax(SDL_GAMEPAD_AXIS_LEFTX);
    case 1: return ax(SDL_GAMEPAD_AXIS_LEFTY);
    case 2: return trig(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) - trig(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    case 3: return ax(SDL_GAMEPAD_AXIS_RIGHTX);
    case 4: return ax(SDL_GAMEPAD_AXIS_RIGHTY);
    default: return 0.0;
  }
}

int32_t SdlInput::joystick_caps(uint32_t* flags, uint32_t* axes) {
  // DIDC_FORCEFEEDBACK (0x100) when the device rumbles (FIX (A): the effects
  // are played as rumble, sdl_force.cpp); zInput then takes it exclusive.
  *flags = joy_ && SDL_GetBooleanProperty(SDL_GetJoystickProperties(joy_), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false)
               ? 0x100u
               : 0u;
  *axes = joy_ ? static_cast<uint32_t>(axis_count()) : 0;
  return 0;
}

// DIJOFS_X .. DIJOFS_RZ (0..0x14) are SDL's axes 0..5; DIJOFS_SLIDER 0x18, 0x1c.
int32_t SdlInput::joystick_object_name(uint32_t ofs, char* name, uint32_t n) {
  static const char* kNames[8] = {"X Axis", "Y Axis", "Z Axis", "X Rotation", "Y Rotation", "Z Rotation",
                                  "Slider", "Slider"};
  const uint32_t i = ofs / 4;
  if (!joy_ || i >= 8 || static_cast<int>(i) >= axis_count()) {
    if (n) name[0] = 0;
    return kObjectNotFound;
  }
  std::snprintf(name, n, "%s", kNames[i]);
  return 0;
}
int32_t SdlInput::joystick_set_range(uint32_t ofs, int32_t min, int32_t max) {
  if (ofs / 4 >= 8) return kObjectNotFound;
  range_[ofs / 4][0] = min, range_[ofs / 4][1] = max;
  return 0;
}
int32_t SdlInput::joystick_get_range(uint32_t ofs, int32_t* min, int32_t* max) {
  if (ofs / 4 >= 8) return kObjectNotFound;
  *min = range_[ofs / 4][0], *max = range_[ofs / 4][1];
  return 0;
}
int32_t SdlInput::joystick_set_deadzone(uint32_t ofs, uint32_t dz) {
  if (ofs / 4 >= 8) return kObjectNotFound;
  deadzone_[ofs / 4] = dz > 10000 ? 10000 : dz;
  return 0;
}

void SdlInput::on_event(const SDL_Event& e) {
  switch (e.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
      focused_ = true;
      if (held_at_loss_) released_ = false, activation_guard_ = true, gained_ns_ = e.common.timestamp;  // ours
      held_at_loss_ = false;
      refresh();  // a mission let go before (Ctrl+Alt) stays let go until a click
      break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      held_at_loss_ = win_ && mission_ && !released_;
      activation_guard_ = false;
      focused_ = false;
      if (win_) released_ = true;
      chord_held_ = SDL_KMOD_NONE, chord_armed_ = false;
      // FIX (A): what is let go while away never comes back as an event, and
      // DirectInput's state after the next Acquire was the device's as it
      // was: no button or key stays down from before.
      std::fill(std::begin(buttons_), std::end(buttons_), 0);
      std::fill(std::begin(latched_), std::end(latched_), 0);
      std::fill(std::begin(held_), std::end(held_), false);
      mouse_buttons = 0;
      for (int d = 0; d < 3; ++d) {
        if (acquired_[d] && (coop_[d] & 5) == 5) {  // the system takes a foreground device away
          acquired_[d] = false;
          lost_[d] = true;
        }
      }
      refresh();
      break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
      const bool down = e.type == SDL_EVENT_KEY_DOWN;
      if (!e.key.repeat) release_chord(e.key.scancode, down);
      const uint8_t dik = dik_from_scancode(e.key.scancode);
      if (!dik) break;
      if (down && e.key.repeat) break;  // DirectInput reports a held key once
      held_[dik] = down;
      if (!acquired_[kKeyboard]) break;
      if (keys_.size() >= kBufferSize) {
        overflow_ = true;  // DirectInput keeps the oldest and drops the rest
        break;
      }
      keys_.push_back({dik, down ? 0x80u : 0u, static_cast<uint32_t>(e.key.timestamp / 1000000), sequence_++});
      break;
    }
    case SDL_EVENT_MOUSE_MOTION:
      mouse_x = e.motion.x, mouse_y = e.motion.y;
      cursor_moved_ = true;
      raw_dx_ += e.motion.xrel, raw_dy_ += e.motion.yrel;
      if (acquired_[kMouse] && !muted()) {
        float k = captured_ ? 1.0f : mickey_scale;
        if (!captured_ && fitted() && window) {
          int w = 0, h = 0;
          SDL_GetWindowSize(window, &w, &h);
          k = sdlfe::mickey_scale(static_cast<float>(w), static_cast<float>(h), *shape);
        }
        dx_ += e.motion.xrel * k, dy_ += e.motion.yrel * k;
      }
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      if (acquired_[kMouse] && !muted()) dz_ += e.wheel.y * 120.0f;  // WHEEL_DELTA a notch
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
      // DirectInput's order: left, right, middle, then the fourth.
      const int b = e.button.button == SDL_BUTTON_LEFT ? 0 : e.button.button == SDL_BUTTON_RIGHT ? 1
                    : e.button.button == SDL_BUTTON_MIDDLE ? 2 : e.button.button == SDL_BUTTON_X1 ? 3 : -1;
      if (b < 0) break;
      const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
      const bool activating = down && activation_guard_ && e.button.timestamp < gained_ns_ + kActivationNs;
      if (down) activation_guard_ = false;
      if (activating) {  // the click that brought the focus back (the mouse already taken): not the game's
        swallow_[b] = true;
      } else if (down && muted() && focused_) {  // a click inside takes the mouse back, unseen by the game
        released_ = false;
        swallow_[b] = true;
        refresh();
      } else if (swallow_[b] && !down) {
        swallow_[b] = false;
      } else if (!muted() && !swallow_[b]) {
        buttons_[b] = down ? 0x80 : 0;
        if (down) latched_[b] = 0x80;
      }
      if (b < 3) mouse_buttons = down ? mouse_buttons | (1u << b) : mouse_buttons & ~(1u << b);
      mouse_x = e.button.x, mouse_y = e.button.y;
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------------------
// SdlZinHost

namespace {
SDL_Scancode scancode_of(uint32_t dik) {
  for (int s = 0; s < SDL_SCANCODE_COUNT; ++s)
    if (dik_from_scancode(static_cast<SDL_Scancode>(s)) == dik) return static_cast<SDL_Scancode>(s);
  return SDL_SCANCODE_UNKNOWN;
}
}  // namespace

void SdlZinHost::window_size(int* w, int* h) const {
  *w = fw_, *h = fh_;
  if (input_ && input_->window) SDL_GetWindowSize(input_->window, w, h);
}
// With any_resolution the client area is the frame (the display rect) and the
// mapping goes through its rect in the window (sdl_fit.h); else the 640x480
// frame stretched over the window.
void SdlZinHost::client_size(int32_t* w, int32_t* h) {
  *w = fw_, *h = fh_;
  if (input_ && input_->fitted()) *w = input_->shape->client_w(), *h = input_->shape->client_h();
}
void SdlZinHost::client_to_screen(int32_t* x, int32_t* y) {
  int w, h;
  window_size(&w, &h);
  if (input_ && input_->fitted()) {
    float wx, wy;
    frame_to_window(static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(w), static_cast<float>(h),
                    *input_->shape, &wx, &wy);
    *x = static_cast<int32_t>(std::lround(wx)), *y = static_cast<int32_t>(std::lround(wy));
    return;
  }
  *x = *x * w / fw_, *y = *y * h / fh_;
}
void SdlZinHost::screen_to_client(int32_t* x, int32_t* y) {
  int w, h;
  window_size(&w, &h);
  if (input_ && input_->fitted() && w > 0 && h > 0) {
    float fx, fy;
    window_to_frame(static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(w), static_cast<float>(h),
                    *input_->shape, &fx, &fy);
    *x = static_cast<int32_t>(std::floor(fx)), *y = static_cast<int32_t>(std::floor(fy));
    return;
  }
  if (w > 0 && h > 0) *x = *x * fw_ / w, *y = *y * fh_ / h;
}
// Windows' cursor follows the game's mouse; a captured (relative) mouse has none.
// In a window the player's cursor is never moved (sdl_input.h).
void SdlZinHost::set_cursor_pos(int32_t x, int32_t y) {
  if (input_ && input_->window && !input_->in_window() && !SDL_GetWindowRelativeMouseMode(input_->window))
    SDL_WarpMouseInWindow(input_->window, static_cast<float>(x), static_cast<float>(y));
}
// A virtual key for every key the DIK table knows (the scan code itself).
uint32_t SdlZinHost::map_virtual_key(uint32_t scan) {
  return scancode_of(scan) != SDL_SCANCODE_UNKNOWN ? scan : 0;
}
// VK_SHIFT, VK_CONTROL, VK_MENU held (0x80), VK_CAPITAL and VK_NUMLOCK toggled (1).
bool SdlZinHost::keyboard_state(uint8_t* st) {
  std::memset(st, 0, 256);
  const SDL_Keymod m = SDL_GetModState();
  if (m & SDL_KMOD_SHIFT) st[0x10] = 0x80;
  if (m & SDL_KMOD_CTRL) st[0x11] = 0x80;
  if (m & SDL_KMOD_ALT) st[0x12] = 0x80;
  if (m & SDL_KMOD_CAPS) st[0x14] = 1;
  if (m & SDL_KMOD_NUM) st[0x90] = 1;
#if defined(__APPLE__)
  // FIX (A): a Mac has no Num Lock and its keypad always types digits; SDL
  // reports the lock off, which made the keypad the cursor keys in a text
  // field (key_char's NumLock-off codes): no IP address from the keypad.
  st[0x90] = 1;
#endif
  return true;
}
int32_t SdlZinHost::to_ascii(uint32_t, uint32_t scan, const uint8_t* st, uint16_t* out) {
  const SDL_Scancode sc = scancode_of(scan);
  if (sc == SDL_SCANCODE_UNKNOWN) return 0;
  SDL_Keymod m = SDL_KMOD_NONE;
  if (st[0x10] & 0x80) m = static_cast<SDL_Keymod>(m | SDL_KMOD_SHIFT);
  if (st[0x14] & 1) m = static_cast<SDL_Keymod>(m | SDL_KMOD_CAPS);
  const SDL_Keycode k = SDL_GetKeyFromScancode(sc, m, false);
  if (k == SDLK_RETURN) return *out = 0x0d, 1;
  if (k == SDLK_BACKSPACE) return *out = 0x08, 1;
  if (k == SDLK_ESCAPE) return *out = 0x1b, 1;
  if (k == SDLK_TAB) return *out = 0x09, 1;
  if (k < 0x20 || k >= 0x7f) return 0;
  *out = static_cast<uint16_t>(k);
  return 1;
}

// 0x00597d90(name, 7, size, per user): saved with the other settings.
uint32_t* SdlZinHost::settings_block(const char* name, uint32_t size) {
  zreg::Entry* e = zreg::create(name, zreg::kBlock, size, zreg::kUser);
  return e ? reinterpret_cast<uint32_t*>(e->data) : nullptr;
}

}  // namespace sdlfe
