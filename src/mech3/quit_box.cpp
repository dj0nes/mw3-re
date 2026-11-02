#include "mech3/quit_box.h"

#include <cstdio>

#include "gamez/zutil/zfix.h"
#include "mech3/app_hooks.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
constexpr Handle kLobbyState = 0x609848;  // a second Multi-Exit State (0x00470650's): the mission ended, the main menu
constexpr float kFadeSeconds = 2.0f;      // [0x005e7274]
constexpr uint32_t kMsgQuitGame = 0xde1;  // the message
constexpr uint32_t kMsgTitle = 7;         // "MechWarrior 3"
}  // namespace

// Mech3.exe 1.2 @ 0x0048dfe0: OK and Cancel shown, the other buttons and the
// backdrop hidden, the texts by message id, the cursor's capture off.
QuitBox::QuitBox(Engine& e, const char* file, const char* name)
    : MessageBox(file, name, true), unused(e, -1), e_(e) {
  buttons[0].show_for(-1.0f);
  buttons[1].show_for(-1.0f);
  for (int i = 2; i < 8; ++i) buttons[i].hide();
  backdrop.hide();
  message.set_text_id(kMsgQuitGame);
  title.set_text_id(kMsgTitle);
  if (cursor) static_cast<ztwod::Cursor*>(cursor)->set_capture(0);
}

// Mech3.exe 1.2 @ 0x0048e390
void QuitBox::answer_ok() {
  e_.network_leave();
  e_.network_set(0);  // 0x0047fc40 on the app's network part ([0x00608c88])
  e_.fade_to_state(kLobbyState, 0, 1, 2, kFadeSeconds, 0, 0, 0);
  MessageBox::answer_ok();
  close();
}

// Mech3.exe 1.2 @ 0x0048e3d0
void QuitBox::answer_cancel() {
  MessageBox::answer_cancel();
  close();
}

constexpr uint32_t kKeyY = 0x15, kKeyN = 0x31, kKeyEscape = 0x01;  // DIK codes

// Mech3.exe 1.2 @ 0x0048e2d0: shown once; the command on mouse button 1
// unbound meanwhile (a click answers the box, not the mech). FIX (bug),
// playtest W44/W46: the box (drawn by nothing in the port then) kept the
// mouse's fire unbound for the rest of the match; with bug fixes on the
// match goes on under it untouched (Owen): the mouse stays bound, Y leaves,
// N or Escape again closes it, and only Y's and N's own commands are lifted
// while it shows.
void QuitBox::prompt() {
  // With bug fixes on each fresh Escape press toggles it (held, it stays as it is).
  if (zfix::options().bugs && e_.key_state(kKeyEscape) != 1) return;
  if (active) {
    if (zfix::options().bugs) close();
    return;
  }
  set_active(1);
  std::printf("mw3: quit box up\n");  // ours: the tests follow it
  if (zfix::options().bugs) {
    mouse_cmd = 0;
    const uint32_t keys[2] = {kKeyY, kKeyN};
    for (int i = 0; i < 2; ++i) {
      held_cmds[i][0] = e_.key1_command(keys[i]);
      held_cmds[i][1] = e_.key2_command(keys[i]);
      if (held_cmds[i][0]) e_.bind_key1(keys[i], 0);
      if (held_cmds[i][1]) e_.bind_key2(keys[i], 0);
    }
    return;
  }
  mouse_cmd = e_.mouse_command(1);
  if (mouse_cmd) {
    mouse_button = e_.mouse_of(mouse_cmd);
    e_.bind_mouse(0, mouse_cmd);
  }
}

// Mech3.exe 1.2 @ 0x0048e320
void QuitBox::close() {
  if (!active) return;
  if (mouse_cmd) e_.bind_mouse(mouse_button, mouse_cmd);
  const uint32_t keys[2] = {kKeyY, kKeyN};
  for (int i = 0; i < 2; ++i) {
    if (held_cmds[i][0]) e_.bind_key1(keys[i], held_cmds[i][0]);
    if (held_cmds[i][1]) e_.bind_key2(keys[i], held_cmds[i][1]);
    held_cmds[i][0] = held_cmds[i][1] = 0;
  }
  set_active(0);
  std::printf("mw3: quit box down\n");
}

void QuitBox::keys() {
  if (!active || !zfix::options().bugs) return;
  if (e_.key_state(kKeyY) == 1) answer_ok();
  else if (e_.key_state(kKeyN) == 1) answer_cancel();
}

}  // namespace mech3
