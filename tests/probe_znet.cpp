// zNetwork's session checks and its send (multiplayer-port.md piece 1) for
// tests/differential/test_znet_session.py.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "gamez/znetwork/znet_session.h"

namespace {
// A transport that keeps what the send was asked; `result` 0 sent, 1 sent
// pending (DPERR_PENDING), 2 failed.
struct Recorder : znet::Transport {
  uint32_t to = 0, size = 0, asked_id = 0, guaranteed = 0, first = 0, calls = 0;
  int result = 0;
  bool host(const znet::SessionDesc&) override { return false; }
  bool join(const std::string&) override { return false; }
  void close() override {}
  bool send(uint32_t t, const void* d, size_t n, bool g, uint32_t* id) override {
    ++calls, to = t, size = static_cast<uint32_t>(n), guaranteed = g, asked_id = id != nullptr;
    first = 0;
    std::memcpy(&first, d, n < 4 ? n : 4);
    if (id) *id = 77;
    return result != 2;
  }
  bool receive(znet::Packet*) override { return false; }
};
}  // namespace

extern "C" void probe_znet_state(int init, int open, uint32_t lobby, int has_local, uint32_t local_id, uint32_t host_id,
                                 int out[3]) {
  znet::Session& s = znet::session();
  s.initialised = init != 0, s.open = open != 0, s.lobby = lobby, s.host_id = host_id;
  s.local.reset();
  if (has_local) s.local = std::make_unique<znet::Player>(), s.local->id = local_id;
  out[0] = znet::active(), out[1] = static_cast<int>(znet::local_id()), out[2] = znet::is_host();
  s = znet::Session{};
}

// out: the answer, then the transport's calls, to, size, guaranteed, the id asked for, the first word.
extern "C" void probe_znet_send(int has_local, uint32_t local_id, int async, int guaranteed, uint32_t to, uint32_t size,
                                int result, uint32_t out[7]) {
  Recorder r;
  r.result = result;
  znet::Session& s = znet::session();
  s = znet::Session{};
  s.transport = &r, s.initialised = true, s.async = async != 0;
  if (has_local) s.local = std::make_unique<znet::Player>(), s.local->id = local_id;
  const uint8_t msg[8] = {0x0f, 0x00, static_cast<uint8_t>(size), 0x00, 1, 2, 3, 4};
  out[0] = znet::send(msg, size, guaranteed != 0, to);
  out[1] = r.calls, out[2] = r.to, out[3] = r.size, out[4] = r.guaranteed, out[5] = r.asked_id, out[6] = r.first;
  s = znet::Session{};
}

// The handlers and the dispatch (piece 2): ops of three words, op 0 on(type,
// handler), 1 off(type, handler), 2 clear, 3 dispatch(type, sender); each
// handler call logged as (handler, sender, type). Handler 7 removes handler
// 0 of type 5 while it runs, to show a removal during a dispatch.
namespace {
int32_t* g_log = nullptr;
int32_t g_logged = 0, g_cap = 0;
template <int N>
void logged(uint32_t from, const uint8_t* msg, size_t) {
  if (g_logged + 3 > g_cap) return;
  uint16_t type;
  std::memcpy(&type, msg, 2);
  g_log[g_logged++] = N, g_log[g_logged++] = static_cast<int32_t>(from), g_log[g_logged++] = type;
  if (N == 7) znet::off(5, &logged<0>);
}
const znet::Handler kHandlers[8] = {&logged<0>, &logged<1>, &logged<2>, &logged<3>,
                                    &logged<4>, &logged<5>, &logged<6>, &logged<7>};
}  // namespace

extern "C" int32_t probe_znet_handlers(const int32_t* ops, int32_t n, int32_t* log, int32_t cap) {
  znet::session() = znet::Session{};
  g_log = log, g_logged = 0, g_cap = cap;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t op = ops[3 * i], a = ops[3 * i + 1], b = ops[3 * i + 2];
    if (op == 0) znet::on(static_cast<uint16_t>(a), kHandlers[b & 7]);
    else if (op == 1 && std::getenv("MW3_PROBE_BREAK") && std::string(std::getenv("MW3_PROBE_BREAK")) == "1") {
      auto& hs = znet::session().handlers;  // the break: only the first match removed
      for (auto it = hs.begin(); it != hs.end(); ++it)
        if (it->type == a && it->fn == kHandlers[b & 7]) {
          hs.erase(it);
          break;
        }
    } else if (op == 1) znet::off(static_cast<uint16_t>(a), kHandlers[b & 7]);
    else if (op == 2) znet::clear_handlers();
    else {
      const uint8_t msg[4] = {static_cast<uint8_t>(a), static_cast<uint8_t>(a >> 8), 4, 0};
      znet::dispatch(static_cast<uint32_t>(b), msg, 4);
    }
  }
  znet::session() = znet::Session{};
  return g_logged;
}

// The poll (piece 2): a queue of messages (sender, size) through a scripted
// transport; handler 1 on type 0x1f, system ones counted. out: the answer,
// the system ones seen, then the log as above.
namespace {
struct Script : znet::Transport {
  const int32_t* q = nullptr;
  int32_t n = 0, at = 0;
  bool host(const znet::SessionDesc&) override { return false; }
  bool join(const std::string&) override { return false; }
  void close() override {}
  bool send(uint32_t, const void*, size_t, bool, uint32_t*) override { return true; }
  bool receive(znet::Packet* p) override {
    if (at >= n) return false;
    p->from = static_cast<uint32_t>(q[2 * at]);
    p->data.assign(static_cast<size_t>(q[2 * at + 1]), 0);
    if (p->data.size() >= 2) p->data[0] = 0x1f;
    ++at;
    return true;
  }
};
int g_system = 0;
}  // namespace

extern "C" int32_t probe_znet_poll(int32_t max, const int32_t* queue, int32_t n, int32_t* out, int32_t cap) {
  Script t;
  t.q = queue, t.n = n;
  znet::session() = znet::Session{};
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true;
  s.system_message = [](const znet::Packet&) { ++g_system; };
  g_system = 0, g_log = out + 2, g_logged = 0, g_cap = cap - 2;
  znet::on(0x1f, kHandlers[1]);
  out[0] = znet::poll(max);
  out[1] = g_system;
  const int32_t logged_n = g_logged;
  znet::session() = znet::Session{};
  return logged_n + 2;
}

// The system events' translation (piece 3): events of six words (kind,
// player type, id, team, name number, the id's DPCAPS_ISHOST) applied in
// order with the local player `local_id`; the log: (1, type, size, sender)
// for each local message, (2, to, guaranteed, size, type) for each send,
// then (3, count) and each player (id, is_host, team, short and long names'
// numbers), (4, count) and each team (id, name number).
#include "gamez/znetwork/znet_dplay.h"
namespace {
struct Caps : znet::Transport {
  std::vector<uint32_t> hosts;
  std::vector<int32_t>* log = nullptr;
  bool host(const znet::SessionDesc&) override { return false; }
  bool join(const std::string&) override { return false; }
  void close() override {}
  bool send(uint32_t to, const void* d, size_t n, bool g, uint32_t*) override {
    uint16_t type = 0;
    std::memcpy(&type, d, 2);
    log->insert(log->end(), {2, static_cast<int32_t>(to), g ? 1 : 0, static_cast<int32_t>(n), type});
    return true;
  }
  bool receive(znet::Packet*) override { return false; }
  bool is_host(uint32_t id) const override {
    for (uint32_t h : hosts)
      if (h == id) return true;
    return false;
  }
};
std::vector<int32_t>* g_events_log = nullptr;
void any(uint32_t from, const uint8_t* msg, size_t size) {
  uint16_t type, sz;
  std::memcpy(&type, msg, 2);
  std::memcpy(&sz, msg + 2, 2);
  (void)size;
  g_events_log->insert(g_events_log->end(), {1, type, sz, static_cast<int32_t>(from)});
}
int32_t number(const std::string& s) { return std::atoi(s.c_str() + (s.rfind("Player ", 0) == 0 ? 7 : 1)); }
}  // namespace

extern "C" int32_t probe_znet_events(const int32_t* ev, int32_t n, uint32_t local_id, uint32_t host_id, int32_t* out,
                                     int32_t cap) {
  std::vector<int32_t> log;
  Caps t;
  t.log = &log;
  for (int32_t i = 0; i < n; ++i)
    if (ev[6 * i + 5]) t.hosts.push_back(static_cast<uint32_t>(ev[6 * i + 2]));
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true, s.host_id = host_id;
  s.local = std::make_unique<znet::Player>();
  s.local->id = local_id;
  g_events_log = &log;
  for (uint16_t ty = 2; ty <= 0xe; ++ty) znet::on(ty, &any);
  for (int32_t i = 0; i < n; ++i) {
    znet::Packet p;
    znet::SystemEvent& e = p.sys;
    e.kind = static_cast<uint32_t>(ev[6 * i]), e.player_type = static_cast<uint32_t>(ev[6 * i + 1]);
    e.id = static_cast<uint32_t>(ev[6 * i + 2]), e.group = static_cast<uint32_t>(ev[6 * i + 3]);
    e.short_name = "P" + std::to_string(ev[6 * i + 4]);
    e.long_name = "Player " + std::to_string(ev[6 * i + 4]);
    znet::system_message(p);
  }
  log.insert(log.end(), {3, static_cast<int32_t>(znet::lists().players.size())});
  for (auto& r : znet::lists().players)
    log.insert(log.end(), {static_cast<int32_t>(r->id), r->is_host, static_cast<int32_t>(r->team), number(r->short_name),
                           number(r->long_name)});
  log.insert(log.end(), {4, static_cast<int32_t>(znet::lists().teams.size())});
  for (auto& tm : znet::lists().teams) log.insert(log.end(), {static_cast<int32_t>(tm->id), number(tm->name)});
  log.push_back(s.local ? s.local->is_host : -1);
  znet::clear_lists();
  znet::session() = znet::Session{};
  const int32_t m = static_cast<int32_t>(std::min<size_t>(log.size(), static_cast<size_t>(cap)));
  std::copy(log.begin(), log.begin() + m, out);
  return m;
}

// The teams' and data's calls (piece 3b) through a transport that answers as
// told and keeps each call: ops of four words (op, a, b, c); the log: for each
// transport call (10 + kind, its arguments), each answer (20, value), each
// 0xc network error (30, its text's length), then the lists as
// probe_znet_events gives them. Ops: 0 create_team(name a; the transport's
// id b), 1 add_to_team(a, b; ok c), 2 remove_from_team(a, b; c), 3
// destroy_team(a; c), 4 kick(a; c), 5 leave(; c), 6 set_player_data(a, size
// b; c), 7 get_player_data(a, buffer b; the data's size c), 8
// player_data_size(a; c), 9 set_team_data(a, b; c), 10 get_team_data(a, b;
// c), 11 the players enumerated (ids a.. a+b-1, names a+100..), 12 the teams
// enumerated (ids a.., names), 13 team c's players a.. a+b-1.
namespace {
struct Told : znet::Transport {
  std::vector<int32_t>* log = nullptr;
  int32_t ok = 1, id = 0, size = 0, first = 0, count = 0;
  void put(std::initializer_list<int32_t> v) { log->insert(log->end(), v); }
  bool host(const znet::SessionDesc&) override { return false; }
  bool join(const std::string&) override { return false; }
  void close() override {}
  bool send(uint32_t, const void*, size_t, bool, uint32_t*) override { return true; }
  bool receive(znet::Packet*) override { return false; }
  uint32_t create_group(const std::string& n) override {
    put({10, number(n)});
    return static_cast<uint32_t>(id);
  }
  bool add_to_group(uint32_t g, uint32_t p) override { put({11, int32_t(g), int32_t(p)}); return ok; }
  bool delete_from_group(uint32_t g, uint32_t p) override { put({12, int32_t(g), int32_t(p)}); return ok; }
  bool destroy_group(uint32_t g) override { put({13, int32_t(g)}); return ok; }
  bool destroy_player(uint32_t p) override { put({14, int32_t(p)}); return ok; }
  bool set_data(bool g, uint32_t who, const void*, uint32_t n) override {
    put({g ? 16 : 15, int32_t(who), int32_t(n)});
    return ok;
  }
  bool get_data(bool g, uint32_t who, void* d, uint32_t* n) override {
    put({g ? 18 : 17, int32_t(who), d ? 1 : 0, int32_t(*n)});
    const uint32_t have = *n;
    *n = static_cast<uint32_t>(size);
    return d && have >= static_cast<uint32_t>(size);
  }
  bool players(std::vector<Member>* out) override {
    out->clear();
    for (int32_t i = 0; i < count; ++i) out->push_back(Member{uint32_t(first + i), "P" + std::to_string(first + i + 100), "Player " + std::to_string(first + i + 100)});
    return true;
  }
  bool groups(std::vector<Member>* out) override {
    out->clear();
    for (int32_t i = 0; i < count; ++i) out->push_back(Member{uint32_t(first + i), "P" + std::to_string(first + i), "Player " + std::to_string(first + i + 50)});
    return true;
  }
  int32_t members_team = 0;
  bool group_players(uint32_t g, std::vector<uint32_t>* out) override {
    out->clear();
    if (int32_t(g) == members_team)
      for (int32_t i = 0; i < count; ++i) out->push_back(uint32_t(first + i));
    return true;
  }
};
std::vector<int32_t>* g_calls_log = nullptr;
void on_error(uint32_t, const uint8_t* m, size_t size) {
  (void)m;
  g_calls_log->insert(g_calls_log->end(), {30, static_cast<int32_t>(size) - 5});
}
}  // namespace

extern "C" int32_t probe_znet_calls(const int32_t* ops, int32_t n, int32_t* out, int32_t cap) {
  std::vector<int32_t> log;
  Told t;
  t.log = &log;
  g_calls_log = &log;
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true;
  s.local = std::make_unique<znet::Player>();
  s.local->id = 1;
  znet::on(0xc, &on_error);
  uint8_t buf[64] = {};
  const int brk = std::getenv("MW3_PROBE_BREAK") ? std::atoi(std::getenv("MW3_PROBE_BREAK")) : 0;
  for (int32_t i = 0; i < n; ++i) {
    const int32_t op = ops[4 * i], a = ops[4 * i + 1], b = ops[4 * i + 2], c = ops[4 * i + 3];
    t.ok = c;
    int32_t r = 0;
    switch (op) {
      case 0: t.id = b; r = static_cast<int32_t>(znet::create_team(("P" + std::to_string(a)).c_str())); break;
      case 1: r = znet::add_to_team(a, b); break;
      case 2: {
        const znet::PlayerRecord* rec = znet::player(b);
        const uint32_t was = rec ? rec->team : 0;
        r = znet::remove_from_team(a, b);
        if (brk == 2 && r && znet::player(b)) znet::player(b)->team = was;  // the break: the team kept
        break;
      }
      case 3: r = znet::destroy_team(a); break;
      case 4: r = znet::kick(a); break;
      case 5: r = znet::leave(); break;
      case 6: r = znet::set_player_data(a, buf, b); break;
      case 7: { t.size = c; uint32_t sz = b; r = znet::get_player_data(a, b ? buf : nullptr, &sz); log.insert(log.end(), {21, int32_t(sz)}); break; }
      case 8: t.size = c; r = static_cast<int32_t>(znet::player_data_size(a)); break;
      case 9: r = znet::set_team_data(a, buf, b); break;
      case 10: { t.size = c; uint32_t sz = b; r = znet::get_team_data(a, b ? buf : nullptr, &sz); log.insert(log.end(), {21, int32_t(sz)}); break; }
      case 11: t.first = a, t.count = b; r = znet::enum_players(); break;
      case 12:
        t.first = a, t.count = b;
        r = znet::enum_teams();
        if (brk == 1)  // the break: the short name taken
          for (auto& tm : znet::lists().teams) tm->name = "P" + std::to_string(tm->id);
        break;
      case 13: t.first = a, t.count = b, t.members_team = c; r = znet::enum_team_players(); break;
      default: break;
    }
    log.insert(log.end(), {20, r});
  }
  log.insert(log.end(), {3, static_cast<int32_t>(znet::lists().players.size())});
  for (auto& rec : znet::lists().players)
    log.insert(log.end(), {static_cast<int32_t>(rec->id), rec->is_host, static_cast<int32_t>(rec->team),
                           number(rec->short_name), number(rec->long_name)});
  log.insert(log.end(), {4, static_cast<int32_t>(znet::lists().teams.size())});
  for (auto& tm : znet::lists().teams) log.insert(log.end(), {static_cast<int32_t>(tm->id), number(tm->name)});
  znet::clear_lists();
  znet::session() = znet::Session{};
  const int32_t m = static_cast<int32_t>(std::min<size_t>(log.size(), static_cast<size_t>(cap)));
  std::copy(log.begin(), log.begin() + m, out);
  return m;
}

// The local player made (0x005886c0, piece 4): `others` players already in
// the session (ids 2.., names P102..), the transport's new id and whether
// its caps say host; the log: each send (2, to, guaranteed, size, type), the
// answer (20, id), the local player (21, id, is_host), the host id (22, id),
// then the lists as probe_znet_events gives them.
namespace {
struct Maker : znet::Transport {
  std::vector<int32_t>* log = nullptr;
  int32_t others = 0;
  uint32_t new_id = 0;
  bool host_caps = false;
  bool host(const znet::SessionDesc&) override { return false; }
  bool join(const std::string&) override { return false; }
  void close() override {}
  bool send(uint32_t to, const void* d, size_t n, bool g, uint32_t*) override {
    uint16_t type = 0;
    std::memcpy(&type, d, 2);
    log->insert(log->end(), {2, static_cast<int32_t>(to), g ? 1 : 0, static_cast<int32_t>(n), type});
    return true;
  }
  bool receive(znet::Packet*) override { return false; }
  uint32_t create_player(const std::string& s, const std::string&) override {
    log->insert(log->end(), {10, s == "noname" ? -1 : number(s)});
    return new_id;
  }
  bool is_host(uint32_t id) const override { return host_caps && id == new_id; }
  bool players(std::vector<Member>* out) override {
    out->clear();
    for (int32_t i = 0; i < others; ++i)
      out->push_back(Member{uint32_t(2 + i), "P" + std::to_string(102 + i), "Player " + std::to_string(102 + i)});
    return true;
  }
};
}  // namespace

extern "C" int32_t probe_znet_create(int32_t others, uint32_t new_id, int32_t host_caps, int32_t name, int32_t* out,
                                     int32_t cap) {
  std::vector<int32_t> log;
  Maker t;
  t.log = &log, t.others = others, t.new_id = new_id, t.host_caps = host_caps != 0;
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true;
  const std::string n = name >= 0 ? "P" + std::to_string(name) : std::string();
  const int32_t id = static_cast<int32_t>(znet::create_player(name >= 0 ? n.c_str() : nullptr));
  log.insert(log.end(), {20, id, 21, s.local ? static_cast<int32_t>(s.local->id) : -1, s.local ? s.local->is_host : -1,
                         22, static_cast<int32_t>(s.host_id)});
  log.insert(log.end(), {3, static_cast<int32_t>(znet::lists().players.size())});
  for (auto& r : znet::lists().players)
    log.insert(log.end(), {static_cast<int32_t>(r->id), r->is_host, static_cast<int32_t>(r->team),
                           r->short_name == "noname" ? -1 : number(r->short_name),
                           r->long_name == "noname" ? -1 : number(r->long_name)});
  znet::clear_lists();
  znet::session() = znet::Session{};
  const int32_t m = static_cast<int32_t>(std::min<size_t>(log.size(), static_cast<size_t>(cap)));
  std::copy(log.begin(), log.begin() + m, out);
  return m;
}
