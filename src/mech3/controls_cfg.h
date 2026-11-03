#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// mech3's controls setup (docs/controls.md, Setup): the commands' names and
// groups, the default bindings, the axis table and the controls file
// (keys\<name>.zrd). Original file unknown: the code sits between
// briefing.cpp and mech.cpp, after the options (named here after them).
namespace zrdr { struct Node; }

namespace mech3 {

// One of the axis table's bindings (16 bytes): a source, 0 unbound.
struct AxisBinding {
  int32_t source = 0;          // 1..6 the stick's axes, 7, 8 its sliders, 9..11 the mouse's
  float scale = 0, offset = 0, deadzone = 0;
  float value(const void* joy, const void* mouse) const;  // 0x0042fe10 (mech_control.cpp)
  float delta(const void* joy, const void* mouse) const;  // 0x0042fe70
};

// The axis table at 0x006040d0 and its two flags. The static constructor
// 0x00410950 starts it with the joystick's defaults, ready.
struct AxisTable {
  AxisBinding axes[4] = {{3, -0.5f, 0.5f, 0.05f}, {6, -1.0f, 0, 0}, {2, 1.0f, 0, 0}, {1, -1.0f, 0, 0}};  // throttle, twist, pitch, turn
  uint8_t ready = 1;   // 0x00604110  the options and the controls' modes may rewrite it
  uint8_t stock = 0;   // 0x00604111  the controls file's axes were the joystick's defaults: ready again at start (0x00480570)
  void set3(int i, int32_t source, float scale, float offset);              // 0x0040daf0 (on the entry)
  void set_source(int i, int32_t source);                                  // 0x0040db10, not ready
  void set(int i, int32_t source, float scale, float offset, float deadzone);  // 0x0040db30, not ready
};
AxisTable& axis_table();

// A command's entry at 0x00604128 (0x14 bytes, 0xa7 of them).
struct CommandEntry {
  int32_t message = 0;         // +0x00  its name's message id
  const char* name = nullptr;  // +0x04  CMD_..., the controls file's name for it
  uint8_t axis_key = 0;        // +0x08  a key that also has an axis (throttle, turn, twist, pitch)
  int32_t axis = 0;            // +0x10  that axis's entry in the axis table
};

// A group of the controls screen (0x14 bytes, 0x00410b70).
struct ControlGroup {
  std::string title;              // +0x00  a CString, the group's message
  std::vector<int32_t> commands;  // +0x08
};

struct ControlsSetup {
  std::map<std::string, int32_t> names;  // 0x006040b8  CMD_ name -> command, a std::map in strcmp order
  int32_t group = 0;                     // 0x006040c8  the group commands are added to
  std::vector<ControlGroup> groups;      // 0x0060411c
  CommandEntry commands[0xa7];           // 0x00604128
};
ControlsSetup& controls_setup();

// What the setup calls outside: zInput's command map (unset: its port) and
// the messages (unset: zutl::messages()).
struct ControlsSetupCalls {
  const char* (*message)(int32_t id) = nullptr;  // 0x00576040
  int32_t (*bind)(int32_t cmd, const char* name, uint32_t key1, uint32_t key2, uint32_t joy,
                  uint32_t mouse) = nullptr;     // 0x0051d2c0, returns cmd
  void (*map_reset)(bool callbacks) = nullptr;   // 0x0051d1d0
  void (*map_refresh)() = nullptr;               // 0x0051d1c0
  uint32_t (*binding)(int which, int32_t cmd) = nullptr;  // 0 key1, 1 key2, 2 joystick, 3 mouse: 0x0051d200..0x0051d230
};
ControlsSetupCalls& controls_setup_calls();

void controls_free_groups();                        // 0x00410a80
int32_t controls_new_group(int32_t message);        // 0x00410b70: the new group's index
void controls_group_add(int32_t group, int32_t cmd);  // 0x00410dc0
// 0x004110d0: binds a command's defaults, adds it to the current group and
// names it.
void controls_add(int32_t cmd, int32_t message, uint32_t key1, uint32_t key2, uint32_t joy, uint32_t mouse,
                  const char* name);
int32_t controls_set_defaults();                    // 0x00411150: returns 1
// 0x00410fe0: the command of a CMD_ name. An unknown name is added as
// command 0 (std::map's operator[]).
int32_t controls_command(const char* name);
int32_t controls_group_count();                     // 0x00410f80
const char* controls_group_title(int32_t group);    // 0x00410fa0
int32_t controls_group_size(int32_t group);         // 0x00410fb0
int32_t controls_group_command(int32_t group, int32_t i);  // 0x00410fd0

// The controls file (0x004120a0 reads, 0x00411e60 writes), a text .zrd:
//   ( KEYS ( (CMD_x keya(0x..) keyb(0x..) mousebtn(0x..) joybtn(0x..)) ... )
//     AXES ( (Throttle joystick(Z) slope(..) intercept(..) deadzone(..)) ... ) )
void controls_load(const char* path);
void controls_save(const char* path);
uint32_t controls_read_binding(zrdr::Node* entry, const char* key);  // 0x00412520
// Ours (playtest W40): a saved map with no mouse button on TRIGGER1 and
// mouse button 1 on nothing gets it back on TRIGGER1; true when it did.
bool controls_repair_mouse_fire();
// Ours (W40, FIX (bug)): the map the settings save is the one kept here (at
// the load and when the controls menus set it), never the live one.
void controls_keep_saved();
// Ours (Owen, 2026-11-02): MouseAccelX/Y kept the same way, at the load and
// at the options screens' ACCEPT (their apply).
void settings_keep_mouse();
int32_t controls_write_settings();  // zreg::write_all with the kept map and mouse values
const char* axis_source_name(int32_t source);       // 0x004300e0: X, Y, Z, Rx, Ry, Rz, Slider0, Slider1, X, Y, Z
const char* axis_name(int32_t axis);                // Throttle, Twist, Pitch, LR (the turn)

}  // namespace mech3
