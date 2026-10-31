#pragma once
#include <cstdint>

#include "gamez/zweapon/zwep_init.h"
#include "mech3/vec3.h"

// A mech's parts and how they take damage (docs/damage.md). Original file:
// mech3\mech_parts.cpp (the base class sits just before its string, the
// subclasses just after).
namespace mech3 {

struct Mech;
struct MechData;

using Handle = void*;  // an object of a system not ported yet (node, effect, entity)

// The scene-node calls the parts' pose makes (zClass/Object3d.c). Unset: nothing.
struct PoseCalls {
  void (*node_set)(Handle node, const float* m) = nullptr;  // 0x004fc300: m (3x4) is the node's new matrix
  float* (*node_matrix)(Handle node) = nullptr;             // 0x004fc3f0: the object3d's local 3x4
};
PoseCalls& pose_calls();

struct GroundHit;
struct MechDfn;
struct Config;
class MechPart;

// What the parts' setups and updates call outside the parts
// (docs/movement-ground.md, The part updates). The first group run the
// engine's ports when unset; the rest do nothing (and return 0).
struct PartCalls {
  int (*world_position)(Handle node, Vec3* out) = nullptr;  // 0x004ed530; unset: zcls::node_world_position
  int (*world_point)(Handle node, Vec3* p) = nullptr;       // 0x004ed650; unset: zcls::node_world_point
  int (*node_to_world)(Handle node, int mode) = nullptr;    // 0x004ed280 onto the matrix stack; unset: zcls::node_to_world
  const MechDfn* (*dfn)(Mech&) = nullptr;                   // 0x004846a0 on +0x684 +0x0c; unset: mech_dfn

  void (*register_part)(Mech&, MechPart&) = nullptr;        // 0x0041b8d0 (the base setup)
  void (*init)(MechPart&) = nullptr;                        // vtable +0x08 (0x0045fe00, armor and structure)
  void (*tree_update)(Handle node) = nullptr;               // 0x004f7db0 on the mech's node (the leg setup)
  Handle (*foot_effect)() = nullptr;                        // 0x0045f980(0x5e5000, 0x5e5010, 0x5e5020): the splash
  bool (*effect_playing)(Handle effect) = nullptr;          // 0x0045fb80
  void (*effect_free)(Handle effect) = nullptr;             // 0x0045fab0 and the delete: out of the world
  void (*effect_start)(Handle effect, float throttle) = nullptr;  // 0x0045faf0
  void (*effect_place)(Handle effect, float x, float y, float z) = nullptr;  // 0x004fc0b0 on the effect's node (+0x04)
  void (*step_on)(const Vec3& at, GroundHit& hit, float force) = nullptr;   // 0x005836b0: the node's handler (+0xbc +0x0c)
  void (*footstep_ff)(float speed) = nullptr;               // 0x004337c0 on [0x00608620]
  void (*play_at)(Handle sound, const Vec3* at, float volume) = nullptr;  // 0x0056e7e0(.., 0)
  void (*look_limits)(Mech&, float* max_yaw, float* max_pitch, float* min_pitch) = nullptr;  // vt +0x10c(1), vt +0x1c: +0x68, +0x74, +0x70
  bool (*aim_point)(Mech&, Vec3* at) = nullptr;             // +0xd4: +0x18 has a target, +0x0c its point (the arms' aim)
  int (*node_angles)(Handle node, float out[3]) = nullptr;  // 0x004ed5d0: the node's world Euler angles; unset: zcls::node_world_euler
  uint32_t (*screen_flags)(void* screen) = nullptr;         // the HUD's +0x1514
  void* (*sound)(Mech&, int id, float volume) = nullptr;    // 0x00432990(id, volume, 0, 0, -1) on +0x684: not placed
  int (*flag8)(Handle node, int32_t* get, int set) = nullptr;  // 0x004eb660 (get) / 0x004eb620 (set); unset: zcls's
  void (*ground_at)(float x, float y, float z, void* hits) = nullptr;  // 0x004edab0 on [0x00608c84] +4 (a GroundHits)
  uint32_t (*material_soil)(Handle material) = nullptr;     // +0x20; unset: zmod::Material::soil
  Handle (*find_in_tree)(Handle node, const char* name) = nullptr;  // 0x004f9d60; unset: zcls::node_find_in_tree
};
PartCalls& part_calls();

// The engine's side of a part flying off (0x00461d80); unset ones do nothing
// (`copy` and `create` give none, so the part stays).
struct FlyCalls {
  Handle (*copy)(Handle stub) = nullptr;               // 0x004f9af0(stub, 0, 0)
  Handle (*create)() = nullptr;                        // 0x004fb740, an empty object3d
  void (*report)(const char* mech, int32_t id) = nullptr;  // 0x0049bfd0(0x400, .., 0x4a4, "Failed to copy stub for %s partId %d")
  void (*rename)(Handle node, Handle from) = nullptr;  // 0x004eb300 with `from`'s name
  Handle (*parent)(Handle node) = nullptr;             // its first parent (+0x58), none without (+0x54)
  void (*set_flag8)(Handle node, int32_t on) = nullptr;  // 0x004eb620
  void (*remove_child)(Handle parent, Handle child) = nullptr;  // 0x004ebb40
  void (*attach)(Handle node) = nullptr;               // [0x00608c84] vt+0: under the world
  void (*set_flags2c)(Handle node, int32_t bit, int32_t on) = nullptr;  // +0x2c bit 5 (0x004eb8a0), 6 (0x004eb8e0)
  void (*set_pose)(Handle node, const float m[12]) = nullptr;  // 0x00461d30: m's translation, its angles (0x00523bc0)
  void (*add_child)(Handle parent, Handle child) = nullptr;    // 0x004eb9b0
  void (*clear_ids)(Handle node) = nullptr;            // 0x00462000: +0x28, +0x40, +0xbc down the tree
  void (*set_node)(MechPart& p, Handle node) = nullptr;  // vt+0x0c, 0x00460190 (part_set_node)
  void (*mark)(MechPart& p, int32_t id) = nullptr;     // 0x00460050(id, 0): part_mark_nodes
  bool (*instant_action)() = nullptr;                  // 0x00479460
  bool (*opacity_used)(Handle node) = nullptr;         // 0x004fbb10
  void (*use_opacity)(Handle node, int32_t on) = nullptr;  // 0x004fbb90
  float (*opacity)(Handle node) = nullptr;             // 0x004fbaa0
  void (*set_opacity)(Handle node, float v) = nullptr;     // 0x004fba20
};
FlyCalls& fly_calls();

// A part deleted, the exe's vtable +0 with 1: release, then the delete.
void part_delete(MechPart* p);

// What tearing a part or a mech down calls outside the mech code (the
// destructors 0x0045fd40.., 0x00414f40, 0x004152d0); unset ones do nothing.
struct FreeCalls {
  void (*stop_effect)(Handle effect) = nullptr;                // 0x00506ca0
  void (*ref_free)(Handle ref, Handle node) = nullptr;         // +0x68: 0x004e66a0, then 0x005ac5c0 and the delete
  void (*rename)(Handle node, const char* name) = nullptr;     // 0x004eb300
  void (*set_flags2c)(Handle node, int32_t bit, int32_t on) = nullptr;  // 0x004eb8a0 (5), 0x004eb8e0 (6)
  void (*clear_ids)(Handle node) = nullptr;                    // 0x004600b0: +0x28, +0x40 down the tree
  // The mech's.
  void (*debris_free)(Handle node) = nullptr;                  // 0x00413db0 on each: off its parent, destroyed
  void (*decals_free)(Handle decals) = nullptr;                // 0x00473f40 and the delete
  void (*torso_cockpit_off)(Mech& m) = nullptr;                // 0x0041ce10(0)
  bool (*viewed)(Mech& m) = nullptr;                           // [0x005f4c8c] +0x64 is the mech
  void (*view_detach)() = nullptr;                             // 0x0040adf0(0, 0) on the view
  void (*handlers_off)(Mech& m) = nullptr;                     // the other model added, 0x00583400, removed again
  void (*ref_delete)(Handle x68c) = nullptr;                   // +0x68c, vt+0 (1)
  void (*controls_free)(Handle controls) = nullptr;               // +0x37c when its vt+0x18 says so, vt+0 (1)
  void (*node_free)(Mech& m) = nullptr;                        // the node out of the world, its models off, "deletedmech", the three trees destroyed
  void (*entity_free)(Mech& m) = nullptr;                      // vt+0x04(0, 0, 0, 0) and 0x005a7f70: off zEntity's list
  void (*player_object_free)(Mech& m) = nullptr;               // 0x0041e580: +0x378, vt+0 (1), for the pilot's mech
  // The cockpit (Torso::set_cockpit): the type's cockpit node (+0x228 +4;
  // 0x00430ad0 makes the block, mech_data's make_cockpit), whether it is the torso node's
  // child (0x004600e0), its first parent, and the node calls.
  Handle (*cockpit_node)(MechData& data) = nullptr;
  bool (*is_child)(Handle parent, Handle child) = nullptr;
  Handle (*first_parent)(Handle node) = nullptr;
  void (*remove_child)(Handle parent, Handle child) = nullptr;  // 0x004ebb40
  void (*add_child)(Handle parent, Handle child) = nullptr;     // 0x004eb9b0
  void (*mark)(MechPart& p, int32_t id, Handle node) = nullptr; // 0x00460050
};
FreeCalls& free_calls();
// The end callback a part's effects get (0x00508f00 with 0x00461c90, ECX the
// effect, EDX the part): on the stop (value 0) the handle holding it clears.
void part_effect_ended(Handle effect, MechPart& p, int32_t value);

// The weapon a hit comes from: the zWeapon definition and its MW3 half
// (mech3/weapon.h, docs/weapons.md).
struct WeaponType;
using WeaponDef = zwep::WeaponDef;

struct Collision {
  Handle x20 = nullptr;   // +0x20
  Handle node = nullptr;  // +0x24  the node hit
};

enum PartId : int32_t {
  kTorso = 1, kLeftArm, kRightArm, kLeftThigh, kRightThigh, kHead, kLeftTorso, kRightTorso,
  kLeftHand, kRightHand, kLeftCalf, kRightCalf, kLeftFoot, kRightFoot, kHip,
};

enum CriticalKind : int32_t { kCritSink = 0, kCritWeapon, kCritAmmo, kCritEquipment, kCritSpecial };

// Base part, vtable 0x005c2840. Armor and internal structure are fractions,
// 1 = intact; the scales are 100 / points.
class MechPart {
 public:
  Mech* mech = nullptr;          // +0x04  (looked up lazily in the exe, 0x460000)
  Handle node = nullptr;         // +0x08  `found` when it is an object3d (0x00460190)
  Handle found = nullptr;        // +0x0c  the part's node by name under the mech's node
  Handle lod = nullptr;          // +0x10  and under the mech's low-detail model (+0x698)
  float* rotation = nullptr;     // +0x14  the node's local 3x3 (object3d data +0x30); the translation follows it
  Vec3* translation = nullptr;   // +0x18  the node's local translation (object3d data +0x54, 0x00460190)
  float* x1c = nullptr;          // +0x1c  object3d data +0x30 again
  float* x20 = nullptr;          // +0x20  data +0x3c
  float* x24 = nullptr;          // +0x24  data +0x48
  Vec3* x28 = nullptr;           // +0x28  data +0x54 again
  float x2c = 0;                 // +0x2c  the arm's setup sets 1
  uint32_t x30 = 0;              // +0x30
  float recoil = 0;              // +0x34  an arm's: 0..1, the kick (0x00466610), decaying
  int32_t id = -1;               // node +0x28, -1 without a node
  float internal = 1.0f;         // +0x38
  float internal_scale = 1.0f;   // +0x3c
  float armor = 1.0f;            // +0x40
  float armor_scale = 100.0f;    // +0x44
  bool destroyed = false;        // +0x48
  bool submerged = false;        // +0x49  under the feet's water (the base update 0x00461730)
  bool pending = false;          // +0x4a  a pose to apply instead of the next one (+0x4c, +0x58)
  Vec3 pending_pos{};            // +0x4c
  float pending_q[4] = {1, 0, 0, 0};  // +0x58  (a foot's target rotation, set_pose 0x004629b0)
  Handle x68 = nullptr;          // +0x68
  Handle debris_effect = nullptr;  // +0x6c
  Handle trail_effect = nullptr;   // +0x70

  virtual ~MechPart() = default;
  // The exe's destructor body (base 0x0045fd40; torso 0x00464250, thigh
  // 0x00463740, arm 0x00466380, foot 0x004628c0): part_delete runs it, then
  // the delete. The C++ destructor stays empty for the tests' stack parts.
  virtual void release();
  virtual void switch_lod(int32_t lod);  // +0x5c, 0x00467290 (foot 0x004672f0; mech_lod.cpp)
  virtual bool setup();          // +0x04, 0x0045fdd0 (hip 0x00462130, leg 0x004637b0, foot 0x00462860, arm 0x004663e0, hand 0x00466e80)
  virtual bool init(const Config* cfg);  // +0x08, 0x0045fe00: armor and structure (mech_part_build.cpp)
  virtual void update(float dt); // +0x10, 0x00461730 (torso 0x00464e80, hip 0x004623a0, leg 0x00463ee0, foot 0x00463110, arm 0x00466510)
  virtual void node_changed();   // +0x14, 0x00460180: the node's matrix set (0x004fc300); torso 0x004659c0
  virtual int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse);  // +0x18
  virtual void apply_impact(Vec3*, float) {}                  // +0x1c, 0x0040baf0
  virtual void destroy(int variant, int fly);                 // +0x20
  virtual bool critical(int kind, int section);               // +0x28
  virtual void apply_damage(float d, int section);            // +0x38
  virtual void set_internal(float v, int section);            // +0x3c
  virtual void set_armor(float v, int section);               // +0x40
  virtual float get_armor(int) const { return armor; }        // +0x44, 0x00467340
  virtual float get_internal(int) const { return internal; }  // +0x48, 0x00467350
  virtual float get_armor_scale(int) const { return armor_scale; }        // +0x4c, 0x00467360
  virtual float get_internal_scale(int) const { return internal_scale; }  // +0x50, 0x00467370
  virtual void set_pose(const Vec3* pos, const float* q);  // +0x2c, 0x00460280 (hip 0x004624b0, torso 0x00464600, foot 0x004629b0)
  void place(const Vec3* pos, const float* q);  // 0x00460210: through the base set_pose whatever the part (null pos: as it is)

  void detach();                 // vtable +0x0c with no node, 0x00460190
  void lose_contents();          // 0x004617f0
  bool has_equipment(int kind);  // 0x00460cc0
  // vtable +0x60: DamageCalls::fly_off when set (a test's), else fly_away.
  Handle fly_off(Vec3* from, Vec3* x, Vec3* velocity, int flag);
  // 0x00461d80 (thigh 0x00466fe0, arm 0x00467100; `from` and `x` unread):
  // the part's node goes into the world at its pose, a stub (with `stub`)
  // or an empty node in its place; the flying node, with its velocity.
  virtual Handle fly_away(Vec3* velocity, bool stub);
  int base_take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) {
    return MechPart::take_hit(def, coll, damage, impulse);
  }
};

class Hand;
class Calf;
class Foot;

// Torso, vtable 0x005c29e0, 0x1b4 bytes: also the side torsos, as sections
// 0-2 front (centre, left, right) and 3-5 rear.
class Torso : public MechPart {
 public:
  Handle death_effect = nullptr;   // +0x74
  Handle death_effect2 = nullptr;  // +0x78
  float half_width = 0;            // +0x7c
  float half_depth = 0;            // +0x80
  int32_t mode = 0;                // +0x084  0 the AI aims, 1 the player's controls, else frozen
  int32_t aim_local = 0;           // +0x088  the aim direction is in the mech's frame
  Vec3 aim_dir{};                  // +0x08c
  float facing = 0;                // +0x098  the aim direction along the torso's forward (the player: 1)
  int32_t aim = 0;                 // +0x09c  the AI's aim, cancelled by any player input
  bool absolute = false;           // +0x0a0  the inputs are positions, not rates
  bool pitch_lock = false;         // +0x0a1  pitch returns to level, its input dropped
  bool view_aim = false;           // +0x0a2  the AI's aim turns the view (+0xa4), not the torso; no servo sound
  float view_q[4] = {1, 0, 0, 0};  // +0x0a4  the view: the torso turned by the free look
  float look_yaw_angle = 0, look_pitch_angle = 0;  // +0x0b4, +0x0b8  the free look's angles
  float twist_in = 0, pitch_in = 0;    // +0x0bc, +0x0c0  the controls' twist and pitch
  float look_yaw = 0, look_pitch = 0;  // +0x0c4, +0x0c8  the free look
  float kick = 0, kick_twist = 0;  // +0x0d0, +0x0d4  the arms' recoil twist (0x00466610) and its spring
  Vec3 jolt_spin{};                // +0xd8
  Vec3 jolt_turn{};                // +0x0e4  the jolt's spin, integrated
  float jolt_q[4] = {1, 0, 0, 0};  // +0x0f0
  Vec3 jolt{};                     // +0x100
  Vec3 jolt_pos{};                 // +0x10c  the shake spring's position (velocity: jolt)
  float turn_q[4] = {1, 0, 0, 0};  // +0x118  yaw and pitch
  float pitch = 0, yaw = 0;        // +0x138, +0x13c
  float min_pitch = 0, max_pitch = 0, max_yaw = 0;  // +0x140, +0x144, +0x148  def +0x17c, +0x178, +0x174
  float jolt_scale[3] = {};        // +0x14c
  float freq = 0, damp = 0;        // +0x158, +0x15c  def freq, damp_freq: the shake spring
  Vec3 front{};                    // +0x160  the torso's forward
  Vec3 flat_front{};               // +0x16c  and on the ground plane, unit
  float sec_armor[6] = {};         // +0x174 + 4s, sections 1-5
  float sec_armor_scale[6] = {};   // +0x188 + 4s, sections 1-5
  float side_internal[2] = {1, 1};        // +0x1a0, +0x1a4
  float side_internal_scale[2] = {1, 1};  // +0x1a8, +0x1ac
  float rot[4] = {1, 0, 0, 0};     // +0x128  the pose's torso rotation, applied by the torso's update
  int32_t centring = 0;            // +0x1b0  CMD_CENTER_TORSO 1, CMD_CENTER_LEGS 2 (docs/movement-ground.md)

  void set_pose(const Vec3* pos, const float* q) override;  // 0x00464600
  void release() override;                                  // 0x00464250: the death effects stopped
  // 0x00464580 (the mech's 0x0041ce10): the cockpit model (the type's +0x228
  // block's [1]) taken off the torso's node with its ids, and with `on` put
  // back under it as part 1's.
  void set_cockpit(bool on);
  // The look keys' and the controls' calls on the torso (docs/look.md).
  void set_pitch_lock(bool on);          // 0x00464ac0: [0x005e4f28] (1) and on
  void hold_view(bool on);               // 0x00464c90: on, the view (+0xa4) taken from the turn, the look zeroed
  bool set_aim(const Vec3* dir, Vec3* out, bool local);  // 0x00465ea0: false when clamped; null: the aim off
  void aim_ahead(bool local);            // 0x00465f20 with (0, 0, -1)
  void centre(int32_t how);              // 0x00465a00: CENTER_TORSO 1, CENTER_LEGS 2
  void set_mode(int32_t m);              // 0x00466190: +0x84 (2 for a network mech, vt+0x5c), the servo stopped
  bool init(const Config* cfg) override;                    // 0x004644b0
  bool setup() override;                                    // 0x00464290
  void update(float dt) override;                           // 0x00464e80
  void node_changed() override;                             // 0x004659c0
  bool steer(float dt, float* yaw, float* pitch, const float* yaw_in, float* pitch_in);  // 0x00464ce0: true when clamped
  bool aim_angles(const Vec3& dir, float* pitch, float* yaw) const;  // 0x00465bc0: false when clamped
  bool aim_view(Vec3* dir, float* pitch, float* yaw);       // 0x00465d10: true when clamped (dir then the clamped one)
  bool face_angles(const Vec3& dir, float* pitch, float* yaw) const;  // 0x00465c70: aim_angles in the mech's frame
  float update_facing();                                    // 0x00465ad0: facing again (+0x98)
  void shake_q(float out[4]) const;                         // 0x00465940

  int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) override;  // 0x00464720
  void apply_impact(Vec3* impulse, float force) override;  // 0x00464af0
  void destroy(int explode, int fly) override;             // 0x00465f40
  bool critical(int kind, int section) override { return MechPart::critical(kind, section); }  // 0x004647d0
  void set_internal(float v, int section) override;        // 0x00464830
  void set_armor(float v, int section) override;           // 0x00464950
  float get_armor(int s) const override;                   // 0x004649c0
  float get_internal(int s) const override;                // 0x00464a20
  float get_armor_scale(int s) const override;             // 0x004649f0
  float get_internal_scale(int s) const override;          // 0x00464a70
  int hit_section(const Vec3* shot) const;                 // 0x00460350
  bool set_twist(float twist, float look);                 // 0x00466200
  bool set_pitch(float pitch, float look);                 // 0x00466240

 private:
  float* internal_slot(int s);
};

// Thigh, vtable 0x005c2978, 0xd4 bytes; the calf and foot hang off it.
class Leg : public MechPart {
 public:
  Calf* calf = nullptr;          // +0x74
  Foot* foot = nullptr;          // +0x78
  bool knee_back = false;        // +0x7c  mech +0x585 at setup (0x004637b0): the knee bends the other way
  float thigh_angle = 0;         // +0x80  the IK's pitch at the hip
  float knee_angle = 0;          // +0x84
  float thigh_q[4] = {1, 0, 0, 0};  // +0x88
  float calf_q[4] = {1, 0, 0, 0};   // +0x98
  // Set up by setup() 0x004637b0 from the rest pose: the leg's lengths and offsets.
  float rest_x = 0;              // +0xa8  the thigh's rest x in the hip
  float thigh_len = 0;           // +0xac  hip joint to knee
  float ik_b0 = 0;               // +0xb0  (nothing writes it)
  float hip_x = 0;               // +0xb4  the foot's rest x, taken off the target
  float calf_len = 0;            // +0xb8  knee to ankle
  float rest_angle = 0;          // +0xbc  atan2 of the foot's rest y and x from the thigh
  Vec3 ankle{};                  // +0xc0  the foot's rest place in the calf
  float ik_k = 0;                // +0xcc  -(thigh^2 + calf^2)
  float ik_m = 0;                // +0xd0  1 / (2 thigh calf)

  bool solve(const Vec3& p);     // 0x00463c90: the angles and joint rotations for an ankle at p
  bool reach(const Vec3& p, const float q[4]);  // 0x00463e40: the IK onto the thigh, calf and foot
  bool setup() override;         // 0x004637b0
  void update(float dt) override;  // 0x00463ee0: the foot's

  int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) override;  // 0x00463b40
  void destroy(int variant, int fly) override;              // 0x00463f70
  Handle fly_away(Vec3* velocity, bool stub) override;       // 0x00466fe0
  void release() override;                                   // 0x00463740: the calf and the foot deleted
  bool critical(int kind, int section) override;            // 0x00463ef0
};

class Arm : public MechPart {    // vtable 0x005c2a48, 0xd0 bytes
 public:
  Hand* hand = nullptr;          // +0x74
  float length = 0;              // +0x78  to the hand
  float recoil_hold = 0;         // +0x7c  what the recoil decays toward, itself decaying
  float recoil_pitch = 0, recoil_push = 0, recoil_twist = 0;  // +0x80, +0x84, +0x88  def recoil_*
  Vec3 rest{};                   // +0x8c  the arm's rest place
  float aim_pitch = 0, aim_yaw = 0;  // +0x98, +0x9c
  float xa0 = 0;                 // +0xa0
  float aim_q[4] = {1, 0, 0, 0}; // +0xa4
  float aim_rate = 0;            // +0xb4  def aim_slerp_freq
  float min_pitch = 0, max_pitch = 0, min_yaw = 0, max_yaw = 0;  // +0xb8..+0xc4  def aim_*
  float hand_share = 0;          // +0xc8  of the yaw that pitches the arm, the rest left to the hand
  bool hand_aims = false;        // +0xcc  the hand takes the whole aim
  bool setup() override;         // 0x004663e0
  void update(float dt) override;  // 0x00466510
  void aim(float dt);            // 0x00466840
  void recoil_pose();            // 0x004666f0
  void kick(float amount);       // vtable +0x24, 0x00466610: recoil, and the torso's twist
  int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) override;  // 0x00466ba0
  void apply_impact(Vec3* impulse, float force) override;   // 0x00466c30
  void destroy(int variant, int fly) override;              // 0x00466c50
  Handle fly_away(Vec3* velocity, bool stub) override;       // 0x00467100
  void release() override;                                   // 0x00466380: the hand deleted
};

class Head : public MechPart {   // vtable 0x005c2b80, 0x74 bytes
 public:
  int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) override;  // 0x00467200
  void apply_impact(Vec3* impulse, float force) override;   // 0x00466c30
  void destroy(int variant, int fly) override;              // 0x004671e0
};

class Hip : public MechPart {    // vtable 0x005c28a8
 public:
  float rot[4] = {1, 0, 0, 0};    // +0x74  the pose's hip rotation
  float inv[4] = {1, 0, 0, 0};    // +0x84  its inverse
  float track = 0;               // +0x94  between the thighs
  Vec3 offset{};                 // +0x98  added to the pose (y: the crouch, toward crouch_target)
  float target_x = 0;            // +0xa4  (the crouch's target is a vector; only y is read)
  float crouch_target = 0;       // +0xa8
  float target_z = 0;            // +0xac
  void set_pose(const Vec3* pos, const float* q) override;  // 0x004624b0
  bool setup() override;         // 0x00462130
  void update(float dt) override;  // 0x004623a0
  void set_crouch(const Vec3* target);  // 0x004621f0: kept in [-crouch depth, 0]
  void duck(float amount);       // 0x004622a0: down by amount (0..1) x the crouch depth at once
  int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) override;  // 0x00462560
  void apply_impact(Vec3* impulse, float force) override;   // 0x00466c30
  void destroy(int variant, int fly) override;              // 0x00462490
};

// The parts below have no armor or structure: hits go to their parent.
class Foot : public MechPart {   // vtable 0x005c2910, 0xec bytes
 public:
  float sole = 0;                // +0x74  taken off the clearance
  Vec3 normal{0, 1, 0};          // +0x7c  the ground's, under the foot
  bool x88 = false;              // +0x88  cleared by the ground update
  float clearance = 0;           // +0x8c  height above the ground
  float last_clearance = 0;      // +0x90
  int32_t ground_state = 0;      // +0x94  0 air, 1 touching, 2, 3 planted
  uint32_t x98 = 0, x9c = 0;     // +0x98  the toe count again; +0x9c
  uint32_t toe_count = 0;        // +0xa0
  Handle* toe_nodes = nullptr;   // +0xa4
  float (*toe_q)[4] = nullptr;   // +0xa8  each toe's last rotation
  float rot[4] = {1, 0, 0, 0};    // +0xc4  the foot's rotation as its updates set it (the gait's blend-in starts from it)
  Handle shadow = nullptr;       // +0x78  always 0: the constructor hides <c>_shadow and clears it
  bool in_water = false;         // +0xac
  uint32_t xb0 = 0, xb4 = 0, xb8 = 0;  // +0xb0..+0xb8
  float xbc = -1;                // +0xbc
  bool pivot = false;            // +0xc0  counter-spins while the mech turns in place
  float pivot_rate = 0;          // +0xd4
  float spin = 0;                // +0xd8  the counter-spin's angle
  float water_depth = 0;         // +0xdc  water surface above the foot
  float water_surface = 0;       // +0xe0
  Handle effect = nullptr;       // +0xe4
  int32_t soil = 0;              // +0xe8  docs/structs.md: 8 is lava

  void release() override;       // 0x004628c0: the splash stopped and freed, the toe arrays
  void switch_lod(int32_t lod) override;  // 0x004672f0
  Leg* leg() const;              // 0x00462d80
  int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) override;  // 0x00462980
  void destroy(int variant, int fly) override;              // 0x004629f0
  void set_internal(float v, int section) override;         // 0x00462890
  void stop_effect();                                       // 0x00463100
  void set_pivot(float rate);                               // 0x00462dc0
  void set_pose(const Vec3* pos, const float* q) override;  // 0x004629b0: the target, applied by the foot's update
  void show_shadow(bool on);                                // 0x0041eca0
  bool setup() override;                                    // 0x00462860
  void update(float dt) override;                           // 0x00463110: spin, the step's effects
  float ground_update();                                    // 0x00462a10: returns the clearance
  void tilt(const GroundHit& contact);                      // 0x00462de0
};

class Calf : public MechPart {   // vtable 0x005c2b18
 public:
  Leg* leg() const;              // 0x00466f80
  int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) override;  // 0x00466f50
  void destroy(int variant, int fly) override;              // 0x00466f30
  bool critical(int kind, int section) override;            // 0x00466fc0
  void set_internal(float v, int section) override;         // 0x00466f00
};

class Hand : public MechPart {   // vtable 0x005c2ab0
 public:
  float rot[4] = {1, 0, 0, 0};   // +0x74  the aim's share (0x00466840)
  Vec3 rest{};                   // +0x84  the hand's rest place
  bool setup() override;         // 0x00466e80
  Arm* arm() const;              // 0x00466e40
  int take_hit(WeaponDef* def, Collision* coll, float damage, Vec3* impulse) override;  // 0x00467440
  void apply_impact(Vec3* impulse, float force) override;   // 0x00466c30
  void destroy(int variant, int fly) override;              // 0x00466e10
  void set_internal(float v, int section) override;         // 0x00466eb0
};

// TARGET's view of a mech (vt+0x68 0x0041b9d0, while it is active): each
// part's node blended red (0x004fb8c0, [0x005e4fd8]) by (1 - its internal) x
// 0.6 when `on`, and the blend switched (0x004fb840); a thigh (0x00463aa0)
// tints its foot's node too and the torso (0x004643d0) the hip's, by their
// own internal. Parts' vt+0x54 (0x00462040), in the exe's order: legs, arms,
// head, torso.
float part_view_tint(MechPart& p, bool on);            // 0x00462040, its internal back
float leg_view_tint(Leg& l, bool on);                  // 0x00463aa0
float torso_view_tint(Torso& t, Mech& m, bool on);     // 0x004643d0
void mech_view_tint(Mech& m, bool on);                 // 0x0041b9d0

// The hit factors 0x00413e60 sets at the mechs' set-up by [0x00608e45] (a
// network game): the damage scale is the scripts' DamageScale (0x005e1f68,
// interp_globals()); these two are the legs'. As the exe's data until then.
struct HitGlobals {
  float leg_scale = 1.0f;        // 0x005e1f74  a leg's hit damage (0.5 networked)
  float limp_structure = 0.3f;   // 0x005e1f78  a leg limps under it (0.4 alone, 0.35 networked)
};
HitGlobals& hit_globals();
void hit_globals_init(bool network);                   // 0x00413e60's first part
void hit_globals_reset();  // tests: a new process's (the exe's data), DamageScale too

}  // namespace mech3
