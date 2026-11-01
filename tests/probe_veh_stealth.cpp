// Probes for test_veh_stealth.py (docs/editor.md, The stealth object): the
// stealth object's constructor 0x004a4de0, its overlay 0x004a55c0 with the
// image 0x004a5850, and its controls' answers (0x004a51b0's class). The
// basic vehicle's calls out go through probe_veh_basic.cpp's hooks (bound to
// the original's block); the stealth's own through these. Every call goes to
// the shared callback; the controls and nodes are the original's addresses.
#include <cstdint>
#include <cstring>

#include "gamez/zgame/unported.h"
#include "mech3/veh_stealth.h"
#include "probe_app.h"

extern "C" void probe_vb_hooks(int on);
extern "C" void probe_vb_bind(void* v, uint32_t addr);

namespace {
namespace m3 = mech3;

uint32_t lo(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
m3::Handle hd(uint32_t v) { return reinterpret_cast<m3::Handle>(static_cast<uintptr_t>(v)); }
uint32_t peek(uint32_t a) { return probe_app::call("peek32", {a}); }

uint32_t g_addr = 0, g_ctl = 0;  // the original's stealth and controls
m3::StealthControls* g_c = nullptr;
m3::Stealth* g_s = nullptr;

struct Hooks : m3::StealthHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return probe_app::call(key, args);
  }
  uint32_t ref(const void* p) override {
    if (g_s && p == static_cast<const m3::BasicVehicle*>(g_s)) return g_addr;
    if (g_c && p == static_cast<const m3::VehicleControls*>(g_c)) return g_ctl;
    return lo(p);
  }
  void on_new(uint32_t size, const void* p) override {
    if (size != 0x104) return;
    g_s = static_cast<m3::Stealth*>(const_cast<void*>(p));
    probe_vb_bind(static_cast<m3::BasicVehicle*>(g_s), g_addr);
  }
  zrdr::Node* reader_item(zrdr::Node* node, int32_t i) override {  // the list's slot by the original's memory
    return node ? static_cast<zrdr::Node*>(hd(peek(lo(node) + 4) + 8 * static_cast<uint32_t>(i))) : nullptr;
  }
  bool aiv_named(const char* name, float pos[3], float* yaw) override {
    const uint32_t rec = unported("0x004e0680", {name});
    if (!rec) return false;
    for (int i = 0; i < 3; ++i) pos[i] = zgame::from_bits(peek(rec + 8 + 4 * static_cast<uint32_t>(i)));
    *yaw = zgame::from_bits(peek(rec + 0x14));
    return true;
  }
  // The basic vehicle's own: stubbed alike on both sides.
  void vehicle_construct(m3::BasicVehicle& v, m3::Handle ctl) override {
    unported("0x004ac530", {ref(&v), ref(ctl), 0});
    v.controls = static_cast<m3::VehicleControls*>(ctl);
  }
  void vehicle_setup(m3::BasicVehicle& v, zrdr::Node* common, const char* name, int32_t kind) override {
    unported("0x0049ec00", {ref(&v), lo(common), name, kind});
  }
  void vehicle_motion(m3::BasicVehicle& v, zrdr::Node* mode, const char* name, const char* type) override {
    unported("0x0049ec90", {ref(&v), lo(mode), name, type, g_ctl + 0x214});
  }
  void vehicle_points(m3::BasicVehicle& v) override { unported("0x0049f660", {ref(&v)}); }
  void vehicle_place(m3::BasicVehicle& v, const float pos[3], float yaw) override {
    unported("0x004a3630", {ref(&v), zgame::bits(pos[0]), zgame::bits(pos[1]), zgame::bits(pos[2]), zgame::bits(yaw)});
  }
  m3::StealthLayer* layer_create(m3::Stealth& s) override {
    return reinterpret_cast<m3::StealthLayer*>(static_cast<uintptr_t>(unported("0x004a55c0", {ref(&s)})));
  }
  void image_set(ztwod::Image& i, const char* name) override { unported("0x0059bb10", {name}), (void)i; }
};
Hooks g_hooks;
}  // namespace

extern "C" {

// 0x004a4de0 over controls whose motion mode and state are `mode` and
// `state`: 1 when made; the stealth's +0x09, the layer's handle and the
// controls' two fields after.
int probe_stealth_create(uint32_t addr, uint32_t ctl, int32_t mode, int32_t state, uint32_t* out) {
  probe_vb_hooks(1);
  m3::set_stealth_hooks(&g_hooks);
  m3::vehicle_markers() = {};
  g_addr = addr, g_ctl = ctl, g_s = nullptr;
  delete g_c;
  g_c = new m3::StealthControls;
  g_c->motion.mode = mode, g_c->state = state;
  m3::Stealth* s = m3::stealth_create(g_c);
  out[0] = s->x09 ? 1 : 0;
  out[1] = lo(s->layer);
  out[2] = static_cast<uint32_t>(g_c->motion.mode);
  out[3] = static_cast<uint32_t>(g_c->state);
  s->layer = nullptr;  // the stub's handle, not an object
  s->controls = nullptr;
  m3::set_stealth_hooks(nullptr);
  probe_vb_hooks(0);
  delete s;
  return 1;
}

// 0x004a55c0: the layer's and the image's fields as the original lays them
// out (layer +0x04 active; image +0x0c flags, +0x10 timer, +0x14 x, +0x18 y,
// +0x40 u40, +0x48 center), and whether the layer holds the image alone.
void probe_stealth_layer(uint32_t* out) {
  Hooks h;
  m3::set_stealth_hooks(&h);
  m3::Stealth s;
  {
    m3::StealthLayer l(&s);
    out[0] = static_cast<uint32_t>(l.active);
    out[1] = l.image.flags;
    out[2] = zgame::bits(l.image.timer);
    out[3] = static_cast<uint32_t>(l.image.x);
    out[4] = static_cast<uint32_t>(l.image.y);
    out[5] = l.image.u40;
    out[6] = static_cast<uint32_t>(l.image.center);
    out[7] = (l.head == &l.image && l.tail == &l.image && l.image.owner == &l && !l.image.next) ? 1 : 0;
    out[8] = l.owner == &s ? 1 : 0;
    l.remove(&l.image);
  }
  m3::set_stealth_hooks(nullptr);
}

}  // extern "C"

namespace {
float g_fx = 0, g_fy = 0;
struct FrameHooks : m3::StealthHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return probe_app::call(key, args);
  }
  uint32_t command(int32_t cmd) override { return unported("0x0051d300", {cmd}); }
  int32_t mouse(zin::MouseInfo* m) override {
    m->fx = g_fx, m->fy = g_fy;
    return static_cast<int32_t>(unported("0x0051ba60", {}));
  }
  float frame_dt() override { return zgame::from_bits(probe_app::call("peek32", {0x72c52c})); }
};
}  // namespace

extern "C" {
// 0x004a5280 on controls with these fields and the mouse at (fx, fy):
// out the throttle, turn, in40, pitch_in, out48, pitch_out, mouse x, y (as
// bits) and the answer.
void probe_stealth_frame(float throttle, float turn, uint32_t ramp, float in40, float pitch_in, float pitch, float fx, float fy,
                         uint32_t* out) {
  FrameHooks h;
  m3::set_stealth_hooks(&h);
  m3::StealthControls c;
  c.throttle = throttle, c.turn = turn, c.ramp = ramp, c.in40 = in40, c.pitch_in = pitch_in, c.angles.x = pitch;
  g_fx = fx, g_fy = fy;
  const int r = c.update(0.0f);
  const float f[8] = {c.throttle, c.turn, c.in40, c.pitch_in, c.out48, c.pitch_out, c.mouse_x, c.mouse_y};
  for (int i = 0; i < 8; ++i) out[i] = zgame::bits(f[i]);
  out[8] = static_cast<uint32_t>(r);
  m3::set_stealth_hooks(nullptr);
}
}  // extern "C"

extern "C" {
// The controls' answers: vt+0x08 (the goal), vt+0x1c (destroyed), vt+0x20.
void probe_stealth_controls(int32_t* out) {
  m3::StealthControls c;
  out[0] = c.path_goal();
  out[1] = c.destroyed() ? 1 : 0;
  out[2] = c.dynamics();
}

}  // extern "C"

namespace {
uint32_t g_step_ctl = 0, g_step_img = 0;
struct StepHooks : FrameHooks {
  void image_set(ztwod::Image&, const char*) override {}
  void layer_off() override {}
  void reticle_pick(ztwod::Image&) override { unported("0x004a58d0", {g_step_img}); }
};
struct StepControls : m3::StealthControls {
  int update(float dt) override { return static_cast<int>(probe_app::call("ictl.vt+0x0c", {g_step_ctl + 0xa08, zgame::bits(dt)})); }
};
}  // namespace

extern "C" {
// 0x004a5050 on a stealth at `addr` (its controls at `ctl`, the overlay's
// image at `img`) with the controls' mouse at (mx, my): the controls' frame,
// the move and the matrix through the shared callback; out the reticle's
// place.
void probe_stealth_step(uint32_t addr, uint32_t ctl, uint32_t img, float mx, float my, float dt, int32_t* out) {
  StepHooks h;
  m3::set_stealth_hooks(&h);
  probe_vb_hooks(1);
  g_step_ctl = ctl, g_step_img = img;
  auto* c = new StepControls;
  c->mouse_x = mx, c->mouse_y = my;
  auto* s = new m3::Stealth;
  s->controls = c;
  probe_vb_bind(static_cast<m3::BasicVehicle*>(s), addr);
  s->layer = new m3::StealthLayer(s);
  m3::stealth_frame(*s, dt);
  out[0] = s->layer->image.x, out[1] = s->layer->image.y;
  s->layer->remove(&s->layer->image);
  delete s->layer;
  s->layer = nullptr;
  s->controls = nullptr;
  delete c;
  probe_vb_bind(nullptr, 0);
  delete s;  // the C++ object only (the original's destructor 0x004a4fc0 is stealth_delete's)
  probe_vb_hooks(0);
  m3::set_stealth_hooks(nullptr);
}
}  // extern "C"
