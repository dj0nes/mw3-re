#pragma once
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gamez/zgame/unported.h"

namespace zrdr {
struct Node;
}

// C:\zipper\Mech3\mech_objective.cpp: a mission's objectives (docs/objectives.md).
// objectives.zrd (mpinfo.zrd in multiplayer) has a block per mission with a
// description and four groups: primary, secondary, final and fail. A group
// holds conditions, one object per type; obj_list nests groups. This is the
// reading side (0x0045c2d0 and below); the start and the checks come later.
namespace mech3 {

class Store;
class Objective;

// The calls out of the reading, by the original's address.
class ObjectiveHooks : public zgame::Unported {
 public:
  virtual zrdr::Node* load(const char* name);                // 0x00556740: zrdr::load
  virtual void free_tree(zrdr::Node* root);                  // 0x00556820: zrdr::free_node
  virtual const char* localize(const char* name);            // 0x00575fb0: zutl::localize
  virtual const char* message(int32_t id) { unported("0x00576040", {id}); return nullptr; }
  // 0x00575fd0 into 0x40 bytes: message `id` with one string.
  virtual std::string message_with(int32_t id, const char* s) {
    unported("0x00575fd0", {0x40, id, s});
    return std::string();
  }
  virtual zgame::Handle sound_find(const char* name) { return unported("0x0056f6f0", {name}); }
  virtual zgame::Handle entity_find(const char* name) { return unported("0x004d7330", {name}); }
  virtual uint8_t entity_flag(zgame::Handle e) { return static_cast<uint8_t>(unported("entity+0x08", {e})); }
  // The entity's vt+0xa4: the name an objective shows (null: none).
  virtual const char* entity_label(zgame::Handle e) { unported("entity.vt+0xa4", {e}); return nullptr; }
  // The nav points ([0x00609980], 0x00479be0 by name; 0 without the list).
  virtual zgame::Handle nav_find(const char* name) { return unported("0x00479be0", {0x609980, name}); }
  virtual void nav_show(zgame::Handle nav, uint8_t on) { unported("0x0047a270", {0x609980, nav, on}); }
  virtual zgame::Handle node_find(const char* name) { return unported("0x004f7fe0", {6, name}); }
  virtual zgame::Handle subnode_find(zgame::Handle node, const char* name) { return unported("0x004f9d60", {node, name}); }
  virtual zgame::Handle valve_find(const char* name) { return unported("0x004d3c40", {name}); }
  virtual void valve_set(zgame::Handle valve, int32_t v) { unported("0x004d3c00", {valve, v}); }
  // The pilot's game ([0x0060861c]+0x6c) vt+0x2c (gametypes.md); -1 without one.
  virtual int32_t game_type() { return static_cast<int32_t>(unported("game.vt+0x2c", {})); }
  virtual void training_free() { unported("game.vt+0x44", {}); }  // training keeps its own
  virtual void report(int32_t level, int32_t line, const char* fmt, const char* a, zgame::Handle b) {  // 0x0049bfd0
    unported("0x0049bfd0", {level, "C:\\zipper\\Mech3\\mech_objective.cpp", line, fmt, a, b});
  }
  virtual void default_point(float out[3]) { out[0] = out[1] = out[2] = 0.0f; }  // 0x0063d870

  // The check and the completion (0x0045e540, 0x0045e110).
  virtual zgame::Handle player_vehicle() { return unported("[0x0060861c]+0x14", {}); }
  virtual void ask_voice(zgame::Handle sound, float priority) {  // 0x0048f6c0 on the missions object
    unported("0x0048f6c0", {0x6098d0, sound, zgame::bits(priority)});
  }
  virtual void hud_message(float seconds, int32_t id, const char* text) {  // 0x0040fe70
    unported("0x0040fe70", {zgame::bits(seconds), id, text});
  }
  virtual void hud_message_line(float seconds, const char* text) {  // 0x0040fe20
    unported("0x0040fe20", {zgame::bits(seconds), text});
  }
  virtual Store* salvage_store();                                // [0x00609990]: mission_salvage()
  virtual uint8_t node_active(zgame::Handle node) { return static_cast<uint8_t>(unported("node+0x24", {node})); }
  virtual zgame::Handle view_vehicle() { return unported("[0x005f4c8c]+0x64", {}); }
  virtual zgame::Handle mech_screen(zgame::Handle vehicle) { return unported("0x00430aa0", {vehicle}); }  // on +0x684
  virtual void objectives_box_reset(zgame::Handle screen) { unported("0x00446ad0", {screen}); }    // its +0x17964
  // The types' checks (vt+0x04): a valve's value (its +0x04 object's
  // vt+0x04), an object's position (0x005a85e0), a node's world position
  // (0x004ed530), a nav point's (vt+0x18).
  virtual int32_t valve_value(zgame::Handle valve) { return static_cast<int32_t>(unported("valve.vt+0x04", {valve})); }
  virtual void object_position(zgame::Handle o, float out[3]) {
    unported("0x005a85e0", {o});
    out[0] = out[1] = out[2] = 0.0f;
  }
  virtual void node_position(zgame::Handle node, float out[3]) { unported("0x004ed530", {node}); (void)out; }
  virtual void nav_position(zgame::Handle nav, float out[3]) {
    unported("nav.vt+0x18", {nav});
    out[0] = out[1] = out[2] = 0.0f;
  }
  // Instant Action's game (0x00479460): its mode (+0x44) and number (+0x6c).
  virtual int32_t ia_mode() { return static_cast<int32_t>(unported("ia+0x44", {})); }
  virtual int32_t ia_number() { return static_cast<int32_t>(unported("ia+0x6c", {})); }
  // 0x00575fd0 into 0x100 bytes, with one number or none.
  virtual std::string message_number(int32_t id, bool has_n, int32_t n) {
    unported("0x00575fd0", {0x100, id, n, has_n ? 1u : 0u});
    return std::string();
  }
  // zEntity's list ([0x0073655c..0x00736560]), each entry's vt+0x2c object.
  virtual std::vector<zgame::Handle> world_objects() { return {}; }
  virtual uint8_t object_active(zgame::Handle o) { return static_cast<uint8_t>(unported("obj+0x08", {o})); }
  // The player's vehicle's +0xc8 mask against the object's team (vt+0x28, +0xc0).
  virtual bool object_hostile(zgame::Handle o) { return unported("obj.vt+0x28", {o}) != 0; }
  virtual int32_t object_x48(zgame::Handle o) { return static_cast<int32_t>(unported("obj.vt+0x48", {o})); }
  // Training's types (piece 5). Read: a command by its CMD_ name (0x00410fe0)
  // and its key, joystick button and mouse button (0x0051d1f0, 0x0051d220,
  // 0x0051d230); a key by name (0x0051aee0).
  virtual int32_t command_find(const char* name) { return static_cast<int32_t>(unported("0x00410fe0", {name})); }
  virtual int32_t command_key(int32_t cmd) { return static_cast<int32_t>(unported("0x0051d1f0", {cmd})); }
  virtual int32_t command_joy(int32_t cmd) { return static_cast<int32_t>(unported("0x0051d220", {cmd})); }
  virtual int32_t command_mouse(int32_t cmd) { return static_cast<int32_t>(unported("0x0051d230", {cmd})); }
  virtual int32_t key_by_name(const char* name) { return static_cast<int32_t>(unported("0x0051aee0", {name})); }
  // Checked: the screen's speed (+0x1ddc), the node its reticle's aim hits
  // (0x00444330(0), +0x24; 0 for none) and a node's parent (+0x58's first
  // when +0x54 is 1, else 0); a vehicle's target slot (+0x6c) and the slot's
  // entity (0x004e6fe0), aimed part (+0xf8), facing (+0x24..+0x2c), heat warning (+0x69c),
  // weapon manager (+0xd4); the manager's current weapon's type (0x004b04c0,
  // +0x28) and group (+0x28); a weapon of the screen fired (0x004464d0); the
  // mission's clock ([0x0060991c]); the view camera's angles (its +0x20);
  // zInput's key, joystick and mouse buttons (0x0051a9b0, 0x0051dcf0, 0x0051b6e0).
  virtual float screen_speed(zgame::Handle screen) { return zgame::from_bits(unported("screen+0x1ddc", {screen})); }
  virtual zgame::Handle aim_hit_node(zgame::Handle screen) { return unported("0x00444330", {screen, 0}); }
  virtual zgame::Handle node_parent(zgame::Handle node) { return unported("node+0x58", {node}); }
  virtual zgame::Handle vehicle_target_slot(zgame::Handle v) { return unported("vehicle+0x6c", {v}); }
  virtual zgame::Handle contact_entity(zgame::Handle slot) { return unported("0x004e6fe0", {slot}); }
  virtual int32_t vehicle_aimed_part(zgame::Handle v) { return static_cast<int32_t>(unported("vehicle+0xf8", {v})); }
  virtual void vehicle_facing(zgame::Handle v, float out[3]) {
    unported("vehicle+0x24", {v});
    out[0] = out[1] = out[2] = 0.0f;
  }
  virtual int32_t vehicle_heat_warning(zgame::Handle v) { return static_cast<int32_t>(unported("vehicle+0x69c", {v})); }
  virtual zgame::Handle vehicle_weapons(zgame::Handle v) { return unported("vehicle+0xd4", {v}); }
  virtual int32_t current_weapon_type(zgame::Handle wm) { return static_cast<int32_t>(unported("0x004b04c0", {wm})); }
  virtual int32_t weapon_group(zgame::Handle wm) { return static_cast<int32_t>(unported("wm+0x28", {wm})); }
  virtual uint8_t weapon_fired(zgame::Handle screen, int32_t weapon) {
    return static_cast<uint8_t>(unported("0x004464d0", {screen, weapon}));
  }
  virtual float mission_time() { return zgame::from_bits(unported("[0x0060991c]", {})); }
  virtual void view_angles(float out[3]) {
    unported("view+0x20", {});
    out[0] = out[1] = out[2] = 0.0f;
  }
  virtual uint8_t key_down(int32_t key) { return static_cast<uint8_t>(unported("0x0051a9b0", {key})); }
  virtual uint8_t joy_button(int32_t b) { return static_cast<uint8_t>(unported("0x0051dcf0", {b})); }
  virtual uint8_t mouse_button(int32_t b) { return static_cast<uint8_t>(unported("0x0051b6e0", {b})); }
};

// A node_state entry of cond_salvage (0x0045e9f0), 0x0c bytes.
struct NodeSalvage {
  zgame::Handle node = 0;                 // +0x00
  uint8_t active = 0;              // +0x04  its "state" is "active"
  std::unique_ptr<Store> salvage;  // +0x08
  NodeSalvage();
  NodeSalvage(NodeSalvage&&) noexcept;
  ~NodeSalvage();                  // 0x0045eb70
};

// The base (vtable 0x005c2660, 0x34 bytes; 0x0045b9e0, dtor 0x0045bb00).
class Objective {
 public:
  // Which vtable the original gives it.
  enum Kind : uint8_t {
    kGroup, kDestroy, kNodeChange, kReachArea, kValveChange,
    // training's (vtables 0x005c2720 .. 0x005c2698)
    kReachVelocity, kAimAt, kTarget, kTimeLimit, kViewObject, kFaceObject, kHeatWarning, kUserCommand,
    kJoystickPress, kKeyPress, kMousePress, kWeaponFire, kWeaponSelect, kWeaponGroupSelect
  };
  Objective(Kind k, const char* desc);
  virtual ~Objective();
  Objective(const Objective&) = delete;
  Objective& operator=(const Objective&) = delete;
  void set_desc(const char* text);  // 0x0045f110
  // vt+0x04: the base's 0x0045f2a0 (done); each type's own.
  virtual uint8_t check(ObjectiveHooks& h) { (void)h; return done; }

  Kind kind;
  zgame::Handle nav = 0;                       // +0x04
  uint8_t done = 0;                     // +0x08
  uint8_t failed = 0;                   // PM +0x09: done by failing (the box's "Failed")
  std::optional<std::string> desc;      // +0x0c
  std::unique_ptr<Store> salvage;       // +0x10
  // +0x14: the low byte of the description's pointer (the constructor reads
  // its one argument twice); nothing reads it.
  std::list<NodeSalvage> node_salvage;  // +0x18, count +0x1c
  zgame::Handle sound = 0;                     // +0x20
  zgame::Handle valve = 0;                     // +0x24  set_valve's
  int32_t value = 1;                    // +0x28
  std::optional<std::string> persist;   // +0x2c
  uint8_t visible = 1;                  // +0x30 (PM +0x34)
  uint8_t silent = 0;                   // +0x31
};

// A group: a block's list or obj_list (vtable 0x005c2678, 0x4c bytes;
// dtor 0x0045e410).
class ObjectiveGroup : public Objective {
 public:
  explicit ObjectiveGroup(const char* desc) : Objective(kGroup, desc) {}
  uint8_t b34 = 1;                      // +0x34
  uint8_t simultaneous = 0;             // +0x35
  int32_t goal_count = 0;               // +0x38
  int32_t x3c = 0;                      // +0x3c
  // +0x40: a byte of whatever the stack holds (a pointer's low byte in
  // 0x0045bf60, unset locals elsewhere); nothing reads it.
  std::list<std::unique_ptr<Objective>> items;  // +0x44, count +0x48
  uint8_t check(ObjectiveHooks& h) override { return check_group(h, 0); }  // 0x0045f150
  uint8_t check_group(ObjectiveHooks& h, uint8_t announce);              // 0x0045e540
};

// destroy_entity (vtable 0x005c2668 over the named target 0x005c2680, 0x40 bytes).
class DestroyObjective : public Objective {
 public:
  DestroyObjective(const char* desc, const char* name, zgame::Handle entity);  // 0x0045f2b0 inline
  DestroyObjective(Kind k, const char* desc, const char* name, zgame::Handle entity);  // the same base, another type
  std::optional<std::string> name;      // +0x34
  zgame::Handle entity = 0;                    // +0x38
  uint8_t x3c = 0;                      // +0x3c
  void find_entity(ObjectiveHooks& h);  // 0x0045e630
  void relabel(ObjectiveHooks& h);      // 0x0045f340
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f3b0
};

// node_change (vtable 0x005c2730, 0x3c bytes).
class NodeChangeObjective : public Objective {
 public:
  NodeChangeObjective(const char* desc, zgame::Handle n, uint8_t a) : Objective(kNodeChange, desc), node(n), active(a) {}
  zgame::Handle node;                          // +0x34
  uint8_t active;                       // +0x38
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f180
};

// reach_area (vtable 0x005c2728, 0x4c bytes): a node, a nav point or a point.
class ReachAreaObjective : public Objective {
 public:
  ReachAreaObjective(const char* desc, zgame::Handle n, const float p[3], float r);
  zgame::Handle node;                          // +0x34
  float point[3];                       // +0x38
  float radius;                         // +0x44
  float radius_sq;                      // +0x48
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045e890
};

// valve_change (vtable 0x005c26f8, 0x3c bytes).
class ValveChangeObjective : public Objective {
 public:
  ValveChangeObjective(const char* desc, zgame::Handle v, int32_t x) : Objective(kValveChange, desc), watch(v), target(x) {}
  zgame::Handle watch;                         // +0x34
  int32_t target;                       // +0x38
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f1c0
};

// Training's types (piece 5, docs/objectives.md, Training's types). Each
// completes as its test holds and clears again when it does not (complete's
// argument), except where noted.

// reach_velocity (vtable 0x005c2720, 0x3c bytes): the player's screen's speed
// against `velocity` by `test` (0 GT, 1 GE, 2 EQ, 3 NE, 4 LE, 5 LT).
class ReachVelocityObjective : public Objective {
 public:
  ReachVelocityObjective(const char* d, int32_t t, float v) : Objective(kReachVelocity, d), test(t), velocity(v) {}
  int32_t test;                         // +0x34
  float velocity;                       // +0x38
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f630
};

// aim_at_object (vtable 0x005c2718, 0x38 bytes): the reticle's aim on the
// node or a node under it.
class AimAtObjective : public Objective {
 public:
  AimAtObjective(const char* d, zgame::Handle n) : Objective(kAimAt, d), node(n) {}
  zgame::Handle node;                   // +0x34
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f800
};

// target_object (vtable 0x005c2708 over the named target, 0x40 bytes): the
// entity targeted, and its `part` aimed at when one is named.
class TargetObjective : public DestroyObjective {
 public:
  TargetObjective(const char* d, const char* n, zgame::Handle e, int32_t p)
      : DestroyObjective(kTarget, d, n, e), part(p) {}
  int32_t part;                         // +0x3c  (over the destroy's byte)
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f8f0
};

// time_limit (vtable 0x005c2700, 0x38 bytes): the mission's clock past `seconds`.
class TimeLimitObjective : public Objective {
 public:
  TimeLimitObjective(const char* d, float s) : Objective(kTimeLimit, d), seconds(s) {}
  float seconds;                        // +0x34
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045e6c0
};

// view_object and face_object (vtables 0x005c26e8, 0x005c26d8 over the named
// target, 0x3c bytes): the view camera's, or the player's mech's, level
// direction within about 11 degrees of the entity's (a dot product over 0.98).
class FacingObjective : public DestroyObjective {
 public:
  FacingObjective(Kind k, const char* d, const char* n, zgame::Handle e) : DestroyObjective(k, d, n, e) {}
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f400 (view), 0x0045f520 (face)
};

// heat_warning (vtable 0x005c26d0, 0x34 bytes): the player's heat warning on.
class HeatWarningObjective : public Objective {
 public:
  explicit HeatWarningObjective(const char* d) : Objective(kHeatWarning, d) {}
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f7b0
};

// User_Command, joystick_press, key_press, mouse_press (vtables 0x005c26c8,
// 0x005c26c0, 0x005c26b8, 0x005c26b0): a key (-1 none), a joystick button
// and a mouse button (0 none) down.
class InputObjective : public Objective {
 public:
  InputObjective(Kind k, const char* d, int32_t a, int32_t b = 0, int32_t c = 0)
      : Objective(k, d), x34(a), x38(b), x3c(c) {}
  // User_Command: the key, the joystick button, the mouse button; the others
  // their one (key_press's key, joystick_press's and mouse_press's button).
  int32_t x34;                          // +0x34
  int32_t x38;                          // +0x38  (User_Command only)
  int32_t x3c;                          // +0x3c  (User_Command only)
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045e790, 0x0045e760, 0x0045e700, 0x0045e730
};

// weapon_fire, weapon_select, weapon_group_select (vtables 0x005c26a8,
// 0x005c26a0, 0x005c2698, 0x38 bytes): the weapon (0..36) fired or chosen,
// the group (0..4) chosen.
class WeaponObjective : public Objective {
 public:
  WeaponObjective(Kind k, const char* d, int32_t v) : Objective(k, d), id(v) {}
  int32_t id;                           // +0x34
  uint8_t check(ObjectiveHooks& h) override;  // 0x0045f220, 0x0045e800, 0x0045e850
};

// The missions object's +0x78..+0x88.
struct Objectives {
  std::optional<std::string> desc;      // +0x78 (calloc'd)
  std::unique_ptr<ObjectiveGroup> primary, secondary, final, fail;  // +0x7c..+0x88
};

// The training game's lessons (its +0x44 object: +0x38..+0x40 the list, 0x14
// bytes each, +0x44 the current one). The mission's objectives point at the
// current lesson's (training's objectives_free releases them, the lessons
// own them).
struct TrainingLessons {
  std::vector<std::unique_ptr<Objectives>> list;
  int32_t current = -1;
  int32_t add(std::unique_ptr<Objectives> o);  // 0x0045ed40: the old count
  int32_t select(int32_t i);                   // 0x0045efa0: when in range; the current one
  int32_t get(Objectives& mission);            // 0x0045efd0: the current one's into the mission's
  int32_t next(Objectives& mission);           // 0x0045f080: the next one, or none (-1)
  void clear();                                // 0x0045ef10
};
// Mech3.exe 1.2 @ 0x00437140 (the training game's vt+0x40): `block` of
// `file` as one lesson read into `mission`; else `<block>_set`'s lists, else
// the blocks `<block>1`, `<block>2`... while they read, each a lesson, the
// first one given to the mission. False when none reads.
bool training_load(TrainingLessons& t, const char* file, const char* block, Objectives& mission, ObjectiveHooks& h);

// Mech3.exe 1.2 @ 0x0045c2d0: `block` of `file` into `o`; false when the file
// or block is missing or no group has an objective.
// `dir` (the mission's directory, or null) picks the data typos fixed after
// the read (fix_objective_data).
bool objectives_load(const char* file, const char* block, Objectives& o, ObjectiveHooks& h, const char* dir = nullptr);
// FIX (bug), a data typo (fixes.md, Data typos; for the importer when there is
// one): c2 m3's secondary destroy objectives name firefly_01 / firefly_02,
// the mission makes firefly_03 / firefly_04; pointed at those.
void fix_objective_data(const char* dir, const char* block, Objectives& o, ObjectiveHooks& h);
bool read_objectives(zrdr::Node* block, Objectives& o, ObjectiveHooks& h);      // 0x0045c340
void read_group(zrdr::Node* block, const char* key, const char* default_desc,
                std::unique_ptr<ObjectiveGroup>& out, ObjectiveHooks& h);        // 0x0045bf60
void add_mobile_hq(const char* desc, std::unique_ptr<ObjectiveGroup>& fail, ObjectiveHooks& h);  // 0x0045bc90
// 0x0045c920: the condition whose type name is `slot` (its arguments follow
// it). Types the port does not read yet report themselves through unported().
std::unique_ptr<Objective> parse_condition(zrdr::Node* slot, ObjectiveHooks& h);
std::unique_ptr<Store> read_salvage(zrdr::Node* node);                           // 0x004711a0
void read_node_salvage(Objective& o, zrdr::Node* cond, ObjectiveHooks& h);       // 0x0045e9f0
zgame::Handle find_node(zrdr::Node* args, ObjectiveHooks& h);                           // 0x0045c810 ("node")
void objectives_free(Objectives& o, ObjectiveHooks& h);                          // 0x0045c1d0
void objectives_delete(Objectives& o);                                           // 0x0045c250
// Mech3.exe 1.2 @ 0x0045e110: done set; when it is newly true (with a player
// vehicle) the voice, the message panel's lines, the salvage into the
// mission's store, set_valve, the HUD's objectives box.
void complete(Objective& o, uint8_t done, ObjectiveHooks& h);
// Mech3.exe 1.2 @ 0x0045c180: each of primary, final and fail that is done
// is visible (the cockpit's flash of the objectives).
uint8_t objectives_visible(const ObjectiveGroup* primary, const ObjectiveGroup* final, const ObjectiveGroup* fail);
// Mech3.exe 1.2 @ 0x0045c4a0: Instant Action's and multiplayer's "destroy
// the enemies": a group of every active hostile object into primary
// (secondary in multiplayer).
void add_enemy_objectives(std::unique_ptr<ObjectiveGroup>& primary, std::unique_ptr<ObjectiveGroup>& secondary,
                          ObjectiveHooks& h);

}  // namespace mech3
