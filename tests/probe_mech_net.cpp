// The mission's network side (multiplayer-port.md piece 5a) for
// tests/differential/test_mech_net.py: a script of starts, ends, messages
// routed and players leaving, each call out logged as the test logs the
// exe's.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zeffect/zeff_anim_save.h"
#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zutil/ztimer.h"
#include "mech3/mech_net.h"

namespace {
using mech3::NetMech;
std::vector<int32_t>* g_log;
uint32_t g_clock;
int32_t g_create_ok, g_slot_ok, g_player_mech, g_ff, g_break, g_cleared;

void log(std::initializer_list<int32_t> v) { g_log->insert(g_log->end(), v); }
int32_t bits(float f) {
  int32_t b;
  std::memcpy(&b, &f, 4);
  return b;
}
uint16_t type_of(const uint8_t* msg) {
  uint16_t t;
  std::memcpy(&t, msg, 2);
  return t;
}
int32_t owner(const NetMech* m) { return m ? static_cast<int32_t>(m->owner) : -1; }

struct Data : znet::Transport {
  bool host(const znet::SessionDesc&) override { return false; }
  bool join(const std::string&) override { return false; }
  void close() override {}
  bool send(uint32_t, const void*, size_t, bool, uint32_t*) override { return true; }
  bool receive(znet::Packet*) override { return false; }
  bool set_data(bool group, uint32_t id, const void* data, uint32_t size) override {
    if (!group) log({71, static_cast<int32_t>(id), data ? 1 : 0, static_cast<int32_t>(size)});
    return true;
  }
};

int crater_veto(const zwep::CraterOrder*) { return 1; }
void anim_sent(zeff::AnimLogRecord*) {}

// Each routed handler logs its code, the mech (or the message's type for
// the ones given only the message) and, for the chat, the sender.
template <int Code, bool MsgOnly = false, bool From = false>
void route(NetMech* m, uint32_t from, const uint8_t* msg, size_t) {
  if (MsgOnly) return log({Code, type_of(msg)});
  log({Code, owner(m)});
  if (From) log({static_cast<int32_t>(from)});
}

void wire() {
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  c.state = route<1>, c.ping = route<2>, c.clock = route<3, true>, c.respawn = route<4>;
  c.kill = route<5, true>, c.scores = route<6>, c.rules = route<7>, c.posture = route<8>;
  c.knockback = route<9>, c.effect = route<10>, c.drop = route<11>, c.chat = route<12, false, true>;
  c.part_off = route<13>, c.crater = route<14, true>, c.send_done = route<16, true>;
  c.create = [](NetMech*, uint32_t from, const uint8_t*, size_t) {  // 0x0046d450's stand-in
    log({15, static_cast<int32_t>(from)});
    if (!g_create_ok) return;
    auto* m = new NetMech();
    m->owner = g_break == 1 ? from + 100 : from;  // BREAK=1: made for someone else
    mech3::net_game().mechs.push_back(m);
  };
  c.set_position = [](NetMech& m, const float p[3]) { log({41, owner(&m), bits(p[0]), bits(p[1]), bits(p[2])}); };
  c.reckon = [](NetMech& m, float dt) { log({42, owner(&m), bits(dt)}); };
  c.now_ms = [] { return ++g_clock; };
  c.hud_message = [](float s, int32_t id, const char* name) {
    log({50, bits(s), id, name ? std::atoi(name + 1) : -1});
  };
  c.forget = [](NetMech& m) { log({51, owner(&m)}); };
  c.free = [](NetMech* m) {  // the destructor's list removal, without the mech's own
    log({40, owner(m)});
    mech3::net_game().mechs.remove(m);
    delete m;
  };
  c.lobby_left = [](uint32_t p) {
    log({52, static_cast<int32_t>(p)});
    if (g_slot_ok) log({53, 1});
  };
  c.crater_veto = crater_veto;
  c.anim_sent = anim_sent;
  c.game_clear = [] { g_cleared = 1; };
  c.player_mech_end = [] { if (g_player_mech) log({70}); };
  c.ff_stop = [] { if (g_ff) log({72}); };
  c.config_free = [](mech3::Config* cfg) { log({73, static_cast<int32_t>(reinterpret_cast<uintptr_t>(cfg))}); };
}

int32_t handler_code(const znet::HandlerEntry& e) {
  if (e.fn == mech3::net_route) return 1;
  if (e.fn == mech3::net_player_left) return 2;
  return e.type == 1 ? 3 : 4;  // mech_net.cpp's own two: 0x004d72a0, 0x004709d0
}
}  // namespace

// ops: dt bits, local id, then 7 ints each: [kind, a..f]. Kinds: 1 a
// message (from, type, the create succeeds, the position's three floats),
// 2 a player leaving (from, the lobby slot is there), 3 the start (names
// missing), 4 the end (the pilot has a mech, force feedback, configs, a
// mask of the null ones).
extern "C" int32_t probe_mech_net(const int32_t* ops, int32_t n, int32_t* out, int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  g_clock = 1000;
  g_break = 0;
  if (const char* b = std::getenv("BREAK")) g_break = std::atoi(b);
  wire();
  mech3::NetGame& g = mech3::net_game();
  g = mech3::NetGame{};
  float dt;
  std::memcpy(&dt, &ops[0], 4);
  ztimer::state().dt = dt;
  Data t;
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true;
  for (uint32_t id = 1; id <= 8; ++id) {
    auto r = std::make_unique<znet::PlayerRecord>();
    r->id = id;
    r->short_name = "P" + std::to_string(id);
    znet::lists().players.push_back(std::move(r));
  }
  s.local = std::make_unique<znet::Player>();
  s.local->id = static_cast<uint32_t>(g_break == 2 ? ops[1] % 8 + 1 : ops[1]);  // BREAK=2: another
  zeff::anim_log().net = nullptr, zeff::anim_log().mask = 0;
  zdec::craters().veto = nullptr;
  for (int32_t i = 2; i + 7 <= n; i += 7) {
    const int32_t* o = ops + i;
    if (o[0] == 1) {
      g_create_ok = o[3];
      uint8_t msg[0x40] = {};
      const uint16_t type = static_cast<uint16_t>(o[2]), size = sizeof msg;
      std::memcpy(msg, &type, 2);
      std::memcpy(msg + 2, &size, 2);
      std::memcpy(msg + 4, &o[4], 12);
      mech3::net_route(static_cast<uint32_t>(o[1]), msg, sizeof msg);
    } else if (o[0] == 2) {
      g_slot_ok = o[2];
      const uint8_t msg[8] = {6, 0, 8, 0};
      mech3::net_player_left(static_cast<uint32_t>(o[1]), msg, sizeof msg);
    } else if (o[0] == 3) {
      for (int32_t k = 0; k < o[1]; ++k) g.missing.push_back("m");
      const bool was = g.running;
      g_cleared = 0;
      mech3::net_start();
      if (!was) {
        for (const znet::HandlerEntry& e : s.handlers) log({60, e.type, handler_code(e), e.arg});
        if (g_cleared) log({80, 0, 0, 0});
        log({79, bits(g.reckon_gain), bits(g.reckon_damp)});
      }
      log({75, static_cast<int32_t>(g.missing.size())});
    } else if (o[0] == 4) {
      g_player_mech = o[1], g_ff = o[2];
      for (int32_t k = 0; k < o[3]; ++k)
        g.configs.push_back((o[4] >> k & 1) ? nullptr : reinterpret_cast<mech3::Config*>(static_cast<uintptr_t>(k + 1)));
      for (int32_t k = 0; k < o[5]; ++k) g.missing.push_back("m");
      mech3::net_end();
      log({62, static_cast<int32_t>(s.handlers.size()), 74, static_cast<int32_t>(g.configs.size()), 75,
           static_cast<int32_t>(g.missing.size())});
      g.configs.clear();
    }
    if (o[0] >= 3)
      log({76, zdec::craters().veto == crater_veto ? 1 : zdec::craters().veto ? 2 : 0, 77, g.running ? 1 : 0, 78,
           zeff::anim_log().net == anim_sent ? 1 : zeff::anim_log().net ? 2 : 0,
           static_cast<int32_t>(zeff::anim_log().mask)});
    log({99, static_cast<int32_t>(g.mechs.size())});
    for (const NetMech* m : g.mechs) log({owner(m), static_cast<int32_t>(m->heard)});
  }
  for (NetMech* m : g.mechs) delete m;
  g = mech3::NetGame{};
  for (const znet::HandlerEntry& e : std::vector<znet::HandlerEntry>(s.handlers)) znet::off(e.type, e.fn);
  znet::clear_lists();
  znet::session() = znet::Session{};
  mech3::net_calls() = mech3::NetCalls{};
  zeff::anim_log().net = nullptr;
  zdec::craters().veto = nullptr;
  const int32_t m = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + m, out);
  return m;
}

// The launch (0x0046da80) for test_mech_net_launch.py. in: the local id,
// the roster, the skin, the team, the start slot, unlimited ammo, the
// outside view, the spawn point found, its position (3) and heading as
// bits, the force feedback; then the packed loadout.
namespace {
int32_t g_team, g_start, g_found, g_ff2;
float g_spawn[4];
}  // namespace

extern "C" int32_t probe_mech_net_launch(const int32_t* in, const uint32_t* blob, int32_t words, int32_t* out,
                                         int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  g_team = in[3], g_start = in[4], g_found = in[7], g_ff2 = in[12];
  std::memcpy(g_spawn, &in[8], 16);
  c.dfn_load = [](int32_t r) { log({1, r}); };
  c.team_of = [](uint32_t p) { log({2, static_cast<int32_t>(p)}); return static_cast<uint32_t>(g_team); };
  c.unlimited = [](mech3::Mech&, bool on) { log({3, on ? 1 : 0}); };
  c.scores_reset = [] { log({5}); };
  c.view_outside = [] { log({6, 1}); };
  c.start_of = [](uint32_t p) { log({7, static_cast<int32_t>(p)}); return g_start; };
  c.spawn_point = [](const char* name, float pos[3], float* yaw) {
    log({8, std::atoi(name + 7)});
    if (!g_found) return false;
    std::memcpy(pos, g_spawn, 12), *yaw = g_spawn[3];
    return true;
  };
  c.place = [](mech3::Mech&, const float p[3], float yaw) { log({9, bits(p[0]), bits(p[1]), bits(p[2]), bits(yaw)}); };
  c.ff_spawn = [] { if (g_ff2) log({10, 1}); };
  Data t;
  struct Recorder : Data {
    bool set_data(bool group, uint32_t id, const void* data, uint32_t size) override {
      if (group) return true;
      log({4, static_cast<int32_t>(id), static_cast<int32_t>(size)});
      const auto* w = static_cast<const uint8_t*>(data);
      for (uint32_t k = 0; k + 4 <= size; k += 4) {
        int32_t v;
        std::memcpy(&v, w + k, 4);
        g_log->push_back(v);
      }
      return true;
    }
  } rec;
  znet::session() = znet::Session{};
  znet::Session& s = znet::session();
  s.transport = &rec, s.initialised = true, s.open = true;
  s.local = std::make_unique<znet::Player>();
  s.local->id = static_cast<uint32_t>(in[0]);
  mech3::NetGame& g = mech3::net_game();
  g = mech3::NetGame{};
  g.x9c = 1, g.xa0 = 7, g.xa4 = 1;
  auto* data = static_cast<mech3::MechData*>(::operator new(sizeof(mech3::MechData)));
  std::memset(static_cast<void*>(data), 0, sizeof(mech3::MechData));
  data->roster = in[1];
  auto* m = new mech3::Mech();
  m->pilot = data;
  m->net_acked = 0, m->net_sent = 5;
  const std::vector<uint32_t> packed(blob, blob + words);
  const bool ok = mech3::net_launch_player(*m, packed, in[2], in[5] != 0, in[6] != 0);
  log({11, ok ? 1 : 0, 12, static_cast<int32_t>(m->team), static_cast<int32_t>(m->allies),
       static_cast<int32_t>(m->enemies), 13, m->net_acked, static_cast<int32_t>(m->net_sent), 14, g.x9c,
       static_cast<int32_t>(g.xa0), g.xa4});
  m->pilot = nullptr;
  delete m;
  ::operator delete(data);
  g = mech3::NetGame{};
  znet::session() = znet::Session{};
  c = mech3::NetCalls{};
  (void)t;
  const int32_t n = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + n, out);
  return n;
}
