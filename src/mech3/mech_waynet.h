#pragma once
#include <cstdint>
#include <list>
#include <string>

#include "gamez/zgame/unported.h"
#include "gamez/ztwod/twod.h"
#include "mech3/zai_steps.h"

namespace mech3::ai {
struct NetNodeRec;
}

// The waynets (docs/vehicles.md, The mcbwaynet; The nav waynet): numbered
// nets of named points with one selected, shown in the world by the
// "waypoint" node, on the radar by an icon and at the screen's edge by an
// arrow. mech3\mech_waynet.cpp (0x00479640..0x0047a950). The mission's nav
// waynet (waynet_N.zrd, version 107, 0x004797c0 into the missions' +0xb0)
// holds the nav points the objectives name; the mcbwaynet ([0x0060b544])
// the field bases' dock points, made from the arrangenodes nodes as the nets
// compile, which the player's MFB command picks one of and asks the dock.
namespace mech3 {

using Handle = void*;

// A point (0x0047a320, 0x38 bytes, vtable 0x005c3828): a Behaviour at a
// place, named twice (the record's name and its auxname, which it answers
// by), with its links' first.
struct WaynetPoint : ai::Behaviour {
  WaynetPoint(const char* name, const char* label, const float pos[3], const int32_t* links, int32_t n, uint8_t mfb,
              uint8_t xf, uint8_t xd, uint8_t xe, uint8_t hidden);  // 0x0047a320
  ~WaynetPoint() override = default;                                  // 0x0047a480
  const float* position() const override { return pos; }            // vt+0x18 0x00402b60
  const char* label_or_null() const { return has_label ? label.c_str() : nullptr; }  // vt+0x24 0x0047a930
  bool set_position(const float* p);                                  // vt+0x2c 0x0047a530
  void set_reached(uint8_t on);                                       // 0x0047a500: its line once

  uint8_t mfb = 0;            // +0x0c  a dock point
  uint8_t xd = 0, xe = 0, xf = 0;  // +0x0d..+0x0f
  uint8_t hidden = 0;         // +0x10  the record's hidden
  Handle sound = nullptr;     // +0x14  "nav<label>" (not a dock point's)
  uint32_t x18 = 0;           // +0x18
  float pos[3] = {};          // +0x1c
  std::string name;           // +0x28  strdup'd
  bool has_name = false;
  std::string label;          // +0x2c
  bool has_label = false;
  int32_t first_link = 0;     // +0x30
  uint8_t reached = 0;        // +0x34
};

// A waynet (0x00479640, vtable 0x005c37b0, over the compiled net's
// 0x004bfe40: element 0 the default behaviour, the points after).
class Waynet : public ai::CompiledNet {
 public:
  Waynet(int32_t id, const char* name);  // 0x00479640
  ~Waynet() override;                     // 0x004796f0

  virtual void begin();                   // vt+0x2c 0x00479a50: the marker found (once), point 0 selected
  virtual void next();                    // vt+0x60 0x00479b10
  virtual void prev();                    // vt+0x64 0x00479b30
  virtual void nearest(const float* pos);  // vt+0x68 0x00479b50
  bool select(int32_t i);                 // 0x00479b90: false (the marker hidden) out of range
  void reselect() { select(current); }   // 0x00479bd0
  void select_point(znet::Element* e) { select(index_of(e)); }  // 0x0047a250
  WaynetPoint* find(const char* label) const;  // 0x00479be0 (case not counted)
  const float* position_of(int32_t i) const;   // 0x00479aa0: the zero vector without an element
  void place_marker();                    // 0x00479ac0
  void add(znet::Element* e) { elems.push_back(e); }  // 0x0047a010
  bool remove_last(znet::Element* e);    // 0x0047a1e0: when it is the last
  // 0x0047a270: `p` off the watched list, then at its end if `add` (an
  // objective's nav point, 0x0045cf9d).
  void note(znet::Element* p, bool add);
  // vt+0x6c 0x00479c50, each mission frame on the mission's waynet: the
  // watched points and the selected one within 25 m of `vehicle` marked
  // reached (their nav line once); the selected one with autonext reached,
  // the next selected. The selected point's reached.
  virtual uint8_t frame(Handle vehicle);
  // 0x00479ef0 (the HUD's data update, on the mission's waynet): the arrow
  // hidden, then with `active` shown at the screen's edge toward the
  // selected point when it is off the screen (the nav or MFB set by its +0x0c).
  void hud_arrow(bool active);

  std::string label;                      // +0x1c  strdup'd
  int32_t current = 0;                    // +0x20
  uint8_t x24 = 0;                        // +0x24  the id's low byte
  std::list<znet::Element*> watched;      // +0x28  the points the objectives watch
};

// The mcbwaynet (0x0047a550, 0x48 bytes, vtable 0x005c38e0) and the MFB
// command's state.
class McbWaynet : public Waynet {
 public:
  using Waynet::Waynet;
  void next() override;                   // vt+0x60 0x0047a5c0
  void prev() override;                   // vt+0x64 0x0047a620
  void nearest(const float* pos) override;  // vt+0x68 0x0047a680
  void clear();                           // 0x0047a6b0 (0x0047a720: the dock's marker clear)
  void command(Handle mech);              // 0x0047a7a0 (0x0047a910, MCB_COMMAND 0x7b)
  uint8_t frame(Handle) override { return 0; }  // vt+0x6c 0x004cd490: nothing

  int32_t passed = 0;                     // +0x30  how often the picked point came round
  znet::Element* picked = nullptr;        // +0x34
  znet::Element* chosen = nullptr;        // +0x38  the point the request went with
  znet::Element* nav_point = nullptr;     // +0x3c  the mission's waynet's point before
  Handle nav = nullptr;                   // +0x40  the mission's waynet before ([0x00609980])
  int32_t step = 0;                       // +0x44  0 to pick, 1 to ask, 2 to cancel
};

WaynetPoint* as_point(znet::Element* e);  // vt+0x78: a point itself, else none (0x0040bae0)
McbWaynet* mcb_waynet();       // [0x0060b544]
McbWaynet* mcb_waynet_make();  // 0x004d5460: made once (an id of 10000 up, kept by znet's clear)
void mcb_waynet_free();        // 0x004d54f0 (the mission's cleanup)
// 0x0047a730: a mech (its vt+0x50) made the dock's requester, while a base
// is up; 0x0047a780 is the same through the mcbwaynet (the AI's 0x004ea2b7).
bool mfb_set_requester(Handle mech);
// The net compile's (0x004bde20): an arrangenodes node's point in the
// mcbwaynet, unless one of its auxname is there.
void waynet_point_from_node(const ai::NetNodeRec& r, const float pos[3]);
// 0x004797c0 (the mission load's 0x00490829, ECX the name: waynet_%d,
// waynet_ia%d, waynet_mp%d by game type with the mission number): the file
// built by a short-lived builder (version 107, 0x004be690); a waynet then
// counted in for the icons and its marker placed. None: null.
Waynet* nav_waynet_make(const char* name);
// 0x004798f0 / 0x00479a30: a waynet counted in (the first makes the icons)
// and out (the last frees them, 0x00479850).
void waynet_icons_ref();
void waynet_icons_unref();
// 0x00479850 (the mission's end, and the last waynet's delete): a leak
// reported when waynets are still counted, the count 0, the marker
// forgotten, the icons and arrow images freed.
void waynet_icons_free();
// 0x00479ed0 (the HUD's frame): the arrow drawn unless hidden.
void waynet_arrow_draw();
// For the tests (ours): the arrow's place and whether it is shown.
bool waynet_arrow_place(int32_t* x, int32_t* y);
// The radar's icons ([0x00608ba8] nav, [0x00608bac] MFB), null before the
// first waynet.
ztwod::Image* waynet_icon(bool mfb);

// The calls out.
class WaynetHooks : public zgame::Unported {
 public:
  static uint32_t low(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
  static Handle h(uint32_t v) { return reinterpret_cast<Handle>(static_cast<uintptr_t>(v)); }
  virtual uint32_t ref(const void* p) { return low(p); }
  virtual void on_new(uint32_t size, const void* p) { (void)size, (void)p; }
  virtual int32_t alloc_id();  // 0x0058b1c0: znet's first free in 10000..10099 ([0x005f2a48])
  // The icons (0x004798f0 / 0x00479850): zImage's directory (0x00516f50),
  // the radar's two icons (zTwoD images, 0x0059b830 into 0xbc bytes, deleted
  // by vt+0x00(1)) and the arrow's eight images (zImage 0x005179d0, released
  // by 0x00517b10); the leak report (0x0049bfd0).
  virtual void image_path(const char* path);
  virtual Handle icon_make(const char* name);
  virtual void icon_free(Handle icon);
  virtual Handle image_get(const char* name);
  virtual void image_release(Handle image);
  virtual void report_leak(int32_t count);
  // The arrow (the zTwoD image at 0x00608ae0, made at start-up by 0x0059b970(0)):
  // hidden (0x0059ff50), its image set (0x0059bb50) and its size (the
  // image's +4 / +6 shorts, 0 without one), placed (0x0059bab0), shown for
  // ever (0x0059ff10(-1)), hidden or not (+0x0c bit 4), drawn (0x0059bb70);
  // the screen's size (0x0040d760 / 0x0040d770) and a world point on the
  // screen (0x0053cf10: bits 1 left, 2 right, 4 above, 8 below; x, y).
  virtual void arrow_hide();
  virtual void arrow_image(Handle image);
  virtual void arrow_size(int32_t* w, int32_t* h);
  virtual void arrow_place(int32_t x, int32_t y);
  virtual void arrow_show();
  virtual bool arrow_hidden();
  virtual void arrow_draw();
  virtual int32_t screen_w();
  virtual int32_t screen_h();
  virtual uint32_t project(const float in[3], float out[3]);
  // The nav waynet's build (0x004797c0): the builder with the build flag
  // (zainetedit, version 107); a waynet's id (0x0058b1c0 from 10000).
  virtual Waynet* build_waynet(int32_t id, const char* name);
  // The "waypoint" node (0x004f7fe0(6)), its parents (+0x54), the world's
  // add ([0x00608c84] vt+0x00), shown (0x004eb170), its place (0x004fc0b0).
  virtual Handle node_named(const char* name) { return h(unported("0x004f7fe0", {6, name})); }
  virtual int32_t node_parents(Handle node) { return static_cast<int32_t>(unported("node+0x54", {low(node)})); }
  virtual void world_add(Handle node) { unported("world.vt+0x0", {low(node)}); }
  virtual void node_set_active(Handle node, int32_t on) { unported("0x004eb170", {low(node), on}); }
  virtual void node_set_translation(Handle node, float x, float y, float z) {
    unported("0x004fc0b0", {low(node), zgame::bits(x), zgame::bits(y), zgame::bits(z)});
  }
  // A point's nav line: zSound's by name (0x0056f6f0) and played (0x0056e3d0).
  virtual Handle sound_find(const char* name) { return h(unported("0x0056f6f0", {name})); }
  virtual void sound_play(Handle s) { unported("0x0056e3d0", {low(s), 0x3f800000}); }
  // The MFB command's: the mission's waynet ([0x00609980], the missions'
  // +0xb0: 0x00491d00 sets it and reselects), a mech's place (0x005a85e0)
  // and vehicle (vt+0x50), the dock (the registry 0x00605ae0: its alive
  // count, idle state, request 0x0045a500, front's abort 0x0045a540, its
  // requester 0x0045b740, the radio lines +0x98 / +0x9c, 0x004e6d50).
  virtual Handle mission_waynet() { return h(unported("[0x00609980]", {})); }
  virtual void set_mission_waynet(Handle w) { unported("0x00491d00", {0x6098d0, low(w)}); }
  virtual const float* entity_position(Handle e) { return reinterpret_cast<const float*>(h(unported("0x005a85e0", {low(e)}))); }
  virtual Handle entity_vehicle(Handle e) { return h(unported("entity.vt+0x50", {low(e)})); }
  virtual int32_t bases_alive();
  virtual bool dock_idle();
  virtual void dock_message(Handle mech);
  virtual void dock_abort();
  virtual bool dock_set_requester(Handle mech);
  virtual void say(int32_t line);  // the registry's sounds[line]
};
void set_waynet_hooks(WaynetHooks* h);  // null: the defaults
WaynetHooks& waynet_hooks();
// The marker ([0x00608ba4]) and the icons' count ([0x00608ba0]).
Handle& waynet_marker();
int32_t& waynet_icons_count();

}  // namespace mech3
