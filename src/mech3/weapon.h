#pragma once
#include <cstdint>
#include <list>
#include <vector>

#include "gamez/zmath/zmth_vector.h"
#include "gamez/zweapon/zwep_init.h"
#include "mech3/mech_config.h"
#include "mech3/mech_reader.h"

// mech3\weapon.cpp: the weapons at run time (docs/weapons.md). The functions
// from 0x004af5a0 to 0x004b5000 sit between veh_basic.cpp and the one
// weapon.cpp names (0x004b4660); they are taken to be weapon.cpp.
namespace mech3 {

using WeaponDef = zwep::WeaponDef;

// The MW3 half of a weapon definition, at def +0x1f0: 0xc8 bytes made by
// 0x004b0020 through 0x004af5a0 from the def's weapons.zrd entry.
struct WeaponType {
  int32_t id = -1;                   // +0x00  mech-lab index (0x004848a0 on the def's NAME)
  WeaponDef* def = nullptr;          // +0x04
  float heat = 0;                    // +0x08  HEAT: added to the firing mech
  float heat_2 = 0;                  // +0x0c  HEAT's second value: added to the target (0 in every weapon)
  bool show_on_radar = false;        // +0x10
  float zaps_hud = 0;                // +0x14  ZAPS_HUD: the HUD static a hit adds
  float zaps_hud_2 = 0;              // +0x18  and its hold in seconds (the first value again in the exe)
  struct Lab {
    float tons = 0;                  // MASS clan / sphere
    int32_t slots = 0;
    bool present = false;
    uint8_t pad[3] = {};
  } lab[2];                          // +0x1c Clan, +0x28 Inner Sphere
  float tons_per_round = 0;          // +0x34
  int32_t rounds_per_ton = 0;        // +0x38  MASS ammo, x SALVO truecount
  int32_t salvo_count = 0;           // +0x3c  0 without SALVO
  float salvo_delta = 0;             // +0x40
  int32_t salvo_truecount = 0;       // +0x44
  float impact_force = 0;            // +0x48
  bool is_narc = false;              // +0x4c
  float burst_radius = 0;            // +0x50
  int32_t burst_count = 0;           // +0x54
  WeaponDef* burst = nullptr;        // +0x58  BURST wep
  zwep::Handle looped_sound = nullptr;     // +0x5c  U_LOOPED
  zwep::Handle recharge_sound = nullptr;   // +0x60  U_RECHARGE_SND
  zwep::Handle name_sound = nullptr;       // +0x64  NAMESOUND
  zwep::Handle part_damage_sound = nullptr;   // +0x68  with NAMESOUND
  zwep::Handle part_destroy_sound = nullptr;  // +0x6c
  float range = 0;                   // +0x70  BURST adds the burst's range
  float range_sq = 0;                // +0x74
  float half_range = 0;              // +0x78  beams under water
  float half_range_sq = 0;           // +0x7c
  uint8_t x80 = 0;                   // +0x80  the low byte of the ctor's first argument (the reader node)
  struct SalvoMark {                 // 0x10 bytes
    zwep::Vec3f3 at;                 // where a salvo's shot hit
    float until;                     // +0x0c  game time it is forgotten
  };
  std::list<SalvoMark*> salvo_marks; // +0x84  head, size +0x88 (0x004afc00)
  float salvo_radius_sq = 0;         // +0x8c
  float salvo_delay = 0;             // +0x90
  float recoil = 0;                  // +0x94
  float recharge_rate = 0;           // +0x98  period x U_FIRE_TIME / U_RECHARGE_TIME
  float fire_time = 0.5f;            // +0x9c  U_FIRE_TIME
  WeaponDef* strike_weapon = nullptr;  // +0xa0  U_DESIGNATE_AIRSTRIKE
  // The airstrike's lock (0x004afed0): the missiles home on strike_point.
  zwep::Vec3f3 strike_point{};       // +0xa4
  int32_t strike_flag = 1;           // +0xb0  zWeapon's lock flag
  zwep::LockRef strike_lock;         // +0xb4  the lock: no entity, strike_point, standing still
  float lrm_flight = 0;              // +0xc0  LRMs: time to fly 70% of the range
  float ready_at = 0;                // +0xc4  the AI fires this type again from then (0x004b4c70)

  WeaponType() = default;
  WeaponType(zrdr::Node* node, WeaponDef* def);  // 0x004af5a0
  ~WeaponType();                                 // 0x004afb10
  WeaponType(const WeaponType&) = delete;
  WeaponType& operator=(const WeaponType&) = delete;

  uint32_t salvo_mark(const zwep::Vec3f3& at);   // 0x004afc00
  void strike(zwep::Handle owner_node, const zwep::Vec3f3& dir, const zwep::Vec3f3& at,
              zwep::Handle hit_node = nullptr);  // 0x004afed0
  bool ready() const;                            // 0x004b4c70: game time has reached ready_at
  void hold_for_flight();                        // 0x004b4c90: ready_at = now + lrm_flight
  void set_ready_at(float t) { ready_at = t; }   // 0x004b4cb0
};

void make_weapon_type(zrdr::Node* node, WeaponDef* def);  // 0x004b0020, the zwep::init callback
void free_weapon_type(WeaponDef* def);                    // 0x004b0090, the zwep::shutdown callback

// The def callbacks (def +0x1ec) this code sets.
uint32_t salvo_spread(WeaponDef* def, const zwep::HitRecord* hit, zwep::Projectile* p);  // 0x004afe70
uint32_t airstrike(WeaponDef* def, const zwep::HitRecord* hit, zwep::Projectile* p);     // 0x004afea0
uint32_t burst(WeaponDef* def, const zwep::HitRecord* hit, zwep::Projectile* p);         // 0x004b44d0
void reset_ready_times();                       // 0x004b4bd0, every mech-lab weapon type
int32_t lab_rounds(int32_t weapon, int32_t n);  // 0x004b4900: the mech lab's rounds for n

// The WeaponType of a weapon by its def name: 0x00581ed0 then +0x1f0.
const WeaponType* weapon_type(const char* name);

// ---------------------------------------------------------------------------
// The weapons a vehicle carries (docs/weapons.md, "The manager").

using Handle = void*;
struct Vec3f { float x, y, z; };
struct Weapon;
struct WeaponManager;

// A pool of rounds, shared by every weapon of one type on a manager; a beam's
// own bin holds its charge. 0x18 bytes.
struct AmmoBin {
  int32_t refs = 1;                    // +0x00
  int32_t amount = 0;                  // +0x04
  int32_t max = 0;                     // +0x08  grows when more is put in
  bool infinite = false;               // +0x0c  amount and max read -1
  bool warned = false;                 // +0x0d  "out of ammo" already played
  WeaponManager* manager = nullptr;    // +0x10
  float damage = 0;                    // +0x14  the first weapon's DAMAGE
  bool attach(Weapon* w);              // 0x004b4cc0
  int32_t left() const { return infinite ? -1 : amount; }    // 0x004b4cf0
  int32_t capacity() const { return infinite ? -1 : max; }   // 0x004b4d00
  void set_amount(int32_t n);          // 0x004b4d10
  void add(int32_t n) { set_amount(amount + n); }            // 0x004b4dc0
};

struct WeaponGroup {                   // 0x54 bytes, ctor 0x004b1d80
  WeaponManager* manager = nullptr;    // +0x00
  int32_t index = 0;                   // +0x04
  uint32_t count = 0;                  // +0x08
  int32_t current = -1;                // +0x0c
  Weapon* current_weapon = nullptr;    // +0x10
  Weapon* weapons[16] = {};            // +0x14
  Weapon* at(uint32_t i) const { return i < count ? weapons[i] : nullptr; }
  int32_t find(Weapon* w) const;       // 0x004b1db0
  bool select(Weapon* w);              // 0x004b1de0
  bool add(Weapon* w);                 // 0x004b1eb0
  bool remove(Weapon* w);              // 0x004b1ef0
  void next();                         // 0x004b1f40
  void prev();                         // 0x004b2000
  void release();                      // 0x004b20b0
  bool trigger();                      // 0x004b2100
  void held();                         // 0x004b21f0
};

// A mounted weapon (vtable 0x005c9088): projectiles. BeamWeapon overrides.
struct Weapon {
  int32_t lock_flag = 0;               // +0x04  1 once locked (its address goes to zWeapon)
  zwep::LockRef lock;                  // +0x08  target (vt+0x24), its position (0x005a8630) and velocity (vt+0x0c)
  Handle projectile_target = nullptr;  // +0x14
  float rating = 0;                    // +0x18  damage a second, salvos not counted (docs/weapons.md)
  int32_t slot = 0;                    // +0x1c  its index on the manager
  Handle node = nullptr;               // +0x20  the firing part's node
  Handle part = nullptr;               // +0x24  the firing part
  int32_t index = -1;                  // +0x28  mech-lab index
  WeaponDef* def = nullptr;            // +0x2c
  float reload = 0;                    // +0x30  seconds until it can fire
  Vec3f target{};                      // +0x34
  Vec3f muzzle{};                      // +0x40
  Vec3f direction{};                   // +0x4c
  AmmoBin* bin = nullptr;              // +0x58
  bool locked = false;                 // +0x5c
  bool fcs = false;                    // +0x5d  FCS in the firing part
  int32_t group = -1;                  // +0x60
  int32_t salvo = 0;                   // +0x64  SALVO count, 0 if none
  int32_t salvo_left = 0;              // +0x68
  int32_t x6c = 0;                     // +0x6c
  float damage = 0;                    // +0x70  0..1, 1 destroyed
  bool enabled = true;                 // +0x74
  Handle loop_sound = nullptr;         // +0x78
  Handle recharge_sound = nullptr;     // +0x7c
  Firepoint* firepoint = nullptr;      // +0x80
  Vec3f spread{};                      // +0x84  offset from a weapon sharing the firepoint
  WeaponItem* item = nullptr;          // +0x90
  WeaponManager* manager = nullptr;    // +0x94

  Weapon(WeaponItem* item, WeaponManager* m, bool beam);   // 0x004b2530
  Weapon(WeaponDef* def, Handle node, WeaponManager* m, bool beam);  // 0x004b28d0 (other vehicles)
  virtual ~Weapon();                                       // +0x00, 0x004b28b0 with 0x004b2bc0
  virtual bool fire(const Vec3f* velocity);                // +0x04, 0x004b3450
  virtual bool is_beam() const { return false; }           // +0x08, 0x004e42d0
  virtual bool is_firing() const { return false; }         // +0x0c, 0x004e42d0
  virtual float dps() const;                               // +0x10, 0x004b4de0
  virtual int32_t ammo() const { return bin ? bin->left() : 0; }  // +0x14, 0x004b2830
  virtual void update(float dt);                           // +0x18, 0x004b2c20
  virtual bool lock_on(Handle target, bool* fcs);          // +0x1c, 0x004b3370
  virtual bool try_lock(Handle record, bool* fcs);         // +0x20, 0x004b3330
  virtual bool aim_and_fire(const Vec3f* at, bool force);  // +0x24, 0x004b3660
  virtual bool trigger();                                  // +0x28, 0x004b3190
  virtual void release();                                  // +0x2c, 0x004b3260
  virtual void held();                                     // +0x30, 0x004b3280
  virtual float ready() const;                             // +0x34, 0x004b2840

  bool aim(Vec3f* at, Vec3f* muzzle, Vec3f* dir, const Vec3f* velocity);  // 0x004b36e0
  bool type_ready() const;                                 // 0x004b4ba0, the AI's LRM wait
  bool can_fire() const;                                   // 0x004b3a20
  float min_range_sq() const;                              // 0x004b3990
  float max_range() const;                                 // 0x004b39a0
  float max_range_sq() const;                              // 0x004b39e0
  WeaponGroup* group_ptr() const;                          // 0x004b3a90
  bool set_group(int32_t g);                               // 0x004b1e10
  bool set_enabled(int32_t side, bool force);              // 0x004b2f90
  void add_damage(float d);                                // 0x004b2d50
  void destroy();                                          // 0x004b3020

 protected:
  void out_of_ammo_note(bool fired);  // the tail of 0x004b3190 and 0x004b3280
};

struct BeamWeapon : Weapon {           // vtable 0x005c90c0, 0xa4 bytes
  bool firing = false;                 // +0x98
  const Vec3f* aim_at = nullptr;       // +0x9c  the caller's aim point
  Handle beam = nullptr;               // +0xa0  the zWeapon beam
  float recharge_carry = 0;            // the rebuild's: the recharge's fraction (FIX (B), beam_recharge)
  float fired_time = 0;                // the rebuild's: time since the trigger (TAG's FIX (bug))

  BeamWeapon(WeaponItem* item, WeaponManager* m);          // 0x004b3c10
  BeamWeapon(WeaponDef* def, Handle node, WeaponManager* m);  // 0x004b3d70
  ~BeamWeapon() override;                                  // 0x004b3d50 with 0x004b3e90
  bool is_beam() const override { return true; }           // 0x004b3d30
  bool is_firing() const override { return firing; }       // 0x004b3d40
  float dps() const override;                              // 0x004b4e50
  int32_t ammo() const override { return -1; }             // 0x004a5260
  void update(float dt) override;                          // 0x004b4230
  bool aim_and_fire(const Vec3f* at, bool force) override; // 0x004b3f40
  bool trigger() override;                                 // 0x004b3fc0
  void release() override;                                 // 0x004b4180
  void held() override;                                    // 0x004b41b0
  float ready() const override;                            // 0x004b4470
  void stop();                                             // 0x004b4140
 private:
  void init();
};

struct WeaponManager {                 // 0x94 bytes, Vehicle +0xd4
  bool trigger_down = false;           // +0x00
  bool trigger_was = false;            // +0x01
  int32_t state = 0;                   // +0x04  1 pressed, 2 held, 3 released
  float alpha_end = 0;                 // +0x08
  Vec3f aim{};                         // +0x0c
  bool aim_valid = false;              // +0x18
  int32_t mode = 0;                    // +0x1c  0 chain, 1 single, 2 group
  int32_t destroyed = 0;               // +0x20
  uint32_t count = 0;                  // +0x24
  int32_t group = 0;                   // +0x28  -1 none
  WeaponGroup* groups[5] = {};         // +0x2c
  Weapon* weapons[16] = {};            // +0x40
  Handle owner = nullptr;              // +0x80  the vehicle's mech, 0 for others
  Handle vehicle = nullptr;            // +0x84
  bool has_target = false;             // +0x88
  bool online = true;                  // +0x89
  float ammo_value = 0;                // +0x8c  sum of ammo x damage over the bins
  float ammo_value_inv = 0;            // +0x90  1 / the largest that sum has been

  explicit WeaponManager(Handle vehicle);                  // 0x004b00d0
  ~WeaponManager();                                        // 0x004b01d0
  WeaponManager(const WeaponManager&) = delete;
  WeaponManager& operator=(const WeaponManager&) = delete;
  void set_unlimited(bool on);                             // 0x004b0220
  void reset();                                            // 0x004b0290
  void forget_target(Handle target);                       // 0x004b0370
  Weapon* find(int32_t index, uint32_t from) const;        // 0x004b03c0
  Weapon* add(WeaponItem* item);                           // 0x004b0400
  Weapon* add(int32_t index, Handle node, int32_t group);  // 0x004b0460 with no vehicle lookup
  Weapon* add(int32_t index, Handle node, const char* name, int32_t group);  // 0x004b0460: the node named under `node`
  Weapon* current() const;                                 // 0x004b04c0
  WeaponGroup* group_ptr() const { return group == -1 ? nullptr : groups[group]; }
  bool next_group();                                       // 0x004b04f0
  bool prev_group();                                       // 0x004b0610
  bool select_group(int32_t g);                            // 0x004b0730
  void next_weapon();                                      // 0x004b0790
  void prev_weapon();                                      // 0x004b07e0
  void mode_sound() const;                                 // 0x004b0830
  void release();                                          // 0x004b0880
  bool net_fire(uint32_t weapon, Handle target, int32_t action);  // 0x004b08c0
  void alpha_strike(float seconds);                        // 0x004b0a70
  void alpha_release();                                    // 0x004b0ab0
  bool jettison();                                         // 0x004b0ae0
  void set_aim(const Vec3f* at);                           // 0x004b0bc0
  void set_online(bool on);                                // 0x004b0bf0
  void set_trigger(bool down) { trigger_was = trigger_down; trigger_down = down; }  // 0x004b0c20
  void update(float dt);                                   // 0x004b0c30
  bool critical(int32_t part, bool ammo);                  // 0x004b0de0
  void part_lost(int32_t part);                            // 0x004b1040
  void destroy_all();                                      // 0x004b1110
  bool arm_restriction();                                  // 0x004b2da0
  void refresh_hud() const;   // the "owner is the player: redraw the groups" tail many methods share
  void ammo_gauge() const;                                 // 0x004b0d80 (its argument is unused)

 private:
  void make_groups();
};

Weapon* make_weapon(WeaponItem* item, WeaponManager* m);   // 0x004b2230
// The weapon half of a mech taking a configuration (0x00414501..0x00414732,
// inside its configuration step): the manager remade, every weapon item
// mounted, the configuration kept (mech +0x3ec), then each ammo item into its
// weapon's bin, equipment counted (+0x3ac; jump jets through add_jump_jet),
// the AMS given 100 rounds per AMS (+0x3f0), TAG mounted on the first
// firepoint in group 5.
struct Ams;
void configure_weapons(Handle mech, WeaponManager* m, Config* cfg, Config** kept, int32_t equipment[15], Ams* ams,
                       Handle node);
// 0x004b4660: a vehicle's weapons from its definition's `weapons` list, a
// new manager for it (the old one deleted); nullptr without the list.
WeaponManager* build_vehicle_weapons(Handle vehicle, WeaponManager* old, zrdr::Node* def);
Weapon* make_vehicle_weapon(int32_t index, Handle node, WeaponManager* m);  // 0x004b23f0
// A beam's recharge this frame, `x` the frame's (heat-scaled) seconds: the exe's
// ftol(rate x), or with zfix per_second the 30 fps frame's amount spread over
// the frame with the fraction carried (FIX (B), docs/fixes.md).
int32_t beam_recharge(float rate, float x, float dt, float* carry);
// 0x004b4a40: spawns the shot; with SHOW_ON_RADAR the projectile goes on the radar.
Handle spawn_projectile(WeaponDef* def, Handle node, Handle x64, const Vec3f* muzzle, const Vec3f* dir,
                        const Vec3f* velocity, int32_t p5, int32_t p6, bool hostile);

// 0x00585010 (the port, or WeaponCalls::spawn when set).
Handle spawn_shot(WeaponDef* def, Handle node, Handle x64, const Vec3f* muzzle, const Vec3f* dir,
                  const Vec3f* velocity, int32_t p5, int32_t p6, bool hostile);

// The radar's list of shots in the air this frame (0x004b49c0..0x004b4b96).
struct RadarBlip {                     // 8 bytes
  zwep::Projectile* shot;              // +0
  bool hostile;                        // +4  to the player (the shooter's enemies hold the player's team)
};
std::list<RadarBlip*>& radar_blips();                      // 0x0060b228
void radar_add(zwep::Projectile* p, bool hostile);         // 0x004b49c0
void radar_not_hostile(zwep::Projectile* p);               // 0x004b4a20
void radar_hostile(zwep::Projectile* p);                   // 0x004b4a30
void radar_clear();                                        // 0x004b4b10 (action 0x004b49b0)
void radar_objects_create();                               // 0x004b4ab0
void radar_objects_destroy();                              // 0x004b4ae0

// The anti-missile system (0x1c bytes, made by the mech at 0x41609c with
// range 150, period 0.6 and 100 rounds; the configuration step gives it 100
// rounds per AMS, 0x4146f9). It picks the nearest shot on the radar list and
// shoots it down by setting its health to 0.
struct Ams {
  bool on = false;                     // +0x00
  float range = 0;                     // +0x04  in |dx| + |dz|, height ignored
  float next = 0;                      // +0x08  clock time it can fire again
  float period = 0;                    // +0x0c
  zwep::Projectile* target = nullptr;  // +0x14
  int32_t rounds = 0;                  // +0x18
  Ams(float range, float period, int32_t rounds);           // 0x004b3ab0
  bool find(int32_t side, const zwep::Vec3f3* at);          // 0x004b3ae0 (a first argument unused)
  const zwep::Vec3f3* target_at() const;                     // 0x004b3ba0
  void fire(int32_t count);                                  // 0x004b3bb0
};

// The world the weapons read (globals of other modules).
struct WeaponWorld {
  Handle player = nullptr;             // [0x005f4c8c] +0x64
  Handle mission_player = nullptr;     // [0x0060861c] +0x14
  bool force_feedback = false;         // [0x00608620] the force feedback object exists
  bool lock_targets = false;           // [0x00605fb4]  missiles are given the locked target
  float clock = 0;                     // [0x0072c530]
  float time = 0;                      // [0x0060991c]  game time
  float dt = 0;                        // [0x0072c52c]
  float cos_35 = 0;                    // [0x0060b264], made on first use
  bool cos_35_made = false;            // [0x0060b21c] bit 0
};
WeaponWorld& weapon_world();

// Calls into code that is not ported yet. Unset ones do nothing and return 0.
struct WeaponCalls {
  Handle (*vehicle_mech)(Handle vehicle) = nullptr;           // vt+0x2c then vt+0x50: the mech, 0 if none
  Handle (*vehicle_entity)(Handle vehicle) = nullptr;         // vt+0x2c
  Handle (*vehicle_node)(Handle vehicle) = nullptr;           // +0x04
  const Vec3f* (*vehicle_velocity)(Handle vehicle) = nullptr; // &+0x30
  Handle (*entity_node)(Handle entity) = nullptr;             // +0x04
  Handle (*entity_x64)(Handle entity) = nullptr;              // &+0x64
  Handle (*entity_target)(Handle entity) = nullptr;           // +0x6c, a record whose +4 is the target
  Handle (*record_target)(Handle record) = nullptr;           // record +4
  Handle (*target_projectile)(Handle target) = nullptr;       // vt+0x2c then vt+0x58
  uint32_t (*entity_enemies)(Handle entity) = nullptr;        // +0xc8, its enemies' teams
  uint32_t (*player_team)() = nullptr;                        // mission player vt+0x24, vt+0x28, +0xc0
  int32_t (*owner_x58)(Handle owner) = nullptr;               // owner vt+0x58: nonzero skips the lock
  std::vector<Firepoint*>* (*firepoints)(Handle owner) = nullptr;  // 0x004846a0 on +0x684 +0xc, then +0x24
  Handle (*part_by_id)(Handle owner, int32_t id) = nullptr;   // 0x0041d510
  Handle (*part_node)(Handle part) = nullptr;                 // +0x08
  Handle (*part_mech)(Handle part) = nullptr;                 // 0x00460000
  Handle (*node_part)(Handle node) = nullptr;                 // 0x0040ef30
  const char* (*node_name)(Handle node) = nullptr;            // the node's name at +0
  bool (*part_in_water)(Handle part) = nullptr;               // +0x49
  int32_t (*node_id)(Handle node) = nullptr;                  // +0x28, -1 without a node
  int32_t (*fcs_count)(Handle owner) = nullptr;               // +0x3b8
  bool (*equipment_in_part)(Handle owner, int32_t kind, int32_t part) = nullptr;  // 0x00460c10
  Config* (*config)(Handle owner) = nullptr;                  // +0x3ec
  float (*heat)(Handle owner) = nullptr;                      // vt+0x9c
  void (*add_heat)(Handle owner, float h) = nullptr;          // 0x00419e40
  void (*recoil)(Handle part, float r) = nullptr;             // part vt+0x24
  void (*net_event)(Handle owner, int32_t kind, Weapon* w) = nullptr;  // vt+0x168 (0x16: a weapon)
  void (*ff_weapon)(int32_t index) = nullptr;                // 0x00433630 on [0x00608620]
  void (*test_fired)(const Weapon& w) = nullptr;           // for the tests: each projectile as it spawns
  Handle (*target_mech)(Handle target) = nullptr;             // vt+0x2c then vt+0x50
  void (*lock_notice)(Handle mech, Handle vehicle, WeaponDef* def) = nullptr;  // 0x0041abd0
  int32_t (*ecm_count)(Handle mech) = nullptr;                // +0x3cc
  const zwep::Vec3f3* (*target_position)(Handle target) = nullptr;  // 0x005a8630
  Handle (*target_key)(Handle target) = nullptr;              // vt+0x24: what a lock remembers
  Handle (*net_target)(Handle id) = nullptr;                  // the mission player, or the list at 0x00605f84 by +0x6fc
  const zwep::Vec3f3* (*target_velocity)(Handle target) = nullptr;  // vt+0x0c
  // The rebuild's (TAG's FIX (bug)): the unit a hit node belongs to as a lock
  // (its live position and velocity); false for none, or a thing that
  // stands still: the strike then homes on the point as the exe's does.
  bool (*strike_target)(Handle hit_node, zwep::LockRef* out) = nullptr;
  int32_t (*node_position)(Handle node, Vec3f* out) = nullptr;  // 0x004ed650: nonzero = no position
  const Vec3f* (*node_axis)(Handle node) = nullptr;           // node +0x38 +0x78: the facing
  float (*normalize)(Vec3f* v, Vec3f* out) = nullptr;         // 0x0051e3d0, returns |v|
  Handle (*spawn)(WeaponDef* def, Handle node, Handle x64, const Vec3f* muzzle, const Vec3f* dir,
                  const Vec3f* velocity, int32_t p5, int32_t p6, bool hostile) = nullptr;  // 0x00585010 (hostile is not passed)
  void (*radar_track)(Handle projectile, bool hostile) = nullptr;  // +0x5d0 = 0x004b4a30 / 0x004b4a20
  Handle (*beam_create)(WeaponDef* def, Handle owner_node, Handle node, const Vec3f* muzzle,
                        const Vec3f* dir) = nullptr;           // 0x00584a70(0, node, muzzle, dir, 1)
  void (*beam_start)(Handle beam) = nullptr;                  // 0x00581230
  void (*beam_stop)(Handle beam) = nullptr;                   // 0x00581400
  Handle (*beam_free)(Handle beam) = nullptr;                 // 0x00584b40
  void (*mech_sound)(int32_t index) = nullptr;                // 0x0041e770 (1.0, -1.0)
  Handle (*play_loop)(Handle sound) = nullptr;                // 0x0056e3d0 (1.0)
  Handle (*play_at)(Handle sound, const Vec3f* pos) = nullptr;  // 0x0056e7e0 (1.0, 0)
  void (*stop_sound)(Handle s) = nullptr;                     // 0x0056e9f0
  void (*destroyed_sounds)(Handle name, Handle destroyed) = nullptr;  // 0x004e6dc0 (1.0, 0, 0, -1.0, 0)
  void (*voice_destroyed)(int32_t index) = nullptr;           // 0x004853d0
  bool (*screen)(Handle owner) = nullptr;                     // 0x00430aa0 on +0x684 is not 0
  void (*hud_groups)(Handle owner) = nullptr;                 // 0x00446240 on the screen +0x6520
  void (*hud_weapons)(Handle owner) = nullptr;                // 0x00446520
  void (*hud_fire)(Handle owner) = nullptr;                   // 0x004464c0
  Weapon* (*hud_highlight)(Handle owner) = nullptr;           // screen +0x1795c
  // The arm restriction (0x004b2da0).
  Handle (*view)() = nullptr;                                 // [0x005f4c8c]+0x58 vt+0x1c
  int32_t (*view_kind)(Handle view) = nullptr;                // view +0x40
  int32_t (*control_mode)(Handle owner) = nullptr;            // +0x37c +0xc
  const zmth::Quat* (*torso_aim)(Handle owner) = nullptr;    // 0x00465940 on the torso (+0x39c)
  const zmth::Quat* (*torso_look)(Handle owner) = nullptr;   // the torso's +0xa4 when +0xa2, else 0
  // The HUD ammo gauge (0x004b0d80): the screen, its gauge (+0x144c) vt+0x6c
  // and vt+0x68, 0x00440a30 on +0x410c with those two, 0x00443d20 on the gauge.
  Handle (*hud_screen)(Handle owner) = nullptr;               // 0x00430aa0 on +0x684
  int32_t (*gauge_6c)(Handle screen) = nullptr;
  int32_t (*gauge_68)(Handle screen) = nullptr;
  int32_t (*hud_440a30)(Handle screen, int32_t a, int32_t b) = nullptr;
  void (*hud_443d20)(Handle screen, int32_t v) = nullptr;
  void (*add_jump_jet)(Handle mech) = nullptr;                       // 0x00417300(mech, 1)
  // The vehicle builder (0x004b4660).
  Handle (*node_find)(Handle node, const char* name) = nullptr;      // 0x004eb0d0
  Handle (*node_by_name)(Handle node, const char* name) = nullptr;   // 0x004abb60
  // The radar list's object (0x004b4ab0).
  Handle (*object3d_create)() = nullptr;                      // 0x004fb740
  void (*set_action_priority)(Handle node, int32_t p) = nullptr;  // 0x004eb5b0
  void (*set_action)(Handle node, void (*fn)()) = nullptr;    // 0x004eb450
  void (*node_destroy)(Handle node) = nullptr;                // 0x004eaea0
};
WeaponCalls& weapon_calls();

}  // namespace mech3
