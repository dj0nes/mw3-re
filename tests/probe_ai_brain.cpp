// Probes for test_ai_brain.py (docs/ai.md, The AI controls): the AI mech
// controls 0x004d8750 and the AI vehicle controls 0x004e71c0 with the pilot's
// brain 0x004cf330 (mech3/mech_control, mech3/zai_brain), made and deleted.
// Every call out goes to the shared callback (a calltrace.Replay of the
// original's run); the port's objects get the original's addresses from the
// `new`s they stand for, so their images can be laid over the emulator's
// blocks. The records are the shipped aiv.zrd ones read by the ported reader
// (their regions and valves given made-up addresses), changed or not.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gamez/znetwork/znet_object.h"
#include "gamez/zreader/zreader.h"
#include "mech3/mech_control.h"
#include "mech3/zai_brain.h"
#include "mech3/zai_steps.h"
#include "mech3/zainet_region.h"
#include "mech3/zrdraiv.h"
#include "probe_app.h"

namespace {
using probe_app::call;
namespace ai = mech3::ai;

uint32_t lo(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
void* hd(uint32_t v) { return reinterpret_cast<void*>(static_cast<uintptr_t>(v)); }
uint32_t peek32(uint32_t a) { return call("peek32", {a}); }

// --- port objects by the original's addresses ------------------------------
std::map<const void*, uint32_t> g_addr;
std::map<uint32_t, const void*> g_obj;
void bind(const void* p, uint32_t a) {
  g_addr[p] = a;
  g_obj[a] = p;
}
uint32_t addr_of(const void* p) {
  if (!p) return 0;
  auto it = g_addr.find(p);
  return it != g_addr.end() ? it->second : lo(p);
}
template <class T>
T* obj_at(uint32_t a) {
  if (!a) return nullptr;
  auto it = g_obj.find(a);
  return const_cast<T*>(static_cast<const T*>(it != g_obj.end() ? it->second : hd(a)));
}

// The records' regions and valves by made-up addresses (both ways).
std::map<const void*, uint32_t> g_ref_id;
std::map<uint32_t, const void*> g_ref_ptr;
uint32_t g_next_ref = 0x2b000000;
uint32_t ref_id(const void* p) {
  if (!p) return 0;
  auto it = g_ref_id.find(p);
  if (it != g_ref_id.end()) return it->second;
  const uint32_t id = g_next_ref += 0x10;
  g_ref_id[p] = id, g_ref_ptr[id] = p;
  return id;
}
const void* ref_ptr(uint32_t id) {
  if (!id) return nullptr;
  auto it = g_ref_ptr.find(id);
  if (it != g_ref_ptr.end()) return it->second;
  g_ref_id[hd(id)] = id, g_ref_ptr[id] = hd(id);
  return hd(id);
}

// The shipped records.
struct Rec {
  std::string name;
  std::unique_ptr<ai::AiParams> params;
};
std::vector<Rec> g_recs;
std::unique_ptr<ai::AiParams> g_rec;  // the case's record

// What the case made.
enum Kind { kMech = 0, kVehicle, kOrders, kMap, kAim, kRadio };
std::map<const void*, int> g_kind;
std::vector<std::unique_ptr<znet::Object>> g_nets;
ai::AiNet* g_builtin = nullptr;

struct Hooks : ai::BrainHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override { return call(key, args); }
  bool entity_active(zgame::Handle e) override { return (call("peek8", {e + 8}) & 0xff) != 0; }  // a read
  void on_new(uint32_t size, const void* p) override { bind(p, call("new", {size})); }
  void on_delete(const void* p) override { call("delete", {addr_of(p)}); }
  uint32_t ref(const void* p) override { return addr_of(p); }
  znet::Object* net_find(int32_t id) override { return obj_at<znet::Object>(call("0x0058b160", {id})); }
  ai::AiNet* builtin_net() override { return g_builtin; }
  void* sound_find(const char* name) override { return hd(call("0x0056f6f0", {name})); }
  bool player_allies(uint32_t* allies) override {
    const uint32_t pilot = peek32(0x0060861c);
    const uint32_t v = pilot ? peek32(pilot + 0x14) : 0;
    if (!v) return false;
    *allies = peek32(v + 0xc4);
    return true;
  }
  // 0x004ac800's two calls; the answer is the test's address, passed on as it is.
  void* owner_vehicle(void* owner) override {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(call("owner.vt+0xec", {addr_of(owner)})));
  }
  void path_drop_temporary(void* v) override { call("0x00402510", {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(v))}); }
};
Hooks g_hooks;

// --- images -------------------------------------------------------------------
struct Img {
  uint8_t* b;
  uint8_t* mask;
  uint32_t size;
  void put(uint32_t off, const void* v, uint32_t n) {
    if (off + n > size) return;
    std::memcpy(b + off, v, n);
    std::memset(mask + off, 1, n);
  }
  void u(uint32_t off, uint32_t v) { put(off, &v, 4); }
  void f(uint32_t off, float v) { put(off, &v, 4); }
  void b8(uint32_t off, uint32_t v) {
    const uint8_t x = static_cast<uint8_t>(v);
    put(off, &x, 1);
  }
  void p(uint32_t off, const void* v) { u(off, addr_of(v)); }
  void v3(uint32_t off, const float* v) { f(off, v[0]), f(off + 4, v[1]), f(off + 8, v[2]); }
  void skip(uint32_t off, uint32_t n) { std::memset(mask + off, 2, n); }  // compared apart
};

constexpr uint32_t kElem = 0x7b8;  // the first order's place in its block (13 of 26)

void order_image(Img& o, uint32_t at, const ai::Order& r, uint32_t vtable, uint32_t self) {
  o.u(at, vtable);
  o.b8(at + 4, r.x04);
  o.p(at + 8, r.owner);
  o.u(at + 0xc, r.net ? self + 0x30 : 0);
  o.u(at + 0x10, r.state ? self + 0x54 : 0);
  o.u(at + 0x14, static_cast<uint32_t>(r.x14));
  o.v3(at + 0x18, r.x18);
  o.v3(at + 0x24, r.x24);
  if (const auto* link = dynamic_cast<const ai::NetLink*>(r.net.get())) {
    o.u(at + 0x30, link->builtin ? 0x005cab50 : 0x005ca748);
    o.u(at + 0x34, self);
    o.p(at + 0x38, link->net());
  }
  if (dynamic_cast<const ai::SharedState*>(r.state.get())) {
    o.u(at + 0x54, 0x005cbed0);
    o.u(at + 0x58, self);
  }
}

// The brain at `at` of its controls block `base` (address `addr`).
void brain_image(Img& o, uint32_t at, uint32_t addr, const ai::Brain& b, uint32_t vtable, uint32_t vbptr) {
  const uint32_t self = addr + at;
  o.u(at, vtable);
  o.put(at + 4, b.params.raw + 4, 0xcc);
  o.skip(at + 0xd0, 12);  // the orders, pilot and voice: their copies are compared apart
  o.f(at + 0xdc, b.xdc);
  o.u(at + 0xe0, vbptr);
  o.p(at + 0xe4, b.xe4);
  o.f(at + 0xe8, b.xe8), o.f(at + 0xec, b.xec);
  o.u(at + 0xf0, static_cast<uint32_t>(b.xf0));
  o.f(at + 0xf4, b.xf4);
  o.b8(at + 0xf8, b.xf8);
  o.f(at + 0xfc, b.xfc);
  o.f(at + 0x100, b.x100);
  o.b8(at + 0x104, b.x104);
  const uint32_t fi = at + 0x108;
  const ai::Firing& f = b.firing;
  o.u(fi, static_cast<uint32_t>(f.phase));
  o.f(fi + 4, f.time), o.f(fi + 8, f.quick_delay), o.f(fi + 0xc, f.delay), o.f(fi + 0x10, f.rest);
  o.p(fi + 0x14, f.plan);
  o.f(fi + 0x18, f.reach_arg), o.f(fi + 0x1c, f.track_arg), o.f(fi + 0x20, f.fire_arg), o.f(fi + 0x24, f.range);
  o.u(fi + 0x28, f.obstacle);
  o.u(fi + 0x2c, f.brain ? self : 0);
  o.p(fi + 0x30, f.first);
  o.u(fi + 0x34, 0x005c9458);  // the look-around
  o.p(fi + 0x38, f.look.owner), o.f(fi + 0x3c, f.look.until);
  o.v3(fi + 0x40, f.look.dir);
  o.u(fi + 0x4c, static_cast<uint32_t>(f.look.phase));
  o.p(fi + 0x50, f.second);
  o.p(fi + 0x54, f.aim);
  // The order deque (MSVC: allocator byte, first and last iterators, map,
  // map size, size) and its owner.
  const uint32_t q = at + 0x160;
  o.b8(q, 0);
  if (b.orders.empty()) {
    for (uint32_t i = 4; i < 0x30; i += 4) o.u(q + i, 0);
  } else {
    const uint32_t blk = addr_of(b.order_block), map = addr_of(b.order_map);
    const uint32_t first = blk + kElem, last = first + 0x98 * static_cast<uint32_t>(b.orders.size());
    o.u(q + 4, blk), o.u(q + 8, blk + 0xf70), o.u(q + 0xc, first), o.u(q + 0x10, map + 4);
    o.u(q + 0x14, blk), o.u(q + 0x18, blk + 0xf70), o.u(q + 0x1c, last), o.u(q + 0x20, map + 4);
    o.u(q + 0x24, map), o.u(q + 0x28, 2), o.u(q + 0x2c, static_cast<uint32_t>(b.orders.size()));
  }
  o.u(q + 0x30, self);
  order_image(o, at + 0x194, b.order, 0x005cd8e8, self + 0x194);
  o.b8(at + 0x22c, b.x22c);
  o.f(at + 0x230, b.x230);
  o.p(at + 0x234, b.x234), o.p(at + 0x238, b.path.finder), o.p(at + 0x23c, b.x23c);
  o.u(at + 0x240, 0x005cc290);
  o.skip(at + 0x244, 1);  // bit 0 cleared, the rest the heap's
  o.u(at + 0x248, static_cast<uint32_t>(b.step.x08));
  o.u(at + 0x24c, 0x00603fa0);
  const ai::Steer& s = b.steer;
  o.v3(at + 0x250, s.start);
  o.f(at + 0x25c, s.dir[0]), o.f(at + 0x260, s.dir[1]);
  o.f(at + 0x264, s.along_min), o.f(at + 0x268, s.length), o.f(at + 0x26c, s.along_at);
  o.f(at + 0x270, s.across), o.f(at + 0x274, s.across_to), o.f(at + 0x278, s.rate);
  o.f(at + 0x27c, s.along), o.f(at + 0x280, s.side);
  o.f(at + 0x284, s.near_dist), o.f(at + 0x288, s.x38);
  o.u(at + 0x28c, s.x3c), o.u(at + 0x290, s.x40), o.u(at + 0x294, s.x44);
  o.v3(at + 0x298, b.x298);
  o.b8(at + 0x2a4, b.x2a4);
  o.p(at + 0x2a8, b.radio);
}

// The fields 0x004265a0 sets (most derived 0): not +0x40 nor +0x5b.
void controls_image(Img& o, const mech3::MechControls& c) {
  o.u(8, c.flags), o.u(0xc, static_cast<uint32_t>(c.view));
  o.f(0x10, c.throttle), o.f(0x14, c.turn), o.f(0x18, c.pitch), o.f(0x1c, c.twist), o.f(0x20, c.jump);
  o.f(0x24, c.jj_strafe), o.f(0x28, c.jj_forward), o.f(0x2c, c.axis_2c), o.f(0x30, c.axis_30);
  o.b8(0x34, c.fire);
  o.u(0x38, static_cast<uint32_t>(c.wrapped));
  o.b8(0x3c, c.joy_throttle_set), o.b8(0x3d, c.jump_held), o.b8(0x3e, c.throttle_held);
  o.b8(0x3f, c.throttle_returns);
  o.f(0x44, c.throttle_target), o.f(0x48, c.turn_target), o.f(0x4c, c.jump_target);
  o.b8(0x50, c.reverse);
  o.u(0x54, c.last_pov);
  o.b8(0x58, c.moves[0]), o.b8(0x59, c.moves[1]), o.b8(0x5a, c.x5a);
}

void image(Img& o, uint32_t addr, const void* obj, int kind) {
  switch (kind) {
    case kMech: {
      const auto& c = *static_cast<const mech3::AiControls*>(obj);
      o.u(0, 0x005cd318), o.u(4, 0x005cd340);
      controls_image(o, c);
      brain_image(o, 0x5c, addr, *c.brain, 0x005cd2c8, 0x005cd338);
      o.u(0x308, 0);  // the vtordisp
      o.u(0x30c, 0x005cd210);
      o.u(0x310, 0), o.u(0x314, 0), o.u(0x318, 0);
      o.p(0x31c, c.owner);
      break;
    }
    case kVehicle: {
      const auto& c = *static_cast<const ai::AiVehicleControls*>(obj);
      o.u(0, 0x005cd880), o.u(4, 0x005cd8b8);
      o.u(0x38, static_cast<uint32_t>(c.state));
      o.u(0x78c, addr + 0x790);
      o.u(0x790, 0x005c8340);
      o.f(0x794, c.turret.lo_sin), o.f(0x798, c.turret.lo_cos);
      o.f(0x79c, c.turret.hi_sin), o.f(0x7a0, c.turret.hi_cos);
      brain_image(o, 0xa04, addr, *c.brain, 0x005cd830, 0x005cd8b0);
      o.v3(0xcb0, c.aim.dir);
      o.b8(0xcbc, c.aim.set);
      o.u(0xcc0, 0);
      o.u(0xcc4, 0x005cd778);
      o.u(0xcc8, 0), o.u(0xccc, 0), o.u(0xcd0, 0);
      o.p(0xcd4, c.owner);
      break;
    }
    case kOrders: {  // the deque's block: its orders from the middle
      const auto& d = *static_cast<const std::deque<ai::Order>*>(obj);
      uint32_t at = kElem;
      for (const ai::Order& r : d) {
        order_image(o, at, r, 0x005cd8d8, addr + at);
        at += 0x98;
      }
      break;
    }
    case kMap: {  // map[1] the block (map[0] is never written)
      const auto& blk = *static_cast<const void* const*>(obj);
      o.p(4, blk);
      break;
    }
    case kAim: {
      const auto& a = *static_cast<const ai::Aim*>(obj);
      o.v3(0, a.offset), o.v3(0xc, a.drift);
      o.b8(0x18, a.active);
      o.f(0x1c, a.until);
      o.f(0x20, a.limit2);
      break;
    }
    case kRadio: {
      const auto& r = *static_cast<const ai::Radio*>(obj);
      for (uint32_t i = 0; i < 16; ++i) o.u(4 * i, lo(r.sounds[i]));
      break;
    }
  }
}

void classify_brain(const ai::Brain& b) {
  if (b.order_block) g_kind[b.order_block] = kOrders;
  if (b.order_map) g_kind[b.order_map] = kMap;
  if (b.firing.aim) g_kind[b.firing.aim] = kAim;
  if (b.radio) g_kind[b.radio] = kRadio;
}

std::vector<std::unique_ptr<mech3::AiControls>> g_mechs;
std::vector<std::unique_ptr<ai::AiVehicleControls>> g_vehicles;

const ai::Brain* brain_at(uint32_t a) {
  auto it = g_obj.find(a);
  if (it == g_obj.end()) return nullptr;
  auto k = g_kind.find(it->second);
  if (k == g_kind.end()) return nullptr;
  if (k->second == kMech) return static_cast<const mech3::AiControls*>(it->second)->brain.get();
  if (k->second == kVehicle) return static_cast<const ai::AiVehicleControls*>(it->second)->brain.get();
  return nullptr;
}

void copy_out(char* out, int cap, const char* s) {
  if (!s) {
    out[0] = 0;
    return;
  }
  std::snprintf(out, static_cast<size_t>(cap), "%s", s);
}

}  // namespace

extern "C" {

// Every aiv.zrd record of `archive` read by the ported reader, kept.
int32_t probe_ab_load_aiv(const char* archive) {
  g_recs.clear();
  ai::aiv_file_reset();
  zrdr::init();
  zrdr::close_archives(true);
  if (!zrdr::open_archive(archive, true)) return -1;
  {
    probe_app::Mute mute;
    ai::AivRecord rec;
    const int32_t n = ai::aiv_file().record_count();
    for (int32_t i = 0; i < n; ++i) {
      const char* name = nullptr;
      if (!rec.read(i, &name) || !name) continue;
      g_recs.push_back({name, std::make_unique<ai::AiParams>(rec)});
    }
  }
  zrdr::close_archives(true);
  ai::aiv_file_reset();
  return static_cast<int32_t>(g_recs.size());
}

// Record i: its name, +0x04..+0xcf (raw[0xd0]), its orders (0x20 bytes each,
// the region and valve as their made-up addresses; up to cap), the pilot
// and voice ("" for none, has[0..1] whether given). The order count.
int32_t probe_ab_record(int32_t i, char* name, uint8_t* raw, uint8_t* orders, int32_t cap, char* pilot,
                        char* voice, int32_t* has) {
  if (i < 0 || i >= static_cast<int32_t>(g_recs.size())) return -1;
  const ai::AiParams& p = *g_recs[static_cast<size_t>(i)].params;
  copy_out(name, 256, g_recs[static_cast<size_t>(i)].name.c_str());
  std::memcpy(raw, p.raw, 0xd0);
  copy_out(pilot, 256, p.pilot), copy_out(voice, 256, p.sound_tag);
  has[0] = p.pilot != nullptr, has[1] = p.sound_tag != nullptr, has[2] = p.vlist != nullptr;
  int32_t n = 0;
  if (p.vlist)
    for (const ai::VListEntry& e : *p.vlist) {
      if (n < cap) {
        uint8_t* o = orders + 0x20 * n;
        std::memcpy(o, &e.f00, 4), std::memcpy(o + 4, &e.f04, 4), std::memcpy(o + 8, &e.f08, 4);
        std::memcpy(o + 0xc, &e.f0c, 4), std::memcpy(o + 0x10, &e.f10, 4);
        const uint32_t r = ref_id(e.region), v = ref_id(e.valve);
        std::memcpy(o + 0x14, &r, 4);
        o[0x18] = e.flags, o[0x19] = e.flags2, o[0x1a] = e.pad[0], o[0x1b] = e.pad[1];
        std::memcpy(o + 0x1c, &v, 4);
      }
      ++n;
    }
  return n;
}

// The case's record: +0x04..+0xcf, n orders (as probe_ab_record gives them,
// -1 for no list at all), the pilot and voice (null for none).
void probe_ab_set_record(const uint8_t* raw, int32_t n, const uint8_t* orders, const char* pilot,
                         const char* voice) {
  g_rec = std::make_unique<ai::AiParams>();
  std::memcpy(g_rec->raw + 4, raw + 4, 0xcc);
  if (n >= 0) {
    g_rec->vlist = new std::list<ai::VListEntry>;
    for (int32_t k = 0; k < n; ++k) {
      const uint8_t* o = orders + 0x20 * k;
      ai::VListEntry e;
      std::memcpy(&e.f00, o, 4), std::memcpy(&e.f04, o + 4, 4), std::memcpy(&e.f08, o + 8, 4);
      std::memcpy(&e.f0c, o + 0xc, 4), std::memcpy(&e.f10, o + 0x10, 4);
      uint32_t r, v;
      std::memcpy(&r, o + 0x14, 4), std::memcpy(&v, o + 0x1c, 4);
      e.region = static_cast<ai::Region*>(const_cast<void*>(ref_ptr(r)));
      e.valve = static_cast<ai::Valve*>(const_cast<void*>(ref_ptr(v)));
      e.flags = o[0x18], e.flags2 = o[0x19], e.pad[0] = o[0x1a], e.pad[1] = o[0x1b];
      g_rec->vlist->push_back(e);
    }
  }
  g_rec->pilot = pilot ? strdup(pilot) : nullptr;
  g_rec->sound_tag = voice ? strdup(voice) : nullptr;
}

// A new case: the hooks, no objects.
void probe_ab_reset() {
  ai::set_brain_hooks(&g_hooks);
  {
    probe_app::Mute mute;  // the last case's leftovers go silently
    g_mechs.clear();
    g_vehicles.clear();
    g_nets.clear();
  }
  g_builtin = nullptr;
  g_kind.clear();
  g_addr.clear();
  g_obj.clear();
}

// A net at `addr`: kind 0 the net base (not compiled), 1 a compiled net, 2
// the built-in one (0x00604008).
void probe_ab_net(uint32_t addr, int32_t kind, int32_t id) {
  std::unique_ptr<znet::Object> n;
  if (kind == 0) n = std::make_unique<ai::AiNet>(id);
  else if (kind == 1) n = std::make_unique<ai::CompiledNet>(id, 0);
  else n = std::make_unique<ai::BuiltinNet>(id, 0);
  if (kind == 2) g_builtin = static_cast<ai::AiNet*>(n.get());
  bind(n.get(), addr);
  g_nets.push_back(std::move(n));
}

// 0x0049fe90 (kind 0: `new(0x320)`, the AI mech controls) or 0x0049ff10
// (kind 1: 0x004ac860(0xcd8), the AI vehicle controls), over the case's
// record. The block's address.
uint32_t probe_ab_make(int32_t kind, uint32_t owner, uint32_t net) {
  ai::AiNet* n = obj_at<ai::AiNet>(net);
  if (kind == 0) {
    const uint32_t a = call("new", {0x320});
    auto c = std::make_unique<mech3::AiControls>(static_cast<mech3::Mech*>(hd(owner)), n, *g_rec);
    bind(c.get(), a);
    g_kind[c.get()] = kMech;
    g_addr[c->brain.get()] = a + 0x5c;  // the brain's address (a base in the exe)
    classify_brain(*c->brain);
    g_mechs.push_back(std::move(c));
    return a;
  }
  const uint32_t a = call("0x004ac860", {0xcd8});
  auto c = std::make_unique<ai::AiVehicleControls>(hd(owner), n, *g_rec);
  bind(c.get(), a);
  g_kind[c.get()] = kVehicle;
  g_addr[c->brain.get()] = a + 0xa04;
  classify_brain(*c->brain);
  g_vehicles.push_back(std::move(c));
  return a;
}

// The controls at `addr` deleted through their IControls slot 0 with 1.
void probe_ab_delete(uint32_t addr) {
  for (auto it = g_mechs.begin(); it != g_mechs.end(); ++it)
    if (addr_of(it->get()) == addr) {
      g_mechs.erase(it);
      call("delete", {addr});
      return;
    }
  for (auto it = g_vehicles.begin(); it != g_vehicles.end(); ++it)
    if (addr_of(it->get()) == addr) {
      g_vehicles.erase(it);
      call("0x004ac890", {addr, 0xcd8});
      return;
    }
}

// The port's object at the original's block `addr`: its image over `buf`
// (pre-filled by the test) and the bytes it sets in `mask` (1, or 2 for the
// bytes compared apart). -1: no object.
int probe_ab_image(uint32_t addr, uint32_t size, uint8_t* buf, uint8_t* mask) {
  auto it = g_obj.find(addr);
  if (it == g_obj.end()) return -1;
  auto k = g_kind.find(it->second);
  if (k == g_kind.end()) return -2;
  Img o{buf, mask, size};
  image(o, addr, it->second, k->second);
  return k->second;
}

// The brain of the controls at `addr`: its settings' orders (as
// probe_ab_record gives them), pilot and voice. The order count, -1 for no
// list, -2 for no brain.
int32_t probe_ab_brain(uint32_t addr, uint8_t* orders, int32_t cap, char* pilot, char* voice, int32_t* has) {
  const ai::Brain* b = brain_at(addr);
  if (!b) return -2;
  const ai::AiParams& p = b->params;
  copy_out(pilot, 256, p.pilot), copy_out(voice, 256, p.sound_tag);
  has[0] = p.pilot != nullptr, has[1] = p.sound_tag != nullptr;
  if (!p.vlist) return -1;
  int32_t n = 0;
  for (const ai::VListEntry& e : *p.vlist) {
    if (n < cap) {
      uint8_t* o = orders + 0x20 * n;
      std::memcpy(o, &e.f00, 4), std::memcpy(o + 4, &e.f04, 4), std::memcpy(o + 8, &e.f08, 4);
      std::memcpy(o + 0xc, &e.f0c, 4), std::memcpy(o + 0x10, &e.f10, 4);
      const uint32_t r = ref_id(e.region), v = ref_id(e.valve);
      std::memcpy(o + 0x14, &r, 4);
      o[0x18] = e.flags, o[0x19] = e.flags2, o[0x1a] = e.pad[0], o[0x1b] = e.pad[1];
      std::memcpy(o + 0x1c, &v, 4);
    }
    ++n;
  }
  return n;
}

// 2b2: one of the AI vehicle controls' slots on the controls at `addr`
// (their IControls and primary slots, docs/vehicles.md): the answer, a
// float's bits, after a setter the field it set.
void probe_ab_vset(uint32_t addr, uint32_t throttle, uint32_t turn) {
  auto* c = static_cast<ai::AiVehicleControls*>(const_cast<void*>(g_obj.at(addr)));
  c->throttle = zgame::from_bits(throttle), c->turn = zgame::from_bits(turn);
}
uint32_t probe_ab_vslot(uint32_t addr, int slot, uint32_t arg) {
  auto* c = static_cast<ai::AiVehicleControls*>(const_cast<void*>(g_obj.at(addr)));
  const float f = zgame::from_bits(arg);
  switch (slot) {
    case 0: return c->reversing();
    case 1: return zgame::bits(c->throttle_now());
    case 2: return zgame::bits(c->turn);
    case 3: return zgame::bits(c->turn_goal());
    case 4: c->set_throttle(f); return zgame::bits(c->throttle);
    case 5: c->set_turn(f); return zgame::bits(c->turn);
    case 6: c->fire((arg & 0xff) != 0); return c->fire_on;
    case 7: return c->on_path();
    case 8: return c->destroyed();
    case 9: return static_cast<uint32_t>(c->dynamics());
    case 10: return static_cast<uint32_t>(c->path_mode());
    case 11: return static_cast<uint32_t>(c->path_goal());
    case 12: c->stop(); return zgame::bits(c->throttle) ^ zgame::bits(c->turn);
    default: return 0xdeadbeef;
  }
}

}  // extern "C"

