#include "mech3/controls_menu.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "gamez/zutil/zfix.h"
#include "mech3/controls_cfg.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
int32_t g_mouse_guard = 0;  // [0x006056f4]

// The render target from zVideo's buffers (0x00558290), as the option screens'.
void target_buffers(Engine& e) {
  const Handle a = e.video_back_buffer();
  const Handle b = e.video_buffer_da0();
  const Handle r = e.video_buffer_d90();
  e.render_set_target2(e.video_target(), r, b, a);
}

// MSVCRT's _splitpath, the file name part: after the drive and the last
// slash, before the last dot.
std::string file_part(const char* path) {
  std::string p = path ? path : "";
  if (p.size() >= 2 && p[1] == ':') p.erase(0, 2);
  const size_t slash = p.find_last_of("\\/");
  if (slash != std::string::npos) p.erase(0, slash + 1);
  const size_t dot = p.rfind('.');
  if (dot != std::string::npos) p.erase(dot);
  return p.substr(0, 0xff);
}

const CommandEntry& entry_of(int32_t cmd) {
  static const CommandEntry kNone{};
  return cmd >= 0 && cmd < 0xa7 ? controls_setup().commands[cmd] : kNone;
}
AxisBinding& axis_at(int32_t axis) { return axis_table().axes[axis & 3]; }

// Mech3.exe 1.2 @ 0x00430220: the first axis bound to a source, 4 for none.
int32_t axis_of_source(int32_t source) {
  for (int32_t i = 0; i < 4; ++i)
    if (axis_table().axes[i].source == source) return i;
  return 4;
}

// Mech3.exe 1.2 @ 0x00430180: a source the popup offers. The original tests
// zInput's axis table (0x0051da90) entry by entry against null, a static
// array: the stick's six axes are offered whatever the stick has, its two
// sliders never. FIX (bug), W56: the sliders too (a throttle lever is often
// one: a T.16000M's could not be picked).
bool source_listed(int32_t source) {
  if (zfix::options().bugs && (source == 7 || source == 8)) return true;
  return (source >= 1 && source <= 6) || (source >= 9 && source <= 11);
}
// Ours (W56): a slider's reading is DirectInput's default 0..65535 (zInput
// sets a range on the six axes only); the popup shows and binds it as -1..1.
constexpr float kSliderHalf = 32767.5f;
bool slider(int32_t source) { return source == 7 || source == 8; }

// 0x0042c7a0, 0x0042ca70, 0x0042cc10: taking a key or button from the command
// that has it; asked only when one does and it is not the same command.
enum Device { kKey, kJoyButton, kMouseButton };
bool confirm(Engine& e, Device d, uint32_t what, int32_t old, int32_t cmd) {
  if (what == 0 || old == cmd) return true;
  char old_name[0x28], new_name[0x28], name[0x28];
  e.command_name(old, old_name, 0x28);
  e.command_name(cmd, new_name, 0x28);
  const char* n = d == kKey ? e.key_name(what, name, 0x28)
                  : d == kJoyButton ? e.joy_button_name(what, name, 0x28)
                                    : e.mouse_button_name(what, name, 0x28);
  (void)n;
  static const int32_t kCaption[] = {0xf27, 0xf29, 0xf2b};
  static const int32_t kText[] = {0xf28, 0xf2a, 0xf2a};
  return e.message_box3(1, kCaption[d], kText[d], name, old_name, new_name) == 1;
}
}  // namespace

int32_t& controls_mouse_guard() { return g_mouse_guard; }

// ---- the lists ----

// Mech3.exe 1.2 @ 0x0042f590
int32_t ControlsList::add(const char* text, int32_t value) {
  const int32_t old = static_cast<int32_t>(strings.size());
  strings.emplace_back(text ? text : "");
  values.push_back(value);
  return old;
}

void ControlsList::clear() {
  strings.clear();
  values.clear();
  first = -1;
}

// FIX (bug): the original reads the item unchecked (a crash for a group with
// no listed command, which the shipped setup never has); 0 here.
int32_t ControlsList::value_at(int32_t i) const {
  return i >= 0 && static_cast<size_t>(i) < values.size() ? values[static_cast<size_t>(i)] : 0;
}

ControlsScreen* ControlsList::screen() { return dynamic_cast<ControlsScreen*>(dialog); }

// Mech3.exe 1.2 @ 0x0042d3c0 (key A), 0x0042d450 (key B), 0x0042d4d0
// (joystick: each source's reading taken as its range), 0x0042d5b0 (mouse:
// not while the guard runs); the commands' is the check button's (0x0059db90).
void ControlsList::on_left_up() {
  ControlsScreen* s = screen();
  switch (kind) {
    case kCommands: break;
    case kKeyA: s->start_capture(1); break;
    case kKeyB: s->start_capture(2); break;
    case kJoy:
      s->start_capture(3);
      s->sample_axes();
      break;
    case kMouse:
      if (g_mouse_guard > 0) return;
      s->start_capture(4);
      break;
  }
  CheckButton::on_left_up();
}

// Mech3.exe 1.2 @ 0x0042d3f0, 0x0042d480, 0x0042d530, 0x0042d5e0: the row's
// key or button unbound (a capture of key or button 0).
void ControlsList::on_right_up() {
  ControlsScreen* s = screen();
  const int32_t row = first;
  switch (kind) {
    case kCommands: break;
    case kKeyA:
      if (s->bind_key1(0, row)) ListBox::select(row);
      break;
    case kKeyB: s->bind_key2(0, row); break;
    case kJoy:
      s->bind_joy(0, row);
      s->clear_joy_axis();
      break;
    case kMouse:
      if (g_mouse_guard > 0) return;
      s->bind_mouse(0, row);
      s->clear_mouse_axis();
      break;
  }
}

// Mech3.exe 1.2 @ 0x0042d3a0, 0x0042d420, 0x0042d4a0, 0x0042d550, 0x0042d610:
// a row clicked; in a binding column the capture starts too.
void ControlsList::select(int32_t index) {
  ControlsScreen* s = dynamic_cast<ControlsScreen*>(dialog);
  switch (kind) {
    case kCommands: break;
    case kKeyA: s->start_capture(1); break;
    case kKeyB: s->start_capture(2); break;
    case kJoy:
      s->start_capture(3);
      s->sample_axes();
      break;
    case kMouse: s->start_capture(4); break;
  }
  s->select_command(index);
}

// ---- the buttons ----

void ControlsButton::on_left_up() {
  auto* s = dynamic_cast<ControlsScreen*>(dialog);
  AxisTable& t = axis_table();
  switch (kind) {
    case kAccept:  // 0x0042d960: a changed map asks to be saved first (the popup in save mode)
      if (s->dirty && e_.message_box(1, 0xfb4, 0xfb5) == 1) {
        s->push_load_save(1);
        return;
      }
      e_.input_map_refresh();
      s->apply(1);
      controls_keep_saved();  // ours (W40): the map accepted is the one saved
      s->state_escape();
      return;
    case kCancel:  // 0x0042d730: a changed map goes back to the defaults, then the file is read again
      if (s->dirty) {
        e_.controls_defaults();
        e_.input_map_refresh();
        if (e_.joystick_present()) {
          t.set3(0, 3, -0.5f, 0.5f);
          t.axes[0].deadzone = 0.05f;
          t.ready = 0;
          t.set3(1, 6, -1.0f, 0.0f);
          t.axes[1].deadzone = 0.0f;
          t.ready = 0;
          t.set3(2, 2, 1.0f, 0.0f);
          t.axes[2].deadzone = 0.0f;
          t.ready = 0;
          t.set(3, 1, -1.0f, 0.0f, 0.0f);
        } else {
          t.set3(2, 10, 1.0f, 0.0f);
          t.axes[2].deadzone = 0.0f;
          t.ready = 0;
          t.set3(1, 9, 1.0f, 0.0f);
          t.axes[1].deadzone = 0.0f;
          t.axes[0].source = 0;
          t.ready = 0;
          t.set_source(3, 0);
        }
        t.ready = 1;
        s->load_save(file_part(e_.active_ctl_cfg()).c_str(), false);
      }
      s->state_escape();
      return;
    case kResetMouse:  // 0x0042d210: pitch and twist on the mouse's Y and X, saved as DefaultCtlConfig
      e_.controls_defaults();
      e_.input_map_refresh();
      t.axes[2] = {10, 1.0f, 0.0f, 0.0f};
      t.axes[1] = {9, 1.0f, 0.0f, 0.0f};
      t.axes[0].source = 0;
      t.axes[3].source = 0;
      t.ready = 1;
      s->load_save(e_.message(0xfc0), true);
      e_.set_use_joystick(0);
      Control::on_left_up();
      return;
    case kResetJoy:  // 0x0042d2b0: the stick's defaults (0x00410950's), saved as DefaultCtlConfig
      e_.controls_defaults();
      e_.input_map_refresh();
      t.axes[0] = {3, -0.5f, 0.5f, 0.05f};
      t.axes[1] = {6, -1.0f, 0.0f, 0.0f};
      t.axes[2] = {2, 1.0f, 0.0f, 0.0f};
      t.axes[3] = {1, -1.0f, 0.0f, 0.0f};
      t.ready = 1;
      s->load_save(e_.message(0xfc0), true);
      e_.set_use_joystick(1);
      Control::on_left_up();
      return;
    case kAssignAxis:  // 0x0042d8f0
      if (!enabled) return;
      Control::on_left_up();
      if (s) s->push_axis_popup(0);
      return;
  }
}

void ControlsArrow::on_left_up() {
  auto* s = dynamic_cast<ControlsScreen*>(dialog);
  switch (kind) {
    case kNext:  // 0x0042d6f0
      s->step_command(1);
      Control::on_left_up();
      return;
    case kPrev:  // 0x0042d710
      s->step_command(-1);
      Control::on_left_up();
      return;
    case kExport:  // 0x0042d890
      if (s) s->push_load_save(1);
      return;
    case kImport:  // 0x0042d8c0
      if (s) s->push_load_save(0);
      return;
  }
}

// Mech3.exe 1.2 @ 0x0042d640
void GroupCycle::on_left_up() {
  CycleButton::on_left_up();
  auto* s = dynamic_cast<ControlsScreen*>(dialog);
  s->fill(s->group());
  s->show_file_name();
}

// Mech3.exe 1.2 @ 0x0042ad70
void GroupCycle::after_bind() {
  CycleButton::after_bind();
  for (int32_t i = 0; i < e_.controls_group_count(); ++i) add_item(e_.controls_group_title(i), i);
}

// ---- the popups' states ----

// Mech3.exe 1.2 @ 0x0042e6a0
AxisPopupState::AxisPopupState(Engine& e, Handle addr) : MenuState("Assign Axis Popup State", addr), e_(e) {
  popup = new AxisPopupScreen(e, addr);  // new(0x12194)
  popup->owner = this;
  screen_ = e.adopt_screen(popup, 0x12194);
  if (!screen_) {  // no engine to own it: this state does
    delete popup;
    popup = nullptr;
  }
}

// The shell's destructor (0x0048eb80) without the app: the controls screen
// goes when the program's engine does, after the app.
AxisPopupState::~AxisPopupState() {
  if (screen_) e_.vcall(screen_, 0x08, {1});
  screen_ = 0;
}

// Mech3.exe 1.2 @ 0x0042e720
bool AxisPopupState::enter() {
  prepare();
  return ShellState::enter();
}

// The stick taken, the axis's source (or the one the screen found) selected
// and the inversion shown from the scale's sign, the screen behind grabbed.
void AxisPopupState::prepare() {
  e_.joy_use();
  if (!popup) return;
  popup->axis = axis;
  const AxisBinding& b = axis_at(axis);
  popup->sources.select_value(source ? source : b.source);
  popup->invert.set_checked(!(b.scale >= 0.0f) ? 1 : 0);
  zimg::Texture* g = ztwod::host().screen_grab(2);
  popup->grab = g;
  if (g) popup->background.set_image(g);
}

// Mech3.exe 1.2 @ 0x0042e7c0
void AxisPopupState::leave() {
  ShellState::leave();
  finish();
}

void AxisPopupState::finish() {
  if (popup && popup->grab) {
    ztwod::host().texture_free(popup->grab);
    popup->grab = nullptr;
  }
  e_.joy_unuse();
}

// Mech3.exe 1.2 @ 0x0042e7f0
void AxisPopupState::apply(float scale, float offset) {
  const int32_t src = popup ? popup->sources.selected_value() : 0;
  if (owner) owner->bind_axis(src, scale, offset);
}

// Mech3.exe 1.2 @ 0x0042f230 (its name copied from the axis popup's)
LoadSavePopupState::LoadSavePopupState(Engine& e, Handle addr)
    : MenuState("Assign Axis Popup State", addr), e_(e) {
  popup = new LoadSavePopupScreen(e, addr);  // new(0x11cd4)
  popup->owner = this;
  screen_ = e.adopt_screen(popup, 0x11cd4);
  if (!screen_) {  // no engine to own it: this state does
    delete popup;
    popup = nullptr;
  }
}

LoadSavePopupState::~LoadSavePopupState() {
  if (screen_) e_.vcall(screen_, 0x08, {1});
  screen_ = 0;
}

// Mech3.exe 1.2 @ 0x0042f2b0
bool LoadSavePopupState::enter() {
  prepare();
  return ShellState::enter();
}

void LoadSavePopupState::prepare() {
  if (!popup) return;
  popup->save = save;
  zimg::Texture* g = ztwod::host().screen_grab(2);
  popup->grab = g;
  if (g) popup->backdrop.set_image(g);
  popup->bar.set_text(file_part(e_.active_ctl_cfg()).c_str());
  popup->fill();
}

// Mech3.exe 1.2 @ 0x0042f340
void LoadSavePopupState::leave() {
  ShellState::leave();
  finish();
}

// Mech3.exe 1.2 @ 0x0042f380
void LoadSavePopupState::apply(const char* name, uint8_t to_file) {
  if (owner) owner->load_save(name, to_file != 0);
}

void LoadSavePopupState::finish() {
  if (!popup || !popup->grab) return;
  ztwod::host().texture_free(popup->grab);
  popup->grab = nullptr;
  popup->backdrop.set_image(static_cast<zimg::Texture*>(nullptr));
}

// ---- the screen ----

// Mech3.exe 1.2 @ 0x0042a590
ControlsScreen::ControlsScreen(Engine& e, Handle s, Handle self)
    : MenuScreen(e, s), accept(e, ControlsButton::kAccept), cancel(e, ControlsButton::kCancel),
      reset_mouse(e, ControlsButton::kResetMouse), reset_joy(e, ControlsButton::kResetJoy), group_cycle(e),
      assign_axis(e, ControlsButton::kAssignAxis), option_text(e), axis_popup(e, self + 0x16760),
      load_save_popup(e, self + 0x1677c), self_(self) {
  target_buffers(e);
  mode = 0;
  e.input_reset();
  const bool moving = !e.fast_menu();
  if (load("dlgOptions.zrd", "CTL_CFG_DIALOG", moving)) {
    borrow_background(true);
    bind_primitive(&lines, "LINES");
    static const char* const kTitles[] = {"TITLE_TEXT1", "TITLE_TEXT2", "TITLE_TEXT3",
                                          "TITLE_TEXT4", "TITLE_TEXT5", "TITLE_TEXT6"};
    for (int i = 0; i < 6; ++i) bind_primitive(&titles[i], kTitles[i]);
    bind_button(&accept, "ACCEPT");
    bind_button(&cancel, "CANCEL");
    bind_button(&reset_mouse, "CMD_RESET_TO_MOUSE_BTN");
    bind_button(&reset_joy, "CMD_RESET_TO_JSTICK_BTN");
    bind_button(&assign_axis, "CMD_ASSIGN_AXIS_BTN");
    if (!moving) {  // FastMenu: drawn once (the background stays in the list, unlike the option screens')
      bake();
      remove(&lines);
      for (ztwod::Text& t : titles) remove(&t);
      accept.set_baked(true);
      cancel.set_baked(true);
      reset_mouse.set_baked(true);
      reset_joy.set_baked(true);
      assign_axis.set_baked(true);
    }
    bind_primitive(&file_name, "CURRENT_FILENAME");
    bind_button(&commands, "CMD_COMMAND_BTN");
    bind_button(&key_a, "CMD_KEYA_BTN");
    bind_button(&key_b, "CMD_KEYB_BTN");
    bind_button(&joy, "CMD_JOY_BTN");
    bind_button(&mouse, "CMD_MOUSE_BTN");
    bind_button(&group_cycle, "CMD_SET_BTN");
    bind_button(&next, "CMD_NEXT_CMD_BTN");
    bind_button(&prev, "CMD_PREV_CMD_BTN");
    bind_button(&export_btn, "CMD_EXPORT_BTN");
    bind_button(&import_btn, "CMD_IMPORT_BTN");
    bind_primitive(&description, "CMD_DESCRIPTION");
    bind_primitive(&option_text, "OPTIONTEXT");
    unload();
  }
  set_flags_all(0);
  set_active(1);
  fill(0);
  group_cycle.select_value(0);
  dirty = 0;
  show_file_name();
}

// Mech3.exe 1.2 @ 0x0042b880: the members' destructors.
ControlsScreen::~ControlsScreen() = default;

// Mech3.exe 1.2 @ 0x0042be10: in a capture mode the other columns are
// unchecked each frame and zInput asked for the key or button; Escape
// cancels (a key capture takes it as the key 1, which also cancels). The
// stick capture opens the axis popup for the first source moved more than
// 1.0 since the capture began (half its travel).
void ControlsScreen::update(float dt) {
  Window::update(dt);
  switch (mode) {
    case 0:
      --g_mouse_guard;
      return;
    case 1: {
      key_b.set_checked(0);
      joy.set_checked(0);
      mouse.set_checked(0);
      const uint32_t k = capture_key();
      if (!k) return;
      bind_key1(k, key_a.first);
      key_a.set_checked(0);
      return;
    }
    case 2: {
      key_a.set_checked(0);
      joy.set_checked(0);
      mouse.set_checked(0);
      const uint32_t k = capture_key();
      if (!k) return;
      bind_key2(k, key_b.first);
      key_b.set_checked(0);
      return;
    }
    case 3: {
      Engine& e = engine();
      key_a.set_checked(0);
      key_b.set_checked(0);
      mouse.set_checked(0);
      const Handle j = e.joy_state();
      int32_t moved = 0;
      for (int32_t src = 1; src < 9; ++src) {
        float* r = range[src - 1];
        const float v = e.source_value(src, j);
        if (!(v >= r[0])) r[0] = v;
        if (v > r[1]) r[1] = v;
        if (std::fabs(r[0] - r[1]) > 1.0f) {
          moved = src;
          break;
        }
      }
      if (moved) {
        push_axis_popup(moved);
        mode = 0;
        e.input_reset();
        joy.set_checked(0);
      }
      if (e.input_read_key(0) == 1) {
        mode = 0;
        e.input_reset();
        joy.set_checked(0);
        return;
      }
      const int32_t b = e.joy_wait_button(0);
      if (!b) return;
      bind_joy(static_cast<uint32_t>(b), joy.first);
      joy.set_checked(0);
      return;
    }
    case 4: {
      Engine& e = engine();
      key_a.set_checked(0);
      key_b.set_checked(0);
      joy.set_checked(0);
      if (e.input_read_key(0) == 1) {
        mode = 0;
        e.input_reset();
        mouse.set_checked(0);
        return;
      }
      const int32_t b = e.mouse_wait_button(0);
      if (!b) return;
      bind_mouse(static_cast<uint32_t>(b), mouse.first);
      mouse.set_checked(0);
      g_mouse_guard = 10;
      return;
    }
    default: return;
  }
}

// Mech3.exe 1.2 @ 0x0042c160: one row a command of the group that has a
// name (zInput's, given by the defaults): its message, key A, key B, then the
// joystick and mouse columns. A command that also has an axis (throttle,
// turn, twist, pitch) shows its axis in the stick's column when bound to a
// stick axis (the mouse column keeps the button), in the mouse's when bound
// to a mouse axis (the stick column keeps the button).
void ControlsScreen::fill(int32_t g) {
  Engine& e = engine();
  commands.clear();
  key_a.clear();
  key_b.clear();
  joy.clear();
  mouse.clear();
  for (int32_t i = 0; i < e.controls_group_size(g); ++i) {
    const int32_t cmd = e.controls_group_command(g, i);
    char name[0x28] = {};
    e.command_name(cmd, name, 0x28);
    if (!name[0]) continue;
    const CommandEntry& c = entry_of(cmd);
    commands.add(e.message(c.message), cmd);
    char buf[0x28];
    key_a.add(e.key_name(e.key1_of(cmd), buf, 0x28), cmd);
    key_b.add(e.key_name(e.key2_of(cmd), buf, 0x28), cmd);
    const int32_t src = c.axis_key ? axis_at(c.axis).source : 0;
    if (c.axis_key && src >= 9) {
      joy.add(e.joy_button_name(e.joy_of(cmd), buf, 0x28), cmd);
      mouse.add(e.message_format(0xf5d, axis_source_name(src)), cmd);  // "Mouse %1 axis"
    } else if (c.axis_key && src != 0) {
      joy.add(e.message_format(0xf5c, axis_source_name(src)), cmd);  // "Joystick %1 axis"
      mouse.add(e.mouse_button_name(e.mouse_of(cmd), buf, 0x28), cmd);
    } else {
      joy.add(e.joy_button_name(e.joy_of(cmd), buf, 0x28), cmd);
      mouse.add(e.mouse_button_name(e.mouse_of(cmd), buf, 0x28), cmd);
    }
  }
  select_command(0);
}

// Mech3.exe 1.2 @ 0x0042c4e0: another row ends any capture. The description
// is the command's name message + 0x100.
void ControlsScreen::select_command(int32_t i) {
  Engine& e = engine();
  if (i != commands.first) {
    mode = 0;
    e.input_reset();
    key_a.set_checked(0);
    key_b.set_checked(0);
    joy.set_checked(0);
    mouse.set_checked(0);
  }
  commands.ListBox::select(i);
  key_a.ListBox::select(i);
  key_b.ListBox::select(i);
  joy.ListBox::select(i);
  mouse.ListBox::select(i);
  prev.enabled = i != 0 ? 1 : 0;
  prev.refresh();
  next.enabled = i < static_cast<int32_t>(commands.strings.size()) - 1 ? 1 : 0;
  next.refresh();
  const CommandEntry& c = entry_of(current_command());
  assign_axis.enabled = c.axis_key;
  assign_axis.refresh();
  if (const char* text = e.message(c.message + 0x100)) description.set_text("%s", text);
  else description.set_text("");
}

int32_t ControlsScreen::current_command() const { return commands.value_at(commands.first); }

// Mech3.exe 1.2 @ 0x0042d1c0
int32_t ControlsScreen::step_command(int32_t by) {
  const int32_t i = by + commands.first;
  if (i >= 0 && i < static_cast<int32_t>(commands.strings.size())) commands.ListBox::select(i);
  const int32_t f = commands.first;
  select_command(f);
  return f;
}

// Mech3.exe 1.2 @ 0x0042bd90
void ControlsScreen::show_file_name() {
  const std::string name = file_part(engine().active_ctl_cfg());
  const char* star = dirty ? "*" : "";
  file_name.set_text("%s: %s %s", engine().message(0xfae), name.c_str(), star);  // "CURRENT CONFIG"
}

// Mech3.exe 1.2 @ 0x0042c670: Escape (1) cancels; a key in either slot of
// another command is taken from it once confirmed (from slot B explicitly,
// slot A by the bind itself).
bool ControlsScreen::bind_key1(uint32_t key, int32_t index) {
  Engine& e = engine();
  mode = 0;
  e.input_reset();
  if (key == 1) return true;
  const int32_t old = e.key1_command(key);
  const int32_t g = group();
  const int32_t cmd = e.controls_group_command(g, index);
  if (old) {
    if (!confirm(e, kKey, key, old, cmd)) return false;
  } else if (const int32_t old2 = e.key2_command(key)) {
    if (!confirm(e, kKey, key, old2, cmd)) return false;
    e.bind_key2(key, 0);
  }
  e.bind_key1(key, cmd);
  dirty = 1;
  show_file_name();
  fill(g);
  select_command(index);
  return true;
}

// Mech3.exe 1.2 @ 0x0042c830: as 0x0042c670 with the slots swapped.
bool ControlsScreen::bind_key2(uint32_t key, int32_t index) {
  Engine& e = engine();
  mode = 0;
  e.input_reset();
  if (key == 1) return true;
  const int32_t old = e.key2_command(key);
  const int32_t g = group();
  const int32_t cmd = e.controls_group_command(g, index);
  if (old) {
    if (!confirm(e, kKey, key, old, cmd)) return false;
  } else if (const int32_t old2 = e.key1_command(key)) {
    if (!confirm(e, kKey, key, old2, cmd)) return false;
    e.bind_key1(key, 0);
  }
  e.bind_key2(key, cmd);
  dirty = 1;
  show_file_name();
  fill(g);
  select_command(index);
  return true;
}

// Mech3.exe 1.2 @ 0x0042c960: a stick button replaces the command's stick axis.
bool ControlsScreen::bind_joy(uint32_t b, int32_t index) {
  Engine& e = engine();
  const int32_t old = e.joy_command(b);
  const int32_t g = group();
  const int32_t cmd = e.controls_group_command(g, index);
  mode = 0;
  e.input_reset();
  if (old) {
    if (!confirm(e, kJoyButton, b, old, cmd)) return false;
    e.bind_joy(b, 0);
  }
  clear_joy_axis();
  e.bind_joy(b, cmd);
  dirty = 1;
  show_file_name();
  fill(g);
  select_command(index);
  return true;
}

// Mech3.exe 1.2 @ 0x0042cb00: a mouse button replaces the command's mouse axis.
bool ControlsScreen::bind_mouse(uint32_t b, int32_t index) {
  Engine& e = engine();
  const int32_t old = e.mouse_command(b);
  const int32_t g = group();
  const int32_t cmd = e.controls_group_command(g, index);
  mode = 0;
  e.input_reset();
  if (old) {
    if (!confirm(e, kMouseButton, b, old, cmd)) return false;
    e.bind_mouse(b, 0);
  }
  clear_mouse_axis();
  e.bind_mouse(b, cmd);
  dirty = 1;
  show_file_name();
  fill(g);
  select_command(index);
  return true;
}

// Mech3.exe 1.2 @ 0x0042cca0: the popup's choice for the selected command's
// axis; a source another axis has is taken from it once confirmed.
void ControlsScreen::bind_axis(int32_t src, float scale, float offset) {
  Engine& e = engine();
  const int32_t axis = entry_of(current_command()).axis;
  const int32_t other = axis_of_source(src);
  if (other != axis && other != 4) {
    const char* what = e.message_format(src >= 9 ? 0xf5d : 0xf5c, axis_source_name(src));
    const std::string text = what ? what : "";
    if (e.message_box3(1, 0xfb6, 0xfb7, text.c_str(), axis_name(other), axis_name(axis)) != 1) return;
    axis_at(other).source = 0;
  }
  axis_at(axis) = {src, scale, offset, 0.0f};
  axis_table().ready = 0;
  dirty = 1;
  show_file_name();
  fill(group());
  show_file_name();
}

// Mech3.exe 1.2 @ 0x0042ce90
void ControlsScreen::clear_joy_axis() {
  const CommandEntry& c = entry_of(current_command());
  if (!c.axis_key) return;
  AxisBinding& a = axis_at(c.axis);
  if (a.source != 0 && a.source < 9) {
    a.source = 0;
    axis_table().ready = 0;
  }
  dirty = 1;
  show_file_name();
  fill(group());
  show_file_name();
}

// Mech3.exe 1.2 @ 0x0042cfa0
void ControlsScreen::clear_mouse_axis() {
  const CommandEntry& c = entry_of(current_command());
  if (!c.axis_key) return;
  AxisBinding& a = axis_at(c.axis);
  if (a.source != 0 && a.source >= 9) {
    a.source = 0;
    axis_table().ready = 0;
  }
  dirty = 1;
  show_file_name();
  fill(group());
  show_file_name();
}

// Mech3.exe 1.2 @ 0x0042d0b0. FIX (A): the original makes keys\ in the game's
// folder (_mkdir); the controls file's open makes it in the write dir
// (controls_cfg). FIX (bug): the path is cut at 0x104 bytes (the original's
// sprintf could overrun with a long ActiveCtlCfg).
void ControlsScreen::load_save(const char* name, bool save) {
  Engine& e = engine();
  char path[0x104];
  std::snprintf(path, sizeof path, "%s\\%s.zrd", "keys", name ? name : "");
  if (save) e.controls_write(path);
  else e.controls_read(path);
  std::strncpy(e.active_ctl_cfg(), path, 0x104);
  controls_keep_saved();  // ours (W40): a set loaded, saved or reset is the one saved
  dirty = 0;
  show_file_name();
  fill(group());
  show_file_name();
}

// FIX (A), Reddit v1.0 (docs/fixes.md): the shell's frame reads zInput's
// keyboard first (0x0048ed30, 0x0051b360), emptying the buffer this read
// asks. DirectInput filled it in real time, so a key that came while the
// screen drew was still there; our events arrive before the frame, so the
// capture never saw one. The key the frame's read saw go down is taken.
uint32_t ControlsScreen::capture_key() {
  Engine& e = engine();
  const uint32_t k = e.input_read_key(0);
  return k ? k : e.input_frame_key();
}

void ControlsScreen::start_capture(int32_t m) {
  mode = m;
  engine().input_reset();
}

void ControlsScreen::sample_axes() {
  Engine& e = engine();
  const Handle j = e.joy_state();
  for (int32_t src = 1; src < 9; ++src) {
    const float v = e.source_value(src, j);
    range[src - 1][1] = v;
    range[src - 1][0] = v;
  }
}

void ControlsScreen::push_axis_popup(int32_t src) {
  axis_popup.owner = this;
  axis_popup.axis = entry_of(current_command()).axis;
  axis_popup.source = src;
  engine().push_state(self_ + 0x16760, 0);
}

void ControlsScreen::push_load_save(uint8_t save) {
  load_save_popup.owner = this;
  load_save_popup.save = save;
  engine().push_state(self_ + 0x1677c, 0);
}

// ---- the axis popup ----

// Mech3.exe 1.2 @ 0x0042dd20, 0x0042df60
void AxisCycle::after_bind() {
  CycleButton::after_bind();
  for (int32_t src = 0; src < 12; ++src) {
    if (!source_listed(src)) continue;
    const char* t = src == 0 ? "---" : e_.message_format(src > 8 ? 0xf5d : 0xf5c, axis_source_name(src));
    char text[0x40];
    std::snprintf(text, sizeof text, "%s", t ? t : "");
    add_item(text, src);
  }
}

// Mech3.exe 1.2 @ 0x0042f8b0
void InvertCheck::on_left_up() {
  if (!enabled || !active) return;
  CheckButton::on_left_up();
  notify(1, 0);
}

// Mech3.exe 1.2 @ 0x0042f8f0
void InvertCheck::notify(int32_t a, int32_t b) {
  if (auto* s = dynamic_cast<MenuScreen*>(owner)) s->on_button(id, a, b);
}

// Mech3.exe 1.2 @ 0x0042f920: every label ends at its place (moved left by its width).
bool InvertCheck::read_control(zrdr::Node* n, ztwod::Dialog* d) {
  const bool r = CheckButton::read_control(n, d);
  for (std::vector<ztwod::Text*>* v : {&labels, &disable_labels, &rollover_labels, &activate_labels}) {
    for (ztwod::Text* t : *v) {
      if (!t) continue;
      const int32_t px = t->get_x();
      const int32_t py = t->get_y();
      if (t->needs_render) t->render();
      t->set_pos(px - t->width, py);
    }
  }
  return r;
}

// Mech3.exe 1.2 @ 0x0042da80
AxisPopupScreen::AxisPopupScreen(Engine& e, Handle s)
    : MenuScreen(e, s), ok(e), cancel(e), sources(e) {
  grab = nullptr;
  if (load("dlgOptions.zrd", "ASSIGN_AXIS_POPUP", false)) {
    bind_button(&ok, "MB_OK");
    bind_button(&cancel, "MB_CANCEL");
    bind_button(&sources, "CMD_AXIS_CYCLE_BTN");
    bind_button(&value, "AXIS_VALUE");
    bind_button(&invert, "INVERT_CHECKBOX");
    bind_primitive(&instructions, "INSTRUCTIONS");
    unload();
  }
  instructions.show_for(-1.0f);
  set_flags_all(0);
}

// Mech3.exe 1.2 @ 0x0042e470: the reading -1..1 shown as 0..1 (inverted
// with the box), disabled without a stick.
void AxisPopupScreen::update(float dt) {
  Engine& e = engine();
  const int32_t src = sources.selected_value();
  const Handle j = e.joy_state();
  if (!j) {
    value.enabled = 0;
    value.refresh();
    Window::update(dt);
    return;
  }
  float r = e.source_value(src, j);
  if (zfix::options().bugs && slider(src)) r = r / kSliderHalf - 1.0f;
  float v = (r - -1.0f) * 0.5f;
  if (invert.checked) v = 1.0f - v;
  if (v > 1.0f) v = 1.0f;
  else if (!(v >= 0.0f)) v = 0.0f;
  value.set_raw(v);
  value.enabled = 1;
  value.refresh();
  Window::update(dt);
}

// Mech3.exe 1.2 @ 0x0042e5b0: scale 1 (-1 inverted) and the offset that makes
// the source's reading now 0, no dead zone.
void AxisPopupScreen::OkButton::on_left_up() {
  auto* s = dynamic_cast<AxisPopupScreen*>(dialog);
  if (!s) return;
  const int32_t src = s->sources.selected_value();
  const Handle j = e_.joy_state();
  const float k = s->invert.checked ? -1.0f : 1.0f;
  const float v = e_.source_value(src, j);
  float scale = k, offset = (v * k) * -1.0f;
  if (zfix::options().bugs) {
    // Ours (W56): the throttle bound stop-to-full, wherever the lever sits at
    // OK: the defaults' -0.5 and 0.5 with Invert (which the popup shows
    // checked for them, the scale being negative), +0.5 without; with
    // throttle_reverse the lever map takes it over (mech3::lever_throttle).
    // A slider's reading scaled to -1..1.
    const bool throttle = s->owner && s->owner->axis == 0 && src >= 1 && src <= 8;
    if (throttle) scale = 0.5f * k, offset = 0.5f;
    if (slider(src)) {  // in -1..1: n = r / kSliderHalf - 1
      offset = throttle ? offset - scale : -k * v / kSliderHalf;  // (the reading at OK still reads 0)
      scale /= kSliderHalf;
    }
  }
  if (s->owner) s->owner->apply(scale, offset);
  s->state_escape();
}

// ---- the load/save popup ----

// Mech3.exe 1.2 @ 0x0042e890
LoadSavePopupScreen::LoadSavePopupScreen(Engine& e, Handle s)
    : MenuScreen(e, s), ok(e), cancel(e), bar(e), files(e) {
  if (load("dlgOptions.zrd", "LOAD_SAVE_POPUP", true)) {
    add(&backdrop);
    bind_primitive(&box, "LOAD_BOX");
    bind_button(&ok, "MB_OK");
    bind_button(&cancel, "MB_CANCEL");
    bind_button(&bar, "EDITBAR");
    bind_button(&files, "FILELIST");
    unload();
  }
  set_flags_all(0);
}

// Mech3.exe 1.2 @ 0x0042eff0
void LoadSavePopupScreen::fill() {
  files.clear_items();
  files.layout();
  int32_t n = 0;
  for (const std::string& name : engine().list_files("keys\\*.zrd")) files.add_item(file_part(name.c_str()).c_str(), n++);
}

// Mech3.exe 1.2 @ 0x0042f160
void LoadSavePopupScreen::OkButton::on_left_up() {
  auto* s = dynamic_cast<LoadSavePopupScreen*>(dialog);
  if (!s) return;
  const uint8_t save = s->save;
  const char* name = s->bar.get_text();
  if (s->owner) s->owner->apply(name, save);
  s->state_escape();
}

// The bar's constructor inline in 0x0042e890: what a file name cannot hold
// refused, 20 bytes (19 characters).
LoadSavePopupScreen::NameBar::NameBar(Engine& e) : ShellEditBar(e) {
  editor.set_reject(":|\\<>/?\"*");
  set_size(0x14);
  set_focus(0);
}

// Mech3.exe 1.2 @ 0x0042f1b0
void LoadSavePopupScreen::NameBar::on_left_up() {
  active = 1;
  set_focus(1);
}

// Mech3.exe 1.2 @ 0x0042f1a0
void LoadSavePopupScreen::NameBar::on_return() {
  active = 0;
  set_focus(0);
}
void LoadSavePopupScreen::NameBar::on_cancel() { on_return(); }

// Mech3.exe 1.2 @ 0x0042f1c0: the dialog's bar (+0x115c4 of the list's owner)
// takes the chosen name, or none.
void LoadSavePopupScreen::Files::select(int32_t index) {
  FileList::select(index);
  auto* s = dynamic_cast<LoadSavePopupScreen*>(owner);
  if (!s) return;
  if (index >= 0 && index < item_count()) s->bar.set_text(items[static_cast<size_t>(index)]->text);
  else s->bar.set_text(nullptr);
}

}  // namespace mech3
