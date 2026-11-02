#include "mech3/veh_stealth.h"

#include "gamez/zreader/zreader.h"

namespace mech3 {

namespace {
StealthHooks g_default_hooks;
StealthHooks* g_hooks = &g_default_hooks;
const float kOrigin[3] = {0, 0, 0};  // [0x0063d870]
}  // namespace

void set_stealth_hooks(StealthHooks* h) { g_hooks = h ? h : &g_default_hooks; }
StealthHooks& stealth_hooks() { return *g_hooks; }

zrdr::Node* StealthHooks::reader_item(zrdr::Node* node, int32_t i) { return node ? &node->list[i] : nullptr; }
void StealthHooks::vehicle_setup(BasicVehicle& v, zrdr::Node* common, const char* name, int32_t kind) {
  mech3::vehicle_setup(v, common, name, kind);
}
void StealthHooks::vehicle_motion(BasicVehicle& v, zrdr::Node* mode, const char* name, const char* type) {
  if (v.controls) vehicle_motion_read(*v.controls, v.node, mode, name, type, &v.controls->point_count);
}
void StealthHooks::vehicle_points(BasicVehicle& v) { vehicle_points_place(v); }
void StealthHooks::vehicle_place(BasicVehicle& v, const float pos[3], float yaw) { mech3::vehicle_place(v, pos, yaw); }
StealthLayer* StealthHooks::layer_create(Stealth& s) {
  auto* l = new StealthLayer(&s);
  on_new(0x108, l);
  return l;
}
void StealthHooks::vehicle_construct(BasicVehicle& v, Handle ctl) { mech3::vehicle_construct(v, ctl, nullptr); }
void StealthHooks::vehicle_destroy(BasicVehicle& v) { mech3::vehicle_destroy(v); }

// Mech3.exe 1.2 @ 0x004a5280 (through IControls vt+0x0c, 0x004a5ae0).
int StealthControls::update(float dt) { return g_hooks->frame(*this, dt); }

// Mech3.exe 1.2 @ 0x004a55c0, with the image's 0x004a5850("ret_off").
StealthLayer::StealthLayer(Stealth* s) : owner(s) {
  StealthHooks& h = *g_hooks;
  h.image_set(image, "ret_off");
  image.center = 1;
  image.set_pos(0, 0);
  image.u40 = 1;
  image.dirty();
  image.show_for(-1.0f);
  add(&image);
  set_active(1);
}

// Mech3.exe 1.2 @ 0x004a56b0: the readout, only while active.
void StealthLayer::update(float dt) {
  (void)dt;
  if (active) g_hooks->readout(*this);
}

// Mech3.exe 1.2 @ 0x004a5690
void StealthLayer::set_active(int32_t on) {
  active = on;
  if (!on) g_hooks->layer_off();
}

// Mech3.exe 1.2 @ 0x004a4de0
Stealth* stealth_create(StealthControls* ctl) {
  StealthHooks& h = *g_hooks;
  VehBasicHooks& vb = veh_basic_hooks();
  auto* s = new Stealth;
  h.on_new(0x104, s);
  h.vehicle_construct(*s, ctl);  // 0x004ac530(ctl, 0)
  vehicle_markers_create();      // 0x004ac3d0
  const Handle node = h.object3d_create();
  vb.world_add(node);
  vb.node_set_name(node, "Stealth");
  if (node) {
    vb.entity_set_node(*s, node);  // 0x004d6c90(node, 0, 0, 0)
    vb.entity_register(*s);        // 0x005a7f50
  }
  vb.set_position(*s, kOrigin);       // 0x005a8550
  vb.node_set_intersect(node, 0);     // 0x004eb6d0: out of the segment queries
  vb.node_set_flag8(node, 0);         // 0x004eb620
  // The exe writes the motion's mode through null when there are no controls.
  if (s->controls) s->controls->motion.mode = VehicleMotion::kBasic, s->controls->state = 0;
  s->layer = h.layer_create(*s);
  s->x09 = false;  // zEntity's +0x09: the entity walk passes it by
  zrdr::Node* root = h.reader_load("vehicle.zrd");
  zrdr::Node* def = h.reader_find(root, "stealth");
  zrdr::Node* common = h.reader_find(def, "common_mode");
  zrdr::Node* mode = h.reader_item(def, 4);
  h.vehicle_setup(*s, common, "stealth", 0);
  h.vehicle_motion(*s, mode, "stealth", "stealth");
  h.vehicle_points(*s);
  h.reader_free(root);
  h.aiv_open();
  float pos[3] = {}, yaw = 0;
  if (h.aiv_named("stealth", pos, &yaw)) h.vehicle_place(*s, pos, yaw);
  h.aiv_close();
  return s;
}

// Mech3.exe 1.2 @ 0x004a4fc0, and the delete.
void stealth_delete(Stealth* s) {
  if (!s) return;
  StealthHooks& h = *g_hooks;
  VehBasicHooks& vb = veh_basic_hooks();
  vehicle_markers_delete();  // 0x004ac4e0
  if (s->node) {
    vb.entity_set_node(*s, nullptr);  // 0x004d6c90(0, 0, 0, 0)
    vb.entity_unregister(*s);         // 0x005a7f70
  }
  delete s->layer;  // 0x004a5640
  s->layer = nullptr;
  h.vehicle_destroy(*s);  // 0x004ac650
  delete s;
}

}  // namespace mech3
