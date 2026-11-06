// Ours, not original code: a game controller as the mouse and the keys the
// game already reads (docs/pad-bridge.md). On tvOS there is no mouse or
// keyboard: in the menus the left stick moves a cursor and A clicks; in a
// mission the sticks are the game's joystick axes (SdlInput, and the axis
// table's controller defaults, mech3::set_pad_stick_axes), and every button
// and trigger is a key of the game's own key table, with LB a hold layer
// for the less common ones. On the Mac, mw3.cfg's pad_bridge=1 turns it on,
// to tune it with the same pad.
#pragma once
#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

namespace sdlfe {

struct FrameRect {
  float x, y, w, h;  // the game frame inside the window, in window points
};

// A stick's position shaped (SdlInput's gamepad sticks with the bridge on):
// a round dead zone `dz`, the rest scaled to 0..1 and raised to `power`
// (1 straight, 2 fine near the centre), the direction kept.
void shape_stick(float x, float y, float dz, float power, float* ox, float* oy);

// The triggers, as buttons past SDL's (pressed past half way).
constexpr int kPadLeftTrigger = SDL_GAMEPAD_BUTTON_COUNT;
constexpr int kPadRightTrigger = SDL_GAMEPAD_BUTTON_COUNT + 1;

// A mission's button: the game's keys it holds down (pressed in order, let go
// in reverse), and keys tapped once it is let go (a look's way back).
struct MissionBinding {
  int button;  // SDL_GamepadButton or kPad*Trigger
  bool layer;  // with LB held
  std::vector<SDL_Scancode> keys, then;
  const char* action;  // docs/controls.md's name for it
};
const std::vector<MissionBinding>& mission_bindings();

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
  // The system keyboard (tvOS): up while the game's text field has the focus
  // (zin::text_focus, given each frame), or by Y in the menus. While it is
  // up the pad drives it, not the game; its Back (B or Menu) or its Done
  // (Enter) closes it, and a field that keeps the focus gets it back only
  // by Y or a new focus.
  void text_focus(bool focused);
  bool keyboard_open() const { return keyboard_; }
  // Shows (true) or hides the keyboard; unset: SDL's text input on `window`,
  // with the controller's input given to the system while it is up.
  std::function<void(bool)> show_keyboard;
  SDL_Window* window = nullptr;

  static constexpr float kDeadZone = 0.15f;
  static constexpr float kSpeed = 600.0f;  // window points a second at full tilt

 private:
  struct Action {
    bool click = false;                // the left mouse button
    std::vector<SDL_Scancode> keys;    // pressed in order, let go in reverse
    std::vector<SDL_Scancode> then;    // tapped after the release
  };
  std::vector<SDL_Event> button(int b, bool down, bool in_mission);
  std::vector<SDL_Event> press(const Action& a) const;
  std::vector<SDL_Event> release(const Action& a) const;
  std::vector<SDL_Event> release_all();
  void clamp();
  void keyboard(bool on);
  bool keyboard_ = false, focus_ = false;
  FrameRect frame_{0, 0, 1920, 1080};
  float x_ = 960, y_ = 540;
  bool lb_ = false;
  bool trigger_[2] = {};  // LT, RT past half way
  std::map<int, Action> held_;  // by SDL_GamepadButton
  SDL_Gamepad* pad_ = nullptr;
  SDL_JoystickID pad_id_ = 0;
};

}  // namespace sdlfe
