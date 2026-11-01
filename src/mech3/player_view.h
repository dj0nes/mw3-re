#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

#include "gamez/zgame/unported.h"

namespace zrdr {
struct Node;
}

// The player's view and its cameras (docs/view.md). File unknown: the view
// (0x0040aca0..0x0040bb00) sits between mech3\briefing.cpp and mech3\mech.cpp,
// the mech cameras (0x00421530..0x004222d0) between mech_anim.cpp and
// mech_data.cpp, their set-up 0x00430e70 in mech_data.cpp; named after the
// view. The view ([0x005f4c8c]) owns the scene's camera node (camera1); each
// frame its camera mode, one of the target mech's six cameras, places that
// node: in the cockpit, behind, above.
namespace zcls { struct SegmentHits; }

namespace mech3 {

using Node3 = void*;  // a zClass node (zcls::Node)
struct Mech;
struct Vehicle;
class PlayerView;

// A camera mode (base vtable 0x005bc2c0; 0x78 bytes as the mech's are).
class CameraMode {
 public:
  explicit CameraMode(int32_t id);                      // 0x0040a950
  virtual ~CameraMode() = default;                      // +0x00
  virtual void activate(PlayerView& v) { (void)v; }     // +0x04 (0x004bf620)
  void deactivate(PlayerView& v) { on_switch(v, 0); }   // +0x08 (0x0040a9f0)
  virtual bool update(float dt, PlayerView& v);         // +0x0c 0x0040b8d0
  virtual void set_look(int32_t look) { look_ = look; }  // +0x10 0x0040bac0
  virtual void reset(PlayerView& v);                    // +0x14 0x0040b820
  virtual bool usable(PlayerView& v) { (void)v; return true; }  // +0x18 0x0040bad0
  virtual void on_switch(PlayerView& v, bool on) { (void)v, (void)on; }  // +0x20 0x0040baf0
  virtual bool read(void* node) { (void)node; return false; }  // +0x24

  int32_t id;                      // +0x04  0 tether, 1 attach (cockpit), 2 zippercam, 3 godseye, 4 mcbtether, 5 area
  float delta_rot[4];              // +0x08  the offset's rotation (w, x, y, z)
  float desired_pos[3];            // +0x18
  float dist_vary_factor;          // +0x24
  float dist;                      // +0x28
  float dist_factor;               // +0x2c  per unit of speed
  float dist_vary;                 // +0x30
  float dist_catch_up;             // +0x34  the speed's smoothing rate
  float turn_rate;                 // +0x38  the offset's rotation follows at dt x this
  float look_rate;                 // +0x3c  the look follows at dt x this
  int32_t look_ = 2;               // +0x40
};

// The mech cameras (0x00421530, vtable 0x005bd2f8): tether, zippercam, godseye, mcbtether.
class MechCamera : public CameraMode {
 public:
  explicit MechCamera(int32_t id);                      // 0x00421530
  void activate(PlayerView& v) override;                // 0x00421ad0
  bool update(float dt, PlayerView& v) override;        // 0x00421760
  void set_look(int32_t look) override;                 // 0x00421c00
  void reset(PlayerView& v) override;                   // 0x00421900
  bool usable(PlayerView& v) override;                  // 0x00421a30
  bool read(void* node) override;                       // 0x004215d0

  float look_q[4];                 // +0x44  the look's rotation
  uint8_t torso_look = 0;          // +0x54  look along the torso
  float dist_now;                  // +0x58
  float speed = 0;                 // +0x5c  smoothed forward speed
  PlayerView* view = nullptr;      // +0x60
  uint8_t fixed = 0;               // +0x64  the cockpit camera's: no mech rotation
  float look_left = 1.5533430f;    // +0x68  look limits, radians
  float look_right = -1.5533430f;  // +0x6c
  float look_down = -0.2617994f;   // +0x70
  float look_up = 0.7853982f;      // +0x74
};

// "attach" (vtable 0x005be9f0): the cockpit camera.
class CockpitCamera : public MechCamera {
 public:
  CockpitCamera() : MechCamera(1) {}
  bool update(float dt, PlayerView& v) override;        // 0x00421e40
  void reset(PlayerView& v) override;                   // 0x00421d40
  void on_switch(PlayerView& v, bool on) override;      // 0x00421d50
};

// "area" (vtable 0x005be9c8): a fixed camera that keeps the mech in view.
class AreaCamera : public MechCamera {
 public:
  AreaCamera() : MechCamera(5) {}
  bool update(float dt, PlayerView& v) override;        // 0x004221d0
  void set_look(int32_t look) override;                 // 0x004220c0
};

// A mech type's cameras (+0x24 of its data), and the current one's id (+0x10).
struct CameraList {
  std::vector<CameraMode*> modes;  // +0x04..+0x0c
  int32_t current = 0;             // +0x10
  CameraMode* find(int32_t id) const;  // 0x0040aa00
  ~CameraList();                       // 0x0040abf0
};
// Mech3.exe 1.2 @ 0x00430e70: the six cameras with their defaults, then each
// one's block of the mech definition's `camera` reader node; `roster` picks
// tether's height; area's from the view camera's field of view.
CameraList* make_cameras(int32_t roster);

// What the view calls outside it: zClass's camera and nodes, the world's
// queries and fog, the weather, the mech's torso, controls and HUD. One per
// original function or read; the keys are the addresses.
class ViewHooks : public zgame::Unported {
 public:
  // How a node or object is named in the hooks' records (the probe's addresses).
  virtual uint32_t id(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
  uint32_t m(Mech& x) { return id(&x); }
  uint32_t v(Vehicle& x) { return id(&x); }

  // zClass.
  virtual void camera_set_position(Node3 cam, float x, float y, float z) {   // 0x004f3870
    unported("0x004f3870", {id(cam), zgame::bits(x), zgame::bits(y), zgame::bits(z)});
  }
  virtual void camera_set_rotation(Node3 cam, float x, float y, float z) {   // 0x004f36b0
    unported("0x004f36b0", {id(cam), zgame::bits(x), zgame::bits(y), zgame::bits(z)});
  }
  virtual void camera_add_position(Node3 cam, float x, float y, float z) {   // 0x004f3920
    unported("0x004f3920", {id(cam), zgame::bits(x), zgame::bits(y), zgame::bits(z)});
  }
  virtual void camera_set_horizon(Node3 cam, Node3 horizon) { unported("0x004f4090", {id(cam), id(horizon)}); }
  virtual Node3 find_node(int32_t list, const char* name) = 0;           // 0x004f7fe0
  // A node's data: the flags (+0x00, 0x20: the world matrix is stale) and the
  // world position (+0x84). Reads, not calls.
  virtual uint32_t node_flags(Node3 node) = 0;
  virtual const float* node_position(Node3 node) = 0;
  // 0x0051e9d0, 0x0051fde0, 0x004ed280(node, 1), 0x0051ea00: the stale world matrix rebuilt.
  virtual void node_refresh(Node3 node) { unported("0x004ed280", {id(node), 1}); }
  virtual void node_world_position(Node3 node, float out[3]) = 0;        // 0x004ed530
  virtual Node3 node_parent(Node3 node) = 0;                             // +0x58[0] when +0x54
  virtual void node_remove_child(Node3 parent, Node3 child) {            // 0x004ebb40
    unported("0x004ebb40", {id(parent), id(child)});
  }
  virtual void node_add_child(Node3 parent, Node3 child) {               // 0x004eb9b0
    unported("0x004eb9b0", {id(parent), id(child)});
  }
  virtual Node3 world_node() = 0;                                        // [0x00608c84]+4
  virtual void node_set_intersect(Node3 node, int32_t on) { unported("0x004eb6d0", {id(node), on}); }
  virtual void query_mode(int32_t on) { unported("0x004ed9c0", {on}); }
  // 0x004eed90 on the world: what the segment a-b meets; its result (0 when
  // it found any) and the first record's first byte.
  virtual int32_t segment_query(const float a[3], const float b[3], uint8_t* found) = 0;
  virtual const float* camera_position(Node3 cam) = 0;                   // the camera's data +0x14
  // The mission's fog (world.zrd): the water's or the air's range (2),
  // altitude (2) and colour (3) ([0x00609900..] / [0x006098dc..]).
  virtual const float* mission_fog(bool water) = 0;
  virtual void set_camera_option(int32_t mode) { unported("0x0040ce10", {mode}); }
  // The mech definition's reader (0x004846a0 on the roster, its +0x08) and
  // zReader's find (0x005569e0), float (0x00556a60) and vec3 (0x005570b0).
  virtual void* dfn_reader(int32_t roster) = 0;
  virtual void* reader_find(void* node, const char* key) = 0;
  virtual bool reader_float(void* node, const char* key, float* out) = 0;
  virtual bool reader_vec3(void* node, const char* key, float out[3]) = 0;
  // [0x005f4c8c]'s camera's field of view (data +0xec); false without a view.
  virtual bool view_fov(float* fov) = 0;
  // 0x004edab0 on the world: the ground under (x, y, z), kept for the zone;
  // true when it found none.
  virtual bool ground_query(float x, float y, float z) = 0;
  // 0x004abd90 on the last ground query: the ground's height; the hit's
  // index, the water's kind (1 water) and level.
  virtual float water_query(float y, float height, int32_t* index, uint8_t flag, int32_t* kind, float* level) = 0;
  virtual uint8_t water_flag() = 0;                                      // [0x005e06f0]
  // 0x0040b080's calls: the camera's zone from hit `index` (0x004f6c80),
  // [0x0074a328], zEntity's 0x005a80d0 and the animations' 0x00502d70.
  virtual void camera_zone(Node3 cam, int32_t index, Vehicle* target) = 0;
  virtual void world_fog_range(float a, float b) = 0;                    // 0x004f2440
  virtual void world_fog_color(float r, float g, float b) = 0;           // 0x004f23f0
  virtual void world_fog_altitude(float a, float b) = 0;                 // 0x004f2420
  // The weather object ([0x006098d0]): +0x0c bit 4 (hidden), vt+0x64 / vt+0x60(-1).
  virtual bool has_weather() = 0;
  virtual uint32_t weather_flags() = 0;
  virtual void weather_hide() { unported("weather.vt+0x64", {}); }
  virtual void weather_show() { unported("weather.vt+0x60", {zgame::bits(-1.0f)}); }
  // 0x0040b310: an outside camera pulled in before walls (pull_in below);
  // the tests stand in for it.
  virtual void pull_in(float pos[3], const float at[3], const float back[3]);
  // 0x004efcd0 on the world ([0x00608c84] +4): `points` / 2 segments, a
  // record each (zcls::segment_query).
  virtual void world_segments(float* segs, int32_t points, zcls::SegmentHits* out) = 0;
  virtual const float* entity_position(Vehicle& t) = 0;                  // 0x005a85e0

  // The view's target, a mech or (the game editor's, editor.md S4) the
  // stealth object: its camera `id` (vt+0x10c; a mech's from its data, the
  // stealth's the vehicles' list 0x004acde0), taken and let go (vt+0x100,
  // vt+0x104).
  virtual CameraMode* target_camera(Vehicle& t, int32_t id) = 0;
  virtual void target_attached(Vehicle& t) { unported("target.vt+0x100", {v(t)}); }
  virtual void target_detached(Vehicle& t) { unported("target.vt+0x104", {v(t)}); }

  // The mech.
  virtual uint32_t mech_dead(Mech& t) { return unported("mech.vt+0xa0", {m(t)}); }
  virtual const float* torso_aim(Mech& t) = 0;                           // 0x0041ce00 (0x00465940)
  virtual void torso_cockpit(Mech& t, int32_t on) { unported("0x0041ce10", {m(t), on}); }
  virtual void mech_41ca30(Mech& t, int32_t on) { unported("0x0041ca30", {m(t), on}); }
  virtual void mech_41ca60(Mech& t, int32_t on) { unported("0x0041ca60", {m(t), on}); }
  virtual float torso_pitch_rate(Mech& t) { return zgame::from_bits(unported("0x00465900", {m(t)})); }
  virtual float torso_yaw_rate(Mech& t) { return zgame::from_bits(unported("0x004658c0", {m(t)})); }
  virtual void torso_464ac0(Mech& t, int32_t on) { unported("0x00464ac0", {m(t), on}); }
  virtual void torso_465a00(Mech& t, int32_t on) { unported("0x00465a00", {m(t), on}); }
  virtual void torso_466190(Mech& t, int32_t on) { unported("0x00466190", {m(t), on}); }
  // The controls (+0x37c): +0x08 flags (4: the torso looks), +0x0c kind; 0x00426780(arg).
  virtual uint32_t controls_flags(Mech& t) = 0;
  virtual int32_t controls_kind(Mech& t) = 0;
  virtual void controls_426780(Mech& t, uint32_t arg) { unported("0x00426780", {m(t), arg}); }
  virtual uint32_t option_5f4d44() = 0;                                  // [[0x005f4d44]]
  virtual void anim_502da0(int32_t on) { unported("0x00502da0", {on}); }
  virtual uint8_t head_bob() = 0;                                        // [0x005e2448]: the head bob is on
  virtual float head_bob_scale() = 0;                                    // [0x005e244c], 0.15
  // The HUD (0x00430aa0 on the pilot data) and its look's 0x00446240 on +0x6520.
  virtual uint32_t mech_screen(Mech& t) { return unported("0x00430aa0", {m(t)}); }
  virtual void screen_look(uint32_t screen) { unported("0x00446240", {screen + 0x6520}); }
  virtual Vehicle* player_target() = 0;                                  // [0x005f4c8c]+0x64
  // The pilot's game (0x00479480) and its +0x8e: outside cameras barred.
  virtual uint32_t pilot_game() { return unported("0x00479480", {}); }
  virtual uint8_t game_byte_8e(uint32_t game) = 0;
};
ViewHooks& view_hooks();

// Mech3.exe 1.2 @ 0x004a3290 (ECX the records, EDX how many): the nearest
// hit to `from` among them, false for none. A hit on soil 1 (water) is
// dropped (its node cleared) while the first is looked for; docs/view.md.
bool nearest_hit(zcls::SegmentHits* hits, int32_t n, const float from[3], float out[3]);
// Mech3.exe 1.2 @ 0x0040b310: the camera at `pos`, looking at `at` along
// -`back`, brought to the near side of what the view's target sees it
// through, then off what is beside it.
void pull_in(Vehicle* target, float pos[3], const float at[3], const float back[3]);
void set_view_hooks(ViewHooks* h);

// The player's view (0x0040aca0, vtable 0x005bc2e8; base zView 0x004b5060).
class PlayerView {
 public:
  PlayerView(Node3 camera, Node3 window, Node3 x, int32_t hardware);  // 0x0040aca0
  void add_to_camera(Node3 node);                       // +0x00 0x0040bab0: `node` a child of the camera
  void update(float dt);                                // +0x08 0x0040ada0
  bool attach(Vehicle* target, CameraMode* mode);       // 0x0040adf0
  void set_camera_mode(int32_t id);                     // 0x0040ae70
  // The viewed mech falling (Mech's fall actions, mech_move) and standing
  // again: its torso centred, the mode told (the cockpit camera stops
  // turning with the mech, 0x00421d50). view.md, The fall camera.
  void mech_falls(Mech& m);                             // 0x00413af0 (ECX the mech)
  void mech_stands(Mech& m);                            // 0x00413b70
  void reset();                                         // 0x0040ad90
  void cycle_camera();                                  // 0x0040aed0
  bool follow_ground();                                 // 0x0040af30
  void set_underwater(bool on);                         // 0x0040b0e0
  void weather_check();                                 // 0x0040b9a0
  // 0x0040b7e0: the camera at `pos` (+ the offset turned by `rot` times
  // `dist`), looking along `look` then the mode's delta.
  void place(const float pos[3], float dist, const float rot[4], const float offset[3], const float look[4],
             const float delta[4]);
  void place_position(const float pos[3], float dist, const float rot[4], const float offset[3]);  // 0x0040b600
  void place_look(const float look[4], const float delta[4]);   // 0x0040b750
  void place_now(const float pos[3], float dist, const float rot[4], const float offset[3], const float look[4],
                 const float delta[4]);                         // 0x0040b210: no smoothing

  Node3 camera = nullptr;         // +0x04  the camera node (camera1)
  Node3 window = nullptr;          // +0x08
  Node3 x0c = nullptr;             // +0x0c
  int32_t hardware = 0;            // +0x14  [0x0072f348] when made
  float speed = 0;                 // +0x18  the target's smoothed speed
  float rot_q[4] = {1, 0, 0, 0};   // +0x1c  the offset's rotation, smoothed
  float look_q[4] = {1, 0, 0, 0};  // +0x2c  the look, smoothed
  float final_q[4] = {1, 0, 0, 0};  // +0x3c  look x delta
  float pos[3] = {};               // +0x4c  where the camera is put
  CameraMode* mode = nullptr;      // +0x58
  int32_t last_mode = 1;           // +0x5c
  float dist = 0;                  // +0x60
  Vehicle* target = nullptr;       // +0x64  a mech, or the stealth object (editor.md S4)
  uint8_t handled = 0;             // +0x68
  uint8_t x69 = 0;                 // +0x69
  uint8_t underwater = 0;          // +0x6a
  float dt = 0;                    // +0x78
};

}  // namespace mech3
