#include "mech3/mech_net.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gamez/zdeclient/zdec_crater.h"
#include "mech3/mech_parts.h"
#include "gamez/zmath/zmth_vector.h"
#include "crt/msvc_math.h"
#include "gamez/zeffect/zeff_anim_save.h"
#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zutil/ztimer.h"
#include "mech3/mech_config.h"
#include "mech3/mech_dead.h"
#include "mech3/mech_build.h"
#include "mech3/mech_control.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_skin.h"
#include "mech3/weapon.h"

namespace mech3 {

namespace {
NetGame g_net;
NetCalls g_calls;

// The messages 0x0046d880 takes, in the exe's order of registration.
constexpr uint16_t kRouted[] = {0xf, 0x10, 0x11, 0x16, 0x15, 0x18, 0x12, 0x13, 0x17, 0x1a, 0x1b, 0x19, 0x21, 0x14, 0x1f};

// Mech3.exe 1.2 @ 0x004d72a0, message 1's: returns 0.
void ignore(uint32_t, const uint8_t*, size_t) {}
// Mech3.exe 1.2 @ 0x004709d0, message 0xe's (piece 6).
void send_done(uint32_t from, const uint8_t* msg, size_t size) {
  if (g_calls.send_done) g_calls.send_done(nullptr, from, msg, size);
  else net_send_done(msg, size);
}

void call(NetRoute fn, NetMech* m, uint32_t from, const uint8_t* msg, size_t size) {
  if (fn) fn(m, from, msg, size);
}
}  // namespace

NetGame& net_game() { return g_net; }
NetCalls& net_calls() { return g_calls; }

NetMech* net_mech(uint32_t owner) {
  for (NetMech* m : g_net.mechs)
    if (m->owner == owner) return m;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0046ceb0
void net_start() {
  if (g_net.running) return;
  g_net.missing.clear();
  for (uint16_t t : kRouted) znet::on(t, net_route, 2);
  znet::on(6, net_player_left, 2);
  znet::on(1, ignore, 2);
  znet::on(0xe, send_done, 2);
  zdec::craters().veto = g_calls.crater_veto;
  g_net.running = true;
  weapon_world().lock_targets = true;  // the same flag
  zeff::log_set_net(g_calls.anim_sent, 0xc);
  if (g_calls.game_clear) g_calls.game_clear();
  g_net.reckon_gain = -6.5f;
  g_net.reckon_damp = -0.3f;
}

// Mech3.exe 1.2 @ 0x0046d100
void net_end() {
  if (!g_net.running) return;
  for (uint16_t t : kRouted) znet::off(t, net_route);
  znet::off(6, net_player_left);
  znet::off(1, ignore);
  znet::off(0xe, send_done);
  zdec::craters().veto = nullptr;
  g_net.running = false;
  weapon_world().lock_targets = false;
  if (g_calls.player_mech_end) g_calls.player_mech_end();
  while (!g_net.mechs.empty()) {
    NetMech* m = g_net.mechs.front();
    if (g_calls.forget) g_calls.forget(*m);
    g_net.mechs.pop_front();
    if (m) g_calls.free ? g_calls.free(m) : net_mech_delete(m);
  }
  znet::set_player_data(znet::local_id(), nullptr, 0);
  zeff::log_set_net(nullptr, 0xc);
  if (g_calls.ff_stop) g_calls.ff_stop();
  for (Config*& c : g_net.configs) {
    if (c) g_calls.config_free ? g_calls.config_free(c) : delete c;
    c = nullptr;
  }
  g_net.configs.clear();
  g_net.missing.clear();
}

// Mech3.exe 1.2 @ 0x0046d880. The handlers return to zNetwork's dispatch,
// which drops the answer.
void net_route(uint32_t from, const uint8_t* msg, size_t size) {
  if (size < 2) return;
  uint16_t type;
  std::memcpy(&type, msg, 2);
  const NetCalls& c = g_calls;
  if (type == 0x19) return call(c.crater, nullptr, from, msg, size);
  NetMech* m = net_mech(from);
  if (!m) {
    if (type != 0xf) return;
    if (c.create) c.create(nullptr, from, msg, size);
    else net_mech_create(from);
    if (!(m = net_mech(from))) return;
    // Where it is first, from the state's position, then the state applied.
    float pos[3] = {};
    std::memcpy(pos, msg + 4, size >= 16 ? 12 : size > 4 ? size - 4 : 0);
    if (c.set_position) c.set_position(*m, pos);
    m->heard = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
    net_route(from, msg, size);
    if (c.reckon) c.reckon(*m, ztimer::state().dt);
    return;
  }
  switch (type) {
    case 0xf: return c.state ? c.state(m, from, msg, size) : net_state_apply(*m, msg, size);
    case 0x10: return c.ping ? c.ping(m, from, msg, size) : net_ping_in(*m, msg, size);
    case 0x11: return call(c.clock, m, from, msg, size);
    case 0x12: return call(c.respawn, m, from, msg, size);
    case 0x13: return call(c.kill, m, from, msg, size);
    case 0x14: return call(c.scores, m, from, msg, size);
    case 0x16: return call(c.rules, m, from, msg, size);
    case 0x17: return call(c.posture, m, from, msg, size);
    case 0x18: return call(c.knockback, m, from, msg, size);
    case 0x1a: return call(c.effect, m, from, msg, size);
    case 0x1b: return call(c.drop, m, from, msg, size);
    case 0x1f: return call(c.chat, m, from, msg, size);
    case 0x21: return call(c.part_off, m, from, msg, size);
    default: return;  // 0x15 is registered but not handled in a mission
  }
}

// Mech3.exe 1.2 @ 0x0046d3b0. 0x40800000 is 4 s.
void net_player_left(uint32_t from, const uint8_t* msg, size_t size) {
  (void)msg, (void)size;
  NetMech* m = net_mech(from);
  if (!m) return;
  if (g_calls.hud_message) g_calls.hud_message(4.0f, 0x26a, znet::player_name(from));
  if (g_calls.forget) g_calls.forget(*m);
  g_calls.free ? g_calls.free(m) : net_mech_delete(m);
  if (g_calls.lobby_left) g_calls.lobby_left(from);
}

namespace {
// ftol of x to a byte, x clamped to -127..127 first (the controls' axes).
uint8_t axis_byte(float x) {
  float v = x * 127.0f;  // [0x005c33e8]
  if (v > 127.0f) v = 127.0f;
  else if (v < -127.0f) v = -127.0f;  // [0x005c33ec]
  return static_cast<uint8_t>(crt::msvc_ftol(v));
}
// A part's fraction left as a byte: 1 is 255, 0 is 0, else ceil(f x 255).
uint32_t fraction_byte(float f) {
  if (f == 1.0f) return 0xff;
  if (f == 0.0f) return 0;
  return static_cast<uint32_t>(crt::msvc_ftol(std::ceil(static_cast<double>(f * 255.0f)))) & 0xff;
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) { std::memcpy(b.data() + at, &v, 4); }
void put_f(std::vector<uint8_t>& b, size_t at, float v) { std::memcpy(b.data() + at, &v, 4); }
void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) { std::memcpy(b.data() + at, &v, 2); }
float game_time() { return g_calls.game_time ? g_calls.game_time() : 0.0f; }
}  // namespace

// The 0xf message of 0x0046f160. Its bytes that nothing writes are 0 (the
// exe's static buffer has 0 there; a malloc'd one, whatever the heap held).
std::vector<uint8_t> net_state_message(Mech& m) {
  const NetCalls& c = g_calls;
  const auto shots = static_cast<uint32_t>(m.net_shots.size());
  uint32_t parts = 0;  // bits 0..9 counted, though only 1..8 are written
  for (int i = 9; i >= 0; --i) parts += (m.net_damaged >> i & 1) != 0;
  const size_t head = 0x3c + shots;
  std::vector<uint8_t> b(head + 4 * parts, 0);
  put16(b, 0, 0xf);
  put16(b, 2, static_cast<uint16_t>(b.size()));
  float pos[3] = {0, 0, 0};
  if (c.position) c.position(m, pos);
  std::memcpy(b.data() + 4, pos, 12);
  put16(b, 0x10, static_cast<uint16_t>(crt::msvc_ftol(m.velocity.x * 127.0f)));
  put16(b, 0x12, static_cast<uint16_t>(crt::msvc_ftol(m.velocity.y * 127.0f)));
  put16(b, 0x14, static_cast<uint16_t>(crt::msvc_ftol(m.velocity.z * 127.0f)));
  put_f(b, 0x18, static_cast<float>(std::atan2(static_cast<double>(m.orientation[2]), m.orientation[0])));
  float angles[3] = {0, 0, 0};
  if (m.torso) zmth::quat_angles(m.torso->turn_q, angles);  // 0x00525f80
  put_f(b, 0x1c, angles[0]);
  put_f(b, 0x20, angles[1]);
  const MechControls* ctl = m.controls;
  b[0x24] = axis_byte(ctl ? ctl->throttle_target : 0.0f);  // IControls vt+0x2c
  b[0x25] = axis_byte(ctl ? ctl->turn : 0.0f);             // vt+0x24
  // The aim: from the torso's node to the weapons' aim point, as a turn off
  // straight ahead (0, 0, -1) and a distance.
  float at[3] = {0, 0, 0};
  if (c.torso_position) c.torso_position(m, at);
  const auto* w = static_cast<const WeaponManager*>(m.ammo);
  zmth::Vec3 d{(w ? w->aim.x : 0.0f) - at[0], (w ? w->aim.y : 0.0f) - at[1], (w ? w->aim.z : 0.0f) - at[2]};
  const float range = zmth::normalize(d);  // 0x004020d0
  const float ahead[3] = {0.0f, 0.0f, -1.0f};
  float q[4], turn[3];
  zmth::quat_between(ahead, &d.x, q);  // 0x00525a50
  zmth::quat_log(q, turn);             // 0x005259c0
  put_f(b, 0x28, turn[0]);
  put_f(b, 0x2c, turn[1]);
  put_f(b, 0x30, range);
  // The flags: 0 nothing struck last, 1 inactive, 2-6 the motion state, 7-10
  // the first action, 11 limping, 12-18 the shots, 19-22 the parts, 23
  // passive, 24 powered down, 25 MASC, 26 the AMS on, 27 NARCed, 30 jumping,
  // 31 crouched (docs/network-messages.md).
  uint32_t f = m.collider.clear == 0 ? 1u : 0u;
  f |= (m.active ? 0u : 1u) << 1;
  f |= (static_cast<uint32_t>(m.anim_state) & 0x1f) << 2;
  f |= (m.actions.empty() ? 0u : static_cast<uint32_t>(m.actions.front()) & 0xf) << 7;
  f |= (m.limp != 0 ? 1u : 0u) << 11;  // -1 (none) too
  f |= (shots & 0x7f) << 12;
  f |= (parts & 0xf) << 19;
  f |= ((m.xbc >> 2 & 1) != 0 ? 1u : 0u) << 23;  // passive radar
  f |= (m.is_powered_down() ? 1u : 0u) << 24;  // vt+0xa0
  f |= (m.masc_time != 0.0f ? 1u : 0u) << 25;
  f |= (m.ams && m.ams->on ? 1u : 0u) << 26;
  f |= (m.narc_time > 0.0f ? 1u : 0u) << 27;
  f |= (ctl && ctl->jump != 0.0f ? 1u : 0u) << 30;  // IControls vt+0x38
  f |= (m.crouched ? 1u : 0u) << 31;
  put32(b, 0x38, f);
  put32(b, 0x34, c.target_player ? c.target_player(m) : 0);
  size_t at_shot = 0x3c;
  for (const NetShot& s : m.net_shots)
    b[at_shot++] = static_cast<uint8_t>((s.slot & 0xf) | (s.trigger & 3) << 4 | (s.target != 0) << 6);
  size_t at_part = head;
  uint32_t done = 0;
  for (int32_t id = 1; done < parts && id <= 8; ++id) {
    if (!(m.net_damaged & 1u << id)) continue;
    const int32_t sub = id == 7 ? 1 : id == 8 ? 2 : 0;  // the side torsos are the torso's sections
    const MechPart* p = part_by_id(m, id);
    uint32_t v = static_cast<uint32_t>(id) & 0xff;
    if (p) {
      v |= fraction_byte(p->get_internal(sub)) << 8;  // vt+0x48
      v |= fraction_byte(p->get_armor(sub)) << 16;    // vt+0x44
      v |= fraction_byte(p->get_armor(sub + 3)) << 24;
    }
    put32(b, at_part, v);
    at_part += 4, ++done;
  }
  return b;
}

NetMech* net_mech_of(Handle vehicle) {
  for (NetMech* m : g_net.mechs)
    if (m == vehicle) return m;
  return nullptr;
}

// Mech3.exe 1.2 @ 0x00470ab0
uint32_t net_next_player() {
  if (g_net.mechs.empty()) return 0;
  bool next = false;
  for (const NetMech* m : g_net.mechs) {
    if (next) return g_net.ping_turn = m->owner;
    next = m->owner == g_net.ping_turn;
  }
  return g_net.ping_turn = g_net.mechs.front()->owner;
}

// Mech3.exe 1.2 @ 0x00470b40
float net_send_period(int32_t players) {
  if (g_net.rate_setting == 1) return 0.1f;
  if (g_net.rate_setting != 0 && g_net.rate_setting != 2) return 0.25f;
  switch (players) {
    case 2: case 3: case 4: return 1.0f / 7.0f;
    case 5: return 0.2f;
    case 6: return 0.25f;
    case 7: return 2.0f / 7.0f;
    case 8: return 1.0f / 3.0f;
    default: return 0.5f;
  }
}

// Mech3.exe 1.2 @ 0x0046f160
int32_t net_send_event(Mech& m, int32_t kind, intptr_t arg) {
  if (!g_net.running || m.remote) return 0;  // a network mech's vt+0x168 is 0x0046ef50: 0
  const NetCalls& c = g_calls;
  switch (kind) {
    case 0xf: {
      std::vector<uint8_t> b = net_state_message(m);
      const uint32_t id = znet::send(b.data(), static_cast<uint32_t>(b.size()), false, 0);
      m.net_shots.clear();  // the records deleted (0x00470bc0)
      m.net_damaged = 0;
      return static_cast<int32_t>(id);
    }
    case 0x10: {  // the ping, to the next player round the list
      const uint32_t id = g_net.ping_counter++;
      if (znet::is_host() && g_net.ping_counter % 10 == 0 && c.rules_check) c.rules_check();
      const uint32_t to = net_next_player();
      NetMech* them = to ? net_mech(to) : nullptr;
      if (!them) return 0;
      const uint32_t now = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
      uint8_t b[0x14] = {0x10, 0, 0x14, 0};
      const uint32_t words[4] = {id, them->ping_in[1], g_net.acked_time, now - them->ping_heard};
      std::memcpy(b + 4, words, 16);
      znet::send(b, sizeof b, false, to);
      them->ping_sent = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
      them->ping_id = id;
      return 0;
    }
    case 0x15:  // a part's damage goes with the next state
      m.net_damaged |= 1u << (static_cast<uint32_t>(arg) & 31);
      return 0;
    case 0x16: {  // a shot, with the next state
      const auto* w = reinterpret_cast<const Weapon*>(arg);
      NetShot s;
      s.slot = w->slot;
      s.trigger = m.ammo ? static_cast<const WeaponManager*>(m.ammo)->state : 0;
      const NetMech* t = w->locked && w->projectile_target ? net_mech_of(w->projectile_target) : nullptr;
      s.target = t ? t->owner : 0;
      m.net_shots.push_back(s);
      return 0;
    }
    case 0x11: case 0x12: case 0x13: case 0x17: case 0x21:
      return c.send_other ? c.send_other(m, kind, arg) : 0;
    default:
      return 0;
  }
}

// Mech3.exe 1.2 @ 0x0046ef70. The game time is [0x0060991c], the clock left
// [0x00609920] while [0x00609924].
void net_mech_tick(Mech& m) {
  const NetCalls& c = g_calls;
  NetGameFields* g = c.game ? c.game() : nullptr;
  if (!g) return;
  const float now = game_time();
  if (g_net.next_state <= now) {
    if (g_net.resends++ >= 0x19) {
      g_net.resends = 0;
      m.net_damaged = 0x1fe;  // every part, every 26th state
    }
    m.net_sent = static_cast<uint32_t>(net_send_event(m, 0xf, 0));
    g_net.next_state = net_send_period(g->players) + now;
    m.net_acked = 0;
  }
  if (g_net.next_ping <= game_time()) {
    net_send_event(m, 0x10, 0);
    g_net.next_ping = game_time() - -5.1f;  // [0x005c33d8]
  }
  const bool timed = c.clock_on && c.clock_on();
  const float left = c.clock_left ? c.clock_left() : 0.0f;
  if (timed) {
    if (left <= 60.0f && !g_net.warned) {
      g_net.warned = 1;
      g_net.last_second = 0;
      if (c.hud_message) c.hud_message(4.0f, 0xf0b, nullptr);
    }
    if (left <= 1.0f && !g_net.last_second) g_net.last_second = 1;
    if (left < 3.0f && g->end_state && *g->end_state == 0) *g->end_state = 1;
  }
  if (znet::is_host() && g->clock_next && *g->clock_next <= game_time()) {
    net_send_event(m, 0x11, reinterpret_cast<intptr_t>(g));
    *g->clock_next = game_time() - -5.0f;  // [0x005c33e4]
  }
  const int32_t state = g->end_state ? *g->end_state : 0;
  if (state != 0 && state != 3) net_send_event(m, 0x11, reinterpret_cast<intptr_t>(g));
  if (state == 3 && g->end_time && game_time() > *g->end_time && c.game_over) c.game_over();
}

namespace {
// The apply's calls on the mech, through the test hook when set.
void mech_do(NetMech& m, uint32_t fn, int32_t arg) {
  if (g_calls.mech_call) return g_calls.mech_call(m, fn, arg);
  switch (fn) {
    case 0x41cc30: m.power(arg); break;
    case 0x41df50: m.masc(arg != 0); break;
    case 0x41ac10: m.mark_narc(arg != 0); break;
    case 0x41bda0: m.crouch(arg != 0); break;
    case 0x415a90: m.end_limp(); break;
    case 0x41d5f0: m.start_limp(arg, false); break;
    case 0x416780: m.queue_action(arg, true); break;
    default: break;
  }
}
}  // namespace

// Mech3.exe 1.2 @ 0x0046e4d0. The controls are set through IControls
// (vt+0x20 the throttle, +0x28 the turn, +0x34 the jump). The aim's turn
// is carried on from the last by the latency over the gap since that
// state, a latency in seconds over a gap in milliseconds (so barely), and
// its z dropped then (it is 0 anyway).
void net_state_apply(NetMech& m, const uint8_t* msg, size_t size) {
  const NetCalls& c = g_calls;
  if (size < sizeof(NetStateIn)) return;  // FIX (bug): the exe reads 0x3c bytes whatever came
  std::memcpy(&m.state, msg, sizeof(NetStateIn));
  const NetStateIn& st = m.state;
  const bool inactive = (st.flags >> 1 & 1) != 0;
  if (!inactive) {
    constexpr double kInv127 = 0.00787402;  // [0x005c33c8]
    for (int i = 0; i < 3; ++i) m.velocity_in[i] = static_cast<float>(st.vel[i] * kInv127);
    if (m.controls) {
      m.controls->set_throttle(static_cast<float>(st.throttle * kInv127));
      m.controls->set_turn(static_cast<float>(st.turn * kInv127));
    }
    float tq[4];
    zmth::quat_two_angles(st.torso[1], st.torso[0], tq);  // 0x00525280
    if (m.torso) std::memcpy(m.torso->turn_q, tq, sizeof tq);  // 0x004646f0
    zmth::Vec3 v{st.aim[0], st.aim[1], 0.0f};
    zmth::Quat q;
    zmth::quat_exp(v, q);  // 0x00525900
    const float qn[4] = {q.w, q.x, q.y, q.z};
    float use[4];
    if (m.aim_q[1] == 0.0f || m.aim_q[2] == 0.0f) {
      std::memcpy(use, qn, sizeof use);
    } else {
      const uint32_t now = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
      const double gap = static_cast<double>(now - m.heard);
      const double f = gap > 1.0 ? (m.latency + gap) / gap : 1.0;
      for (int i = 0; i < 3; ++i) use[i] = static_cast<float>((qn[i] - m.aim_q[i]) * f + m.aim_q[i]);
      use[3] = 0.0f;
    }
    std::memcpy(m.aim_q, qn, sizeof qn);
    m.heard = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
    const zmth::Quat uq{use[0], use[1], use[2], use[3]};
    const zmth::Vec3 ahead{0.0f, 0.0f, -st.range};
    zmth::Vec3 dir;
    zmth::quat_rotate(uq, ahead, dir);  // 0x00525840
    float at[3] = {0, 0, 0};
    if (c.torso_position) c.torso_position(m, at);
    const Vec3f aim{at[0] + dir.x, at[1] + dir.y, at[2] + dir.z};
    if (c.aim) c.aim(m, &aim.x);
    else if (auto* w = static_cast<WeaponManager*>(m.ammo)) w->set_aim(&aim);  // 0x004b0bc0
    m.heading_q[0] = static_cast<float>(std::cos(static_cast<double>(st.heading)));
    m.heading_q[1] = 0.0f;
    m.heading_q[2] = static_cast<float>(std::sin(static_cast<double>(st.heading)));
    m.heading_q[3] = 0.0f;
    const uint8_t passive = static_cast<uint8_t>(st.flags >> 23 & 1);
    if (passive != (m.xbc >> 2 & 1)) {
      m.xbc = static_cast<uint8_t>((m.xbc & ~4) | passive << 2);
      if (c.radar_toggled) c.radar_toggled(m);  // vt+0xb8
    }
    Handle target = nullptr;
    if (st.target == znet::local_id()) target = c.pilot_mech ? c.pilot_mech() : nullptr;
    else target = net_mech(st.target);  // 0x00470c20
    if (c.pick_target) c.pick_target(m, target);  // 0x004a7b40
  }
  // The shots and the damaged parts after the fixed part: the parts follow
  // the shots, and are taken only when the message holds more than them.
  if (size >= 4) {
    uint16_t full;
    std::memcpy(&full, msg + 2, 2);
    if (full > 0x3c && full <= size) {
      const uint32_t shots = st.flags >> 12 & 0x7f, parts = st.flags >> 19 & 0xf;
      const uint8_t* shot = shots ? msg + 0x3c : nullptr;
      const uint8_t* part = shots == 0 ? msg + 0x3c : shots + 0x3c < full ? msg + 0x3c + shots : nullptr;
      if (!inactive && shot) {
        auto* w = static_cast<WeaponManager*>(m.ammo);
        for (uint32_t k = 0; k < shots; ++k) {
          const uint8_t b = shot[k];
          const uint32_t at = b & 0x40 ? st.target : 0;  // a player id, as the exe passes it
          if (c.fire) c.fire(m, b & 0xf, at, b >> 4 & 3);
          else if (w) w->net_fire(b & 0xf, reinterpret_cast<Handle>(static_cast<uintptr_t>(at)), b >> 4 & 3);  // 0x004b08c0
        }
      }
      for (uint32_t k = 0; part && k < parts && part + 4 * k + 4 <= msg + full; ++k) {
        uint32_t word;
        std::memcpy(&word, part + 4 * k, 4);
        const uint32_t id = word & 0xff;
        if (id == 0 || id >= 9) continue;
        const int32_t sub = id == 7 ? 1 : id == 8 ? 2 : 0;
        MechPart* p = part_by_id(m, static_cast<int32_t>(id));
        if (!p) continue;
        constexpr float k255 = 1.0f / 255.0f;  // [0x005e56ec]
        p->set_internal(static_cast<float>(word >> 8 & 0xff) * k255, sub);  // vt+0x3c
        p->set_armor(static_cast<float>(word >> 16 & 0xff) * k255, sub);    // vt+0x40
        p->set_armor(static_cast<float>(word >> 24) * k255, sub + 3);
      }
    }
  }
  const uint32_t f = st.flags;
  const bool down = (f >> 24 & 1) != 0;
  if (down && !m.is_powered_down()) mech_do(m, 0x41cc30, 1);  // vt+0xa0
  else if (!down && m.is_powered_down()) mech_do(m, 0x41cc30, 0);
  if (m.ams) m.ams->on = (f >> 26 & 1) != 0;
  if ((f >> 25 & 1) != (m.masc_time != 0.0f ? 1u : 0u)) mech_do(m, 0x41df50, static_cast<int32_t>(f >> 25 & 1));
  if ((f >> 27 & 1) != (m.narc_time > 0.0f ? 1u : 0u)) mech_do(m, 0x41ac10, static_cast<int32_t>(f >> 27 & 1));
  if ((m.crouched ? 1u : 0u) != (f >> 31)) mech_do(m, 0x41bda0, static_cast<int32_t>(f >> 31));
  if (m.controls) m.controls->set_jump((f >> 30 & 1) ? 1.0f : 0.0f);
  const int32_t state = static_cast<int32_t>(f >> 2 & 0x1f);
  bool respawn = false;
  if (m.anim_state != state) {
    switch (m.anim_state) {
      case 0:
        if (state != 1 && static_cast<uint32_t>(state) < 9) mech_do(m, 0x415a90, 0);
        break;
      case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9:
        if (state == 0) mech_do(m, 0x41d5f0, static_cast<int32_t>(f >> 11 & 1));
        break;
      case 10: respawn = true; break;
      case 12: if (state <= 9) mech_do(m, 0x416780, 6); break;
      case 13: if (state <= 9) mech_do(m, 0x416780, 5); break;
      default: break;
    }
  }
  if (respawn && c.respawn_at) c.respawn_at(m, st.pos);  // 0x0046ed30 with the static 0x12 [0x005e55f8]
}

// Mech3.exe 1.2 @ 0x0046ed70
void net_reckon(NetMech& m, float dt) {
  const NetCalls& c = g_calls;
  if (m.state.flags >> 1 & 1) return;
  float p[3] = {0, 0, 0};
  if (c.position) c.position(m, p);  // 0x005a85e0
  float q[4];
  if (m.state.flags & 1) {  // to the state's place and heading at once
    std::memcpy(p, m.state.pos, sizeof p);
    std::memcpy(q, m.heading_q, sizeof q);
  } else {
    float k = fast_exp(dt * g_net.reckon_gain);
    if (k > 1.0f) k = 1.0f;
    else if (k < 0.0f) k = 0.0f;
    const float j = 1.0f - k;  // 0x0051e380: p toward the state's place
    p[0] = j * m.state.pos[0] + k * p[0];
    p[1] = m.state.pos[1] * j + k * p[1];
    p[2] = m.state.pos[2] * j + k * p[2];
    for (int i = 0; i < 3; ++i) m.state.pos[i] += m.velocity_in[i] * dt;
    const float damp = fast_exp(dt * g_net.reckon_damp);
    for (float& v : m.velocity_in) v *= damp;
    zmth::quat_slerp(m.orientation, m.heading_q, k, q);  // 0x00525ce0
  }
  if (c.set_position) c.set_position(m, p);  // vtable +0x18
  if (m.transform) zmth::quat_matrix(q, m.transform);  // 0x00525740
  std::memcpy(m.orientation, q, sizeof q);
  m.entity_moved();  // 0x005a85c0
}

// Mech3.exe 1.2 @ 0x00470a00. 0.0005 halves the round trip into seconds.
void net_ping_in(NetMech& m, const uint8_t* msg, size_t size) {
  const NetCalls& c = g_calls;
  uint32_t w[5] = {};
  std::memcpy(w, msg, std::min<size_t>(size, sizeof w));
  if (m.ping_id == w[2]) {
    const uint32_t now = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
    const double l = (static_cast<double>(now - m.ping_sent - w[4]) - static_cast<double>(g_net.acked_time - w[3])) *
                     0.0005;  // [0x005c3408]
    m.latency = l <= 0.0 ? 0.0f : static_cast<float>(l);
  } else {
    m.latency = 0;
  }
  std::memcpy(m.ping_in, w, sizeof w);
  m.ping_heard = c.now_ms ? c.now_ms() : ztimer::ticks_ms();
}

// Mech3.exe 1.2 @ 0x004709d0. The exe's message held a pointer to
// DirectPlay's DPMSG_SENDCOMPLETE (+0x1c the id, +0x24 the send time); ours
// has them as its words (multiplayer-port.md piece 2).
void net_send_done(const uint8_t* msg, size_t size) {
  if (size < 12) return;
  uint32_t id, sent;
  std::memcpy(&id, msg + 4, 4);
  std::memcpy(&sent, msg + 8, 4);
  Mech* p = g_calls.pilot_mech ? g_calls.pilot_mech() : nullptr;
  if (p && id == p->net_sent) {
    p->net_acked = 1;
    g_net.acked_time = sent;
  }
}

// Mech3.exe 1.2 @ 0x0046e0a0
void net_spawn_offset(float pos[3], int32_t slot) {
  if (slot >= 2 && slot <= 4) pos[0] += 10.0f;
  else if (slot != 1 && slot != 5) pos[0] -= 10.0f;
  if (slot <= 2) pos[2] += 10.0f;
  else if (slot != 3 && slot != 7) pos[2] -= 10.0f;
}

// Mech3.exe 1.2 @ 0x0046d820
bool net_read_record(uint32_t player, std::vector<uint8_t>* out) {
  out->clear();
  uint32_t size = znet::player_data_size(player);
  bool ok = false;
  if (size > 7) {
    out->resize(size);
    ok = znet::get_player_data(player, out->data(), &size);
  }
  if (!ok) out->clear();
  return ok;
}

// Mech3.exe 1.2 @ 0x0046da80. The mech's definition is loaded first
// (0x004846a0) and not used. The record is made by 0x0046dd90 (kind 2,
// zero-filled to its size).
bool net_launch_player(Mech& m, const std::vector<uint32_t>& p, int32_t skin, bool unlimited_ammo, bool outside_view) {
  const NetCalls& c = g_calls;
  const int32_t roster = m.pilot ? m.pilot->roster : 0;
  c.dfn ? (void)c.dfn(roster) : (void)mech_dfn(roster);
  g_net.next_state = 0, g_net.warned = 0, g_net.last_second = 0;
  m.net_sent = 0;
  m.net_acked = 1;
  const uint32_t local = znet::local_id();
  if (p.empty()) return false;
  const uint32_t n = p[0] & 0xffff;
  std::vector<uint8_t> rec(n + 0xc, 0);
  const uint32_t kind = 2;
  std::memcpy(rec.data(), &kind, 4);
  std::memcpy(rec.data() + 0xc, p.data(), std::min<size_t>(n, 4 * p.size()));
  std::memcpy(rec.data() + 8, &roster, 4);
  std::memcpy(rec.data() + 4, &skin, 4);
  m.set_team(c.team_of ? c.team_of(local) : 0);
  if (c.unlimited) c.unlimited(m, unlimited_ammo);
  else if (auto* w = static_cast<WeaponManager*>(m.ammo)) w->set_unlimited(unlimited_ammo);
  znet::set_player_data(local, rec.data(), static_cast<uint32_t>(rec.size()));
  if (c.scores_reset) c.scores_reset();
  if (outside_view && c.view_outside) c.view_outside();
  const int32_t slot = c.start_of ? c.start_of(local) : 0;
  char name[0x20];
  std::snprintf(name, sizeof name, "remote_%d", slot);
  float pos[3] = {0, 0, 0}, yaw = 0;  // 0x0063d870
  if (!c.spawn_point || !c.spawn_point(name, pos, &yaw)) return false;
  net_spawn_offset(pos, slot);
  c.place ? c.place(m, pos, yaw) : m.set_place(pos, yaw);  // vtable +0xdc
  if (c.ff_spawn) c.ff_spawn();
  return true;
}

// Mech3.exe 1.2 @ 0x0046e280
NetMech* net_mech_new(uint32_t owner, MechData* data, NetControls* ctl, Config* cfg, const char* name) {
  const NetCalls& c = g_calls;
  auto* m = new NetMech();
  mech_construct(*m, data, ctl, cfg, name);  // 0x004149f0
  if (c.label_new) m->label = c.label_new(*m);
  m->remote = true;  // vtable 0x005c3508
  m->owner = owner;
  m->collides = false;
  g_net.mechs.push_back(m);
  if (c.world_add) c.world_add(*m);
  if (m->torso) m->torso->set_mode(2);
  m->power(0);
  return m;
}

// Mech3.exe 1.2 @ 0x0046d450. The record's word 0 is 2 for a mech's.
// FIX (bug): a record too short for a loadout's head gives none, and one
// under 12 bytes reads zeros past its end (the exe reads past both), and the controls are deleted when the type's data does
// not load (the exe leaves them).
void net_mech_create(uint32_t from) {
  const NetCalls& c = g_calls;
  const char* name = znet::player_name(from);
  std::vector<uint8_t> rec;
  uint32_t kind = 0;
  if (net_read_record(from, &rec)) std::memcpy(&kind, rec.data(), 4);
  const size_t size = rec.size();
  if (rec.size() < 0xc) rec.resize(0xc, 0);
  int32_t roster = -1;
  const MechDfn* dfn = nullptr;
  Config* cfg = nullptr;
  uint32_t team = 0;
  int32_t skin = 0;
  if (kind == 2) {
    std::memcpy(&roster, rec.data() + 8, 4);
    dfn = c.dfn ? c.dfn(roster) : mech_dfn(roster);
    team = c.team_of ? c.team_of(from) : 0;
    std::memcpy(&skin, rec.data() + 4, 4);
    char cfg_name[0x80];
    std::snprintf(cfg_name, sizeof cfg_name, "%s's Config", name ? name : "(null)");
    if (size >= 0xc + 0x34) {
      std::vector<uint32_t> packed((size - 0xc) / 4);
      std::memcpy(packed.data(), rec.data() + 0xc, 4 * packed.size());
      cfg = c.unpack ? c.unpack(packed.data(), packed.size()) : Config::unpacked(packed.data(), packed.size());
    }
    if (cfg) {
      if (c.valid ? c.valid(*cfg) : cfg->validate(false)) {
        std::free(cfg->name);
        cfg->name = strdup(cfg_name);
        g_net.configs.push_back(cfg);
      } else {
        g_net.missing.push_back(from);
        c.config_free ? c.config_free(cfg) : delete cfg;
        cfg = nullptr;
      }
    }
  }
  if (!dfn || !cfg || roster == -1) return;
  auto* ctl = new NetControls();
  ctl->init();  // 0x004265a0(0, 0)
  MechData* data = c.data ? c.data(roster) : mech_build_hooks().mech_data(roster, 1);
  if (!data) {
    delete ctl;
    return;
  }
  NetMech* m = c.make ? c.make(from, data, ctl, cfg, name) : net_mech_new(from, data, ctl, cfg, name);
  if (!m) return;
  m->set_team(team);
  Handle s = nullptr;
  if (dfn->skins && skin >= 0 && skin < static_cast<int32_t>(dfn->skins->skins.size())) s = dfn->skins->skins[skin];
  c.skin ? c.skin(*m, s) : (void)mech_set_skin(*m, s);
  if (c.sensor_range) c.sensor_range(*m);  // 0x004d6d20 with its own +0x9c
  m->latency = 0;
  if (znet::is_host() && c.host_replay) c.host_replay();
}

// Mech3.exe 1.2 @ 0x0046e3a0, with the destructor 0x0046e3c0 (its
// 0x005a05a0, zEntity3d's, is the Mech's release here).
void net_mech_unlist(NetMech* m) {
  g_net.mechs.remove(m);
  dead_mechs().remove(m);
  if (m->label && g_calls.label_free) g_calls.label_free(m->label);
  m->label = nullptr;
}
void net_mech_delete(NetMech* m) {
  if (!m) return;
  net_mech_unlist(m);
  m->release();
  delete m;
}

}  // namespace mech3
