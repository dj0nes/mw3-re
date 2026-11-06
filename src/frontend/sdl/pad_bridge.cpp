#include "frontend/sdl/pad_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sdlfe {

namespace {

constexpr uint32_t kAll = 0x800003ffu;  // every button and the hat: the bridge's keys, not the joystick's

SDL_Event key_event(SDL_Scancode s, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.timestamp = SDL_GetTicksNS();
  e.key.scancode = s;
  e.key.key = SDL_GetKeyFromScancode(s, SDL_KMOD_NONE, false);
  e.key.down = down;
  return e;
}

}  // namespace

// The mission's buttons (docs/pad-bridge.md), each a command of the game's
// default key table (docs/controls.md). Each action once, base or layer.
const std::vector<MissionBinding>& mission_bindings() {
  using B = MissionBinding;
  static const std::vector<B> k = {
      {kPadRightTrigger, false, {SDL_SCANCODE_SPACE}, {}, "Fire weapon"},
      {kPadLeftTrigger, false, {SDL_SCANCODE_KP_ENTER}, {}, "Alpha Strike"},
      {SDL_GAMEPAD_BUTTON_SOUTH, false, {SDL_SCANCODE_J}, {}, "Jump Jet"},
      {SDL_GAMEPAD_BUTTON_EAST, false, {SDL_SCANCODE_C}, {}, "Crouch"},
      {SDL_GAMEPAD_BUTTON_WEST, false, {SDL_SCANCODE_KP_0}, {}, "Lock Zoom reticule"},
      {SDL_GAMEPAD_BUTTON_NORTH, false, {SDL_SCANCODE_RIGHTBRACKET}, {}, "Next weapon group"},
      {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, false, {SDL_SCANCODE_LCTRL, SDL_SCANCODE_E}, {}, "Target nearest enemy"},
      {SDL_GAMEPAD_BUTTON_BACK, false, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_R}, {}, "Toggle Radar map"},
      {SDL_GAMEPAD_BUTTON_START, false, {SDL_SCANCODE_ESCAPE}, {}, "Pause menu"},
      {SDL_GAMEPAD_BUTTON_LEFT_STICK, false, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_SLASH}, {}, "Center torso to legs"},
      {SDL_GAMEPAD_BUTTON_RIGHT_STICK, false, {SDL_SCANCODE_Q}, {}, "Target object under reticule"},
      {SDL_GAMEPAD_BUTTON_DPAD_UP, false, {SDL_SCANCODE_N}, {}, "Next Op point"},
      // The looks while held, then forward again (the hat's looks were toggles).
      {SDL_GAMEPAD_BUTTON_DPAD_LEFT, false, {SDL_SCANCODE_LALT, SDL_SCANCODE_KP_4}, {SDL_SCANCODE_LALT, SDL_SCANCODE_KP_5}, "Look Left"},
      {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, false, {SDL_SCANCODE_LALT, SDL_SCANCODE_KP_6}, {SDL_SCANCODE_LALT, SDL_SCANCODE_KP_5}, "Look Right"},
      {SDL_GAMEPAD_BUTTON_DPAD_DOWN, false, {SDL_SCANCODE_LALT, SDL_SCANCODE_KP_PERIOD}, {SDL_SCANCODE_LALT, SDL_SCANCODE_KP_5}, "Look back"},
      // Hold LB:
      {SDL_GAMEPAD_BUTTON_SOUTH, true, {SDL_SCANCODE_G}, {}, "Get up"},
      {SDL_GAMEPAD_BUTTON_EAST, true, {SDL_SCANCODE_F}, {}, "Flush Coolant"},
      {SDL_GAMEPAD_BUTTON_WEST, true, {SDL_SCANCODE_X}, {}, "Cycle Camera Mode"},
      {SDL_GAMEPAD_BUTTON_NORTH, true, {SDL_SCANCODE_LEFTBRACKET}, {}, "Previous weapon group"},
      {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true, {SDL_SCANCODE_E}, {}, "Target next enemy"},
      {kPadRightTrigger, true, {SDL_SCANCODE_BACKSLASH}, {}, "Toggle fire mode"},
      {kPadLeftTrigger, true, {SDL_SCANCODE_V}, {}, "Toggle MASC"},
      {SDL_GAMEPAD_BUTTON_BACK, true, {SDL_SCANCODE_F11}, {}, "Lancemate command screen"},
      {SDL_GAMEPAD_BUTTON_START, true, {SDL_SCANCODE_S}, {}, "Startup/Shutdown"},
      {SDL_GAMEPAD_BUTTON_LEFT_STICK, true, {SDL_SCANCODE_SLASH}, {}, "Center Legs"},
      {SDL_GAMEPAD_BUTTON_RIGHT_STICK, true, {SDL_SCANCODE_O}, {}, "Override Thermal Shutdown"},
      {SDL_GAMEPAD_BUTTON_DPAD_UP, true, {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_N}, {}, "Previous Op point"},
      {SDL_GAMEPAD_BUTTON_DPAD_DOWN, true, {SDL_SCANCODE_D}, {}, "Toggle Damage display"},
      {SDL_GAMEPAD_BUTTON_DPAD_LEFT, true, {SDL_SCANCODE_T}, {}, "Clear Target Selection"},
      {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, true, {SDL_SCANCODE_R}, {}, "Toggle Radar size"},
  };
  return k;
}

void shape_stick(float x, float y, float dz, float power, float* ox, float* oy) {
  const float m = std::sqrt(x * x + y * y);
  if (m <= dz || m <= 0.0f) {
    *ox = *oy = 0.0f;
    return;
  }
  const float t = std::pow(std::min(1.0f, (m - dz) / (1.0f - dz)), power);
  *ox = x / m * t, *oy = y / m * t;
}

PadBridge::PadBridge() = default;
PadBridge::~PadBridge() {
  if (pad_) SDL_CloseGamepad(pad_);
}

void PadBridge::set_frame(FrameRect r) {
  frame_ = r;
  clamp();
}

void PadBridge::clamp() {
  x_ = std::clamp(x_, frame_.x, std::max(frame_.x, frame_.x + frame_.w - 1));
  y_ = std::clamp(y_, frame_.y, std::max(frame_.y, frame_.y + frame_.h - 1));
}

std::vector<SDL_Event> PadBridge::press(const Action& a) const {
  std::vector<SDL_Event> out;
  if (a.click) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    e.button.timestamp = SDL_GetTicksNS();
    e.button.button = SDL_BUTTON_LEFT, e.button.down = true, e.button.clicks = 1;
    e.button.x = x_, e.button.y = y_;
    out.push_back(e);
  }
  for (SDL_Scancode s : a.keys) out.push_back(key_event(s, true));
  return out;
}

std::vector<SDL_Event> PadBridge::release(const Action& a) const {
  std::vector<SDL_Event> out;
  for (auto it = a.keys.rbegin(); it != a.keys.rend(); ++it) out.push_back(key_event(*it, false));
  if (a.click) {
    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.timestamp = SDL_GetTicksNS();
    e.button.button = SDL_BUTTON_LEFT, e.button.down = false, e.button.clicks = 1;
    e.button.x = x_, e.button.y = y_;
    out.push_back(e);
  }
  for (SDL_Scancode s : a.then) out.push_back(key_event(s, true));
  for (auto it = a.then.rbegin(); it != a.then.rend(); ++it) out.push_back(key_event(*it, false));
  return out;
}

std::vector<SDL_Event> PadBridge::release_all() {
  std::vector<SDL_Event> out;
  for (const auto& [b, a] : held_) {
    const std::vector<SDL_Event> r = release(a);
    out.insert(out.end(), r.begin(), r.end());
  }
  held_.clear();
  lb_ = false;
  trigger_[0] = trigger_[1] = false;
  return out;
}

std::vector<SDL_Event> PadBridge::on_event(const SDL_Event& e, bool in_mission) {
  switch (e.type) {
    case SDL_EVENT_GAMEPAD_ADDED:
      if (!pad_) {
        pad_ = SDL_OpenGamepad(e.gdevice.which);
        if (pad_) {
          pad_id_ = e.gdevice.which;
          const char* name = SDL_GetGamepadName(pad_);
          std::printf("mw3: pad bridge: \"%s\" is the menus' cursor\n", name ? name : "");
        }
      }
      return {};
    case SDL_EVENT_GAMEPAD_REMOVED:
      // A pad switched off mid-press: no key or click stays down.
      if (pad_ && e.gdevice.which == pad_id_) SDL_CloseGamepad(pad_), pad_ = nullptr, pad_id_ = 0;
      return release_all();
    case SDL_EVENT_KEY_DOWN:
      // The keyboard's Done sends Enter and goes away.
      if (keyboard_ && e.key.scancode == SDL_SCANCODE_RETURN) keyboard(false);
      return {};
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
      return button(e.gbutton.button, e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, in_mission);
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      // A trigger is a button past half way, let go under a quarter.
      const int t = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ? 0 : e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ? 1 : -1;
      if (t < 0) return {};
      const bool was = trigger_[t];
      const bool now = was ? e.gaxis.value >= 8192 : e.gaxis.value >= 16384;
      if (now == was) return {};
      trigger_[t] = now;
      return button(t == 0 ? kPadLeftTrigger : kPadRightTrigger, now, in_mission);
    }
    default:
      return {};
  }
}

std::vector<SDL_Event> PadBridge::button(int b, bool down, bool in_mission) {
  if (!down) {
    if (keyboard_ && (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_START) && !held_.count(b)) {
      keyboard(false);  // its Back closed it; the field keeps the focus
      return {};
    }
    if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
      lb_ = false;  // a layered button still held keeps its key until let go
      return {};
    }
    auto it = held_.find(b);
    if (it == held_.end()) return {};
    const std::vector<SDL_Event> out = release(it->second);
    held_.erase(it);
    return out;
  }
  if (held_.count(b) || keyboard_) return {};  // the system keyboard has the pad
  if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
    lb_ = true;
    return {};
  }
  Action a;
  if (!in_mission) {
    if (b == SDL_GAMEPAD_BUTTON_SOUTH) a.click = true;
    else if (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_START) a.keys = {SDL_SCANCODE_ESCAPE};
    else if (b == SDL_GAMEPAD_BUTTON_NORTH) keyboard(true);  // the system keyboard, by hand
  } else {
    for (const MissionBinding& m : mission_bindings())
      if (m.button == b && m.layer == lb_) a.keys = m.keys, a.then = m.then;
  }
  if (!a.click && a.keys.empty()) return {};
  held_[b] = a;
  return press(a);
}

std::vector<SDL_Event> PadBridge::tick(double dt_s, bool in_mission, float left_x, float left_y) {
  if (in_mission || dt_s <= 0) return {};
  const float m = std::sqrt(left_x * left_x + left_y * left_y);
  if (m <= kDeadZone) return {};
  // Slow near the centre, fast at full tilt: speed * (the tilt past the dead
  // zone, 0..1) squared.
  const float t = std::min(1.0f, (m - kDeadZone) / (1.0f - kDeadZone));
  const float v = kSpeed * t * t * static_cast<float>(dt_s);
  const float x0 = x_, y0 = y_;
  x_ += left_x / m * v, y_ += left_y / m * v;
  clamp();
  if (x_ == x0 && y_ == y0) return {};
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.timestamp = SDL_GetTicksNS();
  e.motion.x = x_, e.motion.y = y_;
  e.motion.xrel = x_ - x0, e.motion.yrel = y_ - y0;
  return {e};
}

void PadBridge::keyboard(bool on) {
  if (on == keyboard_) return;
  keyboard_ = on;
  if (show_keyboard) {
    show_keyboard(on);
    return;
  }
  // tvOS: the system's keyboard is UIKit's, which hears the controller only
  // while SDL lets the system have its input (else Menu would leave the app).
  SDL_SetHint(SDL_HINT_APPLE_TV_CONTROLLER_UI_EVENTS, on ? "1" : "0");
  if (window) {
    if (on) SDL_StartTextInput(window);
    else SDL_StopTextInput(window);
  }
  std::printf("mw3: pad bridge: the system keyboard %s\n", on ? "up" : "down");
}

void PadBridge::text_focus(bool focused) {
  if (focused == focus_) return;
  focus_ = focused;
  keyboard(focused);
}

// Every button and the hat, in the menus and in a mission alike: the bridge
// gives their keys; the joystick is the sticks alone.
uint32_t PadBridge::joystick_mask(bool) const { return kAll; }

void PadBridge::open_present() {
  if (pad_) return;
  int n = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&n);
  if (ids && n > 0) {
    SDL_Event e{};
    e.type = SDL_EVENT_GAMEPAD_ADDED, e.gdevice.which = ids[0];
    on_event(e, false);
  }
  SDL_free(ids);
}

void PadBridge::left_stick(float* x, float* y) const {
  *x = *y = 0;
  if (!pad_) return;
  *x = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
  *y = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
}

}  // namespace sdlfe
