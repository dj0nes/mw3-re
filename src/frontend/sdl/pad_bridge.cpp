#include "frontend/sdl/pad_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sdlfe {

namespace {

// The DirectInput button (the XInput layout SdlInput reads, docs/input.md)
// of an SDL gamepad button; 31 for the d-pad (the hat); -1 for none.
int joystick_bit(int b) {
  switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return 0;
    case SDL_GAMEPAD_BUTTON_EAST: return 1;
    case SDL_GAMEPAD_BUTTON_WEST: return 2;
    case SDL_GAMEPAD_BUTTON_NORTH: return 3;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 4;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 5;
    case SDL_GAMEPAD_BUTTON_BACK: return 6;
    case SDL_GAMEPAD_BUTTON_START: return 7;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return 8;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return 9;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 31;
    default: return -1;
  }
}

// Hold LB + a button: the keys of its action (docs/controls.md's defaults).
std::vector<SDL_Scancode> layer_keys(int b) {
  switch (b) {
    case SDL_GAMEPAD_BUTTON_SOUTH: return {SDL_SCANCODE_LCTRL, SDL_SCANCODE_E};  // target nearest enemy
    case SDL_GAMEPAD_BUTTON_EAST: return {SDL_SCANCODE_Z};                      // zoom reticule
    case SDL_GAMEPAD_BUTTON_WEST: return {SDL_SCANCODE_Q};                      // target under the reticule
    case SDL_GAMEPAD_BUTTON_NORTH: return {SDL_SCANCODE_X};                     // cycle camera
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return {SDL_SCANCODE_BACKSLASH};    // fire mode (joystick 5, LB's)
    case SDL_GAMEPAD_BUTTON_BACK: return {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_R}; // radar map
    case SDL_GAMEPAD_BUTTON_START: return {SDL_SCANCODE_S};                     // startup/shutdown
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return {SDL_SCANCODE_SLASH};            // centre legs (joystick 8, Start's)
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return {SDL_SCANCODE_F11};             // lancemate commands
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return {SDL_SCANCODE_N};                   // next nav point
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return {SDL_SCANCODE_G};                 // get up
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return {SDL_SCANCODE_F};                 // flush coolant
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return {SDL_SCANCODE_V};                // MASC
    default: return {};
  }
}

constexpr uint32_t kAll = 0x800003ffu;                     // every button and the hat
constexpr uint32_t kMissionAlways = (1u << 4) | (1u << 7);  // LB and Start

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
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
      const int b = e.gbutton.button;
      if (held_.count(b)) return {};
      if (b == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
        lb_ = true;
        return {};
      }
      Action a;
      a.bit = joystick_bit(b);
      if (!in_mission) {
        if (b == SDL_GAMEPAD_BUTTON_SOUTH) a.click = true;
        else if (b == SDL_GAMEPAD_BUTTON_EAST || b == SDL_GAMEPAD_BUTTON_START) a.keys = {SDL_SCANCODE_ESCAPE};
        else if (b == SDL_GAMEPAD_BUTTON_NORTH && window) {
          // The system keyboard; it sends each character as key presses.
          if (SDL_TextInputActive(window)) SDL_StopTextInput(window);
          else SDL_StartTextInput(window);
        }
      } else if (lb_) {
        a.keys = layer_keys(b);
      } else if (b == SDL_GAMEPAD_BUTTON_START) {
        a.keys = {SDL_SCANCODE_ESCAPE};  // the pause menu
      }
      if (!a.click && a.keys.empty()) return {};
      held_[b] = a;
      return press(a);
    }
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
      const int b = e.gbutton.button;
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
    default:
      return {};
  }
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

uint32_t PadBridge::joystick_mask(bool in_mission) const {
  if (!in_mission || lb_) return kAll;
  uint32_t m = kMissionAlways;
  for (const auto& [b, a] : held_)
    if (a.bit >= 0) m |= 1u << a.bit;
  return m;
}

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
