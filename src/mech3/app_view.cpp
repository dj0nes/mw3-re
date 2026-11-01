#include "mech3/app_view.h"
#include "mech3/mech_move.h"

#include "mech3/hud.h"
#include "mech3/mech_hud.h"

#include <cstring>
#include <memory>

#include "gamez/zclass/camera.h"
#include "mech3/app_hud.h"
#include "gamez/zclass/class.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_di.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/list.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "mech3/app_mech.h"
#include "mech3/mech.h"
#include "mech3/mech_control.h"
#include "mech3/mech_parts.h"
#include "mech3/mech_reader.h"
#include "mech3/player_view.h"
#include "mech3/app_vehicle.h"
#include "mech3/veh_basic.h"
#include "mech3/veh_basic_object.h"

namespace mech3 {

namespace {
ViewProgram g_p;
bool g_wired = false;
std::unique_ptr<PlayerView> g_view;
// The view's target as the program names it, and as a mech (null while the
// view is on the stealth object, the game editor's: editor.md S4).
void* g_target = nullptr;
Mech* g_target_mech = nullptr;

zcls::Node* zn(void* p) { return static_cast<zcls::Node*>(p); }

struct ProgramViewHooks : ViewHooks {
  void set_camera_option(int32_t m) override { program_camera_set(m); }  // 0x0040ce10 (the camera key, playtest #5)
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg>) override {
    return g_wired && g_p.unported ? g_p.unported(g_p.ctx, key) : 0;
  }
  void camera_set_position(Node3 cam, float x, float y, float z) override { zcls::camera_set_position(zn(cam), x, y, z); }
  void torso_cockpit(Mech& m, int32_t on) override {  // 0x0041ce10 -> 0x00464580 on the torso
    if (m.torso) m.torso->set_cockpit(on != 0);
  }
  void camera_set_rotation(Node3 cam, float x, float y, float z) override { zcls::camera_set_rotation(zn(cam), x, y, z); }
  void camera_add_position(Node3 cam, float x, float y, float z) override { zcls::camera_add_position(zn(cam), x, y, z); }
  void camera_set_horizon(Node3 cam, Node3 horizon) override {
    if (horizon) zcls::camera_set_horizon(zn(cam), zn(horizon));
  }
  Node3 find_node(int32_t list, const char* name) override { return zcls::list_find_by_name(list, name); }
  uint32_t node_flags(Node3 node) override {
    const zcls::Node* n = zn(node);
    return n && n->data ? *static_cast<const uint8_t*>(n->data) : 0;
  }
  // The object3d's world position (its data +0x84).
  const float* node_position(Node3 node) override {
    static float none[3] = {};
    const zcls::Node* n = zn(node);
    return n && n->data ? reinterpret_cast<const float*>(static_cast<const uint8_t*>(n->data) + 0x84) : none;
  }
  void node_refresh(Node3 node) override {
    zmth::Mat34 m;
    zmth::push(&m);
    zmth::load_identity();
    zcls::node_to_world(zn(node), 1);
    zmth::pop();
  }
  void node_world_position(Node3 node, float out[3]) override { zcls::node_world_position(zn(node), out); }
  Node3 node_parent(Node3 node) override {
    const zcls::Node* n = zn(node);
    return n && n->parent_count() > 0 ? n->parents[0] : nullptr;
  }
  void node_remove_child(Node3 parent, Node3 child) override {
    if (parent) zcls::node_remove_child(zn(parent), zn(child));
  }
  void node_add_child(Node3 parent, Node3 child) override {
    if (parent) zcls::node_add_child(zn(parent), zn(child));
  }
  Node3 world_node() override { return g_view ? zcls::camera_world(zn(g_view->camera)) : nullptr; }
  void node_set_intersect(Node3 node, int32_t on) override { zcls::node_set_intersect(zn(node), on); }
  void query_mode(int32_t on) override { unported("0x004ed9c0", {on}); }
  // Not ported yet (cls_di.c): no cover over the camera, no ground under it.
  int32_t segment_query(const float*, const float*, uint8_t* found) override {
    unported("0x004eed90", {});
    *found = 0;
    return 1;
  }
  const float* camera_position(Node3 cam) override {
    const zcls::Node* n = zn(cam);
    static float none[3] = {};
    return n && n->data ? reinterpret_cast<const float*>(static_cast<const uint8_t*>(n->data) + 0x14) : none;
  }
  // The ground under the camera (0x004edab0 on the world, kept for the
  // water check 0x004abd90): an outside camera is lifted out of the ground,
  // and under water the world takes the water's fog (0x0040b0e0).
  zcls::GroundHits ground{};
  bool ground_query(float x, float y, float z) override {
    ground.count = 0;
    Node3 w = world_node();
    return !w || zcls::ground_point(zn(w), x, y, z, &ground) != 0;
  }
  float water_query(float y, float height, int32_t* index, uint8_t flag, int32_t* kind, float* level) override {
    return ground_pick(ground, y, height, index, flag, kind, level);  // 0x004abd90
  }
  uint8_t water_flag() override { return 1; }
  // Mech3.exe 1.2 @ 0x0040b080 on the ground's hit `index`: the camera's
  // zone list from that polygon's (0x004f6c80), then the render's
  // [0x0074a328], the view's target's zones (zEntity's 0x005a80d0: its +0x64,
  // its node's zone byte through 0x005a80c0 -> 0x004eb810) and the
  // animations' (0x00502d70). docs/view.md section 1.
  void camera_zone(Node3 cam, int32_t index, Vehicle* target) override {
    if (index < 0 || index >= ground.count) return;
    zcls::Node* c = zn(cam);
    zcls::camera_set_zone(c, reinterpret_cast<const uint8_t*>(&ground.hit[index].zone[0]));
    if (!c || !c->data || c->cls() != 1) return;
    const uint8_t* list = static_cast<zcls::Camera*>(c->data)->raw + 0x1e4;
    std::memcpy(zmod::zones().list, list, 4);
    if (target) {
      std::memcpy(&target->x64, list, 4);
      zcls::node_set_zone(zn(target->node), list[1]);
    }
    zeff::anim_set_zone(list);
  }
  void world_fog_range(float a, float b) override {
    if (Node3 w = world_node()) zcls::world_set_fog_range(zn(w), a, b);
  }
  void world_fog_color(float r, float g, float b) override {
    if (Node3 w = world_node()) zcls::world_set_fog_color(zn(w), r, g, b);
  }
  void world_fog_altitude(float a, float b) override {
    if (Node3 w = world_node()) zcls::world_set_fog_altitude(zn(w), a, b);
  }
  bool has_weather() override { return false; }
  uint32_t weather_flags() override { return 0; }
  // The wall pull-in (0x0040b310) is the port's; its query, on the world.
  void world_segments(float* segs, int32_t points, zcls::SegmentHits* out) override {
    if (Node3 w = world_node()) zcls::segment_query(zn(w), segs, points, out);  // 0x004efcd0
  }
  const float* entity_position(Vehicle& t) override {
    static float p[3];
    zcls::node_world_position(zn(t.node), p);
    return p;
  }
  CameraMode* mech_camera(Mech& t, int32_t id) {
    auto* list = t.pilot ? static_cast<CameraList*>(t.pilot->cameras) : nullptr;
    return list ? list->find(id) : nullptr;
  }
  // A mech's vt+0x10c, else the stealth's (0x004acde0: the vehicles' list).
  CameraMode* target_camera(Vehicle& t, int32_t id) override {
    if (&t == g_target_mech) return mech_camera(*g_target_mech, id);
    return vehicle_camera(id);
  }
  // A mech's vt+0x100 and vt+0x104 do nothing here (the HUD's set-up is
  // program_view_attach's); the stealth's vt+0x100 is empty (0x005ac5c0),
  // its vt+0x104 (0x004a50d0) turns its overlay off.
  void target_attached(Vehicle&) override {}
  void target_detached(Vehicle& t) override { program_vehicle_view_detached(t); }
  uint32_t mech_dead(Mech& t) override { return t.anim_state == 10 ? 1u : 0u; }
  const float* torso_aim(Mech& t) override {
    static float q[4] = {1, 0, 0, 0};
    if (t.torso) t.torso->shake_q(q);
    return q;
  }
  // Mech3.exe 1.2 @ 0x00465900
  float torso_pitch_rate(Mech& t) override {
    const Torso* s = t.torso;
    if (!s || -s->max_yaw == s->yaw || s->yaw == s->max_yaw) return 0.0f;
    return s->twist_in;
  }
  // Mech3.exe 1.2 @ 0x004658c0
  float torso_yaw_rate(Mech& t) override {
    const Torso* s = t.torso;
    if (!s || s->pitch == s->min_pitch || s->pitch == s->max_pitch) return 0.0f;
    return s->pitch_in;
  }
  // The free look's and the cockpit's calls on the mech (playtest #14).
  void mech_41ca30(Mech& t, int32_t on) override { mech_set_hud_fall(&t, on); }  // 0x0041ca30
  void mech_41ca60(Mech& t, int32_t on) override {                              // 0x0041ca60
    t.x634 = on ? t.x634 | 1u : t.x634 & ~1u;
  }
  void torso_464ac0(Mech& t, int32_t on) override {
    if (t.torso) t.torso->set_pitch_lock(on != 0);
  }
  void torso_465a00(Mech& t, int32_t how) override {
    if (t.torso) t.torso->centre(how);
  }
  void torso_466190(Mech& t, int32_t m) override {
    if (t.torso) t.torso->set_mode(m);
  }
  void controls_426780(Mech& t, uint32_t v) override {
    if (t.controls) t.controls->set_view(static_cast<int32_t>(v));
  }
  // 0x00502da0: [0x0073f75c], the animations' "in the cockpit" (anim block +0x3c).
  void anim_502da0(int32_t on) override { zeff::anim().block.put<int32_t>(0x3c, on); }
  uint32_t controls_flags(Mech& t) override { return t.controls ? t.controls->flags : 0; }
  int32_t controls_kind(Mech& t) override { return t.controls ? t.controls->view : 0; }
  uint32_t option_5f4d44() override { return static_cast<uint32_t>(program_mouse_mode()); }
  uint8_t head_bob() override { return 1; }       // [0x005e2448]
  float head_bob_scale() override { return 0.15f; }  // [0x005e244c]
  Vehicle* player_target() override { return g_view ? g_view->target : nullptr; }
  // 0x00430aa0 on the view's mech (its HUD; a token, only handed back) and
  // the HUD's weapon list laid out again (0x00446240 on +0x6520).
  uint32_t mech_screen(Mech& t) override { return program_pilot_hud(t.pilot) ? 1 : 0; }
  void screen_look(uint32_t screen) override {
    Mech* t = program_view_mech();
    if (screen && t) program_hud_weapons_layout(program_pilot_hud(t->pilot));
  }
  uint32_t pilot_game() override { return 0; }
  uint8_t game_byte_8e(uint32_t) override { return 0; }
  const float* mission_fog(bool water) override { return program_mission_fog(water); }
  void* dfn_reader(int32_t roster) override {
    const MechDfn* d = mech_dfn(roster);
    return d ? d->reader : nullptr;
  }
  void* reader_find(void* node, const char* key) override {
    return node ? zrdr::find(static_cast<zrdr::Node*>(node), key) : nullptr;
  }
  bool reader_float(void* node, const char* key, float* out) override {
    return node && zrdr::get_float(static_cast<zrdr::Node*>(node), key, out);
  }
  bool reader_vec3(void* node, const char* key, float out[3]) override {
    return node && zrdr::get_vec3(static_cast<zrdr::Node*>(node), key, out);
  }
  bool view_fov(float* fov) override {
    if (!g_view || !g_view->camera) return false;
    const zcls::Node* n = zn(g_view->camera);
    *fov = n->data ? static_cast<const zcls::Camera*>(n->data)->get<float>(0xec) : 1.0471976f;
    return true;
  }
};
ProgramViewHooks g_hooks;
}  // namespace

void wire_view(const ViewProgram* p) {
  g_wired = p != nullptr;
  g_p = p ? *p : ViewProgram{};
  g_view.reset();
  set_view_hooks(p ? &g_hooks : nullptr);
  // The viewed mech's falls and getting up (0x00413af0 / 0x00413b70).
  move_calls().fall_camera = p ? +[](Mech& m) { if (g_view) g_view->mech_falls(m); } : nullptr;
  move_calls().restore_camera = p ? +[](Mech& m) { if (g_view) g_view->mech_stands(m); } : nullptr;
  // The controls' view state (0x00426780) asks the view and the mech's cameras.
  ControlsCalls& k = controls_calls();
  k.mech_camera = p ? +[](Mech& m, int32_t id) -> CameraMode* { return g_hooks.mech_camera(m, id); } : nullptr;
  k.view_target = p ? +[]() -> Mech* { return program_view_mech(); } : nullptr;
  k.view_mode = p ? +[]() -> CameraMode* { return g_view ? g_view->mode : nullptr; } : nullptr;
  // [0x005f4c8c] +0x58 +0x40: the view's camera's look (0 and 1 the side views).
  k.camera_view = p ? +[]() -> int { return g_view && g_view->mode ? g_view->mode->look_ : 0; } : nullptr;
  k.mw2_jets = p ? +[]() {
    int32_t mw2_jump_jets();  // options.cpp
    return mw2_jump_jets() != 0;
  } : nullptr;  // 0x0040d1e0, the MW2JJets option
}
PlayerView* program_view() { return g_view.get(); }
int32_t program_view_mode_id() { return g_view && g_view->mode ? g_view->mode->id : -1; }
bool program_view_look(int32_t* look) {
  if (!g_view || !g_view->mode) return false;
  *look = g_view->mode->look_;
  return true;
}
void* program_view_target() { return g_view && g_view->target ? g_target : nullptr; }
Mech* program_view_mech() { return g_view && g_view->target ? g_target_mech : nullptr; }
int32_t program_view_mode(bool* fixed) {
  *fixed = false;
  if (!g_view || !g_view->mode) return -1;
  if (auto* c = dynamic_cast<const CockpitCamera*>(g_view->mode)) *fixed = c->fixed != 0;
  return g_view->mode->id;
}
const uint8_t* program_view_camera_zones() {
  const zcls::Node* c = g_view ? zn(g_view->camera) : nullptr;
  if (!c || !c->data || c->cls() != 1) return nullptr;
  return static_cast<const zcls::Camera*>(c->data)->raw + 0x1e4;
}

bool program_view_positions(float camera[3], float torso[3]) {
  Mech* m = program_view_mech();
  if (!m || !m->torso) return false;
  zcls::node_world_position(zn(g_view->camera), camera);
  zcls::node_world_position(zn(m->torso->node), torso);
  return true;
}

namespace {
void sync() {
  if (g_p.sync) g_p.sync(g_p.ctx);
}
}  // namespace

void program_view_make(void* camera) {
  if (!g_wired) return;
  g_view = std::make_unique<PlayerView>(camera, nullptr, nullptr, g_p.hardware ? g_p.hardware(g_p.ctx) : 1);
  sync();
}

void program_view_free() {
  g_view.reset();
  g_target = nullptr, g_target_mech = nullptr;
}

// Mech3.exe 1.2 @ 0x0041ce30: the view onto the mech in its data's current
// camera, then the HUD's set-up for it (0x00430aa0, 0x0043bb80).
void program_view_attach(Mech& m) {
  if (!g_view) return;
  auto* list = m.pilot ? static_cast<CameraList*>(m.pilot->cameras) : nullptr;
  g_target = &m, g_target_mech = &m;
  g_view->attach(&m, list ? list->find(list->current) : nullptr);
  if (Hud* h = m.pilot ? pilot_hud(*m.pilot) : nullptr) h->setup(&m);
  sync();
}
void program_view_attach_vehicle(Vehicle& v, void* handle, CameraMode* mode) {
  if (!g_view) return;
  g_target = handle, g_target_mech = nullptr;
  g_view->attach(&v, mode);
  sync();
}
void program_view_detach() {
  if (!g_view) return;
  g_view->attach(nullptr, nullptr);
  g_target = nullptr, g_target_mech = nullptr;
  sync();
}
// The look keys and FREELOOK's: the view's mode vt+0x10 (0 left, 1 right,
// 2 ahead, 4 up, 5 down; 3 none).
void program_view_look(int32_t look) {
  if (!g_view || !g_view->mode) return;
  g_view->mode->set_look(look);
  sync();
}
bool program_view_ground_probe(float x, float y, float z, float* y_after, bool* under) {
  if (!g_view) return false;
  zcls::camera_set_position(zn(g_view->camera), x, y, z);
  const bool found = g_view->follow_ground();
  float p[3];
  zcls::node_world_position(zn(g_view->camera), p);
  if (const float* c = g_hooks.camera_position(g_view->camera)) p[1] = c[1];
  *y_after = p[1];
  *under = g_view->underwater != 0;
  return found;
}
bool program_view_find_water(float x0, float z0, float range, float out[4]) {
  Node3 w = g_view ? g_hooks.world_node() : nullptr;
  if (!w) return false;
  zcls::GroundHits hits{};
  for (float dz = -range; dz <= range; dz += 10.0f)
    for (float dx = -range; dx <= range; dx += 10.0f) {
      const float x = x0 + dx, z = z0 + dz;
      if (zcls::ground_point(zn(w), x, 2000.0f, z, &hits) != 0) continue;
      int32_t best = 0, in_water = 0;
      float surface = -200.0f;
      const float ground = ground_pick(hits, 2000.0f, 4000.0f, &best, 1, &in_water, &surface);  // water not ground, as the view
      if (surface > ground + 2.0f) {
        out[0] = x, out[1] = surface, out[2] = z, out[3] = ground;
        return true;
      }
    }
  return false;
}
int32_t program_view_look_state() { return g_view && g_view->mode ? g_view->mode->look_ : -1; }
int32_t program_view_camera_id() { return g_view && g_view->mode ? g_view->mode->id : -1; }
void program_view_set_camera(int32_t id) {
  if (!g_view) return;
  g_view->set_camera_mode(id);
  sync();
}
void* program_cameras(int32_t roster) { return make_cameras(roster); }
void program_cameras_free(void* list) { delete static_cast<CameraList*>(list); }

}  // namespace mech3
