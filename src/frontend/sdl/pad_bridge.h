// Ours, not original code: a game controller as the mouse and the keys the
// game already reads (docs/pad-bridge.md). On tvOS there is no mouse or
// keyboard: in the menus the left stick moves a cursor and A clicks; in a
// mission the pad is the game's joystick (SdlInput), and LB held turns the
// other buttons into the keyboard-only actions. On the Mac, mw3.cfg's
// pad_bridge=1 turns it on, to tune it with the same pad.
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <map>
#include <vector>

namespace sdlfe {

struct FrameRect {
  float x, y, w, h;  // the game frame inside the window, in window points
};

class PadBridge {
 public:
  PadBridge();
  ~PadBridge();
  PadBridge(const PadBridge&) = delete;
  PadBridge& operator=(const PadBridge&) = delete;

  void set_frame(FrameRect r);
  // A gamepad event in, the keyboard and mouse events it stands for out;
  // other events: none. Opens the first gamepad that is added (SDL sends a
  // pad's buttons only once it is open).
  std::vector<SDL_Event> on_event(const SDL_Event& e, bool in_mission);
  // Once a frame: the cursor's motion from the left stick (menus only).
  std::vector<SDL_Event> tick(double dt_s, bool in_mission, float left_x, float left_y);
  // The joystick buttons SdlInput must read as up (bit i = DirectInput
  // button i of the XInput layout; bit 31 = the hat).
  uint32_t joystick_mask(bool in_mission) const;
  float cursor_x() const { return x_; }
  float cursor_y() const { return y_; }
  // A gamepad already connected, opened (its ADDED event may be gone).
  void open_present();
  // The open pad's left stick, -1..1 (0, 0 without one).
  void left_stick(float* x, float* y) const;
  // Y in the menus shows the system keyboard for this window (tvOS).
  SDL_Window* window = nullptr;

  static constexpr float kDeadZone = 0.15f;
  static constexpr float kSpeed = 600.0f;  // window points a second at full tilt

 private:
  struct Action {
    bool click = false;                // the left mouse button
    std::vector<SDL_Scancode> keys;    // pressed in order, let go in reverse
    int bit = -1;                      // the joystick bit it keeps masked while held
  };
  std::vector<SDL_Event> press(const Action& a) const;
  std::vector<SDL_Event> release(const Action& a) const;
  std::vector<SDL_Event> release_all();
  void clamp();
  FrameRect frame_{0, 0, 1920, 1080};
  float x_ = 960, y_ = 540;
  bool lb_ = false;
  std::map<int, Action> held_;  // by SDL_GamepadButton
  SDL_Gamepad* pad_ = nullptr;
  SDL_JoystickID pad_id_ = 0;
};

}  // namespace sdlfe
