#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "mech3/mech.h"
#include "mech3/mech_control.h"

// The game's network side in a mission (original file unknown: the code
// sits between mech3\mech_reader.cpp and mech3\mech_salvage.cpp,
// 0x0046ceb0..0x00470c00; docs/multiplayer-port.md, pieces 5-8, and
// docs/network.md). The start registers the in-game messages with
// zNetwork, the router hands each to the remote mech it is from (a first
// mech state 0xf makes that mech), a player leaving takes theirs away, the
// end undoes it all.
namespace zeff { struct AnimLogRecord; }
namespace zwep { struct CraterOrder; }

namespace mech3 {

struct Config;
struct MechDfn;

// The network mech (0xc1c bytes, vtable 0x005c3508): a remote player's
// mech, driven by their messages; made by 0x0046d450 from the player's
// record. Its vtable's own slots (the update, the dead reckoning) are
// pieces 6 and 7.
// A mech's state as it arrives (0x3c bytes; network-messages.md, the 0xf
// mech state), kept by its network mech.
struct NetStateIn {
  uint16_t type = 0, size = 0;  // +0x00
  float pos[3] = {};            // +0x04  advanced by the dead reckoning
  int16_t vel[3] = {};          // +0x10  x 127
  uint16_t x16 = 0;             // +0x16
  float heading = 0;            // +0x18  half the yaw
  float torso[2] = {};          // +0x1c  the torso's two angles
  int8_t throttle = 0, turn = 0;  // +0x24  x 127
  uint8_t x26[2] = {};          // +0x26
  float aim[2] = {};            // +0x28  the aim's turn
  float range = 0;              // +0x30  and distance
  uint32_t target = 0;          // +0x34  the target's player
  uint32_t flags = 0;           // +0x38
};
static_assert(sizeof(NetStateIn) == 0x3c, "the state's fixed part");

struct NetMech : Mech {
  uint32_t ping_heard = 0;     // +0x6d8  when their last ping came (timeGetTime, ms; 0x00470a00)
  uint32_t ping_in[5] = {};    // +0x6dc  that ping (type and size, id, the id echoed, a time, a delay)
  uint32_t ping_sent = 0;      // +0x6f0  when ours went to them
  uint32_t ping_id = 0;        // +0x6f4  its id
  float latency = 0;           // +0x6f8  seconds, from their answer (0x00470a00); 0 once made
  uint32_t owner = 0;  // +0x6fc  the player it stands for
  NetStateIn state;    // +0x700  the last state applied (0x0046e4d0)
  uint32_t heard = 0;  // +0x73c  when its last state came (timeGetTime, ms)
  float aim_q[4] = {1, 0, 0, 0};      // +0x740  the last state's aim turn as a quaternion (0x005eda50 at first)
  float heading_q[4] = {1, 0, 0, 0};  // +0x750  its heading: (cos h, 0, sin h, 0)
  float velocity_in[3] = {};          // +0x760  its velocity, damped by the dead reckoning
  Handle label = nullptr;  // +0x76c  an object 0x005a00c0 makes (GDI text, not ported)
};

// The network mech's controls (0x74 bytes, vtables 0x005c34e0 and
// 0x005c3428; 0x0046d450 makes them over the base 0x004265a0): the base's
// smoothing (IControls +0x0c 0x0046ef60), owned by the mech (vt+0x18
// 0x004d7210). Their pilot tick (vt+0xa4) is _purecall in the exe.
struct NetControls : MechControls {
  bool release() override { return true; }
};

struct NetGame {
  bool running = false;                // [0x00605fb4]  a network game is up
  std::list<NetMech*> mechs;           // [0x00605f84], count [0x00605f88]: the remote mechs
  std::vector<Config*> configs;        // [0x00605f74..0x00605f7c]: their loadouts (0x0046d450)
  std::list<uint32_t> missing;         // [0x00605f94], count [0x00605f98]: players whose loadout failed (0x0046d450); never read
  // The dead reckoning's (0x0046ed70) gains, set by the start: the exe's
  // data holds -6.5 and -3.5 before.
  float reckon_gain = -6.5f;           // [0x005e55f0]
  float reckon_damp = -3.5f;           // [0x005e55ec]
  // The local mech's sends (0x0046ef70); the first three cleared at the launch.
  uint8_t last_second = 0;             // [0x00605f9c]  the clock is under a second
  float next_state = 0;                // [0x00605fa0]  when the next state goes (game time)
  uint8_t warned = 0;                  // [0x00605fa4]  the one-minute warning shown
  float next_ping = 0;                 // [0x00605fcc]
  int32_t resends = 0;                 // [0x00605fd0]  states since every part's damage went
  uint32_t rate_setting = 0;           // [0x00608e28]  the lobby's send rate (0x00470b40)
  uint32_t ping_counter = 1;           // [0x005e5690]
  uint32_t ping_turn = 0;              // [0x00605fc8]  the player pinged last (0x00470ab0)
  uint32_t acked_time = 0;             // [0x00605fc4]  the last state acknowledged's send time (0x004709d0)
  uint8_t echo = 0;                    // [0x00605fb8]  an effect heard is being replayed: not sent again
};
NetGame& net_game();

// The network game's fields the sends use (MultiplayerGame, the app's).
struct NetGameFields {
  int32_t score_limit = 0;       // +0x70  the kills that end the game
  int32_t players = 0;           // +0x50  the lobby's count
  float* clock_next = nullptr;   // +0x7c  when the host sends the clock (0x11) again
  float* end_time = nullptr;     // +0x80
  int32_t* end_state = nullptr;  // +0x84  0, 1 ending (the clock under 3 s), 3 ended
  int32_t* kills = nullptr;      // +0x98  the pilot's totals over network games
  int32_t* deaths = nullptr;     // +0x9c
  uint8_t* flags = nullptr;      // +0x8c respawn on, +0x8d unlimited ammo, +0x8e (cleared when respawn is off)
};

// The in-game messages' handlers and what the router calls besides, until
// pieces 5b-7 and the lobby are ported. Unset ones do nothing. The mech
// ones (ECX the mech) get it; 0x00470740, 0x0046eba0 and 0x0046fde0 are
// given only the message.
using NetRoute = void (*)(NetMech* m, uint32_t from, const uint8_t* msg, size_t size);
struct NetCalls {
  NetRoute state = nullptr;       // 0xf  0x0046e4d0: a mech's state applied
  NetRoute ping = nullptr;        // 0x10 0x00470a00
  NetRoute clock = nullptr;       // 0x11 0x00470740 (the message only)
  NetRoute respawn = nullptr;     // 0x12 0x0046ed30
  NetRoute kill = nullptr;        // 0x13 0x0046eba0 (the message only)
  NetRoute scores = nullptr;      // 0x14 0x004708e0
  NetRoute rules = nullptr;       // 0x16 0x0058b380 (zNetwork's)
  NetRoute posture = nullptr;     // 0x17 0x0046ed10
  NetRoute knockback = nullptr;   // 0x18 0x004705b0 (the mech, the message)
  NetRoute effect = nullptr;      // 0x1a 0x0046ff50
  NetRoute drop = nullptr;        // 0x1b 0x0046ffd0 (the mech, the message)
  NetRoute chat = nullptr;        // 0x1f 0x00470490 (the sender, the message)
  NetRoute part_off = nullptr;    // 0x21 0x0046fff0
  NetRoute crater = nullptr;      // 0x19 0x0046fde0 (the message only, any sender)
  NetRoute send_done = nullptr;   // 0xe  0x004709d0 (the message only): the pilot's mech's state acknowledged
  NetRoute create = nullptr;      // test hook; unset: net_mech_create (0x0046d450)
  void (*set_position)(NetMech& m, const float pos[3]) = nullptr;  // vt+0x18, zEntity3d's 0x005a8550
  void (*reckon)(NetMech& m, float dt) = nullptr;  // vt+0x158, 0x0046ed70 with [0x0072c52c]
  uint32_t (*now_ms)() = nullptr;                  // 0x00575d30, timeGetTime; unset: ztimer's clock
  void (*hud_message)(float seconds, int32_t id, const char* name) = nullptr;  // 0x0040fe70
  void (*hud_message2)(float seconds, int32_t id, const char* a, const char* b) = nullptr;  // the same with two
  void (*hud_line)(float seconds, const char* text) = nullptr;  // 0x0040fe20: a line as it is (never a format)
  uint32_t (*lobby_team_id)(uint32_t player) = nullptr;  // 0x00495b60 on the network game's +0x48: its team, 0 none
  void (*forget)(NetMech& m) = nullptr;            // 0x004aa6b0, the sensors' (ai::entities_forget)
  void (*free)(NetMech* m) = nullptr;              // vt+0 (1), 0x0046e3a0; unset: net_mech_delete
  void (*lobby_left)(uint32_t player) = nullptr;   // 0x00495b10 on the game's +0x48: the slot's +0x18 = 1
  // The start's and end's.
  int (*crater_veto)(const zwep::CraterOrder* order) = nullptr;  // 0x0046fd30, zDecal's [0x0060bf88]
  void (*anim_sent)(zeff::AnimLogRecord* record) = nullptr;       // 0x0046feb0, the anim log's [0x0060c714]
  void (*game_clear)() = nullptr;                  // the network game's +0x7c, +0x80, +0x84 = 0
  void (*player_mech_end)() = nullptr;             // 0x0041e580 on the pilot's mech, when there is one
  void (*ff_stop)() = nullptr;                     // 0x00433530 on [0x00608620], when there is one
  void (*config_free)(Config* c) = nullptr;        // 0x00424350 and the delete
  // The launch's (0x0046da80): the lobby's and the network game's.
  uint32_t (*team_of)(uint32_t player) = nullptr;  // 0x00435910 on the network game
  int32_t (*start_of)(uint32_t player) = nullptr;  // the player's lobby slot +0x20 (0x00495b10)
  void (*scores_reset)() = nullptr;                // 0x004361e0 on the network game
  void (*view_outside)() = nullptr;                // 0x0040ae70(1) on [0x005f4c8c]
  // aiv.zrd's record of that name (0x004e0490, 0x004e0680): its place and heading.
  bool (*spawn_point)(const char* name, float pos[3], float* yaw) = nullptr;
  void (*ff_spawn)() = nullptr;                    // 0x004338a0(1) on [0x00608620], when there is one
  // The creation's (0x0046d450): the label 0x005a00c0 and its free 0x005a05a0,
  // the world's add ([0x00608c84] vt+0x00), the host's replay to the joiner
  // (0x004700a0, and 0x00500c60 with 0x004700e0: the anims and craters).
  Handle (*label_new)(NetMech& m) = nullptr;
  void (*label_free)(Handle label) = nullptr;
  void (*world_add)(NetMech& m) = nullptr;
  void (*host_replay)() = nullptr;
  void (*sensor_range)(Mech& m) = nullptr;         // 0x004d6d20 (the AI target's) with its own range
  // The pilot's respawn's (0x0046dde0, piece 8d):
  void (*pilot_set_mech)(Mech& m, int32_t roster) = nullptr;  // 0x004783c0 on the pilot
  void (*view_camera)(int32_t id) = nullptr;       // 0x0040ae70 on [0x005f4c8c]
  void (*options_apply)() = nullptr;               // 0x0040cb30
  void (*objectives_again)() = nullptr;            // 0x00491050 on the missions (0x006098d0)
  void (*ff_respawn)() = nullptr;                  // 0x004338d0(0), 0x004338a0(1) on [0x00608620], when there is one
  // The sends' (0x0046f160, 0x0046ef70): the game, the mission's clock, the
  // kinds of pieces 7 and 8.
  NetGameFields* (*game)() = nullptr;              // 0x00479480 on [0x0060861c]: none outside a network game
  float (*game_time)() = nullptr;                  // [0x0060991c]
  float (*clock_left)() = nullptr;                 // [0x00609920]
  void (*set_clock_left)(float v) = nullptr;       // [0x00609920] = (0x00470740)
  bool (*clock_on)() = nullptr;                    // [0x00609924]
  int32_t (*send_other)(Mech& m, int32_t kind, intptr_t arg) = nullptr;  // test hook: the kinds a test stands in for
  void (*game_over)() = nullptr;                   // test hook; unset: net_game_over (0x004707c0)
  // The game over's: the lobby's deaths (0x00435b40), the pilot written
  // (0x00478ce0, its header, vt+0x04, closed), the fade to the Multi-Exit
  // State.
  int32_t (*lobby_deaths)(uint32_t player) = nullptr;
  void (*pilot_save)() = nullptr;
  void (*exit_fade)() = nullptr;
  // The crater's: a node's mech (0x0040ef10), the crater made (0x004ffaa0;
  // the test's), the station's repair (0x0045a140; the test's).
  Mech* (*node_mech)(Handle node) = nullptr;
  int32_t (*crater_make)(const zwep::CraterOrder* order) = nullptr;
  void (*station)(NetMech& m, uint32_t id) = nullptr;
  // ... and the materials by index (0x00536070, 0x00535ff0) and the anim
  // log's lookup and replay (0x0050b680, 0x0050ca90), the test's.
  Handle (*take_material)(int32_t index) = nullptr;
  uint32_t (*material_index)(Handle material) = nullptr;
  bool (*log_has)(const zeff::AnimLogRecord* rec) = nullptr;
  void (*log_replay)(const zeff::AnimLogRecord* rec) = nullptr;
  // The apply's: the target picked (0x004a7b40, null none), the passive
  // radar's light (the mech's vt+0xb8, 0x0041ded0), the respawn (0x0046ed30
  // with the state's place; piece 7), the pilot's mech (+0x14).
  void (*pick_target)(NetMech& m, Handle vehicle) = nullptr;
  void (*radar_toggled)(NetMech& m) = nullptr;
  void (*respawn_at)(NetMech& m, const float pos[3]) = nullptr;
  Mech* (*pilot_mech)() = nullptr;
  // The events' (piece 7): a mech's name (vt+0xa4: 0x0041df30, a network
  // mech's player's 0x004709c0); the lobby's scores (piece 11): a suicide
  // 0x00435ac0, a kill 0x004359b0, a player's kills 0x00435a30 and score
// 0x00435a50 (kills less suicides; its team's in team play), the list
  // (id, kills, deaths) for 0x14, the kills and deaths set (0x004359f0,
  // 0x00435b00); the HUD's scores (0x00446ad0 on the view's mech's HUD,
  // when it shows them).
  const char* (*mech_name)(Mech& m) = nullptr;
  void (*lobby_suicide)(uint32_t player) = nullptr;
  void (*lobby_kill)(uint32_t player) = nullptr;
  int32_t (*lobby_kills)(uint32_t player) = nullptr;  // 0x00435a30 (the game's end)
  int32_t (*lobby_score)(uint32_t player) = nullptr;  // 0x00435a50 (the kill's check; unset: lobby_kills)
  int32_t (*lobby_scores)(uint32_t* out, int32_t cap) = nullptr;  // 3 words a player; their count
  void (*lobby_set_kills)(uint32_t player, uint32_t kills) = nullptr;
  void (*lobby_set_deaths)(uint32_t player, uint32_t deaths) = nullptr;
  void (*hud_scores)() = nullptr;
  // ... and the respawn's rebuild (0x0046e0f0) and place (0x00418e90) as
  // the test sees them.
  void (*rebuild)(NetMech& m) = nullptr;
  void (*reset_at)(NetMech& m, const float pos[3]) = nullptr;
  // The state's reads of the mech: its world position (0x005a85e0), the
  // torso node's (0x004ed530), the target's player (0x004e7010 +0x6fc).
  bool (*position)(Mech& m, float out[3]) = nullptr;
  bool (*torso_position)(Mech& m, float out[3]) = nullptr;
  uint32_t (*target_player)(Mech& m) = nullptr;
  // Test hooks; unset run the port: 0x004846a0, 0x00430d10, 0x004b0220 on
  // +0xd4, vtable +0xdc, the mech's making (0x0046e280).
  const MechDfn* (*dfn)(int32_t roster) = nullptr;
  MechData* (*data)(int32_t roster) = nullptr;
  NetMech* (*make)(uint32_t owner, MechData* data, NetControls* ctl, Config* cfg, const char* name) = nullptr;
  // ... and the apply's mech calls (fn the exe address: 0x0041cc30 power,
  // 0x0041df50 MASC, 0x0041ac10 NARC, 0x0041bda0 crouch, 0x00415a90 the
  // limp's end, 0x0041d5f0 its start, 0x00416780 an action), the weapons'
  // aim (0x004b0bc0) and replayed shots (0x004b08c0);
  void (*mech_call)(NetMech& m, uint32_t fn, int32_t arg) = nullptr;
  void (*aim)(NetMech& m, const float at[3]) = nullptr;
  void (*fire)(NetMech& m, uint32_t slot, uint32_t target, int32_t trigger) = nullptr;
  // ... and 0x004259c0, Validate 0x004254d0, the skin 0x00414390.
  Config* (*unpack)(const uint32_t* p, size_t words) = nullptr;
  bool (*valid)(const Config& c) = nullptr;
  void (*skin)(NetMech& m, Handle skin) = nullptr;
  void (*unlimited)(Mech& m, bool on) = nullptr;
  void (*place)(Mech& m, const float pos[3], float yaw) = nullptr;
};
NetCalls& net_calls();

void net_start();   // 0x0046ceb0: once, before the multiplayer mission's launch
void net_end();     // 0x0046d100: once, as the player leaves the game
NetMech* net_mech(uint32_t owner);  // the list's lookup by +0x6fc (inlined in the exe)
// Ours: the network mech a vehicle is, if it is one (the exe reads +0x6fc of
// whatever it is given: past the end of a plain mech; FIX (A)).
NetMech* net_mech_of(Handle vehicle);
// 0x0046d880, the handler of every in-game message: to the sender's mech,
// a first 0xf making it; 0x19 straight on, 0x15 and the rest dropped.
void net_route(uint32_t from, const uint8_t* msg, size_t size);
// 0x0046d3b0, local message 6: the player's mech away and its lobby slot
// marked gone.
void net_player_left(uint32_t from, const uint8_t* msg, size_t size);
// 0x0046f160, the mech's vt+0x168: what it tells the others, by kind: 0xf
// its state (with the shots and damaged parts since the last; the send's
// id back), 0x10 a ping to the next player, 0x15 a part's damage to send
// (arg the part id), 0x16 a shot (arg the Weapon); the rest piece 7's.
// 0 without a network game.
int32_t net_send_event(Mech& m, int32_t kind, intptr_t arg);
// The state message as it goes (0x3c bytes, then a byte a shot, a word a
// damaged part).
std::vector<uint8_t> net_state_message(Mech& m);
// 0x0046ef70 (the local mechs' vt+0x158): the state at the send rate (every
// 26th with every part's damage), a ping every 5.1 s, the clock's warnings
// and the host's clock sends, the game's end.
void net_mech_tick(Mech& m);
float net_send_period(int32_t players);  // 0x00470b40 (ECX the players)
uint32_t net_next_player();              // 0x00470ab0: round the remote mechs' players

// 0x0046e4d0 (NetCalls::state unset): a state applied to the sender's mech:
// unless it is inactive, its velocity, throttle and turn, torso, aim
// (extrapolated by the latency), heading, passive radar and target; its
// shots replayed and parts' damage set; then its power, AMS, MASC, NARC,
// crouch, jump and motion state's changes (a respawn's is piece 7's).
void net_state_apply(NetMech& m, const uint8_t* msg, size_t size);
// 0x0046ed70, a network mech's vt+0x158: to the state's place and heading,
// at once when it said so (flag bit 0), else blended at the gains and
// carried on at its damped velocity.
void net_reckon(NetMech& m, float dt);
// 0x00470a00 (NetCalls::ping unset): their ping; the latency from our last
// one they answer.
void net_ping_in(NetMech& m, const uint8_t* msg, size_t size);
// 0x004709d0 (NetCalls::send_done unset): local message 0xe, the pilot's
// last state has gone (its id and send time).
void net_send_done(const uint8_t* msg, size_t size);

// The events (piece 7a): the receivers' defaults.
void net_posture_in(NetMech& m, const uint8_t* msg, size_t size);   // 0x0046ed10: the action queued
bool net_pilot_respawn(Mech& m);                                     // 0x0046dde0: the pilot's mech made again at a start
void net_respawn_in(NetMech& m, const float pos[3]);                // 0x0046ed30: a dead mech made again there
void net_mech_rebuild(NetMech& m);                                  // 0x0046e0f0: its fresh mech, in place
void net_kill_in(const uint8_t* msg, size_t size);                  // 0x0046eba0: the HUD's line; the host scores
void net_score_kill(uint32_t victim, uint32_t killer);              // 0x00470170: the host's count and 0x14
void net_scores_in(const uint8_t* msg, size_t size);                // 0x004708e0
void net_shove_in(NetMech& from, const uint8_t* msg, size_t size);  // 0x004705b0: a collision's push on the pilot
void net_part_off_in(const uint8_t* msg, size_t size);              // 0x0046fff0: the pilot's part shot off
// 0x00470320: a collision's push on a network mech, to its player.
void net_send_shove(Mech& other, float damage, float vx, float vz);

// The world's events (piece 7b). 0x0046fd30, zDecal's veto in a network
// game: a crater the pilot's shot makes goes through the host (0x19), which
// makes it and sends it to all; no other crater is made here. One with a
// negative radius (made from a 0x19) passes, its radius made positive.
int net_crater_veto(const zwep::CraterOrder* order);
void net_crater_in(const uint8_t* msg, size_t size);   // 0x0046fde0
void net_anim_sent(zeff::AnimLogRecord* rec);          // 0x0046feb0, the anim log's: the effect to all (0x1a)
void net_effect_in(const uint8_t* msg, size_t size);   // 0x0046ff50: replayed unless one like it runs
void net_send_drop(uint32_t station);                  // 0x0046ff90: a repair station used (0x1b)
void net_drop_in(NetMech& m, const uint8_t* msg, size_t size);  // 0x0046ffd0
void net_clock_in(const uint8_t* msg, size_t size);    // 0x00470740: the clock, and the game's end with its reason
void net_game_over();  // 0x004707c0
// 0x004700a0 and 0x00500c60 with 0x004700e0: the host's effects and craters
// sent again to a player joining.
void net_host_replay();

// 0x0046da80 (ECX the pilot), at the multiplayer launch: the local player's
// record (2, the skin, the mech, the packed loadout: Config::packed of the
// game's slot 0) as its player data, its team, the game's settings on it,
// and its place at aiv.zrd's "remote_<n>" for its lobby slot. False without
// that place.
bool net_launch_player(Mech& m, const std::vector<uint32_t>& packed, int32_t skin, bool unlimited_ammo,
                       bool outside_view);
// 0x0046e0a0 (ECX the point, EDX the slot): players sharing a start point
// spread 10 m by their slot.
void net_spawn_offset(float pos[3], int32_t slot);
// 0x0046d450 (NetCalls::create unset): the sender's mech from their record:
// its loadout unpacked and named "<name>'s Config" (one that fails the
// mech lab's checks puts the player on the missing list instead, and no
// mech is made), its controls, the type's data, the mech, its team and skin; the
// host replays the anims and craters to it.
void net_mech_create(uint32_t from);
// 0x0046e280: the mech, with the network's: not colliding, on the list, in
// the world, its torso in mode 2, powered up. net_mech_construct builds it
// in place (the respawn's rebuild does so on the old mech's memory).
NetMech* net_mech_new(uint32_t owner, MechData* data, NetControls* ctl, Config* cfg, const char* name);
void net_mech_construct(NetMech* m, uint32_t owner, MechData* data, NetControls* ctl, Config* cfg, const char* name);
// 0x0046d820: a player's record, empty when it is under 8 bytes or not got.
bool net_read_record(uint32_t player, std::vector<uint8_t>* out);

// 0x0046e3a0 / 0x0046e3c0: off the remote mechs' and the dead mechs'
// lists (net_mech_unlist, with the label), then the mech's own (0x004152d0)
// and the delete.
void net_mech_delete(NetMech* m);
void net_mech_unlist(NetMech* m);

}  // namespace mech3
