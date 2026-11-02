// A field base's repair of a mech (docs/vehicles.md, The repair). Original
// file: mech3\mech.cpp (0x004154e0, 0x00415a90, after the death and before
// the motions' set-up 0x00415ca0).
#include "mech3/mech_repair.h"

#include <cstdio>

#include <cmath>

#include "gamez/zweapon/zwep_init.h"
#include "mech3/hud.h"
#include "mech3/mech.h"
#include "mech3/mech_config.h"
#include "mech3/mech_hud.h"
#include "mech3/mech_names.h"
#include "mech3/mech_parts.h"
#include "mech3/weapon.h"

#pragma STDC FP_CONTRACT OFF

namespace mech3 {

namespace {
RepairHooks g_default;
RepairHooks* g_hooks = &g_default;
}  // namespace

void set_repair_hooks(RepairHooks* h) { g_hooks = h ? h : &g_default; }
RepairHooks& repair_hooks() { return *g_hooks; }

bool RepairHooks::viewed(Mech& m) { return world().player == &m; }
void* RepairHooks::hud(Mech& m) { return m.pilot ? pilot_hud(*m.pilot) : nullptr; }
void RepairHooks::hud_weapons_refresh(void* hud) {
  if (hud) static_cast<Hud*>(hud)->weapons.refresh_all();
}
void RepairHooks::hud_reset(void* hud) {
  if (hud) static_cast<Hud*>(hud)->reset();
}

// Mech3.exe 1.2 @ 0x004154e0 (the dock's repair state, dt unused). The
// internals, ammo, armour, coolant and jump fuel back; parts already
// destroyed stay so (their set_internal / set_armor keep 0), heat is not
// touched. Ammo comes from the store by need; any armour of the mech's
// kind restores all of it, costing a half ton per half ton missing,
// rounded, as far as the store has it (unlimited items cost nothing).
bool Mech::repair(float dt) {
  (void)dt;
  RepairHooks& h = *g_hooks;
  MechPart* const sections[6] = {legs[0], legs[1], arms[0], arms[1], head, torso};
  for (int i = 0; i < 5; ++i) sections[i]->set_internal(1.0f, 0);
  for (int s = 0; s < 3; ++s) torso->set_internal(1.0f, s);

  Store* store = h.store();
  auto* m = static_cast<WeaponManager*>(ammo);
  for (uint32_t i = 0; i < m->count; ++i) {
    Weapon* w = m->weapons[i];
    const int32_t index = w ? w->index : -1;
    if (index == -1 || !weapon_uses_ammo(index) || !w->bin) continue;
    int32_t need = w->bin->capacity() - w->bin->left();
    AmmoItem want;  // 0x0041ebd0
    want.weapon = w->def->type->id;
    want.count = 1;
    ConfigItem* found = store ? repair_store_find(store, want) : nullptr;
    int32_t taken = 0;
    if (found) {
      taken = need;
      if (!found->b10) {
        if (found->count <= need) need = found->count;
        found->count -= need;
        taken = need;
        if (found->count < 0) found->count = 0;
      }
    }
    w->bin->add(taken);
    m = static_cast<WeaponManager*>(ammo);
  }

  EquipmentItem want;  // 0x0041ec00
  want.equipment = config->ferro ? 2 : 1;
  want.tech = config->armor_tech;
  want.count = 1;
  ConfigItem* found = store ? repair_store_find(store, want) : nullptr;
  // Ours (playtest W35): what the repair did with the armour, in the log.
  std::printf("mw3: repair: %s armour (%s, tech %d): %s\n", h.viewed(*this) ? "the player's" : "a mech's",
              config->ferro ? "ferro-fibrous" : "standard", static_cast<int>(config->armor_tech),
              !store ? "no store (not a campaign): not restored"
                     : found ? "restored from the store" : "none of that kind in the store: not restored");
  if (found) {
    // The armour left in points (each part's share times 100), summed as
    // the exe does, in play's single precision (docs/vehicles.md).
    float sum = sections[0]->get_armor(0) / sections[0]->get_armor_scale(0);
    for (int i = 1; i < 5; ++i) sum = sections[i]->get_armor(0) / sections[i]->get_armor_scale(0) + sum;
    for (int s = 0; s < 5; ++s) sum = torso->get_armor(s) / torso->get_armor_scale(s) + sum;
    const float left = std::floor((torso->get_armor(5) / torso->get_armor_scale(5) + sum) * 100.0f);
    const int32_t points = armor_points(config->armor_tech, config->ferro != 0, config->armor_tons);
    float per = 16.0f;  // 0x005bcd5c
    if (config->ferro) per = (config->armor_tech == 1 ? 1.2f : 1.12f) * 16.0f;
    const float d = std::floor(0.5f - (static_cast<float>(points - static_cast<int32_t>(left)) / per) * -2.0f);
    const float cost = d * 0.5f + d * 0.5f;
    if (cost > 0.0f) repair_store_remove(store, found, static_cast<int32_t>(cost));
    for (int i = 0; i < 5; ++i) sections[i]->set_armor(1.0f, 0);
    for (int s = 0; s < 6; ++s) torso->set_armor(1.0f, s);
  }

  coolant = 1.0f;
  jet_fuel = equipment[11] > 0 ? 1.0f : 0.0f;
  if (ams) ams->rounds = equipment[0] * 100;
  end_limp();
  if (h.viewed(*this)) {
    h.hud_weapons_refresh(h.hud(*this));
    h.hud_reset(h.viewed(*this) ? h.hud(*this) : nullptr);
  }
  net_damaged = 0x1fe;  // every part's damage with the next network state
  return true;
}

// Mech3.exe 1.2 @ 0x00415a90
void Mech::end_limp() {
  if ((limp == 0 || limp == 1) && anims[11]) {
    delete anims[11];  // 0x004152c0 -> 0x005a9dc0
    anims[11] = nullptr;
    limp = -1;
  }
}

}  // namespace mech3
