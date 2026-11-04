#include "pm/gamez/zmodel/gmod_cull.h"

#include <cstring>

#include "gamez/zutil/zfix.h"

#pragma STDC FP_CONTRACT OFF

namespace pm::zmod {
uint32_t rounded_sum(float a, float b);
namespace {
CullState g_cull;

// The 1.5 * 2^52 rounding trick (0x005f0a78): the sum's low word. On a 53-bit
// FPU (zfix fpu53, as in play) that is the value rounded. At 24 bits the sum
// keeps multiples of 2^29 only: 0 unless the value is huge (CLAUDE.md, FPU
// precision). Everything before it runs at the FPU's precision too.
uint32_t low_word(double sum) {
  uint64_t bits;
  std::memcpy(&bits, &sum, 8);
  return static_cast<uint32_t>(bits);
}

// A channel's value + 0.5, rounded, in 0..255.
uint32_t channel(float v) {
  const int32_t c = static_cast<int32_t>(rounded_sum(v, 0.5f));
  return c >= 0x100 ? 0xff : c < 0 ? 0 : static_cast<uint32_t>(c);
}
}  // namespace

CullState& cull_state() { return g_cull; }

uint32_t rounded_sum(float a, float b) {
  if (zfix::options().fpu53) return low_word(static_cast<double>(a) + b + 6755399441055744.0);
  return low_word((a + b) - -6755399441055744.0f);
}

uint32_t rounded_product(float a, float b) {
  if (zfix::options().fpu53) return low_word(static_cast<double>(a) * b + 6755399441055744.0);
  return low_word((a * b) - -6755399441055744.0f);
}

// Pirate's Moon 1.0 @ 0x005403d0
void set_fade(float fade) { g_cull.fade = fade; }

// Pirate's Moon 1.0 @ 0x0054faa0
int plane_codes(const float* pos, int n, uint32_t* codes) {
  const CullState& c = g_cull;
  const float near_z = clip3d_state().near_z;
  for (int i = 0; i < n; ++i) codes[i] = 0;
  auto each = [&](uint32_t code, auto&& out) {
    int in = 0;
    for (int i = 0; i < n; ++i) {
      if (out(pos + i * 3)) codes[i] |= code;
      else ++in;
    }
    return in;
  };
  int in = 1;  // the exe's register still holds the positions' address
  if (c.planes & 0x10) {
    in = each(0x10, [&](const float* p) { return !(p[2] >= near_z); });  // a NaN is behind
    if (in == 0) return 0;
  }
  if (c.planes & 8) each(8, [&](const float* p) { return p[2] <= c.slope_y * p[1]; });
  if ((c.planes & 1) && !each(1, [&](const float* p) { return p[2] <= -(c.slope_x * p[0]); })) return 0;
  if ((c.planes & 2) && !each(2, [&](const float* p) { return p[2] <= c.slope_x * p[0]; })) return 0;
  if ((c.planes & 4) && !each(4, [&](const float* p) { return p[2] <= -(c.slope_y * p[1]); })) return 0;
  if (!(c.planes & 0x20)) return 1;
  return each(0x20, [&](const float* p) { return c.far_z <= p[2]; }) != 0;
}

// Pirate's Moon 1.0 @ 0x0054fc60
int polygon_codes(int n, const int32_t* vertices, uint32_t* out) {
  int count[6] = {};
  static const uint32_t kPlanes[6] = {0x10, 0x20, 1, 2, 4, 8};
  *out = 0;
  for (int i = 0; i < n; ++i) {
    const uint32_t code = g_cull.codes[vertices[i]];
    if (!code) continue;
    for (int k = 0; k < 6; ++k) count[k] += (code & kPlanes[k]) != 0;
    *out |= code;
  }
  for (int k = 0; k < 6; ++k)
    if (count[k] == n) return 0;
  return 1;
}

// Pirate's Moon 1.0 @ 0x005439e0
void tl_colours(ClipPoly& p) {
  uint32_t alpha = 0xff000000u;
  if (!(g_cull.fade >= 1.0f)) {  // or a NaN
    const uint32_t a = rounded_product(g_cull.fade, 255.0f);
    alpha = (a > 0xff ? 0xffu : a) << 24;
  }
  for (int v = 0; v < p.count; ++v) {
    const float* r = p.rec + v * 8;
    const uint32_t colour = (channel(r[0]) << 16) | (channel(r[1]) << 8) | channel(r[2]) | alpha;
    std::memcpy(p.tl + v * 8 + 4, &colour, 4);
  }
}

}  // namespace pm::zmod
