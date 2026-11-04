#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mech3/app_mech.h"

// The vehicles in the program (app_vehicle.cpp; docs/vehicles.md, In the
// program). Pointers stay void* here: mech.h's Handle is not app_mission's.
namespace mech3 {

struct Vehicle;

void wire_vehicles(const MechWiring* w);  // from wire_mech_build; null: the defaults again
bool is_program_vehicle(const void* p);   // made by the mission start, still alive
uint8_t program_vehicle_active(const void* p);  // its +0x08 (zEntity's active flag)
void* program_vehicle_create(void* ctl, const char* type, const char* name);  // 0x004ac320
// The stealth object (veh_stealth.h): its controls (0x004ac860(0xa1c) and
// 0x004a51b0), the object over them (0x004a4de0), the one made ([0x00609a2c]).
void* program_stealth_controls();
void program_vehicles_new_process();         // tests: as a new process, no stealth or vehicles
void* program_stealth_create(void* ctl);
void* program_stealth();
void program_stealth_frame(float dt);  // its vt+0x34 (0x004a1040) from the player's tick
// The view letting go of a vehicle (its vt+0x104): the stealth's turns its
// overlay off (0x004a50d0); a mech's and others', nothing.
void program_vehicle_view_detached(Vehicle& v);
bool program_vehicle_sensed(const void* v);  // zEntity +0x09: the entity walk runs it
void* program_field_base_create(void* ctl, const char* type, const char* name);  // 0x004589d0
bool program_is_field_base(const void* v);  // vt+0xf0 / vt+0xf4: itself (0x004d2a70), else 0
void* program_campaign();                   // app_menus.cpp: the pilot's campaign (0x00479440), null without one
int32_t program_campaign_bases();           // app_menus.cpp: the campaign's +0xa8 (3 for the debug launch's)
void program_field_bases_mission_start();
void program_field_bases_frame(float dt);   // 0x0045a270 from the mission's frame
// The waynets (mech_waynet): the mission's waynet (the missions' +0xb0,
// app_mission's), the nets' begin for the mcbwaynet, the MFB command (0x73),
// N / Shift+N / Ctrl+N on the mcbwaynet (false: it is not the mission's),
// its clear and free at the mission's end.
void program_waynet_mission(void* (*get)(), void (*set)(void*));
void program_nets_begin();
void program_mcb_command(void* mech);
bool program_mission_waynet_key(int32_t cmd, void* mech);
void program_mission_waynet_clear();
void program_mcb_waynet_free();
void* program_mcb_waynet();
void* program_mission_waynet();
// Mech3.exe 1.2 @ 0x00490950: the mobile bases' stations in a network game
// (ALLOW MOBILE BASES) on the mission's waynet; how many were made.
int32_t program_mp_stations(void* waynet);
// [0x00479480]+0xa8: the network game's ALLOW MOBILE BASES (app_mission's).
void program_set_mp_mobile_bases(bool (*fn)());
// For the tests: the mission waynet's element i, its label and place; false
// for one without a place.
bool program_mission_point(int32_t i, const char** label, float pos[3]);
// The mcbwaynet's points (count, without the default behaviour at 0) and the
// dock's point after a request: its label and place; false without one.
int32_t program_mcb_points();
bool program_mcb_point(int32_t i, float pos[3]);  // point i (1..count) of the mcbwaynet, for the tests
void* program_field_base(int32_t i);              // the registry's base i (0..2), for the tests
bool program_dock_point(const char** label, float pos[3]);  // [0x00609980]: the mcbwaynet while the MFB command is picking
int32_t program_field_bases_count();          // the registry's count (0x00605ae0 +0x10)
int32_t program_field_bases_lines();          // the dock's radio lines found, and its effects (+1)
// The dock as the mission start leaves it: its states' list (1: the idle one
// only), to begin, the bases held, "mcbdeploy" made.
struct DockSummary {
  int32_t states = -1;
  bool idle_first = false, begin_next = false, drive = true, deploy_valve = false;
  // The front state: 0 idle, 1 places, 2 contact, 3 deploy, 4 repair, 5 close, 6 reset, 7 parked
  // (ours, after a repair with bugs on); -1 none.
  int32_t front = -1;
  bool busy = false;
  void* requester = nullptr;
};
DockSummary program_dock_summary();
// The nav waynet (vehicles.md, The nav waynet): made at the mission load
// (0x004797c0), deleted with the missions' +0xb4, its frame (vt+0x6c), the
// icons freed at the mission's end (0x00479850); for the HUD the selected
// point (the radar's marker: its icon, place and name) and the edge arrow
// (0x00479ef0, 0x00479ed0).
void* program_nav_waynet_make(const char* name);
bool program_nav_waynet_delete(void* o);
bool program_is_nav_waynet(void* o);
uint8_t program_nav_waynet_frame(void* w, void* vehicle);
void program_waynet_icons_free();
// For the tests: the waynets in znet's registry order, their labels and point counts.
int32_t program_waynets(const char** names, int32_t* points, int32_t cap);
// For the tests: the waynet marker's place (false: none found), the i-th waynet's selected point's.
bool program_waynet_marker(float out[3]);
bool program_waynet_selected(int32_t i, float out[3]);
void* program_waypoint_marker();
void* program_waypoint_icon(void* p);
bool program_is_waypoint_icon(const void* b);  // one of the radar's two waypoint icons
void program_waypoint_position(void* p, float out[3]);
const char* program_waypoint_name(void* p);
// A waynet's points (tests): the count, the selected one, the watched
// list's size, and per point (up to 8) its label, name, reached, nav line, place.
struct NavSummary {
  int32_t count = 0, current = -1, watched = 0;
  std::string labels[8], names[8];
  bool reached[8] = {}, has_line[8] = {};
  float pos[8][3] = {};
};
NavSummary program_nav_summary(void* waynet);
void* program_nav_find(const char* name);       // the objectives' "nav" (0x0045c8f0)
const char* program_nav_name(void* p);  // a nav point's +0x28 ("" for none)
void program_nav_watch(void* point, int32_t on);  // and its watch (0x0047a270)
void program_waypoint_arrow(int32_t active);
void program_waypoint_arrow_draw();
// The AI's refit (zai_repair, the brain hooks): the bases up (0x0045a000),
// the dock asked for a mech (0x0047a780: the mcbwaynet there, 0x0047a730),
// the mcbwaynet there, its selected point's place, the dock's two flags.
int32_t program_bases_alive();
bool program_mfb_ask(void* mech);
bool program_has_mcb();
bool program_mcb_point(float out[3]);
bool program_dock_drive();
bool program_dock_contact();
// What a field base's repair (0x004154e0) restores, on a mech (tests): the
// armour (the mean over its eleven sections), the least internals (legs,
// arms, head, the torso's three), the ammo weapons' rounds left and held
// (not the beams' charge), coolant, jump fuel.
struct MechWear {
  float armor = 0, internal = 0;
  int32_t ammo = 0, ammo_max = 0;
  float coolant = 0, jet_fuel = 0;
};
MechWear program_mech_wear(void* mech);
// The mech worn down (tests): every section's armour and internals set
// (above 0: nothing destroyed), the ammo weapons' bins emptied, coolant
// and fuel 0.
void program_mech_wear_down(void* mech, float armor, float internal);
void program_field_base_aim_arms(void* v, bool open);  // 0x004589a0 on its arms (the dock's, 4b; tests)   // 0x00459e80's registry part (0x00459e50, [0x0060b4d0])
void* program_vehicle_controls(void* v);  // +0xe4
void* program_vehicle_marker(int32_t kind);  // [0x0060b120 + 4 kind] (0x0045b970), ally, enemy, other
void* program_vehicle_node(void* v);      // +0x04
void* program_vehicle_centroid(void* v);  // +0x14
void* program_vehicle_target_node(void* v);  // +0x10
void program_vehicle_view_tint(void* v, bool on);  // vt+0x68 0x004ad390 (TARGET's view)
void program_vehicle_place(void* v, const float pos[3], float yaw);          // vt+0xdc
void program_vehicle_setup(void* v, void* def, const char* name, int32_t kind);  // vt+0x158
void program_vehicle_motion(void* v, void* mode, const char* name, const char* type);  // 0x0049ec90 -> 0x0049ef40
void program_vehicle_points(void* v);     // 0x0049f660
void program_vehicle_touch(void* v);      // 0x005833b0(v, node, 1, 0x004af550)
void program_vehicle_refresh(void* v);    // 0x005a85c0
void program_vehicle_path_drop(void* v);  // 0x00402510: the basic path's temporary entries
// The team masks at +0xc0 / +0xc4 / +0xc8 (set_team_masks' writes and reads).
bool program_vehicle_mask(void* v, uint32_t off, uint32_t* get, const uint32_t* set);
void program_vehicles_forget();           // the mission's end
// Tests: a weapons.zrd shot of `weapon` from `killer` (an entity; its node
// zWeapon's attacker) for more than its health, through its damage (vt+0x160:
// 0x004ad3f0, a field base's 0x00458f50).
bool program_vehicle_kill(void* v, void* killer, const char* weapon);
float program_vehicle_damage(void* v, float amount);  // tests: `amount` off its health (vt+0x160), the health left 0..1
// Tests: `amount` by `weapon` (vt+0x160 with its definition), the health left 0..1;
// the vehicle's glow light (its controls' +0x398 +0x10), none without.
float program_vehicle_hit_by(void* v, const char* weapon, float amount);
void* program_vehicle_glow_light(const void* v);
bool program_vehicle_free(void* v);       // vt+0x00 0x004ac630(1); false when not a program vehicle
// What the AI's brain asks of a vehicle (docs/vehicles.md, In the program),
// by the basic vehicle's slots; the rest (position, forward, matrix, weapons,
// active, velocity) is zEntity's, the mech helpers' (app_mech.h).
float program_vehicle_health(void* v);                      // vt+0x98 0x004ad820
float program_vehicle_speed_at(void* v, float share);       // vt+0x118
float program_vehicle_stopping_distance(void* v, float s);  // vt+0x120
float program_vehicle_stop_share(void* v, float d);         // vt+0x12c
float program_vehicle_move_speed(void* v);                  // vt+0x114
bool program_vehicle_can_face(void* v, const float d[3]);   // vt+0x8c
bool program_vehicle_in_reach(void* v, float cosine);       // vt+0x150
bool program_vehicle_aim_clamp(void* v, const float d[3], float out[3]);  // vt+0x90
void program_vehicle_set_aim(void* v, const float d[3]);    // vt+0x154
void program_vehicle_velocity(void* v, float out[3]);       // vt+0x0c 0x005a82d0: +0x30
const float* program_vehicle_velocity_at(void* v);         // the same, as the pointer it is
const char* program_vehicle_name(void* v);                  // its node's
void program_vehicle_forget(void* v, uint32_t self, uint32_t x);  // vt+0xd4 0x004aeec0
void* program_vehicle_target(void* v);                      // the AiTarget base (ai::AiTarget*)
void program_vehicle_hit_an_ally(void* v, void* victim);    // vt+0x140's controls, vt+0x98
void program_vehicle_frame(void* v, float dt);              // vt+0x34 0x004a1040, when +0x09 is set
// The sensors' (app_sensors.cpp): vt+0xcc 0x004aee00 (active, and the
// brain's vt+0x0c), vt+0xd0 0x004aee40 (the brain's vt+0x10, true without
// one), vt+0xc8 0x004add90 (the brain's empty vt+0x08, the engine sound
// released) and the ping's 0x004adce0 (its engine sound at `pos`).
bool program_vehicle_awake(void* v);
bool program_vehicle_sees(void* v, uint32_t self);
void program_vehicle_sense_refresh(void* v);
void program_vehicle_engine_play(void* v, const float pos[3]);
void program_vehicle_set_sensed(void* v, bool on);  // ZEntity's +0x09: its frame runs
// valves.zrd's (app_mission.cpp's WiredValveHooks).
void* program_vehicle_named(const char* name);  // zEntity's by name (0x005a8210): its node's
void program_vehicle_destroy(void* v);          // vt+0x70 0x004ad2d0
void* program_vehicle_brain(void* controls);    // a program vehicle's AI controls' brain (ai::Brain*), else null
std::vector<void*> program_vehicles();    // this mission's, as made
// What the headless tests check of one.
struct VehicleSummary {
  const char* name = nullptr;  // its node's
  bool in_world = false, ai_controls = false, owned = false, active = false;
  float health = 0, health_max = 0;
  int32_t mode = -1, supports = 0, points = 0;
  uint32_t team = 0;
  // A field base's: its arms' parts found, their time (0 shut, 1 open), its
  // place in the registry (-1 none).
  bool field_base = false;
  int32_t arm_parts = 0, registry = -1;
  float arms = 0;
  float pos[3] = {};
};
bool describe_vehicle(const void* v, VehicleSummary* out);

}  // namespace mech3
