#pragma once
#include <cstdint>
#include <new>
#include <vector>

#include "mech3/controls_menu.h"
#include "mech3/debriefing.h"
#include "mech3/lab_items.h"
#include "mech3/menu_widgets.h"

// The mech lab's screen (docs/mechlab.md section 2): the Lab State's
// (new(0x1b468), 0x0044b920), dlgMechLabII.zrd's LABMENU. Original file
// unknown: 0x00448830..0x004561ff, between mech_data.cpp and mech_map.cpp.
// Step 2 of mechlab.md section 5: the screen, its own controls and the page
// and slot switches. The pages' controls are made as the original makes
// them; what they read, lay out and show is theirs, hooked until their steps
// (Engine::unported "lab.vt+<slot>" with the control's offset).
namespace mech3 {

class LabScreen;

// N objects made in place in order and destroyed in reverse, as the
// original's eh vector constructor (0x005ac2a0) and destructor (0x005ac160).
template <typename T, int N>
class InPlace {
 public:
  template <typename... A>
  explicit InPlace(A&... args) {
    for (int i = 0; i < N; ++i) new (&buf_[i]) T(args...);
  }
  ~InPlace() {
    for (int i = N - 1; i >= 0; --i) (*this)[i].~T();
  }
  InPlace(const InPlace&) = delete;
  InPlace& operator=(const InPlace&) = delete;
  T& operator[](int i) { return *reinterpret_cast<T*>(&buf_[i]); }
  static constexpr int size() { return N; }

 private:
  struct alignas(T) Slot { unsigned char b[sizeof(T)]; };
  Slot buf_[N];
};

// A control whose reading and showing belong to a later step: its calls go
// out as "lab.vt+<slot>" with its offset in the screen.
struct LabLater {
  Engine* e = nullptr;
  uint32_t at = 0;                   // the offset in the original's screen
  void call(uint32_t slot, std::initializer_list<Arg> args = {}) const;
  void call_fn(const char* key, std::initializer_list<Arg> args = {}) const;  // a function of its own
  uint32_t ask(const char* key) const;  // the same, its answer
};

// PANEL_LEFT, PANEL_RIGHT, PANEL_CENTER (0x0044b010, vtable 0x005c0e98): an
// image with three looks, "single", "double" and "double2" (0x0044b100).
class LabPanel : public ztwod::Image {
 public:
  LabPanel() : ztwod::Image(0) {}    // 0x0044b010
  ~LabPanel() override;              // 0x0044b070: "double2"'s is not given back
  bool read(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x78  0x0044b100
  void show_look(int32_t look);      // 0x0044b180: 1 single, 2 double, 3 double2

  zimg::Texture* single = nullptr;   // +0xbc
  zimg::Texture* twice = nullptr;    // +0xc0  "double"
  zimg::Texture* twice2 = nullptr;   // +0xc4  "double2"
};

// The lab's push buttons (vtable 0x005c1630; 0x0044f260, or 0x0040a5d0 with
// the vtable set after): CONFIG_*, SORT_CHASSIS*, the engine's.
using LabButton = ShellButton;

// CONFIG_EDIT (vtable 0x005c14f0): the bar placed at its label's offset,
// the label where the bar is asked to go (0x0044a710).
class LabEditBar : public ShellEditBar {
 public:
  explicit LabEditBar(Engine& e) : ShellEditBar(e) { set_size(0x11); }  // 0x005a4ec0, 0x005a5280(0x11)
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0044a710
  void on_return() override;         // +0x90  0x0044a6e0: the name taken
  void on_cancel() override;         // +0x94  0x0044f4b0
  // 0x0044a2f0: the list's chosen loadout's name typed over its row
  // (`rename` true: RENAME; false: SAVE's copy under a taken name).
  void start(class LabConfigList& list, bool rename);
  bool finish(bool cancel);          // 0x0044a3a0: false while the bar stays up
  uint32_t x38c = 0;                 // +0x38c  the loadout being named (0: none)
  uint8_t x390 = 0;                  // +0x390  renaming (1) or naming SAVE's copy (0)
};

// SKIN_LIST (vtable 0x005c1448): the player's (slot 0's) skin, whichever
// slot is shown.
class LabSkinList : public CycleButton {
 public:
  explicit LabSkinList(Engine& e) : CycleButton(e, -1) {}
  void select(int32_t i) override;   // +0xa0  0x0044b1c0
};

// The lab's check buttons (PART_*, SELECT_*): CheckButton's reading
// (0x00456980), their labels shown only while unchecked (0x0044f2c0).
class LabCheck : public InvertCheck {
 public:
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override {  // +0x84  0x00456980
    return ztwod::CheckButton::read_control(node, dialog);
  }
  int32_t set_checked(int32_t on) override;  // +0x8c  0x0044f2c0
};

// PART_HEAD..PART_RLEG (vtable 0x005c13b0; PART_HEAD made by 0x0044ed40(0x12)
// and the rest by 0x0044f290, all the same class after): check buttons with
// id 0x12 and the part's number at +0x16c. Their press is step 6's.
class LabPart : public LabCheck {
 public:
  LabPart() { id = 0x12; }
  // +0x30 0x0044f340: a checked one unchecks and reports none (0) first;
  // then its part, to the screen's 0x12 (ASSIGNED_LIST's choice).
  void on_left_up() override;
  int32_t part = 0;                  // +0x16c
};

// LOADOUT_LIST (0x0044b210, vtable 0x005c0f18): a FileList of the shown
// loadout's contents, none of its rows lit or chosen (0x0044f4e0).
class LabLoadoutList : public FileList {
 public:
  explicit LabLoadoutList(Engine& e) : FileList(e, -1) {}
  bool can_light(int32_t) override { return false; }   // +0x90  0x0044f4e0
  bool can_choose(int32_t) override { return false; }  // +0x94  0x0044f4e0
  // 0x0044b230: the weapons (with their ammunition), a blank, the heat
  // sinks, then the equipment but armour and sinks; none for no loadout.
  using FileList::refresh;  // the base's refresh() stays (ours: not hidden)
  void refresh(Handle cfg);
};

// CHASSIS_LIST (0x004490d0, vtable 0x005c0cc0): the lab store's mechs, each
// with its tonnage; for a lancemate in a campaign "none" first.
class LabChassisList : public StoreList {
 public:
  explicit LabChassisList(Engine& e) : StoreList(e, 0, 0) { x209 = 0; }
  ~LabChassisList() override;        // 0x00449180: the rows' attachments deleted
  bool can_light(int32_t) override { return true; }   // +0x90  0x0045b6e0
  bool can_choose(int32_t) override { return true; }  // +0x94  0x0045b6e0
  void select(int32_t index) override;  // +0x98  0x00449b30: the slot's chassis
  void layout() override;            // +0xa4  0x0044f4c0: StoreList's, then the tonnages 0x00449760
  void refill() override;            // +0xa8  0x00449250
  void set_store(Handle s);          // 0x00449220: no choice, the store's mechs
  void set_sort(int32_t by);         // 0x00449160: 0 SORT_CHASSISNAME, 1 SORT_CHASSISMASS
  int32_t find_mech(int32_t mech);   // 0x00449a80: its row, 0 when not there
  bool select_mech(int32_t mech);    // 0x00449b00
  int32_t mech_at(int32_t index);    // the row's item's mech, -1 for none (the inline reads)
  int32_t sort = 0;                  // +0x6d0
};

// CONFIG_LIST (vtable 0x005c1588): the shown slot's mech's loadouts, those
// the lab store cannot fill marked (message 0x23e); CONFIG_ADD (never
// enabled), CONFIG_DELETE, CONFIG_RENAME, CONFIG_SAVE (ids 13, 15, 14, 16)
// inside it.
class LabConfigList : public FileList {
 public:
  explicit LabConfigList(Engine& e)
      : FileList(e, -1), add(e, 0xd), del(e, 0xf), rename(e, 0xe), save(e, 0x10) {}
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0044f390
  void show_for(float seconds) override;  // +0x60  0x0044f400
  void hide() override;              // +0x64  0x0044f460
  void select(int32_t index) override;  // +0x98  0x00449c40: the slot's loadout
  void fill();                       // 0x00449fb0
  void select_config(Handle config); // 0x00449d40: its row by name, none when modified
  void enable_buttons();             // 0x0044a1f0
  Handle value_at(int32_t i) const;  // 0x0044edc0: a row's loadout, 0 out of range
  LabButton add;                     // +0x200
  LabButton del;                     // +0x358
  LabButton rename;                  // +0x4b0
  LabButton save;                    // +0x608
};

// SELECT_WEAPON/AMMO/EQUIP/ARMOR in the unassigned list (vtable 0x005c1f48):
// InvertCheck's press, reporting its id to the screen.
class LabSelect : public LabCheck {
 public:
  explicit LabSelect(int32_t i) { id = i; }
};

// ASSIGNED_LIST (0x004552e0, vtable 0x005c1fe0; the base's 0x22c bytes and
// +0x22c): the shown loadout (+0x224) by part, each part a row (its object
// the part's number, 1..8) with its fixed slots (cockpit, engine, gyro,
// actuators...), ferro-fibrous and endo steel, then the loadout's items on
// it; a part row shows "used/total". Choosing a part row picks the part
// (the screen's buttons, the 3D view, the unassigned list's rules); an
// item row takes the item back off to the lab's store (docs/mechlab.md,
// The criticals page).
class LabAssignedList : public LabItemList {
 public:
  explicit LabAssignedList(Engine& e) : LabItemList(e) {}  // 0x004552e0
  int32_t add_text(const char* text, int32_t n) override;  // +0x90  0x00455bf0: STATIC_FONT
  int32_t add_object(Handle item, int32_t n) override;     // +0x94  0x00455b60
  int32_t add_part(int32_t part) override;                 // +0x98  0x00455af0
  void scroll(int32_t by) override;  // +0x9c  0x004559e0: the next or previous part
  bool can_light(int32_t index) override;  // +0xa4  0x00455f30
  void select(int32_t index) override;     // +0xac  0x00455fb0
  bool can_roll(int32_t index) override { return !is_part_row(index); }  // +0xb0  0x00455f90
  void refill() override;            // +0xb8  0x004554d0
  bool is_part_row(int32_t row) const;   // 0x00455d70: its object a part's number
  bool is_item_row(int32_t row) const;   // 0x00455df0
  Handle item_at_row(int32_t row) const;  // 0x00455e50
  int32_t part_of_row(int32_t row) const;  // 0x00455ec0: -1 not a part row
  int32_t row_of_part(int32_t part) const;  // 0x004560d0: -1 none
  int32_t part() const { return part_of_row(selected); }  // 0x00455eb0
  void choose(int32_t part);         // 0x00456130: the part's row pressed
  void pick(int32_t part);           // 0x00456150: the part chosen (no refill)
  void show_slots();                 // 0x00455c50: each part row's used and total
  bool editable() const;             // the loadout the player's (+0x1c) and the lab not read-only
  // 0x004553f0: a part's ferro-fibrous and endo steel rows (from the two
  // counts left) and its items.
  void add_part_items(bool spread, int32_t* ferro, int32_t* endo);

  int32_t listing = 0;               // +0x22c  the part being listed (not set by the constructor)
};

// UNASSIGNED_LIST (0x004535d0, vtable 0x005c1d68), 0xeac bytes: the lab
// store's items of one tab (SELECT_WEAPON 25 kind 1, SELECT_AMMO 23 kind 2,
// SELECT_EQUIP 24 kind 3, SELECT_ARMOR 26 kind 3's armour), sorted by
// SORT_ITEM/QTY/MASS/CRIT (27..30), each row greyed when it cannot go on
// the mech (+0xa8, 0x00453840), with its mass and how many the store has.
// Its +0xac puts the row's item on (0x00454600, `lab_put_on`).
class LabUnassignedList : public LabItemList, public LabLater {
 public:
  explicit LabUnassignedList(Engine& e);  // 0x004535d0
  void show_for(float seconds) override;  // +0x60  0x00454db0
  void hide() override;              // +0x64  0x00454e70
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x00454fc0
  int32_t add_object(Handle item, int32_t n) override;  // +0x94  0x00454f60: NORMAL_FONT
  bool can_light(int32_t index) override { return can_xa8(index); }  // +0xa4  0x00453830
  bool can_xa8(int32_t index) override;  // +0xa8  0x00453840: may the row's item go on the shown loadout
  void select(int32_t index) override { e_.lab_put_on(*this, index); }  // +0xac  0x00454600 (lab_put_on)
  void refill() override;            // +0xb8  0x00453f20
  void set_tab(int32_t kind, bool armor);  // 0x004550d0
  void set_sort(int32_t by);         // 0x00453f00: 0 id, 1 mass, 2 count, 3 slots
  bool call_bool(uint32_t slot, int32_t index) const;

  int32_t short_size[2] = {};        // +0x22c  SHORT_SIZE: the equipment and armour tabs'
  int32_t long_size[2] = {};         // +0x234  SIZE kept
  int32_t down_dy = 0;               // +0x23c  DOWN below the rows
  LabSelect weapon{0x19};            // +0x244
  LabSelect ammo{0x17};              // +0x3b0
  LabSelect equip{0x18};             // +0x51c
  LabSelect armor{0x1a};             // +0x688
  ShellButton sort_item;             // +0x7f4  (vtable 0x005c1eb8)
  ShellButton sort_qty;              // +0x94c
  ShellButton sort_mass;             // +0xaa4
  ShellButton sort_crit;             // +0xbfc
  uint8_t armor_tab = 0;             // +0xd54
  int32_t kind = 1;                  // +0xd58  the tab's item kind
  int32_t sort = 0;                  // +0xd5c
  ztwod::Control titles;             // +0xd60  ALIST_TITLES (vtable 0x005c1e28): the slots left
};

// A weapon row's location (vtable 0x005c16c0, 0x5d4 bytes): a dark panel
// with the part's name right-aligned in it.
class LabLocationTag : public HudPanel {
 public:
  LabLocationTag() = default;        // 0x0043a7e0, 0x005a15a0
  ~LabLocationTag() override = default;  // 0x0044e380 (deleting 0x0044e360)
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0044dfb0: the text 5 inside the right edge
  void update(float dt) override;    // +0x24  0x0044dff0: shown: the panel, then the text
  ztwod::Text text;                  // +0x108
};

// WEAPONS_UNGROUPED (vtable 0x005c1300) and WEAPONS_GROUPED (0x005c1250):
// the shown loadout's weapons of no group, and the five groups' headers
// (their number, the row's value 0..4) each with its weapons (docs/mechlab.md,
// The weapon groups page). A weapon pressed in the first joins the group
// chosen in the second; one pressed in the second leaves its group.
class LabWeaponList : public FileList {
 public:
  LabWeaponList(Engine& e, bool g) : FileList(e, -1), grouped(g) {}
  ~LabWeaponList() override;         // 0x0044dee0: the rows' tags deleted (deleting 0x0044c5c0, 0x0044c5e0)
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x0044df70: STATIC_FONT
  bool can_choose(int32_t i) override;  // +0x94  the grouped's 0x0044e560: a group's row
  void select(int32_t index) override;  // +0x98  0x0044e3f0 / 0x0044e490
  bool can_roll(int32_t i) override;    // +0x9c  the grouped's 0x0044e540: not a group's row
  void layout() override;            // +0xa4  0x0044f240: FileList's, then 0x0044e020
  virtual Handle weapon_at(int32_t row) const;  // +0xa8  0x0044e450 / 0x0044e5e0
  int32_t group_of(int32_t row) const;  // 0x0044e630: a group's row's group, else -1
  void select_group(int32_t g);      // 0x0044e4e0: the group's row selected (out of 0..4: the first)
  void add_weapon(Handle item);      // 0x0044e3d0 / 0x0044e5c0: its label (vt+0x1c)
  void add_group(int32_t g);         // 0x0044e580: message 0x24b with its number
  void lay_rows();                   // 0x0044e020: the rows placed, the weapons' tags

  bool grouped;
  Handle font_static = 0;            // +0x200  STATIC_FONT (the groups' rows)
};

// A row of a data table (vtable 0x005c18b8), 0x960 bytes: a label and its value.
class LabRow : public ztwod::FontText {
 public:
  LabRow() : ztwod::FontText(nullptr, 0, 0) {}
  void update(float dt) override;    // +0x24  0x0044f190: the value too
  ztwod::FontText value{nullptr, 0, 0};  // +0x4b0
  // 0x0044ef50: the value "%d", or "%d %s" with a unit.
  void set_int(int32_t v, const char* unit);
  // 0x0044ef00: the label too (its text as the format).
  void set_int(const char* label, int32_t v, const char* unit);
  // 0x0044ee80: the label and "%.1f" of v; with a unit the format is " %s",
  // so the unit is never shown (and the number gets a leading space).
  void set_float(const char* label, float v, const char* unit);
  // 0x00457f90: the value alone, as set_float's.
  void set_float(float v, const char* unit);
};

// The lab's data tables (0x004569c0, vtable 0x005c20a0), 0x188 bytes: rows
// of a label and a value from POSITION, by OFFSET, SPACING and WIDTH
// (docs/mechlab.md, The data tables).
class LabTable : public ztwod::Control {
 public:
  LabTable() = default;              // 0x004569c0
  ~LabTable() override;              // 0x0044f530: the rows deleted
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x00456c30
  void update(float dt) override;    // +0x24  0x00456bc0: the rows while shown
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x00456a60
  void add_rows(int32_t n);          // 0x0044f040: up to n rows
  Handle font_label = 0;             // +0x14c  LABEL_FONT
  Handle font_value = 0;             // +0x150  VALUE_FONT
  Handle font_alt = 0;               // +0x154  ALTERNATE_FONT
  int32_t width = 0x82;              // +0x158  WIDTH: the values' column
  int32_t offset[2] = {5, 12};       // +0x15c  OFFSET: the first row
  int32_t spacing[2] = {0, 14};      // +0x164  SPACING (its x is not stepped)
  int32_t pos[2] = {};               // +0x16c  POSITION (not set by the constructor)
  std::vector<LabRow*> rows;         // +0x178 (+0x184 points at it)
};

// ENGINE_TABLE (vtable 0x005c1130), 0x6e8 bytes: seven rows (five used) and
// the engine's buttons: faster and slower (19, 20) beside the rating, and
// the engine type's two (21, 22) beside it.
class LabEngineTable : public LabTable {
 public:
  explicit LabEngineTable(Engine& e);  // 0x004569c0, the buttons, 0x0044f040(7)
  bool read_control(zrdr::Node*, ztwod::Dialog*) override;  // +0x84  0x0044b830
  void show_for(float seconds) override;  // +0x60  0x0044b770
  void hide() override;              // +0x64  0x0044b7e0
  using LabTable::refresh;  // the base's refresh() stays (ours: not hidden)
  void refresh(Handle config);       // 0x0044b4b0
  Engine& e_;
  LabButton faster;                  // +0x188  id 0x13, TOGGLE_RIGHT
  LabButton slower;                  // +0x2e0  id 0x14, TOGGLE_LEFT
  LabButton type_right;              // +0x438  id 0x15, TOGGLE_RIGHT
  LabButton type_left;               // +0x590  id 0x16, TOGGLE_LEFT
};

// MECH_DATA's ENDOSTEEL (vtable 0x005c21c0): the internal structure,
// Standard (0) or Endo Steel (1). A step changes the shown loadout's
// (0x00457160; docs/mechlab.md, The data panels).
class LabEndoCycle : public CycleButton {
 public:
  explicit LabEndoCycle(Engine& e) : CycleButton(e, -1) {}
  void step(int32_t by) override;    // +0xa4  0x00457160
  void show_config(Handle config);   // 0x00457130: the loadout's choice
};

// MECH_DATA (0x00456cd0, vtable 0x005c2130, new(0x5ac)): four rows, the
// run and walk speeds, the loadout's tons and the mech's most; ENDOSTEEL.
class LabMechData : public LabTable {
 public:
  explicit LabMechData(Engine& e);
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x004570a0
  void show_for(float seconds) override;  // +0x60  0x004572e0
  void hide() override;              // +0x64  0x00457310
  using LabTable::refresh;  // the base's refresh() stays (ours: not hidden)
  void refresh(Handle config);       // 0x00457330
  Engine& e_;
  LabEndoCycle endo;                 // +0x188
};

// CHASSIS_DATA (0x00457600, vtable 0x005c2270, new(0x188)): ten rows, the
// loadout's tons by kind (0x00425300's 0..7), the total and the mech's most.
class LabChassisData : public LabTable {
 public:
  explicit LabChassisData(Engine& e);
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x00457e80: half a row before the last two
  using LabTable::refresh;  // the base's refresh() stays (ours: not hidden)
  void refresh(Handle config);       // 0x00457940
  Engine& e_;
};

// ARMOR_TITLES' ARMOR_REMOVE (vtable 0x005c1080): half a ton of armour back
// to the store; its texts show the tons.
class LabArmorRemove : public ShellButton {
 public:
  explicit LabArmorRemove(Engine& e) : ShellButton(e, -1) {}
  void on_left_up() override;        // +0x30  0x00450cc0 (step 8b3)
  void set_tons(float tons);         // 0x00450bd0: the second normal, rollover and disabled texts
};

// ARMOR_TITLES' ARMOR_TOGGLE (0x0044fab0, vtable 0x005c1968): one section's
// armour points, with the text of its most (none for the rear three).
class LabArmorToggle : public CycleButton {
 public:
  explicit LabArmorToggle(Engine& e) : CycleButton(e, -1) {}
  void step(int32_t by) override;    // +0xa4  0x004509b0 (step 8b3)
  // 0x00450840: bound to ARMOR_TOGGLE and placed.
  void place(ztwod::Dialog* dialog, int32_t section, ztwod::Text* most, int32_t px, int32_t py);
  using CycleButton::refresh;  // the base's refresh() stays (ours: not hidden)
  void refresh(Handle config);       // 0x00450880: its one choice the points
  int32_t section = 0;               // +0x424  a part (0..10; 3..5 the rear torsos)
  ztwod::Text* most = nullptr;       // +0x428
};

// ARMOR_TITLES' ARMOR_DISTRIBUTE (0x0044fad0, vtable 0x005c1a10): the armour
// spread anew over the sections; enabled while points are unassigned.
class LabArmorDistribute : public ShellButton {
 public:
  explicit LabArmorDistribute(Engine& e) : ShellButton(e, -1) {}
  void on_left_up() override;        // +0x30  0x00450c70 (step 8b3)
};

// ARMOR_TITLES (vtable 0x005c0ff0, new(0x31e0), made inline in the screen's
// constructor): the armour page's panel (docs/mechlab.md, The armour panel).
class LabArmorTitles : public ztwod::Control {
 public:
  explicit LabArmorTitles(Engine& e);
  void show_for(float seconds) override;  // +0x60  0x00450fb0: the arrows and REMOVE only while editable
  void hide() override;              // +0x64  0x00451050
  bool read_control(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x84  0x004510a0
  using ztwod::Control::refresh;  // the base's refresh() stays (ours: not hidden)
  void refresh(Handle config);       // 0x00450e00
  Engine& e_;
  LabArmorRemove remove;             // +0x14c  ARMOR_REMOVE
  LabArmorToggle toggles[11];        // +0x2a4  by section
  LabArmorDistribute distribute;     // +0x3088  ARMOR_DISTRIBUTE
};

// MESSAGE_AREA (0x0044e6d0, vtable 0x005c1740).
class LabMessage : public ztwod::FontText {
 public:
  LabMessage() : ztwod::FontText(nullptr, 0, 0) {}
  bool read(zrdr::Node* node, ztwod::Dialog* dialog) override;  // +0x78  0x0044e700: SOUND
  void set_text(const char* format, ...) override;  // +0x7c  0x0044e740: with the sound
  void set_text_id(uint32_t id, ...) override;      // +0x98  0x0044e770: likewise
  uint32_t x4b0 = 0;                 // +0x4b0
  float x4b4 = 1.0f;                 // +0x4b4
};

// 0x00454600 (UNASSIGNED_LIST's +0xac, docs/mechlab.md, The item rules):
// the row's item onto the shown slot's loadout from the lab's store, if the
// rule allows it; then the screen refreshed.
void lab_put_on(Engine& e, LabUnassignedList& list, int32_t index);
// 0x00455fb0's item row (ASSIGNED_LIST's +0xac): the item back to the lab's store.
void lab_take_off(Engine& e, LabAssignedList& list, int32_t index);
// [0x005e47d4]: which end of the part's firepoints the next weapon takes
// (1 at start: the last).
uint8_t& lab_firepoint_toggle();

// 0x004496c0: CHASSIS_LIST's order, `by` 0 the name (FIX (bug), zfix bugs:
// the exe compares the mechs' numbers) or 1 the tonnage, then the number.
bool chassis_before(Engine& e, int32_t by, int32_t ma, int32_t mb);

// The screen (vtable 0x005c1110), 0x1b468 bytes.
class LabScreen : public MenuScreen {
 public:
  LabScreen(Engine& e, int32_t slots);  // 0x0044b920: the lance's slot count
  ~LabScreen() override;             // 0x0044c600 (deleting 0x0044c2f0)
  void on_button(int32_t id, int32_t a, int32_t b) override;  // +0x14  0x0044d4d0
  void switch_page(int32_t page);    // 0x0044ce30: 6 is ACCEPT
  void show_slots(int32_t slot);     // 0x0044ce90: the lance's buttons, `slot` chosen
  void refresh();                    // 0x0044cf50: the page for the shown slot's loadout
  void refresh(Handle config);       // 0x0044cf70
  void edit_name(bool start, bool rename);  // 0x0044dca0
  // 0x00449e10: a slot without a loadout gets the first its mech's list
  // has that the store can fill (else the first).
  void default_config(Handle slot, Handle store);
  void show_page(int32_t page, bool show);  // 0x0044ca50
  HelpButton* tab(int32_t page);     // 0x0044ca20: null out of 0..6
  void check_part(int32_t part);     // 0x0044d3e0: the part buttons, that one checked
  void refresh_groups();             // 0x0044dcf0: on the weapon groups page, both lists anew
  bool engine_editable();            // 0x0044dc80: a loadout shown and the flag [0x00605a84] clear
  void engine_press(int32_t id);     // 0x0044d4d0's 19..22: the engine faster, slower, its type
  static void lab_tab_glow(HelpButton& t, bool on, ztwod::Image* glow);  // 0x0044af90

  int32_t page = -1;                 // +0x111ec
  ztwod::Image temp_background{0};   // +0x111f0  TEMP_BACKGROUND
  LabPanel panel_center;             // +0x112ac
  LabPanel panel_left;               // +0x11374
  LabPanel panel_right;              // +0x1143c
  LabMechData* mech_data = nullptr;  // +0x11504  MECH_DATA (new(0x5ac))
  LabLoadoutList loadouts;           // +0x11508
  ztwod::Image icon_glow{0};         // +0x11708  ICON_GLOW
  InPlace<HelpButton, 7> tabs;       // +0x117c4  ICON_CHASSIS..ICON_ACCEPT, 0x17c each
  ztwod::Image pilot_glow{0};        // +0x12228  PILOT_GLOW
  InPlace<MechButton, 12> mechs;     // +0x122e4  0x64c each, ICON_MECH1..4 the first four
  LabChassisList chassis;            // +0x16e74
  LabConfigList configs;             // +0x17548
  LabEditBar config_edit;            // +0x17ca8
  LabSkinList skins;                 // +0x1803c
  LabButton sort_name;               // +0x18460  id 0x1f
  LabButton sort_mass;               // +0x185b8  id 0x20
  LabPart part_head;                 // +0x18710
  LabPart parts[7];                  // +0x18880..+0x19120
  LabUnassignedList unassigned;      // +0x19290
  LabAssignedList assigned;          // +0x1a13c  ASSIGNED_LIST
  LabWeaponList ungrouped;           // +0x1a36c
  LabWeaponList grouped;             // +0x1a570
  WinTitle current_chassis;          // +0x1a774  CURRENT_CHASSIS (vtable 0x005c11c0)
  LabArmorTitles* armor_titles = nullptr;  // +0x1a8c0  ARMOR_TITLES (new(0x31e0))
  LabChassisData* chassis_data = nullptr;  // +0x1a8c4  CHASSIS_DATA (new(0x188))
  LabEngineTable engine_table;             // +0x1a8c8
  LabMessage message;                // +0x1afb0  MESSAGE_AREA
};

}  // namespace mech3
