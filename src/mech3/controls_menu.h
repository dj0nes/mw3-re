#pragma once
#include <cstdint>
#include <vector>

#include "mech3/menu_states.h"
#include "mech3/menu_widgets.h"
#include "mech3/options_menu.h"

// The controls screen of the options (docs/menus.md, The controls screen):
// dlgOptions.zrd's CTL_CFG_DIALOG, the command lists by group with their two
// keys, joystick and mouse buttons, key and button capture, the defaults, and
// the popups it pushes: ASSIGN_AXIS_POPUP (an axis for the throttle, turn,
// twist or pitch) and LOAD_SAVE_POPUP (a controls file by name). It edits
// zInput's command map and the axis table (controls_cfg) in place. Original
// file unknown: 0x0042a590..0x0042f8f0, between mech3\mech_anim.cpp and
// mech3\mech_data.cpp, with the option screens' neighbours.
namespace mech3 {

class ControlsScreen;
class AxisPopupScreen;

// A column of the screen (vtables 0x005bdff0 commands, 0x005bdf58 key A,
// 0x005bdec0 key B, 0x005bde28 joystick, 0x005bdd90 mouse), a ListBox whose
// items are {_strdup'd text, the command} (8 bytes; the value kept here).
class ControlsList : public ztwod::ListBox {
 public:
  enum Kind { kCommands, kKeyA, kKeyB, kJoy, kMouse };
  explicit ControlsList(Kind k) : kind(k) {}
  void on_left_up() override;        // +0x30  0x0059db90 / 0x0042d3c0, 0x0042d450, 0x0042d4d0, 0x0042d5b0
  void on_right_up() override;       // +0x34  -, 0x0042d3f0, 0x0042d480, 0x0042d530, 0x0042d5e0
  void select(int32_t index) override;  // +0x90  0x0042d3a0, 0x0042d420, 0x0042d4a0, 0x0042d550, 0x0042d610
  int32_t add(const char* text, int32_t value);  // 0x0042f590: the old count
  void clear();                      // the inline clear of 0x0042c160 (0x0042f7a0's)
  int32_t value_at(int32_t i) const;  // items[i]'s command (unchecked in the original)
  ControlsScreen* screen();          // the dialog (+0xc8)

  Kind kind;
  std::vector<int32_t> values;       // the items' second word, beside strings
};

// ACCEPT, CANCEL, CMD_RESET_TO_MOUSE_BTN, CMD_RESET_TO_JSTICK_BTN and
// CMD_ASSIGN_AXIS_BTN (vtables 0x005be268, 0x005be1c8, 0x005be128, 0x005be088,
// 0x005bda08), slide buttons.
class ControlsButton : public SlideButton {
 public:
  enum Kind { kAccept, kCancel, kResetMouse, kResetJoy, kAssignAxis };
  ControlsButton(Engine& e, Kind k) : SlideButton(e, -1), kind(k) {}
  // 0x0042d960, 0x0042d730, 0x0042d210, 0x0042d2b0, 0x0042d8f0
  void on_left_up() override;
  Kind kind;
};

// CMD_NEXT_CMD_BTN, CMD_PREV_CMD_BTN, CMD_EXPORT_BTN, CMD_IMPORT_BTN (vtables
// 0x005bdc58, 0x005bdbc8, 0x005bdb38, 0x005bdaa8): plain zTwoD controls.
class ControlsArrow : public ztwod::Control {
 public:
  enum Kind { kNext, kPrev, kExport, kImport };
  explicit ControlsArrow(Kind k) : kind(k) {}
  void on_left_up() override;        // +0x30  0x0042d6f0, 0x0042d710, 0x0042d890, 0x0042d8c0
  Kind kind;
};

// CMD_SET_BTN (vtable 0x005bdce8): the groups, by their titles.
class GroupCycle : public CycleButton {
 public:
  explicit GroupCycle(Engine& e) : CycleButton(e, -1) {}
  void on_left_up() override;        // +0x30  0x0042d640: the group's commands listed
  void after_bind() override;        // +0x88  0x0042ad70
};

// "Assign Axis Popup State" (0x0042e6a0, vtable 0x005be6a8), 0x1c bytes inside
// the controls screen (+0x16760): its screen made with it, pushed by the
// screen with the axis to set and the source to show first.
class AxisPopupState : public MenuState {
 public:
  AxisPopupState(Engine& e, Handle addr);  // 0x0042e6a0
  ~AxisPopupState() override;        // 0x0048eb80: its screen deleted through the engine it was made with
  bool enter() override;             // +0x0c  0x0042e720
  void leave() override;             // +0x18  0x0042e7c0
  void prepare();                    // 0x0042e720 before the shell's enter
  void finish();                     // 0x0042e7c0 after the shell's leave
  void apply(float scale, float offset);  // 0x0042e7f0: the owner binds the chosen source

  AxisPopupScreen* popup = nullptr;  // +0x0c's object
  ControlsScreen* owner = nullptr;   // +0x10
  int32_t axis = 0;                  // +0x14  0 throttle, 1 twist, 2 pitch, 3 turn
  int32_t source = 0;                // +0x18  shown first; 0: the axis's own

 private:
  Engine& e_;
};

class LoadSavePopupScreen;

// "Assign Axis Popup State" again by its name (0x0042f230, vtable 0x005be978):
// the LOAD_SAVE_POPUP's, 0x18 bytes at +0x1677c, its screen made with it.
class LoadSavePopupState : public MenuState {
 public:
  LoadSavePopupState(Engine& e, Handle addr);  // 0x0042f230
  ~LoadSavePopupState() override;    // 0x0048eb80: its screen deleted through the engine it was made with
  bool enter() override;             // +0x0c  0x0042f2b0
  void leave() override;             // +0x18  0x0042f340
  // 0x0042f2b0 before the shell's enter: the mode, the screen behind as the
  // backdrop, the active file's name in the bar, the files listed.
  void prepare();
  void finish();                     // 0x0042f340 after the shell's leave: the grab freed
  virtual void apply(const char* name, uint8_t save);  // 0x0042f380: the owner's 0x0042d0b0

  LoadSavePopupScreen* popup = nullptr;  // +0x0c's object
  ControlsScreen* owner = nullptr;   // +0x10
  uint8_t save = 0;                  // +0x14  1 save, 0 load

 private:
  Engine& e_;
};

// The controls screen (vtable 0x005bd968), 0x16c94 bytes.
class ControlsScreen : public MenuScreen {
 public:
  // `self`: its handle, which the popups' states are at offsets of.
  ControlsScreen(Engine& e, Handle state, Handle self);  // 0x0042a590
  ~ControlsScreen() override;        // 0x0042b880 (deleting 0x0042adb0)
  void update(float dt) override;    // +0x00  0x0042be10: the capture

  void fill(int32_t group);          // 0x0042c160: the lists of a group, the first selected
  void select_command(int32_t i);    // 0x0042c4e0: a row of every list, the arrows, the text
  int32_t step_command(int32_t by);  // 0x0042d1c0
  void show_file_name();             // 0x0042bd90: "<message 0xfae>: <name> [*]"
  // The captures: a key, button or axis for the command in row `index`;
  // false when the reassignment was refused.
  bool bind_key1(uint32_t key, int32_t index);  // 0x0042c670
  bool bind_key2(uint32_t key, int32_t index);  // 0x0042c830
  bool bind_joy(uint32_t b, int32_t index);     // 0x0042c960
  bool bind_mouse(uint32_t b, int32_t index);   // 0x0042cb00
  void bind_axis(int32_t source, float scale, float offset);  // 0x0042cca0
  void clear_joy_axis();             // 0x0042ce90: the command's axis off a stick source
  void clear_mouse_axis();           // 0x0042cfa0: off a mouse source
  void load_save(const char* name, bool save);  // 0x0042d0b0: keys\<name>.zrd, then ActiveCtlCfg
  void start_capture(int32_t mode);  // the lists' presses: the mode, zInput reset
  uint32_t capture_key();            // ours: 0x0051aa00's key, else the frame's (FIX (A))
  void sample_axes();                // 0x0042d4d0's loop: each source's reading as its range
  void push_axis_popup(int32_t source);  // 0x0042bfca / 0x0042d911
  void push_load_save(uint8_t save);     // 0x0042d890, 0x0042d8c0, 0x0042d98a
  int32_t group() { return group_cycle.selected_value(); }
  int32_t current_command() const;   // commands' item at its selection
  Handle self() const { return self_; }

  ztwod::Text titles[6];             // +0x111ec  TITLE_TEXT1..6
  ControlsButton accept;             // +0x12eb4  ACCEPT
  ControlsButton cancel;             // +0x130a0  CANCEL
  ControlsButton reset_mouse;        // +0x1328c  CMD_RESET_TO_MOUSE_BTN
  ControlsButton reset_joy;          // +0x13478  CMD_RESET_TO_JSTICK_BTN
  ControlsList commands{ControlsList::kCommands};  // +0x13664  CMD_COMMAND_BTN
  ControlsList key_a{ControlsList::kKeyA};    // +0x13cb4  CMD_KEYA_BTN
  ControlsList key_b{ControlsList::kKeyB};    // +0x14304  CMD_KEYB_BTN
  ControlsList joy{ControlsList::kJoy};       // +0x14954  CMD_JOY_BTN
  ControlsList mouse{ControlsList::kMouse};   // +0x14fa4  CMD_MOUSE_BTN
  GroupCycle group_cycle;            // +0x155f4  CMD_SET_BTN
  ControlsArrow next{ControlsArrow::kNext};      // +0x15a18  CMD_NEXT_CMD_BTN
  ControlsArrow prev{ControlsArrow::kPrev};      // +0x15b64  CMD_PREV_CMD_BTN
  ControlsArrow export_btn{ControlsArrow::kExport};  // +0x15cb0  CMD_EXPORT_BTN
  ControlsArrow import_btn{ControlsArrow::kImport};  // +0x15dfc  CMD_IMPORT_BTN
  ControlsButton assign_axis;        // +0x15f48  CMD_ASSIGN_AXIS_BTN
  ztwod::FontText description{nullptr, 0, 0};  // +0x16134  CMD_DESCRIPTION
  Marquee option_text;               // +0x165e4  OPTIONTEXT (none in the shipped dialog)
  ztwod::Image lines{0};             // +0x166a4  LINES
  AxisPopupState axis_popup;         // +0x16760
  LoadSavePopupState load_save_popup;  // +0x1677c
  ztwod::FontText file_name{nullptr, 0, 0};  // +0x16794  CURRENT_FILENAME
  int32_t mode = 0;                  // +0x16c44  0 idle, capturing 1 key A, 2 key B, 3 joystick, 4 mouse
  float range[8][2] = {};            // +0x16c50  each stick source's lowest and highest reading in mode 3
  uint8_t dirty = 0;                 // +0x16c90  the map changed since the file was read or written

 private:
  Handle self_;
};

// The mouse column's guard ([0x006056f4]): set to 10 frames by a mouse
// button's capture, counted down in mode 0; capture starts again at <= 0.
int32_t& controls_mouse_guard();

// The axis popup's choices (vtable 0x005be4c0, +0x88 0x0042dd20): the
// stick's six axes and the mouse's three (0x00430180).
class AxisCycle : public CycleButton {
 public:
  explicit AxisCycle(Engine& e) : CycleButton(e, -1) {}
  void after_bind() override;        // 0x0042dd20 (with 0x0042df60)
};

// INVERT_CHECKBOX (vtable 0x005be428), 0x16c bytes: a check button whose
// labels end at their POSITION, reporting its press to the screen.
class InvertCheck : public ztwod::CheckButton {
 public:
  void on_left_up() override;        // +0x30  0x0042f8b0
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0042f920
  virtual void notify(int32_t a, int32_t b);  // +0x90  0x0042f8f0: the owner's +0x14
  int32_t id = -1;                   // +0x164
  uint8_t active = 1;                // +0x168
};

// AXIS_VALUE: a slider showing the source's reading (menu_widgets.h).
using AxisSlider = ShellSlider;

// The axis popup (0x0042da80, vtable 0x005be408), 0x12194 bytes.
class AxisPopupScreen : public MenuScreen {
 public:
  AxisPopupScreen(Engine& e, Handle state);  // 0x0042da80
  ~AxisPopupScreen() override = default;  // 0x0042e230 (deleting 0x0042dff0)
  void update(float dt) override;    // +0x00  0x0042e470: the reading shown, 0..1

  // MB_OK (vtable 0x005be608, +0x30 0x0042e5b0): the source bound, centred
  // where it is now, then the state's +0x28.
  class OkButton : public SlideButton {
   public:
    explicit OkButton(Engine& e) : SlideButton(e, -1) {}
    void on_left_up() override;
  };

  OkButton ok;                       // +0x111ec  MB_OK
  StateBackButton cancel;            // +0x113d8  MB_CANCEL
  AxisCycle sources;                 // +0x115c4  CMD_AXIS_CYCLE_BTN
  InvertCheck invert;                // +0x119e8  INVERT_CHECKBOX
  AxisSlider value;                  // +0x11b54  AXIS_VALUE
  ztwod::FontText instructions{nullptr, 0, 0};  // +0x11cdc  INSTRUCTIONS
  zimg::Texture* grab = nullptr;     // +0x1218c  the screen behind, taken on entering
  int32_t axis = 0;                  // +0x12190  (written, never read)
  AxisPopupState* owner = nullptr;   // the state at +0x10fec, as an object
};

// The load/save popup (0x0042e890, vtable 0x005be6d8), 0x11cd4 bytes:
// LOAD_SAVE_POPUP's file name bar over the list of keys\*.zrd; OK loads or
// saves the bar's name through the controls screen (docs/menus.md).
class LoadSavePopupScreen : public MenuScreen {
 public:
  LoadSavePopupScreen(Engine& e, Handle state);  // 0x0042e890
  ~LoadSavePopupScreen() override = default;  // 0x0042eda0 (deleting 0x0042eb50)
  void update(float dt) override { Window::update(dt); }  // +0x00  0x0042efe0
  // 0x0042eff0: the list emptied and filled with the files' names, each its
  // place in the listing as its value. FIX (A): the listing is
  // Engine::list_files (the original's FindFirstFileA never closed its handle).
  void fill();

  // MB_OK (vtable 0x005be8d8, +0x30 0x0042f160): the bar's name loaded or
  // saved by the controls screen (0x0042f380 -> 0x0042d0b0), then the state's +0x28.
  class OkButton : public SlideButton {
   public:
    explicit OkButton(Engine& e) : SlideButton(e, -1) {}
    void on_left_up() override;
  };
  // EDITBAR (vtable 0x005be7a0), 0x390 bytes: a click gives it the focus,
  // Enter or Escape takes it away; the characters a file name cannot have
  // are refused, 19 at most.
  class NameBar : public ShellEditBar {
   public:
    explicit NameBar(Engine& e);
    void on_left_up() override;        // +0x30  0x0042f1b0
    void on_return() override;         // +0x90  0x0042f1a0
    void on_cancel() override;         // +0x94  0x0042f1a0
    uint8_t active = 0;                // +0x38c
  };
  // FILELIST (vtable 0x005be6f8): a choice shows in the bar.
  class Files : public FileList {
   public:
    explicit Files(Engine& e) : FileList(e, -1) {}
    void select(int32_t index) override;  // +0x98  0x0042f1c0
  };

  OkButton ok;                       // +0x111ec  MB_OK
  StateBackButton cancel;            // +0x113d8  MB_CANCEL
  NameBar bar;                       // +0x115c4  EDITBAR
  Files files;                       // +0x11954  FILELIST
  ztwod::Image box{0};               // +0x11b54  LOAD_BOX
  ztwod::Image backdrop{0};          // +0x11c10  the screen behind (the list's first object)
  zimg::Texture* grab = nullptr;     // +0x11ccc  taken on entering
  uint8_t save = 0;                  // +0x11cd0
  LoadSavePopupState* owner = nullptr;  // the state at +0x10fec, as an object
};

}  // namespace mech3
