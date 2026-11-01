#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mech3/app_mech.h"

// The sensors' hooks (mech3/sensors, docs/ai.md section 15) wired to the
// program's objects: the mechs on zEntity's list (the port's) and
// targets.zrd's objects. Not original code; app_sensors.cpp.
namespace mech3 {

// The engine as the mech build has it, and Mech3Msg.dll's text (0x00576040).
void wire_sensors(const MechWiring* w, const char* (*message)(void* e, int32_t id));

// What a vehicle senses, for the program's tests: its contacts nearest first.
struct SensedContact {
  uint32_t seen = 0;       // the handle of what it saw
  float distance = 0;      // metres
  bool in_sight = false;
};
int32_t sensed_contacts(const void* mech, SensedContact* out, int32_t cap);
// The contact the mech has selected (its +0x6c), -1 none or not among its contacts.
int32_t selected_contact(const void* mech);

// The HUD's contacts and their objects of any class (entity_classes.h): a
// contact's vehicle (0x004e6fe0), entity (+8) and size (0x004e7070); an
// object's team (vt+0x28 +0xc0), blip (vt+0x3c), hidden (vt+0xa8), mech
// (vt+0x50), name (vt+0xa4), place (0x005a85e0), centre (vt+0xbc) and its
// centroid node's place (+0x14).
void* hud_contact_vehicle(const void* contact);
void* hud_contact_entity(const void* contact);
float hud_contact_size(const void* contact);
uint32_t hud_object_team(const void* o);
bool hud_object_classed(const void* o);  // an entity class the sensors know (its vt+0x28 +0xc0 a team)
void* hud_object_blip(const void* o, int32_t kind);
bool hud_object_is_field_base(const void* o);  // for the HUD's report
bool hud_object_is_target(const void* o);      // a targets.zrd object (or trap)
bool hud_object_hidden(const void* o);
void* hud_object_mech(const void* o);
const char* hud_object_name(const void* o);
void hud_object_place(const void* o, float out[3]);
void hud_object_centre(const void* o, float out[3]);
void hud_object_centroid(const void* o, float out[3]);
void* hud_object_node(const void* o);  // +0x04, by class (a turret's or target's a handle)
void hud_object_aim_place(const void* o, float out[3]);  // 0x005a8630 (the lead point's)
void hud_object_velocity(const void* o, float out[3]);   // vt+0xc
const float* weapon_lock_point(const void* o);           // 0x005a8630, a live pointer
const float* weapon_lock_velocity(void* o);              // vt+0xc, a pointer
void* hud_target_marker(int32_t kind);                   // [0x0060b4f0 + 4 kind]
void hud_object_view_tint(const void* o, bool on);      // vt+0x68: TARGET's view tint, by class

// Ours, for --campaign-chain's kills (campaign.md section 14): what the
// mission player could shoot, alive (zEntity's +0x08) on a team that is not
// the player's or its allies': zEntity's mechs and vehicles and the turrets
// (not team 0), and targets.zrd's objects (team 0 too).
struct ChainTarget {
  uint32_t h = 0;
  const char* kind = "";
  std::string name;  // its node's, before it dies (a dead mech's node is "deadmech")
};
std::vector<ChainTarget> chain_hostiles();
void* chain_object(uint32_t h);  // the object a hostile's handle names (tests)
bool chain_alive(uint32_t h);
// One shot of the player's landing on h's aim node: zWeapon's impact
// (0x005826b0) with a stand-in projectile the player owns, so the node's
// damage handler, the victim's damage and death and the kill credit (with
// its salvage) run as for a real shot. `def` the weapon's; true when killed.
bool chain_hit(uint32_t h, void* def);
// The same on a scene node (the first under it a shot can damage); false
// when none can be.
bool chain_hit_node(void* node, void* def);
// The nodes of the animations a destroyed target's destroy animation still
// waits for (its prerequisites, not yet run): what has to fall first, as
// c1m3's legs under the buildings.
std::vector<void*> chain_prerequisite_nodes();
// A projectile weapon a mech carries in the game (not a beam, no impact
// callback, not NARC): the chain's gun.
void* chain_weapon();
// The weapon types a mech or vehicle carries, as its salvage (0x00472f00)
// reads them: each weapon manager slot that holds one.
std::vector<int32_t> chain_weapon_types(uint32_t h);
// The mission player set down `dist` m off the entity whose node is `name`
// (Mech vt+0xdc, 0x00418f30), as if walked there; false when none.
bool chain_player_to(const char* name, float dist);
// The same by a scene node, unless the player is within 1.5 x dist of it.
bool chain_player_near(void* node, float dist, bool at_its_level = false);
// at_its_level: the ground at most 10 m above the node (a cave's floor, not the hill over it).
bool chain_player_at(const float at[3]);  // the player set on the ground at a point (a reach_area's), at most 10 m above it
// Where a net link that sets `valve` to `value` starts (a valve_assign edge, c3m1's m1mfbreach): the place to call
// the bases from.
bool chain_handle_at(uint32_t h, float out[3]);  // an entity's position by its handle
bool chain_player_face(const float at[3]);       // the player turned where it stands to face a point
uint32_t chain_entity_named(const char* name);   // 0x004d7330's lookup (app_mission.cpp): the AI mechs, turrets, vehicles, targets
bool chain_valve_link_at(const char* valve, int32_t value, float out[3]);
// Where the entity whose node is `name` is; false when none.
bool chain_entity_at(const char* name, float out[3]);
// The same at the middle of a loaded region (its corners' mean, on the
// highest ground there that is not water); false when none is loaded.
bool chain_player_to_region(const char* name);

}  // namespace mech3
