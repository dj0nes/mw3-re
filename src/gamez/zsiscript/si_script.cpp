#include "gamez/zsiscript/si_script.h"

#include "crt/strcase.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "gamez/zerror/zerr.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmath/zmth_vector.h"
#include "gamez/zutil/zutl_zar.h"

#pragma STDC FP_CONTRACT OFF

namespace zsis {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zSIScript\\si_script.cpp";
constexpr uint32_t kVersion = 4;  // 0x005f4bac

// Mech3.exe 1.2 @ 0x005aa530
void reset_matrix(Part& p, bool offset_too) {
  const float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  for (int i = 0; i < 9; ++i) p.matrix[i] = id[i];
  if (offset_too) p.offset[0] = p.offset[1] = p.offset[2] = 0;
}

uint32_t read4(std::FILE* f, void* out) { return static_cast<uint32_t>(std::fread(out, 1, 4, f)); }

// (b - a) * f + a, the keys' lerp in every sampler.
void lerp3(const float* a, const float* b, float f, float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = (b[i] - a[i]) * f + a[i];
}
}  // namespace

// Mech3.exe 1.2 @ 0x005aa500
Part::Part()
    : translations(nullptr), velocities(nullptr), rotations(nullptr), has_matrix(false), matrix{},
      offset{}, owns_buffers(true), name(nullptr), script(nullptr) {
  reset_matrix(*this, true);
}

// Mech3.exe 1.2 @ 0x005aa5a0. With `share`, uses another part's buffers.
bool Part::attach(Script* s, const Part* share) {
  if (!s) return false;
  script = s;
  reset_matrix(*this, false);
  if (!share) {
    owns_buffers = true;
    const uint32_t n = s->frame_count + 1;
    translations = static_cast<float*>(std::malloc(n * 12));
    rotations = static_cast<float*>(std::malloc(n * 16));
    return true;
  }
  owns_buffers = false;
  name = share->name;
  translations = share->translations;
  rotations = share->rotations;
  velocities = share->velocities;
  return true;
}

// Mech3.exe 1.2 @ 0x005aa6d0. Flags: 8 translations, 2 velocities (never
// in motion.zbd), 4 rotations.
bool Part::read(std::FILE* f, long end) {
  zutl_zar::read_string(f, &name);
  const uint32_t n = script->frame_count + 1;
  uint32_t flags = 0;
  read4(f, &flags);
  if (end >= 0) {  // ours: the keys within the entry
    const uint64_t need = ((flags & 8) ? 12ull : 0) + ((flags & 2) ? 12ull : 0) + ((flags & 4) ? 16ull : 0);
    if (std::ftell(f) > end || need * n > static_cast<uint64_t>(end - std::ftell(f))) return false;
  }
  if (flags & 8) std::fread(translations, 1, n * 12, f);
  if (flags & 2) {
    if (!velocities) velocities = static_cast<float*>(std::malloc(n * 12));
    std::fread(velocities, 1, n * 12, f);
  }
  if (flags & 4) std::fread(rotations, 1, n * 16, f);
  return true;
}

// Mech3.exe 1.2 @ 0x005aa3b0
void Script::set_loop_time(float t) {
  if (!active) return;
  if (t <= 0.0f) {
    zerr::reportf(0x400, kFile, 0x1d3, "CZSIScript::SetLoopTime( t=%.2f )", static_cast<double>(t));
    return;
  }
  time = 0;
  loop_time = t;
  inv_loop_time = 1.0f / t;
  frame_time = t / static_cast<float>(frame_count);
  inv_frame_time = 1.0f / frame_time;
}

// Mech3.exe 1.2 @ 0x005a9c70. Read through the archive's file, not checked.
bool Script::read(std::FILE* f, long end) {
  uint32_t version = 0;
  read4(f, &version);
  if (version != kVersion) return false;
  read4(f, &loop_time);
  read4(f, &frame_count);
  read4(f, &part_count);
  if (end >= 0) {  // ours: every part at least a name's length and its flags; a frame's keys
    const uint64_t left = std::ftell(f) > end ? 0 : static_cast<uint64_t>(end - std::ftell(f));
    if (frame_count == 0 || static_cast<uint64_t>(part_count) * 8 > left ||
        static_cast<uint64_t>(frame_count) + 1 > left / 12 || !(loop_time > 0.0f)) {
      part_count = 0;
      return false;
    }
  }
  set_loop_time(loop_time);
  read4(f, &stride);
  read4(f, &mult);
  parts = static_cast<Part**>(std::calloc(part_count, sizeof(Part*)));
  for (uint32_t i = 0; i < part_count; ++i) {
    parts[i] = new Part;
    parts[i]->attach(this, nullptr);
    if (!parts[i]->read(f, end)) {  // ours: a broken copy's, all freed
      for (uint32_t j = 0; j <= i; ++j) {
        parts[j]->destroy();
        delete parts[j];
      }
      std::free(parts);
      parts = nullptr;
      part_count = 0;
      return false;
    }
  }
  return true;
}

// Mech3.exe 1.2 @ 0x005a99f0 (after the base constructor 0x005a9920): a new
// part per part of `from`, on the same buffers.
void Script::share(const Script& from) {
  active = true;
  part_count = from.part_count;
  parts = static_cast<Part**>(std::calloc(part_count, sizeof(Part*)));
  for (uint32_t i = 0; i < part_count; ++i) {
    if (!from.parts[i]) continue;
    parts[i] = new Part;
    parts[i]->attach(this, from.parts[i]);
  }
  frame_count = from.frame_count;
  set_loop_time(from.loop_time);
  rate = from.rate;
  stride = from.stride;
  mult = from.mult;
}

// Mech3.exe 1.2 @ 0x005aa450
void Script::set_fps(float fps) { set_loop_time(static_cast<float>(frame_count) / fps); }

// Mech3.exe 1.2 @ 0x005abae0 and 0x00432a80. The phase is cached by t alone:
// a new rate takes effect at the next new t. set_loop_time resets the cached
// t to 0 but not the keys, so after a resample a first sample at t = 0 uses
// the last resampled keys (docs/motion.md, Playback). FIX (bug): keys past
// the new last one, where the exe reads beyond the arrays, are recomputed.
void Script::set_phase(float t, bool force) {
  const bool stale = key0 < 0 || key1 > static_cast<int32_t>(frame_count);
  if (t == time && !force && !stale) return;
  const float p = rate * t;
  time = t;
  phase = p;
  if (p == 0.0f) {
    phase = 0;
    frac = 0;
    key0 = 0;
    key1 = 1;
    return;
  }
  if (!wraps && p == loop_time) {
    phase = 1.0f;
    frac = 0;
    key1 = static_cast<int32_t>(frame_count);
    key0 = static_cast<int32_t>(frame_count) - 1;
    return;
  }
  phase = p / loop_time;
  const float ph = phase - static_cast<float>(std::floor(double{phase}));
  const uint32_t n = wraps ? frame_count : frame_count - 1;  // fild of a zero-extended qword
  const float v = static_cast<float>(int64_t{n}) * ph;
  phase = ph;
  key0 = static_cast<int32_t>(std::floor(double{v}));
  key1 = static_cast<int32_t>(std::ceil(double{v}));
  frac = v - static_cast<float>(key0);
}

// Mech3.exe 1.2 @ 0x004212c0 (mech_anim.cpp's copy, always recomputed); the
// keys and fraction handed out.
void Script::phase_at(float t, int32_t* k0, int32_t* k1, float* f) {
  set_phase(t, true);
  if (k0) *k0 = key0;
  if (k1) *k1 = key1;
  if (f) *f = frac;
}

// Mech3.exe 1.2 @ 0x005aa270. Marks the part used. Not exact: a prefix match.
Part* Script::find_part(const char* want, bool exact) {
  for (uint32_t i = 0; i < part_count; ++i) {
    Part* p = parts[i];
    if (!p) continue;
    const int c = exact ? strcasecmp(p->name, want) : strncasecmp(p->name, want, std::strlen(want));
    if (c == 0) {
      p->used = true;
      return p;
    }
  }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x005aa310
void Script::free_unused() {
  for (uint32_t i = 0; i < part_count; ++i) {
    Part* p = parts[i];
    if (p && !p->used) {
      p->destroy();
      delete p;
      parts[i] = nullptr;
    }
  }
}

// Mech3.exe 1.2 @ 0x005aa360. Stops at the first part that fails.
bool Script::set_frames(uint32_t n) {
  bool ok = true;
  if (frame_count == n) return true;
  for (uint32_t i = 0; i < part_count && ok; ++i)
    if (parts[i]) ok = parts[i]->resample(n);
  frame_count = n;
  set_loop_time(loop_time);
  return ok;
}

// Mech3.exe 1.2 @ 0x005aa480: key `first` becomes key 0 in every part.
void Script::rotate_keys(uint32_t first) {
  if (first == 0) return;
  bool ok = true;
  for (uint32_t i = 0; i < part_count && ok; ++i)
    if (parts[i]) ok = parts[i]->rotate_keys(first);
}

// Mech3.exe 1.2 @ 0x005aa4c0
bool make_velocities(Part* p) { return p ? p->make_velocities() : false; }

// Mech3.exe 1.2 @ 0x005aa4e0
bool free_velocities(Part* p) { return p ? p->free_velocities() : false; }

// Mech3.exe 1.2 @ 0x005abab0
bool swap_parts(Part** a, Part** b) {
  char* na = (*a)->name;
  char* nb = (*b)->name;
  Part* t = *a;
  *a = *b;
  *b = t;
  (*a)->name = na;
  (*b)->name = nb;
  return true;
}

// Mech3.exe 1.2 @ 0x005aac10
void Part::destroy() {
  if (!owns_buffers) return;
  if (translations) std::free(translations);
  if (rotations) std::free(rotations);
  if (velocities) std::free(velocities);
  if (name) std::free(name);
}

// Mech3.exe 1.2 @ 0x005aae10. compose: m, then the current matrix.
void Part::set_matrix(const float m[12], bool compose) {
  if (!m) {
    has_matrix = false;
    return;
  }
  has_matrix = true;
  if (!compose) {
    std::memcpy(matrix, m, 36);
    std::memcpy(offset, m + 9, 12);
    return;
  }
  float o[12], r[12];
  std::memcpy(o, matrix, 36);
  std::memcpy(o + 9, offset, 12);
  for (int row = 0; row < 3; ++row) {
    const float* a = m + 3 * row;
    r[3 * row] = (a[0] * o[0] + a[1] * o[3]) + a[2] * o[6];
    r[3 * row + 1] = (a[0] * o[1] + a[1] * o[4]) + a[2] * o[7];
    r[3 * row + 2] = (a[1] * o[5] + a[2] * o[8]) + a[0] * o[2];
  }
  r[9] = ((m[9] * o[0] + m[10] * o[3]) + m[11] * o[6]) + o[9];
  r[10] = ((m[9] * o[1] + m[10] * o[4]) + m[11] * o[7]) + o[10];
  r[11] = ((m[10] * o[5] + m[11] * o[8]) + m[9] * o[2]) + o[11];
  std::memcpy(matrix, r, 36);
  std::memcpy(offset, r + 9, 12);
}

// Mech3.exe 1.2 @ 0x00421440 (the samplers inline the same sums).
void Part::transform(float p[3]) const {
  if (!has_matrix) return;
  const float x = p[0], y = p[1], z = p[2];
  const float* m = matrix;
  p[0] = ((x * m[0] + y * m[3]) + z * m[6]) + offset[0];
  p[1] = ((x * m[1] + y * m[4]) + z * m[7]) + offset[1];
  p[2] = ((x * m[2] + y * m[5]) + z * m[8]) + offset[2];
}

// Mech3.exe 1.2 @ 0x004214c0
const float* Part::key(uint32_t i, float* out) const {
  const float* k = translations + 3 * i;
  if (!out) return k;
  out[0] = k[0], out[1] = k[1], out[2] = k[2];
  return out;
}

// Mech3.exe 1.2 @ 0x005aafe0
void Part::translation(float t, float out[3]) {
  script->set_phase(t, false);
  const Script& s = *script;
  lerp3(translations + 3 * s.key0, translations + 3 * s.key1, s.frac, out);
  transform(out);
}

// Mech3.exe 1.2 @ 0x005ab1e0
void Part::rotation(float t, float out[4]) {
  script->set_phase(t, false);
  const Script& s = *script;
  zmth::quat_slerp(rotations + 4 * s.key0, rotations + 4 * s.key1, s.frac, out);
}

// Mech3.exe 1.2 @ 0x005ab390. Zero without velocity keys; the matrix's
// offset is added too.
void Part::velocity(float t, float out[3]) {
  if (!velocities) {
    out[0] = out[1] = out[2] = 0.0f;  // 0x0063d870
    return;
  }
  script->set_phase(t, false);
  const Script& s = *script;
  lerp3(velocities + 3 * s.key0, velocities + 3 * s.key1, s.frac, out);
  transform(out);
  const float r = script->rate;
  out[0] = r * out[0], out[1] = r * out[1], out[2] = r * out[2];
}

// Mech3.exe 1.2 @ 0x005ab4f0: this part and b at this part's script's keys,
// then from one to the other by w. No matrix.
void Part::blend_translation(float t, const Part& b, float w, float out[3]) {
  script->set_phase(t, false);
  const Script& s = *script;
  lerp3(translations + 3 * s.key0, translations + 3 * s.key1, s.frac, out);
  float o[3];
  lerp3(b.translations + 3 * s.key0, b.translations + 3 * s.key1, s.frac, o);
  for (int i = 0; i < 3; ++i) out[i] = (o[i] - out[i]) * w + out[i];
}

// Mech3.exe 1.2 @ 0x005ab700
void Part::blend_velocity(float t, const Part& b, float w, float out[3]) {
  script->set_phase(t, false);
  const Script& s = *script;
  if (velocities) lerp3(velocities + 3 * s.key0, velocities + 3 * s.key1, s.frac, out);
  else out[0] = out[1] = out[2] = 0.0f;
  float o[3] = {0, 0, 0};
  if (b.velocities) lerp3(b.velocities + 3 * s.key0, b.velocities + 3 * s.key1, s.frac, o);
  for (int i = 0; i < 3; ++i) out[i] = (o[i] - out[i]) * w + out[i];
  const float r = script->rate;
  out[0] = r * out[0], out[1] = r * out[1], out[2] = r * out[2];
}

// Mech3.exe 1.2 @ 0x005ab8c0
void Part::blend_rotation(float t, const Part& b, float w, float out[4]) {
  script->set_phase(t, false);
  const Script& s = *script;
  zmth::quat_slerp(rotations + 4 * s.key0, rotations + 4 * s.key1, s.frac, out);
  float o[4];
  zmth::quat_slerp(b.rotations + 4 * s.key0, b.rotations + 4 * s.key1, s.frac, o);
  zmth::quat_slerp(out, o, w, out);
}

// Mech3.exe 1.2 @ 0x005aac60: per key (translation - previous) per frame
// time; key 0 (which saw the origin as its previous) becomes the mean of
// keys n - 1 and 1, and key n closes the loop.
bool Part::make_velocities() {
  if (velocities) return true;
  const uint32_t n = script->frame_count;
  const float per = script->inv_frame_time;
  velocities = static_cast<float*>(std::malloc((n + 1) * 12));
  float prev[3] = {0, 0, 0};  // 0x0063d870
  for (uint32_t i = 0; i < n; ++i) {
    const float* k = translations + 3 * i;
    const float d[3] = {k[0] - prev[0], k[1] - prev[1], k[2] - prev[2]};
    float* v = velocities + 3 * i;
    v[0] = per * d[0], v[1] = per * d[1], v[2] = per * d[2];
    prev[0] = k[0], prev[1] = k[1], prev[2] = k[2];
  }
  lerp3(velocities + 3 * (n - 1), velocities + 3, 0.5f, velocities);
  std::memcpy(velocities + 3 * n, velocities, 12);
  return true;
}

// Mech3.exe 1.2 @ 0x005aadf0
bool Part::free_velocities() {
  if (velocities) {
    std::free(velocities);
    velocities = nullptr;
  }
  return true;
}

// Mech3.exe 1.2 @ 0x005aa780. Sampled through the old keys at rate 1 with
// the matrix reset; the caller sets the script's frame count afterwards.
bool Part::resample(uint32_t n) {
  if (n == script->frame_count) return true;
  const float step = script->loop_time / static_cast<float>(int64_t{n});
  const bool vel = velocities != nullptr;
  float* tr = static_cast<float*>(std::malloc((n + 1) * 12));
  float* ro = static_cast<float*>(std::malloc((n + 1) * 16));
  float* ve = vel ? static_cast<float*>(std::malloc((n + 1) * 12)) : nullptr;
  script->rate = 1.0f;
  reset_matrix(*this, true);
  float t = 0;
  for (uint32_t k = 0; k < n; ++k) {
    translation(t, tr + 3 * k);
    rotation(t, ro + 4 * k);
    if (vel) velocity(t, ve + 3 * k);
    t = t + step;
  }
  std::free(translations);
  std::free(rotations);
  if (vel) std::free(velocities);
  translations = tr;
  rotations = ro;
  if (vel) velocities = ve;
  std::memcpy(tr + 3 * n, tr, 12);
  std::memcpy(ro + 4 * n, ro, 16);
  if (vel) std::memcpy(ve + 3 * n, ve, 12);
  return true;
}

// Mech3.exe 1.2 @ 0x005aaa40
bool Part::rotate_keys(uint32_t first) {
  const uint32_t n = script->frame_count;
  const uint32_t rest = n - first;
  auto turn = [&](float*& keys, uint32_t w) {
    float* k = static_cast<float*>(std::malloc((n + 1) * w * 4));
    std::memcpy(k, keys + w * first, rest * w * 4);
    std::memcpy(k + w * rest, keys, first * w * 4);
    std::free(keys);
    keys = k;
    std::memcpy(k + w * n, k, w * 4);
  };
  turn(translations, 3);
  if (velocities) turn(velocities, 3);
  turn(rotations, 4);
  return true;
}

// Mech3.exe 1.2 @ 0x005aa940: the left-right mirror of keys 0..frames: x
// negated, and each rotation's y and z Euler angles.
void Part::mirror(uint32_t frames) {
  for (uint32_t i = 0; i <= frames; ++i) {
    float* k = translations + 3 * i;
    k[0] = -k[0];
    float* q = rotations + 4 * i;
    zmth::Mat34 m{};
    zmth::quat_matrix(q, m.m);
    float e[3];
    zmth::matrix_to_euler(m, e);
    zmth::euler_to_quat(-e[1], e[0], -e[2], q);  // the exe passes (yaw, pitch, roll)
  }
}

}  // namespace zsis
