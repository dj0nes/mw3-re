#pragma once
#include <cstdint>
#include <vector>

#include "gamez/ztwod/twod.h"
#include "mech3/app_hooks.h"

// What the game's menu screens share on top of zTwoD (docs/mainmenu.md): the
// screens' base, the scrolling background, the game's push button, its
// rippling subclass and the scrolling title. Original file unknown: the screen
// base and the button sit between mech3\briefing.cpp and mech3\mech.cpp
// (0x0040a110..0x0040a9f0), the rippling button and the title between
// mech3\mech_anim.cpp and mech3\mech_data.cpp (0x0042a140..0x0042fb30) and
// between mech3\mech_waynet.cpp and mech3\mission.cpp (0x00485510..0x00485650).
// Outside zTwoD they call the options and the app through mech3::Engine.
namespace mech3 {

// The background of the menus (vtable 0x005bc1b0), 0x13c bytes: unless
// FastMenu, the image scrolls by (step_x, step_y) a frame and wraps, drawn as
// up to four pieces cut at its edges.
class ScrollImage : public ztwod::Image {
 public:
  explicit ScrollImage(Engine& e);   // in the screens' constructor 0x0040a110
  void draw() override;              // +0x04  0x0040a490
  void update(float dt) override;    // +0x24  0x0040a210

  int32_t step_x = 1, step_y = 1;    // +0xbc
  uint8_t piece[4] = {};             // +0xc4  draw this piece next (never set by the constructor)
  int32_t piece_pos[4][2] = {};      // +0xc8  where each piece goes; the first at (0, 0)
  ztwod::Rect view;                  // +0xe8  the image's part at the screen's top left
  ztwod::Rect piece_src[4];          // +0xf8
  uint8_t moving = 0;                // +0x138  not FastMenu

 private:
  Engine& e_;
  float steps_ = 0.0f;               // FIX (B): frames of scrolling owed (not in the original)
};

// The main menu's title (MECH3TEXT, vtable 0x005bd988): unless FastMenu it
// slides left `speed` pixels a frame and comes back in at the screen's right.
class Marquee : public ztwod::Image {
 public:
  explicit Marquee(Engine& e);       // in the main menu's constructor 0x00486260
  void update(float dt) override;    // +0x24  0x0042fb30

  float speed = 2.0f;                // +0xbc

 private:
  Engine& e_;
  float steps_ = 0.0f;               // FIX (B): pixels owed (not in the original)
};

// A menu screen (vtable 0x005bc190): a dialog with the shell state that owns
// it, a still image for FastMenu and the BACKGROUND. The main menu's, the
// options' and the other menus' screens derive from it.
class MenuScreen : public ztwod::Dialog {
 public:
  MenuScreen(Engine& e, Handle state);  // 0x0040a110
  ~MenuScreen() override = default;  // 0x0040a570 (deleting 0x0040a550)
  // +0x10 (0x004a5bc0 here, empty): the option screens' refresh (0) from the
  // options and apply (1) to them (docs/menus.md).
  virtual void apply(int32_t to_options) { (void)to_options; }
  // +0x14 (0x0040a510): a button of the screen was pressed; nothing here.
  virtual void on_button(int32_t id, int32_t a, int32_t b) {
    (void)id, (void)a, (void)b;
  }
  virtual uint8_t set_busy(uint8_t on);  // +0x18  0x0040a520: returns the old value
  virtual uint8_t busy() { return busy_; }  // +0x1c  0x0040a540
  Engine& engine() { return e_; }
  // Mech3.exe 1.2 @ 0x00486ed0: everything drawn once without the cursor and
  // grabbed; the grab becomes the static background and the first object (FastMenu).
  void bake();
  // The shell state's +0x28 (its Escape: most pop themselves), as the BACK
  // buttons call it ([screen+0x10fec]->+0x28).
  void state_escape() { e_.vcall(state, 0x28, {}); }
  // The sub-screens' BACKGROUND (inline in 0x004379f0, 0x00438f60, 0x0042a590,
  // 0x004289a0): the main menu's image ([0x00609854]) and its scrolling, or the
  // dialog's own without a main menu (the credits do not check); still and at
  // the origin when the main menu's is still, and (not the credits) while a
  // mission is running: the menus of the pause screen.
  void borrow_background(bool option_screen);

 private:
  Engine& e_;

 public:
  Handle state = 0;                  // +0x10fec
  uint8_t busy_ = 0;                 // +0x10ff0  the buttons take no mouse while set
  ztwod::Image still{0};             // +0x10ff4  FastMenu: the screen drawn once, as one image
  ScrollImage background;            // +0x110b0  BACKGROUND
};

// The game's push button (vtable 0x005bc230), 0x158 bytes: a zTwoD Control
// with an id it reports to its screen when pressed.
class ShellButton : public ztwod::Control {
 public:
  ShellButton(Engine& e, int32_t id);  // 0x0040a5d0
  ~ShellButton() override;           // 0x0040a600
  ztwod::Rect* get_rect() override;  // +0x2c  0x0040a6b0
  void on_left_up() override;        // +0x30  0x0042a3d0: pressed, then tells the screen
  bool accepts(ztwod::Window* w, bool inside) override;  // +0x54  0x0040a720
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0040a680
  virtual void notify(int32_t a, int32_t b);  // +0x8c  0x0040a6e0: the screen's +0x14
  // +0x90: 0x0040a9c0 here, a body folded with another class's destructor;
  // every button that is baked overrides it.
  virtual void set_baked(bool on) { (void)on; }
  virtual void init_ripple() {}      // +0x94  0x004bf620 (it pops one argument)
  virtual void restart_ripple() { dirty(); }  // +0x98  0x0040a9f0

  uint8_t reading = 0;               // +0x14c  inside read_control
  int32_t id;                        // +0x150
  uint8_t borrowed = 0;              // +0x154  the images are not its own (cleared before the Control frees them)

 protected:
  Engine& e_;                        // the outside calls (not a field of the original)
};

// A push button with a ripple around it (vtable 0x005bd888), 0x1ec bytes: on
// rollover a ring spreads from its centre while the ripple fades (twod.md,
// Ripple). Baked (FastMenu) it has no image of its own: the screen grab shows it.
class SlideButton : public ShellButton {
 public:
  SlideButton(Engine& e, int32_t id);  // 0x00438d70
  ~SlideButton() override;           // 0x0042a450
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0042a400: and the ripple with it
  void update(float dt) override;    // +0x24  0x00485650
  ztwod::Rect* get_rect() override;  // +0x2c  0x00485590
  void on_enter() override;          // +0x3c  0x00485570
  void on_leave() override;          // +0x40  0x00485640
  int32_t hit(int32_t px, int32_t py) override;  // +0x5c  0x004855f0
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0042a1d0
  void after_bind() override;        // +0x88  0x0042a250
  void set_baked(bool on) override;  // +0x90  0x0042a160
  void init_ripple() override;       // +0x94  0x00485510
  void restart_ripple() override;    // +0x98  0x00485530

  zimg::Texture* stashed = nullptr;  // +0x158  the normal image while baked
  ztwod::Rect reach;                 // +0x15c  the square RADIUS around the centre (read by nothing here)
  ztwod::Ripple ripple;              // +0x16c
  uint8_t rippling = 0;              // +0x1b8
  float speed = 0.0f;                // +0x1bc  the ring's growth, pixels a second
  float amplitude = 0.0f;            // +0x1c0  7, less 0.3 a frame; it stops at 2
  float wavelength = 0.0f;           // +0x1c4
  float radius_carry = 0.0f;         // ours: the growth's fraction (menu_ripple.h, FIX (B))
  float phase = 0.0f;                // +0x1c8
  int32_t radius = 0;                // +0x1cc
  int32_t max_radius = 0;            // +0x1d0  MAXRADIUS (200)
  float reach_radius = 0.0f;         // +0x1d4  RADIUS (20)
  float reach_sq = 0.0f;             // +0x1d8
  std::vector<ztwod::Object2D*> owned;  // +0x1dc  (HP STL) deleted with the button; empty in the main menu

 protected:
  void read_ripple(zrdr::Node* node);  // the shared tail of +0x84: RADIUS, MAXRADIUS
};

// The game's slider (vtable 0x005bbc90, destructor 0x00406110 with the
// briefing): a click sets it from the mouse. The briefing's progress bar and
// the controls' AXIS_VALUE.
class ShellSlider : public ztwod::Slider {
 public:
  void on_left_up() override { set_from_mouse(); }  // +0x30  0x0059e2f0
  void set_raw(float v) override { set_value(v); }   // +0x8c  0x0059e350
};

// The mech lab's push button (0x0044aaf0, vtable 0x005c0e00; the briefing's
// subclass 0x00406070, vtable 0x005bbd20, differs only in its destructor),
// 0x17c bytes: under the mouse it blinks between its normal and ACTIVATE
// images every 0.05 s and shows its HELP labels. Original file unknown:
// 0x0044aaf0..0x0044af60, in the mech lab's code (with 0x0040a910).
class HelpButton : public ShellButton {
 public:
  HelpButton(Engine& e, int32_t id);  // 0x0044aaf0
  ~HelpButton() override;            // 0x0044ab80: the HELP labels deleted
  void update(float dt) override;    // +0x24  0x0040a910
  void on_enter() override;          // +0x3c  0x0044ae80
  void on_leave() override;          // +0x40  0x0044aee0
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0044ac90
  void set_blink(bool on);           // 0x0044ae10: off shows the image for its state
  void blink(float dt);              // 0x0044af20

  uint8_t selected = 0;              // +0x158  the mech lab's chosen one: no blink, ACTIVATE's image
  uint8_t blinking = 0;              // +0x159
  float period = 0.05f;              // +0x15c
  float timer = 0.0f;                // +0x160
  int32_t glow[2] = {};              // +0x164  GLOW_OFFSET (read by nothing here)
  std::vector<ztwod::Text*> help;    // +0x170  (+0x16c its allocator byte) HELP's labels, in the dialog
};

// A choice stepped through with two arrows (vtable 0x005c5ba0, "cycle button"
// by its destructor's text), 0x424 bytes: TOGGLE_LEFT and TOGGLE_RIGHT are
// controls of their own, the choice shows in the first LABEL. The option
// screens' DIFFICULTY, CAMERA, ON/OFF switches, RESOLUTION and the rest.
// Original file unknown: 0x0048b280..0x0048c050, between mech3\mech_waynet.cpp
// and mech3\mission.cpp; the helpers 0x00438e10.. and 0x0043a710 sit with the
// option screens.
class CycleButton : public ShellButton {
 public:
  struct Item {                      // 8 bytes, new'd
    char* text;                      // +0x00  _strdup'd
    int32_t value;                   // +0x04
  };
  CycleButton(Engine& e, int32_t id);  // 0x0048b280
  ~CycleButton() override;           // 0x0048b380 (deleting 0x0048b360, 0x004898a0)
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0048b9b0: the label and arrows with it
  void update(float dt) override;    // +0x24  0x0048b550: the arrows' rollover
  void on_left_up() override;        // +0x30  0x0048b6b0
  void on_drag(int32_t, int32_t) override {}  // +0x4c  0x0048b940
  int32_t hit(int32_t px, int32_t py) override;  // +0x5c  0x0048b950
  void show_for(float seconds) override;  // +0x60  0x0048b4e0
  void hide() override;              // +0x64  0x0048b520
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0048be70
  void after_bind() override;        // +0x88  0x0048bf60; each kind adds its choices
  void notify(int32_t a, int32_t b) override;  // +0x8c  0x0042fbc0: the owner's +0x14 with the cycle id
  // +0x90 (0x0048b490): enables (not bakes) the button and both arrows.
  void set_baked(bool on) override;
  void set_enabled(int32_t on);      // the same slot with its int argument
  void set_arrows(uint8_t prev_on, uint8_t click_on, uint8_t next_on);  // +0x94  0x0048bd40
  void set_arrows(uint8_t prev_on, uint8_t next_on);  // +0x98  0x0048bcf0
  virtual void set_label_font(Handle font);  // +0x9c  0x0048ba80
  virtual void select(int32_t i);    // +0xa0  0x0048b850: shown in the first label
  virtual void step(int32_t by);     // +0xa4  0x0048b790: wraps or stops at the ends

  int32_t add_item(const char* text, int32_t value);  // 0x0048bac0: the old count
  void clear_items();                // 0x0048bda0
  Item* item(int32_t i);             // 0x00438e10: null out of range
  int32_t value_at(int32_t i);       // 0x00438e50: 0 out of range
  int32_t select_value(int32_t v);   // 0x0048b8f0: the first with v, or -1
  void select_each_value(int32_t v);  // 0x0043a710: every one with v (the last stays)
  int32_t count() const { return static_cast<int32_t>(items.size()); }  // 0x004fd1c0
  int32_t selected_value() { return item(index) ? item(index)->value : 0; }  // the screens' inline read

  std::vector<Item*> items;          // +0x15c  (+0x158 its allocator byte)
  uint8_t click_steps = 0;           // +0x168  a press off the arrows steps forward too
  int32_t index = -1;                // +0x16c
  uint8_t wraps = 1;                 // +0x170
  int32_t hover = 0;                 // +0x174  1 over the right arrow, 2 the left
  int32_t prev_dx = 0, prev_dy = 0;  // +0x178  TOGGLE_LEFT's place from the button
  int32_t next_dx = 0, next_dy = 0;  // +0x180  TOGGLE_RIGHT's
  ztwod::Control prev;               // +0x188  TOGGLE_LEFT
  ztwod::Control next;               // +0x2d4  TOGGLE_RIGHT
  int32_t cycle_id;                  // +0x420  reported with a press
};

// A cycle whose one choice (alert) shows in a second font (0x00438090, vtables
// 0x005bf6e8, 0x005bf3b0, 0x005bf308, 0x005bf260), 0x430 bytes: OFF / ON with
// fonts.zrd's ia_small_green and, for `alert`, ia_small_red.
class AlertCycle : public CycleButton {
 public:
  AlertCycle(Engine& e, int32_t alert_index) : CycleButton(e, -1), alert(alert_index) {}
  void after_bind() override;        // +0x88  0x00437fe0
  void select(int32_t i) override;   // +0xa0  0x00438040

  Handle font_normal = 0;            // +0x424
  Handle font_alert = 0;             // +0x428
  int32_t alert;                     // +0x42c
};

// The ON / OFF cycle (+0x88 0x00438ed0): OFF 0, ON 1.
class OnOffCycle : public CycleButton {
 public:
  explicit OnOffCycle(Engine& e) : CycleButton(e, -1) {}
  void after_bind() override;        // 0x00438ed0
};

// The LOW / MEDIUM / HIGH cycle (+0x88 0x00438f10): values 0, 1, 2.
class LevelCycle : public CycleButton {
 public:
  explicit LevelCycle(Engine& e) : CycleButton(e, -1) {}
  void after_bind() override;        // 0x00438f10
};

// ---- the file list (docs/menus.md, The file list) ----
// The list of names the controls' load/save popup, the pilot screen, the mech
// lab and the pause screen pick from. Original file unknown: 0x0048bfe0..
// 0x0048d770 and 0x0048e4e0..0x0048e630, between mech3\mech_waynet.cpp and
// mech3\mission.cpp, by the cycle button.

class FileList;

// A row (vtable 0x005c5c48), 0x4d8 bytes: a Text showing an item, with an
// object that hears its rollover (+0x4cc; none in the popup).
class FileRow : public ztwod::Text {
 public:
  FileRow(FileList* list, Handle font);  // 0x0048bfe0
  void update(float dt) override;    // +0x24  0x0048c070
  ztwod::Rect* get_rect() override { return &fixed_rect; }  // +0x2c  0x0048e540
  void on_left_up() override;        // +0x30  0x0048e520: the list's +0x98 with its index
  void on_enter() override;          // +0x3c  0x0048e550
  void on_leave() override;          // +0x40  0x0048e560
  void flash(bool on, Handle font);  // 0x0048c0b0: in the font's colour, or back

  ztwod::Object2D* attached = nullptr;  // +0x4cc
  int32_t index = 0;                 // +0x4d0  the item shown (not set by the constructor)
  FileList* list;                    // +0x4d4
};

// UP and DOWN (vtables 0x005c5e30, 0x005c5da0), 0x15c bytes: a press scrolls
// the list a row, enabled or not (the list asks first).
class FileListArrow : public ShellButton {
 public:
  FileListArrow(Engine& e, FileList* l, int32_t step) : ShellButton(e, -1), list(l), by(step) {}
  void on_left_up() override;        // +0x30  0x0048e4e0 (up, -1), 0x0048e500 (down, +1)
  void after_bind() override { flags &= ztwod::kHidden; }  // +0x88  0x00456800

  FileList* list;                    // +0x158
  int32_t by;                        // which of the two (the vtable in the original)
};

// The list (vtable 0x005c5cf8), 0x200 bytes: SIZE rows of the items from the
// one at `top`, the selected one shown in its own row and font over a BAR
// image, ITEMROLLOVER's image under the row the mouse is on; UP and DOWN
// scroll, repeating while held. The game adds the items.
class FileList : public ShellButton {
 public:
  struct Item {                      // 8 bytes, new'd (freed by 0x0044ed90)
    char* text;                      // +0x00  _strdup'd
    int32_t value;                   // +0x04
  };
  FileList(Engine& e, int32_t id);   // 0x0048c0e0
  ~FileList() override;              // 0x0048c250 (deleting 0x0048c230)
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0048d2c0
  void update(float dt) override;    // +0x24  0x0048c910
  ztwod::Rect* get_rect() override { return nullptr; }  // +0x2c  0x004d72a0
  void on_left_up() override;        // +0x30  0x0048d6a0
  void on_right_up() override;       // +0x34  0x0048d750
  void on_enter() override;          // +0x3c  0x0048d4a0
  void on_leave() override;          // +0x40  0x0048d4c0
  void on_drag(int32_t px, int32_t py) override;  // +0x4c  0x0048d3e0: the row under the mouse lit
  int32_t hit(int32_t px, int32_t py) override;   // +0x5c  0x0048d210
  void show_for(float seconds) override;  // +0x60  0x0048c880
  void hide() override;              // +0x64  0x0048c8c0
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0048c410
  void after_bind() override;        // +0x88  0x0048c7f0
  virtual bool can_light(int32_t) { return true; }      // +0x90  0x0040bad0
  virtual bool can_choose(int32_t) { return true; }     // +0x94  0x0040bad0 (asked by nothing here)
  virtual void select(int32_t index);  // +0x98  0x0048d0d0: -1 none; scrolled into view
  virtual bool can_roll(int32_t) { return true; }       // +0x9c  0x0040bad0: the rollover image
  virtual void set_row_font(int32_t index, Handle font);  // +0xa0  0x0048d1c0
  virtual void layout();             // +0xa4  0x0048cda0

  int32_t add_item(const char* text, int32_t value);  // 0x0042fbf0: the old count
  void clear_items();                // the inline frees of 0x0048c250 and 0x0042eff0
  void select_index(int32_t index) { select(index); }  // 0x0048d0c0
  void scroll(int32_t by);           // 0x0048d160: top within 0..items
  void place_at(ztwod::Image* o);    // 0x0048d2f0: at o, the rectangle its image's
  void set_rows(int32_t n);          // 0x0048d770
  FileRow* row_at(int32_t px, int32_t py);  // 0x0048d540
  FileRow* row_under_mouse();        // 0x0048d500: at the dialog's cursor
  bool over_up(int32_t px, int32_t py);    // 0x0048d600: shown and enabled
  bool over_down(int32_t px, int32_t py);  // 0x0048d650
  void stop_sound(int32_t i);        // 0x0048d380
  void play_sound(int32_t i, float volume);  // 0x0048d3b0
  int32_t item_count() const { return static_cast<int32_t>(items.size()); }

  // +0x158 the vector's allocator byte (the constructor stores its id's low byte)
  std::vector<Item*> items;          // +0x15c
  std::vector<FileRow*> rows;        // +0x16c  (+0x168 its allocator byte)
  FileRow* selected_row = nullptr;   // +0x178  SELECTED_FONT
  int32_t selected = -1;             // +0x17c
  int32_t top = 0;                   // +0x180  the first item shown
  int32_t size = 8;                  // +0x184  SIZE: rows
  ztwod::Rect rect;                  // +0x188  where the rows are (hit)
  int32_t spacing_x = 0, spacing_y = 12;  // +0x198  SPACING (x for every row, y a row)
  int32_t offset_x = 0, offset_y = 0;     // +0x1a0  OFFSET: the first row from the button
  Handle font_normal = 0;            // +0x1a8  NORMAL_FONT
  Handle font_selected = 0;          // +0x1ac  SELECTED_FONT, else NORMAL_FONT
  Handle font_rollover = 0;          // +0x1b0  FONT (with ITEMROLLOVER)
  int32_t bar_dx = 0, bar_dy = 0;    // +0x1b4  BAR's OFFSET from the selected row
  int32_t roll_dx = 0, roll_dy = 0;  // +0x1bc  ITEMROLLOVER's
  ztwod::Image* roll_image = nullptr;  // +0x1c4  ITEMROLLOVER's BITMAP
  ztwod::Image* bar_image = nullptr;   // +0x1c8  BAR's BITMAP
  int32_t sounds[2] = {};            // +0x1cc  ROLLOVER_SOUND, SELECT_SOUND (played: none)
  int32_t playing[2] = {};           // +0x1d4
  int32_t arrow_lit = 0;             // +0x1dc  1 UP, 2 DOWN under the mouse
  FileListArrow* up = nullptr;       // +0x1e0  UP
  FileListArrow* down = nullptr;     // +0x1e4  DOWN
  uint8_t inside = 0;                // +0x1e8
  FileRow* lit = nullptr;            // +0x1ec  the row under the mouse
  float repeat_timer = 0.5f;         // +0x1f0
  float repeat_delay = 0.5f;         // +0x1f4  before an arrow held repeats
  float repeat_rate = 0.15f;         // +0x1f8  then a step each
  uint8_t repeating = 0;             // +0x1fc
  uint8_t no_repeat = 0;             // +0x1fd  (nothing sets it)
};

// The mech lab's list of a store's items (0x0048d880, vtable 0x005c5ec0),
// the briefing's REALLOC_LIST and the debriefing's SALVAGE_LIST: the items
// by kind (mechs, weapons, ammo, equipment), a header for each with +0x209,
// the tonnage under it with +0x20d (campaign.md section 9).
class StoreList : public FileList {
 public:
  StoreList(Engine& e, Handle store, int32_t kinds);  // 0x0048d880
  ~StoreList() override = default;   // 0x0048d930: the title, then the list
  void update(float dt) override;    // +0x24  0x0048df90: the title with the list's +0x20d
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0048db30
  void after_bind() override;        // +0x88  0x0048db70
  bool can_light(int32_t i) override;   // +0x90  0x0048df00: an item with a value
  bool can_choose(int32_t i) override { return can_light(i); }  // +0x94  0x0048df50
  void layout() override;            // +0xa4  0x0048ccf0: the rows of items without value in TITLE_FONT
  virtual void refill();             // +0xa8  0x0048dbe0: the store's items as rows by kind
  void add_row(const struct ConfigItem* item, int32_t kind, Handle previous, int32_t* found);  // 0x0048d9a0
  Handle selected_value();           // 0x0048d950: the selected item's value, 0 none
  uint8_t kinds_or_all(int32_t k) const { return k < 4 ? kinds[k] : all; }  // +0x204 + kind

  Handle x200 = 0;                   // +0x200  a filter on the items (a function; 0 none)
  uint8_t kinds[4] = {};             // +0x204  which kinds it lists (0x0048df60; 5: all)
  uint8_t all = 0;                   // +0x208
  uint8_t x209 = 1, x20a = 1, x20b = 0, x20c = 0, x20d = 0;  // +0x209..+0x20d (0x0040a780..0x0040a7c0)
  int32_t x210 = 3;                  // +0x210  the techs listed (weapons' and equipment's)
  Handle store;                      // +0x214  (0x0040a760 sets it, then refills)
  int32_t x218 = 0;                  // +0x218
  ztwod::FontText title{nullptr, 0, 0};  // +0x21c
  Handle title_font = 0;             // +0x6cc  STATIC_FONT
};

// An edit bar of the game's screens (the controls file's, the pilot's, the
// mech lab's): its first label follows the bar (0x0048b1d0, 0x0048b220;
// destructor 0x00487860 / 0x00487870, between mech3\mech_waynet.cpp and
// mech3\mission.cpp), and while it reads its entry it has no rectangle
// unless FastMenu (0x0042f860, 0x0042f890, with the controls screen).
class ShellEditBar : public ztwod::EditBar {
 public:
  explicit ShellEditBar(Engine& e) : e_(e) {}
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0048b220
  ztwod::Rect* get_rect() override;  // +0x2c  0x0042f890
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0042f860
  void after_bind() override;        // +0x88  0x0048b1d0: the label's place kept

  // +0x380: the first label's place after binding; set_pos adds it to the new
  // place (an absolute place, so a moved bar's label lands that much further).
  int32_t label_x = 0, label_y = 0;
  uint8_t reading = 0;               // +0x388  set while reading, unless FastMenu

 protected:
  Engine& e_;
};

}  // namespace mech3
