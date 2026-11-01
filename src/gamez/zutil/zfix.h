#pragma once

// The rebuild's switches for fixes that change play against the original
// (docs/fixes.md). Not original code. The probes turn them off, so the
// differential tests check the original path.
namespace zfix {

// The frame the original's per-frame steps were tuned at (Owen, 2026-10-20):
// a step of x a frame becomes x * 30 a second.
constexpr float kReferenceFps = 30.0f;

struct Options {
  bool per_second = true;  // FIX (B): per-frame steps scaled by dt (fixes.md, Frame rate)
  bool bugs = true;        // FIX (bug): the play-changing bug fixes decided with Owen (CHANGES.md)
  // Not a fix: the 1.5 * 2^52 rounding trick gives what a 53-bit FPU gives, as
  // on Windows 11 in play (Owen, 2026-10-20: mechs cast shadows); false gives
  // the 24-bit result Direct3D 6's FPU setting would (CLAUDE.md, FPU precision).
  bool fpu53 = true;
  bool any_resolution = true;  // FIX (B): any screen size and 16:9 (resolution.md)
  bool nested_lock = true;     // FIX (A): a 3D lock inside a lock leaves the outer one (render-frame.md)
  // Ours: the menus on the mission's canvas when it is wider than 640x480
  // (16:9), on by default (Owen, 2026-10-31); a display choice, not a bug
  // fix: mw3.cfg's widemenus=0, --no-wide-menus (resolution.md section 12).
  bool wide_menus = true;
};

inline Options& options() {
  static Options o;
  return o;
}

// A per-frame step's share of this frame: 1 in the exe, dt * 30 with the fix
// (exactly 1 at dt = 1/30).
inline float frame_steps(float dt) { return options().per_second ? dt * kReferenceFps : 1.0f; }

}  // namespace zfix
