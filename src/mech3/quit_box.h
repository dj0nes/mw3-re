#pragma once
#include <cstdint>

#include "gamez/ztwod/twod.h"
#include "mech3/menu_widgets.h"

// The mission's quit box (docs/menus.md, The mission's quit box): zTwoD's
// message box kept for the whole mission, made inactive by the Core state's
// start (0x00480c70) and shown by its frame only in a multiplayer game
// (0x0048e2d0), as the question "leave the game?". Original file unknown:
// 0x0048dfe0..0x0048e3f0, between the store list (0x0048dbe0) and the
// shell's states (0x0048e400).
namespace mech3 {

// Vtable 0x005c5f70, 0x12688 bytes.
class QuitBox : public ztwod::MessageBox {
 public:
  QuitBox(Engine& e, const char* file, const char* name);  // 0x0048dfe0
  // +0x08 0x0048e180 / 0x0048e1a0: the members' destructors only.
  void answer_ok() override;      // +0x10  0x0048e390: the network game left
  void answer_cancel() override;  // +0x14  0x0048e3d0
  // +0x2c 0x0048e3f0 jumps to the base's.
  void prompt();                  // 0x0048e2d0
  void close();                   // 0x0048e320
  // Ours (playtest W46, with bug fixes on): while shown, Y leaves (OK) and N
  // closes (Cancel); called each frame before the box's update.
  void keys();

  // Ours (W46): Y's and N's commands (key1, key2), lifted while shown.
  int32_t held_cmds[2][2] = {};

  SlideButton unused;             // +0x12494  built (id -1), never bound or added
  int32_t mouse_cmd = 0;          // +0x12680  the command on mouse button 1, unbound while shown
  uint32_t mouse_button = 0;      // +0x12684  its button, bound back by the close

 private:
  Engine& e_;
};

}  // namespace mech3
