// Probes for test_mech_build.py (docs/mech-build.md): the launch 0x00491150
// and the mech's constructor chain from 0x00414150 (mech3/mission_launch,
// mech3/mech_build, the parts', skis' and controls' constructors). Every
// call out goes to the shared callback (a calltrace.Replay of the original's
// run); the fake objects (nodes, the pilot, the view, the mech data, the
// definitions and their configurations) live in the emulator's memory and
// are read through peek, written through poke. The port's own objects get the
// original's addresses from the `new`s they stand for, so their images can be
// laid over the emulator's blocks and compared field by field.
#include <cstdint>
#include "gamez/zmodel/gmod_const.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "gamez/zclass/cls_mechlib.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_zar.h"
#include "mech3/mech.h"
#include "mech3/mech_build.h"
#include "mech3/mech_config.h"
#include "mech3/mech_control.h"
#include "mech3/mech_reader.h"
#include "mech3/mech_skin.h"
#include "mech3/mech_skis.h"
#include "mech3/mission_launch.h"
#include "mech3/weapon.h"
#include "mech3/zrdraiv.h"
#include "probe_app.h"

namespace mech3 {
void player_set_60b56c(uint8_t v);  // mech3/player.h (its Handle clashes with mech.h's)
uint8_t player_get_60b56c();
}  // namespace mech3

namespace {
using probe_app::call;
using mech3::Handle;
namespace m3 = mech3;

uint32_t lo(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
Handle hd(uint32_t v) { return reinterpret_cast<Handle>(static_cast<uintptr_t>(v)); }
uint32_t fb(float f) { return zgame::bits(f); }
uint32_t peek32(uint32_t a) { return call("peek32", {a}); }
uint8_t peek8(uint32_t a) { return static_cast<uint8_t>(call("peek8", {a})); }
void poke32(uint32_t a, uint32_t v) { call("poke32", {a, v}); }
void poke8(uint32_t a, uint8_t v) { call("poke8", {a, v}); }

// --- port objects by the original's addresses ----------------------------------
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
  auto it = g_obj.find(a);
  return const_cast<T*>(static_cast<const T*>(it != g_obj.end() ? it->second : hd(a)));
}

// What the test set up.
uint32_t g_missions = 0, g_pilot = 0, g_view = 0, g_interp = 0, g_rec = 0;
uint8_t g_record[0xd0];
bool g_have_record = false;
std::vector<std::unique_ptr<m3::MechData>> g_pilots;
std::vector<std::unique_ptr<m3::CockpitModel>> g_blocks;
std::vector<std::unique_ptr<m3::AiControls>> g_ctls;
std::map<const m3::MechControls*, uint32_t> g_ictl;  // a fake controller's IControls base
std::set<const m3::MechPart*> g_inited;               // parts whose vt+0x08 took a configuration
bool g_configured = false;                            // the configuration step passed its checks
std::map<uint32_t, std::string> g_names;              // C strings read from the emulator

const char* cstr(uint32_t a) {
  auto it = g_names.find(a);
  if (it != g_names.end()) return it->second.c_str();
  std::string s;
  for (uint32_t i = 0; a && i < 256; ++i) {
    const char c = static_cast<char>(peek8(a + i));
    if (!c) break;
    s.push_back(c);
  }
  return g_names.emplace(a, s).first->second.c_str();
}

struct Hooks : m3::MechBuildHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override { return call(key, args); }
  uint32_t ref(const void* p) override { return addr_of(p); }
  void on_new(uint32_t size, const void* p) override {
    const uint32_t a = call("new", {size});
    if (p) bind(p, a);
  }
  void on_calloc(uint32_t n, uint32_t size, const void* p) override { bind(p, call("calloc", {n, size})); }
  Handle alloc(uint32_t size) override { return hd(call("new", {size})); }
  m3::MechData* mech_data(int32_t index, int32_t create) override {
    return obj_at<m3::MechData>(call("0x00430d10", {index, create}));
  }
  const m3::MechDfn* dfn(int32_t roster) override { return obj_at<m3::MechDfn>(call("0x004846a0", {roster})); }
  int32_t node_box(Handle node, float box[6]) override {
    const auto r = static_cast<int32_t>(call("0x004ebd50", {lo(node)}));
    if (r == 0)
      for (uint32_t i = 0; i < 6; ++i) box[i] = zgame::from_bits(call("@box", {i}));
    return r;
  }
  void node_get_translation(Handle node, float* x, float* y, float* z) override {
    call("0x004fc020", {lo(node)});
    *x = zgame::from_bits(call("@tx", {0}));
    *y = zgame::from_bits(call("@tx", {1}));
    *z = zgame::from_bits(call("@tx", {2}));
  }
  const char* node_name(Handle n) override { return cstr(lo(n)); }
  int32_t node_class(Handle n) override { return static_cast<int32_t>(peek32(lo(n) + 0x34)); }
  Handle node_data(Handle n) override { return hd(peek32(lo(n) + 0x38)); }
  Handle node_model(Handle n) override { return hd(peek32(lo(n) + 0x3c)); }
  int32_t node_child_count(Handle n) override { return static_cast<int32_t>(peek32(lo(n) + 0x5c)); }
  Handle node_child(Handle n, int32_t i) override { return hd(peek32(peek32(lo(n) + 0x60) + 4 * i)); }
  int32_t node_parent_count(Handle n) override { return static_cast<int32_t>(peek32(lo(n) + 0x54)); }
  Handle node_parent(Handle n, int32_t i) override { return hd(peek32(peek32(lo(n) + 0x58) + 4 * i)); }
  float node_min_y(Handle n) override { return zgame::from_bits(peek32(lo(n) + 0x78)); }
  int32_t node_id(Handle n) override { return static_cast<int32_t>(peek32(lo(n) + 0x28)); }
  void node_set_owner(Handle n, int32_t id, void* owner) override {
    poke32(lo(n) + 0x40, addr_of(owner));
    poke32(lo(n) + 0x28, static_cast<uint32_t>(id));
  }
  void* node_owner(Handle n) override {
    if (!n || !(peek32(lo(n) + 0x28) & 0xff)) return nullptr;
    return obj_at<void>(peek32(lo(n) + 0x40));
  }
  float peek_float(const float* p) override { return zgame::from_bits(peek32(addr_of(p))); }
  // The skins (0x00475a80, 0x004757f0) stay calls out here: test_skins.py has them.
  Handle skins_decals(Handle skins, Handle node) override { return hd(call("0x00475a80", {ref(skins), 0, lo(node)})); }
  uint8_t skins_apply(Handle skins, m3::Mech& m, Handle skin) override {
    return static_cast<uint8_t>(call("0x004757f0", {ref(skins), ref(&m), ref(skin)}));
  }
  void entity_set_node(m3::Mech& m, Handle node, const char* name) override {
    call("0x004d6c90", {addr_of(&m), lo(node), name, 0, 0});
    m.node = node;
    m.transform = reinterpret_cast<float*>(hd(call("@transform", {})));
    m.target_node = hd(call("@target", {}));
    m.centroid = hd(call("@centroid", {}));
  }
  void controls_set_owner(m3::MechControls* c, m3::Mech& m) override {
    if (g_ictl.count(c)) call("ictl.vt+0x8", {g_ictl[c], addr_of(&m)});
    c->owner = &m;
  }
  uint32_t controls_ref(m3::MechControls* c) override {
    auto it = g_ictl.find(c);
    return it != g_ictl.end() ? it->second : addr_of(c) + 0x14c;
  }
  m3::MechAnim* gait_copy(m3::MechData& p, int32_t i) override {
    return obj_at<m3::MechAnim>(call("0x00431fb0", {addr_of(&p), i}));
  }
  m3::MechAction* action_copy(m3::MechData& p, int32_t i) override {
    return obj_at<m3::MechAction>(call("0x00432030", {addr_of(&p), i}));
  }
  m3::MechAnim* make_blend(m3::MechAnim* a, m3::MechAnim* b) override {
    return obj_at<m3::MechAnim>(call("0x0041ff30", {addr_of(a), addr_of(b)}));
  }
  void part_setup(m3::MechPart& p) override { call("part.vt+0x4", {addr_of(&p)}); }
  void part_init(m3::MechPart& p, m3::Config* cfg) override {
    if (p.init(cfg)) g_inited.insert(&p);
  }
  void collider_setup(m3::Mech& m) override { call("0x00422360", {addr_of(&m) + 0xfc, addr_of(&m)}); }
  void ski_set(m3::Skis& s, int32_t side) override { call("0x004764a0", {addr_of(&s), side, 0, 0}); }
  bool config_valid(m3::Config* cfg) override { return (call("0x004254d0", {addr_of(cfg), 0}) & 0xff) != 0; }
  void config_ungroup_equipment(m3::Config* cfg) override { call("0x00425960", {addr_of(cfg)}); }
  void configure_weapons(m3::Mech& m, m3::Config* cfg) override {
    g_configured = true;
    call("weapons", {addr_of(&m), addr_of(cfg)});
    auto* wm = static_cast<m3::WeaponManager*>(m.ammo);
    wm->count = call("@wcount", {});
    for (uint32_t i = 0; i < 5; ++i) wm->groups[i]->count = call("@wgroup", {i});
  }
  void weapons_select(m3::WeaponManager* wm, int32_t g) override { call("0x004b0730", {addr_of(wm), g}); }
  void set_command_callback(int32_t cmd) override { call("0x0051d2f0", {cmd, 0x00426cb0}); }
  bool joy_present() override { return call("0x0051dc10", {}) != 0; }
  int32_t torso_auto_return() override { return static_cast<int32_t>(call("0x0040d010", {})); }
  Handle view() override { return hd(g_view); }
  Handle view_camera(Handle v) override { return hd(peek32(lo(v) + 0x58)); }
  uint32_t camera_mode(Handle c) override { return peek32(lo(c) + 4); }
  void message_box(int32_t message, const char* name) override { call("0x004ac150", {0, 0x1e0, message, name}); }
};

struct Launch : m3::LaunchHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override { return call(key, args); }
  Handle missions() override { return hd(g_missions); }
  Handle pilot() override { return hd(g_pilot); }
  Handle view() override { return hd(g_view); }
  Handle interp() override { return hd(g_interp); }
  uint32_t field32(Handle o, uint32_t off) override { return peek32(lo(o) + off); }
  void set_field32(Handle o, uint32_t off, uint32_t v) override { poke32(lo(o) + off, v); }
  void set_field8(Handle o, uint32_t off, uint8_t v) override { poke8(lo(o) + off, v); }
  m3::Config* config_of(uint32_t h) override { return obj_at<m3::Config>(h); }
  Handle rec_ref(const m3::ai::AivRecord*) override { return hd(g_rec); }
  void clear_60b570() override { poke8(0x60b570, 0); }
  void mech_attach_view(m3::Mech& m) override {
    call("0x0041ce30", {addr_of(&m), static_cast<uint32_t>(zmod::zones().mode)});
  }
  m3::ai::AivRecord* aiv_read_named(m3::ai::AivRecord& rec, const char* name) override {
    if (!call("0x004e0680", {g_rec, name}) || !g_have_record) return nullptr;
    std::memcpy(rec.raw + 4, g_record + 4, sizeof g_record - 4);
    return &rec;
  }
};

Hooks g_hooks;
Launch g_launch;
m3::Mech* g_mech = nullptr;

// --- the images -------------------------------------------------------------
enum Kind {
  kMech, kHip, kTorso, kHead, kArm, kHand, kLeg, kCalf, kFoot, kManager, kGroup, kAms, kSkis,
  kListHead, kPair, kWrapper, kToeNodes, kToeQuats, kControls,
};
std::map<const void*, Kind> g_kind;
std::map<const void*, const m3::Foot*> g_toes;  // a toe array's foot

void classify() {
  g_kind.clear();
  g_toes.clear();
  m3::Mech* m = g_mech;
  if (m3::PlayerControls* pc = m3::player_controls_instance()) g_kind[pc] = kControls;
  if (!m) return;
  g_kind[m] = kMech;
  g_kind[&m->parts] = g_kind[&m->actions] = g_kind[&m->net_shots] = g_kind[&m->x6d0] = kListHead;
  g_kind[m->tracked] = kPair;
  if (m->hip) g_kind[m->hip] = kHip;
  if (m->torso) g_kind[m->torso] = kTorso;
  if (m->head) g_kind[m->head] = kHead;
  for (m3::Arm* a : m->arms)
    if (a) g_kind[a] = kArm, g_kind[a->hand] = kHand;
  for (m3::Leg* l : m->legs) {
    if (!l) continue;
    g_kind[l] = kLeg, g_kind[l->calf] = kCalf, g_kind[l->foot] = kFoot;
    if (l->foot->toe_nodes) g_kind[l->foot->toe_nodes] = kToeNodes, g_toes[l->foot->toe_nodes] = l->foot;
    if (l->foot->toe_q) g_kind[l->foot->toe_q] = kToeQuats, g_toes[l->foot->toe_q] = l->foot;
  }
  if (auto* wm = static_cast<m3::WeaponManager*>(m->ammo)) {
    g_kind[wm] = kManager;
    for (m3::WeaponGroup* g : wm->groups) g_kind[g] = kGroup;
  }
  if (m->ams) g_kind[m->ams] = kAms;
  if (m3::Skis* s = m->skis) {
    g_kind[s] = kSkis;
    g_kind[&s->base] = g_kind[&s->ski[0]] = g_kind[&s->ski[1]] = kWrapper;
  }
}

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
  void i(uint32_t off, int32_t v) { put(off, &v, 4); }
  void f(uint32_t off, float v) { put(off, &v, 4); }
  void b8(uint32_t off, uint32_t v) {
    const uint8_t x = static_cast<uint8_t>(v);
    put(off, &x, 1);
  }
  void p(uint32_t off, const void* v) { u(off, addr_of(v)); }
  void v3(uint32_t off, const m3::Vec3& v) { f(off, v.x), f(off + 4, v.y), f(off + 8, v.z); }
  void q(uint32_t off, const float* v) {
    for (int k = 0; k < 4; ++k) f(off + 4 * k, v[k]);
  }
  void fs(uint32_t off, const float* v, int n) {
    for (int k = 0; k < n; ++k) f(off + 4 * k, v[k]);
  }
  void list(uint32_t off, const void* head) {  // an MSVC list: its allocator byte (not set), head, size
    p(off + 4, head);
    u(off + 8, 0);
  }
};

void part_image(Img& o, const m3::MechPart& p, uint32_t vtable) {
  o.u(0, vtable);
  o.p(4, p.mech);
  o.p(8, p.node);
  o.p(0xc, p.found), o.p(0x10, p.lod);
  o.p(0x14, p.rotation), o.p(0x18, p.translation), o.p(0x1c, p.x1c), o.p(0x20, p.x20), o.p(0x24, p.x24);
  o.p(0x28, p.x28);
  o.f(0x2c, p.x2c), o.u(0x30, p.x30), o.f(0x34, p.recoil);
  o.f(0x38, p.internal), o.f(0x3c, p.internal_scale), o.f(0x40, p.armor);
  if (g_inited.count(&p)) o.f(0x44, p.armor_scale);
  o.b8(0x48, p.destroyed), o.b8(0x49, p.submerged), o.b8(0x4a, p.pending);
  o.v3(0x4c, p.pending_pos), o.q(0x58, p.pending_q);
  o.p(0x68, p.x68), o.p(0x6c, p.debris_effect), o.p(0x70, p.trail_effect);
}

void mech_image(Img& o, const m3::Mech& m) {
  o.u(0, 0x005bcef8);
  o.p(4, m.node), o.b8(8, m.active), o.b8(9, m.x09), o.p(0xc, m.transform);
  o.p(0x10, m.target_node), o.p(0x14, m.centroid);
  o.v3(0x18, m.x18), o.v3(0x24, m.forward), o.v3(0x30, m.velocity), o.v3(0x3c, m.vec_3c);
  o.q(0x48, m.orientation), o.v3(0x58, m.vec_58), o.b8(0x64, m.x64);
  o.f(0x68, m.height);
  o.p(0x6c, m.target), o.p(0x70, m.ally_target), o.p(0x74, m.enemy_target), o.p(0x78, m.saved_target);
  o.p(0x7c, m.own_lance), o.p(0x80, m.nearest_lance);
  o.b8(0x84, m.x84);
  o.p(0x88, m.tracked), o.u(0x8c, addr_of(m.tracked) + 8), o.u(0x90, addr_of(m.tracked) + 8);
  o.p(0x94, m.own_player), o.p(0x98, m.player_contact), o.f(0x9c, m.sensor_range), o.f(0xa0, m.range), o.f(0xa4, m.range2);
  o.b8(0xa8, m.xa8);
  for (int k = 0; k < 3; ++k) o.u(0xac + 4 * k, 0);  // the contacts: none
  o.p(0xb8, m.touching);
  o.u(0xc0, m.team), o.u(0xc4, m.allies), o.u(0xc8, m.enemies), o.u(0xcc, m.shadow ? 1u : 0u), o.p(0xd0, m.net);
  o.p(0xd4, m.ammo), o.p(0xd8, m.net_collision);
  o.b8(0xdc, m.power_down_pending), o.b8(0xdd, m.heat_shutdown), o.b8(0xde, m.powered_down);
  o.f(0xe0, m.last_hit_time), o.p(0xe4, m.last_hit_part), o.p(0xe8, m.xe8), o.i(0xf8, m.xf8);
  const m3::Collider& c = m.collider;
  o.i(0xfc, c.clear);
  for (uint32_t k = 0; k < 6; ++k) o.u(0x100 + 4 * k, 0);  // the five lists, empty
  o.f(0x118, c.restitution), o.f(0x11c, c.yaw_kick), o.i(0x120, c.count);
  for (int k = 0; k < 15; ++k) o.v3(0x124 + 12 * k, c.local[k]), o.v3(0x1d8 + 12 * k, c.world[k]),
                               o.v3(0x28c + 12 * k, c.last[k]);
  o.fs(0x340, c.matrix, 12);
  o.u(0x370, c.x370);
  for (int k = 0; k < 4; ++k) o.b8(0x374 + k, m.blocked[k]);
  o.p(0x378, m.chat), o.p(0x37c, m.controls);
  o.list(0x380, &m.parts);
  o.p(0x38c, m.legs[0]), o.p(0x390, m.legs[1]), o.p(0x394, m.arms[0]), o.p(0x398, m.arms[1]);
  o.p(0x39c, m.torso), o.p(0x3a0, m.hip), o.p(0x3a4, m.head), o.p(0x3a8, m.jet_node);
  for (int k = 0; k < 15; ++k) o.i(0x3ac + 4 * k, m.equipment[k]);
  o.p(0x3e8, m.decals), o.p(0x3ec, m.config), o.p(0x3f0, m.ams);
  for (int k = 0; k < 6; ++k) o.u(0x3f4 + 4 * k, m.x3f4[k]);
  o.f(0x40c, m.x40c), o.b8(0x410, m.damaged), o.b8(0x411, m.killed_by_nuke), o.f(0x414, m.alive_time);
  o.p(0x41c, m.skis), o.i(0x420, m.anim_index);
  for (int k = 0; k < 13; ++k) o.p(0x424 + 4 * k, m.anims[k]);
  for (int k = 0; k < 9; ++k) o.p(0x458 + 4 * k, m.action_objects[k]);
  o.list(0x47c, &m.actions);
  o.f(0x510, m.walk_speed), o.f(0x514, m.x514), o.f(0x518, m.max_speed);
  o.u(0x51c, m.x51c), o.f(0x520, m.turn_rate), o.u(0x524, m.x524);
  o.f(0x528, m.speed_factor), o.f(0x52c, m.speed_target);
  if (g_configured) o.f(0x530, m.throttle_up), o.f(0x534, m.throttle_down), o.f(0x538, m.turn_accel);
  o.f(0x53c, m.knock), o.v3(0x540, m.knock_vec), o.f(0x54c, m.lean), o.f(0x550, m.x550);
  o.f(0x554, m.heat), o.f(0x558, m.heat_added), o.f(0x55c, m.heat_dissipation), o.f(0x560, m.jet_burn);
  o.f(0x564, m.jet_fuel), o.b8(0x568, m.infinite_fuel), o.f(0x56c, m.coolant), o.f(0x570, m.narc_time);
  o.f(0x574, m.masc_time), o.i(0x578, m.anim_state), o.i(0x57c, m.prev_state), o.i(0x580, m.jump_jets_firing);
  o.b8(0x584, m.no_power_down), o.b8(0x585, m.knee_back), o.b8(0x586, m.crouched), o.b8(0x587, m.hud_saved);
  o.b8(0x588, m.wading), o.u(0x58c, m.gait_frames), o.f(0x590, m.lie_time), o.v3(0x594, m.impulse);
  o.v3(0x5a0, m.push), o.i(0x5ac, m.limp), o.b8(0x5b0, m.collides);
  for (int k = 0; k < 25; ++k) o.p(0x5b8 + 4 * k, m.sounds[k]);
  o.u(0x61c, m.x61c), o.p(0x620, m.jet_effect), o.u(0x624, m.x624), o.u(0x628, m.x628), o.u(0x62c, m.x62c);
  o.b8(0x630, m.invincible), o.u(0x634, m.x634), o.i(0x638, m.hud_fall), o.f(0x63c, m.x63c), o.v3(0x640, m.x640);
  o.i(0x658, m.heat_sinks.tech), o.i(0x65c, m.heat_sinks.count);
  if (g_configured) o.u(0x660, m.heat_sinks.doubles ? 1 : 0);  // the configuration's dword
  else o.b8(0x660, m.heat_sinks.doubles);
  o.p(0x664, m.attacker), o.list(0x674, &m.net_shots), o.u(0x680, m.net_damaged), o.p(0x684, m.pilot);
  o.u(0x688, m.detail), o.p(0x68c, m.x68c), o.u(0x690, m.x690), o.p(0x694, m.model_node), o.p(0x698, m.lod_node);
  o.u(0x69c, m.heat_state), o.f(0x6a0, m.shutdown_time), o.f(0x6a4, m.beep_time), o.f(0x6a8, m.voice_time);
  o.u(0x6ac, m.x6ac), o.f(0x6b0, m.frame_dt), o.f(0x6b4, m.dead_time), o.fs(0x6b8, m.knockdown, 5);
  o.list(0x6cc, &m.x6d0);
}

void image(Img& o, const void* obj, Kind k) {
  using namespace m3;
  switch (k) {
    case kMech: mech_image(o, *static_cast<const Mech*>(obj)); break;
    case kHip: {
      const auto& p = *static_cast<const Hip*>(obj);
      part_image(o, p, 0x005c28a8);
      o.q(0x74, p.rot), o.f(0x94, p.track), o.v3(0x98, p.offset);
      o.f(0xa4, p.target_x), o.f(0xa8, p.crouch_target), o.f(0xac, p.target_z);
      break;
    }
    case kTorso: {
      const auto& p = *static_cast<const Torso*>(obj);
      part_image(o, p, 0x005c29e0);
      o.p(0x74, p.death_effect), o.p(0x78, p.death_effect2), o.f(0x7c, p.half_width), o.f(0x80, p.half_depth);
      o.i(0x84, p.mode), o.i(0x88, p.aim_local), o.f(0x98, p.facing), o.i(0x9c, p.aim);
      o.b8(0xa0, p.absolute), o.b8(0xa1, p.pitch_lock), o.b8(0xa2, p.view_aim), o.q(0xa4, p.view_q);
      o.f(0xb4, p.look_yaw_angle), o.f(0xb8, p.look_pitch_angle), o.f(0xbc, p.twist_in), o.f(0xc0, p.pitch_in);
      o.f(0xd0, p.kick), o.f(0xd4, p.kick_twist), o.v3(0xd8, p.jolt_spin), o.v3(0xe4, p.jolt_turn);
      o.q(0xf0, p.jolt_q), o.v3(0x100, p.jolt), o.v3(0x10c, p.jolt_pos), o.q(0x118, p.turn_q), o.q(0x128, p.rot);
      o.f(0x138, p.pitch), o.f(0x13c, p.yaw), o.f(0x140, p.min_pitch), o.f(0x144, p.max_pitch);
      o.f(0x148, p.max_yaw), o.v3(0x160, p.front), o.v3(0x16c, p.flat_front);
      if (g_inited.count(&p) && p.mech->config)
        for (int s = 1; s <= 5; ++s) o.f(0x174 + 4 * s, p.sec_armor[s]), o.f(0x188 + 4 * s, p.sec_armor_scale[s]);
      o.f(0x1a0, p.side_internal[0]), o.f(0x1a4, p.side_internal[1]);
      if (g_inited.count(&p)) o.f(0x1a8, p.side_internal_scale[0]), o.f(0x1ac, p.side_internal_scale[1]);
      o.i(0x1b0, p.centring);
      break;
    }
    case kHead: part_image(o, *static_cast<const Head*>(obj), 0x005c2b80); break;
    case kCalf: part_image(o, *static_cast<const Calf*>(obj), 0x005c2b18); break;
    case kArm: {
      const auto& p = *static_cast<const Arm*>(obj);
      part_image(o, p, 0x005c2a48);
      o.p(0x74, p.hand), o.f(0x78, p.length), o.f(0x7c, p.recoil_hold);
      o.f(0x98, p.aim_pitch), o.f(0x9c, p.aim_yaw), o.f(0xa0, p.xa0), o.q(0xa4, p.aim_q);
      break;
    }
    case kHand: {
      const auto& p = *static_cast<const Hand*>(obj);
      part_image(o, p, 0x005c2ab0);
      o.q(0x74, p.rot);
      break;
    }
    case kLeg: {
      const auto& p = *static_cast<const Leg*>(obj);
      part_image(o, p, 0x005c2978);
      o.p(0x74, p.calf), o.p(0x78, p.foot), o.q(0x88, p.thigh_q), o.q(0x98, p.calf_q);
      o.f(0xa8, p.rest_x), o.f(0xac, p.thigh_len), o.f(0xb0, p.ik_b0), o.f(0xb4, p.hip_x);
      break;
    }
    case kFoot: {
      const auto& p = *static_cast<const Foot*>(obj);
      part_image(o, p, 0x005c2910);
      o.f(0x74, p.sole), o.p(0x78, p.shadow), o.f(0x8c, p.clearance), o.f(0x90, p.last_clearance);
      o.i(0x94, p.ground_state), o.u(0x98, p.x98), o.u(0x9c, p.x9c), o.u(0xa0, p.toe_count);
      o.p(0xa4, p.toe_nodes), o.p(0xa8, p.toe_q), o.b8(0xac, p.in_water), o.u(0xb0, p.xb0), o.u(0xb4, p.xb4);
      o.u(0xb8, p.xb8), o.f(0xbc, p.xbc), o.b8(0xc0, p.pivot), o.q(0xc4, p.rot), o.f(0xd4, p.pivot_rate);
      o.f(0xd8, p.spin), o.f(0xdc, p.water_depth), o.f(0xe0, p.water_surface), o.p(0xe4, p.effect);
      break;
    }
    case kManager: {
      const auto& w = *static_cast<const WeaponManager*>(obj);
      o.b8(0, w.trigger_down), o.b8(1, w.trigger_was), o.f(8, w.alpha_end);
      o.f(0xc, w.aim.x), o.f(0x10, w.aim.y), o.f(0x14, w.aim.z), o.b8(0x18, w.aim_valid);
      o.i(0x1c, w.mode), o.i(0x20, w.destroyed), o.u(0x24, w.count), o.i(0x28, w.group);
      for (int g = 0; g < 5; ++g) o.p(0x2c + 4 * g, w.groups[g]);
      o.p(0x80, w.owner), o.p(0x84, w.vehicle), o.b8(0x88, w.has_target), o.b8(0x89, w.online);
      o.f(0x8c, w.ammo_value), o.f(0x90, w.ammo_value_inv);
      break;
    }
    case kGroup: {
      const auto& g = *static_cast<const WeaponGroup*>(obj);
      o.p(0, g.manager), o.i(4, g.index), o.u(8, g.count), o.i(0xc, g.current), o.p(0x10, g.current_weapon);
      break;
    }
    case kAms: {
      const auto& a = *static_cast<const Ams*>(obj);
      o.b8(0, a.on), o.f(4, a.range), o.f(8, a.next), o.f(0xc, a.period), o.p(0x14, a.target), o.i(0x18, a.rounds);
      break;
    }
    case kSkis: {
      const auto& s = *static_cast<const Skis*>(obj);
      o.p(0, s.mech), o.b8(4, s.slid);
      o.p(0x38, &s.base), o.p(0x3c, &s.ski[0]), o.p(0x40, &s.ski[1]);
      o.b8(0x6c, s.down[0]), o.b8(0x6d, s.down[1]), o.f(0x70, s.slope[0]), o.f(0x74, s.slope[1]);
      o.f(0x78, s.half_track);
      for (int k = 0; k < 7; ++k) {
        o.v3(0x7c + 12 * k, s.world[k]), o.v3(0xd0 + 12 * k, s.local[k]);
        o.f(0x124 + 4 * k, s.ground[k]), o.f(0x140 + 4 * k, s.vy[k]), o.f(0x2ac + 4 * k, s.clearance[k]);
        o.b8(0x2e4 + k, s.landed[k]);
        const GroundHit& c = s.contact[k];
        const uint32_t at = 0x194 + 0x28 * k;
        o.v3(at, c.normal), o.f(at + 0xc, c.x), o.f(at + 0x10, c.height), o.f(at + 0x14, c.z);
        o.put(at + 0x18, c.zone, 8), o.p(at + 0x20, c.material), o.p(at + 0x24, c.node);
      }
      o.f(0x2ec, s.length), o.f(0x2f0, s.half), o.f(0x2f8, s.inv_half), o.f(0x2fc, s.inv_half2);
      break;
    }
    case kListHead: o.p(0, obj), o.p(4, obj); break;
    case kPair: o.u(0, 0), o.u(4, 0); break;
    case kWrapper: o.u(0, 0x005bd2f4), o.p(4, *static_cast<const Handle*>(obj)); break;
    case kToeNodes: {
      const Foot* f = g_toes[obj];
      for (uint32_t t = 0; t < f->toe_count; ++t) o.p(4 * t, f->toe_nodes[t]);
      break;
    }
    case kToeQuats: {
      const Foot* f = g_toes[obj];
      for (uint32_t t = 0; t < f->toe_count; ++t) o.q(16 * t, f->toe_q[t]);
      break;
    }
    case kControls: {
      const auto& c = *static_cast<const PlayerControls*>(obj);
      o.u(0, 0x005bd710), o.u(4, 0x005bd730), o.u(8, c.flags), o.i(0xc, c.view);
      o.f(0x10, c.throttle), o.f(0x14, c.turn), o.f(0x18, c.pitch), o.f(0x1c, c.twist), o.f(0x20, c.jump);
      o.f(0x24, c.jj_strafe), o.f(0x28, c.jj_forward), o.f(0x2c, c.axis_2c), o.f(0x30, c.axis_30);
      o.b8(0x34, c.fire), o.i(0x38, c.wrapped), o.b8(0x3c, c.joy_throttle_set), o.b8(0x3d, c.jump_held);
      o.b8(0x3e, c.throttle_held), o.b8(0x3f, c.throttle_returns), o.f(0x40, c.joy_throttle);
      o.f(0x44, c.throttle_target), o.f(0x48, c.turn_target), o.f(0x4c, c.jump_target), o.b8(0x50, c.reverse);
      o.u(0x54, c.last_pov), o.b8(0x58, c.moves[0]), o.b8(0x59, c.moves[1]), o.b8(0x5a, c.x5a);
      o.u(0x5c, 0x005bd708);
      o.f(0x138, c.look_dx), o.f(0x13c, c.look_dy), o.f(0x140, c.look_x), o.f(0x144, c.look_y);
      o.u(0x148, 0), o.u(0x14c, 0x005bd650), o.u(0x150, 0), o.u(0x154, 0), o.u(0x158, 0), o.p(0x15c, c.owner);
      break;
    }
  }
}

// --- the data images: a definition with its skins, configurations and firepoints --
struct Layout {
  std::vector<uint8_t> b;
  uint32_t base;
  uint32_t take(uint32_t n) {
    const uint32_t at = static_cast<uint32_t>(b.size());
    b.resize(at + ((n + 15) & ~15u), 0);
    return base + at;
  }
  void u(uint32_t a, uint32_t v) { std::memcpy(&b[a - base], &v, 4); }
  void f(uint32_t a, float v) { std::memcpy(&b[a - base], &v, 4); }
  void b8(uint32_t a, uint8_t v) { b[a - base] = v; }
  uint32_t str(const char* s) {
    const uint32_t n = static_cast<uint32_t>(std::strlen(s)) + 1;
    const uint32_t a = take(n);
    std::memcpy(&b[a - base], s, n);
    return a;
  }
};

void config_image(Layout& l, uint32_t a, const m3::Config& c) {
  const uint32_t items = l.take(0xc);  // an empty item list's head
  l.u(items, items), l.u(items + 4, items);
  l.u(a, 0x005bd3c0), l.u(a + 8, items);
  l.b8(a + 0x10, c.base_flag), l.u(a + 0x14, static_cast<uint32_t>(c.mech));
  l.b8(a + 0x1c, c.player_made), l.b8(a + 0x1d, c.b1d), l.b8(a + 0x1e, c.training), l.b8(a + 0x1f, c.modified);
  l.b8(a + 0x20, c.endo_steel), l.u(a + 0x24, static_cast<uint32_t>(c.engine)), l.b8(a + 0x28, c.xl);
  l.f(a + 0x2c, c.armor_tons), l.u(a + 0x30, static_cast<uint32_t>(c.armor_tech)), l.b8(a + 0x34, c.ferro);
  for (int k = 0; k < 11; ++k) l.u(a + 0x38 + 4 * k, static_cast<uint32_t>(c.armor[k]));
  l.u(a + 0x64, static_cast<uint32_t>(c.sink_tech)), l.u(a + 0x68, static_cast<uint32_t>(c.sinks));
  l.b8(a + 0x6c, c.double_sinks);
  l.u(a + 0x70, c.g70), l.f(a + 0x74, c.turn_rate), l.u(a + 0x78, c.g78), l.f(a + 0x7c, c.accel_rate);
  l.f(a + 0x80, c.decel_rate), l.f(a + 0x84, c.turn_accel), l.u(a + 0x88, c.crc);
}

int dfn_layout(m3::MechDfn& d, uint32_t base, uint8_t* out, int cap, bool bind_objects) {
  Layout l{{}, base};
  const uint32_t a = l.take(0x1a8);
  std::memcpy(&l.b[0], d.raw, 0x1a8);
  l.u(a + 0x08, 0);  // the reader tree is not read from here
  if (bind_objects) bind(&d, a);
  if (m3::SkinSet* s = d.skins) {
    const uint32_t ss = l.take(0x14);
    const uint32_t n = static_cast<uint32_t>(s->skins.size());
    const uint32_t arr = n ? l.take(4 * n) : 0;
    for (uint32_t k = 0; k < n; ++k) {
      const uint32_t sk = l.take(0x18);
      if (bind_objects) bind(s->skins[k], sk);
      l.u(arr + 4 * k, sk);
    }
    l.u(ss + 4, arr), l.u(ss + 8, arr + 4 * n), l.u(ss + 0xc, arr + 4 * n);
    l.u(a + 0x1c, ss);
    if (bind_objects) bind(s, ss);
  } else {
    l.u(a + 0x1c, 0);
  }
  if (m3::ConfigList* cl = d.configs) {
    const uint32_t ca = l.take(0x18);
    const uint32_t n = static_cast<uint32_t>(cl->configs.size());
    const uint32_t arr = n ? l.take(4 * n) : 0;
    l.u(ca, static_cast<uint32_t>(cl->count)), l.u(ca + 4, static_cast<uint32_t>(cl->mech));
    l.u(ca + 0xc, arr), l.u(ca + 0x10, arr + 4 * n), l.u(ca + 0x14, arr + 4 * n);
    for (uint32_t k = 0; k < n; ++k) {
      const uint32_t c = l.take(0xd8);
      l.u(arr + 4 * k, c);
      config_image(l, c, *cl->configs[k]);
      if (bind_objects) bind(cl->configs[k], c);
    }
    l.u(a + 0x20, ca);
  } else {
    l.u(a + 0x20, 0);
  }
  if (auto* fps = d.firepoints) {
    const uint32_t v = l.take(0x10);
    const uint32_t n = static_cast<uint32_t>(fps->size());
    const uint32_t arr = n ? l.take(4 * n) : 0;
    l.u(v + 4, arr), l.u(v + 8, arr + 4 * n), l.u(v + 0xc, arr + 4 * n);
    for (uint32_t k = 0; k < n; ++k) {
      const m3::Firepoint& fp = *(*fps)[k];
      const uint32_t fa = l.take(0x20);
      l.u(arr + 4 * k, fa);
      l.u(fa, fp.name ? l.str(fp.name) : 0);
      l.f(fa + 4, fp.radius), l.f(fa + 8, fp.share), l.u(fa + 0xc, static_cast<uint32_t>(fp.part));
      l.u(fa + 0x10, static_cast<uint32_t>(fp.model_part));
      for (int c = 0; c < 3; ++c) l.f(fa + 0x14 + 4 * c, fp.position[c]);
      if (bind_objects) bind(&fp, fa);
    }
    l.u(a + 0x24, v);
  } else {
    l.u(a + 0x24, 0);
  }
  for (int k = 0; k < 18; ++k) {
    const m3::PartSlot& s = d.parts[k];
    const uint32_t at = a + 0x28 + 0x10 * k;
    l.u(at, s.name ? l.str(s.name) : 0);
    l.f(at + 4, s.f4), l.u(at + 8, static_cast<uint32_t>(s.i8)), l.u(at + 0xc, static_cast<uint32_t>(s.id));
  }
  if (static_cast<int>(l.b.size()) > cap) return -static_cast<int>(l.b.size());
  std::memcpy(out, l.b.data(), l.b.size());
  return static_cast<int>(l.b.size());
}

// A tree read from mechlib.zbd, for the test to lay out as fake nodes.
std::vector<zcls::Node*> g_tree;
void walk(zcls::Node* n) {
  g_tree.push_back(n);
  for (int32_t i = 0; n->children && i < n->child_count(); ++i) walk(n->children[i]);
}

}  // namespace

extern "C" {

void probe_mb_reset(uint32_t missions, uint32_t pilot, uint32_t view, uint32_t interp) {
  g_addr.clear(), g_obj.clear(), g_inited.clear(), g_names.clear(), g_ictl.clear();
  g_configured = false;
  g_missions = missions, g_pilot = pilot, g_view = view, g_interp = interp;
  g_mech = nullptr;
  m3::player_controls_instance() = nullptr;  // (leaked: each case starts the singleton afresh)
  m3::mech_build_state() = m3::MechBuildState{};
  m3::set_mech_build_hooks(&g_hooks);
  m3::set_launch_hooks(&g_launch);
  m3::axis_table().ready = 0;
  m3::controls_calls() = m3::ControlsCalls{};
  m3::controls_calls().joy_use = [] { call("0x0051b2c0", {}); };
  m3::DamageCalls& d = m3::damage_calls();
  d = m3::DamageCalls{};
  d.anim_speed = [](Handle a, float v) { call("0x0041ef90", {addr_of(a), fb(v)}); };
  d.gait_speed = [](m3::Mech& m, float v) { call("0x004329c0", {addr_of(m.pilot), addr_of(&m) + 0x4a0, fb(v)}); };
  m3::weapon_calls() = m3::WeaponCalls{};
  m3::weapon_calls().vehicle_mech = [](Handle v) { return v; };
  zfix::options().bugs = false;
  zfix::options().per_second = false;
}

// The definitions from reader.zbd through the ported reader (saved loadouts off).
int probe_mb_open(const char* reader) {
  zrdr::close_archives(true);
  zrdr::init();
  return zrdr::open_archive(reader, true) ? 1 : 0;
}

int probe_mb_config_count(int roster) {
  m3::MechDfn* d = m3::mech_dfn(roster);
  return d && d->configs ? static_cast<int>(d->configs->configs.size()) : -1;
}

// A configuration's fields changed for a crafted case: 0 engine, 1 sinks,
// 2 double sinks, 3 sink tech, 4 armor slot (v >> 16) = v & 0xffff, 5 turn,
// 6 accel, 7 decel, 8 turn accel (floats as bits), 9 the mech, 10 g70, 11 g78.
uint32_t probe_mb_config_set(int roster, int i, int field, uint32_t v) {
  m3::Config& c = *m3::mech_dfn(roster)->configs->configs[i];
  auto swap = [](auto& f, auto x) {
    uint32_t old = 0;
    std::memcpy(&old, &f, sizeof f);
    std::memcpy(&f, &x, sizeof f);
    return old;
  };
  switch (field) {
    case 0: return swap(c.engine, static_cast<int32_t>(v));
    case 1: return swap(c.sinks, static_cast<int32_t>(v));
    case 2: return swap(c.double_sinks, static_cast<uint8_t>(v));
    case 3: return swap(c.sink_tech, static_cast<int32_t>(v));
    case 4: return swap(c.armor[(v >> 16) % 11], static_cast<int32_t>(v & 0xffff));
    case 5: return swap(c.turn_rate, zgame::from_bits(v));
    case 6: return swap(c.accel_rate, zgame::from_bits(v));
    case 7: return swap(c.decel_rate, zgame::from_bits(v));
    case 8: return swap(c.turn_accel, zgame::from_bits(v));
    case 9: return swap(c.mech, static_cast<int32_t>(v));
    case 10: return swap(c.g70, v);
    case 11: return swap(c.g78, v);
    default: return 0;
  }
}

// The definition and what hangs off it laid out at `base` (returns the size,
// negative when `cap` is short). `bind` 1 maps the port's objects to it.
int probe_mb_dfn_image(int roster, uint32_t base, uint8_t* out, int cap, int bind_objects) {
  m3::MechDfn* d = m3::mech_dfn(roster);
  return d ? dfn_layout(*d, base, out, cap, bind_objects != 0) : 0;
}

// Drop a definition's skins (a crafted case), keeping the pointer to put back.
void* probe_mb_dfn_skins(int roster, void* skins) {
  m3::MechDfn* d = m3::mech_dfn(roster);
  void* old = d->skins;
  d->skins = static_cast<m3::SkinSet*>(skins);
  return old;
}

// The mech type at `addr` (its image is the test's): roster, model trees,
// the cockpit block (x228 at `block`, its [1] the node) or none.
void probe_mb_pilot(uint32_t addr, int roster, uint32_t model, uint32_t lod0, uint32_t lod1, uint32_t block,
                    uint32_t cockpit_node, uint32_t name_addr) {
  auto p = std::make_unique<m3::MechData>();
  p->roster = roster;
  p->name = cstr(name_addr);
  p->model = hd(model);
  p->models[0] = hd(lod0), p->models[1] = hd(lod1);
  if (block) {
    g_blocks.emplace_back(new m3::CockpitModel{hd(cockpit_node)});
    p->cockpit_model = g_blocks.back().get();
    bind(p->cockpit_model, block);
  }
  bind(p.get(), addr);
  g_pilots.push_back(std::move(p));
}

// An AI controller at `addr` (its IControls base at `ictl`).
void probe_mb_ai_controls(uint32_t addr, uint32_t ictl) {
  g_ctls.push_back(std::make_unique<m3::AiControls>());
  bind(g_ctls.back().get(), addr);
  g_ictl[g_ctls.back().get()] = ictl;
}

void probe_mb_record(const uint8_t* raw, int have, uint32_t addr) {
  std::memcpy(g_record, raw, sizeof g_record);
  g_have_record = have != 0;
  g_rec = addr;
}

// [0x00605628] the mechs made, [0x0060b56c], [0x005ea1d0] zModel's zone mode.
void probe_mb_globals(uint32_t* g, int set) {
  if (set) {
    m3::mech_build_state().made = static_cast<int32_t>(g[0]);
    if (g[1] != 0) m3::player_set_60b56c(static_cast<uint8_t>(g[1]));
    zmod::zones().mode = static_cast<int32_t>(g[2]);
    return;
  }
  g[0] = static_cast<uint32_t>(m3::mech_build_state().made);
  g[1] = m3::player_get_60b56c();
  g[2] = static_cast<uint32_t>(zmod::zones().mode);
}

void probe_mb_controls(uint32_t mode) { m3::player_controls(mode); }

int probe_mb_launch() {
  const bool ok = m3::launch_player();
  g_mech = m3::launched_mech();
  classify();
  return ok ? 1 : 0;
}

uint32_t probe_mb_create(int index, uint32_t ctl, int cfg, const char* name) {
  g_mech = m3::mech_create(index, obj_at<m3::MechControls>(ctl), cfg, name);
  classify();
  return addr_of(g_mech);
}

// The port's object at the original's block `addr`: its image over `buf`
// (pre-filled by the test) and the bytes it sets in `mask`. -1: no object.
int probe_mb_image(uint32_t addr, uint32_t size, uint8_t* buf, uint8_t* mask) {
  auto it = g_obj.find(addr);
  if (it == g_obj.end()) return -1;
  auto k = g_kind.find(it->second);
  if (k == g_kind.end()) return -2;
  Img o{buf, mask, size};
  image(o, it->second, k->second);
  return static_cast<int>(k->second);
}

// mechlib.zbd's tree `entry`: the node count, then probe_mb_tree_node for each
// (depth first, the root first).
int probe_mb_tree(const char* zbd, const char* entry) {
  static zutl_zar::Archive a{};
  zutl_zar::init(a);
  if (!zutl_zar::open(a, zbd)) return -1;
  std::FILE* f = zutl_zar::seek(a, zutl_zar::find(a, entry), nullptr);
  if (!f) return -2;
  zcls::nodes() = zcls::NodePool{};
  zmod::models() = zmod::ModelPool{};
  zmod::reset_pool();
  zcls::TreeReader reader;
  zcls::Node* root = zcls::read_tree(reader, f, nullptr);
  g_tree.clear();
  if (root) walk(root);
  return static_cast<int>(g_tree.size());
}

// One node: its name (36 bytes), class, parent and child indices (-1 none),
// whether it has a model, its +0x74 box and its object3d data +0x30..+0x60.
int probe_mb_tree_node(int i, char* name, int32_t* info, int32_t* children, int cap, float* box, float* data) {
  const zcls::Node* n = g_tree[i];
  std::memcpy(name, n->raw, 36);
  auto index = [](const zcls::Node* x) {
    for (size_t k = 0; k < g_tree.size(); ++k)
      if (g_tree[k] == x) return static_cast<int32_t>(k);
    return -1;
  };
  info[0] = n->cls();
  info[1] = n->parent_count() && n->parents ? index(n->parents[0]) : -1;
  info[2] = n->model != nullptr;
  int c = 0;
  for (int32_t k = 0; n->children && k < n->child_count() && c < cap; ++k) children[c++] = index(n->children[k]);
  std::memcpy(box, n->raw + 0x74, 24);
  if (n->data && n->cls() == 5) std::memcpy(data, static_cast<const uint8_t*>(n->data) + 0x30, 0x30);
  else std::memset(data, 0, 0x30);
  return c;
}

}  // extern "C"
