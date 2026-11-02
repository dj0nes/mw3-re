#include "mech3/mech_command.h"

#include <cstring>

#include "gamez/zclass/class.h"
#include "mech3/mech.h"
#include "mech3/mech_collide.h"
#include "mech3/lance_orders.h"
#include "mech3/mech_control.h"
#include "mech3/mech_move.h"
#include "mech3/weapon.h"

// Original file: mech3\mech.cpp (0x0041be80 sits between it and
// mech_anim.cpp). The commands' ids: docs/controls.md, the binding table.
namespace mech3 {

namespace {
CommandCalls g_calls;

constexpr float kRangePassive = 500.0f;    // 0x005e8d80
constexpr float kRange = 1000.0f;          // 0x005e8d84
constexpr float kRangeSensors = 2000.0f;   // 0x005e8d88, with a C3 computer (equipment kind 6)
constexpr float kAlphaSeconds = 0.2f;      // 0x3e4ccccd
constexpr int kSoundJettison = 0x10;       // 0x0041e770's ids
constexpr int kSoundZoom = 0x17;           // 0x00432990's

void record(const char* key, int32_t cmd) {
  if (g_calls.unported) g_calls.unported(key, cmd);
}

void hud(void* screen, int32_t cmd) {
  if (g_calls.hud) g_calls.hud(screen, cmd);
  else record("the HUD's command", cmd);
}

void other(Mech& m, int32_t cmd, const char* key) {
  if (g_calls.other) g_calls.other(m, cmd);
  else record(key, cmd);
}

void view_camera(int32_t mode) {
  if (g_calls.view_camera) g_calls.view_camera(mode);
  else record("view +0x58 vtable +0x10", mode);
}

void controls_view(Mech& m, int32_t mode) {  // 0x00426780
  if (update_calls().controls_view) update_calls().controls_view(*m.controls, mode);
  else m.controls->set_view(mode);
}

void set_view_option(Mech& m) {
  if (g_calls.set_view_option) g_calls.set_view_option(m.controls->view);
}

uintptr_t p(const void* v) { return reinterpret_cast<uintptr_t>(v); }
uintptr_t fbits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

// The mech's and its manager's own calls, which the differential test logs.
template <typename F>
uintptr_t own(uint32_t addr, uintptr_t a, uintptr_t b, F real) {
  if (g_calls.own) return g_calls.own(addr, a, b);
  return real();
}
}  // namespace

CommandCalls& command_calls() { return g_calls; }

namespace {
Mech* target_mech(const Mech& m) {  // 0x004e6ff0 on +0x6c
  return m.target && collide_calls().contact_mech ? collide_calls().contact_mech(m.target) : nullptr;
}
}  // namespace

// Mech3.exe 1.2 @ 0x0041d6f0: where the chosen part is. The side torsos aim
// at 0.75 of a half width out along the target's torso node; the width is this
// mech's own torso's (+0x39c on ESI), not the target's. A part gone gives the
// target's position and drops the choice.
bool Mech::aim_part() {
  if (!target || xf8 == -1) return false;
  Mech* tm = target_mech(*this);
  MechPart* p = tm ? part_by_id(*tm, xf8) : nullptr;
  if (!p) {
    if (target->seen) {
      xf8 = -1;
      if (const float* at = g_calls.seen_position ? g_calls.seen_position(target->seen) : nullptr)
        part_aim = Vec3{at[0], at[1], at[2]};
    }
    return false;
  }
  if (xf8 == 7 || xf8 == 8) {
    part_aim = Vec3{torso->half_width * (xf8 == 8 ? 0.75f : -0.75f), 0.0f, 0.0f};  // 0x005bce50, 0x005bce54
    if (part_calls().world_point) part_calls().world_point(p->node, &part_aim);
    else zcls::node_world_point(static_cast<zcls::Node*>(p->node), &part_aim.x);  // 0x004ed650
  } else if (part_calls().world_position) {
    part_calls().world_position(p->node, &part_aim);
  } else {
    zcls::node_world_position(static_cast<zcls::Node*>(p->node), &part_aim.x);  // 0x004ed530
  }
  return true;
}

// Mech3.exe 1.2 @ 0x0041d810: with a targeting computer (equipment kind 14,
// `tc`) and a target, a part it still has; the same key again, or no such
// part, drops the choice.
void Mech::target_part(int32_t id) {
  int32_t want = -1;
  if (equipment[14] > 0 && target && target_mech(*this)) {
    const MechPart* p = part_by_id(*target_mech(*this), id);
    if (p && !p->destroyed) want = id;
  }
  xf8 = want == xf8 ? -1 : want;
  aim_part();
}

// Mech3.exe 1.2 @ 0x0041be80: vtable +0xfc, the commands the player's keys
// send the vehicle the view looks at (0x00426cb0). A mech not active (+0x08)
// takes only the second group (the controls' view, the HUD's zoom, the lock
// cleared). Every command ends in the controls' vtable +0 with it, which
// does nothing (0x004a5bc0) for each controls class: left out.
void Mech::command(int32_t cmd) {
  HeatCalls& hc = heat_calls();
  const bool viewed = this == world().player;
  void* screen = viewed && hc.screen ? hc.screen(*this) : nullptr;
  auto* wm = static_cast<WeaponManager*>(ammo);
  auto down = [&] { return own(0x41cc10, 0, 0, [&] { return uintptr_t{is_powered_down()}; }) != 0; };  // vt+0xa0
  auto select = [&](int32_t g) {  // 0x004b0730
    return (own(0x4b0730, p(wm), g, [&] { return uintptr_t{wm->select_group(g)}; }) & 0xff) != 0;
  };
  auto into_group = [&](Weapon* w) {  // the selected group takes the weapon (0x004b1de0)
    WeaponGroup* grp = wm->group_ptr();
    own(0x4b1de0, p(grp), p(w), [&] {
      if (grp) grp->select(w);
      return uintptr_t{0};
    });
  };
  auto target_set = [&](ai::Contact* c) {  // vtable +0xc0 (0x0041d890): AiTarget's, then the HUD's
    own(0x41d890, p(c), 0, [&] {
      set_target(c, false);
      if (screen) hud(screen, 0x49);
      return uintptr_t{0};
    });
  };
  // Z and Q: the object under the reticle becomes the target when it is among
  // the contacts, else the target is cleared (0x004a7b40); Z skips allies.
  auto pick_aimed = [&](bool zoom) {
    void* node = nullptr;
    if (g_calls.hud_aimed_node) node = g_calls.hud_aimed_node(screen);
    else record("0x00444330", cmd);
    const uint32_t e = g_calls.node_entity ? g_calls.node_entity(node) : 0;
    if (!e) return;
    if (zoom && ((g_calls.entity_team ? g_calls.entity_team(e) : 0) & allies)) return;
    if (g_calls.entity_refuses && g_calls.entity_refuses(e, zoom)) return;
    if (target && target->seen == e) return;  // 0x004a7b40
    ai::Contact* found = nullptr;
    for (ai::Contact* c : contacts)
      if (c->seen == e) {
        found = c;
        break;
      }
    target_set(found);  // 0x004a7ba0
  };
  if (active) {
    switch (cmd) {
      case 6:  // CENTER_TORSO, CENTER_LEGS
      case 7:
        if (g_calls.torso_center) g_calls.torso_center(*this, cmd == 6 ? 1 : 2);
        else torso->centre(cmd == 6 ? 1 : 2);
        break;
      case 0x13:  // crouch
        if (!down()) own(0x41bda0, !crouched, 0, [&] { return crouch(!crouched), uintptr_t{0}; });
        break;
      case 0x15:  // ZOOM_LOCK (the HUD, even off the view), its sound kept at +0x614; then RETICULE_ZOOM's
        hud(screen, cmd);
        sounds[23] = hc.play_sound ? hc.play_sound(*this, kSoundZoom) : nullptr;
        [[fallthrough]];
      case 0x14:  // RETICULE_ZOOM
        if (screen) pick_aimed(true);
        break;
      case 0x25:
        own(0x4b0790, p(wm), 0, [&] { return wm->next_weapon(), uintptr_t{0}; });
        break;
      case 0x26:
        own(0x4b07e0, p(wm), 0, [&] { return wm->prev_weapon(), uintptr_t{0}; });
        break;
      case 0x27:  // NEXT_WEAPON: the weapon the HUD highlights, its group selected, it selected there
        if (screen) {
          if (!g_calls.hud_weapon) record("the HUD's weapon", cmd);
          auto* w = static_cast<Weapon*>(g_calls.hud_weapon ? g_calls.hud_weapon(screen) : nullptr);
          if (w && select(w->group)) into_group(w);
        }
        break;
      case 0x28:  // the fire mode: chain, single, group
        wm->mode = (wm->mode + 1) % 3;
        select(wm->group);
        own(0x4b0830, p(wm), 0, [&] { return wm->mode_sound(), uintptr_t{0}; });
        break;
      case 0x2a:
        own(0x4b04f0, p(wm), 0, [&] { return uintptr_t{wm->next_group()}; });
        break;
      case 0x2b:
        own(0x4b0610, p(wm), 0, [&] { return uintptr_t{wm->prev_group()}; });
        break;
      case 0x2c: case 0x2d: case 0x2e: case 0x2f: case 0x30:  // F1..F5
        select(cmd - 0x2c);
        break;
      case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: {  // Shift+F1..F5: the current weapon into a group
        const int32_t g = cmd - 0x31;
        auto* w = reinterpret_cast<Weapon*>(own(0x4b04c0, p(wm), 0, [&] { return p(wm->current()); }));
        if (!w) break;
        own(0x4b1e10, p(w), g, [&] { return w->set_group(g), uintptr_t{0}; });
        if (select(g)) into_group(w);
        break;
      }
      case 0x36: case 0x37: case 0x38:  // the op points: the stations' vt+0x60 / +0x64 / +0x68(position)
        if (g_calls.stations && g_calls.stations()) other(*this, cmd, "[0x00609980] vtable +0x60..+0x68");
        break;
      case 0x39:  // alpha strike
        if (!down())
          own(0x4b0a70, p(wm), fbits(kAlphaSeconds), [&] { return wm->alpha_strike(kAlphaSeconds), uintptr_t{0}; });
        break;
      case 0x3a:  // the AMS on or off
        if (ams) ams->on = !ams->on;
        break;
      case 0x3b:  // jettison the ammo
        if (own(0x4b0ae0, p(wm), 0, [&] { return uintptr_t{wm->jettison()}; }) & 0xff)
          if (hc.play_mech_sound) hc.play_mech_sound(kSoundJettison);
        break;
      case 0x3e:  // no key
        other(*this, cmd, "0x0041d070");
        break;
      case 0x3f: {  // power down / up
        const int32_t pd = down() ? 0 : 1;
        own(0x41cc30, pd, 0, [&] { return power(pd), uintptr_t{0}; });
        break;
      }
      case 0x40:  // the shutdown override (docs/heat.md)
        own(0x419190, 1, 0, [&] { return heat_override(true), uintptr_t{0}; });
        break;
      case 0x41:  // coolant flush
        own(0x419e60, 0, 0, [&] { return flush_coolant(), uintptr_t{0}; });
        break;
      case 0x44:
        own(0x41b840, 0, 0, [&] { return get_up(), uintptr_t{0}; });
        break;
      // The target keys, each for side 0.
      case 0x46: own(0x4d6710, 0, 0, [&] { return next_enemy(0), uintptr_t{0}; }); break;     // E
      case 0x47: own(0x4d64b0, 0, 0, [&] { return next_ally(0), uintptr_t{0}; }); break;      // W
      case 0x4b: own(0x4d6970, 0, 0, [&] { return nearest_ally(0), uintptr_t{0}; }); break;   // Ctrl+W
      case 0x4c: own(0x4d6a80, 0, 0, [&] { return nearest_enemy(0), uintptr_t{0}; }); break;  // Ctrl+E
      case 0x4d: own(0x4d6260, 0, 0, [&] { return prev_enemy(0), uintptr_t{0}; }); break;     // Shift+E
      case 0x4e: own(0x4d5ee0, 0, 0, [&] { return prev_ally(0), uintptr_t{0}; }); break;      // Shift+W
      case 0x48:  // TARGET_CURSOR
        if (screen) pick_aimed(false);
        break;
      case 0x4f: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: {  // lance orders
        static const int32_t kOrder[8] = {2, 0, 1, 0, 9, 0xa, 0xb, 0xc};  // by cmd - 0x4f (0x50 unused)
        const int32_t order = kOrder[cmd - 0x4f];
        if (g_calls.lance_order) {
          if (MechControls* c = g_calls.lance_order(*this, order)) controls = c;
        } else if (MechControls* c = lance_dispatch(controls, order)) {  // 0x00469930 -> 0x00469940
          controls = c;
        }
        break;
      }
      case 0x57: saved_target = target; break;   // 0x004d7220 DIRECT_AWAITCMD
      case 0x58: saved_target = nullptr; break;  // 0x004d7230
      case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f: case 0x60: {  // the target's part
        static const int32_t kPart[8] = {6, 1, 7, 8, 2, 3, 0xb, 0xc};
        if (g_calls.target_part) g_calls.target_part(*this, kPart[cmd - 0x59]);
        else target_part(kPart[cmd - 0x59]);
        break;
      }
      case 0x62: view_camera(0); break;
      case 0x63: view_camera(1); break;
      case 0x64: view_camera(4); break;
      case 0x65: view_camera(5); break;
      case 0x66: view_camera(2); break;
      case 0x67: view_camera(3); break;
      case 0x68: case 0x74: case 0x75: case 0xa5:
        if (screen) hud(screen, cmd);
        break;
      case 0x69: {  // the radar: its range doubled up to the sensors' reach, then passive, then 500 again
        const ai::Handle self = g_calls.handle ? g_calls.handle(*this) : 0;
        auto range = [&](float r) {
          own(0x4d6d20, fbits(r), 0, [&] { return set_sensor_range(self, r), uintptr_t{0}; });
        };
        if (!(xbc & 4)) {
          const float reach = equipment[6] > 0 ? kRangeSensors : kRange;
          if (reach > sensor_range) {
            range(sensor_range + sensor_range);
            break;
          }
          range(kRangePassive);
          xbc |= 4;
        } else {
          range(kRangePassive);
          xbc &= ~4;
        }
        other(*this, cmd, "vtable +0xb8");  // the HUD's passive light (0x0041ded0)
        break;
      }
      case 0x6b:  // the HUD on or off, while powered up
        if (!down() && screen) hud(screen, cmd);
        break;
      case 0x6e:  // MASC, with the equipment
        if (equipment[12] > 0) {
          const bool on = masc_time == 0.0f;  // 0x005bcd3c
          own(0x41df50, on, 0, [&] { return masc(on), uintptr_t{0}; });
        }
        break;
      case 0x73:  // MCB_COMMAND
        other(*this, cmd, "0x0047a910");
        break;
      case 0x76: case 0x77:  // CHAT_TEAM, CHAT_ALL: the network game's chat panel (0x0041e4e0)
        other(*this, cmd, "0x0041e4e0");
        break;
      default:
        break;
    }
  }
  switch (cmd) {
    case 0x16:  // TOGGLE_TMODE
      switch (controls->view) {
        case 0: case 1: controls_view(*this, 2); break;
        case 2: case 3: controls_view(*this, 0); break;
        default: break;
      }
      set_view_option(*this);
      break;
    case 0x17:  // FREELOOK
      if (controls->view == 0) {
        controls_view(*this, 1);
      } else if (controls->view == 1) {
        controls_view(*this, 0);
        view_camera(2);
      }
      set_view_option(*this);
      break;
    case 0x43:  // OBJECTIVES
      if (screen) hud(screen, cmd);
      break;
    case 0x49:  // TARGET_NONE
      target_set(nullptr);
      break;
    case 0xa6:
      other(*this, cmd, "0x0040f070");
      break;
    default:
      break;
  }
}

}  // namespace mech3
