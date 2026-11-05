// Ours, not original code: a 16-bit RGB565 picture as 32-bit ARGB8888, for
// an SDL renderer texture. The launcher draws into a 565 canvas (the game's
// own ripple writes 565), and Metal in the tvOS Simulator refuses a B5G6R5
// texture, so the canvas goes up as ARGB8888 on every platform. Each channel
// expands as a GPU expands a unorm texel (v * 255 / max, rounded), so the
// pixels on screen are the ones a B5G6R5 texture gave
// (rgb565_as_argb8888_draws_the_same_as_b5g6r5).
#pragma once

#include <cstddef>
#include <cstdint>

namespace sdlfe {

inline uint32_t argb_of_565(uint16_t p) {
  const uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
  return 0xff000000u | (r * 255 + 15) / 31 << 16 | (g * 255 + 31) / 63 << 8 | (b * 255 + 15) / 31;
}

// Pitches in bytes, as SDL's.
inline void rgb565_to_argb8888(const uint16_t* src, int src_pitch, uint32_t* dst, int dst_pitch, int width, int height) {
  static const struct Table {
    uint32_t v[65536];
    Table() {
      for (uint32_t p = 0; p < 65536; ++p) v[p] = argb_of_565(static_cast<uint16_t>(p));
    }
  } table;
  for (int y = 0; y < height; ++y) {
    const auto* s = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(src) + static_cast<size_t>(y) * src_pitch);
    auto* d = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(dst) + static_cast<size_t>(y) * dst_pitch);
    for (int x = 0; x < width; ++x) d[x] = table.v[s[x]];
  }
}

}  // namespace sdlfe
