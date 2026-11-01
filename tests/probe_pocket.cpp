// The pocket commander's controls (test_pocket.py): mech3/mech_pocket on a
// real Mech and player controls, the view, HUD and panel stood in (logged).
#include <cstdint>
#include <cstring>
#include <vector>

#include "gamez/zinput/zinput.h"
#include "mech3/lance_orders.h"
#include "mech3/mech.h"
#include "mech3/mech_control.h"
#include "mech3/mech_pocket.h"
#include "mech3/zai_brain.h"

using namespace mech3;

namespace {
std::vector<uint32_t> g_log;
const int32_t* g_in;
int g_panels = 0;
uint32_t fbits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
void log2(uint32_t a, uint32_t b) { g_log.push_back(a), g_log.push_back(b); }
bool viewed_hud() { return g_in[2] && g_in[3]; }
// in: 0 screen height, 1 camera mode (-1 none), 2 the mech viewed, 3 a HUD,
// 4 powered down, 5 the instance the controls (0) or another (1), 6 +0x154,
// 7 the key count, 8.. the keys.
void put_inputs(const MechControls& c) {
  g_log.push_back(c.throttle_returns), g_log.push_back(c.throttle_held), g_log.push_back(fbits(c.throttle_target));
  g_log.push_back(c.x5a), g_log.push_back(fbits(c.joy_throttle)), g_log.push_back(c.reverse);
  g_log.push_back(fbits(c.throttle)), g_log.push_back(fbits(c.jump)), g_log.push_back(fbits(c.jj_forward));
  g_log.push_back(fbits(c.jj_strafe)), g_log.push_back(fbits(c.turn)), g_log.push_back(fbits(c.pitch));
  g_log.push_back(fbits(c.twist)), g_log.push_back(c.fire), g_log.push_back(c.joy_throttle_set);
}
}  // namespace

extern "C" {

int32_t probe_pocket_run(const int32_t* in, const float* inputs, uint32_t* out, int32_t cap) {
  g_log.clear();
  g_in = in;
  g_panels = 0;
  PocketCalls& pc = pocket_calls();
  pc = PocketCalls{};
  pc.view_pocket = [](bool on) { log2(1, on); };
  pc.camera_mode = []() { return g_in[1]; };
  pc.set_camera_mode = [](int32_t id) { log2(2, static_cast<uint32_t>(id)); };
  pc.view_state = [](PocketControls&, int32_t v) { log2(3, static_cast<uint32_t>(v)); };
  pc.hud_mode = [](Mech&, bool on) {
    if (viewed_hud()) log2(4, on);
  };
  pc.hud_hold = [](Mech&, bool hold) {
    if (viewed_hud()) log2(7, hold);
  };
  pc.display_width = []() { return g_in[0]; };
  pc.net_element = []() { return 0u; };
  pc.panel_make = [](PocketControls&) -> void* {
    g_log.push_back(5);
    return reinterpret_cast<void*>(static_cast<uintptr_t>(0x5000 + ++g_panels));
  };
  pc.panel_free = [](void*) { g_log.push_back(6); };
  pc.panel_frame = [](PocketControls&, float) { g_log.push_back(10), g_log.push_back(11); };
  pc.panel_pick = [](PocketControls&, int32_t unit) {
    log2(8, static_cast<uint32_t>(unit));
    return true;
  };
  pc.player_keys = [](PocketControls&, int32_t cmd) { log2(9, static_cast<uint32_t>(cmd)); };

  Mech m;
  m.powered_down = in[4] != 0;
  auto* prev = new PlayerControls();
  prev->construct(1);
  const float* f = inputs;
  prev->throttle_returns = f[0] != 0, prev->throttle_held = f[1] != 0, prev->throttle_target = f[2];
  prev->x5a = f[3] != 0, prev->joy_throttle = f[4], prev->reverse = f[5] != 0, prev->throttle = f[6];
  prev->jump = f[7], prev->jj_forward = f[8], prev->jj_strafe = f[9], prev->turn = f[10], prev->pitch = f[11];
  prev->twist = f[12], prev->fire = static_cast<uint8_t>(f[13]), prev->joy_throttle_set = f[14] != 0;
  prev->owner = &m;
  m.controls = prev;
  PlayerControls other;
  PlayerControls*& inst = player_controls_instance();
  PlayerControls* const saved_inst = inst;
  inst = in[5] ? &other : prev;
  auto id = [&](const void* p) -> uint32_t { return p == prev ? 1u : p == &other ? 2u : p ? 3u : 0u; };

  MechControls* r = lance_dispatch(prev, 2);
  PocketControls* p = r ? r->pocket() : nullptr;
  g_log.push_back(0x20), g_log.push_back(p ? 1 : 0);
  if (p) {
    g_log.push_back(static_cast<uint32_t>(p->saved_camera)), g_log.push_back(id(p->prev));
    g_log.push_back(id(p->prev_instance)), g_log.push_back(static_cast<uint32_t>(p->order));
    g_log.push_back(static_cast<uint32_t>(p->panel_x)), g_log.push_back(static_cast<uint32_t>(p->panel_y));
    g_log.push_back(p->panel ? 1 : 0), g_log.push_back(inst == p), g_log.push_back(pocket_globals().active == p);
    put_inputs(*p);
    p->x154 = static_cast<uint32_t>(in[6]);
    for (int k = 0; k < in[7]; ++k) p->command(in[8 + k]);
    g_log.push_back(0x21);
    p->update(1.0f / 30);
    g_log.push_back(0x22);
    g_log.push_back(fbits(p->jump_target)), g_log.push_back(p->jump_held), g_log.push_back(fbits(p->jj_strafe));
    g_log.push_back(fbits(p->jj_forward)), g_log.push_back(p->fire);
    MechControls* back = lance_dispatch(p, 2);
    g_log.push_back(0x23), g_log.push_back(id(back)), g_log.push_back(id(inst));
    g_log.push_back(pocket_globals().active ? 1 : 0), g_log.push_back(prev->owner == &m);
    put_inputs(*prev);
  }
  inst = saved_inst;
  m.controls = nullptr;
  delete prev;
  pc = PocketCalls{};
  const int32_t n = static_cast<int32_t>(g_log.size());
  if (n > cap) return -1;
  std::memcpy(out, g_log.data(), 4 * g_log.size());
  return n;
}

}  // extern "C"

namespace {
// The orders over the units picked (test_pocket_orders.py).
Mech* g_umech[4];
uint32_t g_uhandle[5];  // the four units' mechs, then the target (the emulator's addresses)
float g_upos[5][3];
int32_t g_ubrain[4], g_answers[16], g_nanswer = 0, g_upicked[4], g_uhas[4];
ai::Contact g_upc[4];
uint32_t uindex(uint32_t h) {
  for (uint32_t i = 0; i < 5; ++i)
    if (g_uhandle[i] == h) return i + 1;
  return 0;
}
struct OrderHooks : ai::BrainHooks {
  zgame::Handle vehicle_handle(void* v) override {
    for (int i = 0; i < 4; ++i)
      if (g_umech[i] == v) return g_uhandle[i];
    return 0;
  }
  void position(zgame::Handle v, float out[3]) override {
    if (const uint32_t i = uindex(v)) std::memcpy(out, g_upos[i - 1], 12);
  }
  ai::AiTarget* targetable(zgame::Handle v) override {
    const uint32_t i = uindex(v);
    return i == 5 ? &g_target_t : nullptr;
  }
  ai::AiTarget g_target_t;
};
OrderHooks g_ohooks;
}  // namespace

extern "C" {

// in: the order, on a target (0/1), the count, each unit (4: present, picked,
// a brain, a contact on the player), [0x00605f58] the target (0/1), each
// mate's allies and enemies (4 x 2), the target's team/allies/enemies, the
// answers (8); handles: 5; pos: 15. out: the log (each order: 0x1a, order,
// unit, contact (0x11+unit for the player contact, else 0 and the made one's
// owner, seen, entity, dist2, delta)), then 0x2b, the answer, the target's
// team, allies, enemies.
int32_t probe_pocket_orders(const int32_t* in, const uint32_t* handles, const float* pos, uint32_t* out, int32_t cap) {
  g_log.clear();
  ai::set_brain_hooks(&g_ohooks);
  Mech mechs[4];
  for (int i = 0; i < 5; ++i) g_uhandle[i] = handles[i], std::memcpy(g_upos[i], pos + 3 * i, 12);
  const int32_t count = in[2];
  for (int i = 0; i < 4; ++i) {
    g_umech[i] = &mechs[i];
    g_uhas[i] = in[3 + 4 * i], g_upicked[i] = in[4 + 4 * i], g_ubrain[i] = in[5 + 4 * i];
    mechs[i].player_contact = in[6 + 4 * i] ? &g_upc[i] : nullptr;
    mechs[i].allies = static_cast<uint32_t>(in[20 + 2 * i]), mechs[i].enemies = static_cast<uint32_t>(in[21 + 2 * i]);
  }
  pocket_globals().marker = in[19] ? handles[4] : 0x1234;
  g_ohooks.g_target_t.team = static_cast<uint32_t>(in[28]);
  g_ohooks.g_target_t.allies = static_cast<uint32_t>(in[29]), g_ohooks.g_target_t.enemies = static_cast<uint32_t>(in[30]);
  for (int k = 0; k < 8; ++k) g_answers[k] = in[31 + k];
  g_nanswer = 0;
  PocketCalls& pc = pocket_calls();
  pc = PocketCalls{};
  pc.view_state = [](PocketControls&, int32_t) {};
  static int32_t s_count;
  s_count = count;
  pc.panel_units = [](PocketControls&, PocketUnit* o, int32_t cap) -> int32_t {
    const int32_t n = s_count < cap ? s_count : cap;
    for (int32_t i = 0; i < n; ++i) {
      o[i] = PocketUnit{};
      if (i < 4 && g_uhas[i]) o[i].picked = g_upicked[i] != 0, o[i].mech = g_umech[i];
    }
    return n;
  };
  LanceCalls& lc = lance_calls();
  lc.brain = [](Mech& m) -> ai::Brain* {
    for (int i = 0; i < 4; ++i)
      if (g_umech[i] == &m && g_ubrain[i]) return reinterpret_cast<ai::Brain*>(static_cast<uintptr_t>(0x200 + i));
    return nullptr;
  };
  lc.order = [](ai::Brain& b, int32_t order, ai::Contact* c) -> int32_t {
    const uint32_t unit = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&b) - 0x200);
    g_log.push_back(0x1a), g_log.push_back(static_cast<uint32_t>(order == 12 ? 7 : order)), g_log.push_back(unit);
    int k = -1;
    for (int i = 0; i < 4; ++i)
      if (c == &g_upc[i]) k = i;
    if (k >= 0) {
      g_log.push_back(0x11 + static_cast<uint32_t>(k));
    } else {
      g_log.push_back(0), g_log.push_back(uindex(c->owner)), g_log.push_back(uindex(c->seen));
      g_log.push_back(uindex(c->entity)), g_log.push_back(fbits(c->dist2));
      for (int i = 0; i < 3; ++i) g_log.push_back(fbits(c->delta[i]));
    }
    return g_answers[g_nanswer++ & 7];
  };
  PocketControls p(nullptr, nullptr, 2, 1);
  const int32_t r = pocket_order(p, in[0], in[1] ? handles[4] : 0, in[1] != 0);
  g_log.push_back(0x2b), g_log.push_back(static_cast<uint32_t>(r));
  g_log.push_back(g_ohooks.g_target_t.team), g_log.push_back(g_ohooks.g_target_t.allies);
  g_log.push_back(g_ohooks.g_target_t.enemies);
  lc = LanceCalls{};
  pc = PocketCalls{};
  ai::set_brain_hooks(nullptr);
  for (Mech& m : mechs) m.player_contact = nullptr;
  const int32_t n = static_cast<int32_t>(g_log.size());
  if (n > cap) return -1;
  std::memcpy(out, g_log.data(), 4 * g_log.size());
  return n;
}

}  // extern "C"

namespace {
// The mode's own keys (test_pocket_keys.py).
const int32_t kKeyCmds[17] = {0x18, 0x19, 2, 3, 0xc, 0xd, 0xe, 0xf, 0x10, 0x11, 0x12, 0x14, 8, 9, 4, 5, 0x3d};
int32_t g_cmd_state[17];
int32_t g_mw2, g_busy;
zin::MouseInfo g_mouse_info;
int g_screen_token;
}  // namespace

extern "C" {

// in: powered down, jets, airborne, MW2 jets, a HUD, its zoom locked, flags,
// view, joy set, jump held, throttle held, throttle returns, reverse, the
// 17 commands' states; f: dt, the mouse fx fy fdx fdy, look x y dx dy,
// throttle turn pitch twist jump, the three targets, joy throttle, the
// mech's three rates. out: the log (0x31 tick, 0x32 zoom v, 0x33 mouse set
// x y), 0x40, then the fields.
int32_t probe_pocket_keys(const int32_t* in, const float* f, uint32_t* out, int32_t cap) {
  g_log.clear();
  ControlsCalls& k = controls_calls();
  const ControlsCalls saved = k;
  for (int i = 0; i < 17; ++i) g_cmd_state[i] = in[13 + i];
  g_mw2 = in[3], g_busy = in[5];
  k.command = [](int cmd) -> uint32_t {
    for (int i = 0; i < 17; ++i)
      if (kKeyCmds[i] == cmd) return static_cast<uint32_t>(g_cmd_state[i]);
    return 0;
  };
  g_mouse_info = zin::MouseInfo{};
  g_mouse_info.fx = f[1], g_mouse_info.fy = f[2], g_mouse_info.fdx = f[3], g_mouse_info.fdy = f[4];
  k.mouse_info = []() -> const void* { return &g_mouse_info; };
  k.mouse_set = [](float x, float y) { g_log.push_back(0x33), g_log.push_back(fbits(x)), g_log.push_back(fbits(y)); };
  k.mw2_jets = [] { return g_mw2 != 0; };
  k.pilot_tick = [](MechControls&) { g_log.push_back(0x31); };
  k.screen_busy = [](void*) -> uint8_t { return static_cast<uint8_t>(g_busy); };
  k.zoom = [](void*, bool in) { g_log.push_back(0x32), g_log.push_back(in ? 1 : 0); };
  PocketCalls& pc = pocket_calls();
  pc = PocketCalls{};
  pc.view_state = [](PocketControls&, int32_t) {};
  pc.view_screen = in[4] ? +[](Mech&) -> void* { return &g_screen_token; } : nullptr;
  Mech m;
  m.powered_down = in[0] != 0;
  m.power_down_pending = false;
  m.equipment[11] = in[1];
  m.no_power_down = in[2] != 0;
  m.throttle_up = f[18], m.throttle_down = f[19], m.turn_accel = f[20];
  PocketControls p(nullptr, nullptr, 2, 1);
  p.owner = &m;
  p.flags = static_cast<uint32_t>(in[6]), p.view = in[7];
  p.joy_throttle_set = in[8] != 0, p.jump_held = in[9] != 0, p.throttle_held = in[10] != 0;
  p.throttle_returns = in[11] != 0, p.reverse = in[12] != 0;
  p.look_x = f[5], p.look_y = f[6], p.look_dx = f[7], p.look_dy = f[8];
  p.throttle = f[9], p.turn = f[10], p.pitch = f[11], p.twist = f[12], p.jump = f[13];
  p.throttle_target = f[14], p.turn_target = f[15], p.jump_target = f[16], p.joy_throttle = f[17];
  p.keys(f[0]);
  g_log.push_back(0x40);
  for (float v : {p.throttle, p.turn, p.pitch, p.twist, p.jump, p.jj_strafe, p.jj_forward, p.throttle_target,
                  p.turn_target, p.jump_target, p.look_x, p.look_y, p.look_dx, p.look_dy})
    g_log.push_back(fbits(v));
  g_log.push_back(p.fire), g_log.push_back(p.joy_throttle_set), g_log.push_back(p.throttle_held);
  g_log.push_back(p.reverse);
  p.owner = nullptr;
  k = saved;
  pc = PocketCalls{};
  const int32_t n = static_cast<int32_t>(g_log.size());
  if (n > cap) return -1;
  std::memcpy(out, g_log.data(), 4 * g_log.size());
  return n;
}

}  // extern "C"
