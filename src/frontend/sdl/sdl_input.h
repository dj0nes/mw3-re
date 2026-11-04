#pragma once
#include <SDL3/SDL.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "frontend/sdl/sdl_fit.h"
#include "gamez/zinput/zinput.h"
#include "platform/platform.h"

// platform::Input on SDL3 (docs/program.md, Input): DirectInput's devices as
// zInput opens them (0x0051b0a0). FIX (A): DirectInput does not exist on
// macOS. The keyboard's events arrive as SDL scancodes (USB usages) and are
// given back as the DIK codes (the PC keyboard's set-1 scan codes) the game's
// key tables use, one key to one key; the mouse's motion is summed into
// DIMOUSESTATE's relative counts, the first joystick read into a DIJOYSTATE2
// with DirectInput's default 0..65535 axis range.
namespace sdlfe {

// The DIK code of an SDL scancode, 0 for keys a 1998 keyboard did not have.
uint8_t dik_from_scancode(SDL_Scancode s);

// W54 (ours, FIX (A); docs/input.md, the joystick on SDL): SDL numbers a raw
// stick's axes by the ones it has, packed in DirectInput's order (X, Y, Z,
// RX, RY, RZ, sliders), where DIJOYSTATE2 keeps each at its own place: a
// stick with X, Y, twist (RZ) and a throttle slider gave the twist as Z, the
// game's throttle. `slots[i]` is SDL's axis for DIJOYSTATE2's axis i (X, Y,
// Z, RX, RY, RZ, slider 0, slider 1), -1 for none.
// The Generic Desktop axis usages (0x30 X .. 0x38 Wheel) a HID report
// descriptor's inputs carry, in SDL's order (by usage), each once.
std::vector<uint16_t> hid_axis_usages(const uint8_t* desc, size_t n);
// From those usages; false when their count is not SDL's.
bool axis_slots_from_usages(const std::vector<uint16_t>& usages, int sdl_axes, int slots[8]);
// mw3.cfg's joystick_axes= (SDL's axis 0, 1, ... each X, Y, Z, Rx, Ry, Rz,
// S0, S1 or -); false for a word it does not know or a slot twice.
bool axis_slots_from_text(const std::string& text, int sdl_axes, int slots[8]);
// The slots as joystick_axes= writes them (for the log).
std::string axis_slots_text(const int slots[8], int sdl_axes);
// Which attached device a start opens, with no joystick= in mw3.cfg: a
// gamepad, else one with 2 axes and 4 buttons or more, else any.
int joystick_rank(bool gamepad, int axes, int buttons);

class SdlInput : public platform::Input {
 public:
  ~SdlInput() override;
  bool open(Device d) override;
  void close(Device d) override;
  int32_t acquire(Device d) override;
  int32_t unacquire(Device d) override;
  int32_t keyboard_data(platform::KeyEvent* out, uint32_t* count) override;
  int32_t mouse_state(platform::MouseState* out) override;
  int32_t joystick_state(platform::JoyState* out) override;
  void set_cooperative_level(Device d, uint32_t flags) override;
  // W54 (ours): mw3.cfg's joystick= (a part of the device's name, any case:
  // that one, else the best ranked) and joystick_axes= (the axis map, else
  // the device's HID descriptor, then kKnownSticks, then SDL's packed order).
  std::string joystick_want, joystick_axes;
  // The axis usages of the open stick's HID descriptor, empty when it cannot
  // be read (tests replace it; read_hid_usages by default).
  std::function<std::vector<uint16_t>(SDL_Joystick*)> hid_usages;
  const char* axis_source() const { return axis_source_; }
  const int* axis_slots() const { return slot_; }
  bool unplugged() const { return unplugged_; }
  int32_t joystick_caps(uint32_t* flags, uint32_t* axes) override;
  bool joystick_gamepad() const override { return pad_ != nullptr || (unplugged_ && pad_was_); }
  SDL_Joystick* joystick() const { return joy_; }  // the force feedback's device (sdl_force.h)
  int32_t joystick_object_name(uint32_t ofs, char* name, uint32_t n) override;
  int32_t joystick_set_range(uint32_t ofs, int32_t min, int32_t max) override;
  int32_t joystick_get_range(uint32_t ofs, int32_t* min, int32_t* max) override;
  int32_t joystick_set_deadzone(uint32_t ofs, uint32_t dz) override;

  // The window an exclusive mouse captures (relative mouse mode, as
  // DirectInput's DISCL_EXCLUSIVE hides and holds the cursor). Captured, a
  // mickey is a count of the mouse's own, as DirectInput's were at any screen
  // size (FIX (A); SDL's relative motion is raw, without the system's speed).
  // Not captured, the mickeys a window pixel of motion makes (the game's 640
  // over the window's width).
  SDL_Window* window = nullptr;
  float mickey_scale = 1.0f;
  // With any_resolution, the frame's shape in the window (sdl_fit.h): an
  // uncaptured mouse's scale is then a frame pixel a mickey, through the
  // pillarboxed rect.
  const FrameShape* shape = nullptr;
  bool captured() const { return captured_; }
  bool fitted() const { return shape && shape->fit; }

  // FIX (A), ours (docs/input.md, the mouse in a window): in a window the
  // mouse is the player's to take out. The menus never capture: the cursor
  // is free and zInput's mouse is put where it is (`absolute_cursor`, the
  // exe's 0x0051b4f0). A mission captures; kReleaseKeys pressed together and
  // let go with no other key between, the pause, or the focus lost let the
  // mouse go (the game then sees no motion or buttons) until a click inside
  // the window, which the game does not see. Fullscreen holds it as before.
  static constexpr SDL_Keymod kReleaseKeys = SDL_KMOD_CTRL | SDL_KMOD_ALT;
  std::function<bool()> windowed;    // shown, not fullscreen (unset: fullscreen's rules)
  std::function<bool()> in_mission;  // the mission's own state (not a menu or the pause)
  std::function<void(float, float)> absolute_cursor;  // window point -> zInput's mouse
  bool released() const { return released_; }
  bool in_window() const { return win_; }  // as of the last refresh
  void refresh();  // the capture as the window, the state and the release say

  // From the message pump: every SDL input event passes through here.
  void on_event(const SDL_Event& e);

  // The keys held now, by DIK code (kept from the events, for tools).
  bool key_down(uint8_t dik) const { return held_[dik]; }
  // The mouse in window pixels and its buttons now (Windows' cursor, GetCursorPos).
  float mouse_x = 0, mouse_y = 0;
  uint32_t mouse_buttons = 0;  // bit 0 left, 1 right, 2 middle
  // Window-pixel motion since the last call, whether or not zInput holds the
  // mouse (for tools such as the free camera; the game reads mouse_state).
  void take_motion(float* x, float* y) { *x = raw_dx_, *y = raw_dy_, raw_dx_ = raw_dy_ = 0; }

  static constexpr uint32_t kBufferSize = 0x80;  // DIPROP_BUFFERSIZE (0x0051a170)

 private:
  void capture(bool on);
  int axis_count() const;
  bool open_joystick();
  void map_axes();
  void neutral(platform::JoyState* out) const;
  double pad_axis(int i) const;
  bool opened_[3] = {};
  bool acquired_[3] = {};
  bool lost_[3] = {};
  uint32_t coop_[3] = {};
  bool focused_ = true;
  bool captured_ = false;  // capture(true) asked: the relative mouse
  bool muted() const { return released_ && win_ && mission_; }
  void release_chord(SDL_Scancode s, bool down);
  bool win_ = false, mission_ = false, absolute_ = false;
  bool released_ = false;
  bool cursor_hidden_ = false;
  bool cursor_moved_ = false;  // the cursor moved since the menus took it (else zInput's place stays)
  SDL_Keymod chord_held_ = SDL_KMOD_NONE;
  bool chord_armed_ = false, chord_spoiled_ = false;
  bool swallow_[4] = {};  // the recapturing click's buttons, kept from the game until let go
  // Ours (playtest, Owen): a held mission's mouse comes back with the focus,
  // no click needed; the click that brought it back (within kActivationNs of
  // the focus) is still not the game's (input.md section 8).
  static constexpr uint64_t kActivationNs = 250000000ull;
  bool held_at_loss_ = false, activation_guard_ = false;
  uint64_t gained_ns_ = 0;
  int32_t range_[8][2] = {{0, 65535}, {0, 65535}, {0, 65535}, {0, 65535},
                          {0, 65535}, {0, 65535}, {0, 65535}, {0, 65535}};
  uint32_t deadzone_[8] = {};
  std::deque<platform::KeyEvent> keys_;
  bool overflow_ = false;
  uint32_t sequence_ = 0;
  bool held_[256] = {};
  float dx_ = 0, dy_ = 0, dz_ = 0;
  float raw_dx_ = 0, raw_dy_ = 0;
  uint8_t buttons_[4] = {};
  uint8_t latched_[4] = {};  // a press not yet read (FIX (A): a click inside one frame)
  SDL_Joystick* joy_ = nullptr;
  // FIX (A): a gamepad SDL knows (an XInput pad, a PlayStation one) is read
  // as DirectInput showed an XInput pad on Windows: X, Y the left stick, Z the
  // triggers on one centred axis (LT up, RT down), RX, RY the right stick, no
  // RZ; buttons A B X Y LB RB Back Start LS RS; the d-pad the hat. SDL's raw
  // order differs by platform and its triggers rest at one end, which pinned
  // the game's throttle (Z) and twist (RZ). Other sticks keep the raw path.
  SDL_Gamepad* pad_ = nullptr;
  int slot_[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  const char* axis_source_ = "";
  SDL_JoystickID id_ = 0;
  SDL_GUID guid_{};
  bool unplugged_ = false;  // ours: taken out; it reads centred until it is back
  bool pad_was_ = false;    // it was opened as a gamepad
  platform::JoyState last_;  // the last live read
  bool tried_ = false;      // zInput asked for a joystick
};

// The default hid_usages: the device's HID descriptor through SDL_hid, only
// a joystick, gamepad or multi-axis interface of its vendor and product
// (SDL lists no other kind, so no keyboard is opened: no macOS Input
// Monitoring prompt); empty when it cannot be opened (Linux's root-only
// hidraw, a virtual pad).
std::vector<uint16_t> read_hid_usages(SDL_Joystick* j);

// zInput's Win32 calls on SDL (FIX (A), docs/input.md): the client area is
// the game's frame (the window scaled to it), the cursor SDL's, and the
// characters (MapVirtualKey, GetKeyboardState, ToAscii) the SDL keymap's.
class SdlZinHost : public zin::Host {
 public:
  SdlZinHost(SdlInput* input, int frame_w = 640, int frame_h = 480) : input_(input), fw_(frame_w), fh_(frame_h) {}
  void client_to_screen(int32_t* x, int32_t* y) override;
  void screen_to_client(int32_t* x, int32_t* y) override;
  void set_cursor_pos(int32_t x, int32_t y) override;
  void client_size(int32_t* w, int32_t* h) override;
  uint32_t map_virtual_key(uint32_t scan) override;
  bool keyboard_state(uint8_t* state) override;
  int32_t to_ascii(uint32_t vk, uint32_t scan, const uint8_t* state, uint16_t* out) override;
  uint32_t* settings_block(const char* name, uint32_t size) override;  // the settings store's entry

 private:
  void window_size(int* w, int* h) const;
  SdlInput* input_;
  int fw_, fh_;
};

}  // namespace sdlfe
