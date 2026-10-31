#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "mech3/mech.h"

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

// The network mech (0xc1c bytes, vtable 0x005c3508): a remote player's
// mech, driven by their messages. Its constructor 0x0046e280 and creation
// 0x0046d450 are piece 5b; here only what the list reads.
struct NetMech : Mech {
  uint32_t x6f8 = 0;   // +0x6f8  0 once made (0x0046d450)
  uint32_t owner = 0;  // +0x6fc  the player it stands for
  uint32_t heard = 0;  // +0x73c  when its last state came (timeGetTime, ms)
};

struct NetGame {
  bool running = false;                // [0x00605fb4]  a network game is up
  std::list<NetMech*> mechs;           // [0x00605f84], count [0x00605f88]: the remote mechs
  std::vector<Config*> configs;        // [0x00605f74..0x00605f7c]: their loadouts (0x0046d450)
  std::list<std::string> missing;      // [0x00605f94], count [0x00605f98]: mechs that did not load (0x0046d450)
  // The dead reckoning's (0x0046ed70) gains, set by the start: the exe's
  // data holds -6.5 and -3.5 before.
  float reckon_gain = -6.5f;           // [0x005e55f0]
  float reckon_damp = -3.5f;           // [0x005e55ec]
};
NetGame& net_game();

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
  NetRoute create = nullptr;      // 0x0046d450: the sender's mech made from its first 0xf (piece 5b)
  void (*set_position)(NetMech& m, const float pos[3]) = nullptr;  // vt+0x18, zEntity3d's 0x005a8550
  void (*reckon)(NetMech& m, float dt) = nullptr;  // vt+0x158, 0x0046ed70 with [0x0072c52c]
  uint32_t (*now_ms)() = nullptr;                  // 0x00575d30, timeGetTime; unset: ztimer's clock
  void (*hud_message)(float seconds, int32_t id, const char* name) = nullptr;  // 0x0040fe70
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
};
NetCalls& net_calls();

void net_start();   // 0x0046ceb0: once, before the multiplayer mission's launch
void net_end();     // 0x0046d100: once, as the player leaves the game
NetMech* net_mech(uint32_t owner);  // the list's lookup by +0x6fc (inlined in the exe)
// 0x0046d880, the handler of every in-game message: to the sender's mech,
// a first 0xf making it; 0x19 straight on, 0x15 and the rest dropped.
void net_route(uint32_t from, const uint8_t* msg, size_t size);
// 0x0046d3b0, local message 6: the player's mech away and its lobby slot
// marked gone.
void net_player_left(uint32_t from, const uint8_t* msg, size_t size);
// 0x0046e3a0 / 0x0046e3c0: off the remote mechs' and the dead mechs'
// lists, then the mech's own (0x004152d0) and the delete.
void net_mech_delete(NetMech* m);

}  // namespace mech3
