#include "gamez/znetwork/znet_session.h"

#include "gamez/znetwork/znet_dplay.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>

namespace znet {

Session& session() {
  static Session s;
  return s;
}

void initialise(Transport* t) {
  Session& s = session();
  s.transport = t;
  s.initialised = t != nullptr;
  s.async = t && t->async_sends();
  if (t) wire_players();  // 0x00586bfc's registration of 0xd, the translation
}

bool host_session(const SessionDesc& desc, const char* name) {
  Session& s = session();
  if (!s.initialised || s.open || !s.transport->host(desc)) return false;
  s.open = true, s.desc = desc;
  return create_player(name) != 0;
}

bool join_session(const std::string& address, const char* name) {
  Session& s = session();
  if (!s.initialised || s.open || !s.transport->join(address)) return false;
  s.open = true, s.desc = s.transport->session_desc();  // 0x00587580's copy of the joined one
  return create_player(name) != 0;
}

// Mech3.exe 1.2 @ 0x005881f0
const std::vector<Connection>& connections() {
  static const std::vector<Connection> kOne = {{"LAN / Internet (UDP)", 2}};
  return kOne;
}

// Mech3.exe 1.2 @ 0x00586ff0 with 0x00587140
int32_t find_games(uint32_t* count) {
  Session& s = session();
  if (count) *count = 0;
  if (!s.initialised || !s.transport) return kErrGeneric;
  const int32_t r = s.transport->find_sessions(s.address, &s.games);
  if (count) *count = static_cast<uint32_t>(s.games.size());
  return r;
}

void stop_search() {
  if (Session& s = session(); s.transport) s.transport->stop_search();
}

// Mech3.exe 1.2 @ 0x00586ea0 (0x005872a0 the entry, 0x00587680 its copy)
bool game_info(int32_t index, GameInfo* out) {
  const Session& s = session();
  const SessionDesc* d = nullptr;
  uint32_t players = 0;
  if (index == -1) {  // 0x00586ed0: dwCurrentPlayers, here the players known
    if (!s.open) return false;
    d = &s.desc;
    players = static_cast<uint32_t>(lists().players.size());
  } else if (index >= 0 && static_cast<size_t>(index) < s.games.size()) {
    d = &s.games[static_cast<size_t>(index)].desc;
    players = s.games[static_cast<size_t>(index)].players;
  }
  if (!d) return false;
  for (int i = 0; i < 4; ++i) out->user[i] = d->user[i];
  out->max_players = d->max_players, out->players = players;
  out->name = d->name;  // an empty one is [0x005e56f0] in the exe's copy
  return true;
}

// Mech3.exe 1.2 @ 0x00586f20 (0x005876f0)
bool set_game_info(const GameInfo& info) {
  Session& s = session();
  if (!is_host() || !s.transport) return false;
  for (int i = 0; i < 4; ++i) s.desc.user[i] = info.user[i];
  s.desc.max_players = info.max_players;
  return s.transport->set_session_desc(s.desc);
}

// Mech3.exe 1.2 @ 0x00586e10
bool refresh_session() {
  Session& s = session();
  if (!s.transport || !s.open) return false;
  s.desc = s.transport->session_desc();
  return true;
}

// Mech3.exe 1.2 @ 0x00586f60 (0x00587370 the description, 0x00587480 Open)
int32_t open_host(const GameInfo& info) {
  Session& s = session();
  if (!s.initialised || s.open || !s.transport) return kErrGeneric;
  SessionDesc d;
  d.name = info.name, d.max_players = info.max_players;
  for (int i = 0; i < 4; ++i) d.user[i] = info.user[i];
  if (!s.transport->host(d)) return kErrGeneric;
  s.open = true, s.desc = d;
  return kOk;
}

// Mech3.exe 1.2 @ 0x00586fb0 (0x00587580 Open on the entry)
int32_t open_join(const GameInfo& info) {
  Session& s = session();
  if (!s.initialised || s.open || !s.transport) return kErrGeneric;
  if (info.index < 0 || static_cast<size_t>(info.index) >= s.games.size()) return kErrGeneric;
  if (!s.transport->join(s.games[static_cast<size_t>(info.index)].address)) return kErrGeneric;
  s.open = true, s.desc = s.transport->session_desc();
  return kOk;
}

void close_session() {
  Session& s = session();
  if (!s.open) return;
  if (s.transport) s.transport->close();
  s.open = false, s.desc = SessionDesc{}, s.host_id = 0;
  s.local.reset();
  clear_lists();
}

// Mech3.exe 1.2 @ 0x00586ce0
bool active() {
  const Session& s = session();
  return s.initialised && (s.open || s.lobby != 0);
}

// Mech3.exe 1.2 @ 0x00586d10
uint32_t local_id() {
  const Session& s = session();
  return s.local ? s.local->id : 0;
}

// Mech3.exe 1.2 @ 0x00586d20
bool is_host() {
  const Session& s = session();
  return s.local && s.host_id == local_id();
}

// Mech3.exe 1.2 @ 0x005888a0: SendEx from the local player, DPSEND_GUARANTEED
// when asked, DPSEND_ASYNC when the provider allows ([0x00730765]); a failure
// other than DPERR_PENDING goes to the error report (0x0058a880, line 0x1d9).
// FIX (bug): without a local player the exe reads [0x0073075c]+4 through
// null; nothing is sent.
uint32_t send(const void* msg, uint32_t size, bool guaranteed, uint32_t to) {
  Session& s = session();
  if (!s.transport || !s.local) return 0;
  uint32_t id = 0;
  if (!s.transport->send(to, msg, size, guaranteed, s.async ? &id : nullptr)) {
    std::fprintf(stderr, "mw3: network: a message (type 0x%x, %u bytes) was not sent\n",
                 size >= 2 ? static_cast<const uint8_t*>(msg)[0] | static_cast<const uint8_t*>(msg)[1] << 8 : 0, size);
    return 0;
  }
  return s.async ? id : 0;
}

// Mech3.exe 1.2 @ 0x00587aa0
bool on(uint16_t type, Handler fn, int32_t arg) {
  session().handlers.push_back(HandlerEntry{type, fn, arg});
  return true;
}

// Mech3.exe 1.2 @ 0x00587b20 (std::remove then erase). FIX (bug): while a
// dispatch runs the entries are marked and erased after it; the exe's walk
// (0x00587c40) read the deleted node.
bool off(uint16_t type, Handler fn) {
  Session& s = session();
  for (HandlerEntry& e : s.handlers)
    if (e.type == type && e.fn == fn) e.fn = nullptr;
  if (!s.dispatching)
    s.handlers.erase(std::remove_if(s.handlers.begin(), s.handlers.end(), [](const HandlerEntry& e) { return !e.fn; }),
                     s.handlers.end());
  return true;
}

// Mech3.exe 1.2 @ 0x00587a00
void clear_handlers() {
  Session& s = session();
  if (s.dispatching) {
    for (HandlerEntry& e : s.handlers) e.fn = nullptr;
    return;
  }
  s.handlers.clear();
}

// Mech3.exe 1.2 @ 0x00587c40. A handler added meanwhile is reached too (the
// list's end moves on), as in the exe.
void dispatch(uint32_t from, const uint8_t* msg, size_t size) {
  Session& s = session();
  if (size < 2) return;
  uint16_t type;
  std::memcpy(&type, msg, 2);
  ++s.dispatching;
  for (size_t i = 0; i < s.handlers.size(); ++i) {
    const HandlerEntry e = s.handlers[i];
    if (e.fn && e.type == type) e.fn(from, msg, size);
  }
  if (--s.dispatching == 0)
    s.handlers.erase(std::remove_if(s.handlers.begin(), s.handlers.end(), [](const HandlerEntry& e) { return !e.fn; }),
                     s.handlers.end());
}

// Mech3.exe 1.2 @ 0x00588930. DirectPlay's Receive into the growing buffer
// [0x007307d8] (its size [0x007307dc]) is the transport's; `free_buffer`
// freed it, so here it does nothing.
int32_t poll(int32_t max, bool free_buffer) {
  (void)free_buffer;
  if (!active()) return 0;
  Session& s = session();
  int32_t handled = 0;
  Packet p;
  while (s.transport && s.transport->receive(&p)) {
    if (p.data.size() > 3) {
      --max, ++handled;
      if (p.from == 0) {
        if (s.system_message) s.system_message(p);
      } else {
        dispatch(p.from, p.data.data(), p.data.size());
      }
    }
    if (max == 0) break;
    if (!active()) break;  // ours: a handler ended the session
  }
  return handled;
}

// Mech3.exe 1.2 @ 0x00586c20. The players' and teams' lists with piece 3.
void shutdown() {
  Session& s = session();
  if (!s.initialised) return;
  poll(-1);
  clear_handlers();
  if (s.transport) s.transport->stop_search(), s.transport->close();  // DestroyPlayer(local) and Close
  s.initialised = false;
  s.open = false;
  s.lobby = 0;
  s.async = false;
  s.host_id = 0;
  s.local.reset();
  s.games.clear();
  s.address.clear();
  s.transport = nullptr;
  clear_lists();  // 0x00587db0, 0x00587e30, 0x00587ed0
}

// ---------------------------------------------------------------------------
// The in-process hub

struct Hub::Impl {
  struct End;
  struct Group {
    uint32_t id = 0;
    std::string name;
    std::vector<uint32_t> members;
    std::vector<uint8_t> data;
  };
  std::vector<std::unique_ptr<End>> ends;
  std::vector<Group> groups;
  SessionDesc desc;
  uint32_t next_id = 1, next_msg = 1, host_id = 0, next_group = 1000;
  bool hosted = false;
  void system(End* to, const SystemEvent& e);
  void to_all(const SystemEvent& e, const End* but = nullptr);
  Group* group(uint32_t id) {
    for (Group& g : groups)
      if (g.id == id) return &g;
    return nullptr;
  }
  End* end_of(uint32_t id);
};

struct Hub::Impl::End : Transport {
  Hub::Impl* hub = nullptr;
  uint32_t id = 0;
  std::string short_name, long_name;
  std::vector<uint8_t> data;  // the player's replicated data
  bool opened = false;
  std::deque<Packet> inbox;
  uint32_t create_group(const std::string& name) override {
    if (!id) return 0;
    Group g;
    g.id = hub->next_group++, g.name = name;
    hub->groups.push_back(g);
    SystemEvent e;
    e.kind = kSysCreatePlayerOrGroup, e.player_type = 0, e.id = g.id, e.short_name = e.long_name = name;
    hub->to_all(e, this);
    return g.id;
  }
  bool destroy_group(uint32_t gid) override {
    Group* g = hub->group(gid);
    if (!id || !g) return false;
    hub->groups.erase(hub->groups.begin() + (g - hub->groups.data()));
    SystemEvent e;
    e.kind = kSysDestroyPlayerOrGroup, e.player_type = 0, e.id = gid;
    hub->to_all(e);
    return true;
  }
  bool add_to_group(uint32_t gid, uint32_t player) override {
    Group* g = hub->group(gid);
    if (!id || !g || !hub->end_of(player)) return false;
    if (std::find(g->members.begin(), g->members.end(), player) == g->members.end()) g->members.push_back(player);
    SystemEvent e;
    e.kind = kSysAddPlayerToGroup, e.group = gid, e.id = player;
    hub->to_all(e);
    return true;
  }
  bool delete_from_group(uint32_t gid, uint32_t player) override {
    Group* g = hub->group(gid);
    if (!id || !g) return false;
    auto it = std::find(g->members.begin(), g->members.end(), player);
    if (it == g->members.end()) return false;
    g->members.erase(it);
    SystemEvent e;
    e.kind = kSysDeletePlayerFromGroup, e.group = gid, e.id = player;
    hub->to_all(e);
    return true;
  }
  bool destroy_player(uint32_t player) override {
    End* who = hub->end_of(player);
    if (!id || !who) return false;
    who->close();  // the others hear it (and its inbox is emptied)
    if (who != this) {  // a kick: then it hears its own end
      SystemEvent e;
      e.kind = kSysDestroyPlayerOrGroup, e.player_type = 1, e.id = player;
      hub->system(who, e);
    }
    return true;
  }
  bool players(std::vector<Member>* out) override {
    out->clear();
    for (auto& o : hub->ends)
      if (o->id) out->push_back(Member{o->id, o->short_name, o->long_name});
    return opened;  // after Open, before CreatePlayer too (0x005886c0)
  }
  bool groups(std::vector<Member>* out) override {
    out->clear();
    for (const Group& g : hub->groups) out->push_back(Member{g.id, g.name, g.name});
    return opened;
  }
  bool group_players(uint32_t gid, std::vector<uint32_t>* out) override {
    out->clear();
    const Group* g = hub->group(gid);
    if (!g) return false;
    *out = g->members;
    return true;
  }
  bool set_data(bool is_group, uint32_t who, const void* d, uint32_t size) override {
    std::vector<uint8_t>* into = nullptr;
    if (is_group) {
      if (Group* g = hub->group(who)) into = &g->data;
    } else if (End* e = hub->end_of(who)) {
      into = &e->data;
    }
    if (!id || !into) return false;
    into->assign(static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + size);
    SystemEvent e;
    e.kind = kSysSetPlayerOrGroupData, e.player_type = is_group ? 0 : 1, e.id = who;
    hub->to_all(e);  // the setter too (as the teams' changes)
    return true;
  }
  bool get_data(bool is_group, uint32_t who, void* d, uint32_t* size) override {
    const std::vector<uint8_t>* from = nullptr;
    if (is_group) {
      if (Group* g = hub->group(who)) from = &g->data;
    } else if (End* e = hub->end_of(who)) {
      from = &e->data;
    }
    if (!from) {
      *size = 0;
      return false;
    }
    const uint32_t have = *size;
    *size = static_cast<uint32_t>(from->size());
    if (!d || have < from->size()) return false;  // DPERR_BUFFERTOOSMALL
    std::memcpy(d, from->data(), from->size());
    return true;
  }
  bool is_host(uint32_t who) const override { return who && who == hub->host_id; }
  SessionDesc session_desc() const override { return hub->desc; }
  bool set_session_desc(const SessionDesc& d) override {
    if (!id || id != hub->host_id) return false;
    hub->desc = d;
    SystemEvent e;
    e.kind = kSysSetSessionDesc, e.desc = d;
    hub->to_all(e, this);
    return true;
  }
  // The search finds the hub's game, if one is hosted ("loopback" to join).
  int32_t find_sessions(const std::string&, std::vector<FoundGame>* out) override {
    out->clear();
    if (!hub->hosted) return kOk;
    FoundGame g;
    g.desc = hub->desc, g.address = "loopback";
    for (const auto& e : hub->ends) g.players += e->id != 0 ? 1 : 0;
    out->push_back(g);
    return kOk;
  }
  bool host(const SessionDesc& d) override {
    if (hub->hosted || opened) return false;
    hub->hosted = opened = true;
    hub->desc = d;
    return true;
  }
  bool join(const std::string&) override {
    if (!hub->hosted || opened) return false;
    return opened = true;
  }
  // The first player made is the host; the others hear of a new one (it
  // learns of them by enumerating, 0x005886c0).
  uint32_t create_player(const std::string& s, const std::string& l) override {
    if (!opened || id) return 0;
    short_name = s, long_name = l;
    id = hub->next_id++;
    if (!hub->host_id) hub->host_id = id;
    SystemEvent e;
    e.kind = kSysCreatePlayerOrGroup, e.player_type = 1, e.id = id, e.short_name = s, e.long_name = l;
    hub->to_all(e, this);
    return id;
  }
  void close() override {
    if (!id) return;
    SystemEvent e;
    e.kind = kSysDestroyPlayerOrGroup, e.player_type = 1, e.id = id;
    id = 0;
    opened = false;
    inbox.clear();
    for (auto& o : hub->ends)
      if (o->id) hub->system(o.get(), e);
  }
  bool send(uint32_t to, const void* data, size_t size, bool, uint32_t* msg_id) override {
    if (!id) return false;
    Packet p;
    p.from = id, p.to = to;
    p.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
    for (auto& o : hub->ends)
      if (o.get() != this && o->id && (to == 0 || to == o->id)) o->inbox.push_back(p);
    if (msg_id) {
      *msg_id = hub->next_msg++;
      SystemEvent e;
      e.kind = kSysSendComplete, e.id = id, e.msg_id = *msg_id;
      hub->system(this, e);
    }
    return true;
  }
  bool receive(Packet* out) override {
    if (inbox.empty()) return false;
    *out = std::move(inbox.front());
    inbox.pop_front();
    return true;
  }
};

void Hub::Impl::to_all(const SystemEvent& e, const End* but) {
  for (auto& o : ends)
    if (o->id && o.get() != but) system(o.get(), e);
}

Hub::Impl::End* Hub::Impl::end_of(uint32_t id) {
  for (auto& o : ends)
    if (id && o->id == id) return o.get();
  return nullptr;
}

void Hub::Impl::system(End* to, const SystemEvent& e) {
  Packet p;
  p.sys = e;
  const uint32_t about = e.kind == kSysSendComplete ? e.msg_id : e.id;
  p.data.resize(8);
  std::memcpy(p.data.data(), &e.kind, 4);
  std::memcpy(p.data.data() + 4, &about, 4);
  to->inbox.push_back(std::move(p));
}

Hub::Hub() : impl(std::make_unique<Impl>()) {}
Hub::~Hub() = default;

Transport* Hub::endpoint() {
  impl->ends.push_back(std::make_unique<Impl::End>());
  impl->ends.back()->hub = impl.get();
  return impl->ends.back().get();
}

}  // namespace znet
