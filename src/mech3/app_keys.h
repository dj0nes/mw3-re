#pragma once
#include <cstdint>

// The app's own keys (between briefing.cpp and mech.cpp, after the options;
// the file is not known): the commands whose keys reach the app, not the
// mech (docs/keybindings.md).
namespace mech3 {

// Where the app's keys go; null calls are skipped.
struct AppKeyCalls {
  void (*cycle_camera)() = nullptr;    // 0x0040aed0 on the player's view [0x005f4c8c]
  void (*vmode_cycle)() = nullptr;     // 0x004ab910: the next screen mode, saved as InGameVMode
  void (*message_toggle)() = nullptr;  // 0x0040f070: the message panel
  void (*debug_7e)() = nullptr;        // 0x004abbc0 (command 0x7e, which no key reaches)
  // Ours: --debug-camera / debugcamera=1: Shift+F12 to command 0x9a, the
  // game editor's (docs/editor.md S4; the exe binds no key to it).
  bool debug_camera = false;
};
AppKeyCalls& app_key_calls();

// Mech3.exe 1.2 @ 0x0040db60 (from the mission's load): CYCLE_CAM (0x61),
// MESSAGE_TOGGLE (0x70) and VMODE_TOGGLE (0x6c) to 0x0040dba0, 0x7e to 0x004abbc0.
void register_app_keys();

// Mech3.exe 1.2 @ 0x0040dba0 (ECX the command): the cases of the commands it
// is bound to. Its other cases (0x6d, 0x82..0xa4: debug commands no key or
// file can bind) are not ported.
void app_key(int32_t cmd);

}  // namespace mech3
