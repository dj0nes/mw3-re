#include "gamez/zutil/ztimer.h"

#include "platform/platform.h"

namespace ztimer {

namespace {
State g_state;
platform::Clock* g_clock = nullptr;
}  // namespace

State& state() { return g_state; }
void set_clock(platform::Clock* clock) { g_clock = clock; }
uint32_t ticks_ms() { return g_clock ? g_clock->ticks_ms() : 0; }

// Mech3.exe 1.2 @ 0x005759a0: the times zeroed and the last tick read now, so
// the first update's step is the time since the start, not since 0.
void init() {
  State& s = g_state;
  s.now_s = 0;
  s.total_raw = 0;
  s.raw_dt = 0;
  s.total_scaled = 0;
  s.dt = 0;
  const uint32_t ticks = g_clock ? g_clock->ticks_ms() : 0;
  s.prev_s = static_cast<float>(static_cast<double>(ticks) * static_cast<double>(0.001f));  // 0x005ce748
}

// Mech3.exe 1.2 @ 0x00575a00
// FIX (B): ticks since game start, not since boot (docs/fixes.md). The product
// stays unrounded on the x87 stack, hence the double.
void update() {
  State& s = g_state;
  const uint32_t ticks = g_clock ? g_clock->ticks_ms() : 0;
  const float scale = s.scale;
  const double now = static_cast<double>(ticks) * static_cast<double>(0.001f);  // 0x005ce748
  s.now_s = static_cast<float>(now);
  s.scale = 1.0f;
  s.raw_dt = static_cast<float>(now - static_cast<double>(s.prev_s));
  s.dt = scale * s.raw_dt;
  s.total_raw = s.total_raw + s.raw_dt;
  if (s.clamp_enabled != 0 && s.max_dt < s.dt) s.dt = s.max_dt;
  s.prev_s = s.now_s;
  s.total_scaled = s.total_scaled + s.dt;
}

// Mech3.exe 1.2 @ 0x00575ab0: sets the lowest frame rate the clamp allows and
// returns it; a non-positive argument only reads it back.
float set_max_rate(float fps) {
  if (0.0f < fps) {
    g_state.max_dt = 1.0f / fps;
    return fps;
  }
  return 1.0f / g_state.max_dt;
}

// Mech3.exe 1.2 @ 0x00575af0
void set_clamp_enabled(int on) { g_state.clamp_enabled = on; }

}  // namespace ztimer
