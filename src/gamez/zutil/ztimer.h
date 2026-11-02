#pragma once
#include <cstdint>

namespace platform { class Clock; }

// The engine's frame clock. Original file unknown (it sits between zsnd_create.cpp
// and zutl_zar.cpp); globals keep their original addresses in the comments.
namespace ztimer {

struct State {
  float max_dt = 0.125f;     // 0x005f0960  longest frame step, seconds
  int clamp_enabled = 1;     // 0x005f0964
  float prev_s = 0.0f;       // 0x005f0968
  float now_s = 0.0f;        // 0x005f096c
  float scale = 1.0f;        // 0x005f0970  one-frame time scale, reset after use
  float dt = 0.0f;           // 0x0072c52c  scaled, clamped step the game runs on
  float total_scaled = 0.0f; // 0x0072c530
  float raw_dt = 0.0f;       // 0x0072c534
  float total_raw = 0.0f;    // 0x0072c538
};

State& state();
void set_clock(platform::Clock* clock);
uint32_t ticks_ms();  // timeGetTime as the clock gives it (ours: 0 without one)

void init();                       // 0x005759a0, the clock's start (zGameApp start, 0x00480570)
void update();                     // 0x00575a00, once per frame from zGameApp::Run
float set_max_rate(float fps);     // 0x00575ab0
void set_clamp_enabled(int on);    // 0x00575af0

}  // namespace ztimer
