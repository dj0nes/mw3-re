// zWeapon's damage path (gamez/zweapon/zweapon: beam damage, impact, the
// damage hand-off, splash, the projectile hit, the air burst) for
// tests/differential/test_weapons_hit.py, on the definitions probe_weapons.cpp
// loaded. The world (nodes and their damage handlers, materials, the splash
// query's answer, crater and handler results) is a table the test writes; the
// handles are the emulator's addresses. Every call out is logged.
#include <cstdint>
#include <cstring>
#include <vector>

#include "crt/msvc_rand.h"
#include "gamez/zweapon/zweapon.h"
#include "gamez/zweapon/zwep_init.h"
#include "mech3/weapon.h"

namespace {

using zwep::Handle;
using zwep::HitRecord;
using zwep::Projectile;

std::vector<int32_t> g_log;
void log(std::initializer_list<int32_t> v) {
  g_log.push_back(static_cast<int32_t>(v.size()));
  g_log.insert(g_log.end(), v);
}
int32_t fb(float f) {
  int32_t i;
  std::memcpy(&i, &f, 4);
  return i;
}
float bf(int32_t i) {
  float f;
  std::memcpy(&f, &i, 4);
  return f;
}
uint32_t u(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
Handle h(uint32_t v) { return reinterpret_cast<Handle>(static_cast<uintptr_t>(v)); }

// One node of the fake scene: its handler (0, 1 or a handler address), the
// projectile behind handler 1, whether it intersects, and what its handler
// does when called (result, kill, health after).
struct NodeRow {
  uint32_t node, handler, projectile, intersect, live, result, kill, health;
};
std::vector<NodeRow> g_nodes;
struct MatRow {
  uint32_t material;
  int32_t soil;
};
std::vector<MatRow> g_mats;
int32_t g_query_result = 1;
zwep::HitList g_query_list;
const zwep::HitList* g_out = nullptr;  // where the last query wrote
int32_t g_crater = 0;
float g_angles[3] = {};
std::vector<HitRecord> g_recs;  // the records an op passes, [0] the hit
Projectile g_proj;
std::vector<Projectile> g_projs;  // the projectiles behind handler-1 nodes
zwep::HitList g_list;              // a splash list an op passes
uint32_t g_spawned = 0;

const NodeRow* node_row(Handle n) {
  for (const NodeRow& r : g_nodes)
    if (r.node && r.node == u(n)) return &r;
  return nullptr;
}
const NodeRow* handler_row(uintptr_t hd) {
  for (const NodeRow& r : g_nodes)
    if (r.handler == hd) return &r;
  return nullptr;
}
// Where a record lives, as a token: 0x100 + i in g_recs, 0x200 + i in the
// query list, 0x300 + i in the op's list.
int32_t rec_token(const HitRecord* r) {
  if (!r) return -1;
  for (size_t i = 0; i < g_recs.size(); ++i)
    if (&g_recs[i] == r) return static_cast<int32_t>(0x100 + i);
  for (int i = 0; i < 31; ++i) {
    if (g_out && r == &g_out->rec[i]) return 0x200 + i;
    if (r == &g_list.rec[i]) return 0x300 + i;
  }
  return -2;
}

void set_hooks() {
  zwep::HitHooks& k = zwep::hit_hooks();
  k = zwep::HitHooks{};
  k.surface_mark = [](HitRecord* r) { log({1, rec_token(r)}); };
  k.node_handler = [](Handle n) -> uintptr_t {
    const NodeRow* r = node_row(n);
    return r ? r->handler : 0;
  };
  k.node_projectile = [](Handle n) -> Projectile* {
    for (size_t i = 0; i < g_nodes.size(); ++i)
      if (g_nodes[i].node == u(n)) return &g_projs[i];
    return nullptr;
  };
  k.handler_live = [](uintptr_t hd) {
    const NodeRow* r = handler_row(hd);
    return r && r->live != 0;
  };
  k.call_handler = [](uintptr_t hd, zwep::WeaponDef* d, HitRecord* rec, float dmg) -> uint32_t {
    const NodeRow* r = handler_row(hd);
    log({2, static_cast<int32_t>(hd), d->index, rec_token(rec), fb(dmg)});
    if (r->kill) zwep::note_kill(h(r->kill), rec);
    zwep::set_health_after(bf(static_cast<int32_t>(r->health)));
    return r->result;
  };
  k.soil = [](Handle m) {
    for (const MatRow& r : g_mats)
      if (r.material == u(m)) return r.soil;
    return 0;
  };
  k.effect = [](Handle a, const float* v) {
    log({3, static_cast<int32_t>(u(a)), fb(v[0]), fb(v[1]), fb(v[2]), fb(v[3]), fb(v[4]), fb(v[5]), fb(v[6]),
         fb(v[7]), fb(v[8])});
  };
  k.crater = [](zwep::WeaponDef* d, HitRecord* r, int32_t soil, Projectile*) {
    log({4, d->index, rec_token(r), soil});
    return g_crater;
  };
  k.quicksand = [](zwep::WeaponDef* d, HitRecord* r, int32_t soil, Projectile*) {
    log({5, d->index, rec_token(r), soil});
    return 0;
  };
  k.sound = [](zwep::WeaponDef* d, HitRecord* r, int32_t soil, float vol) {
    log({6, d->index, rec_token(r), soil, fb(vol)});
  };
  k.impact_effect = [](int32_t e, const zwep::Vec3f3* at) { log({7, e, fb(at->x), fb(at->y), fb(at->z)}); };
  k.surface_angles = [](const zwep::Vec3f3* n, float out[3]) {
    log({8, fb(n->x), fb(n->y), fb(n->z)});
    std::memcpy(out, g_angles, sizeof g_angles);
  };
  k.node_intersect = [](Handle n) {
    const NodeRow* r = node_row(n);
    return r && r->intersect != 0;
  };
  k.set_intersect = [](Handle n, int32_t on) { log({9, static_cast<int32_t>(u(n)), on}); };
  k.query = [](Handle, const zwep::Vec3f3* at, float radius, zwep::HitList* out) -> int32_t {
    log({10, fb(at->x), fb(at->y), fb(at->z), fb(radius)});
    *out = g_query_list;
    g_out = out;
    return g_query_result;
  };
  mech3::WeaponCalls& c = mech3::weapon_calls();
  c = mech3::WeaponCalls{};
  c.spawn = [](zwep::WeaponDef* d, Handle node, Handle x64, const mech3::Vec3f* m, const mech3::Vec3f* dir,
               const mech3::Vec3f* v, int32_t p5, int32_t p6, bool) -> Handle {
    log({11, d->index, static_cast<int32_t>(u(node)), static_cast<int32_t>(u(x64)), fb(m->x), fb(m->y), fb(m->z),
         fb(dir->x), fb(dir->y), fb(dir->z), fb(v->x), fb(v->y), fb(v->z), p5, p6});
    ++g_spawned;
    return nullptr;
  };
}

HitRecord read_rec(const int32_t* r) {
  HitRecord x;
  x.normal = {bf(r[0]), bf(r[1]), bf(r[2])};
  x.point = {bf(r[3]), bf(r[4]), bf(r[5])};
  x.distance_sq = bf(r[7]);
  x.material = h(static_cast<uint32_t>(r[8]));
  x.node = h(static_cast<uint32_t>(r[9]));
  return x;
}
void put_rec(std::vector<int32_t>& o, const HitRecord& x) {
  o.insert(o.end(), {fb(x.normal.x), fb(x.normal.y), fb(x.normal.z), fb(x.point.x), fb(x.point.y), fb(x.point.z)});
}
void put_proj(std::vector<int32_t>& o, const Projectile& p) {
  o.insert(o.end(), {p.state, fb(p.position.x), fb(p.position.y), fb(p.position.z), fb(p.health),
                     fb(p.damage_scale)});
}

}  // namespace

extern "C" {

// The scene: nodes (8 ints each), materials (2), the query's list (records of
// 10 ints) and result, the crater result, the surface angles.
void probe_hit_world(const uint32_t* nodes, int nn, const int32_t* mats, int nm, const int32_t* qrecs, int nq,
                     int32_t qresult, int32_t crater, const float* angles) {
  set_hooks();
  g_nodes.clear();
  for (int i = 0; i < nn; ++i) {
    const uint32_t* r = nodes + 8 * i;
    g_nodes.push_back({r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]});
  }
  g_projs.assign(g_nodes.size(), Projectile{});
  for (size_t i = 0; i < g_nodes.size(); ++i) g_projs[i].health = bf(static_cast<int32_t>(g_nodes[i].projectile));
  g_mats.clear();
  for (int i = 0; i < nm; ++i) g_mats.push_back({static_cast<uint32_t>(mats[2 * i]), mats[2 * i + 1]});
  g_query_list = zwep::HitList{};
  g_query_list.count = static_cast<uint8_t>(nq);
  for (int i = 0; i < nq; ++i) g_query_list.rec[i] = read_rec(qrecs + 10 * i);
  g_query_result = qresult;
  g_crater = crater;
  std::memcpy(g_angles, angles, sizeof g_angles);
}

// Globals of the hit (zwep::HitState): record_shot, splash, ground fx, the
// pre-hit hook on or off, the counted owner, dt.
void probe_hit_state(const int32_t* s) {
  zwep::HitState& st = zwep::hit_state();
  st.record_shot = s[0];
  st.splash_enabled = s[1];
  st.beam_ground_fx = s[2];
  st.pre_hit = s[3] ? +[](Handle hd, Handle n, float d, zwep::WeaponDef* def) {
    log({12, static_cast<int32_t>(u(hd)), static_cast<int32_t>(u(n)), fb(d), def->index});
  } : nullptr;
  st.counted = h(static_cast<uint32_t>(s[4]));
  st.dt = bf(s[5]);
  st.hits = s[6];
  st.health_after = bf(s[7]);
  st.killed = nullptr;
  st.killed_at = nullptr;
}

// op 0 beam damage, 1 apply, 2 impact, 3 hit, 4 air burst, 5 splash.
// ints: def, the records (10 each, count), the projectile (owner, node, state,
// dir3, pos3, speed, travelled, health, damage scale), extra (anim, exclude,
// owner for apply, damage, from3), the beam (length, damage scale, range
// scale, flags), the splash list records.
int32_t probe_hit_op(int op, int32_t def, const int32_t* recs, int nrec, const int32_t* proj, const int32_t* extra,
                     const int32_t* beam, uint32_t seed) {
  g_log.clear();
  crt::msvc_srand(seed);
  zwep::WeaponDef* d = &zwep::state().defs[static_cast<size_t>(def)];
  g_recs.clear();
  for (int i = 0; i < nrec; ++i) g_recs.push_back(read_rec(recs + 10 * i));
  g_proj = Projectile{};
  g_proj.owner = h(static_cast<uint32_t>(proj[0]));
  g_proj.node = h(static_cast<uint32_t>(proj[1]));
  g_proj.state = proj[2];
  g_proj.direction = {bf(proj[3]), bf(proj[4]), bf(proj[5])};
  g_proj.position = {bf(proj[6]), bf(proj[7]), bf(proj[8])};
  g_proj.speed = bf(proj[9]);
  g_proj.travelled = bf(proj[10]);
  g_proj.health = bf(proj[11]);
  g_proj.damage_scale = bf(proj[12]);
  int32_t r = 0;
  switch (op) {
    case 0: {
      zwep::Beam b;
      b.length = bf(beam[0]);
      b.damage_scale = bf(beam[1]);
      b.range_scale = bf(beam[2]);
      b.flags = static_cast<uint32_t>(beam[3]);
      r = fb(zwep::beam_damage(d, &b));
      log({20, static_cast<int32_t>(b.flags)});
      break;
    }
    case 1: {
      const zwep::Vec3f3 from{bf(extra[4]), bf(extra[5]), bf(extra[6])};
      r = static_cast<int32_t>(zwep::apply_damage(d, h(static_cast<uint32_t>(extra[2])), &from, &g_recs[0],
                                                  bf(extra[3]), h(static_cast<uint32_t>(extra[0]))));
      break;
    }
    case 2: r = static_cast<int32_t>(zwep::impact(d, &g_recs[0], &g_proj, h(static_cast<uint32_t>(extra[0])))); break;
    case 3: zwep::hit(d, &g_proj, &g_recs[0]); break;
    case 4: zwep::air_burst(d, &g_proj); break;
    case 7: break;  // the flight step: probe_hit_step does it after this setup
    case 5: {
      g_list = zwep::HitList{};
      g_list.count = static_cast<uint8_t>(nrec);
      for (int i = 0; i < nrec; ++i) g_list.rec[i] = g_recs[static_cast<size_t>(i)];
      r = zwep::splash(d, &g_proj, &g_list, static_cast<uintptr_t>(static_cast<uint32_t>(extra[1])));
      break;
    }
    default: break;
  }
  return r;
}

int probe_hit_log(int32_t* out, int cap) {
  if (static_cast<int>(g_log.size()) > cap) return -1;
  std::memcpy(out, g_log.data(), g_log.size() * 4);
  return static_cast<int>(g_log.size());
}

// After an op: the hit state, the projectile, the records' normals and
// points, the handler-1 projectiles' health, the defs' salvo marks.
int probe_hit_dump(int32_t* out, int cap) {
  std::vector<int32_t> o;
  const zwep::HitState& st = zwep::hit_state();
  o.insert(o.end(), {fb(st.shot_from.x), fb(st.shot_from.y), fb(st.shot_from.z), fb(st.shot_to.x),
                     fb(st.shot_to.y), fb(st.shot_to.z), static_cast<int32_t>(u(st.attacker)), st.record_shot,
                     static_cast<int32_t>(u(st.killed)), rec_token(st.killed_at), fb(st.health_after), st.hits});
  put_proj(o, g_proj);
  for (const HitRecord& x : g_recs) put_rec(o, x);
  for (const Projectile& p : g_projs) o.push_back(fb(p.health));
  for (const zwep::WeaponDef& d : zwep::state().defs) {
    if (!d.type) continue;
    o.push_back(static_cast<int32_t>(d.type->salvo_marks.size()));
    for (const auto* m : d.type->salvo_marks) o.insert(o.end(), {fb(m->at.x), fb(m->at.y), fb(m->at.z), fb(m->until)});
  }
  const zwep::FireState& f = zwep::fire_state();
  o.push_back(f.lock_flag ? 1 : 0);
  if (static_cast<int>(o.size()) > cap) return -1;
  std::memcpy(out, o.data(), o.size() * 4);
  return static_cast<int>(o.size());
}

void probe_hit_clock(float clock) { mech3::weapon_world().clock = clock; }

// One frame of flight (0x00585cc0). f: base3, vel3, target3, has target,
// speed, max speed, deviation3, drift3, travelled, ceiling; the projectile's
// other fields come from the last probe_hit_op setup (owner, node, pos, ...).
// out: state, velocity3, speed, deviation3, drift3, travelled, move3.
void probe_hit_step(int32_t def, const int32_t* f, uint32_t seed, int32_t* out) {
  static zwep::Vec3f3 target;
  g_log.clear();
  crt::msvc_srand(seed);
  zwep::WeaponDef* d = &zwep::state().defs[static_cast<size_t>(def)];
  g_proj.base_velocity = {bf(f[0]), bf(f[1]), bf(f[2])};
  g_proj.velocity = {bf(f[3]), bf(f[4]), bf(f[5])};
  target = {bf(f[6]), bf(f[7]), bf(f[8])};
  g_proj.target_at = f[9] ? &target : nullptr;
  g_proj.speed = bf(f[10]);
  g_proj.max_speed = bf(f[11]);
  g_proj.deviation = {bf(f[12]), bf(f[13]), bf(f[14])};
  g_proj.drift = {bf(f[15]), bf(f[16]), bf(f[17])};
  g_proj.travelled = bf(f[18]);
  zwep::hit_state().tether_ceiling = bf(f[19]);
  zwep::Vec3f3 move{};
  zwep::step(d, &g_proj, &move);
  const zwep::Projectile& q = g_proj;
  const int32_t v[] = {q.state, fb(q.velocity.x), fb(q.velocity.y), fb(q.velocity.z), fb(q.speed),
                       fb(q.deviation.x), fb(q.deviation.y), fb(q.deviation.z), fb(q.drift.x), fb(q.drift.y),
                       fb(q.drift.z), fb(q.travelled), fb(move.x), fb(move.y), fb(move.z)};
  std::memcpy(out, v, sizeof v);
}

// Data no shipped weapon has, put into a def on both sides: 0 its flags, 1 and
// 2 RANGE_MINIMUM's values, 3 an EFFECT for every impact kind.
void probe_hit_poke(int def, int which, int32_t v) {
  zwep::WeaponDef& d = zwep::state().defs[static_cast<size_t>(def)];
  if (which == 0) d.flags = static_cast<uint32_t>(v);
  if (which == 1) d.range_min[0] = bf(v);
  if (which == 2) d.range_min[1] = bf(v);
  if (which == 3)
    for (int k = 0; k < zwep::kSoilCount; ++k) d.impacts[k].effect = v;
  if (which == 4) d.terrain_height = bf(v);
  if (which == 5) d.detonation_distance_sq = bf(v);
  if (which == 6) d.acceleration = bf(v);
}

}  // extern "C"
