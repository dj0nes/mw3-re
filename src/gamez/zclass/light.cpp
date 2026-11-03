#include "gamez/zclass/light.h"

#include <cstdio>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zgame/zgame_table.h"
#include "gamez/zrender/zrndr_fog.h"

namespace zcls {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zClass\\Light.c";
}  // namespace

Node* light_create() { return zgame::game().zcls_light_create(); }

// Mech3.exe 1.2 @ 0x004fa720. The node's bounds and the light's defaults
// (mech3ax LightMwC), active, on list 9.
Node* mw3_light_create() {
  Node* node = node_alloc();
  if (!node) return zerr::report(0x400, kFile, 0xb8, "Null node pointer."), nullptr;
  node->put(0x74, 1.0f), node->put(0x78, 1.0f), node->put(0x7c, -2.0f);
  node->put(0x80, 2.0f), node->put(0x84, 2.0f), node->put(0x88, -1.0f);
  node->set_flags(node->flags() | 0x100);
  node->put<int32_t>(0x34, 9);
  auto* l = new Light{};
  node->data = l;
  l->put(0x84, 0.0f), l->put(0x88, 1.0f), l->put(0x8c, 0.0f);
  l->put(0x60, 0.0f), l->put(0x64, 0.0f), l->put(0x68, 0.0f);
  l->put(0xa4, 1.0f), l->put(0xa8, 1.0f), l->put(0xac, 1.0f);
  l->put(0x9c, 0.0f), l->put(0xa0, 1.0f);
  l->put(0xb4, 32.0f), l->put(0xb8, 64.0f), l->put(0xbc, 1024.0f), l->put(0xc0, 4096.0f);
  l->put(0xc4, 0.03125f);
  l->put<uint32_t>(0xb0, (l->get<uint32_t>(0xb0) & 0xfffffe33u) | 0x233);
  node_set_active(node, 1);
  l->put<int32_t>(0xc8, 0);
  l->nodes = nullptr;
  push_front(kLights, node);
  return node;
}

// Mech3.exe 1.2 @ 0x004fa850
int light_delete(Node* node) {
  if (!node) return zerr::report(0x400, kFile, 0x11c, "Null node pointer."), 5;
  auto* l = static_cast<Light*>(node->data);
  if (!l) return zerr::report(0x400, kFile, 0x11d, "Null class data pointer"), 5;
  if (l->get<int32_t>(0xc8) > 0) {
    char msg[96];
    std::snprintf(msg, sizeof msg, "ERROR deleting light; Light attached to %d world nodes", l->get<int32_t>(0xc8));
    return zerr::report(0x400, kFile, 0x123, msg), 1;
  }
  delete[] l->nodes;
  l->nodes = nullptr;
  if (l->get<uint32_t>(0xb0) & 0x100) node_touch_frame();
  return node_release(node);
}

namespace {
// The shared shape of the three setters: null checks with their lines, the
// write, the changed bit, the frame stamp for static lights.
template <typename Write>
int set(Node* node, int line, Write write) {
  if (!node) return zerr::report(0x400, kFile, line, "Null node pointer."), 5;
  auto* l = static_cast<Light*>(node->data);
  if (!l) return zerr::report(0x400, kFile, line + 1, "Null class data pointer."), 5;
  const uint32_t flags = l->get<uint32_t>(0xb0);
  write(*l);
  l->put<uint32_t>(0xb0, flags | 1);
  if (flags & 0x100) node_touch_frame();
  return 0;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004faff0
int light_set_orientation(Node* node, float x, float y, float z) {
  return set(node, 0x350, [&](Light& l) { l.put(0x0, x), l.put(0x4, y), l.put(0x8, z); });
}

// Mech3.exe 1.2 @ 0x004fb060
int light_get_orientation(Node* node, float* x, float* y, float* z) {
  if (!node) return zerr::report(0x400, kFile, 0x3a1, "Null node pointer."), 5;
  auto* l = static_cast<Light*>(node->data);
  if (!l) return zerr::report(0x400, kFile, 0x3a2, "Null class data pointer."), 5;
  *x = l->get<float>(0x0), *y = l->get<float>(0x4), *z = l->get<float>(0x8);
  return 0;
}

// Mech3.exe 1.2 @ 0x004fa940
int light_set_ambient(Node* node, float v) {
  return set(node, 0x18b, [&](Light& l) { l.put(0xa0, v); });
}

// Mech3.exe 1.2 @ 0x004fa9b0
int light_set_diffuse(Node* node, float v) {
  return set(node, 0x1ab, [&](Light& l) { l.put(0x9c, v); });
}

namespace {
Light* data_or_report(Node* node, int line) {
  if (!node) return zerr::report(0x400, kFile, line, "Null node pointer."), nullptr;
  auto* l = static_cast<Light*>(node->data);
  if (!l) zerr::report(0x400, kFile, line + 1, "Null class data pointer.");
  return l;
}
uint32_t flags_of(const Light* l) { return l->get<uint32_t>(0xb0); }
void set_flags(Light* l, uint32_t f) { l->put<uint32_t>(0xb0, f); }
// The bit as 0 or -1, the way the exe compares them (shl, sar 31).
int32_t bit(uint32_t f, int n) { return (f >> n) & 1 ? -1 : 0; }

// Saturated (0x100) is recomputed after a mode change: directional (0x80),
// neither ambient (0x20) nor diffuse (0x40) only, and white light.
void resaturate(Light* l) {
  uint32_t f = flags_of(l);
  const int32_t was = bit(f, 8);
  const bool on = (f & 0x80) && !(f & 0x60) && l->get<uint32_t>(0xa4) == 0x3f800000u &&
                  l->get<uint32_t>(0xa8) == 0x3f800000u && l->get<uint32_t>(0xac) == 0x3f800000u;
  f = (f & ~0x100u) | (on ? 0x100u : 0);
  set_flags(l, f);
  if (bit(f, 8) != was) node_touch_frame();
}

// 0x004faba0 / 0x004fac70 / 0x004fad30: one mode bit from EDX & 1.
int set_mode_bit(Node* node, int on, int n, int line) {
  Light* l = data_or_report(node, line);
  if (!l) return 5;
  set_flags(l, (flags_of(l) & ~(1u << n)) | (static_cast<uint32_t>(on & 1) << n));
  set_flags(l, flags_of(l) | 1);
  resaturate(l);
  return 0;
}
}  // namespace

// Mech3.exe 1.2 @ 0x004faa20. Bit 2 from EDX & 1. The touch test compares the
// old bit as 0 / -1 with the argument itself, so a saturated light touches
// the frame for any argument but 0 with the bit clear (a quirk, harmless).
int light_set_bit2(Node* node, int32_t on) {
  Light* l = data_or_report(node, 0x1cd);
  if (!l) return 5;
  const uint32_t old = flags_of(l);
  const uint32_t f = (old & ~4u) | (static_cast<uint32_t>(on & 1) << 2);
  set_flags(l, f);
  if ((f & 0x100) && bit(old, 2) != on) node_touch_frame();
  set_flags(l, flags_of(l) | 1);
  return 0;
}

// Mech3.exe 1.2 @ 0x004faaa0. Bit 3 on, bit 4 off.
int light_set_bit3(Node* node) {
  Light* l = data_or_report(node, 0x1ef);
  if (!l) return 5;
  const uint32_t old = flags_of(l);
  const uint32_t f = (old & ~0x10u) | 8;
  set_flags(l, f);
  if ((f & 0x100) && bit(old, 3) != bit(f, 3)) node_touch_frame();
  set_flags(l, flags_of(l) | 1);
  return 0;
}

// Mech3.exe 1.2 @ 0x004fab20. Bit 4 on, bit 3 off.
int light_set_bit4(Node* node) {
  Light* l = data_or_report(node, 0x212);
  if (!l) return 5;
  const uint32_t old = flags_of(l);
  const uint32_t f = (old & ~8u) | 0x10;
  set_flags(l, f);
  if ((f & 0x100) && bit(old, 4) != bit(f, 4)) node_touch_frame();
  set_flags(l, flags_of(l) | 1);
  return 0;
}

// Mech3.exe 1.2 @ 0x004faba0
int light_set_ambient_only(Node* node, int on) { return set_mode_bit(node, on, 5, 0x232); }
// Mech3.exe 1.2 @ 0x004fac70
int light_set_diffuse_only(Node* node, int on) { return set_mode_bit(node, on, 6, 0x24b); }
// Mech3.exe 1.2 @ 0x004fad30
int light_set_directional(Node* node, int on) { return set_mode_bit(node, on, 7, 0x266); }

// Mech3.exe 1.2 @ 0x004fadf0. Sorted into (near, far); equal ranges are an
// error (only printed) and the far one becomes near - 10.
int light_set_range(Node* node, float a, float b) {
  Light* l = data_or_report(node, 0x283);
  if (!l) return 5;
  l->put(0xb4, a < b ? a : b);
  l->put(0xb8, a > b ? a : b);  // the exe takes b unless a > b
  if (!(b < a || b > a)) l->put(0xb8, l->get<float>(0xb4) - -10.0f);
  const float n = l->get<float>(0xb4), f = l->get<float>(0xb8);
  const float span = f - n;
  set_flags(l, flags_of(l) | 1);
  l->put(0xc4, 1.0f / span);
  l->put(0xbc, n * n);
  l->put(0xc0, f * f);
  if (flags_of(l) & 0x100) node_touch_frame();
  return 0;
}

// Mech3.exe 1.2 @ 0x004faf20
int light_get_range(Node* node, float* near_range, float* far_range) {
  Light* l = data_or_report(node, 0x2b6);
  if (!l) return 5;
  *near_range = l->get<float>(0xb4), *far_range = l->get<float>(0xb8);
  return 0;
}

// Mech3.exe 1.2 @ 0x004faf80
int light_set_position(Node* node, float x, float y, float z) {
  Light* l = data_or_report(node, 0x2da);
  if (!l) return 5;
  l->put(0x0c, x), l->put(0x10, y), l->put(0x14, z);
  set_flags(l, flags_of(l) | 1);
  if (flags_of(l) & 0x100) node_touch_frame();
  return 0;
}

// Mech3.exe 1.2 @ 0x004fb4b0
int light_get_color(Node* node, float* r, float* g, float* b) {
  Light* l = data_or_report(node, 0x462);
  if (!l) return 5;
  *r = l->get<float>(0xa4), *g = l->get<float>(0xa8), *b = l->get<float>(0xac);
  return 0;
}

// Mech3.exe 1.2 @ 0x004fb510. Saturation is judged on the arguments, before
// 0x00568370 clamps the stored colour and hands it to the renderer.
int light_set_color(Node* node, float r, float g, float b) {
  Light* l = data_or_report(node, 0x487);
  if (!l) return 5;
  l->put(0xa8, g);
  uint32_t f = flags_of(l) | 1;
  set_flags(l, f);
  const int32_t was = bit(f, 8);
  l->put(0xa4, r);
  l->put(0xac, b);
  const auto one = [](float v) { return !(v < 1.0f || v > 1.0f); };
  const bool on = (f & 0x80) && !(f & 0x60) && one(r) && one(g) && one(b);
  f = (f & ~0x100u) | (on ? 0x100u : 0);
  set_flags(l, f);
  if (bit(f, 8) != was) node_touch_frame();
  zrndr::set_light_color(l->floats(0xa4));
  return 0;
}

}  // namespace zcls

