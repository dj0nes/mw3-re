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
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"
#include "mech3/mech_net.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_skin.h"
#include "mech3/mech_config.h"

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
      for (int32_t k = 0; k < o[1]; ++k) g.missing.push_back(0);
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
      for (int32_t k = 0; k < o[5]; ++k) g.missing.push_back(0);
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
  c.dfn = [](int32_t r) -> const mech3::MechDfn* { log({1, r}); return nullptr; };
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
  g.last_second = 1, g.next_state = 7, g.warned = 1;
  auto* data = static_cast<mech3::MechData*>(::operator new(sizeof(mech3::MechData)));
  std::memset(static_cast<void*>(data), 0, sizeof(mech3::MechData));
  data->roster = in[1];
  auto* m = new mech3::Mech();
  m->pilot = data;
  m->net_acked = 0, m->net_sent = 5;
  const std::vector<uint32_t> packed(blob, blob + words);
  const bool ok = mech3::net_launch_player(*m, packed, in[2], in[5] != 0, in[6] != 0);
  log({11, ok ? 1 : 0, 12, static_cast<int32_t>(m->team), static_cast<int32_t>(m->allies),
       static_cast<int32_t>(m->enemies), 13, m->net_acked, static_cast<int32_t>(m->net_sent), 14, g.last_second,
       bits(g.next_state), g.warned});
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

// The creation (0x0046d450) for test_mech_net_create.py. in: the sender,
// whether it has a name, the record's size as asked, whether the get
// succeeds, the dfn found, its skin count, the unpack's answer, Validate's,
// the type's data loaded, host; then the record's words.
namespace {
int32_t g_c[16];
std::vector<uint8_t> g_rec;
mech3::MechDfn* g_dfn;
mech3::SkinSet* g_skins;
mech3::Config* g_cfg;
int32_t name_number(const char* s) {
  if (!s) return -1;
  if (std::strncmp(s, "(null)", 6) == 0) return -2;
  return std::atoi(s + 1);
}
struct Records : Data {
  bool get_data(bool group, uint32_t, void* data, uint32_t* size) override {
    if (group) return false;
    if (!data) {
      *size = static_cast<uint32_t>(g_c[2]);
      return false;
    }
    if (!g_c[3]) return false;
    std::memcpy(data, g_rec.data(), std::min<size_t>(*size, g_rec.size()));
    return true;
  }
};
}  // namespace

extern "C" int32_t probe_mech_net_create(const int32_t* in, const uint32_t* rec, int32_t words, int32_t* out,
                                         int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  std::memcpy(g_c, in, sizeof g_c);
  g_rec.assign(reinterpret_cast<const uint8_t*>(rec), reinterpret_cast<const uint8_t*>(rec) + 4 * words);
  if (g_rec.size() < static_cast<size_t>(in[2])) g_rec.resize(static_cast<size_t>(in[2]), 0);
  g_rec.resize(static_cast<size_t>(in[2]));
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  if (!g_dfn) {
    g_dfn = static_cast<mech3::MechDfn*>(::operator new(sizeof(mech3::MechDfn)));
    std::memset(static_cast<void*>(g_dfn), 0, sizeof(mech3::MechDfn));
    g_skins = new mech3::SkinSet(0);
  }
  g_skins->skins.clear();
  for (int32_t i = 0; i < g_c[5]; ++i) g_skins->skins.push_back(reinterpret_cast<mech3::Skin*>(uintptr_t(0x100 + i)));
  g_dfn->skins = g_c[5] >= 0 ? g_skins : nullptr;
  c.dfn = [](int32_t r) -> const mech3::MechDfn* {
    log({1, r});
    return g_c[4] ? g_dfn : nullptr;
  };
  c.team_of = [](uint32_t p) {
    log({2, static_cast<int32_t>(p)});
    return static_cast<uint32_t>(g_c[11]);
  };
  c.unpack = [](const uint32_t* p, size_t) -> mech3::Config* {
    log({30, static_cast<int32_t>(p[0])});
    if (!g_c[6]) return nullptr;
    g_cfg = new mech3::Config(-1, "remote config");
    return g_cfg;
  };
  c.valid = [](const mech3::Config&) {
    log({31});
    return g_c[7] != 0;
  };
  c.config_free = [](mech3::Config* cfg) {
    log({22, cfg == g_cfg ? 1 : 0});
    delete cfg;
  };
  c.data = [](int32_t r) -> mech3::MechData* {
    log({24, r, 1});
    return g_c[8] ? reinterpret_cast<mech3::MechData*>(uintptr_t(0x200)) : nullptr;
  };
  c.make = [](uint32_t owner, mech3::MechData*, mech3::NetControls* ctl, mech3::Config* cfg,
              const char* name) -> NetMech* {
    log({25, static_cast<int32_t>(owner), cfg == g_cfg ? 1 : 0, name_number(name)});
    auto* m = new NetMech();
    m->owner = owner;
    delete ctl;
    mech3::net_game().mechs.push_back(m);
    return m;
  };
  c.skin = [](NetMech&, mech3::Handle s) { log({26, static_cast<int32_t>(reinterpret_cast<uintptr_t>(s))}); };
  c.sensor_range = [](mech3::Mech&) { log({27}); };
  c.host_replay = [] { log({28, 29, 1}); };
  Records t;
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true;
  s.local = std::make_unique<znet::Player>();
  s.local->id = 9;
  s.host_id = g_c[9] ? 9u : 1u;
  if (in[1]) {
    auto r = std::make_unique<znet::PlayerRecord>();
    r->id = static_cast<uint32_t>(in[0]);
    r->short_name = "P" + std::to_string(in[0]);
    znet::lists().players.push_back(std::move(r));
  }
  mech3::NetGame& g = mech3::net_game();
  g = mech3::NetGame{};
  g_cfg = nullptr;
  mech3::net_mech_create(static_cast<uint32_t>(in[0]));
  for (mech3::Config* cfg : g.configs) log({23, name_number(cfg->name)});
  for (uint32_t p : g.missing) log({21, static_cast<int32_t>(p)});
  for (NetMech* m : g.mechs)
    log({12, static_cast<int32_t>(m->team), static_cast<int32_t>(m->allies), static_cast<int32_t>(m->enemies),
         static_cast<int32_t>(m->latency)});
  log({99, static_cast<int32_t>(g.mechs.size())});
  for (NetMech* m : g.mechs) delete m;
  for (mech3::Config* cfg : g.configs) delete cfg;
  g = mech3::NetGame{};
  znet::clear_lists();
  znet::session() = znet::Session{};
  c = mech3::NetCalls{};
  const int32_t n = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + n, out);
  return n;
}

// The sends (0x0046f160, 0x0046ef70) for test_mech_net_send.py. The input
// layout is the test's (In below); the log: each send (1, to, guaranteed,
// size, its words), the HUD line (2, id), the rules check (3), the game's
// end (4), a piece-7 kind (5, kind); then the state after.
#include "mech3/mech_parts.h"
#include "mech3/weapon.h"
namespace {
enum In {
  kKind, kArg, kVel, kQw = kVel + 3, kQy, kTurnQ, kThrottle = kTurnQ + 4, kTurn, kJump, kTorsoAt, kAim = kTorsoAt + 3,
  kClear = kAim + 3, kActive, kAnimState, kAction, kLimp, kXbc, kPower, kMasc, kAms, kNarc, kCrouched, kTarget,
  kDamaged, kShots, kShot, kTorsoInternal = kShot + 15, kTorsoArmor, kSecArmor, kSideInternal = kSecArmor + 5,
  kParts = kSideInternal + 2, kPos = kParts + 10, kRemotes = kPos + 3, kOwners, kEcho = kOwners + 4,
  kHeard = kEcho + 4, kPingTurn = kHeard + 4, kPingCounter, kAcked, kNow, kHost, kPlayers, kClockNext, kEndTime,
  kEndState, kGameTime, kClockLeft, kClockOn, kNextState, kNextPing, kResends, kRateSetting, kWarned, kLastSecond,
  kHasGame, kWeaponLocked, kWeaponSlot, kWeaponTarget, kState, kInCount
};
const int32_t* g_in;
float fbits(int32_t b) {
  float f;
  std::memcpy(&f, &b, 4);
  return f;
}
uint32_t g_send_id;
float g_clock_next, g_end_time;
int32_t g_end_state;
mech3::NetGameFields g_fields;
struct Sends : Data {
  bool send(uint32_t to, const void* d, size_t n, bool guaranteed, uint32_t* id) override {
    log({1, static_cast<int32_t>(to), guaranteed ? 1 : 0, static_cast<int32_t>(n)});
    const auto* b = static_cast<const uint8_t*>(d);
    for (size_t k = 0; k < n; k += 4) {
      int32_t v = 0;
      std::memcpy(&v, b + k, std::min<size_t>(4, n - k));
      g_log->push_back(v);
    }
    if (id) *id = ++g_send_id;
    return true;
  }
};
}  // namespace

extern "C" int32_t probe_mech_net_send(const int32_t* in, int32_t* out, int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  g_in = in;
  g_send_id = 100;
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  c.position = [](mech3::Mech&, float o[3]) {
    for (int i = 0; i < 3; ++i) o[i] = fbits(g_in[kPos + i]);
    return true;
  };
  c.torso_position = [](mech3::Mech&, float o[3]) {
    for (int i = 0; i < 3; ++i) o[i] = fbits(g_in[kTorsoAt + i]);
    return true;
  };
  c.target_player = [](mech3::Mech&) { return static_cast<uint32_t>(g_in[kTarget]); };
  c.now_ms = [] { return static_cast<uint32_t>(g_in[kNow]); };
  c.hud_message = [](float, int32_t id, const char*) { log({2, id}); };
  c.game_over = [] { log({4}); };
  c.clock = [](NetMech*, uint32_t, const uint8_t*, size_t) { log({7}); };  // 0x00470740
  g_clock_next = fbits(in[kClockNext]), g_end_time = fbits(in[kEndTime]), g_end_state = in[kEndState];
  g_fields = mech3::NetGameFields{0, in[kPlayers], &g_clock_next, &g_end_time, &g_end_state};
  c.game = []() -> mech3::NetGameFields* { return g_in[kHasGame] ? &g_fields : nullptr; };
  c.game_time = [] { return fbits(g_in[kGameTime]); };
  c.clock_left = [] { return fbits(g_in[kClockLeft]); };
  c.clock_on = [] { return g_in[kClockOn] != 0; };
  Sends t;
  znet::session() = znet::Session{};
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true, s.async = true;
  s.local = std::make_unique<znet::Player>();
  s.local->id = 9;
  s.host_id = in[kHost] ? 9u : 1u;
  mech3::NetGame& g = mech3::net_game();
  g = mech3::NetGame{};
  g.running = true;
  g.ping_turn = static_cast<uint32_t>(in[kPingTurn]), g.ping_counter = static_cast<uint32_t>(in[kPingCounter]);
  g.acked_time = static_cast<uint32_t>(in[kAcked]);
  g.next_state = fbits(in[kNextState]), g.next_ping = fbits(in[kNextPing]), g.resends = in[kResends];
  g.rate_setting = static_cast<uint32_t>(in[kRateSetting]);
  g.warned = static_cast<uint8_t>(in[kWarned]), g.last_second = static_cast<uint8_t>(in[kLastSecond]);
  std::vector<NetMech*> remotes;
  for (int32_t k = 0; k < in[kRemotes]; ++k) {
    auto* r = new NetMech();
    r->owner = static_cast<uint32_t>(in[kOwners + k]);
    r->ping_in[1] = static_cast<uint32_t>(in[kEcho + k]);
    r->ping_heard = static_cast<uint32_t>(in[kHeard + k]);
    g.mechs.push_back(r);
    remotes.push_back(r);
  }
  auto* m = new mech3::Mech();
  m->velocity = {fbits(in[kVel]), fbits(in[kVel + 1]), fbits(in[kVel + 2])};
  m->orientation[0] = fbits(in[kQw]), m->orientation[2] = fbits(in[kQy]);
  mech3::Torso torso;
  for (int i = 0; i < 4; ++i) torso.turn_q[i] = fbits(in[kTurnQ + i]);
  torso.internal = fbits(in[kTorsoInternal]), torso.armor = fbits(in[kTorsoArmor]);
  for (int i = 0; i < 5; ++i) torso.sec_armor[1 + i] = fbits(in[kSecArmor + i]);
  torso.side_internal[0] = fbits(in[kSideInternal]), torso.side_internal[1] = fbits(in[kSideInternal + 1]);
  m->torso = &torso;
  mech3::Arm arm0, arm1;
  mech3::Leg leg0, leg1;
  mech3::Head head;
  mech3::MechPart* parts[5] = {&arm0, &arm1, &leg0, &leg1, &head};
  for (int i = 0; i < 5; ++i) parts[i]->internal = fbits(in[kParts + 2 * i]), parts[i]->armor = fbits(in[kParts + 2 * i + 1]);
  m->arms[0] = &arm0, m->arms[1] = &arm1, m->legs[0] = &leg0, m->legs[1] = &leg1, m->head = &head;
  mech3::NetControls ctl;
  ctl.throttle_target = fbits(in[kThrottle]), ctl.turn = fbits(in[kTurn]), ctl.jump = fbits(in[kJump]);
  m->controls = &ctl;
  mech3::WeaponManager wm(nullptr);
  wm.aim = {fbits(in[kAim]), fbits(in[kAim + 1]), fbits(in[kAim + 2])};
  wm.state = in[kState];
  m->ammo = &wm;
  m->collider.clear = in[kClear];
  m->active = in[kActive] != 0;
  m->anim_state = in[kAnimState];
  if (in[kAction] >= 0) m->actions.push_back(in[kAction]);
  m->limp = in[kLimp];
  m->xbc = static_cast<uint8_t>(in[kXbc]);
  m->power_down_pending = (in[kPower] & 1) != 0, m->powered_down = (in[kPower] & 2) != 0;
  m->masc_time = fbits(in[kMasc]), m->narc_time = fbits(in[kNarc]);
  mech3::Ams ams(0, 0, 0);
  ams.on = in[kAms] == 2;
  m->ams = in[kAms] ? &ams : nullptr;
  m->crouched = in[kCrouched] != 0;
  m->net_damaged = static_cast<uint32_t>(in[kDamaged]);
  for (int32_t k = 0; k < in[kShots]; ++k) {
    mech3::NetShot sh;
    sh.slot = in[kShot + 3 * k], sh.trigger = in[kShot + 3 * k + 1], sh.target = static_cast<uint32_t>(in[kShot + 3 * k + 2]);
    m->net_shots.push_back(sh);
  }
  // The sender reads only the slot, the lock and its target: raw storage.
  alignas(mech3::Weapon) static unsigned char weapon_raw[sizeof(mech3::Weapon)];
  std::memset(weapon_raw, 0, sizeof weapon_raw);
  mech3::Weapon& weapon = *reinterpret_cast<mech3::Weapon*>(weapon_raw);
  weapon.slot = in[kWeaponSlot];
  weapon.locked = in[kWeaponLocked] != 0;
  const int32_t wt = in[kWeaponTarget];  // 0 none, 1..4 a remote mech, 5 a plain object
  static mech3::Mech plain;
  weapon.projectile_target = wt == 0 ? nullptr : wt <= static_cast<int32_t>(remotes.size()) ? remotes[wt - 1] : &plain;
  c.kill = [](NetMech*, uint32_t, const uint8_t*, size_t) { log({6}); };  // 0x0046eba0, the receive side's test
  mech3::Arm part;  // 0x21's: a part of the mech kWeaponTarget names, its id kArg (none below 0)
  part.mech = wt == 0 ? nullptr : static_cast<mech3::Mech*>(weapon.projectile_target);
  part.id = in[kArg] < 0 ? -1 : in[kArg];
  static int node_stub;
  part.node = in[kArg] < 0 ? nullptr : &node_stub;
  int32_t ret = 0;
  if (in[kKind] == 100) {
    mech3::net_mech_tick(*m);
  } else {
    intptr_t arg = in[kArg];
    if (in[kKind] == 0x16) arg = reinterpret_cast<intptr_t>(&weapon);
    if (in[kKind] == 0x13) arg = reinterpret_cast<intptr_t>(weapon.projectile_target);
    if (in[kKind] == 0x21) arg = reinterpret_cast<intptr_t>(&part);
    if (in[kKind] == 0x11) arg = reinterpret_cast<intptr_t>(&g_fields);  // the game
    ret = mech3::net_send_event(*m, in[kKind], arg);
  }
  part.mech = nullptr, part.node = nullptr;
  log({90, ret, static_cast<int32_t>(m->net_damaged), static_cast<int32_t>(m->net_shots.size())});
  for (const mech3::NetShot& sh : m->net_shots) log({sh.slot, sh.x04, sh.trigger, static_cast<int32_t>(sh.target)});
  log({91, static_cast<int32_t>(m->net_sent), m->net_acked, bits(g.next_state), bits(g.next_ping), g.resends, g.warned,
       g.last_second, static_cast<int32_t>(g.ping_turn), static_cast<int32_t>(g.ping_counter), g_end_state,
       bits(g_clock_next)});
  for (NetMech* r : remotes) log({92, static_cast<int32_t>(r->ping_sent), static_cast<int32_t>(r->ping_id)});
  m->torso = nullptr, m->controls = nullptr, m->ammo = nullptr, m->ams = nullptr;
  m->arms[0] = m->arms[1] = nullptr, m->legs[0] = m->legs[1] = nullptr, m->head = nullptr;
  delete m;
  for (NetMech* r : remotes) delete r;
  g = mech3::NetGame{};
  znet::session() = znet::Session{};
  c = mech3::NetCalls{};
  const int32_t n = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + n, out);
  return n;
}
extern "C" int32_t probe_mech_net_in_count() { return kInCount; }

// The receive side (0x0046e4d0, 0x0046ed70, 0x00470a00, 0x004709d0) for
// test_mech_net_apply.py; the input layout is the test's (Rx below).
namespace {
enum Rx {
  rMode, rLen, rMsg, rFlagsIn = rMsg + 64, rPosIn, rVelIn = rPosIn + 3, rHeadingQ = rVelIn + 3, rAimQ = rHeadingQ + 4,
  rHeard = rAimQ + 4, rLatency, rOrient, rWorld = rOrient + 4, rTorsoAt = rWorld + 3, rXbc = rTorsoAt + 3, rPower,
  rMasc, rNarc, rCrouched, rAnimState, rAms, rLocal, rNow, rDt, rPingId, rPingSent, rAcked, rPilotSent, rRemotes,
  rOwners, rRxCount = rOwners + 4
};
const int32_t* g_rx;
std::vector<NetMech*> g_rx_remotes;
mech3::Mech* g_rx_pilot;
struct LogPart : mech3::Arm {
  int32_t index = 0;
  void set_internal(float v, int s) override { log({13, index, 0, bits(v), s}); }
  void set_armor(float v, int s) override { log({13, index, 1, bits(v), s}); }
};
struct LogTorsoRx : mech3::Torso {
  void set_internal(float v, int s) override { log({13, 1, 0, bits(v), s}); }
  void set_armor(float v, int s) override { log({13, 1, 1, bits(v), s}); }
};
int32_t mech_code(const void* p) {
  if (!p) return 0;
  if (p == g_rx_pilot) return 1;
  for (size_t k = 0; k < g_rx_remotes.size(); ++k)
    if (p == g_rx_remotes[k]) return 2 + static_cast<int32_t>(k);
  return -1;
}
}  // namespace

extern "C" int32_t probe_mech_net_apply(const int32_t* in, int32_t* out, int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  g_rx = in;
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  c.now_ms = [] { return static_cast<uint32_t>(g_rx[rNow]); };
  c.position = [](mech3::Mech&, float o[3]) {
    for (int i = 0; i < 3; ++i) o[i] = fbits(g_rx[rWorld + i]);
    return true;
  };
  c.torso_position = [](mech3::Mech&, float o[3]) {
    for (int i = 0; i < 3; ++i) o[i] = fbits(g_rx[rTorsoAt + i]);
    return true;
  };
  c.mech_call = [](NetMech&, uint32_t fn, int32_t arg) { log({10, static_cast<int32_t>(fn), arg}); };
  c.aim = [](NetMech&, const float a[3]) { log({11, bits(a[0]), bits(a[1]), bits(a[2])}); };
  c.fire = [](NetMech&, uint32_t slot, uint32_t target, int32_t trigger) {
    log({12, static_cast<int32_t>(slot), static_cast<int32_t>(target), trigger});
  };
  c.radar_toggled = [](NetMech&) { log({14}); };
  c.pick_target = [](NetMech&, mech3::Handle v) { log({15, mech_code(v)}); };
  c.respawn_at = [](NetMech&, const float p[3]) { log({16, bits(p[0]), bits(p[1]), bits(p[2])}); };
  c.set_position = [](NetMech&, const float p[3]) { log({20, bits(p[0]), bits(p[1]), bits(p[2])}); };
  static mech3::Mech pilot;
  g_rx_pilot = &pilot;
  pilot.net_sent = static_cast<uint32_t>(in[rPilotSent]), pilot.net_acked = 0;
  c.pilot_mech = []() -> mech3::Mech* { return g_rx_pilot; };
  Data t;
  znet::session() = znet::Session{};
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true;
  s.local = std::make_unique<znet::Player>();
  s.local->id = static_cast<uint32_t>(in[rLocal]);
  mech3::NetGame& g = mech3::net_game();
  g = mech3::NetGame{};
  g.running = true;
  g.acked_time = static_cast<uint32_t>(in[rAcked]);
  g.reckon_damp = -0.3f;
  g_rx_remotes.clear();
  for (int32_t k = 0; k < in[rRemotes]; ++k) {
    auto* r = new NetMech();
    r->owner = static_cast<uint32_t>(in[rOwners + k]);
    g.mechs.push_back(r);
    g_rx_remotes.push_back(r);
  }
  auto* m = new NetMech();
  m->remote = true;
  m->state.flags = static_cast<uint32_t>(in[rFlagsIn]);
  for (int i = 0; i < 3; ++i) m->state.pos[i] = fbits(in[rPosIn + i]), m->velocity_in[i] = fbits(in[rVelIn + i]);
  for (int i = 0; i < 4; ++i)
    m->heading_q[i] = fbits(in[rHeadingQ + i]), m->aim_q[i] = fbits(in[rAimQ + i]), m->orientation[i] = fbits(in[rOrient + i]);
  m->heard = static_cast<uint32_t>(in[rHeard]);
  m->latency = fbits(in[rLatency]);
  float transform[12] = {};
  m->transform = transform;
  m->xbc = static_cast<uint8_t>(in[rXbc]);
  m->power_down_pending = (in[rPower] & 1) != 0, m->powered_down = (in[rPower] & 2) != 0;
  m->masc_time = fbits(in[rMasc]), m->narc_time = fbits(in[rNarc]);
  m->crouched = in[rCrouched] != 0;
  m->anim_state = in[rAnimState];
  mech3::Ams ams(0, 0, 0);
  ams.on = in[rAms] == 2;
  m->ams = in[rAms] ? &ams : nullptr;
  m->ping_id = static_cast<uint32_t>(in[rPingId]);
  m->ping_sent = static_cast<uint32_t>(in[rPingSent]);
  LogTorsoRx torso;
  m->torso = &torso;
  LogPart parts[5];
  for (int i = 0; i < 5; ++i) parts[i].index = 2 + i;
  m->arms[0] = &parts[0], m->arms[1] = &parts[1];
  m->legs[0] = reinterpret_cast<mech3::Leg*>(static_cast<mech3::MechPart*>(&parts[2]));
  m->legs[1] = reinterpret_cast<mech3::Leg*>(static_cast<mech3::MechPart*>(&parts[3]));
  m->head = reinterpret_cast<mech3::Head*>(static_cast<mech3::MechPart*>(&parts[4]));
  mech3::NetControls ctl;
  m->controls = &ctl;
  std::vector<uint8_t> msg(static_cast<size_t>(in[rLen]));
  std::memcpy(msg.data(), in + rMsg, msg.size());
  const int32_t mode = in[rMode];
  if (mode == 0) mech3::net_state_apply(*m, msg.data(), msg.size());
  else if (mode == 1) mech3::net_reckon(*m, fbits(in[rDt]));
  else if (mode == 2) mech3::net_ping_in(*m, msg.data(), msg.size());
  else mech3::net_send_done(msg.data(), msg.size());
  log({90});
  const uint32_t* st = reinterpret_cast<const uint32_t*>(&m->state);
  for (int i = 0; i < 15; ++i) g_log->push_back(static_cast<int32_t>(st[i]));
  for (float v : m->velocity_in) g_log->push_back(bits(v));
  for (float v : m->heading_q) g_log->push_back(bits(v));
  for (float v : m->aim_q) g_log->push_back(bits(v));
  for (float v : m->orientation) g_log->push_back(bits(v));
  for (int i = 0; i < 9; ++i) g_log->push_back(bits(transform[i]));
  for (float v : torso.turn_q) g_log->push_back(bits(v));
  log({91, static_cast<int32_t>(m->heard), m->xbc, m->ams ? m->ams->on : -1, bits(ctl.throttle_target),
       bits(ctl.turn_target), bits(ctl.jump_target), ctl.jump_held, bits(m->latency)});
  for (uint32_t w : m->ping_in) g_log->push_back(static_cast<int32_t>(w));
  log({92, static_cast<int32_t>(m->ping_heard), pilot.net_acked, static_cast<int32_t>(g.acked_time)});
  m->torso = nullptr, m->controls = nullptr, m->ams = nullptr, m->transform = nullptr;
  m->arms[0] = m->arms[1] = nullptr, m->legs[0] = m->legs[1] = nullptr, m->head = nullptr;
  delete m;
  for (NetMech* r : g_rx_remotes) delete r;
  g_rx_remotes.clear();
  g = mech3::NetGame{};
  znet::session() = znet::Session{};
  c = mech3::NetCalls{};
  const int32_t n = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + n, out);
  return n;
}
extern "C" int32_t probe_mech_net_rx_count() { return rRxCount; }
// The bug fixes on or off for the next calls (the library starts them off).
extern "C" void probe_mech_net_bugs(int32_t on) { zfix::options().bugs = on != 0; }

// The events' receivers (piece 7a) for test_mech_net_events.py. in: the
// handler (0 posture, 1 respawn, 2 kill, 3 scores in, 4 shove in, 5 part
// off, 6 shove out), the message (16 words) and its length, the local id,
// host, the remote players (4), the receiving mech (0..3 a remote, its
// +0x08 active), the pilot's HUD shows the scores, the game, its score
// limit and end state, the lobby (8 players: id, team, kills, deaths).
namespace {
enum Ev {
  eKind, eLen, eMsg, eLocal = eMsg + 16, eHost, eRemotes, eOwners, eWho = eOwners + 4, eActive, eHudScores, eHasGame,
  eLimit, eEndState, eLobby, eSlots, eShove = eSlots + 32, eEvCount = eShove + 3
};
const int32_t* g_ev;
int32_t g_ev_end;
float g_ev_clock;
mech3::NetGameFields g_ev_fields;
struct LogTorsoEv : mech3::Torso {
  void apply_damage(float d, int s) override { log({30, bits(d), s}); }
  void set_internal(float v, int s) override { log({31, bits(v), s}); }
};
struct LogPartEv : mech3::Arm {
  void set_internal(float v, int s) override { log({31, bits(v), s}); }
};
int32_t slot_of(uint32_t id) {
  for (int32_t k = 0; k < g_ev[eLobby]; ++k)
    if (static_cast<uint32_t>(g_ev[eSlots + 4 * k]) == id) return k;
  return -1;
}
}  // namespace

extern "C" int32_t probe_mech_net_events(const int32_t* in, int32_t* out, int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  g_ev = in;
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  c.mech_call = [](NetMech&, uint32_t fn, int32_t arg) { log({10, static_cast<int32_t>(fn), arg}); };
  c.rebuild = [](NetMech& m) { log({11, static_cast<int32_t>(m.owner)}); };
  c.reset_at = [](NetMech&, const float p[3]) { log({12, bits(p[0]), bits(p[1]), bits(p[2])}); };
  const auto num = [](const char* n) -> int32_t { return !n ? -3 : std::strcmp(n, "Unknown") == 0 ? -1 : std::strcmp(n, "Pilot") == 0 ? -2 : std::atoi(n + 1); };
  static decltype(num) s_num = num;
  c.hud_message = [](float t, int32_t id, const char* a) { log({2, bits(t), id, s_num(a)}); };
  c.hud_message2 = [](float t, int32_t id, const char* a, const char* b) { log({2, bits(t), id, s_num(a), s_num(b)}); };
  c.mech_name = [](mech3::Mech&) -> const char* { return "Pilot"; };
  c.team_of = [](uint32_t p) {
    const int32_t k = slot_of(p);
    return k < 0 ? 0u : static_cast<uint32_t>(g_ev[eSlots + 4 * k + 1]);
  };
  c.lobby_suicide = [](uint32_t p) { log({20, static_cast<int32_t>(p)}); };
  c.lobby_kill = [](uint32_t p) { log({21, static_cast<int32_t>(p)}); };
  c.lobby_kills = [](uint32_t p) {
    const int32_t k = slot_of(p);
    return k < 0 ? 0 : g_ev[eSlots + 4 * k + 2];
  };
  c.lobby_scores = [](uint32_t* o, int32_t n) {
    const int32_t m = std::min(n, g_ev[eLobby]);
    for (int32_t k = 0; k < m; ++k)
      o[3 * k] = static_cast<uint32_t>(g_ev[eSlots + 4 * k]), o[3 * k + 1] = static_cast<uint32_t>(g_ev[eSlots + 4 * k + 2]),
      o[3 * k + 2] = static_cast<uint32_t>(g_ev[eSlots + 4 * k + 3]);
    return m;
  };
  c.lobby_set_kills = [](uint32_t p, uint32_t v) { log({22, static_cast<int32_t>(p), static_cast<int32_t>(v)}); };
  c.lobby_set_deaths = [](uint32_t p, uint32_t v) { log({23, static_cast<int32_t>(p), static_cast<int32_t>(v)}); };
  c.scores_reset = [] { log({24}); };
  c.hud_scores = [] { if (g_ev[eHudScores]) log({25}); };
  g_ev_end = in[eEndState], g_ev_clock = -1.0f;
  g_ev_fields = mech3::NetGameFields{in[eLimit], in[eLobby], &g_ev_clock, nullptr, &g_ev_end};
  c.game = []() -> mech3::NetGameFields* { return g_ev[eHasGame] ? &g_ev_fields : nullptr; };
  c.game_time = [] { return 77.5f; };
  static mech3::Mech pilot;
  pilot = mech3::Mech{};
  LogTorsoEv ptorso;
  pilot.torso = &ptorso;
  LogPartEv parts[5];
  pilot.arms[0] = &parts[0], pilot.arms[1] = &parts[1];
  pilot.legs[0] = reinterpret_cast<mech3::Leg*>(static_cast<mech3::MechPart*>(&parts[2]));
  pilot.legs[1] = reinterpret_cast<mech3::Leg*>(static_cast<mech3::MechPart*>(&parts[3]));
  pilot.head = reinterpret_cast<mech3::Head*>(static_cast<mech3::MechPart*>(&parts[4]));
  g_rx_pilot = &pilot;
  c.pilot_mech = []() -> mech3::Mech* { return g_rx_pilot; };
  Sends t;
  g_send_id = 100;
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true, s.async = true;
  s.local = std::make_unique<znet::Player>();
  s.local->id = static_cast<uint32_t>(in[eLocal]);
  s.host_id = in[eHost] ? s.local->id : 99u;
  for (uint32_t id = 1; id <= 8; ++id) {
    auto r = std::make_unique<znet::PlayerRecord>();
    r->id = id, r->short_name = "P" + std::to_string(id);
    znet::lists().players.push_back(std::move(r));
  }
  mech3::NetGame& g = mech3::net_game();
  g = mech3::NetGame{};
  g.running = true;
  g_rx_remotes.clear();
  for (int32_t k = 0; k < in[eRemotes]; ++k) {
    auto* r = new NetMech();
    r->remote = true;
    r->owner = static_cast<uint32_t>(in[eOwners + k]);
    g.mechs.push_back(r);
    g_rx_remotes.push_back(r);
  }
  NetMech* who = in[eWho] < static_cast<int32_t>(g_rx_remotes.size()) ? g_rx_remotes[in[eWho]] : nullptr;
  if (who) who->active = in[eActive] != 0;
  std::vector<uint8_t> msg(static_cast<size_t>(in[eLen]));
  std::memcpy(msg.data(), in + eMsg, msg.size());
  switch (in[eKind]) {
    case 0: if (who) mech3::net_posture_in(*who, msg.data(), msg.size()); break;
    case 1: {
      float pos[3] = {0, 0, 0};
      if (msg.size() >= 16) std::memcpy(pos, msg.data() + 4, 12);
      if (who) mech3::net_respawn_in(*who, pos);
      break;
    }
    case 2: mech3::net_kill_in(msg.data(), msg.size()); break;
    case 3: mech3::net_scores_in(msg.data(), msg.size()); break;
    case 4: if (who) mech3::net_shove_in(*who, msg.data(), msg.size()); break;
    case 5: mech3::net_part_off_in(msg.data(), msg.size()); break;
    case 6: if (who) mech3::net_send_shove(*who, fbits(in[eShove]), fbits(in[eShove + 1]), fbits(in[eShove + 2])); break;
    default: break;
  }
  log({90, mech_code(pilot.attacker), bits(pilot.velocity.x), bits(pilot.velocity.y), bits(pilot.velocity.z), g_ev_end,
       bits(g_ev_clock), who ? mech_code(who->attacker) : -9});
  pilot.torso = nullptr;
  pilot.arms[0] = pilot.arms[1] = nullptr, pilot.legs[0] = pilot.legs[1] = nullptr, pilot.head = nullptr;
  pilot.attacker = nullptr;
  for (NetMech* r : g_rx_remotes) delete r;
  g_rx_remotes.clear();
  g = mech3::NetGame{};
  znet::clear_lists();
  znet::session() = znet::Session{};
  c = mech3::NetCalls{};
  const int32_t n = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + n, out);
  return n;
}
extern "C" int32_t probe_mech_net_ev_count() { return eEvCount; }

// The world's events (piece 7b) for test_mech_net_world.py. in: the
// handler (0 the veto, 1 a crater in, 2 an effect sent, 3 an effect in,
// 4 a drop sent, 5 a drop in, 6 the clock in, 7 the host's replay of one
// crater), host, local, the message (24 words) and its length, the order
// (10 words: the veto's, or a history entry's), the crater defaults (10),
// the order's shooter is the pilot, the crater made answer, an effect
// like it runs, the game, its end state, the end time and clock as bits.
#include "gamez/zdeclient/zdec_init.h"
#include "gamez/zeffect/zeff_anim_save.h"
namespace {
enum Wd {
  wKind, wHost, wLocal, wLen, wMsg, wOrder = wMsg + 24, wDefaults = wOrder + 10, wPilotShot = wDefaults + 10, wMade,
  wHas, wHasGame, wEndState, wEndTime, wClock, wWdCount
};
const int32_t* g_wd;
int32_t g_wd_end;
float g_wd_end_time, g_wd_clock_left;
mech3::NetGameFields g_wd_fields;
zwep::CraterOrder order_of(const int32_t* w) {  // the exe's 10 dwords, field by field (Handles are wider here)
  zwep::CraterOrder o;
  o.x00 = static_cast<uint32_t>(w[0]), o.points = w[1];
  o.material = reinterpret_cast<mech3::Handle>(static_cast<uintptr_t>(static_cast<uint32_t>(w[2])));
  o.x0c = static_cast<uint32_t>(w[3]), o.depth = fbits(w[4]), o.radius = fbits(w[5]);
  o.at = {fbits(w[6]), fbits(w[7]), fbits(w[8])};
  o.owner = nullptr;
  return o;
}
void log_order(const zwep::CraterOrder* o) {
  log({41, o->points, static_cast<int32_t>(reinterpret_cast<uintptr_t>(o->material)), bits(o->depth), bits(o->radius),
       bits(o->at.x), bits(o->at.y), bits(o->at.z)});
}
}  // namespace

extern "C" int32_t probe_mech_net_world(const int32_t* in, int32_t* out, int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  g_wd = in;
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  static mech3::Mech pilot, other;
  g_rx_pilot = &pilot;
  c.pilot_mech = []() -> mech3::Mech* { return g_rx_pilot; };
  c.node_mech = [](mech3::Handle) -> mech3::Mech* { return g_wd[wPilotShot] ? g_rx_pilot : &other; };
  c.crater_make = [](const zwep::CraterOrder* o) -> int32_t {
    log_order(o);
    return g_wd[wMade];
  };
  c.take_material = [](int32_t i) -> mech3::Handle { log({40, i}); return reinterpret_cast<mech3::Handle>(uintptr_t(0x1000 + i)); };
  c.material_index = [](mech3::Handle m) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m) - 0x1000); };
  c.log_has = [](const zeff::AnimLogRecord*) { log({44}); return g_wd[wHas] != 0; };
  c.log_replay = [](const zeff::AnimLogRecord* r) {
    log({43, mech3::net_game().echo});
    const auto* w = reinterpret_cast<const int32_t*>(r);
    for (int i = 0; i < 20; ++i) g_log->push_back(w[i]);
  };
  c.station = [](NetMech& m, uint32_t id) { log({45, static_cast<int32_t>(m.owner), static_cast<int32_t>(id)}); };
  c.hud_message = [](float t, int32_t id, const char*) { log({2, bits(t), id}); };
  c.set_clock_left = [](float v) { log({46, bits(v)}); };
  g_wd_end = in[wEndState], g_wd_end_time = fbits(in[wEndTime]), g_wd_clock_left = fbits(in[wClock]);
  g_wd_fields = mech3::NetGameFields{0, 0, nullptr, &g_wd_end_time, &g_wd_end};
  c.game = []() -> mech3::NetGameFields* { return g_wd[wHasGame] ? &g_wd_fields : nullptr; };
  c.game_time = [] { return 120.25f; };
  zdec::state().crater = order_of(in + wDefaults);
  Sends t;
  g_send_id = 100;
  znet::session() = znet::Session{};
  znet::Session& s = znet::session();
  s.transport = &t, s.initialised = true, s.open = true, s.async = true;
  s.local = std::make_unique<znet::Player>();
  s.local->id = static_cast<uint32_t>(in[wLocal]);
  s.host_id = in[wHost] ? s.local->id : 99u;
  mech3::NetGame& g = mech3::net_game();
  g = mech3::NetGame{};
  g.running = true;
  zdec::craters().veto = mech3::net_crater_veto;
  std::vector<uint8_t> msg(static_cast<size_t>(in[wLen]));
  std::memcpy(msg.data(), in + wMsg, msg.size());
  NetMech who;
  who.owner = 5;
  switch (in[wKind]) {
    case 0: {
      zwep::CraterOrder o = order_of(in + wOrder);
      o.owner = reinterpret_cast<mech3::Handle>(uintptr_t(0x77));
      const int r = mech3::net_crater_veto(&o);
      log({47, r, bits(o.radius)});
      break;
    }
    case 1: mech3::net_crater_in(msg.data(), msg.size()); break;
    case 2: {
      zeff::AnimLogRecord r;
      std::memcpy(&r, msg.data(), std::min(msg.size(), sizeof r));
      mech3::net_anim_sent(&r);
      break;
    }
    case 3: mech3::net_effect_in(msg.data(), msg.size()); break;
    case 4: mech3::net_send_drop(static_cast<uint32_t>(in[wOrder])); break;
    case 5: mech3::net_drop_in(who, msg.data(), msg.size()); break;
    case 6: mech3::net_clock_in(msg.data(), msg.size()); break;
    case 7: {
      std::vector<uint32_t> w(in + wOrder, in + wOrder + 10);
      zdec::craters().history.clear();
      zdec::HistoryEntry e;
      e.type = 1;
      std::copy(w.begin(), w.end(), e.words);
      zdec::craters().history.push_back(e);
      mech3::net_host_replay();
      zdec::craters().history.clear();
      break;
    }
    default: break;
  }
  log({90, g_wd_end, bits(g_wd_end_time), g.echo});
  zdec::craters().veto = nullptr;
  g = mech3::NetGame{};
  znet::session() = znet::Session{};
  c = mech3::NetCalls{};
  const int32_t n = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + n, out);
  return n;
}
extern "C" int32_t probe_mech_net_wd_count() { return wWdCount; }

// The game over (0x004707c0) for test_mech_net_end.py. in: the local id,
// the game, its kills and deaths, the lobby's kills and deaths of the
// local player.
namespace {
const int32_t* g_end;
int32_t g_end_kills, g_end_deaths;
mech3::NetGameFields g_end_fields;
}  // namespace

extern "C" int32_t probe_mech_net_end(const int32_t* in, int32_t* out, int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  g_end = in;
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  g_end_kills = in[2], g_end_deaths = in[3];
  g_end_fields = mech3::NetGameFields{};
  g_end_fields.kills = &g_end_kills, g_end_fields.deaths = &g_end_deaths;
  c.game = []() -> mech3::NetGameFields* { return g_end[1] ? &g_end_fields : nullptr; };
  c.lobby_kills = [](uint32_t p) { log({20, static_cast<int32_t>(p)}); return g_end[4]; };
  c.lobby_deaths = [](uint32_t p) { log({21, static_cast<int32_t>(p)}); return g_end[5]; };
  c.pilot_save = [] { log({50}); };
  c.exit_fade = [] { log({56, 1, 0, 1, 2, bits(2.0f), 0, 0, 0}); };
  znet::session() = znet::Session{};
  znet::session().local = std::make_unique<znet::Player>();
  znet::session().local->id = static_cast<uint32_t>(in[0]);
  mech3::net_game() = mech3::NetGame{};
  mech3::net_game_over();
  log({90, g_end_kills, g_end_deaths});
  znet::session() = znet::Session{};
  c = mech3::NetCalls{};
  const int32_t n = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + n, out);
  return n;
}

// The pilot's respawn (0x0046dde0) for test_mech_net_respawn.py. in: the
// game's +0x8c (respawn), +0x8d (unlimited), +0x8e; the local id, its team,
// its lobby slot, the roster, a cockpit explosion, force feedback, the
// remote mechs (0..3) and their places (x y z each), the mech's place, then
// 16 starts (found, x y z, yaw).
#include "mech3/mech_data.h"
namespace {
const int32_t* g_rs;
std::vector<std::unique_ptr<mech3::NetMech>> g_rs_mechs;
mech3::Mech* g_rs_me;
uint8_t g_rs_flags[4];
mech3::NetGameFields g_rs_game;
struct RespawnData : mech3::MechDataHooks {
  void anim_restart(mech3::Handle anim, mech3::Handle node) override { log({14, static_cast<int32_t>(low(anim)), static_cast<int32_t>(low(node))}); }
};
}  // namespace

extern "C" int32_t probe_mech_net_respawn(const int32_t* in, int32_t* out, int32_t cap) {
  std::vector<int32_t> lg;
  g_log = &lg;
  g_rs = in;
  mech3::NetCalls& c = mech3::net_calls();
  c = mech3::NetCalls{};
  g_rs_flags[0] = static_cast<uint8_t>(in[0]), g_rs_flags[1] = static_cast<uint8_t>(in[1]);
  g_rs_flags[2] = static_cast<uint8_t>(in[2]), g_rs_flags[3] = 0;
  g_rs_game = mech3::NetGameFields{};
  g_rs_game.flags = g_rs_flags;
  c.game = [] { return &g_rs_game; };
  c.view_camera = [](int32_t id) { log({10, id}); };
  c.pilot_set_mech = [](mech3::Mech&, int32_t roster) { log({11, roster}); };
  c.team_of = [](uint32_t p) { log({2, static_cast<int32_t>(p)}); return static_cast<uint32_t>(g_rs[4]); };
  c.unlimited = [](mech3::Mech&, bool on) { log({3, on ? 1 : 0}); };
  c.options_apply = [] { log({13}); };
  c.start_of = [](uint32_t p) { log({7, static_cast<int32_t>(p)}); return g_rs[5]; };
  c.position = [](mech3::Mech& m, float o[3]) {
    const int32_t* at = &g_rs[19];
    for (size_t k = 0; k < g_rs_mechs.size(); ++k)
      if (g_rs_mechs[k].get() == &m) at = &g_rs[10 + 3 * static_cast<int32_t>(k)];
    std::memcpy(o, at, 12);
    return true;
  };
  c.spawn_point = [](const char* name, float pos[3], float* yaw) {
    const int32_t i = std::atoi(name + 7);
    log({8, i});
    const int32_t* s = &g_rs[22 + 5 * i];
    if (!s[0]) return false;
    std::memcpy(pos, s + 1, 12), std::memcpy(yaw, s + 4, 4);
    return true;
  };
  c.place = [](mech3::Mech&, const float p[3], float yaw) { log({9, bits(p[0]), bits(p[1]), bits(p[2]), bits(yaw)}); };
  c.sensor_range = [](mech3::Mech&) { log({17}); };
  c.objectives_again = [] { log({18}); };
  c.ff_respawn = [] { if (g_rs[8]) log({19, 0, 20, 1}); };
  RespawnData data_hooks;
  mech3::set_mech_data_hooks(&data_hooks);
  Sends rec;
  znet::session() = znet::Session{};
  znet::Session& s = znet::session();
  s.transport = &rec, s.initialised = true, s.open = true;
  s.local = std::make_unique<znet::Player>();
  s.local->id = static_cast<uint32_t>(in[3]);
  mech3::NetGame& g = mech3::net_game();
  g = mech3::NetGame{};
  g.running = true;
  g_rs_mechs.clear();
  for (int32_t k = 0; k < in[9]; ++k) {
    g_rs_mechs.push_back(std::make_unique<mech3::NetMech>());
    g.mechs.push_back(g_rs_mechs.back().get());
  }
  auto* data = static_cast<mech3::MechData*>(::operator new(sizeof(mech3::MechData)));
  std::memset(static_cast<void*>(data), 0, sizeof(mech3::MechData));
  data->roster = in[6];
  mech3::CockpitModel cockpit;
  if (in[7]) {
    data->cockpit_explosion = reinterpret_cast<mech3::Handle>(static_cast<uintptr_t>(0x5150));
    cockpit.node = reinterpret_cast<mech3::Handle>(static_cast<uintptr_t>(0x6160));
    data->cockpit_model = &cockpit;
  }
  auto* m = new mech3::Mech();
  g_rs_me = m;
  m->pilot = data;
  m->team = m->allies = m->enemies = 0;  // as the exe's test memory
  const bool ok = mech3::net_pilot_respawn(*m);
  log({21, ok ? 1 : 0, 12, static_cast<int32_t>(m->team), static_cast<int32_t>(m->allies),
       static_cast<int32_t>(m->enemies), 22, g_rs_flags[2]});
  data->cockpit_model = nullptr;
  m->pilot = nullptr;
  delete m;
  ::operator delete(data);
  g.mechs.clear();
  g_rs_mechs.clear();
  g = mech3::NetGame{};
  mech3::set_mech_data_hooks(nullptr);
  znet::session() = znet::Session{};
  c = mech3::NetCalls{};
  const int32_t n = static_cast<int32_t>(std::min<size_t>(lg.size(), static_cast<size_t>(cap)));
  std::copy(lg.begin(), lg.begin() + n, out);
  return n;
}
