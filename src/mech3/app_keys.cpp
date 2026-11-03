#include "mech3/app_keys.h"

#include "gamez/zinput/zinput.h"
#include "mech3/cheats.h"
#include "mech3/game_editor.h"

namespace mech3 {
namespace {
AppKeyCalls g_calls;
void debug_7e(int32_t) {
  if (g_calls.debug_7e) g_calls.debug_7e();
}
}  // namespace

AppKeyCalls& app_key_calls() { return g_calls; }

// Mech3.exe 1.2 @ 0x0040db60
void register_app_keys() {
  zin::set_callback(0x61, app_key);   // CYCLE_CAM (X)
  zin::set_callback(0x70, app_key);   // MESSAGE_TOGGLE (`)
  zin::set_callback(0x7e, debug_7e);  // 0x004abbc0
  zin::set_callback(0x6c, app_key);   // VMODE_TOGGLE (Shift+V)
  register_cheat_key();               // ours: the cheat box on Shift+Enter (cheats.h)
  if (g_calls.debug_camera) {  // ours: the game editor on Shift+F12 (debug only)
    zin::bind(kEditorCommand, nullptr, kEditorKey, 0, 0, 0);
    zin::set_callback(kEditorCommand, app_key);
  }
}

// Mech3.exe 1.2 @ 0x0040dba0
void app_key(int32_t cmd) {
  switch (cmd) {
    case 0x61:
      if (g_calls.cycle_camera) g_calls.cycle_camera();
      break;
    case 0x6c:
      if (g_calls.vmode_cycle) g_calls.vmode_cycle();
      break;
    case 0x70:
      if (g_calls.message_toggle) g_calls.message_toggle();
      break;
    case 0x9a:  // 0x0040e430; ours: again, the exit (docs/editor.md S4)
      editor_key(cmd);
      break;
    default:
      break;
  }
}

}  // namespace mech3
