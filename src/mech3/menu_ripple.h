// The main menu's ripple step (Mech3.exe 1.2 @ 0x00485650's body), one copy
// for the menus' SlideButton and the launcher (ours: launcher.md) so both
// spread and fade alike (menus.md, The ripple).
#pragma once

#include <cmath>
#include <cstdint>

#include "gamez/zutil/zfix.h"

namespace mech3 {

struct MenuRipple {
  static constexpr int32_t kStartRadius = 20;      // 0x004855e0's restart
  static constexpr int32_t kMaxRadius = 200;       // MAXRADIUS when the dialog has none
  static constexpr float kStartAmplitude = 7.0f;
  static constexpr float kSpeed = 120.0f;          // pixels a second
  static constexpr float kWavelength = 12.0f;
};

// One update of a running ripple: false once it has faded (the amplitude 2
// or less), with nothing changed. `carry` holds the growth's fraction.
inline bool menu_ripple_step(int32_t& radius, float& carry, float& amplitude, float& phase, float speed, float wavelength,
                             int32_t max_radius, float dt) {
  if (!(amplitude > 2.0f)) return false;
  const float d = speed * dt;
  // FIX (B), with zfix's per_second: the growth's fraction carried to the
  // next frame. The exe truncates each frame's, so at 120 fps and above the
  // ring never grows (fixes.md).
  float grow = d;
  if (zfix::options().per_second) {
    grow += carry;
    carry = grow - std::floor(grow);
  }
  radius += static_cast<int32_t>(grow);
  if (radius > max_radius) radius = max_radius;
  else if (radius < 0) radius = 0;
  // FIX (B): 0.3 for each 30 fps frame with zfix's per_second, not each frame.
  amplitude = amplitude - 0.3f * zfix::frame_steps(dt);
  const float q = d / wavelength;
  phase = phase - static_cast<float>(static_cast<double>(q) * -3.141592653589793);
  return true;
}

}  // namespace mech3
