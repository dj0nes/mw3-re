#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "platform/platform.h"

// zInput, the engine's input (C:\zipper\GameZMech3\zInput: zin_kbd.cpp,
// zin_init.cpp; the mouse, joystick and command map follow zin_init.cpp in
// the exe with no name of their own). docs/input.md. Globals keep their
// original addresses in the comments.
namespace zin {

// FIX (A): the Win32 calls zInput makes besides DirectInput, one for one.
struct Host {
  virtual ~Host() = default;
  virtual void client_to_screen(int32_t*, int32_t*) {}   // ClientToScreen(window)
  virtual void screen_to_client(int32_t*, int32_t*) {}   // ScreenToClient(window)
  virtual void set_cursor_pos(int32_t, int32_t) {}       // SetCursorPos
  virtual void client_size(int32_t* w, int32_t* h) { *w = *h = 0; }  // GetClientRect
  virtual uint32_t map_virtual_key(uint32_t) { return 0; }      // MapVirtualKeyA(scan, 1)
  virtual bool keyboard_state(uint8_t*) { return false; }  // GetKeyboardState
  virtual int32_t to_ascii(uint32_t, uint32_t, const uint8_t*, uint16_t*) { return 0; }
  // The settings store's entry the command map's bindings live in (0x00597d90,
  // type 7, per user; gamez/zutil/zutl_reg): null keeps them in the map.
  virtual uint32_t* settings_block(const char*, uint32_t) { return nullptr; }
};

using KeyCallback = void (*)(int32_t key);                         // ECX = the key code
using CharCallback = void (*)(int32_t ch, int32_t key, uintptr_t ctx);  // EDX the key code, the context on the stack
using CommandCallback = void (*)(int32_t command);                 // ECX = the command

constexpr int kKeys = 0x7de;           // key codes: DIK code | 0x100 Alt | 0x200 Ctrl | 0x400 Shift
constexpr uint32_t kAlt = 0x100, kCtrl = 0x200, kShift = 0x400;
// Key, button and command states: 1 pressed this poll, 2 held, 3 pressed
// again while held, | 4 released this poll (docs/input.md, 1).
constexpr uint32_t kPressed = 1, kHeld = 2, kReleased = 4;

struct KeySlot {                       // 0x006385ac, 8 bytes a key
  uint32_t state = 0;
  KeyCallback callback = nullptr;      // +4
};

struct MouseInfo {                     // 0x0063d1d8, what 0x0051b7b0 returns (15 dwords)
  int32_t x = 0, y = 0, z = 0;         // +0x00 the position in the screen, z the wheel (500 centred)
  float fx = 0, fy = 0, fz = 0;        // +0x0c from the centre, -1..1
  int32_t dx = 0, dy = 0, dz = 0;      // +0x18 this read's motion, after the acceleration
  float fdx = 0, fdy = 0, fdz = 0;     // +0x24 the same scaled
  uint32_t left = 0, right = 0, middle = 0;  // +0x30 button states
};

struct JoyAxis {                       // 0x118 bytes, six at 0x0063c6fc (X, Y, Z, RX, RY, RZ)
  int32_t min = 0, max = 0;            // +0x00 the range asked for
  float scale = 0, centre = 0;         // +0x08 2 / (max - min), +0x0c (min + max) / 2
  int32_t deadzone = 0;                // +0x10 in 1/10000 of the range
  char name[0x104] = {};               // +0x14 the object's name; empty: the stick has no such axis
};

// The command map (0x3f5c bytes, constructor 0x0051bb30).
struct CmdMap {
  uint8_t owned = 0;                   // +0x00 a pushed copy, deleted when popped
  int32_t count = 0;                   // +0x04 commands (0xa7 in MW3; 0 is none)
  uint32_t* bindings = nullptr;        // +0x08 per command: key1 bits 0-10, key2 11-21, joystick button 22-25, mouse button 26-27
  std::vector<uint32_t> own_bindings;  // +0x0c a copy's, or the map's own while the store is not ported
  std::vector<CommandCallback> callbacks;  // +0x10
  std::vector<std::string> names;      // +0x14 0x50 bytes each
  // +0x18 key1 -> command [0x7de], +0x1f90 key2 -> command [0x7de], +0x3f08
  // joystick button -> command [16], +0x3f48 mouse button -> command [4]: one
  // block, as in the original, so a key code past 0x7dd lands where it did.
  int32_t table[kKeys * 2 + 16 + 4] = {};
  uint8_t enabled = 1;                 // +0x3f58

  int32_t& key1_cmd(uint32_t k) { return table[k]; }
  int32_t& key2_cmd(uint32_t k) { return table[kKeys + k]; }
  int32_t& joy_cmd(uint32_t b) { return table[2 * kKeys + b]; }
  int32_t& mouse_cmd(uint32_t b) { return table[2 * kKeys + 16 + b]; }
};

struct State {
  State() {  // globals start zeroed, the hats too
    for (int i = 0; i < 4; ++i) joy.pov[i] = joy_prev.pov[i] = joy_read.pov[i] = 0;
  }
  platform::Input* di = nullptr;       // 0x00638578 IDirectInput
  uint32_t window = 0;                 // 0x0063857c the window; zInput is started while nonzero
  uint32_t started = 0;                // 0x00638580
  uint8_t kbd_flags = 0, joy_flags = 0, mouse_flags = 0;  // 0x00638584..86: 1 present, 2 suspended
  uint16_t kbd_users = 0, joy_users = 0, mouse_users = 0;  // 0x00638588, 8a, 8c

  // Keyboard (zin_kbd.cpp).
  uint32_t kbd_ready = 0;              // 0x00638590
  bool kbd_open = false;               // 0x00638594, the device
  std::vector<platform::KeyEvent> events;  // 0x00638598, 0x80 events
  uint32_t event_count = 0;            // 0x0063859c
  std::vector<int32_t> pressed;        // 0x006385a0 the codes of the last read, capacity 0x006385a4
  uint32_t pressed_capacity = 0;
  uint32_t modifiers = 0;              // 0x006385a8
  // Ours, FIX (A): the first plain key down in the last kbd_read, with its
  // modifiers, for the controls screen's capture (frame_key).
  uint32_t frame_key = 0;
  KeySlot keys[kKeys];                 // 0x006385ac
  CharCallback char_callback = nullptr;  // 0x0063c49c
  uintptr_t char_ctx = 0;              // 0x0063c4a0

  // Joystick.
  uint32_t joy_ready = 0;              // 0x0063c4a4
  bool joy_open = false;               // 0x0063c4a8
  platform::JoyState joy, joy_prev;    // 0x0063c4ac, 0x0063c5bc
  uint32_t joy_axes = 0;               // 0x0063c6ec
  uint32_t ff = 0, ff_attack = 0, ff_fade = 0;  // 0x0063c6f0.. DIDC_FORCEFEEDBACK, FFATTACK, FFFADE
  JoyAxis axes[6];                     // 0x0063c6fc
  platform::JoyState joy_read;         // 0x0063d218

  // Mouse.
  uint32_t mouse_ready = 0;            // 0x0063cd8c
  bool mouse_open = false;             // 0x0063cd90
  platform::MouseState mouse_cur, mouse_prev;  // 0x0063cd94, 0x0063cda4
  int32_t centre_y = 0;                // 0x0063d1a0
  platform::MouseState mouse_read;     // 0x0063d1a8
  float scale_x = 0, scale_y = 0, scale_z = 0;  // 0x0063d1b8, bc, c0
  int32_t centre_z = 0, centre_x = 0;  // 0x0063d1c4, c8
  uint32_t mouse_acquired = 0;         // 0x0063d1cc
  int32_t width = 0, height = 0;       // 0x0063d1d0, d4
  MouseInfo mouse;                     // 0x0063d1d8
  uint32_t no_clamp = 0;               // 0x0063d214 (nothing sets it)
  float accel_x = 1.3f, accel_y = 1.3f;  // 0x005ece48, 4c
  uint32_t mode = 5;                   // 0x005ece50 the mouse's cooperative level
  int32_t mouse_result = 1;            // 0x005ece54

  // Command maps.
  CmdMap* map = nullptr;               // 0x0063cdb8
  std::vector<CmdMap*> stack;          // 0x0063cdc4.. a list of saved maps, 0x0063cdd0 its depth
  std::unique_ptr<CmdMap> base;        // the first map, made by 0x0051c630
  std::string key_names[0xde];         // 0x0063cdd4 by DIK code; empty: none
  bool key_named[0xde] = {};
  std::string joy_names[11];           // 0x0063d14c (1..10)
  bool joy_named[11] = {};
  std::string mouse_names[4];          // 0x0063d18c (1..3)
  bool mouse_named[4] = {};
};

State& state();
void reset_state();                    // back to the power-on globals (tests)
void set_input(platform::Input* di);   // what DirectInputCreateA gives; null: it fails
void set_host(Host* host);

// zin_init.cpp
int32_t start(uint32_t window, uint32_t instance);  // 0x0051b0a0: 0, 1 already, -1 no DirectInput
int32_t stop();                        // 0x0051b170
void reset();                          // 0x0051b1c0: all three devices' states cleared
void suspend();                        // 0x0051b050 (window deactivated)
void resume();                         // 0x0051b080
uint32_t kbd_use();                    // 0x0051b290
uint32_t joy_use();                    // 0x0051b2c0
uint32_t joy_unuse();                  // 0x0051b2f0
uint32_t mouse_use();                  // 0x0051b310
uint32_t mouse_unuse();                // 0x0051b340
void poll(bool callbacks);             // 0x0051b360
uint32_t kbd_not_suspended();          // 0x0051b1f0
uint32_t joy_not_suspended();          // 0x0051b1e0
uint32_t mouse_not_suspended();        // 0x0051b1d0

// zin_kbd.cpp
uint32_t kbd_init();                   // 0x0051a170: 0 ok, 1 failed
uint32_t kbd_close();                    // 0x0051a290
void kbd_reset();                      // 0x0051a300
void kbd_age();                        // 0x0051a550
void kbd_read(bool callbacks);         // 0x0051a5d0
void set_char_callback(CharCallback cb, uintptr_t ctx);  // 0x0051a9a0
uint32_t key_state(int32_t key);       // 0x0051a9b0
int32_t set_key_callback(int32_t key, KeyCallback cb);  // 0x0051a9c0: -1 when one is set
void clear_key_callbacks();            // 0x0051a9e0
uint32_t read_key(bool wait);          // 0x0051aa00: the last key pressed
uint32_t frame_key();                  // ours: State::frame_key
uint32_t key_char(uint32_t key);       // 0x0051ab90: its character, 0 none
int32_t key_by_name(const char* name); // 0x0051aee0: the DIK code, -1

// The mouse.
void mouse_to_cursor();                // 0x0051b3c0
void mouse_size_from_window();         // 0x0051b400
void mouse_set(float fx, float fy);    // 0x0051b440
void mouse_from_cursor(int32_t x, int32_t y);  // 0x0051b4f0
void mouse_centre();                   // 0x0051b560
void mouse_set_screen(int32_t w, int32_t h);   // 0x0051b590
uint32_t mouse_init();                 // 0x0051b5f0
uint32_t mouse_button(int32_t b);      // 0x0051b6e0 (1 left, 2 right, 3 middle)
void mouse_toggle_acquire();           // 0x0051b710
uint32_t mouse_close();                // 0x0051b750
void mouse_set_accel(float x, float y);  // 0x0051b790
MouseInfo* mouse_info();               // 0x0051b7b0
void mouse_poll(bool callbacks);       // 0x0051b7c0
int32_t mouse_read(bool callbacks);    // 0x0051b7d0
void mouse_integrate();                // 0x0051b910
int32_t mouse_copy(MouseInfo* out);    // 0x0051ba60: the last read's result
void mouse_reset();                    // 0x0051ba80
uint32_t set_mode(uint32_t mode);      // 0x0051bae0: the old one
int32_t mouse_wait_button(bool wait);  // 0x0051baf0

// The command map.
CmdMap* map_new(const CmdMap* from);   // 0x0051bb30 (with operator new)
void map_free(CmdMap* m);              // 0x0051bc20
void map_rebuild(CmdMap* m);           // 0x0051bca0
void map_create(CmdMap* m, int32_t count, const char* name);  // 0x0051bd70
void map_release(CmdMap* m);           // 0x0051bdf0
void map_clear(CmdMap* m, bool callbacks);  // 0x0051be60
uint32_t pack(uint32_t key1, uint32_t key2, uint32_t joy, uint32_t mouse);  // 0x0051beb0
uint32_t key1_of(int32_t cmd);         // 0x0051bf00 / 0x0051d200
uint32_t key2_of(int32_t cmd);         // 0x0051bf20 / 0x0051d210
uint32_t joy_of(int32_t cmd);          // 0x0051bf40 / 0x0051d220
uint32_t mouse_of(int32_t cmd);        // 0x0051bf60 / 0x0051d230
int32_t key1_command(int32_t key);     // 0x0051bf80 / 0x0051d240
int32_t key2_command(int32_t key);     // 0x0051bf90 / 0x0051d250
int32_t key_command(int32_t key);      // 0x0051bfa0 / 0x0051bee0's twin
int32_t key_of(int32_t cmd);           // 0x0051bee0 / 0x0051d1f0: key1, else key2
int32_t joy_command(int32_t b);        // 0x0051bfc0 / 0x0051d260
int32_t mouse_command(int32_t b);      // 0x0051bfd0 / 0x0051d270
void bind_key1(uint32_t key, int32_t cmd);   // 0x0051bfe0 / 0x0051d280
void bind_key2(uint32_t key, int32_t cmd);   // 0x0051c040 / 0x0051d290
void bind_joy(uint32_t b, int32_t cmd);      // 0x0051c0b0 / 0x0051d2a0
void bind_mouse(uint32_t b, int32_t cmd);    // 0x0051c120 / 0x0051d2b0
void bind(int32_t cmd, const char* name, uint32_t key1, uint32_t key2, uint32_t joy, uint32_t mouse);  // 0x0051c190 / 0x0051d2c0
void mouse_callbacks();                // 0x0051c200
void joy_callbacks();                  // 0x0051c270
uint32_t set_callback(int32_t cmd, CommandCallback cb);  // 0x0051c2b0 / 0x0051d2f0
uint32_t command(int32_t cmd);         // 0x0051c370 / 0x0051d300: the command's state
uint32_t run_callback(int32_t cmd);    // 0x0051c420 / 0x0051c440
const char* command_name(int32_t cmd, char* out, size_t n);  // 0x0051c450 / 0x0051d310
const char* key_name(uint32_t key, char* out, size_t n);     // 0x0051c480 / 0x0051d330
const char* joy_button_name(int32_t b, char* out, size_t n); // 0x0051c5d0 / 0x0051d350
const char* mouse_button_name(int32_t b, char* out, size_t n);  // 0x0051c600 / 0x0051d370
void init(int32_t count, const char* name);  // 0x0051c630, InitInstance's (0xa7, "DefaultCmdMap")
void make_key_names();                 // 0x0051c6b0
void make_joy_names();                 // 0x0051d020
void make_mouse_names();               // 0x0051d080
void shutdown();                       // 0x0051d0c0
void map_refresh();                    // 0x0051d1c0
void map_reset(bool callbacks);        // 0x0051d1d0
CmdMap* current_map();                 // 0x0051d390
void push_map(CmdMap* m);              // 0x0051d3a0: null pushes a copy of the current one
void pop_map();                        // 0x0051d490

// The joystick.
uint32_t joy_init(uint32_t window);    // 0x0051d520: 1 when one was found
bool joy_acquire();                    // 0x0051d6e0
uint32_t joy_setup(const JoyAxis* config);  // 0x0051d700
JoyAxis* joy_config();                 // 0x0051da90
uint32_t joy_close();                  // 0x0051dbe0
bool joy_present();                    // 0x0051dc10
bool joy_gamepad();                    // ours: the platform's joystick_gamepad (W56)
platform::JoyState* joy_read(bool callbacks);  // 0x0051dc20: null when nothing was read
platform::JoyState* joy_state();       // 0x0051dce0
uint32_t joy_button(int32_t b);        // 0x0051dcf0 (1..10)
int32_t joy_wait_button(bool wait);    // 0x0051dd20
void joy_reset();                      // 0x0051dd60
uint32_t force_feedback();             // 0x0051ddb0

uint32_t report(int32_t hr, const char* file, int32_t line);  // 0x0051ddc0: 1 when hr is 0

}  // namespace zin
