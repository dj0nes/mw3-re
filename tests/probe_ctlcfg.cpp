// mech3's controls setup (mech3/controls_cfg.cpp) for
// tests/differential/test_pm_ctlcfg.py. zInput's command map and the messages
// are the test's (a callback that logs each call and answers it); the
// controls file is read from and written to the write dir the test gives.
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "gamez/zutil/zutl_path.h"
#include "mech3/controls_cfg.h"
#include "mech3/mech_control.h"

using namespace mech3;

namespace {
// key, then up to six args (strings as their own pointers), returns the answer.
using Callback = uint32_t (*)(const char* key, const uint32_t* vals, const char* const* strs, int32_t n);
Callback g_cb = nullptr;
char g_msg[16][32];
int g_msg_next = 0;

uint32_t call(const char* key, std::initializer_list<uint32_t> v, const char* s1 = nullptr) {
  uint32_t vals[8] = {};
  const char* strs[8] = {};
  int32_t n = 0;
  for (uint32_t x : v) vals[n++] = x;
  if (s1) strs[1] = s1;
  return g_cb ? g_cb(key, vals, strs, n) : 0;
}

void wire() {
  ControlsSetupCalls& c = controls_setup_calls();
  c.message = [](int32_t id) -> const char* {
    call("0x00576040", {static_cast<uint32_t>(id)});
    char* m = g_msg[g_msg_next++ & 15];
    std::snprintf(m, 32, "MSG_%x", id);
    return m;
  };
  c.bind = [](int32_t cmd, const char* name, uint32_t k1, uint32_t k2, uint32_t joy, uint32_t mouse) {
    call("0x0051d2c0", {static_cast<uint32_t>(cmd), 0, k1, k2, joy, mouse}, name ? name : "");
    return cmd;
  };
  c.map_reset = [](bool callbacks) { call("0x0051d1d0", {callbacks ? 1u : 0u}); };
  c.map_refresh = []() { call("0x0051d1c0", {}); };
  c.binding = [](int which, int32_t cmd) {
    static const char* const keys[4] = {"0x0051d200", "0x0051d210", "0x0051d220", "0x0051d230"};
    return call(keys[which], {static_cast<uint32_t>(cmd)});
  };
  controls_calls().joy_use = []() { call("0x0051b2c0", {}); };
}

void put(std::string& out, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void put(std::string& out, const char* fmt, ...) {
  char buf[512];
  va_list a;
  va_start(a, fmt);
  std::vsnprintf(buf, sizeof buf, fmt, a);
  va_end(a);
  out += buf;
}
uint32_t bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}
float flt(uint32_t u) {
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}
}  // namespace

extern "C" {

void probe_ctl_init(Callback cb, const char* write_dir) {
  g_cb = cb;
  wire();
  controls_setup() = ControlsSetup{};
  axis_table() = AxisTable{};
  zutl::mounts().read.clear();
  zutl::set_write_dir(write_dir);
}

int32_t probe_ctl_defaults() { return controls_set_defaults(); }
void probe_ctl_load(const char* path) { controls_load(path); }
void probe_ctl_save(const char* path) { controls_save(path); }
void probe_ctl_free() { controls_free_groups(); }

// The axis table as 16 words and the two flags.
void probe_ctl_set_table(const uint32_t* w, uint8_t ready, uint8_t stock) {
  AxisTable& t = axis_table();
  for (int i = 0; i < 4; ++i) {
    std::memcpy(&t.axes[i].source, &w[4 * i], 4);
    t.axes[i].scale = flt(w[4 * i + 1]);
    t.axes[i].offset = flt(w[4 * i + 2]);
    t.axes[i].deadzone = flt(w[4 * i + 3]);
  }
  t.ready = ready;
  t.stock = stock;
}

// MechControls::set_mode on controls with these flags and +0x3e; returns
// the flags and writes the byte back.
uint32_t probe_ctl_set_mode(uint32_t flags, uint8_t* held, uint32_t mode) {
  MechControls c;
  c.flags = flags;
  c.throttle_held = *held != 0;
  c.set_mode(mode);
  *held = c.throttle_held;
  return c.flags;
}

// Everything the setup keeps, as test_pm_ctlcfg.py's dump() writes the
// original's memory.
int probe_ctl_dump(char* buf, int size) {
  const ControlsSetup& s = controls_setup();
  std::string out = "C";
  for (int i = 0; i < 0xb0; ++i) {
    const CommandEntry& e = s.commands[i];
    if (e.message || e.name || e.axis_key || e.axis)
      put(out, "%d:%x:%s:%d:%d;", i, e.message, e.name ? e.name : "", e.axis_key, e.axis);
  }
  put(out, " G%d:", s.group);
  for (const ControlGroup& g : s.groups) {
    out += g.title + ":";
    for (int32_t c : g.commands) put(out, "%x,", c);
    out += ";";
  }
  out += " N";
  for (const auto& n : s.names) put(out, "%s=%x;", n.first.c_str(), n.second);
  out += " A";
  const AxisTable& t = axis_table();
  for (const AxisBinding& a : t.axes)
    put(out, "%x,%08x,%08x,%08x;", static_cast<uint32_t>(a.source), bits(a.scale), bits(a.offset), bits(a.deadzone));
  put(out, "%d,%d", t.ready, t.stock);
  if (static_cast<int>(out.size()) >= size) return -1;
  std::memcpy(buf, out.c_str(), out.size() + 1);
  return static_cast<int>(out.size());
}

}  // extern "C"
