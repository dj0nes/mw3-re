// Probes for test_mech_repair.py (docs/vehicles.md, The repair): Mech::repair
// 0x004154e0 and the limp's end 0x00415a90 (mech3/mech_repair). The parts'
// calls (vt+0x3c..+0x4c) and the HUD's go to the shared callback; the store,
// the weapons' bins and the manager are real on both sides.
#include <cstdint>
#include <string>

#include "gamez/zgame/unported.h"
#include "gamez/zweapon/zwep_init.h"
#include "mech3/mech.h"
#include "mech3/mech_anim.h"
#include "mech3/mech_config.h"
#include "mech3/mech_parts.h"
#include "mech3/mech_repair.h"
#include "mech3/weapon.h"
#include "probe_app.h"

mech3::Store* rp_store_make();
void rp_store_free(mech3::Store* s);
void rp_store_add(mech3::Store* s, int32_t kind, int32_t id, int32_t tech, int32_t count, uint8_t b10);
int rp_store_dump(mech3::Store* s, uint32_t* out);

namespace {
namespace m3 = mech3;

uint32_t lo(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A part whose repair calls are the shared callback's, keyed as the
// emulator's fake parts ("leg0.vt+0x3c" ...).
template <class P>
struct Logged : P {
  std::string name;
  uint32_t call(int off, std::initializer_list<zgame::Arg> a) const {
    char key[32];
    std::snprintf(key, sizeof key, "%s.vt+0x%x", name.c_str(), off);
    return probe_app::call(key, a);
  }
  void set_internal(float v, int s) override { call(0x3c, {zgame::bits(v), s}); }
  void set_armor(float v, int s) override { call(0x40, {zgame::bits(v), s}); }
  float get_armor(int s) const override { return zgame::from_bits(call(0x44, {s})); }
  float get_armor_scale(int s) const override { return zgame::from_bits(call(0x4c, {s})); }
};
Logged<m3::Leg> g_legs[2];
Logged<m3::Arm> g_arms[2];
Logged<m3::Head> g_head;
Logged<m3::Torso> g_torso;

m3::Mech g_mech;
m3::Config* g_config = nullptr;
m3::Ams g_ams(0, 0, 0);
m3::WeaponManager* g_manager = nullptr;
m3::AmmoBin g_bins[8];
zwep::WeaponDef g_defs[16];
m3::WeaponType g_types[16];
m3::Store* g_store = nullptr;
bool g_viewed = false;

struct Hooks : m3::RepairHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return probe_app::call(key, args);
  }
  m3::Store* store() override { return g_store; }
  bool viewed(m3::Mech& m) override { return g_viewed && &m == &g_mech; }
  void* hud(m3::Mech& m) override {
    (void)m;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(unported("0x00430aa0", {0x684})));
  }
  void hud_weapons_refresh(void* h) override { unported("0x00446520", {lo(h) + 0x6520}); }
  void hud_reset(void* h) override { unported("0x0043be10", {lo(h)}); }
};
Hooks g_hooks;

void clear_weapons() {
  if (!g_manager) return;
  for (uint32_t i = 0; i < g_manager->count; ++i)
    if (g_manager->weapons[i]) g_manager->weapons[i]->bin = nullptr;  // the bins are the probe's
  delete g_manager;
  g_manager = nullptr;
}
}  // namespace

extern "C" {
void probe_rp_hooks(int on) { m3::set_repair_hooks(on ? &g_hooks : nullptr); }

// i: ferro, armor_tech, equipment[0], equipment[11], ams (0 none), ams
// rounds, limp, anims[11] set, viewed, the weapons' count; f: armor tons,
// coolant, jet fuel, the manager's ammo_value, ammo_value_inv.
// Weapons (count of them): present, index, type id, bin (-1 none).
// Bins (nb): amount, max, infinite, damage bits, warned. Store items (ns): kind, id,
// tech, count, b10.
void probe_rp_setup(const int32_t* i, const float* f, const int32_t* w, int32_t nb, const int32_t* b, int32_t ns,
                    const int32_t* s) {
  clear_weapons();
  if (g_mech.anims[11]) delete g_mech.anims[11];
  g_mech = m3::Mech{};
  const char* names[] = {"leg0", "leg1", "arm0", "arm1", "head", "torso"};
  m3::MechPart* parts[] = {&g_legs[0], &g_legs[1], &g_arms[0], &g_arms[1], &g_head, &g_torso};
  g_legs[0].name = names[0], g_legs[1].name = names[1], g_arms[0].name = names[2], g_arms[1].name = names[3];
  g_head.name = names[4], g_torso.name = names[5];
  (void)parts;
  g_mech.legs[0] = &g_legs[0], g_mech.legs[1] = &g_legs[1];
  g_mech.arms[0] = &g_arms[0], g_mech.arms[1] = &g_arms[1];
  g_mech.head = &g_head, g_mech.torso = &g_torso;
  if (!g_config) g_config = new m3::Config(0, "repair");
  g_config->ferro = static_cast<uint8_t>(i[0]);
  g_config->armor_tech = i[1];
  g_config->armor_tons = f[0];
  g_mech.config = g_config;
  g_mech.equipment[0] = i[2], g_mech.equipment[11] = i[3];
  g_ams = m3::Ams(0, 0, 0);
  g_ams.rounds = i[5];
  g_mech.ams = i[4] ? &g_ams : nullptr;
  g_mech.limp = i[6];
  g_mech.anims[11] = i[7] ? new m3::MechAnim : nullptr;
  g_viewed = i[8] != 0;
  g_mech.coolant = f[1], g_mech.jet_fuel = f[2];
  g_mech.net_damaged = 0;

  g_manager = new m3::WeaponManager(nullptr);
  g_manager->ammo_value = f[3], g_manager->ammo_value_inv = f[4];
  for (int32_t k = 0; k < nb; ++k) {
    m3::AmmoBin& bin = g_bins[k];
    bin = m3::AmmoBin{};
    bin.amount = b[5 * k], bin.max = b[5 * k + 1], bin.infinite = b[5 * k + 2] != 0;
    bin.damage = zgame::from_bits(static_cast<uint32_t>(b[5 * k + 3]));
    bin.warned = b[5 * k + 4] != 0;
    bin.manager = g_manager;
    bin.refs = 100;
  }
  const int32_t n = i[9];
  for (int32_t k = 0; k < n; ++k) {
    const int32_t* x = w + 4 * k;
    if (!x[0]) {
      g_manager->weapons[k] = nullptr;
      continue;
    }
    g_types[k].id = x[2];
    g_defs[k].type = &g_types[k];
    auto* wp = new m3::Weapon(&g_defs[k], nullptr, g_manager, true);
    wp->index = x[1];
    wp->bin = x[3] >= 0 ? &g_bins[x[3]] : nullptr;
    g_manager->weapons[k] = wp;
  }
  g_manager->count = static_cast<uint32_t>(n);
  g_mech.ammo = g_manager;

  if (g_store) rp_store_free(g_store);
  g_store = ns >= 0 ? rp_store_make() : nullptr;
  for (int32_t k = 0; k < ns; ++k) {
    const int32_t* x = s + 5 * k;
    rp_store_add(g_store, x[0], x[1], x[2], x[3], static_cast<uint8_t>(x[4]));
  }
}

uint32_t probe_rp_run(float dt) { return g_mech.repair(dt) ? 1u : 0u; }

// The bins (amount, max, warned each), the manager's two values, coolant,
// jet fuel, the AMS's rounds, limp, anims[11] set, the network damage flags, then the store.
int probe_rp_image(int32_t nb, uint32_t* out) {
  int k = 0;
  for (int32_t j = 0; j < nb; ++j)
    out[k++] = static_cast<uint32_t>(g_bins[j].amount), out[k++] = static_cast<uint32_t>(g_bins[j].max),
    out[k++] = g_bins[j].warned;
  out[k++] = zgame::bits(g_manager->ammo_value), out[k++] = zgame::bits(g_manager->ammo_value_inv);
  out[k++] = zgame::bits(g_mech.coolant), out[k++] = zgame::bits(g_mech.jet_fuel);
  out[k++] = static_cast<uint32_t>(g_ams.rounds);
  out[k++] = static_cast<uint32_t>(g_mech.limp), out[k++] = g_mech.anims[11] ? 1u : 0u, out[k++] = g_mech.net_damaged;
  if (g_store) k += rp_store_dump(g_store, out + k);
  else out[k++] = 0xffffffffu;
  return k;
}
}  // extern "C"
