#pragma once
#include <cstdint>
#include <list>

#include "mech3/mech_anim.h"
#include "mech3/mech_collide.h"
#include "mech3/mech_control.h"
#include "mech3/mech_parts.h"
#include "mech3/mech_skis.h"
#include "mech3/zai_target.h"

// The mech object and the parts of it ported so far. Layout: docs/structs.md.
// Original file: mech3\mech.cpp (these functions sit between it and
// mech_anim.cpp); heat in mech.cpp, damage in mech_damage.cpp.
namespace mech3 {

struct Config;
struct Ams;
using Sound = void*;  // a playing sound (zSound voice), 0 when none

struct HeatSinks {     // embedded at mech +0x658
  int32_t tech = 3;    // +0x000  the mech's tech (0x00484720), then the configuration's sink tech
  int32_t count{};     // +0x004  10 by default
  bool doubles{};      // +0x008
  float rate() const;
  bool lose(int32_t n);  // 0x0045fbc0
};

enum : uint32_t {
  kOverride = 0x1000000,
  kPoweredDown = 0x2000000,
};

// The pilot side of a mech (+0x684): the mech type's data (0x234 bytes,
// mech_data.cpp, docs/mech-build.md section 14): its models, sounds,
// animations and the effects its parts and death use (docs/damage.md). One
// per type, cached and shared by every mech of it.
// The cockpit block 0x00430ad0 makes (8 bytes: vtable 0x005bd2f4, +4 the node).
struct CockpitModel {
  Handle node = nullptr;  // +0x04  the type's cockpit model, or a "null_hud_cage" object3d
};

struct MechData {
  float range_sq[2] = {};        // +0x000  the definition's +0x14c and +0x150, squared
  int32_t refs = 0;              // +0x008  the mechs (and lookups) holding it
  int32_t roster = 0;            // +0x00c  index in the mech roster; 7 is the Elemental
  const char* name = nullptr;    // +0x010  the mech's name, motions are <name>_<gait>
  Handle model = nullptr;        // +0x014  the model tree each mech copies (= models[0])
  Handle models[2] = {};         // +0x018  mechlib's trees (0x00484760): mech_<name>, mech_<name>_1 (low detail)
  uint8_t x20 = 0;               // +0x020  loaded: each step clears it on failure
  uint8_t motion_refused = 0;    // ours: refused for a broken motion (modding.md, M3); a log line, no box
  Handle cameras = nullptr;      // +0x024  the view's camera kinds (0x00430e70)
  Handle sounds[25] = {};        // +0x028  by slot (mech_data.cpp kSounds): engine, ..., the mech's own
  MechAnim* anims[13] = {};      // +0x08c  the gait animations by slot (movement.md); copied to each mech
  float trans[13][2] = {};       // +0x0c0  each gait's throttle bounds from the file; scaled into Mech::gait
  MechAction* actions[9] = {};   // +0x128  the one-shots by kind, 1 fallf .. 8 collapser
  struct Canned {                // +0x14c + 0x14 i: eject, jumpjets, narc (0x00431750), 5 slots
    Handle effect = nullptr;     // +0x00
    float offset[3] = {};        // +0x04  "ofs"
    Handle node = nullptr;       // +0x10  "node", by name on list 6
  } canned[5];
  float limp_speed = 13.0f;      // +0x22c  the limps' stride speed
  struct Soils {                 // +0x230 (0x00432670, 0xb0 bytes), by soil (docs/movement-ground.md)
    Handle step_sound[11] = {};  // +0x00  <name><Soil>, else soil<Soil>
    Handle footprint[11] = {};   // +0x2c  a decal effect, the same names
    float scale[11] = {};        // +0x58  1, grass 0.9
  }* soils = nullptr;
  // +0x1a4 + 0xc x id: destroy, detach, stub (ids 2..5; +0x1bc..+0x1e8).
  // FIX (A): its own array; ids 0 and 1 would overlap canned[3..4] in the exe.
  Handle part_effects[6][3] = {};
  Handle death_effects[4] = {};  // +0x1ec  disable, destroy, fwd and back disable
  Handle x1fc = nullptr;         // +0x1fc  cleared, not set
  Handle cockpit_explosion = nullptr;  // +0x200  cpit_effects_explosion
  Handle landing_dust = nullptr;    // +0x204  <name>_landing_effects (0x0041ecf0)
  Handle landing_splash = nullptr;  // +0x208  mech_splash, on a material of soil 1
  Handle water_death = nullptr;  // +0x20c  nuke_splash
  Handle x210 = nullptr;         // +0x210  cleared, not set
  Handle stubs[4] = {};          // +0x214  larm, rarm, lleg, rleg _stub01.flt
  Handle hud = nullptr;          // +0x224  the HUD's model object (0x00430aa0, made when first asked)
  CockpitModel* cockpit_model = nullptr;  // +0x228  0x00430ad0's cockpit block (deleted by 0x00430580)
};

// The vehicle base every vehicle and mech shares (veh_basic.cpp's neighbour,
// ctor 0x004aeb00, dtor 0x004aeb50): ZEntity, ZEntity3d, AiTarget, then the
// weapons and the network's collision record. Mechs: Mech; the rest:
// mech3/veh_basic.h.
struct Vehicle : ai::AiTarget {
  Handle node = nullptr;     // +0x004  the world node
  bool active = true;        // +0x008
  bool x09 = true;           // +0x009  (ZEntity)
  float* transform = nullptr;  // +0x00c  the node's world 3x4 (object3d data +0x30)
  Handle target_node = nullptr;  // +0x010  zEntity's; the mech's node (0x005a7f50)
  Handle centroid = nullptr;     // +0x014  likewise
  Vec3 x18{};                // +0x018  (ZEntity3d)
  Vec3 forward{};            // +0x024  flat forward, (-row 2.x, 0, -row 2.z) normalised (0x5a86e0)
  Vec3 velocity{};           // +0x030
  Vec3 vec_3c{};             // +0x03c  local velocity, zeroed on power-down (ZEntity3d); forward is -z
  float orientation[4] = {1, 0, 0, 0};  // +0x048  quaternion w, x, y, z (yaw only)
  Vec3 vec_58{};             // +0x058
  uint8_t x64 = 0;           // +0x064  (ZEntity3d) the zones it is in: count, then up to 3 (0x004f6c80's record)
  uint8_t zones[3] = {};     // +0x065
  // +0x068..+0x0d3: AiTarget (0x004d56e0, mech3/zai_target.h), the base; its
  // height becomes the model's box's (12 without one).
  void* ammo;                // +0x0d4  weapons and ammo bins: the WeaponManager (weapon.h)
  NetCollision* net_collision = nullptr;  // +0x0d8  the network's collision record
  // zEntity3d's (mech_place.cpp): vt+0x1c 0x005a85a0, the matrix onto the
  // node and the forward vectors from it; 0x005a8500 the markers' matrices.
  void entity_moved();
  void refresh_markers();
};

// A shot for the next network state (0x10 bytes, 0x0046f160 kind 0x16).
struct NetShot {
  int32_t slot = -1;         // +0x00  the weapon's index on its manager
  int32_t x04 = 0;           // +0x04
  int32_t trigger = 0;       // +0x08  the manager's state: 1 pressed, 2 held, 3 released
  uint32_t target = 0;       // +0x0c  the locked target's player, 0 none
};

struct Mech : Vehicle {
  bool power_down_pending{};   // +0x0dc
  bool heat_shutdown{};        // +0x0dd
  bool powered_down{};         // +0x0de
  float last_hit_time = 0;   // +0x0e0
  MechPart* last_hit_part = nullptr;  // +0x0e4
  Handle xe8 = nullptr;      // +0x0e8  zEntity's target node as the model is set (0x005a8690)
  Vec3 part_aim{};           // +0x0ec  the target's chosen part's world point (0x0041d6f0)
  int32_t xf8 = -1;          // +0x0f8  the target's chosen part (0x0041d810), -1 none
  Collider collider;         // +0x0fc..+0x373 (mech_collide.h)
  class ChatPanel* chat = nullptr;  // +0x378  the network game's chat panel (mech3/chat_panel), none
  MechControls* controls = nullptr;  // +0x37c  (docs/controls.md)
  bool blocked[4] = {};      // +0x374  collider (+0xfc) resolved: vehicle, mech/bump, ceiling, wall/touch
  std::list<MechPart*> parts;  // +0x384, count +0x388
  Leg* legs[2]{};              // +0x38c, +0x390
  Arm* arms[2] = {};         // +0x394, +0x398
  Torso* torso = nullptr;    // +0x39c
  Hip* hip = nullptr;        // +0x3a0
  Head* head = nullptr;      // +0x3a4
  Handle jet_node = nullptr;   // +0x3a8  where the jump jets' flame hangs
  int32_t equipment[15] = {};  // +0x3ac  installed count per equipment kind (docs/weapons.md); [11] jump jets, [12] MASC
  Handle decals = nullptr;   // +0x3e8  the skins' decal state (0x00475a80)
  Config* config = nullptr;  // +0x3ec
  Ams* ams = nullptr;        // +0x3f0  the anti-missile system (weapon.h)
  uint32_t x3f4[6] = {};     // +0x3f4  the kill credit: mechs, vehicles, buildings killed, then their points
  float x40c = 1.0f;         // +0x40c  health() cached
  bool damaged = false;      // +0x410  health() to work out again
  bool killed_by_nuke = false;  // +0x411
  float alive_time = 0;      // +0x414  seconds of full updates
  Skis* skis = nullptr;      // +0x41c  the ground probes (mech_skis.h)
  float blend_weight = 0;    // +0x418  the current blend slot's weight
  int32_t anim_index = 2;    // +0x420  the gait's current slot in anims
  MechAnim* anims[13] = {};  // +0x424  docs/movement.md: 0/1 limp, 2 stand, 3 step, 4 walk, 5 trot, 6 run, 7..11 blends, 12 jump pose
  MechAction* action_objects[10] = {};  // +0x458  by kind, 1 fallf .. 8 collapser (docs/movement-jump.md)
  std::list<int32_t> actions;  // +0x480, count +0x484
  Vec3 momentum{};           // +0x488  carried into an action, x0.6 a frame
  Vec3 action_root{};        // +0x494  the action's last root position
  float gait[13][2] = {};    // +0x4a0  (lo, hi) throttle bounds per gait; [3] step, [4] walk, [5] trot, [6] run
  float hip_height = 0;      // +0x508  the hip's rest height (Hip::setup)
  float crouch_depth = 0;    // +0x50c  def crouch_height, else 0.7 hip_height
  float walk_speed = 0;      // +0x510
  float x514 = 0;            // +0x514  0x00424290 on the configuration (it returns 0)
  float max_speed = 0;       // +0x518
  uint32_t x51c = 0;         // +0x51c  the configuration's +0x70
  float turn_rate = 0;       // +0x520  def max_turn_rate / 2
  uint32_t x524 = 0;         // +0x524  the configuration's +0x78
  float speed_factor = 1;    // +0x528  heat, water, limp and reverse, smoothed
  float speed_target = 1;    // +0x52c
  float throttle_up = 0;     // +0x530  controls' throttle rate toward a larger |target| (docs/movement.md)
  float throttle_down = 0;   // +0x534  and toward a smaller one
  float turn_accel = 0;      // +0x538  controls' turn smoothing rate
  float knock = 0;           // +0x53c  length of knock_vec
  Vec3 knock_vec{};          // +0x540
  float lean = 0;            // +0x54c  roll of the display matrix (detail bit 5)
  float x550 = 1.0f;         // +0x550
  float heat{};                // +0x554  0..2
  float heat_added{};          // +0x558  this frame
  float heat_dissipation{};    // +0x55c  this frame
  float jet_burn = 0;        // +0x560  fuel per second firing: tonnage / (100 * jets)
  float jet_fuel = 1;        // +0x564  0..1
  bool infinite_fuel = false;  // +0x568  nothing sets it
  float coolant = 1.0f;      // +0x56c  the flush's (0x00419e60): doubled, spent on the heat, the rest halved
  float narc_time = 0;       // +0x570
  float masc_time{};           // +0x574  non-zero while MASC is on
  int32_t anim_state{};        // +0x578  1, 12, 13 let a power-down finish; 10 dead
  int32_t prev_state = 0;    // +0x57c
  bool no_power_down{};        // +0x584  airborne (vtable +0x144, 0x41ec70)
  int32_t jump_jets_firing{};  // +0x580
  uint8_t hud_saved{};         // +0x587  the screen state kept while powered down
  Vec3 impulse{};            // +0x594  this frame's; y is the jump jets' climb (+0x598)
  Vec3 push{};               // +0x5a0  outside push: slopes, walls, ledges; decays
  float lie_time = 0;        // +0x590  how long a fallen mech stays down before it gets up
  bool knee_back = false;    // +0x585  the knees bend backward (copied to the legs)
  bool crouched = false;     // +0x586
  bool wading = false;       // +0x588  the wading force-feedback effect is on
  uint32_t gait_frames = 0;  // +0x58c  frames at walk or faster since stand or step
  float gait_steps = 0;      // (port) the same in 1/30 s steps, for the per-second blend-in (zfix)
  int32_t limp = -1;         // +0x5ac  the limping leg, -1 none
  bool collides = true;      // +0x5b0  0 for network-remote mechs: no collision
  Sound sounds[25] = {};     // +0x5b8  all stopped on death; [0] walk, [1] wade, [2] limp, [3] the jump jets
  uint32_t x61c = 0;         // +0x61c
  Handle jet_effect = nullptr;  // +0x620  the jump jets' flame
  uint32_t x624 = 0, x628 = 0, x62c = 0;  // +0x624..+0x62c
  int32_t hud_fall = 1;      // +0x638  0x0041ca30: the player's mech 0 while falling, others 1
  bool remote = false;       // vtable +0x5c: the network class's mechs (0x004d2a70), 0x0040bae0 here
  bool invincible{};           // +0x630  the Invincible cheat (docs/damage.md)
  uint32_t x634 = 0;         // +0x634
  float x63c = 1.0f;         // +0x63c
  Vec3 x640{};               // +0x640
  Vec3 lock_velocity{};      // +0x64c  vt+0xc's (0x0041d480): the lead velocity when last asked
  HeatSinks heat_sinks;      // +0x658
  Handle attacker = nullptr;  // +0x664  the last to hit it
  uint8_t net_acked = 0;     // +0x668  the last state sent has arrived (0x004709d0)
  uint32_t net_sent = 0;     // +0x66c  that send's id
  std::list<NetShot> net_shots;  // +0x678, count +0x67c: the shots since the last state sent (0x0046f160 kind 0x16)
  uint32_t net_damaged = 0;  // +0x680  the parts (bit by id) whose damage goes with the next state (kind 0x15)
  MechData* pilot = nullptr;    // +0x684
  Handle x68c = nullptr;     // +0x68c  updated last each frame (0x004e6480)
  uint32_t detail{};           // +0x688  level-of-detail bits by distance (docs/movement.md): 0 sounds, 1 toes, 2 turn pivot, 3 landing effects, 4 full gait, 5 lean
  uint32_t x690 = 0;         // +0x690
  Handle model_node = nullptr;  // +0x694  the model's root under the mech's node
  Handle lod_node = nullptr;  // +0x698  the low-detail model's copy (0x00430c20)
  uint32_t heat_state{};       // +0x69c  bits 0-2 beep, 3-5 shutdown, 24 override, 25 powered down
  float shutdown_time{};       // +0x6a0
  float beep_time{};           // +0x6a4
  float voice_time{};          // +0x6a8
  uint32_t x6ac = 0;         // +0x6ac
  float frame_dt = 0;        // +0x6b0  dt accumulated until a full update
  float dead_time = 0;       // +0x6b4  when it joined the dead mechs' list (0x0041e350)
  float knockdown[5] = {};   // +0x6b8  base, x 0.8, x 80, threshold, slope (0x414c71, 0x417a10)
  std::list<Handle> x6d0;    // +0x6d0, count +0x6d4: the nodes of parts that flew off (0x00413bb0), never taken off

  Sound* loops() { return sounds; }                 // +0x5b8, three: stopped on power-down
  Sound& servo() { return sounds[4]; }              // +0x5c8  the torso's twist
  Sound& heat_beep() { return sounds[5]; }          // +0x5cc
  Sound& heat_alarm() { return sounds[6]; }         // +0x5d0
  Sound& power_up_sound() { return sounds[7]; }     // +0x5d4
  Sound& power_down_sound() { return sounds[8]; }   // +0x5d8
  Sound& shutdown_warning() { return sounds[20]; }  // +0x608
  Sound& shutdown_voice() { return sounds[21]; }    // +0x60c

  bool is_powered_down() const;
  bool airborne() const { return no_power_down; }   // vtable +0x144, 0x0041ec70
  void add_heat(float heat) { heat_added += heat; }  // 0x00419e40
  void power(int power_down);   // 0x0041cc30
  void finish_power_down();     // 0x0041cac0
  void stop_controls(int arg);  // +0x37c vt+0x4c (0x00426c10)
  Foot* foot(int leg) const;    // 0x0041d4f0
  Calf* calf(int leg) const;    // 0x0041d4d0

  // Placement (mech_place.cpp, docs/mission-start.md section 11).
  int place(const float pos[3], float yaw);       // 0x004161c0: on the ground under pos, in the world
  void set_place(const float pos[3], float yaw);  // vtable +0xdc, 0x00418f30
  void reset_at(const float pos[3]);              // 0x00418e90: at rest at pos
  void set_target_node(Handle n);       // 0x005a8680: what others aim at (zEntity +0x10), then 0x005a85c0
  // The keypad's part targeting (docs/look.md; the parts by id: part_by_id,
  // 0x0041d510): the chosen part's point, the choice.
  bool aim_part();                         // 0x0041d6f0
  void target_part(int32_t id);            // 0x0041d810
  // The weapons' aim (mech.cpp): the HUD's reticle point each frame (0x0041cd40).
  bool aim_at(const Vec3* at, void* torso_out, bool check);  // 0x0041bc20
  // 0x005a85c0 with vtable +0x1c 0x005a85a0: the node's matrix and +0x18, +0x24 again

  // Movement (mech_move.cpp).
  int jump_jets(float dt);            // 0x004173b0, returns +0x580
  void add_jump_jet(bool add);        // 0x00417300
  void ground_step(float dt);         // 0x004162c0: the skis, then a landing
  float altitude() const;             // 0x0041e9c0: above the centre probe's ground
  bool queue_action(int kind, bool force);  // 0x00416780
  int state_for_action(int kind) const;     // 0x00416660
  static int sound_for_action(int kind);    // 0x00416720
  bool death_fall(int kind);                // 0x0041b760
  void stagger(const Vec3& knock);          // 0x0041b7f0
  void get_up();                            // 0x0041b840
  void command(int32_t cmd);                // 0x0041be80: the player's keys (mech_command.cpp)
  void heat_override(bool on);              // 0x00419190: the shutdown override (docs/heat.md)
  void flush_coolant();                     // 0x00419e60
  bool repair(float dt);                    // 0x004154e0: a field base's full repair (mech_repair.cpp, vehicles.md)
  void end_limp();                          // 0x00415a90: the limp's blend freed, the limp over
  void update_action(float dt);             // 0x00416860
  bool vital_part_lost() const;             // inlined in 0x00416660 and 0x00416860
  void select_gait(float dt);         // 0x004181c0
  void masc(bool on);                 // 0x0041df50: max speed x1.25, the speed kept
  void crouch(bool on);               // 0x0041bda0: the hip down by the crouch depth (not the Elemental)
  void ground_dust();                 // 0x004164f3.. / 0x00416d3a..: dust or a splash on the ground below
  void landing_pose();                // 0x0041660b..: landf from its start, its root kept
  void move(float dt, MechAnim* anim, float speed);  // 0x00417a80
  // The pose on the parts (mech_pose.cpp, docs/movement-pose.md).
  float gait_blend(int jets) const;   // 0x00417c65..: the blend-in weight, -1 when none
  void pose_gait(MechAnim& a, int jets, const Vec3& saved);  // 0x00417bbc..0x0041818d in Move
  void pose_jump();                   // 0x004177e7..0x004179fd in JumpJets
  void pose_action(MechAction& a, const Vec3& hip, const float q[4]);  // 0x00416fb8..0x00417293 in ActionUpdate
  void pose_toes(MechAnim& a, float blend);  // the toe loops of the three; blend 1: none
  void turn(float dt);                // vtable +0x170, 0x00418fc0
  Leg* pivot_leg() const;             // 0x00418e00
  void update(float dt);              // vtable +0x34, 0x00419fc0: the frame (mech_update.cpp)
  void aim_at_last_hit();             // 0x0041d130
  // The torso's aim as the AI asks it (docs/ai.md 17.7, mech_part_update.cpp):
  bool can_face(const Vec3& dir);                // vtable +0x8c, 0x0041dc70: the torso's limits allow it
  bool clamp_aim(const Vec3& dir, Vec3* out);    // vtable +0x90, 0x0041dca0: false when clamped (out the clamped)
  bool faces_within(float cosine);               // vtable +0x150, 0x0041dba0: the torso's aim cosine above it
  void set_torso_aim(const Vec3* dir);           // vtable +0x154, 0x0041dbd0: the AI's aim (null: off)
  void lead_velocity(Vec3* out) const;           // vtable +0x08, 0x0041d400 (+0x0c's): what others lead it by
  void on_kill(Handle victim);        // vtable +0x80, 0x0041b420: the kill credit
  void fade_debris(float dt);         // 0x00413cb0 (mech_parts.cpp)
  void dead_update(float dt);         // 0x00419ef0 (mech_dead.cpp)
  void entity_step(float dt);         // 0x005a8000: zEntity3d's turn and move by +0x58 / +0x3c (mech_place.cpp)
  void release();                     // 0x004152d0 with 0x00414f40: the destructor's body (mech_build.cpp)
  void death_prep();                  // 0x0041d070
  void hit_timeout();                 // vtable +0x14c, 0x0041d190
  void movement_sounds();             // 0x0041a843..0x0041aa61, in Update
  bool collide();                     // 0x004225c0 (mech_collide.cpp), true when blocked
  void setup_collider();              // 0x00422360 (on the collider, the mech as argument)
  void collide_segments();            // 0x00422d20
  int32_t collide_query(const float* list, int32_t points);  // 0x00422ef0, 1 when nothing was crossed
  void sort_hits(const zcls::SegmentHits& rec, const Vec3& from, const Vec3& to, int32_t probe);  // 0x00423010
  void wall_response(const CollisionHit& h);    // 0x004231c0
  void ceiling_response(const CollisionHit& h); // 0x00423600
  void vehicle_response();                      // 0x004237c0
  void mech_response();                         // 0x00423a20
  bool touch_response();                        // 0x00423c90
  float turn_rate_now() const;                  // 0x00418f70

  // Damage (mech_damage.cpp).
  int take_damage(WeaponDef* def, Collision* coll, float damage);  // vtable +0x164, 0x0041ac60
  void die();                         // vtable +0x70, 0x0041b0d0
  void on_part_destroyed(MechPart* part);  // 0x0041b5f0
  float health();                          // 0x0041ba40 (vtable +0x98): the parts' health, cached at +0x40c
  // vtable +0x118 0x0041d1c0 / +0x120 0x0041d210 (the AI's walking states,
  // docs/ai.md 17.4a): the speed at a share of the top speed, and how far a
  // speed carries while the throttle eases down (speed / decel_rate).
  float speed_at(float share) const { return max_speed * share; }
  float stopping_distance(float speed) const { return speed / throttle_down; }
  // vt+0x114 0x0041d1d0: the speed the smoothed throttle asks (its
  // controls' vt+0x1c, GetThrottle, times the top speed).
  float throttle_speed() const;
  // vt+0x12c 0x0041d240: the throttle share that still stops within `d`,
  // 0..1 (a NaN 0).
  float stop_share(float d) const {
    const float x = throttle_down * d / max_speed;
    if (x > 1.0f) return 1.0f;
    return x < 0.0f || x != x ? 0.0f : x;
  }
  void destroy_part(int id);          // 0x0041cfa0
  bool start_limp(int leg, bool fall);  // 0x0041d5f0
  void fall();                        // 0x0041b640
  bool knocked_down(int kind);        // 0x0041b660
  void set_max_speed(float v);        // 0x004143c0
  void scale_knockdown(float f);      // 0x00417a10
  float tonnage() const;              // 0x00416770
  void mark_narc(bool on);            // 0x0041ac10
  void note_hit();                    // 0x0041d170
};

// Globals of other modules that the heat and damage code reads.
struct World {
  Mech* player = nullptr;           // 0x005f4c8c +0x64
  Mech* mission_player = nullptr;   // 0x0060861c +0x14  (the same mech in play)
  bool force_feedback = false;      // [0x00608620] the force feedback object exists (mech3/force_feedback)
  float difficulty = 1.0f;          // 0x00608e5c  damage taken by your side: 0.7, 1.0, 1.3
  Vec3 shot[2] = {};                // 0x009a0ec4  the hitting shot: from, to
  bool death_anim_toggle = false;   // 0x00605cc0
  bool full_heat_all_mechs = true;  // 0x005e1fd0
  bool no_heat_haze = false;        // 0x00608e4d
  float time = 0.0f;                // 0x0060991c  game time, seconds
  int32_t last_action = 0;          // 0x0060550c  the kind of the last action finished
  float dt = 0.0f;                  // 0x0072c52c
  int32_t dead_updates = 0;         // 0x006099c0  frames a dead mech played its fall
  float move_volume_floor = 0.0f;   // 0x00605630  the movement sounds' volume at a standstill (0)
};
World& world();

// Calls into code that is not ported yet. Unset ones do nothing and return 0.
struct HeatCalls {
  bool (*heat_option)() = nullptr;                          // 0x0040cfc0
  int (*game_type)() = nullptr;                             // pilot 0x0060861c +0x6c, vtable +0x2c; 3 = multiplayer
  float (*throttle)(Mech&) = nullptr;                       // +0x37c GetThrottle (docs/controls.md)
  void* (*screen)(Mech&) = nullptr;                         // 0x00430aa0 on +0x684
  void (*heat_haze)(void* screen, float amount, int kind, float dt) = nullptr;  // 0x0043c1f0
  Sound (*play_sound)(Mech&, int id) = nullptr;             // 0x00432990 on +0x684
  void (*stop_sound)(Sound) = nullptr;                      // 0x0056e9f0
  void (*play_mech_sound)(int index) = nullptr;             // 0x0041e770
  void (*power)(Mech&, int on) = nullptr;                   // test hook; unset calls Mech::power
  void (*torso_hit)(Mech&, int explode) = nullptr;          // test hook; unset: torso destroy (explode, 1)
  bool (*ammo_explosion)(Mech&) = nullptr;                  // 0x004b0de0 on +0x0d4
  void (*hud_message)(float seconds, int message) = nullptr;  // 0x0040fe70
  void (*stop_controls)(Mech&, int arg) = nullptr;          // +0x37c vtable +0x4c (Stop)
  uint8_t (*screen_power_down)(void* screen) = nullptr;     // 0x0043bb20
  void (*screen_power_up)(void* screen, uint8_t saved) = nullptr;  // 0x0043bb50
  Sound (*play_sound_at)(Mech&, int id) = nullptr;          // 0x00432990 at the mech's position
};
HeatCalls& heat_calls();

// The calls damage makes into code that is not ported yet (scene nodes,
// effects, weapons, HUD, sound, network, statistics). Unset ones do nothing
// and return 0; the screen, throttle, sounds and game type come from HeatCalls.
struct DamageCalls {
  Handle (*attacker)() = nullptr;                    // 0x0040eee0 on the shot's owner [0x009a0edc]
  MechPart* (*node_part)(Handle node) = nullptr;     // 0x0040ef30: the part a node is, if any
  Handle (*node_owner)(Handle node) = nullptr;       // 0x0040ef10: the mech a node belongs to
  const char* (*message)(int id) = nullptr;          // 0x00576040, Mech3Msg.dll
  // +0x37c vtable +0x94; unset: the controls' on_hit (0x004aec90, the AI's 0x004ea520).
  void (*controls_hit)(Mech&, WeaponDef*, Collision*, float damage, Handle attacker) = nullptr;
  void (*controls_stop)(Mech&) = nullptr;            // +0x37c vtable +0xa8
  void (*net_event)(Mech&, int kind, intptr_t arg) = nullptr;  // vtable +0x168, 0x0046f160
  void (*killer_notify)(Handle killer, Mech& victim) = nullptr;  // the killer's vtable +0x80 (a mech's: on_kill)
  // The kill credit's reads of the victim (on_kill): its vtable +0x2c, then
  // that object's +0x50 (a mech), +0x40 (a vehicle), +0x48 (a building).
  // Unset: the victim is a mech (itself, itself, itself, none).
  Handle (*victim_view)(Handle victim, int slot) = nullptr;
  void (*controls_killed)(Mech& killer, Handle victim) = nullptr;  // +0x37c's second base, vt+0xb4
  void (*salvage_mech)(Handle mech) = nullptr;        // 0x00472f00 on the mission's store [0x00609990]
  void (*salvage_vehicle)(Handle vehicle) = nullptr;  // 0x00472ec0 on it
  void (*ff)(uint32_t fn, int arg) = nullptr;        // [0x00608620] the force feedback, its method fn
  void (*ff_hit)(int weapon, const Vec3* impulse, float force) = nullptr;  // 0x00433720
  bool (*networked)() = nullptr;                     // 0x00479480 on [0x0060861c]
  int (*view_mode)() = nullptr;                      // [0x005f4c8c] +0x58 +4, -1 without a view
  void (*view_camera)(int mode) = nullptr;           // 0x0040ae70: the view to camera mode 2, outside
  void (*mission_player_died)() = nullptr;           // 0x0048f9e0 on 0x006098d0
  void (*player_died_hud)(void* screen) = nullptr;   // 0x0041b263..0x0041b30c: the cockpit HUD shuts off
  Handle (*roster_x1c)(Mech&) = nullptr;             // 0x00414370
  void (*damage_decal)(Handle x1c, Handle decals, Collision*, float damage) = nullptr;  // 0x00474d90
  void (*last_hit)(Mech&) = nullptr;                 // 0x0041d130
  void (*zaps_hud)(void* screen, float a, int kind, float b) = nullptr;  // 0x0043be70
  void (*head_hit_hud)(Handle explosion, Handle cockpit) = nullptr;  // 0x00508da0(ECX, EDX; 0, 0, 0): the cracks
  void (*hud_weapons)(void* screen) = nullptr;       // 0x00446520 on screen +0x6520
  void (*equip_voice)(int kind) = nullptr;           // 0x00485390: the callout for an equipment kind
  void (*voice)(int index) = nullptr;                // 0x00485370
  bool (*force_variant0)() = nullptr;                // 0x00586ce0
  void (*weapons_lost)(Mech&, int part) = nullptr;   // 0x004b1040 on +0xd4: the part's weapons are destroyed
  bool (*weapons_critical)(Mech&, int part, int ammo) = nullptr;  // 0x004b0de0 on +0xd4
  void (*weapons_stop)(Mech&) = nullptr;             // 0x004b1110 on +0xd4
  Handle (*fly_off)(MechPart&, Vec3* from, Vec3* x, Vec3* velocity, int flag) = nullptr;  // part vtable +0x60
  Handle (*spawn_effect)(Handle effect, Handle node, const float args[9]) = nullptr;  // 0x00508aa0
  Handle (*spawn_effect_at)(Handle effect, Handle node, float x, float y, float z) = nullptr;  // 0x00508c10
  void (*on_effect_end)(Handle effect, MechPart&) = nullptr;  // 0x00508f00 with 0x00461c90
  void (*node_unlink)(Handle x68, Handle node) = nullptr;     // 0x004e66a0
  void (*node_set_active)(Handle node, int on) = nullptr;     // 0x004eb170
  void (*node_set_name)(Handle node, const char* name) = nullptr;  // 0x004eb300
  void (*stop_foot_effect)(Handle effect) = nullptr;          // 0x0045fb50
  void (*free_part)(MechPart*) = nullptr;                     // vtable +0 (1); unset deletes
  const float* (*node_matrix)(MechPart&) = nullptr;          // node +0x38 +0x60, 3x3 + translation
  void (*node_update)(MechPart&) = nullptr;                   // 0x004ed530
  bool (*anim_ok)(Mech&, int kind, int arg) = nullptr;        // 0x00416780; test hook, unset runs Mech::queue_action
  void (*hip_reset)(MechPart& hip, const Vec3* v) = nullptr;  // test hook (0x004621f0): unset runs Hip::set_crouch
  void (*death_prep)(Mech&) = nullptr;                        // 0x0041d070
  Handle (*canned)(Mech&, int slot, Handle node, const float* at, const float* vel) = nullptr;  // test hook (0x00432830 on +0x684): unset runs canned_effect
  void (*death_anim)(Mech&, int anim) = nullptr;              // 0x0041b760
  void (*stop_action)(Handle obj) = nullptr;                  // 0x00421060
  Handle (*blend_anim)(Handle base, Handle limp) = nullptr;   // 0x0041ff30
  void (*anim_speed)(Handle anim, float v) = nullptr;         // 0x0041ef90; test hook, unset runs set_stride_speed
  void (*gait_speed)(Mech&, float v) = nullptr;               // 0x004329c0 on +0x684 with +0x4a0; test hook, unset runs scale_gaits
  const Vec3* (*position)(Mech&) = nullptr;                   // 0x005a85e0, the world position
};
DamageCalls& damage_calls();

// What the aim (0x0041bc20) calls outside the mech code; unset, nothing (the
// view not the cockpit's, the torso not turned: its aim 0x00465ea0 is not
// ported).
struct AimCalls {
  bool (*cockpit_view)(Mech&) = nullptr;   // [0x005f4c8c] +0x64 is the mech, its camera (+0x58) +4 is 1
  int (*torso_aim)(Mech&, const Vec3* dir, void* out, int on) = nullptr;  // 0x00465ea0 on the torso
};
AimCalls& aim_calls();

void update_heat(Mech& m, float dt);  // 0x004192f0
bool update_ams(Mech& m);             // 0x00419200: the AMS's frame; true when it fired
MechPart* part_by_id(Mech& m, int32_t id);  // 0x0041d510: 1, 7, 8 the torso .. 15 the hip

}  // namespace mech3
