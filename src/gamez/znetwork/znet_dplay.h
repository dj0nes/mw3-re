#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gamez/znetwork/znet_session.h"

// zNetwork's players and teams and the system events' translation
// (gamezmech3\znetwork\znet_dplay.cpp, 0x00588180..0x00589540;
// docs/multiplayer-port.md piece 3).
namespace znet {

// A player's record (0xec bytes, made by the translation on DPSYS_CREATE).
struct PlayerRecord {
  uint8_t is_host = 0;          // +0x00  DPCAPS_ISHOST from GetPlayerCaps (0x00589870)
  uint32_t id = 0;              // +0x04
  uint32_t caps_flags = 0;      // +0x0c  of the DPCAPS at +0x08 (dwSize 0x28)
  std::string short_name;       // +0x44  0x50 bytes (DPNAME's short name, its pointer +0x38 here)
  std::string long_name;        // +0x94  0x50 bytes (+0x3c)
  uint32_t team = 0;            // +0xe4  its team's id, 0 none
  int32_t xe8 = -1;             // +0xe8
};

// A team's record (0x54 bytes).
struct TeamRecord {
  std::string name;             // +0x00  0x4f characters at most
  uint32_t id = 0;              // +0x50
};

// The lists: [0x007307ac] (std::list at 0x007307a8) and [0x007307bc]
// (at 0x007307b8, its count [0x007307c0]). In order of arrival.
struct Lists {
  std::vector<std::unique_ptr<PlayerRecord>> players;
  std::vector<std::unique_ptr<TeamRecord>> teams;
};
Lists& lists();

PlayerRecord* player(uint32_t id);  // 0x00589700: the record of that id, null none
TeamRecord* team(uint32_t id);      // 0x00589960 on the teams: by id
// 0x00588a60: the record's data (+0x44: its short name) of that player, null none.
const char* player_name(uint32_t id);
// 0x005898b0 / 0x00589910: each player (its id and team, is it the host, its
// short name) / each team (its id, 0, 0, its name) through a callback (the
// exe's fastcall: ECX, EDX, then the two pushed); the walks answer the count.
using EachFn = void (*)(uint32_t id, uint32_t team, bool host, const char* name, void* ctx);
int32_t each_player(EachFn fn, void* ctx);
int32_t each_team(EachFn fn, void* ctx);

// 0x00588a70: one system event into the lists and an MW3 local message
// (types 2..0xe, network-messages.md Local only) dispatched.
void system_message(const Packet& p);
// 0x0058ae10: a network error's text as local message 0xc, from the local player.
void network_error(const std::string& text);
// 0x00586d50, 0xd's handler from zNetwork's set-up: the sender is the host.
void on_host(uint32_t from, const uint8_t* msg, size_t size);

// The teams' and the data's calls (piece 3b). A failed call is reported as
// a network error (0x0058a8c0's name for it, 0x0058ae10).
uint32_t create_team(const char* name);              // 0x00588390: the team's id, its record kept
bool add_to_team(uint32_t team, uint32_t player);    // 0x005884c0
bool remove_from_team(uint32_t team, uint32_t player);  // 0x00588500: the record's team cleared
bool destroy_team(uint32_t team);                    // 0x00588590
bool team_named(const char* name);                   // 0x00589680: a team of that name (strcmp) is listed
int32_t enum_teams();                                // 0x00588200: the teams' list made again; their count
bool enum_team_players();                            // 0x00588300: each team's players' records given it
int32_t enum_players();                              // 0x005885c0: records for new players; their count
bool leave();                                        // 0x00588660: the local player destroyed
bool kick(uint32_t player);                          // 0x005886a0
bool get_player_data(uint32_t id, void* data, uint32_t* size);  // 0x00589770
uint32_t player_data_size(uint32_t id);              // 0x00589750
bool set_player_data(uint32_t id, const void* data, uint32_t size);  // 0x00589830
bool get_team_data(uint32_t id, void* data, uint32_t* size);    // 0x005897f0
bool set_team_data(uint32_t id, const void* data, uint32_t size);   // 0x005897b0

// Ours: zNetwork's set-up's registrations and slots (0x00586bfc: 0xd's
// handler; the translation as the poll's), and the lists emptied.
void wire_players();
void clear_lists();

}  // namespace znet
