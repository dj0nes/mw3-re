#include "mech3/controls_cfg.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

#include "crt/msvc_string.h"
#include "gamez/zinput/zinput.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_msg.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zutil/zutl_reg.h"

namespace mech3 {

namespace {
AxisTable g_axes;
ControlsSetup g_setup;
ControlsSetupCalls g_calls;

const char* message(int32_t id) {
  if (g_calls.message) return g_calls.message(id);
  const char* t = zutl::messages().get_text ? zutl::messages().get_text(id) : nullptr;
  return t ? t : "";
}
int32_t bind(int32_t cmd, const char* name, uint32_t k1, uint32_t k2, uint32_t joy, uint32_t mouse) {
  if (g_calls.bind) return g_calls.bind(cmd, name, k1, k2, joy, mouse);
  zin::bind(cmd, name, k1, k2, joy, mouse);
  return cmd;
}
uint32_t binding(int which, int32_t cmd) {
  if (g_calls.binding) return g_calls.binding(which, cmd);
  switch (which) {
    case 0: return zin::key1_of(cmd);
    case 1: return zin::key2_of(cmd);
    case 2: return zin::joy_of(cmd);
    default: return zin::mouse_of(cmd);
  }
}

// The defaults 0x00411150 binds, group by group (each group's title a message).
struct Default {
  int32_t cmd, message;
  uint32_t key1, key2, joy, mouse;
  const char* name;
  int32_t axis = -1;  // an axis's key: the command entry's +0x08 and +0x10
};
struct DefaultGroup {
  int32_t title;
  std::vector<Default> commands;
};
const DefaultGroup kDefaults[] = {
    {0x704, {  // Movement Controls
        {0x18, 0x800, 0xc8, 0x0, 0, 0, "CMD_THROTTLE_UP", 0},
        {0x19, 0x803, 0xd0, 0x0, 0, 0, "CMD_THROTTLE_DN", 0},
        {0x2, 0x801, 0xcb, 0xd2, 0, 0, "CMD_TURN_LEFT", 3},
        {0x3, 0x802, 0xcd, 0xc9, 0, 0, "CMD_TURN_RIGHT", 3},
        {0x8, 0x807, 0x33, 0x0, 0, 0, "CMD_TWIST_LEFT", 1},
        {0x9, 0x808, 0x34, 0x0, 0, 0, "CMD_TWIST_RIGHT", 1},
        {0x4, 0x804, 0xc, 0x0, 0, 0, "CMD_PITCH_UP", 2},
        {0x5, 0x805, 0xd, 0x0, 0, 0, "CMD_PITCH_DN", 2},
        {0x6, 0x806, 0x435, 0x0, 0, 0, "CMD_CENTER_TORSO"},
        {0x7, 0x8a7, 0x35, 0x0, 8, 0, "CMD_CENTER_LEGS"},
        {0x1a, 0x813, 0xe, 0x0, 10, 0, "CMD_THROTTLE_REV"},
        {0x1b, 0x809, 0x2, 0x0, 0, 0, "CMD_THROTTLE1"},
        {0x1c, 0x80a, 0x3, 0x0, 0, 0, "CMD_THROTTLE2"},
        {0x1d, 0x80b, 0x4, 0x0, 0, 0, "CMD_THROTTLE3"},
        {0x1e, 0x80c, 0x5, 0x0, 0, 0, "CMD_THROTTLE4"},
        {0x1f, 0x80d, 0x6, 0x0, 0, 0, "CMD_THROTTLE5"},
        {0x20, 0x80e, 0x7, 0x0, 0, 0, "CMD_THROTTLE6"},
        {0x21, 0x80f, 0x8, 0x0, 0, 0, "CMD_THROTTLE7"},
        {0x22, 0x810, 0x9, 0x0, 0, 0, "CMD_THROTTLE8"},
        {0x23, 0x811, 0xa, 0x0, 0, 0, "CMD_THROTTLE9"},
        {0x24, 0x812, 0xb, 0x0, 0, 0, "CMD_THROTTLE0"},
        {0xc, 0x862, 0x24, 0x0, 7, 0, "CMD_JJ_UP"},
        {0xd, 0x863, 0xc7, 0x0, 0, 0, "CMD_JJ_FORWARD"},
        {0xe, 0x864, 0xcf, 0x0, 0, 0, "CMD_JJ_BACKWARD"},
        {0xf, 0x865, 0xd3, 0x0, 0, 0, "CMD_JJ_LEFT"},
        {0x10, 0x866, 0xd1, 0x0, 0, 0, "CMD_JJ_RIGHT"},
        {0x11, 0x8a8, 0xd2, 0x0, 0, 0, "CMD_JJ_ROTLEFT"},
        {0x12, 0x8a9, 0xc9, 0x0, 0, 0, "CMD_JJ_ROTRIGHT"},
        {0x44, 0x872, 0x22, 0x0, 0, 0, "CMD_GETUP"},
        {0x13, 0x867, 0x2e, 0x0, 0, 0, "CMD_CROUCH"},
        {0x3f, 0x870, 0x1f, 0x0, 0, 0, "CMD_SHUTDOWN"},
        {0x6e, 0x87f, 0x2f, 0x0, 0, 0, "CMD_MASC"},
    }},
    {0x706, {  // Weapon Controls
        {0x3d, 0x857, 0x39, 0x0, 1, 1, "CMD_TRIGGER1"},
        {0x2a, 0x81b, 0x1b, 0x0, 4, 0, "CMD_WEPGRP_NEXT"},
        {0x2b, 0x81c, 0x1a, 0x0, 0, 0, "CMD_WEPGRP_PREV"},
        {0x26, 0x817, 0x21a, 0x0, 0, 0, "CMD_WEP_PREV"},
        {0x25, 0x816, 0x21b, 0x0, 0, 0, "CMD_WEP_NEXT"},
        {0x27, 0x876, 0x1c, 0x0, 0, 0, "CMD_NEXT_WEAPON"},
        {0x2c, 0x821, 0x3b, 0x0, 0, 0, "CMD_WEPGRP_1"},
        {0x2d, 0x822, 0x3c, 0x0, 0, 0, "CMD_WEPGRP_2"},
        {0x2e, 0x823, 0x3d, 0x0, 0, 0, "CMD_WEPGRP_3"},
        {0x2f, 0x824, 0x3e, 0x0, 0, 0, "CMD_WEPGRP_4"},
        {0x30, 0x825, 0x3f, 0x0, 0, 0, "CMD_WEPGRP_5"},
        {0x31, 0x826, 0x43b, 0x0, 0, 0, "CMD_MOVETOWG_1"},
        {0x32, 0x827, 0x43c, 0x0, 0, 0, "CMD_MOVETOWG_2"},
        {0x33, 0x828, 0x43d, 0x0, 0, 0, "CMD_MOVETOWG_3"},
        {0x34, 0x829, 0x43e, 0x0, 0, 0, "CMD_MOVETOWG_4"},
        {0x35, 0x82a, 0x43f, 0x0, 0, 0, "CMD_MOVETOWG_5"},
        {0x28, 0x81a, 0x2b, 0x0, 5, 0, "CMD_FIREMODE"},
        {0x39, 0x818, 0x9c, 0x0, 6, 0, "CMD_ALPHASTRIKE"},
        {0x41, 0x871, 0x21, 0x0, 0, 0, "CMD_COOLANTFLUSH"},
        {0x40, 0x87b, 0x18, 0x0, 0, 0, "CMD_SHUT_OVER"},
        {0x3b, 0x896, 0x425, 0x0, 0, 0, "CMD_JETTISON_AMMO"},
    }},
    {0x708, {  // View Controls
        {0x61, 0x880, 0x2d, 0x0, 0, 0, "CMD_CYCLE_CAM"},
        {0x62, 0x868, 0x14b, 0x0, 0, 0, "CMD_LOOK_LEFT"},
        {0x63, 0x869, 0x14d, 0x0, 0, 0, "CMD_LOOK_RIGHT"},
        {0x64, 0x86a, 0x150, 0x0, 0, 0, "CMD_LOOK_UP"},
        {0x65, 0x86b, 0x148, 0x0, 0, 0, "CMD_LOOK_DOWN"},
        {0x67, 0x86d, 0x153, 0x0, 0, 0, "CMD_LOOK_REAR"},
        {0x66, 0x86c, 0x14c, 0x0, 0, 0, "CMD_LOOK_FRONT"},
        {0x6c, 0x815, 0x42f, 0x0, 0, 0, "CMD_VMODE_TOGGLE"},
    }},
    {0x710, {  // Targeting Controls
        {0x46, 0x85a, 0x12, 0x0, 3, 0, "CMD_TARGET_NEXT_E"},
        {0x4d, 0x892, 0x412, 0x0, 0, 0, "CMD_TARGET_PREV_E"},
        {0x4c, 0x885, 0x212, 0x0, 0, 0, "CMD_TARGET_CLOSE_E"},
        {0x47, 0x859, 0x11, 0x0, 0, 0, "CMD_TARGET_NEXT_F"},
        {0x4e, 0x891, 0x411, 0x0, 0, 0, "CMD_TARGET_PREV_F"},
        {0x4b, 0x886, 0x211, 0x0, 0, 0, "CMD_TARGET_CLOSE_F"},
        {0x48, 0x85b, 0x10, 0x0, 0, 0, "CMD_TARGET_CURSOR"},
        {0x49, 0x874, 0x14, 0x0, 0, 0, "CMD_TARGET_NONE"},
        {0x59, 0x888, 0x48, 0x0, 0, 0, "CMD_TARGET_HEAD"},
        {0x5d, 0x88c, 0x47, 0x0, 0, 0, "CMD_TARGET_LARM"},
        {0x5e, 0x88d, 0x49, 0x0, 0, 0, "CMD_TARGET_RARM"},
        {0x5a, 0x889, 0x4c, 0x0, 0, 0, "CMD_TARGET_TORSO"},
        {0x5b, 0x88a, 0x4b, 0x0, 0, 0, "CMD_TARGET_LTORSO"},
        {0x5c, 0x88b, 0x4d, 0x0, 0, 0, "CMD_TARGET_RTORSO"},
        {0x5f, 0x88e, 0x4f, 0x0, 0, 0, "CMD_TARGET_LLEG"},
        {0x60, 0x88f, 0x51, 0x0, 0, 0, "CMD_TARGET_RLEG"},
    }},
    {0x705, {  // Cockpit Controls
        {0x14, 0x873, 0x2c, 0x0, 0, 0, "CMD_RETICULE_ZOOM"},
        {0x15, 0x89f, 0x52, 0x0, 2, 0, "CMD_ZOOM_LOCK"},
        {0x16, 0x8a3, 0x252, 0x0, 0, 0, "CMD_TOGGLE_TMODE"},
        {0x17, 0x8a4, 0x26, 0x0, 0, 2, "CMD_FREELOOK"},
        {0x74, 0x890, 0x20, 0x0, 0, 0, "CMD_DAMAGE_TOGGLE"},
        {0x75, 0x8a6, 0x220, 0x0, 0, 0, "CMD_EDAMAGE_TOGGLE"},
        {0x68, 0x85c, 0x13, 0x0, 0, 0, "CMD_RADAR_TOGGLE"},
        {0x69, 0x85d, 0x113, 0x0, 0, 0, "CMD_RADAR_RANGE"},
        {0xa5, 0x883, 0x413, 0x0, 0, 0, "CMD_RADAR_MAP_TOGGLE"},
        {0x6b, 0x855, 0x23, 0x0, 0, 0, "CMD_HUD_TOGGLE"},
        {0x3a, 0x882, 0x1e, 0x0, 0, 0, "CMD_AMS_TOGGLE"},
    }},
    {0x707, {  // Communications Controls
        {0x54, 0x893, 0x40, 0x0, 0, 0, "CMD_DIRECT_ATTACKT"},
        {0x55, 0x894, 0x41, 0x0, 0, 0, "CMD_DIRECT_DEFENDT"},
        {0x52, 0x85e, 0x42, 0x0, 0, 0, "CMD_DIRECT_FOLLOW"},
        {0x51, 0x85f, 0x43, 0x0, 0, 0, "CMD_DIRECT_PAUSE"},
        {0x56, 0x895, 0x44, 0x0, 0, 0, "CMD_DIRECT_REFITT"},
        {0x4f, 0x860, 0x57, 0x0, 0, 0, "CMD_DIRECT_POCKET"},
        {0x57, 0x897, 0x457, 0x0, 0, 0, "CMD_DIRECT_AWAITCMD"},
        {0x58, 0x898, 0x257, 0x0, 0, 0, "CMD_DIRECT_AWAITCMDCANCEL"},
        {0x73, 0x87d, 0x32, 0x0, 0, 0, "CMD_MCB_COMMAND"},
        {0x36, 0x879, 0x31, 0x0, 0, 0, "CMD_NAVPT_NEXT"},
        {0x37, 0x87a, 0x431, 0x0, 0, 0, "CMD_NAVPT_PREV"},
        {0x38, 0x89b, 0x231, 0x0, 0, 0, "CMD_NAVPT_NEAR"},
        {0x76, 0x899, 0x4a, 0x0, 0, 0, "CMD_CHAT_TEAM"},
        {0x77, 0x89a, 0x37, 0x0, 0, 0, "CMD_CHAT_ALL"},
    }},
    {0x709, {  // Mission Controls
        {0x43, 0x878, 0x58, 0x0, 0, 0, "CMD_OBJECTIVES"},
        {0x71, 0x87c, 0x210, 0x0, 0, 0, "CMD_MISSION_COMPLETE"},
        {0x70, 0x8a2, 0x29, 0x0, 0, 0, "CMD_MESSAGE_TOGGLE"},
    }},
};
constexpr int32_t kQuickExit = 1, kQuickExitName = 0x8a1, kAltF4 = 0x13e;  // no group, no CMD_ name

const char* const kAxisNames[4] = {"Throttle", "Twist", "Pitch", "LR"};  // 0x005e1e80 .. 0x005e1e6c

// The joystick's defaults, which 0x004120a0 recognises in a file.
bool stock_axis(int i, int32_t src, float slope, float intercept, float deadzone) {
  switch (i) {
    case 0: return src == 3 && slope == -0.5f && intercept == 0.5f && deadzone == 0.05f;
    case 1: return src == 6 && slope == -1.0f && intercept == 0.0f;
    case 2: return src == 2 && slope == 1.0f && intercept == 0.0f;
    default: return src == 1 && slope == -1.0f && intercept == 0.0f;
  }
}

// An entry of KEYS or AXES that the original reads as (NAME ...): its name.
// FIX (bug): anything else is skipped; the original read it as such a list
// and crashed (docs/fixes.md).
const char* entry_name(const zrdr::Node* e) {
  if (e->type != zrdr::List || e->list[0].i < 2 || e->list[1].type != zrdr::String) return nullptr;
  return e->list[1].s;
}
}  // namespace

AxisTable& axis_table() { return g_axes; }
ControlsSetup& controls_setup() { return g_setup; }
ControlsSetupCalls& controls_setup_calls() { return g_calls; }

// Mech3.exe 1.2 @ 0x0040daf0
void AxisTable::set3(int i, int32_t source, float scale, float offset) {
  axes[i].source = source;
  axes[i].scale = scale;
  axes[i].offset = offset;
}

// Mech3.exe 1.2 @ 0x0040db10
void AxisTable::set_source(int i, int32_t source) {
  axes[i].source = source;
  ready = 0;
}

// Mech3.exe 1.2 @ 0x0040db30
void AxisTable::set(int i, int32_t source, float scale, float offset, float deadzone) {
  axes[i] = {source, scale, offset, deadzone};
  ready = 0;
}

// Mech3.exe 1.2 @ 0x00410a80
void controls_free_groups() { g_setup.groups.clear(); }

// Mech3.exe 1.2 @ 0x00410b70
int32_t controls_new_group(int32_t title) {
  const int32_t index = static_cast<int32_t>(g_setup.groups.size());
  g_setup.groups.push_back({message(title), {}});
  return index;
}

// Mech3.exe 1.2 @ 0x00410dc0
void controls_group_add(int32_t group, int32_t cmd) { g_setup.groups[group].commands.push_back(cmd); }

// Mech3.exe 1.2 @ 0x004110d0
void controls_add(int32_t cmd, int32_t msg, uint32_t key1, uint32_t key2, uint32_t joy, uint32_t mouse,
                  const char* name) {
  controls_group_add(g_setup.group, bind(cmd, message(msg), key1, key2, joy, mouse));
  g_setup.commands[cmd].message = msg;
  g_setup.commands[cmd].name = name;
  g_setup.names.emplace(name, 0).first->second = cmd;
}

// Mech3.exe 1.2 @ 0x00411150. The name map and the command entries are kept
// from an earlier call; the groups are made again.
int32_t controls_set_defaults() {
  controls_free_groups();
  for (const DefaultGroup& g : kDefaults) {
    g_setup.group = controls_new_group(g.title);
    for (const Default& d : g.commands) {
      controls_add(d.cmd, d.message, d.key1, d.key2, d.joy, d.mouse, d.name);
      if (d.axis >= 0) {
        g_setup.commands[d.cmd].axis_key = 1;
        g_setup.commands[d.cmd].axis = d.axis;
      }
    }
  }
  bind(kQuickExit, message(kQuickExitName), kAltF4, 0, 0, 0);
  return 1;
}

// Mech3.exe 1.2 @ 0x00410fe0. FIX (bug): the map keeps its own copy of the
// name; the original kept the pointer, which for a name read from a file
// dangles once the file is freed (docs/fixes.md).
int32_t controls_command(const char* name) { return g_setup.names.emplace(name, 0).first->second; }

// Mech3.exe 1.2 @ 0x00410f80
int32_t controls_group_count() { return static_cast<int32_t>(g_setup.groups.size()); }
// Mech3.exe 1.2 @ 0x00410fa0
const char* controls_group_title(int32_t group) { return g_setup.groups[group].title.c_str(); }
// Mech3.exe 1.2 @ 0x00410fb0
int32_t controls_group_size(int32_t group) { return static_cast<int32_t>(g_setup.groups[group].commands.size()); }
// Mech3.exe 1.2 @ 0x00410fd0
int32_t controls_group_command(int32_t group, int32_t i) { return g_setup.groups[group].commands[i]; }

// Mech3.exe 1.2 @ 0x00412520: "0x.." in hex, a leading digit in decimal,
// anything else (an int node too) 0.
uint32_t controls_read_binding(zrdr::Node* entry, const char* key) {
  const char* s = zrdr::get_string(entry, key);
  if (!s) return 0;
  if (std::strncmp(s, "0x", 2) == 0) {
    uint32_t v = 0;
    return crt::msvc_scan_hex(s, &v) ? v : 0;
  }
  if (*s >= '0' && *s <= '9') {
    int32_t v = 0;
    return crt::msvc_scan_dec(s, &v) ? static_cast<uint32_t>(v) : 0;
  }
  return 0;
}

const char* axis_name(int32_t axis) { return axis >= 0 && axis < 4 ? kAxisNames[axis] : ""; }

// Mech3.exe 1.2 @ 0x004120a0. KEYS replaces every binding (zInput's map is
// cleared first; a command the file leaves out ends up unbound, Alt-F4's
// Quick Exit too). AXES clears the table's sources, then sets each axis it
// names; an entry with neither source keeps the one before it (for the first,
// the list's count). The table is left not ready; it is ready again at start
// only when the file's four axes are the joystick's defaults.
void controls_load(const char* path) {
  bool stock[4] = {};
  zrdr::Node* root = zrdr::load(path);
  if (!root) return;
  zrdr::Node* keys = zrdr::find(root, "KEYS");
  if (keys && keys->type == zrdr::List) {  // FIX (bug): KEYS not a list was read as one
    const int32_t count = keys->list[0].i;
    if (g_calls.map_reset) g_calls.map_reset(false);
    else zin::map_reset(false);
    for (int32_t i = 1; i < count; ++i) {
      zrdr::Node* e = &keys->list[i];
      const char* name = entry_name(e);
      if (!name) continue;
      const int32_t cmd = controls_command(name);
      const uint32_t keya = controls_read_binding(e, "keya");
      const uint32_t keyb = controls_read_binding(e, "keyb");
      const uint32_t mouse = controls_read_binding(e, "mousebtn");
      const uint32_t joy = controls_read_binding(e, "joybtn");
      bind(cmd, nullptr, keya, keyb, joy, mouse);
    }
    if (g_calls.map_refresh) g_calls.map_refresh();
    else zin::map_refresh();
  }
  zrdr::Node* axes = zrdr::find(root, "AXES");
  if (axes && axes->type == zrdr::List) {  // FIX (bug), as KEYS
    AxisTable& t = g_axes;
    for (AxisBinding& a : t.axes) a.source = 0;
    t.ready = 0;
    const int32_t count = axes->list[0].i;
    int32_t src = count;
    for (int32_t i = 1; i < count; ++i) {
      zrdr::Node* e = &axes->list[i];
      const char* name = entry_name(e);
      if (!name) continue;
      int32_t axis = 0;
      while (axis < 4 && std::strcmp(kAxisNames[axis], name) != 0) ++axis;
      float slope = 1.0f, intercept = 0.0f, deadzone = 0.0f;
      if (const char* s = zrdr::get_string(e, "joystick")) {
        src = 0;
        for (int32_t k = 1; k < 9; ++k)
          if (std::strcmp(axis_source_name(k), s) == 0) {
            src = k;
            break;
          }
      } else if (const char* m = zrdr::get_string(e, "mouse")) {
        src = 0;
        for (int32_t k = 9; k <= 11; ++k)
          if (std::strcmp(axis_source_name(k), m) == 0) {
            src = k;
            break;
          }
      }
      zrdr::get_float(e, "slope", &slope);
      zrdr::get_float(e, "intercept", &intercept);
      zrdr::get_float(e, "deadzone", &deadzone);
      // FIX (bug): an axis of another name was written as a fifth entry,
      // over the table's flags and the groups' list (docs/fixes.md).
      if (axis < 4) {
        if (stock_axis(axis, src, slope, intercept, deadzone)) stock[axis] = true;
        t.axes[axis] = {src, slope, intercept, deadzone};
      }
      t.ready = 0;
    }
    t.ready = 0;
    t.stock = stock[0] && stock[1] && stock[2] && stock[3];
  }
  zrdr::free_node(root);
}

// Mech3.exe 1.2 @ 0x00411e60. KEYS lists every name in the map in strcmp
// order with the bindings it has; AXES the bound axes. The floats as %f.
void controls_save(const char* path) {
  std::FILE* f = zutl::open(path, "w");
  if (!f) return;
  std::fprintf(f, "(\n");
  std::fprintf(f, "\tKEYS (\n");
  for (const auto& n : g_setup.names) {
    const int32_t cmd = controls_command(n.first.c_str());
    std::fprintf(f, "\t\t(%s", n.first.c_str());
    if (uint32_t v = binding(0, cmd)) std::fprintf(f, "\tkeya(0x%x)", v);
    if (uint32_t v = binding(1, cmd)) std::fprintf(f, "\tkeyb(0x%x)", v);
    if (uint32_t v = binding(3, cmd)) std::fprintf(f, "\tmousebtn(0x%x)", v);
    if (uint32_t v = binding(2, cmd)) std::fprintf(f, "\tjoybtn(0x%x)", v);
    std::fprintf(f, ")\n");
  }
  std::fprintf(f, "\t)\n");
  std::fprintf(f, "\tAXES (\n");
  for (int32_t i = 0; i < 4; ++i) {
    const AxisBinding& a = g_axes.axes[i];
    if (a.source == 0) continue;
    std::fprintf(f, "\t\t(%s", kAxisNames[i]);
    std::fprintf(f, "\t%s(%s)", a.source >= 9 ? "mouse" : "joystick", axis_source_name(a.source));
    std::fprintf(f, "\tslope(%f)", static_cast<double>(a.scale));
    std::fprintf(f, "\tintercept(%f)", static_cast<double>(a.offset));
    std::fprintf(f, "\tdeadzone(%f)", static_cast<double>(a.deadzone));
    std::fprintf(f, ")\n");
  }
  std::fprintf(f, "\t)\n");
  std::fprintf(f, ")\n");
  std::fclose(f);
}

// Ours, playtest W40: an older build saved the map with a quit box's unbind
// in it (fixes.md), so the mouse never fired again. Only that damage is
// undone: fire moved to another button, or button 1 given to another
// command, stays as the player set it.
bool controls_repair_mouse_fire() {  // after the tables' rebuild (the hook's)
  constexpr int32_t kTrigger1 = 0x3d;
  const zin::CmdMap* m = zin::state().map;
  if (!m || m->count < 0xa7 || zin::mouse_of(kTrigger1) != 0) return false;
  for (int32_t c = 1; c < 0xa7; ++c)
    if (zin::mouse_of(c) == 1) return false;
  zin::bind_mouse(1, kTrigger1);
  std::printf("mw3: controls: mouse button 1 was bound to nothing, put back on fire (TRIGGER1)\n");
  return true;
}

// Ours, FIX (bug), playtest W40: the map is the settings' own block, so the
// original saved whatever it held at exit, a quit box's unbind too. The map
// saved is the one the load and the controls menus left (input.md, section 3).
// The same for the mouse's MouseAccelX/Y (Owen, 2026-11-02): kept at the load
// and at the options' ACCEPT, so a value set for a moment (the menus' 1.3 is
// zInput's working copy, 0x00481fc0) never reaches the disk.
namespace {
struct Kept {
  const char* name;
  std::vector<uint8_t> bytes;
};
Kept g_kept[] = {{"DefaultCmdMap", {}}, {"MouseAccelX", {}}, {"MouseAccelY", {}}};
void keep(Kept& k) {
  const zreg::Entry* e = zreg::find(k.name);
  if (e && e->data) k.bytes.assign(e->data, e->data + e->size);
  else k.bytes.clear();
}
}  // namespace

void controls_keep_saved() { keep(g_kept[0]); }

void settings_keep_mouse() { keep(g_kept[1]), keep(g_kept[2]); }

int32_t controls_write_settings() {
  std::vector<std::vector<uint8_t>> live(std::size(g_kept));
  for (size_t i = 0; i < std::size(g_kept); ++i) {
    zreg::Entry* e = zreg::find(g_kept[i].name);
    if (!e || !e->data || g_kept[i].bytes.size() != e->size) continue;  // never kept: the live value, as the original
    live[i].assign(e->data, e->data + e->size);
    std::memcpy(e->data, g_kept[i].bytes.data(), e->size);
  }
  const int32_t r = zreg::write_all();
  for (size_t i = 0; i < std::size(g_kept); ++i)
    if (zreg::Entry* e = zreg::find(g_kept[i].name); e && !live[i].empty()) std::memcpy(e->data, live[i].data(), e->size);
  return r;
}

}  // namespace mech3
