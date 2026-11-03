#include "mech3/mech.h"

#include <cmath>

#include "crt/msvc_rand.h"
#include "gamez/zclass/class.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zutil/zfix.h"
#include "mech3/mech_skis.h"
#include "mech3/mission_world.h"
#include "mech3/weapon.h"
#include "mech3/cheats.h"

namespace mech3 {

namespace {
World g_world;
HeatCalls g_calls;

constexpr float kHeatMax = 2.0f;        // 0x005bcd78
constexpr float kCritical = 2.0f;       // 0x005e1fd8
constexpr float kOverheat = 1.5f;       // 0x005e1fdc
constexpr float kShutdownWarn = 0.95f;  // 0x005e1fe0
constexpr float kShutdownSafe = 0.9f;   // 0x005e1fe4
constexpr float kBeepOn = 0.7f;         // 0x005e1fec
constexpr float kBeepOff = 0.7f;        // 0x005e1ff0
constexpr float kRecover = 0.4f;        // 0x005e1ff4
constexpr float kHazeStart = 0.8f;      // 0x005bcd90
constexpr int kLava = 8;
constexpr int kMultiplayer = 3;  // docs/gametypes.md

constexpr int kSoundBeep = 5, kSoundAlarm = 6, kSoundShutdownWarning = 0x14, kSoundShutdown = 0x15;
constexpr int kMechSoundAmmo = 15, kMechSoundCritical = 26;
constexpr int kMessageHeatDeathDisabled = 0x20c;  // Mech3Msg.dll

float rand_unit() { return static_cast<float>(crt::msvc_rand()) * 3.051851e-05f; }  // 0x005bcd94

void stop(Sound& s) {
  if (s) {
    if (g_calls.stop_sound) g_calls.stop_sound(s);
    s = nullptr;
  }
}

Sound play(Mech& m, int id) { return g_calls.play_sound ? g_calls.play_sound(m, id) : nullptr; }
int game_type() { return g_calls.game_type ? g_calls.game_type() : 0; }

// Torso vtable +0x20 (explode, 1); 0x0041cfa0(1) is the same with explode 0.
void torso_hit(Mech& m, int explode) {
  if (g_calls.torso_hit) g_calls.torso_hit(m, explode);
  else if (explode) m.torso->destroy(1, 1);
  else m.destroy_part(kTorso);
}

uint32_t beep_state(uint32_t s) { return s & 7; }
uint32_t next_beep(uint32_t s) { return (s & ~7u) | ((s + 1) & 7); }
uint32_t next_shutdown(uint32_t s, int step) { return (s & ~0x38u) | ((s + 8 * step) & 0x38); }
}  // namespace

World& world() { return g_world; }
HeatCalls& heat_calls() { return g_calls; }

// Mech3.exe 1.2 @ 0x0045fb90
float HeatSinks::rate() const {
  return static_cast<float>(count * 0.1 * (doubles ? 2.5f : 1.25f));  // 0x005e4ee8, 0x005e4eec
}

// Mech3.exe 1.2 @ 0x0045fbc0. Always false, so the "heat sink lost" callout
// never plays (docs/heat.md).
bool HeatSinks::lose(int32_t n) {
  count -= n;
  if (count < 0) count = 0;
  return false;
}

// Mech3.exe 1.2 @ 0x0041cc10
bool Mech::is_powered_down() const { return power_down_pending || powered_down; }

// +0x37c's vt+0x4c, Stop (0x00426c10): the hook when set (the tests), else the port.
void Mech::stop_controls(int arg) {
  if (g_calls.stop_controls) g_calls.stop_controls(*this, arg);
  else if (controls) controls->halt();
}

// Mech3.exe 1.2 @ 0x0041cc30. Ignored during a heat shutdown; a power-down
// only asks, and finish_power_down completes it when the mech can stop.
void Mech::power(int power_down) {
  if (heat_shutdown) return;
  if (power_down) {
    if (no_power_down) return;
    power_down_pending = true;
    stop_controls(power_down);
    return;
  }
  if (!is_powered_down()) return;
  stop_controls(0);
  void* screen = this == g_world.player && g_calls.screen ? g_calls.screen(*this) : nullptr;
  if (screen && g_calls.screen_power_up) g_calls.screen_power_up(screen, hud_saved);
  if ((detail & 1) && g_calls.play_sound_at) power_up_sound() = g_calls.play_sound_at(*this, 7);
  powered_down = false;
  power_down_pending = false;
}

// Mech3.exe 1.2 @ 0x0041cac0
void Mech::finish_power_down() {
  if (!power_down_pending) return;
  const bool can_stop = anim_state == 1 || anim_state == 12 || anim_state == 13;
  if (!can_stop || no_power_down) {
    if (anim_state == 10) power_down_pending = false;
    return;
  }
  powered_down = true;
  power_down_pending = false;
  vec_3c = {0, 0, 0};  // 0x0063d870
  void* screen = this == g_world.player && g_calls.screen ? g_calls.screen(*this) : nullptr;
  if (screen && g_calls.screen_power_down) hud_saved = g_calls.screen_power_down(screen);
  for (int i = 0; i < 3; ++i) stop(loops()[i]);
  if ((detail & 1) && g_calls.play_sound_at) power_down_sound() = g_calls.play_sound_at(*this, 8);
}

// Mech3.exe 1.2 @ 0x0041d4f0
Foot* Mech::foot(int leg) const { return legs[leg] ? legs[leg]->foot : nullptr; }

// Mech3.exe 1.2 @ 0x0041d4d0
Calf* Mech::calf(int leg) const { return legs[leg] ? legs[leg]->calf : nullptr; }

// Mech3.exe 1.2 @ 0x00419190: the view's mech only; on, while the beep
// sounds (state bits 0-1), sound 0x16 the first time and bit 24 set.
void Mech::heat_override(bool on) {
  if (this != g_world.player) return;
  if (!on) {
    heat_state &= ~0x1000000u;
  } else if (heat_state & 3) {
    if (!(heat_state & 0x1000000u)) sounds[22] = play(*this, 0x16);  // +0x610
    heat_state |= 0x1000000u;
  }
}

// Mech3.exe 1.2 @ 0x00419e60: the coolant doubled and spent on the heat, what
// is left halved, so each flush takes up to twice what is left
// (docs/heat.md, Coolant flush). Mech sound 0x11 for the pilot's mech.
void Mech::flush_coolant() {
  if (coolant == 0.0f || heat == 0.0f) return;
  const float c = coolant + coolant;
  coolant = c;
  const float spent = heat < c ? heat : c;
  heat -= spent;
  coolant = (c - spent) * 0.5f;
  if (this == g_world.mission_player && g_calls.play_mech_sound) g_calls.play_mech_sound(0x11);
}

// Mech3.exe 1.2 @ 0x00419200: the AMS's frame. With a player in play, an AMS
// fitted (+0x3ac) and on: the nearest shot in range from the mech's place
// raised by its height (+0x68), the shots not hostile to the player when the
// player's enemies (+0xc8) hold this mech's team (an enemy's AMS), else the
// hostile ones; one found is fired at, the period shared over the AMS
// fitted (docs/weapons.md, The AMS). Playtest W18.
bool update_ams(Mech& m) {
  Mech* p = g_world.mission_player;
  if (!p || !(m.equipment[0] > 0) || !m.ams || !m.ams->on) return false;
  const Vec3 c = skis_calls().position ? *skis_calls().position(m)
                 : m.transform           ? Vec3{m.transform[9], m.transform[10], m.transform[11]}
                                         : Vec3{0, 0, 0};
  const zwep::Vec3f3 at{c.x, m.height + c.y, c.z};
  const int32_t side = (p->enemies & m.team) ? 0 : 1;  // vt+0x24's vt+0x28's +0xc0
  if (!m.ams->find(side, &at)) return false;
  m.ams->fire(m.equipment[0]);
  return true;
}

// Mech3.exe 1.2 @ 0x004192f0. Walkthrough and thresholds: docs/heat.md.
void update_heat(Mech& m, float dt) {
  World& w = g_world;
  const bool player = &m == w.player;
  const bool full = w.full_heat_all_mechs || player;

  float rate = 1.0f;  // 0x005e1fd4
  if (m.powered_down) rate *= 3.0f;
  if (m.foot(0)->in_water) {
    // Quirk: max(depth / height, 1) makes (3f + 1)^2 at least 16, so the cap
    // always wins and wading is a flat x8.
    float f = m.foot(0)->water_depth / m.height;
    if (f < 1.0f) f = 1.0f;
    f = (f * 3.0f + 1.0f) * (f * 3.0f + 1.0f);
    if (!(f <= 8.0f)) f = 8.0f;
    rate *= f;
  }
  m.heat_dissipation = rate * environment().inv_temperature;  // 0x005e5d40: 1 / the mission's ambient temperature

  if (full) {
    const float t = 1.6f - std::fabs(g_calls.throttle ? g_calls.throttle(m) : 0.0f);
    if (t < 1.0f) m.heat_dissipation *= t;
    if (m.jump_jets_firing) m.heat_added += dt * 0.05f;
    if (m.masc_time != 0.0f) m.heat_added += dt * 0.05f;
  }
  // The exe adds 0.05 a frame. FIX (B): 0.05 per 1/30 s (docs/heat.md, Frame rate).
  if (m.foot(0)->soil == kLava || m.foot(1)->soil == kLava)
    m.heat_added += zfix::options().per_second ? dt * (0.05f * zfix::kReferenceFps) : 0.05f;
  m.heat_dissipation *= m.heat_sinks.rate();
  m.heat_dissipation *= cheat_rate(&m == g_world.mission_player);  // ours: operation cwal (cheats.h)

  if ((g_calls.heat_option && g_calls.heat_option()) || game_type() == kMultiplayer) {
    m.heat -= dt * m.heat_dissipation;
    if (!(m.heat <= kHeatMax)) m.heat = kHeatMax;
    else if (m.heat < 0.0f) m.heat = 0.0f;
    m.heat += m.heat_added;
    if (!(m.heat <= kHeatMax)) m.heat = kHeatMax;
    else if (m.heat < 0.0f) m.heat = 0.0f;
  } else {
    m.heat_added = 0.0f;
    m.heat = 0.0f;
  }

  void* screen = player && g_calls.screen ? g_calls.screen(m) : nullptr;
  if (screen && m.heat >= kHazeStart && !w.no_heat_haze && g_calls.heat_haze)
    g_calls.heat_haze(screen, (m.heat - kHazeStart) * 5.0f, 5, w.dt);

  if (!full) {
    if (m.heat >= kCritical && game_type() != kMultiplayer) torso_hit(m, 1);
    m.heat_added = 0.0f;
    return;
  }

  if (m.is_powered_down()) m.heat_state = (m.heat_state & ~kOverride) | kPoweredDown;
  else m.heat_state &= ~kPoweredDown;
  if (m.heat < kRecover) m.heat_state &= ~kOverride;

  // The warning beep: 0 idle, 1 just started, 2 waiting, 3 sounding; 4-7 cancel.
  for (bool again = true; again;) {
    again = false;
    if (m.heat_state & 0x7e000000) m.heat_state |= 4;
    uint32_t& s = m.heat_state;
    switch (beep_state(s)) {
      case 0:
        if (m.heat > kBeepOn && w.time > m.beep_time) {
          if (player) m.heat_beep() = play(m, kSoundBeep);
          again = true;
          s = next_beep(s);
        }
        break;
      case 1:
        m.beep_time = w.time + 2.0f;
        s = next_beep(s);
        break;
      case 2:
        if (w.time > m.beep_time) s = next_beep(s);
        break;
      case 3:
        if (m.heat < kBeepOff) {
          m.beep_time = w.time + 2.0f;
          if (player) stop(m.heat_beep());
          s &= ~7u;
        }
        break;
      case 4:
        s &= ~7u;
        break;
      default:
        m.beep_time = w.time + 2.0f;
        stop(m.heat_beep());
        s &= ~7u;
        break;
    }
  }

  // Shutdown, in bits 3-5: 0 normal, 1 warned, 2 shutting down, 3 shut down,
  // 4 critical. The top bits change what each state does.
  for (bool again = true; again;) {
    again = false;
    bool critical = false, over = false;
    uint32_t& s = m.heat_state;
    if (!(m.heat < kCritical)) {
      stop(m.heat_beep());
      stop(m.heat_alarm());
      if (player && g_calls.play_mech_sound) g_calls.play_mech_sound(kMechSoundCritical);
      m.shutdown_time = 0.0f;
      s &= ~kPoweredDown;
      s = (s & ~0x18u) | 0x20;
      critical = true;
    } else if (!m.is_powered_down() && !(m.heat < kOverheat)) {
      stop(m.heat_beep());
      stop(m.heat_alarm());
      over = true;
      s = (s & ~0x28u) | 0x10;
    }

    const uint32_t key = (s & 0x7f000000) | ((s >> 3) & 7);
    auto warn = [&] {
      if (m.heat > kShutdownWarn && w.time > m.voice_time) {
        m.voice_time = w.time + 4.0f;
        m.shutdown_time = w.time + 4.0f;
        if (player) m.shutdown_warning() = play(m, kSoundShutdownWarning);
        s = next_shutdown(s, 1);
      }
    };
    auto reset = [&] {
      stop(m.heat_alarm());
      s &= ~0x38u;
    };
    switch (key) {
      case 0:
      case kOverride:
        warn();
        break;
      case 1:
      case kOverride | 1:
        if (m.heat <= kShutdownSafe) {
          m.heat_shutdown = false;
          s &= ~kOverride;
          s &= ~0x38u;
        } else if (w.time > m.shutdown_time) {
          m.shutdown_time = w.time + 3.0f;
          again = true;
          s = next_shutdown(s, 1);
        }
        break;
      case kOverride | 2:
        if (!over) {
          s = next_shutdown(s, 1);
          break;
        }
        [[fallthrough]];
      case 2:
        m.voice_time = w.time + 4.0f;
        if (player) m.shutdown_voice() = play(m, kSoundShutdown);
        g_calls.power ? g_calls.power(m, 1) : m.power(1);
        m.heat_shutdown = true;
        s = next_shutdown(s, 1);
        break;
      case 3:
      case kOverride | 3:
        if (m.heat <= kShutdownSafe) {
          m.heat_shutdown = false;
          s &= ~kPoweredDown;
          reset();
        } else if (w.time > m.shutdown_time) {
          m.shutdown_time = w.time + (0.1f + rand_unit() * 2.9f);
          if (player && !m.heat_alarm()) m.heat_alarm() = play(m, kSoundAlarm);
          s = next_shutdown(s, 1);
        }
        break;
      case 4:
      case kOverride | 4:
        if (!(w.time > m.shutdown_time)) break;
        if (!critical && rand_unit() < 0.8f) {
          // Ride it out: maybe an ammo bin cooks off, then back to state 3.
          if (g_calls.ammo_explosion && g_calls.ammo_explosion(m) && player && g_calls.play_mech_sound)
            g_calls.play_mech_sound(kMechSoundAmmo);
          m.shutdown_time = w.time + 3.0f;
          s = next_shutdown(s, -1);
          break;
        }
        m.heat_shutdown = false;
        if (!m.invincible) {
          if (critical && game_type() != kMultiplayer) torso_hit(m, 1);
          else torso_hit(m, 0);  // 0x0041cfa0(1)
          m.heat = 0.0f;
          s = next_shutdown(s, -1);
        } else {
          if (g_calls.hud_message) g_calls.hud_message(2.0f, kMessageHeatDeathDisabled);
          m.voice_time = w.time + 8.0f;
          if (player) {
            stop(m.heat_beep());
            reset();
          } else {
            s &= ~0x38u;
          }
        }
        break;
      case kPoweredDown | 1:
      case kPoweredDown | kOverride | 1:
        s &= ~0x38u;
        break;
      case kPoweredDown | 2:
      case kPoweredDown | kOverride | 2:
        s &= ~0x38u;
        warn();
        break;
      case kPoweredDown | 3:
      case kPoweredDown | kOverride | 3:
        if (m.heat <= kRecover && w.time > m.shutdown_time) {
          m.heat_shutdown = false;
          g_calls.power ? g_calls.power(m, 0) : m.power(0);
          s &= ~0x38u;
        }
        break;
      case kPoweredDown | 4:
      case kPoweredDown | kOverride | 4:
        m.heat_shutdown = false;
        reset();
        break;
      default:
        break;
    }
  }
  m.heat_added = 0.0f;
}

namespace {
AimCalls g_aim;
}
AimCalls& aim_calls() { return g_aim; }

// Mech3.exe 1.2 @ 0x0041bc20. A point behind the torso's front (its half
// depth along the mech's facing, +0x18) clears the aim. In the cockpit view
// the facing test is skipped and the exe hands the torso an unset direction
// (here zero; the torso's aim 0x00465ea0 is not ported yet).
bool Mech::aim_at(const Vec3* at, void* torso_out, bool check) {
  auto* wm = static_cast<WeaponManager*>(ammo);
  auto torso_aim = [&](const Vec3* d) { return g_aim.torso_aim ? g_aim.torso_aim(*this, d, torso_out, 1) : 0; };
  if (!at) {
    if (wm) wm->set_aim(nullptr);
    return torso_aim(nullptr) == 1;
  }
  bool r = false;
  if (check) {
    Vec3 d{};
    if (!(g_aim.cockpit_view && g_aim.cockpit_view(*this))) {
      float p[3] = {0, 0, 0};
      zcls::node_world_position(static_cast<zcls::Node*>(node), p);  // 0x004ed530
      d = {at->x - p[0], at->y - p[1], at->z - p[2]};
      const float f = (d.z * x18.z + d.y * x18.y) + d.x * x18.x;
      if (f < (torso ? torso->half_depth : 0.0f)) {
        if (wm) wm->set_aim(nullptr);
        return false;
      }
    }
    zmth::Vec3 n{d.x, d.y, d.z};
    zmth::normalize(n);
    const Vec3 dir{n.x, n.y, n.z};
    r = torso_aim(&dir) == 1;
  }
  if (wm) wm->set_aim(reinterpret_cast<const Vec3f*>(at));
  return r;
}

}  // namespace mech3
