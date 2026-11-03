#include "gamez/zmodel/gmod_puff.h"

#include <cmath>
#include <cstring>

#include "crt/msvc_math.h"
#include "crt/msvc_rand.h"
#include "gamez/zclass/class.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_texture.h"
#include "gamez/zmath/zmth_matrix.h"
#include "gamez/zmodel/gmod_clip2d.h"
#include "gamez/zmodel/gmod_cull.h"
#include "gamez/zrender/zrndr_draw.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/ztimer.h"

// The original rounds every step to float (the x87 at 24 bits in play).
#pragma STDC FP_CONTRACT OFF

namespace zmod {

namespace {
PuffState g_state;
PuffHooks g_hooks;

constexpr const char* kFile = "C:\\zipper\\GameZMech3\\zModel\\gmod_puff.cpp";
constexpr float kRandUnit = 3.051851e-05f;  // 0x005ce4d8, 1 / 32767
constexpr float kTwoPi = 6.2831855f;        // 0x005ce4ec
constexpr float kPi = 3.1415927f;           // -0x005ce4f0

float rand_unit() { return static_cast<float>(crt::msvc_rand()) * kRandUnit; }
// (r * k) * (hi - lo) + lo: the ranges' draw.
float rand_range(float lo, float hi) { return rand_unit() * (hi - lo) + lo; }

int32_t node_euler(zcls::Node* n, float out[3]) {
  return g_hooks.node_euler ? g_hooks.node_euler(g_hooks.ctx, n, out) : zcls::node_world_euler(n, out);
}
int32_t node_position(zcls::Node* n, float out[3]) {
  return g_hooks.node_position ? g_hooks.node_position(g_hooks.ctx, n, out) : zcls::node_world_position(n, out);
}

// v * m (rows m[0..2], m[3..5], m[6..8], translation m[9..11]); the x87 sums
// (x + y) + z, then the translation.
void rotate(const float v[3], const float m[12], float out[3]) {
  for (int c = 0; c < 3; ++c) out[c] = v[0] * m[c] + v[1] * m[3 + c] + v[2] * m[6 + c];
}
void transform(const float v[3], const float m[12], float out[3]) {
  for (int c = 0; c < 3; ++c) out[c] = v[0] * m[c] + v[1] * m[3 + c] + v[2] * m[6 + c] + m[9 + c];
}

// The keyframe walks of 0x005489a0: on from `at` while the next key's time
// has been reached (NaN stops), then the key's value or toward the next one.
template <typename Key>
void advance(const std::vector<Key>& keys, size_t& at, float t) {
  while (at + 1 != keys.size() && t >= keys[at + 1].time) ++at;
}
template <typename Key>
bool between(const std::vector<Key>& keys, size_t at, float t) {
  return !(keys[at].time >= t) && at + 1 != keys.size();
}

// age / life, 0 before the start (0x005489a0, twice).
float life_fraction(const Particle& p) { return p.age > 0.0f ? p.inv_life * p.age : 0.0f; }
}  // namespace

PuffState& puff_state() { return g_state; }
PuffHooks& puff_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x0054a710
Puffer* puffer_create() {
  g_state.puffers.emplace_back();
  return &g_state.puffers.back();
}

// Mech3.exe 1.2 @ 0x0054ab40
void puffer_set_active(Puffer* p, int32_t on) {
  p->active = on != 0;
  if (!p->active) p->has_prev = false;
}

void puffer_set_node(Puffer* p, zcls::Node* node) { p->node = node; }  // Mech3.exe 1.2 @ 0x0054ab60

namespace {
void copy3(float* dst, const float v[3]) { std::memcpy(dst, v, 12); }
}  // namespace

// Mech3.exe 1.2 @ 0x0054ab70 .. 0x0054ac10: vec3 copies.
void puffer_set_translate(Puffer* p, const float v[3]) { copy3(p->translate, v); }
void puffer_set_local_velocity(Puffer* p, const float v[3]) { copy3(p->local_velocity, v); }
void puffer_set_world_velocity(Puffer* p, const float v[3]) { copy3(p->world_velocity, v); }
void puffer_set_min_random(Puffer* p, const float v[3]) { copy3(p->min_random, v); }
void puffer_set_max_random(Puffer* p, const float v[3]) { copy3(p->max_random, v); }
void puffer_set_accel(Puffer* p, const float v[3]) { copy3(p->accel, v); }

void puffer_set_by_distance(Puffer* p, int32_t on) { p->by_distance = on != 0; }  // Mech3.exe 1.2 @ 0x0054ac30

// Mech3.exe 1.2 @ 0x0054ac40. Exactly 0 (fcomp C3, so NaN too) changes nothing.
void puffer_set_interval(Puffer* p, float v) {
  if (v < 0.0f || v > 0.0f) p->interval = v, p->inv_interval = 1.0f / v;
}

// Mech3.exe 1.2 @ 0x0054ac70 .. 0x0054acd0: plain stores.
void puffer_set_size_min(Puffer* p, float v) { p->size_min = v; }
void puffer_set_size_max(Puffer* p, float v) { p->size_max = v; }
void puffer_set_life_min(Puffer* p, float v) { p->life_min = v; }
void puffer_set_life_max(Puffer* p, float v) { p->life_max = v; }
void puffer_set_age_min(Puffer* p, float v) { p->age_min = v; }
void puffer_set_age_max(Puffer* p, float v) { p->age_max = v; }
void puffer_set_deviation(Puffer* p, float v) { p->deviation = v; }

// Mech3.exe 1.2 @ 0x0054ace0 / 0x0054ad30. The inverse is end - start itself
// when that is exactly 0 (or NaN).
void puffer_set_near_fade(Puffer* p, float start, float end) {
  const float span = end - start;
  p->near_start = start, p->near_end = end;
  p->near_inv = span < 0.0f || span > 0.0f ? 1.0f / span : span;
}
void puffer_set_far_fade(Puffer* p, float start, float end) {
  const float span = end - start;
  p->far_start = start, p->far_end = end;
  p->far_inv = span < 0.0f || span > 0.0f ? 1.0f / span : span;
}

// Mech3.exe 1.2 @ 0x0054ad80 .. 0x0054ada0
void puffer_set_friction(Puffer* p, float v) { p->friction = v; }
void puffer_set_wind_factor(Puffer* p, float v) { p->wind_factor = v; }
void puffer_set_number(Puffer* p, int32_t n) { p->number = n; }

// Mech3.exe 1.2 @ 0x0054adb0. Also back to one random texture per particle.
void puffer_clear_textures(Puffer* p) {
  p->textures.clear();
  p->timed_textures = false;
}

// Mech3.exe 1.2 @ 0x0054adf0 / 0x0054afc0 (EDX the entry). The last one added
// decides whether textures are timed.
void puffer_add_texture(Puffer* p, zimg::TextureEntry* t) {
  p->textures.push_back({0.0f, t});
  p->timed_textures = false;
}
void puffer_add_timed_texture(Puffer* p, zimg::TextureEntry* t, float time) {
  p->textures.push_back({time, t});
  p->timed_textures = true;
}

// Mech3.exe 1.2 @ 0x0054b190 / 0x0054b1d0
void puffer_clear_colors(Puffer* p) { p->colors.clear(); }
void puffer_add_color(Puffer* p, float time, float r, float g, float b, float a) {
  p->colors.push_back({r, g, b, a, time});
}

// Mech3.exe 1.2 @ 0x0054b420 / 0x0054b460
void puffer_clear_growth(Puffer* p) { p->growth.clear(); }
void puffer_add_growth(Puffer* p, float time, float growth) { p->growth.push_back({time, growth}); }

// Mech3.exe 1.2 @ 0x0054b630 .. 0x0054b670
void wind_set_velocity(const float v[3]) { copy3(g_state.wind.base, v); }
void wind_set_max_speed(float v) { g_state.wind.max_speed = v; }
void wind_set_random_accel(float v) { g_state.wind.accel = v; }
void wind_set_random_angvel(float radians) { g_state.wind.angular_velocity = radians; }

// Mech3.exe 1.2 @ 0x00549410. The wind's angle and speed are left as they
// were (a new mission starts with the last one's wandering wind).
void puff_tick_reset() {
  PuffState& s = g_state;
  if (!s.tick_node) {
    s.tick_node = g_hooks.create_node ? g_hooks.create_node(g_hooks.ctx) : nullptr;
    if (!s.tick_node) {
      zerr::report(0x400, kFile, 0x1e2, "Error allocating puffer tickNode");
    } else {
      if (g_hooks.set_priority) g_hooks.set_priority(g_hooks.ctx, s.tick_node, 5);
      if (g_hooks.set_callback) g_hooks.set_callback(g_hooks.ctx, s.tick_node, puff_tick);
    }
  }
  Wind& w = s.wind;
  for (int i = 0; i < 3; ++i) w.current[i] = w.base[i] = 0.0f;  // the zero vector 0x0063d870
  w.max_speed = 0.0f, w.accel = 0.0f, w.angular_velocity = 0.0f;
}

// Mech3.exe 1.2 @ 0x005495a0
void puff_shutdown() {
  PuffState& s = g_state;
  if (s.tick_node) {
    if (g_hooks.set_callback) g_hooks.set_callback(g_hooks.ctx, s.tick_node, nullptr);
    if (g_hooks.destroy_node) g_hooks.destroy_node(g_hooks.ctx, s.tick_node);
    s.tick_node = nullptr;
  }
  s.puffers.clear();
  s.particles.clear();
}

// Mech3.exe 1.2 @ 0x0054a090. Emits `number` particles per interval reached
// (time, or distance moved) along the path since the last frame; each starts
// part of a frame old. docs/puffers.md, emission.
void puffer_update(Puffer& p, float dt) {
  if (!p.active) return;
  float m[12];
  float cur[3];
  if (p.node) {
    // World euler -> quaternion -> 3x3, the node's world position as row 3.
    float e[3], q[4];
    node_euler(p.node, e);
    zmth::euler_to_quat(e[1], e[0], e[2], q);
    zmth::quat_matrix(q, m);
    node_position(p.node, m + 9);
    transform(p.translate, m, cur);
  } else {
    copy3(cur, p.translate);
  }
  if (p.has_prev) {
    const float d[3] = {cur[0] - p.prev[0], cur[1] - p.prev[1], cur[2] - p.prev[2]};
    float step = dt;
    if (p.by_distance) {
      const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
      step = dist > 200.0f ? 0.0f : dist;  // a jump (teleport) emits nothing
    }
    p.accum = step + p.accum;
    const int32_t n = crt::msvc_ftol(p.inv_interval * p.accum);
    const float frame = ztimer::state().dt;
    for (int32_t i = 1; i <= n; ++i) {
      const float frac = static_cast<float>(i) * p.interval / p.accum;
      if (p.number <= 0) continue;
      const float rest = 1.0f - frac;
      for (int32_t j = 0; j < p.number; ++j) {
        const float life = rand_range(p.life_min, p.life_max);
        const float age = rand_unit() * (p.age_max - p.age_min) + frame * rest + p.age_min;
        if (age >= life) continue;
        Particle q{};
        for (int c = 0; c < 3; ++c) q.pos[c] = (rand_unit() - 0.5f) * p.deviation + frac * d[c] + p.prev[c];
        q.size = rand_range(p.size_min, p.size_max);
        float base[3];
        if (p.node)
          rotate(p.local_velocity, m, base);
        else
          copy3(base, p.local_velocity);
        for (int c = 0; c < 3; ++c)
          q.vel[c] = rand_unit() * (p.max_random[c] - p.min_random[c]) + p.world_velocity[c] + base[c] + p.min_random[c];
        if (p.timed_textures) {
          q.textures = p.textures;
        } else {
          zimg::TextureEntry* t = zimg::default_entry();
          if (!p.textures.empty()) {
            const auto r = static_cast<uint32_t>(crt::msvc_rand());
            t = p.textures[r % p.textures.size()].texture;
          }
          q.textures.push_back({0.0f, t});
        }
        copy3(q.accel, p.accel);
        q.age = age;
        q.life = life;
        q.inv_life = 1.0f / life;
        q.near_start = p.near_start, q.near_end = p.near_end, q.near_inv = p.near_inv;
        q.far_start = p.far_start, q.far_end = p.far_end, q.far_inv = p.far_inv;
        q.friction = p.friction;
        q.wind_factor = p.wind_factor;
        q.colors = p.colors;
        q.growth = p.growth;
        q.texture_at = q.color_at = q.growth_at = 0;
        g_state.particles.push_back(std::move(q));
      }
    }
    p.accum = p.accum - static_cast<float>(n) * p.interval;
  }
  p.has_prev = true;
  copy3(p.prev, cur);
}

// Mech3.exe 1.2 @ 0x005490e0. The wind wanders: its direction by up to
// angvel * dt a frame, its speed by up to `accel` a frame (not scaled by dt),
// clamped to 0..max_speed. Particles relax toward wind * factor at
// e^-(friction dt) (zmth::exp_neg: none at all when friction * dt < 1/51).
void puff_tick() {
  PuffState& s = g_state;
  Wind& w = s.wind;
  const float dt = ztimer::state().dt;
  w.angle = (rand_unit() * 2.0f - 1.0f) * dt * w.angular_velocity + w.angle;
  if (!(w.angle >= 0.0f))
    w.angle = w.angle - -kTwoPi;
  else if (w.angle >= kTwoPi)
    w.angle = w.angle - kTwoPi;
  // FIX (B): the exe steps the speed by up to accel a frame; per 1/30 s here (puffers.md).
  const float step = zfix::options().per_second ? w.accel * dt * zfix::kReferenceFps : w.accel;
  w.speed = (rand_unit() * 2.0f - 1.0f) * step + w.speed;
  if (!(w.speed >= 0.0f)) {
    w.angle = w.angle - -kPi;  // turned round, not wrapped again
    w.speed = -w.speed;
  }
  if (w.speed > w.max_speed) w.speed = w.max_speed;
  // fcos / fsin of the angle as a double; as floats, as in euler_to_quat.
  const auto c = static_cast<float>(std::cos(double{w.angle})), sn = static_cast<float>(std::sin(double{w.angle}));
  w.current[0] = c * w.speed + w.base[0];
  w.current[1] = w.base[1];
  w.current[2] = sn * w.speed + w.base[2];

  for (Puffer& p : s.puffers) puffer_update(p, ztimer::state().dt);

  for (auto it = s.particles.begin(); it != s.particles.end();) {
    Particle& q = *it;
    q.age = dt + q.age;
    for (int c = 0; c < 3; ++c) q.pos[c] = dt * q.vel[c] + q.pos[c];
    for (int c = 0; c < 3; ++c) q.vel[c] = q.accel[c] * dt + q.vel[c];
    if (q.friction < 0.0f || q.friction > 0.0f) {
      // FIX (B): the exe's table truncates to steps of 1/51, so no drag at all
      // above ~51 fps for friction 1; e^-x computed here (puffers.md).
      const float x = ztimer::state().dt * q.friction;
      const float f = zfix::options().per_second ? (x > 5.0f ? 0.0f : x < 0.0f ? 1.0f : std::exp(-x))
                                                 : zmth::exp_neg(x);
      for (int c = 0; c < 3; ++c) {
        const float target = w.current[c] * q.wind_factor;
        q.vel[c] = (q.vel[c] - target) * f + target;
      }
    }
    if (q.age >= q.life)
      it = s.particles.erase(it);
    else
      ++it;
  }
}

// Mech3.exe 1.2 @ 0x005489a0. Size grows by the growth keys, alpha follows the
// colour keys (else 1 - life fraction) times the distance fade; the sprite
// is skipped when culled, faded out or under a pixel (docs/puffers.md).
void particle_draw(Particle& p, const float m[12], float near_z, float fx_near) {
  const float t = life_fraction(p);
  float growth = 1.0f;
  if (!p.growth.empty()) {
    advance(p.growth, p.growth_at, t);
    const PuffGrowthKey& k = p.growth[p.growth_at];
    if (between(p.growth, p.growth_at, t)) {
      const PuffGrowthKey& n = p.growth[p.growth_at + 1];
      growth = (n.growth - k.growth) * ((t - k.time) / (n.time - k.time)) + k.growth;
    } else {
      growth = k.growth;
    }
  }
  uint32_t mask = 0x30;  // near and far only
  if (cull_sphere(p.pos, &mask, growth * p.size)) return;
  float v[3];
  transform(p.pos, m, v);
  const float d = v[2] * near_z;
  float fade;
  if (d >= p.far_end)
    fade = 0.0f;
  else if (d > p.far_start)
    fade = (p.far_end - d) * p.far_inv;
  else if (!(d > p.near_start))
    fade = 0.0f;
  else if (d >= p.near_end)
    fade = 1.0f;
  else
    // FIX (bug), behind bugs: the exe subtracts far_start (+0x3c) here, not
    // near_start (+0x34), so in the fade-in band the alpha is negative and
    // the sprite is skipped. No shipped puffer has a fade-in (docs/fixes.md).
    fade = (d - (zfix::options().bugs ? p.near_start : p.far_start)) * p.near_inv;
  if (!(fade > 0.0f)) return;
  const float inv = 1.0f / v[2];
  const float screen[3] = {inv * v[0] + zmth::projection().cx, inv * v[1] + zmth::projection().cy, inv};
  const float size = inv * fx_near * p.size;
  const float t2 = life_fraction(p);
  float alpha;
  if (p.colors.empty()) {
    alpha = (1.0f - t2) * fade;
  } else {
    advance(p.colors, p.color_at, t2);
    const PuffColorKey& k = p.colors[p.color_at];
    float c[4] = {k.r, k.g, k.b, k.a};
    if (between(p.colors, p.color_at, t2)) {
      const PuffColorKey& n = p.colors[p.color_at + 1];
      const float u = (t2 - k.time) / (n.time - k.time);
      c[0] = (n.r - k.r) * u + k.r;
      c[1] = (n.g - k.g) * u + k.g;
      c[2] = (n.b - k.b) * u + k.b;
      c[3] = (n.a - k.a) * u + k.a;
    }
    for (int i = 0; i < 4; ++i) std::memcpy(clip2d().work[i].color, c, 12);
    alpha = c[3] * fade;
  }
  advance(p.textures, p.texture_at, t2);
  zrndr::draw_sprite(screen, p.textures[p.texture_at].texture, size * growth, alpha, !p.colors.empty());
}

// Mech3.exe 1.2 @ 0x005496c0. The view matrix with the projection's x, y and
// depth factors folded in, then every particle.
void puff_draw() {
  if (g_state.particles.empty()) return;
  const float* view = zmth::view().view.m;
  const zmth::Projection& pr = zmth::projection();
  float m[12];
  for (int r = 0; r < 4; ++r) {
    m[3 * r] = view[3 * r] * pr.fx_near;
    m[3 * r + 1] = view[3 * r + 1] * pr.fy_near;
    m[3 * r + 2] = view[3 * r + 2] * pr.inv_near;
  }
  for (Particle& p : g_state.particles) particle_draw(p, m, pr.near_z, pr.fx_near);
}

}  // namespace zmod
