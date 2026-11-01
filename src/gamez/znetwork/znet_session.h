#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// zNetwork's session (gamezmech3\znetwork\znet_session.cpp and znet_dplay.cpp,
// 0x00586c20..0x00589540; docs/multiplayer-port.md). The exe wrapped
// DirectPlay 4; the port keeps zNetwork's entry points and its globals and
// puts a Transport of ours under them (FIX (A): DirectPlay is gone).
namespace znet {

// The session a host makes and the others join (DPSESSIONDESC2's parts the
// game reads: its name, the player limit and the lobby's words).
struct SessionDesc {
  std::string name;
  uint32_t max_players = 8;
  uint32_t user[4] = {};  // dwUser1..4: the lobby's settings (0x00586ed0, 0x00586f20)
};

// DirectPlay's answers the game reads (HRESULTs).
constexpr int32_t kOk = 0;
constexpr int32_t kErrGeneric = static_cast<int32_t>(0x80004005u);     // E_FAIL
constexpr int32_t kErrConnecting = static_cast<int32_t>(0x8877015eu);  // DPERR_CONNECTING: still searching
constexpr int32_t kErrNoConnection = static_cast<int32_t>(0x887700aau);  // DPERR_NOCONNECTION

// A game the search heard of (ours: DirectPlay's session description as the
// transport got it, and where to join it).
struct FoundGame {
  SessionDesc desc;
  uint32_t players = 0;  // dwCurrentPlayers
  std::string address;   // "ip:port"
};

// zNetwork's game information (0x74 bytes in the exe): the game list's
// entry (0x00586ea0, 0x00587680) and what a host asks for (0x00586f60,
// 0x00587370). The words are the session's dwUser1..4: the host sets bit 0
// of the first, the second is the game's version ([0x005e55e8], checked
// by the join), the fourth 2 in a game in progress and 3 in a locked one
// (which cannot be joined: 0x00494510's lines, 0x00495410).
struct GameInfo {
  uint32_t user[4] = {};    // +0x00..+0x0c
  int32_t index = 0;        // +0x10  the game list's entry to join
  uint32_t max_players = 0; // +0x14
  uint32_t players = 0;     // +0x18
  std::string name;         // +0x1c
};

// One of DirectPlay's system events as the transport gives it (the DPMSG_*
// fields the translation 0x00588a70 reads; multiplayer-port.md section 3:
// fields, not the exe's struct with its pointers).
struct SystemEvent {
  uint32_t kind = 0;         // a SystemKind
  uint32_t player_type = 0;  // DPPLAYERTYPE_: 1 a player, 2 a team
  uint32_t id = 0;           // the player or team (add / delete from a team: the player)
  uint32_t group = 0;        // add / delete from a team: the team
  std::string short_name, long_name;  // create: DPNAME's
  uint32_t msg_id = 0, send_time = 0;  // send done
  SessionDesc desc;          // set session desc
};

// What a transport hands up: a message from a player, or (from 0) one of
// DirectPlay's system events, which the poll turns into MW3 messages 2..0xe
// (network-messages.md, Local only).
struct Packet {
  uint32_t from = 0;  // the sender's player id; 0 a system event
  uint32_t to = 0;    // 0 every player (DPID_ALLPLAYERS), else one player or a team
  std::vector<uint8_t> data;  // a system event's: its kind and id words (so the poll's size check passes)
  SystemEvent sys;
};

// A system event's payload (ours; from 0): its kind, then the player or
// team it is about. The kinds are DirectPlay's DPSYS_* numbers.
enum SystemKind : uint32_t {
  kSysCreatePlayerOrGroup = 0x0003,
  kSysDestroyPlayerOrGroup = 0x0005,
  kSysAddPlayerToGroup = 0x0007,
  kSysDeletePlayerFromGroup = 0x0021,
  kSysSessionLost = 0x0031,
  kSysHost = 0x0101,
  kSysSetPlayerOrGroupData = 0x0102,
  kSysSetPlayerOrGroupName = 0x0103,
  kSysSetSessionDesc = 0x0104,
  kSysSendComplete = 0x010d,
};

// The platform's side: one machine's place in a session.
class Transport {
 public:
  virtual ~Transport() = default;
  // Open a session: make one (its first player the host) or join one by its
  // address (DirectPlay's Open, DPOPEN_CREATE or DPOPEN_JOIN).
  virtual bool host(const SessionDesc& desc) = 0;
  virtual bool join(const std::string& address) = 0;
  // The local player in the opened session (CreatePlayer, its DPNAME); its
  // id, 0 when it failed. The others hear of it (DPSYS_CREATEPLAYERORGROUP).
  virtual uint32_t create_player(const std::string& short_name, const std::string& long_name) {
    (void)short_name, (void)long_name;
    return 0;
  }
  // The joined session's description (the host's).
  virtual SessionDesc session_desc() const { return {}; }
  // The host's change of it (SetSessionDesc): the others hear
  // DPSYS_SETSESSIONDESC. False when not the host.
  virtual bool set_session_desc(const SessionDesc& desc) {
    (void)desc;
    return false;
  }
  virtual void close() = 0;
  // A message from the local player; guaranteed ones arrive, in order. The
  // send's id (for the send-done event) in *id, 0 when it has none.
  virtual bool send(uint32_t to, const void* data, size_t size, bool guaranteed, uint32_t* id) = 0;
  // The next message waiting, false when none.
  virtual bool receive(Packet* out) = 0;
  // Whether sends may be asynchronous (DPCAPS's, [0x00730765]).
  virtual bool async_sends() const { return true; }
  // Whether a player is the session's host (GetPlayerCaps' DPCAPS_ISHOST).
  virtual bool is_host(uint32_t id) const { (void)id; return false; }

  // Teams, players and their data (piece 3b; DirectPlay's groups and its
  // replicated "remote" data, every player told by a system event).
  struct Member {
    uint32_t id = 0;
    std::string short_name, long_name;
  };
  virtual uint32_t create_group(const std::string& name) { (void)name; return 0; }  // the team's id, 0 failed
  virtual bool destroy_group(uint32_t group) { (void)group; return false; }
  virtual bool add_to_group(uint32_t group, uint32_t player) { (void)group, (void)player; return false; }
  virtual bool delete_from_group(uint32_t group, uint32_t player) { (void)group, (void)player; return false; }
  virtual bool destroy_player(uint32_t player) { (void)player; return false; }  // leave (ourselves) or kick
  virtual bool players(std::vector<Member>* out) { out->clear(); return true; }
  virtual bool groups(std::vector<Member>* out) { out->clear(); return true; }
  virtual bool group_players(uint32_t group, std::vector<uint32_t>* out) { (void)group; out->clear(); return true; }
  // Set replicates to every player (DPSET_GUARANTEED); get answers DirectPlay's
  // way: false with *size the data's when the buffer is null or short.
  virtual bool set_data(bool group, uint32_t id, const void* data, uint32_t size) {
    (void)group, (void)id, (void)data, (void)size;
    return false;
  }
  virtual bool get_data(bool group, uint32_t id, void* data, uint32_t* size) {
    (void)group, (void)id, (void)data;
    *size = 0;
    return false;
  }
  // The game list's search (DirectPlay's EnumSessions, asynchronous, ALL |
  // RETURNSTATUS: 0x00587140): `address` empty asks the LAN, else that
  // host. Not blocking: the games heard so far into `out`, kOk, or an error.
  virtual int32_t find_sessions(const std::string& address, std::vector<FoundGame>* out) {
    (void)address;
    out->clear();
    return kErrGeneric;
  }
  virtual void stop_search() {}  // DPENUMSESSIONS_STOPASYNC
};

// The local player's record, [0x0073075c] (0xec bytes in the exe; its id at
// +4, the rest with the players' lists, multiplayer-port.md piece 3).
struct Player {
  uint8_t is_host = 0;  // +0x00  set on DPSYS_HOST (0x00588ec9)
  uint32_t id = 0;      // +0x04
};

// A message handler: the sender and the message (its type word first, then
// its size word; the exe's fastcall ECX, EDX). The size is ours, for the
// handlers' checks (multiplayer-port.md section 3).
using Handler = void (*)(uint32_t from, const uint8_t* msg, size_t size);
struct HandlerEntry {
  uint16_t type = 0;  // +0
  Handler fn = nullptr;  // +4
  int32_t arg = 0;    // +8  (2 at every registration; unread)
};

// zNetwork's globals.
struct Session {
  bool initialised = false;   // [0x007306c8]
  bool open = false;          // [0x00730764]  a session joined or made
  uint32_t lobby = 0;         // [0x0073072c]  a lobby's connection (never: the lobby launch is not ported)
  bool async = false;         // [0x00730765]  sends asynchronous
  uint32_t host_id = 0;       // [0x00730760]  the host's player id
  std::unique_ptr<Player> local;  // [0x0073075c]
  SessionDesc desc;               // [0x00730730]'s description (DPSYS_SETSESSIONDESC, 0x00587740)
  Transport* transport = nullptr;  // [0x00730724]'s place
  std::vector<HandlerEntry> handlers;  // [0x00730794] (a std::list), its count [0x00730798]
  int dispatching = 0;                 // ours: handlers removed meanwhile are erased after
  // The game list (0x00587140's two lists [0x00730768] / [0x00730778], the
  // answers of the last search) and the connection's address ("" the LAN).
  std::vector<FoundGame> games;
  std::string address;
  // The system events' translation into MW3 messages 2..0xe (0x00588a70,
  // with the players' lists: piece 3); null drops them.
  void (*system_message)(const Packet& p) = nullptr;
};
Session& session();

// Ours: zNetwork up on a transport (the exe's 0x00589540 made DirectPlay).
void initialise(Transport* t);
// The session's start: host (0x00586f60) or join an address (0x00586fb0, by
// the game list's index in the exe), then the local player made
// (create_player, 0x005886c0). Ours: both steps in one.
bool host_session(const SessionDesc& desc, const char* name);
bool join_session(const std::string& address, const char* name);
// 0x005886c0: the local player, its record (the name or "noname" both its
// names) after the players, teams and members are enumerated; the host's
// caps make it the host (0xd sent). Its id, 0 when it failed.
uint32_t create_player(const char* name);

// The service providers (0x005881f0, EnumConnections into [0x007307c8]):
// ours one by design, TCP/IP's kind 2 over our transport (IPX, modem and
// serial are gone, not listed; multiplayer-port.md, piece 9's plan).
struct Connection {
  std::string name;
  int32_t kind = 2;  // +0x10: 0 modem, 1 IPX, 2 TCP/IP (an address), 3 serial (0x004949f0's switch)
};
const std::vector<Connection>& connections();
// 0x00586ff0: the games' search through the connection (its address in
// the session); kErrGeneric without zNetwork up; `count` the games heard.
int32_t find_games(uint32_t* count);
void stop_search();
// 0x00586ea0: the game list's entry `index` (-1: the session joined or made)
// into `out`; false when there is none.
bool game_info(int32_t index, GameInfo* out);
// 0x00586f60: a session opened as `info` asks (its name, limit and words);
// 0x00586fb0: the game list's entry info.index joined. kOk or an error; the
// local player is made after (create_player).
int32_t open_host(const GameInfo& info);
// 0x00586f20 (0x005876f0): the host's session given info's words and limit
// (SetSessionDesc); 0x00586e10: the description asked again (GetSessionDesc).
bool set_game_info(const GameInfo& info);
bool refresh_session();
int32_t open_join(const GameInfo& info);
// Ours: the session joined left (its local player not made yet), zNetwork
// still up for another search (the connection screen's name in use).
void close_session();

bool active();          // 0x00586ce0: up, with a session or a lobby's
uint32_t local_id();    // 0x00586d10: the local player's id, 0 without one
bool is_host();         // 0x00586d20: the local player is the host
// 0x005888a0: a message from the local player to `to` (0 all); the async
// send's id, else 0 (also on a failure, which is reported).
uint32_t send(const void* msg, uint32_t size, bool guaranteed, uint32_t to);
// The handlers (network-messages.md, Plumbing).
bool on(uint16_t type, Handler fn, int32_t arg = 2);  // 0x00587aa0: added at the end
bool off(uint16_t type, Handler fn);                  // 0x00587b20: every entry of that type and handler
void clear_handlers();                                // 0x00587a00
void dispatch(uint32_t from, const uint8_t* msg, size_t size);  // 0x00587c40: each handler of its type, in order
// 0x00588930: the waiting messages, at most `max` (-1: all) handled; under
// 4 bytes dropped, the system's (from 0) to its translation, the rest
// dispatched. How many were handled; 0 without a network game.
int32_t poll(int32_t max, bool free_buffer = false);

// 0x00586c20: the session left and zNetwork down (the handlers cleared,
// the local player destroyed, the transport closed, the globals cleared).
void shutdown();

// Ours: an in-process transport for the tests, every endpoint of one Hub in
// the same session: the first to host is the host, ids from 1 in order.
class Hub {
 public:
  Hub();
  ~Hub();
  Transport* endpoint();  // a new machine's transport (the hub keeps it)
  struct Impl;
  std::unique_ptr<Impl> impl;
};

}  // namespace znet
