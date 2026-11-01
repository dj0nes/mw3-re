#include "gamez/znetwork/znet_dplay.h"

#include <algorithm>
#include <cstring>

#include "gamez/zutil/zutl_msg.h"

namespace znet {

Lists& lists() {
  static Lists l;
  return l;
}

// Mech3.exe 1.2 @ 0x00589700
PlayerRecord* player(uint32_t id) {
  for (auto& p : lists().players)
    if (p->id == id) return p.get();
  return nullptr;
}

TeamRecord* team(uint32_t id) {
  for (auto& t : lists().teams)
    if (t->id == id) return t.get();
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00588a60
const char* player_name(uint32_t id) {
  const PlayerRecord* p = player(id);
  return p ? p->short_name.c_str() : nullptr;
}

// Mech3.exe 1.2 @ 0x005898b0: the host check against [0x00730760]; the count
// ([0x007307b0]).
int32_t each_player(EachFn fn, void* ctx) {
  for (auto& p : lists().players) fn(p->id, p->team, session().host_id == p->id, p->short_name.c_str(), ctx);
  return static_cast<int32_t>(lists().players.size());
}

// Mech3.exe 1.2 @ 0x00589910: (its id, 0, 0, the record, whose name is
// first); the count.
int32_t each_team(EachFn fn, void* ctx) {
  for (auto& t : lists().teams) fn(t->id, 0, false, t->name.c_str(), ctx);
  return static_cast<int32_t>(lists().teams.size());
}

namespace {
// A local message (type, size, then its words) dispatched from `from`.
void local(uint16_t type, uint32_t from, const std::vector<uint32_t>& words = {}) {
  std::vector<uint8_t> m(4 + 4 * words.size());
  const uint16_t size = static_cast<uint16_t>(m.size());
  std::memcpy(m.data(), &type, 2);
  std::memcpy(m.data() + 2, &size, 2);
  for (size_t i = 0; i < words.size(); ++i) std::memcpy(m.data() + 4 + 4 * i, &words[i], 4);
  dispatch(from, m.data(), m.size());
}
// 0xd, "I am the host" (no payload), to every player, guaranteed.
void say_host() {
  const uint8_t m[4] = {0x0d, 0, 4, 0};
  send(m, 4, true, 0);
}
// A name from the network cut to its field; FIX (A): control characters as
// '?' (the lead's check of the chat: names reach the lists, the chat's
// headers and the HUD), as the search's game names (9a).
std::string clipped(const std::string& s, size_t n) {
  std::string t = s.size() > n ? s.substr(0, n) : s;
  for (char& c : t)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = '?';
  return t;
}
}  // namespace

namespace {
Transport* tp() { return session().transport; }
// The record a player's arrival or the players' enumeration makes (0x00588a70,
// 0x005893f0 alike).
PlayerRecord* add_record(uint32_t id, const std::string& short_name, const std::string& long_name) {
  auto r = std::make_unique<PlayerRecord>();
  r->id = id;
  r->short_name = clipped(short_name, 0x50);
  r->long_name = clipped(long_name, 0x50);
  r->caps_flags = tp() && tp()->is_host(id) ? 2u : 0u;  // 0x00589870(1)
  r->is_host = (r->caps_flags >> 1) & 1;
  lists().players.push_back(std::move(r));
  return lists().players.back().get();
}
}  // namespace

// Mech3.exe 1.2 @ 0x00588a70. The exe's local types 2 and 0xe carried a
// pointer to DirectPlay's message at +4; here their words (ours, section 3):
// 2 the player (8 bytes, as the exe's), 0xe the send's id and its time (12).
void system_message(const Packet& p) {
  Session& s = session();
  Lists& l = lists();
  const SystemEvent& e = p.sys;
  switch (e.kind) {
    case kSysAddPlayerToGroup: {  // 7: the player into the team, type 5
      if (PlayerRecord* r = player(e.id)) r->team = e.group;
      local(5, e.id);
      return;
    }
    case kSysDeletePlayerFromGroup: {  // 0x21: out of its team, type 2
      if (PlayerRecord* r = player(e.id)) r->team = 0;
      local(2, e.id, {e.id});
      return;
    }
    case kSysDestroyPlayerOrGroup:
      if (e.player_type == 1) {
        // Type 7 when it is ourselves (kicked), else 6, before the record goes.
        local(e.id == local_id() ? 7 : 6, e.id);
        // FIX (bug): the exe unlinked the record and never freed it.
        l.players.erase(std::remove_if(l.players.begin(), l.players.end(),
                                       [&](const std::unique_ptr<PlayerRecord>& r) { return r->id == e.id; }),
                        l.players.end());
      } else {
        if (!team(e.id)) return;
        for (auto& r : l.players)
          if (r->team == e.id) r->team = 0;
        l.teams.erase(std::remove_if(l.teams.begin(), l.teams.end(),
                                     [&](const std::unique_ptr<TeamRecord>& t) { return t->id == e.id; }),
                      l.teams.end());
        local(8, e.id);
      }
      return;
    case kSysCreatePlayerOrGroup:
      if (e.player_type == 1) {
        if (!player(e.id)) {
          add_record(e.id, e.short_name, e.long_name);
          local(3, e.id);
        }
        if (is_host()) say_host();
      } else if (!team(e.id)) {
        auto t = std::make_unique<TeamRecord>();
        t->id = e.id;
        t->name = clipped(e.short_name, 0x4f);
        l.teams.push_back(std::move(t));
      }
      local(4, e.id);
      return;
    case kSysSessionLost:  // DPERR_SESSIONLOST's name as a network error
      network_error(zutl::localize("MSG_DPERR_SESSIONLOST"));  // 0x0058a8c0's name for 0x88770136
      return;
    case kSysHost: {  // we are the host now: say so, to all and to ourselves
      if (s.local) s.local->is_host = 1;
      say_host();
      const uint8_t m[4] = {0x0d, 0, 4, 0};
      dispatch(local_id(), m, 4);
      return;
    }
    case kSysSetPlayerOrGroupData:
      local(e.player_type == 1 ? 9 : 0xa, e.id);
      return;
    case kSysSetPlayerOrGroupName:
      local(0xb, e.id);
      return;
    case kSysSetSessionDesc:  // 0x00587740: the 0x50 bytes into [0x00730730]
      s.desc = e.desc;
      return;
    case kSysSendComplete:
      local(0xe, local_id(), {e.msg_id, e.send_time});
      return;
    default:
      return;
  }
}

// Mech3.exe 1.2 @ 0x0058ae10: type 0xc, its size the text's with its 0 and
// the header, then the text.
void network_error(const std::string& text) {
  std::vector<uint8_t> m(text.size() + 1 + 4, 0);
  const uint16_t type = 0xc, size = static_cast<uint16_t>(m.size());
  std::memcpy(m.data(), &type, 2);
  std::memcpy(m.data() + 2, &size, 2);
  std::memcpy(m.data() + 4, text.data(), text.size());
  dispatch(local_id(), m.data(), m.size());
}

// Mech3.exe 1.2 @ 0x00586d50
void on_host(uint32_t from, const uint8_t*, size_t) { session().host_id = from; }


// Mech3.exe 1.2 @ 0x00588390: DPNAME's short and long names both the name.
uint32_t create_team(const char* name) {
  if (!tp()) return 0;
  const uint32_t id = tp()->create_group(name ? name : "");
  if (!id) {  // reported, no record
    network_error(zutl::localize("MSG_DPERR_GENERIC"));
    return 0;
  }
  auto t = std::make_unique<TeamRecord>();
  t->id = id;
  t->name = clipped(name ? name : "", 0x4f);
  lists().teams.push_back(std::move(t));
  return id;
}

// Mech3.exe 1.2 @ 0x005884c0
bool add_to_team(uint32_t team, uint32_t player) {
  if (!tp()) return false;
  if (!tp()->add_to_group(team, player)) {
    network_error(zutl::localize("MSG_DPERR_GENERIC"));
    return false;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00588500
bool remove_from_team(uint32_t team, uint32_t player) {
  if (!tp()) return false;
  if (!tp()->delete_from_group(team, player)) {
    network_error(zutl::localize("MSG_DPERR_GENERIC"));
    return false;
  }
  if (PlayerRecord* r = znet::player(player)) r->team = 0;
  return true;
}

// Mech3.exe 1.2 @ 0x00589680
bool team_named(const char* name) {
  for (auto& t : lists().teams)
    if (name && t->name == name) return true;
  return false;
}

// Mech3.exe 1.2 @ 0x00588590
bool destroy_team(uint32_t team) {
  if (!tp()) return false;
  if (!tp()->destroy_group(team)) {
    network_error(zutl::localize("MSG_DPERR_GENERIC"));
    return false;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x00588200 with its callback 0x00589300: the long name
// (DPNAME +0xc) here, where the arrival takes the short one.
int32_t enum_teams() {
  if (!tp()) return 0;
  lists().teams.clear();
  std::vector<Transport::Member> gs;
  if (!tp()->groups(&gs)) {
    network_error(zutl::localize("MSG_DPERR_GENERIC"));
    return 0;
  }
  for (const Transport::Member& g : gs) {
    if (team(g.id)) continue;
    auto t = std::make_unique<TeamRecord>();
    t->id = g.id;
    t->name = clipped(g.long_name, 0x4f);
    lists().teams.push_back(std::move(t));
  }
  return static_cast<int32_t>(lists().teams.size());
}

// Mech3.exe 1.2 @ 0x00588300 with its callback 0x005892c0
bool enum_team_players() {
  if (!tp()) return false;
  for (auto& t : lists().teams) {
    std::vector<uint32_t> ids;
    if (!tp()->group_players(t->id, &ids)) {
      network_error(zutl::localize("MSG_DPERR_GENERIC"));
      continue;
    }
    for (uint32_t id : ids)
      if (PlayerRecord* r = player(id)) r->team = t->id;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x005885c0 with its callback 0x005893f0; a failure the
// error report's (0x0058a880, line 0x163): 0.
int32_t enum_players() {
  if (!tp()) return 0;
  std::vector<Transport::Member> ps;
  if (!tp()->players(&ps)) return 0;
  for (const Transport::Member& p : ps)
    if (!player(p.id)) add_record(p.id, p.short_name, p.long_name);
  return static_cast<int32_t>(lists().players.size());
}

// Mech3.exe 1.2 @ 0x00588660
bool leave() {
  Session& s = session();
  if (!s.local || !tp()) return false;
  return tp()->destroy_player(s.local->id);
}

// Mech3.exe 1.2 @ 0x005886a0
bool kick(uint32_t player) { return tp() && tp()->destroy_player(player); }

// Mech3.exe 1.2 @ 0x00589770 (DPGET_REMOTE)
bool get_player_data(uint32_t id, void* data, uint32_t* size) {
  return id && tp() && tp()->get_data(false, id, data, size);
}

// Mech3.exe 1.2 @ 0x00589750
uint32_t player_data_size(uint32_t id) {
  uint32_t size = 0;
  get_player_data(id, nullptr, &size);
  return size;
}

// Mech3.exe 1.2 @ 0x00589830 (DPSET_GUARANTEED)
bool set_player_data(uint32_t id, const void* data, uint32_t size) {
  return id && tp() && tp()->set_data(false, id, data, size);
}

// Mech3.exe 1.2 @ 0x005897f0
bool get_team_data(uint32_t id, void* data, uint32_t* size) { return id && tp() && tp()->get_data(true, id, data, size); }

// Mech3.exe 1.2 @ 0x005897b0
bool set_team_data(uint32_t id, const void* data, uint32_t size) {
  return id && tp() && tp()->set_data(true, id, data, size);
}

// Mech3.exe 1.2 @ 0x005886c0. A failed CreatePlayer is reported (the exe
// then kept a null local player).
uint32_t create_player(const char* name) {
  Session& s = session();
  if (!tp()) return 0;
  const std::string n = name ? name : "noname";
  enum_players();       // 0x005885c0
  enum_teams();         // 0x00588200
  enum_team_players();  // 0x00588300
  const uint32_t id = tp()->create_player(clipped(n, 0x50), clipped(n, 0x50));
  if (!id) {
    network_error(zutl::localize("MSG_DPERR_GENERIC"));
    return 0;
  }
  s.local = std::make_unique<Player>();
  s.local->id = id;
  poll(-1);  // 0x00588930
  PlayerRecord* r = player(id);
  if (!r) r = add_record(id, n, n);  // our record at the list's end
  if (tp()->is_host(id)) {  // 0x00589870(0): DPCAPS_ISHOST
    s.host_id = id;
    r->is_host = s.local->is_host = 1;
    say_host();
  }
  return id;
}

void wire_players() {
  session().system_message = &system_message;
  on(0x0d, &on_host);
}

void clear_lists() {
  lists().players.clear();
  lists().teams.clear();
}

}  // namespace znet
