#pragma once
#include <cstdint>
#include <cstdio>

// Animation scripts: each motion in motion.zbd (docs/motion.md).
// zSIScript\si_script.cpp. Sampling: docs/motion.md, Playback.
namespace zsis {

struct Script;

struct Part {               // 0x4c bytes
  float* translations;      // +0x00  (frames + 1) x vec3
  float* velocities;        // +0x04  (frames + 1) x vec3: the file's flag 2 block, or made by make_velocities
  float* rotations;         // +0x08  (frames + 1) x quaternion (w, x, y, z)
  bool has_matrix;          // +0x0c  samples go through matrix and offset
  float matrix[9];          // +0x10  3x3, row vectors; identity
  float offset[3];          // +0x34
  bool used = false;        // +0x40  found by name (find_part); the rest are freed
  bool owns_buffers;        // +0x41
  char* name;               // +0x44
  Script* script;           // +0x48  whose timing samples it (a blend lends its own)

  Part();                                    // 0x005aa500
  bool attach(Script* s, const Part* share); // 0x005aa5a0
  bool read(std::FILE* f, long end = -1);    // 0x005aa6d0; ours: `end` the entry's (a broken copy refused)
  void destroy();                            // 0x005aac10: the buffers, when owned

  void set_matrix(const float m[12], bool compose);  // 0x005aae10; null clears has_matrix
  void transform(float p[3]) const;          // 0x00421440 (mech_anim.cpp's copy), when has_matrix
  void translation(float t, float out[3]);   // 0x005aafe0
  void rotation(float t, float out[4]);      // 0x005ab1e0
  void velocity(float t, float out[3]);      // 0x005ab390: x the script's rate
  void blend_translation(float t, const Part& b, float w, float out[3]);  // 0x005ab4f0
  void blend_velocity(float t, const Part& b, float w, float out[3]);     // 0x005ab700
  void blend_rotation(float t, const Part& b, float w, float out[4]);     // 0x005ab8c0
  bool make_velocities();                    // 0x005aac60
  bool free_velocities();                    // 0x005aadf0
  bool resample(uint32_t frames);            // 0x005aa780
  bool rotate_keys(uint32_t first);          // 0x005aaa40
  void mirror(uint32_t frames);              // 0x005aa940: x and the y and z turns negated
  const float* key(uint32_t i, float* out) const;  // 0x004214c0: translation key i, copied when out
};

struct Script {             // vtable 0x5d0f20
  float rate = 1.0f;        // +0x04  playback rate
  float mult = 1.0f;        // +0x08  header +0x14, 1.0 in every motion; the mech's stride multiplier
  float stride = -1.0f;     // +0x0c  header +0x10, -1.0 in every motion; the mech's hip travel per loop
  bool active = false;      // +0x10  SetLoopTime does nothing while clear
  char name[0x43] = {};     // +0x11
  float loop_time = 0;      // +0x54
  float inv_loop_time = 0;  // +0x58
  float frame_time = 0;     // +0x5c
  float inv_frame_time = 0; // +0x60
  uint32_t frame_count = 0; // +0x64
  uint32_t part_count = 0;  // +0x68
  Part** parts = nullptr;   // +0x6c
  bool wraps = true;        // +0x70  phase over frame_count keys (the loops); clear: frame_count - 1
  float time = 0;           // +0x74  the time of the cached phase; reset by set_loop_time
  float phase = 0;          // +0x78  fraction of the loop
  float frac = 0;           // +0x7c  between key0 and key1
  int32_t key0 = 0;         // +0x80
  int32_t key1 = 1;         // +0x84

  // 0x005a9c70, vtable +4. Ours (docs/modding.md, M3): with the archive
  // entry's `end`, counts the bytes left cannot hold refuse the motion
  // (false, its parts freed); without one, the original's read.
  bool read(std::FILE* f, long end = -1);
  void share(const Script& from);  // 0x005a99f0: the same parts' buffers, frames, loop and rates
  void set_loop_time(float t);     // 0x005aa3b0
  void set_fps(float fps);         // 0x005aa450
  bool set_frames(uint32_t n);     // 0x005aa360: every part resampled
  void set_phase(float t, bool force);  // 0x005abae0 (wraps clear), 0x00432a80 (set)
  void phase_at(float t, int32_t* k0, int32_t* k1, float* f);  // 0x004212c0
  Part* find_part(const char* name, bool exact);  // 0x005aa270
  void free_unused();              // 0x005aa310
  void rotate_keys(uint32_t first);  // 0x005aa480
};

bool make_velocities(Part* p);     // 0x005aa4c0
bool free_velocities(Part* p);     // 0x005aa4e0
bool swap_parts(Part** a, Part** b);  // 0x005abab0: the parts trade places, the names stay

}  // namespace zsis
