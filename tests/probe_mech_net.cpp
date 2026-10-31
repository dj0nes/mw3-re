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
  c.sensor_range = [](NetMech&) { log({27}); };
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
  c.rules_check = [] { log({3}); };
  c.game_over = [] { log({4}); };
  c.send_other = [](mech3::Mech&, int32_t kind, intptr_t) -> int32_t {
    log({5, kind});
    return 0;
  };
  g_clock_next = fbits(in[kClockNext]), g_end_time = fbits(in[kEndTime]), g_end_state = in[kEndState];
  g_fields = mech3::NetGameFields{in[kPlayers], &g_clock_next, &g_end_time, &g_end_state};
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
  int32_t ret = 0;
  if (in[kKind] == 100) {
    mech3::net_mech_tick(*m);
  } else {
    const intptr_t arg = in[kKind] == 0x16 ? reinterpret_cast<intptr_t>(&weapon) : in[kArg];
    ret = mech3::net_send_event(*m, in[kKind], arg);
  }
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
