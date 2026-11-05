#pragma once
#include <cstdint>
#include <vector>

#include "gamez/zgame/unported.h"
#include "gamez/ztwod/twod.h"
#include "mech3/zai_target.h"

// The pocket commander's panel (mech_pocket.cpp, between 0x004674f0 and
// 0x00469060; docs/ai.md 17.6b): a zTwoD window at the top right of the
// screen listing the cursor's target, the lancemates (one line each, picked
// with the number keys or the mouse) and the orders that can be given (one
// line each, lit when they apply). Item L4c2a: the panel, its lines and
// their states; the frame (0x00469d00) is L4c2b, the lancemates' draw
// (0x00467760) and the panel's (0x00468900) L4c2c.
namespace mech3 {

struct Mech;
class PocketControls;
class PocketPanel;

// [0x00605ee8]: a line's colour by its state (bit 0 under the mouse, bit 1
// picked, bit 2 lit), a COLORREF and the display's pixel (0x00579370), made
// once by the first panel (0x004686e0; [0x00605f4c]); two more at
// [0x00605cc8] and [0x00605cd0] for the panel's own draw.
struct PocketColour {
  uint32_t ref = 0;
  uint16_t pixel = 0;
};
struct PocketColours {
  bool made = false;           // 0x00605f4c
  PocketColour state[8];       // 0x00605ee8
  PocketColour yellow, white;  // 0x00605cc8, 0x00605cd0
};
PocketColours& pocket_colours();

// The orders the panel offers ([0x00605cd8], 0x54 bytes each, [0x00605f54]
// of them), made at the mission's start by 0x00467d80 from the table
// 0x005e5050 (an action, its message, the lance order): only the actions
// bound to a key, each with the key's name.
struct PocketCommand {
  char key[0x10] = {};   // +0x00  0x0051d330's, 15 at most
  char name[0x40] = {};  // +0x10  the message (0x00576040), 63 at most
  int32_t order = 0;     // +0x50
};
struct PocketCommands {
  PocketCommand rows[6];
  int32_t count = 0;     // 0x00605f54
};
PocketCommands& pocket_commands();
void pocket_load_commands();  // 0x00467d80

// The mission's start (0x0046aaa0): the sounds (0x0046a930), the marker,
// the orders' table; and its end (0x0046ab10, 0x0046aa40): the sounds
// forgotten, a mode left up deleted, [0x00605f50], [0x00605f60] and the
// marker cleared (the world frees it).
void pocket_load_sounds();    // 0x0046a930
void pocket_mission_start();  // 0x0046aaa0
void pocket_mission_end();    // 0x0046ab10

// The command target marker [0x00605f58] (0x00468100: 0xd4 bytes, an
// AiTarget by 0x004d7f80 on its node); the program makes it (PanelCalls::
// make_marker). Its zEntity3d part is the port's few fields.
struct PocketMarker : ai::AiTarget {
  void* node = nullptr;          // +0x14  zEntity3d's node
  uint8_t active = 0;            // +0x08  shown (the panel's pick of a spot)
  const char* name = nullptr;    // 0x005a84c0's: message 0x89d
};

// What the panel reaches outside zTwoD (unset: 0 and logged through
// PocketCalls::unported, if set).
struct PanelCalls {
  zgame::Handle (*key_binding)(int32_t action) = nullptr;           // 0x0051d1f0: ECX the action
  void (*key_name)(zgame::Handle binding, char* out, int32_t n) = nullptr;  // 0x0051d330
  Mech* (*entity_mech)(zgame::Handle entity) = nullptr;             // a zEntity's vt+0x2c
  const char* (*unit_name)(Mech& m) = nullptr;                      // vt+0xa4: the pilot's name
  zgame::Handle (*node_entity)(uint32_t node) = nullptr;            // 0x0040eee0 (0x0040e920)
  uint32_t (*entity_team)(zgame::Handle entity) = nullptr;          // vt+0x28's +0xc0
  int32_t (*repair_bays)() = nullptr;                               // 0x0045a000 on 0x00605ae0
  // 0x00468100("cmd_target.flt", 0): the command target marker, an AiTarget
  // (0x004d7f80) on the world's node of that name (else a new object3d),
  // named, on the player's side; then its node switched off (0x004eb170)
  // and it inactive (+0x08).
  zgame::Handle (*make_marker)() = nullptr;
  // The marker shown or not: its node (0x004eb170) and its +0x08.
  void (*marker_on)(zgame::Handle marker, bool on) = nullptr;
};

// What the panel's frame (0x00469d00) reaches outside (unset: none).
struct FrameCalls {
  // 0x00430aa0 on the player's +0x684 when he is the view's (+0x64), else
  // none; its +0x70 == 1 (the mission map up) and the map (0x00491c80 on
  // 0x006098d0) with its size (+0xd8's +0x08, +0x0c).
  zgame::Handle (*view_hud)(Mech& player) = nullptr;
  bool (*hud_map_up)(zgame::Handle hud) = nullptr;
  zgame::Handle (*map)() = nullptr;
  void (*map_size)(zgame::Handle map, int32_t* w, int32_t* h) = nullptr;
  // The cursor on the map (0x00444670 on the HUD's reticle, +0x144c) and
  // the map's pick there (0x004587b0: the spot, the record's node +0x24).
  void (*map_mouse)(zgame::Handle hud, int32_t* x, int32_t* y) = nullptr;
  bool (*map_pick)(float x, float y, float place[3], uint32_t* node) = nullptr;
  // The reticle's aim (0x00444330(0): a record, its node +0x24, none) and
  // the spot it hit (the reticle's +0x8fc).
  zgame::Handle (*reticle_pick)(zgame::Handle hud) = nullptr;
  uint32_t (*record_node)(zgame::Handle record) = nullptr;
  void (*reticle_place)(zgame::Handle hud, float place[3]) = nullptr;
  uint32_t (*trigger)() = nullptr;                              // 0x00467480: command 0x3e's state (0x0051d300; 4 released)
  zgame::Handle (*contact_entity)(void* contact) = nullptr;     // 0x004e6fe0
  uint32_t (*entity_node)(zgame::Handle e) = nullptr;           // its +0x04
  const float* (*entity_place)(zgame::Handle e) = nullptr;      // 0x005a85e0
  const char* (*entity_name)(zgame::Handle e) = nullptr;        // vt+0xa4
  void (*pick_target)(Mech& player, zgame::Handle e) = nullptr; // 0x004a7b40: its contact the target, else none
  void (*marker_move)(zgame::Handle marker, const float* place) = nullptr;  // vt+0x18
  zgame::Handle (*net_entity)(uint32_t net) = nullptr;          // its +0x18's vt+0x2c
  const float* (*net_place)(uint32_t net) = nullptr;            // its vt+0x18
  void (*route_mouse)(ztwod::MouseState* m) = nullptr;          // 0x0051ba60 (unset: the zTwoD host's)
};
FrameCalls& frame_calls();

// What the draws (0x00467760, 0x00468900) reach outside zTwoD (unset: none).
struct DrawCalls {
  // An entity's node's world place (its +0x14's data +0x84, its matrix
  // brought up to date first when flagged, 0x004ed280) and its size (+0x68).
  void (*entity_position)(zgame::Handle e, float out[3]) = nullptr;
  float (*entity_size)(zgame::Handle e) = nullptr;
  uint32_t (*project)(const float in[3], float out[3]) = nullptr;  // 0x0053cf10: bit 3 off the screen
  void (*map_point)(zgame::Handle map, const float in[3], int32_t out[2]) = nullptr;  // 0x004586e0
  void (*screen_rect)(ztwod::Rect* out) = nullptr;                // 0x0040d7b0
  bool (*node_marked)(uint32_t node) = nullptr;                   // 0x0040ee50: the node's +0x28 bit 21
  void (*view_prepare)() = nullptr;                               // 0x004b51f0 on the view
};
DrawCalls& draw_calls();

// [0x00605ed8]: the screen's rect less one pixel at the right and bottom, the
// lines' and marks' clip (made by each panel draw).
ztwod::Rect& pocket_clip();

// Mech3.exe 1.2 @ 0x0046a410 (the HUD's overlay pass, 0x0040fac0): with the
// mode up, the view prepared, the panel drawn (its vt+0x00, 0x0046a3f0) and
// the mode's pending press (+0x184) cleared.
void pocket_draw(float dt);
// Mech3.exe 1.2 @ 0x00469d00 (the mode's update, 0x0046a360): the panel's
// mouse (0x00468820), the cursor (the trigger let go: the reticle's or the
// map's pick, the marker put on a bare spot; else the player's target, or a
// network game's element), the lines re-lit when it moved, the aim copied,
// and the way out once an order's line was pressed (+0x148 / +0x14c: three
// frames, the view's pocket off and the camera back, one frame, then +0x38).
void pocket_frame(PocketControls& c, float dt);
PanelCalls& panel_calls();

// A line (0x4bc bytes, vtable 0x005c2be8; 0x004674f0): text in the font
// 0x005e50c0 at a place, its state +0x4b0 and the panel +0x4b4.
class PocketItem : public ztwod::FontText {
 public:
  PocketItem(PocketPanel* panel, const char* text, int32_t px, int32_t py);
  void on_enter() override;   // +0x3c  0x004675e0: bit 0, the hover sound
  void on_leave() override;   // +0x40  0x00467610
  virtual void refresh();     // +0xb0  0x00467580 (each kind's first)
  virtual Mech* entity() { return nullptr; }  // +0xb4  0x0040bae0
  void restate();             // 0x00467580: bit 0 the mouse over it, bit 1 its +0x30 bit 1

  uint32_t state = 0;         // +0x4b0
  PocketPanel* panel;         // +0x4b4
};

// The target line and the lancemates' (vtable 0x005c2ca0; 0x00467620): its
// unit +0x4b8, always lit.
class PocketMateItem : public PocketItem {
 public:
  PocketMateItem(PocketPanel* panel, Mech* unit, const char* text, int32_t px, int32_t py);
  void draw() override;        // +0x04  0x00467760: in its state's colour, then a line to its unit
  void on_press() override;    // +0x44  0x004676c0: picked
  void on_release() override;  // +0x48  0x00467710
  void refresh() override;     // +0xb0  0x004676a0
  Mech* entity() override;     // +0xb4  0x00467690

  Mech* unit;                  // +0x4b8
};

// An order's line (vtable 0x005c2db0; 0x00467960): its order +0x4b8; each
// kind lights it its way (+0xb0).
class PocketOrderItem : public PocketItem {
 public:
  enum Kind : uint8_t { kAny, kEnemy, kAlly, kSpot, kOne };
  PocketOrderItem(Kind kind, int32_t order, PocketPanel* panel, int32_t px, int32_t py);
  void draw() override;        // +0x04  0x00467d40: in its state's colour
  void on_press() override;    // +0x44  0x00467c90: lit, the order taken
  void on_release() override;  // +0x48  0x00467d00
  void refresh() override;     // +0xb0  0x0046a7b0 / 0x0046a5a0 / 0x0046a670 / 0x0046a740 / 0x0046a810

  Kind kind;
  int32_t order;               // +0x4b8
};

// 0x004679a0: the line for a lance order (0 / 1 / 8 any lancemate picked;
// 3 / 10 a target not the player's ally; 4 / 11 an ally; 5 / 9 a spot;
// 7 / 12 one picked and a repair bay); none for the others.
PocketOrderItem* make_order_item(int32_t order, PocketPanel* panel, int32_t px, int32_t py);

// The panel (0x548 bytes, vtable 0x005c2e68; 0x00468250): a Window with
// the target line at +0x7c and every line at +0x53c (the lancemates first,
// +0x74 of them with empty places, then the orders).
class PocketPanel : public ztwod::Window {
 public:
  PocketPanel(PocketControls* controls, int32_t px, int32_t py, int32_t line_h);  // 0x00468250
  ~PocketPanel() override;               // 0x00468720
  void update(float dt) override;        // +0x00  0x00468900: the lines drawn, the cursor marked
  bool pick(int32_t unit);               // 0x00469060: the unit's line toggled, 1-based
  void route();                          // 0x00468820: the mouse over the lines
  void picked(int32_t delta, int32_t from);  // 0x004687e0: the count, the lines from `from` refreshed

  PocketControls* controls;              // +0x58
  ztwod::Rect rect;                      // +0x5c..+0x68  the lines' box
  int32_t line_h;                        // +0x6c
  int32_t mates = 0;                     // +0x70  lancemates under orders and up
  int32_t places = 0;                    // +0x74  that, 3 at least
  int32_t picked_count = 0;              // +0x78
  PocketMateItem target;                 // +0x7c  "   target"; its +0x4b8 below
  zgame::Handle target_entity = 0;       // +0x534  the cursor's entity
  std::vector<PocketItem*> items;        // +0x53c
};

}  // namespace mech3
