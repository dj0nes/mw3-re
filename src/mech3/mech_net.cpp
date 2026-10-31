#include "mech3/mech_net.h"

#include <cstring>

#include "gamez/zdeclient/zdec_crater.h"
#include "gamez/zeffect/zeff_anim_save.h"
#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zutil/ztimer.h"
#include "mech3/mech_config.h"
#include "mech3/mech_dead.h"
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
    call(c.create, nullptr, from, msg, size);
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
    case 0xf: return call(c.state, m, from, msg, size);
    case 0x10: return call(c.ping, m, from, msg, size);
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

// Mech3.exe 1.2 @ 0x0046e3a0, with the destructor 0x0046e3c0 (its
// 0x005a05a0, zEntity3d's, is the Mech's release here).
void net_mech_delete(NetMech* m) {
  if (!m) return;
  g_net.mechs.remove(m);
  dead_mechs().remove(m);
  m->release();
  delete m;
}

}  // namespace mech3
