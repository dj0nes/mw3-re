#pragma once
#include <cfloat>
#include <cstdint>
#include <list>
#include <vector>

// zModel\gmod_puff.cpp: puffers, the emitters of smoke, fire and dust sprites
// that animations create and steer (anim.zbd event 42, zeff_anim_puffer), the
// particles they emit, the global wind that drifts them, and their drawing
// as screen-facing textured quads (docs/puffers.md). The file holds 0x549410
// by its error report; its neighbours 0x548840..0x54b750 are the same system.
namespace zcls { struct Node; }
namespace zimg { struct TextureEntry; }

namespace zmod {

// The exe's keyframe tracks are MSVC std::vectors (an allocator byte, begin,
// end, capacity); a particle's "current key" is an iterator, here an index.
struct PuffTextureKey {            // 8 bytes
  float time;                      // +0  life fraction
  zimg::TextureEntry* texture;     // +4
};
struct PuffColorKey {              // 0x14 bytes
  float r, g, b, a;                // +0x00  0..255 colour, alpha factor
  float time;                      // +0x10  life fraction
};
struct PuffGrowthKey {             // 8 bytes
  float time;                      // +0  life fraction
  float growth;                    // +4  size factor
};

// A puffer, 0xe4 bytes (0x0054a710); Pirate's Moon's is 0xec (0x00565ad0): its draw priority at
// +0x70, MW3's +0x70.. at +0x74.., the particles' shared colour and growth at +0xe8 (kept per
// particle here: every colour or growth setter drops the share, so the values are the same).
struct Puffer {
  bool active = false;             // +0x00
  int32_t number = 1;              // +0x04  particles per emission
  bool timed_textures = false;     // +0x08  0: a random texture per particle
  std::vector<PuffTextureKey> textures;  // +0x0c
  std::vector<PuffColorKey> colors;      // +0x1c
  std::vector<PuffGrowthKey> growth;     // +0x2c
  bool by_distance = false;        // +0x3c  interval in distance moved, else seconds
  float interval = 1.0f;           // +0x40
  float inv_interval = 1.0f;       // +0x44
  float accum = 0.0f;              // +0x48  time or distance since the last emission
  float size_min = 1.0f, size_max = 1.0f;    // +0x4c, +0x50
  float life_min = 1.0f, life_max = 1.0f;    // +0x54, +0x58
  float age_min = 0.0f, age_max = 0.0f;      // +0x5c, +0x60  start age
  float deviation = 0.0f;          // +0x64  random offset per axis
  float friction = 0.0f;           // +0x68  drag toward the wind; 0: no drag, no wind
  float wind_factor = 1.0f;        // +0x6c
  float priority = 0.0f;           // PM +0x70  the sprites' depth bias, in steps of the inverse-Z tolerance
  zcls::Node* node = nullptr;      // +0x70  emits at this node, turned with it
  float prev[3] = {};              // +0x74  last frame's position
  bool has_prev = false;           // +0x80
  float translate[3] = {};         // +0x84
  float local_velocity[3] = {};    // +0x90  turned with the node
  float world_velocity[3] = {};    // +0x9c
  float min_random[3] = {};        // +0xa8  random velocity range
  float max_random[3] = {};        // +0xb4
  float accel[3] = {};             // +0xc0
  float near_start = 0.0f, near_end = 0.0f, near_inv = 0.0f;           // +0xcc..+0xd4  fade in
  float far_start = FLT_MAX, far_end = FLT_MAX, far_inv = 0.0f;        // +0xd8..+0xe0  fade out
};

// A particle, 0x90 bytes (0x0054a090).
struct Particle {
  float pos[3];                    // +0x00
  float vel[3];                    // +0x0c
  float accel[3];                  // +0x18
  float size;                      // +0x24
  float age;                       // +0x28
  float life;                      // +0x2c
  float inv_life;                  // +0x30
  float near_start, near_end;      // +0x34, +0x38
  float far_start, far_end;        // +0x3c, +0x40
  float near_inv, far_inv;         // +0x44, +0x48
  float friction;                  // +0x4c
  float wind_factor;               // +0x50
  float priority;                  // PM +0x54 (PM's particle is 0x78 bytes, 0x00565320)
  size_t texture_at;               // +0x54  iterator into textures
  std::vector<PuffTextureKey> textures;  // +0x58
  size_t color_at;                 // +0x68
  std::vector<PuffColorKey> colors;      // +0x6c
  std::vector<PuffGrowthKey> growth;     // +0x7c
  size_t growth_at;                // +0x8c
};

// The global wind (world.zrd `global_wind`, docs/world-params.md): a base
// velocity plus a wandering part of random direction and speed.
struct Wind {
  float current[3] = {};           // 0x0072be28  what particles drift toward
  float base[3] = {};              // 0x0072be38  `velocity`
  float angle = 0.0f;              // 0x0072be44  never reset
  float speed = 0.0f;              // 0x0072be48  never reset
  float max_speed = 0.0f;          // 0x0072be4c  `max_speed`
  float angular_velocity = 0.0f;   // 0x0072be50  `random_angvel`, radians
  float accel = 0.0f;              // 0x0072be54  `random_accel`
};

// The exe's two std::lists (0x0072be04 count 0x0072be08, 0x0072be14 count
// 0x0072be18); std::list keeps the addresses the animations hold.
struct PuffState {
  std::list<Puffer> puffers;       // 0x0072be04
  std::list<Particle> particles;   // 0x0072be14
  Wind wind;
  zcls::Node* tick_node = nullptr; // 0x0072be58
  bool on = false;                 // PM [0x00612eb0]: set by the first reset; no puffer is made, none drawn, before
};
PuffState& puff_state();

// The zClass calls for the tick node, not ported (0x004eb5b0, 0x004eb450,
// 0x004eaea0), and the node's world rotation and position (ported in zcls;
// the tests stand in for them). Null create_node makes no node. Pirate's Moon takes the
// node's world matrix instead (0x004ff360); node_euler / node_position are MW3's, unused here.
struct PuffHooks {
  void* ctx = nullptr;
  zcls::Node* (*create_node)(void* ctx) = nullptr;                                 // 0x004fb740
  int32_t (*set_priority)(void* ctx, zcls::Node* node, int32_t priority) = nullptr;  // 0x004eb5b0
  // The node's action callback: puff_tick (0x005490e0), or null to clear it.
  int32_t (*set_callback)(void* ctx, zcls::Node* node, void (*tick)()) = nullptr;  // 0x004eb450
  void (*destroy_node)(void* ctx, zcls::Node* node) = nullptr;                     // 0x004eaea0
  int32_t (*node_euler)(void* ctx, zcls::Node* node, float out[3]) = nullptr;      // 0x004ed5d0
  int32_t (*node_position)(void* ctx, zcls::Node* node, float out[3]) = nullptr;   // 0x004ed530
  // Ours, for the tests: a sprite's cull (Pirate's Moon's [0x00611354]) and draw; null: cull_sphere and
  // zrndr::draw_sprite.
  uint32_t (*cull)(void* ctx, const float centre[3], uint32_t* mask, float radius) = nullptr;
  void (*sprite)(void* ctx, const float screen[3], const zimg::TextureEntry* t, float half_size, float alpha,
                 int32_t colored) = nullptr;
};
PuffHooks& puff_hooks();

// --- Puffers ---
Puffer* puffer_create();                                   // 0x0054a710
void puffer_set_active(Puffer* p, int32_t on);             // 0x0054ab40  off also forgets the position
void puffer_set_node(Puffer* p, zcls::Node* node);         // 0x0054ab60
void puffer_set_translate(Puffer* p, const float v[3]);    // 0x0054ab70
void puffer_set_local_velocity(Puffer* p, const float v[3]);  // 0x0054ab90
void puffer_set_world_velocity(Puffer* p, const float v[3]);  // 0x0054abb0
void puffer_set_min_random(Puffer* p, const float v[3]);   // 0x0054abd0
void puffer_set_max_random(Puffer* p, const float v[3]);   // 0x0054abf0
void puffer_set_accel(Puffer* p, const float v[3]);        // 0x0054ac10
void puffer_set_by_distance(Puffer* p, int32_t on);        // 0x0054ac30
void puffer_set_interval(Puffer* p, float v);              // 0x0054ac40  0 is ignored
void puffer_set_size_min(Puffer* p, float v);              // 0x0054ac70
void puffer_set_size_max(Puffer* p, float v);              // 0x0054ac80
void puffer_set_life_min(Puffer* p, float v);              // 0x0054ac90
void puffer_set_life_max(Puffer* p, float v);              // 0x0054aca0
void puffer_set_age_min(Puffer* p, float v);               // 0x0054acb0
void puffer_set_age_max(Puffer* p, float v);               // 0x0054acc0
void puffer_set_deviation(Puffer* p, float v);             // 0x0054acd0
void puffer_set_near_fade(Puffer* p, float start, float end);  // 0x0054ace0
void puffer_set_far_fade(Puffer* p, float start, float end);   // 0x0054ad30
void puffer_set_friction(Puffer* p, float v);              // 0x0054ad80
void puffer_set_wind_factor(Puffer* p, float v);           // 0x0054ad90
void puffer_set_number(Puffer* p, int32_t n);              // 0x0054ada0
void puffer_set_priority(Puffer* p, float v);              // Pirate's Moon 0x00566150
void puffer_clear_textures(Puffer* p);                     // 0x0054adb0
void puffer_add_texture(Puffer* p, zimg::TextureEntry* t);  // 0x0054adf0  untimed
void puffer_add_timed_texture(Puffer* p, zimg::TextureEntry* t, float time);  // 0x0054afc0
void puffer_clear_colors(Puffer* p);                       // 0x0054b190
void puffer_add_color(Puffer* p, float time, float r, float g, float b, float a);  // 0x0054b1d0
void puffer_clear_growth(Puffer* p);                       // 0x0054b420
void puffer_add_growth(Puffer* p, float time, float growth);  // 0x0054b460

// --- Wind ---
void wind_set_velocity(const float v[3]);                  // 0x0054b630
void wind_set_max_speed(float v);                          // 0x0054b650
void wind_set_random_accel(float v);                       // 0x0054b660
void wind_set_random_angvel(float radians);                // 0x0054b670

// --- The frame ---
// Makes the tick node once (priority 5, callback puff_tick); every call
// zeroes the wind's velocity and limits (world.zrd sets them after).
void puff_tick_reset();                                    // 0x00549410
// The tick node's callback: the wind, every puffer, every particle.
void puff_tick();                                          // 0x005490e0
// One puffer's emission for a frame step (ECX puffer, stack dt).
void puffer_update(Puffer& p, float dt);                   // 0x0054a090
// Frees every puffer and particle and the tick node.
void puff_shutdown();                                      // 0x005495a0

// The world render's call after the scene walk (zcls::world_render_hooks()
// .puffers): every particle through the camera, as a sprite.
void puff_draw();                                          // 0x005496c0
// ECX particle; m: the view matrix scaled by the projection (0x005496c0).
void particle_draw(Particle& p, const float m[12], float near_z, float fx_near);  // 0x005489a0

}  // namespace zmod
