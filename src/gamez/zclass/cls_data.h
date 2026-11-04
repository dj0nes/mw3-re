#pragma once
#include <cstdint>
#include <cstring>

#include "gamez/zclass/cls_node.h"

// The per-class node data as gamez.zbd stores it (docs/gamez.md, section 4).
// Each keeps its bytes as read; the fields after `raw` replace the pointers
// the exe writes over them. Each class's own .c file is the original home.
namespace zcls {

template <int N>
struct Raw {
  uint8_t raw[N];
  template <typename T> T get(int off) const {
    T v;
    std::memcpy(&v, raw + off, sizeof v);
    return v;
  }
  template <typename T> void put(int off, T v) { std::memcpy(raw + off, &v, sizeof v); }
  float* floats(int off) { return reinterpret_cast<float*>(raw + off); }
};

struct Camera : Raw<0x1e8> {
  Node* nodes[4] = {};             // +0x00..+0x0c  indices in the file
};

struct Partition : Raw<0x48> {
  Node** nodes = nullptr;          // +0x3c  count i16 at +0x3a
  uint8_t pm_tail[0x10] = {};      // PM only, +0x48..+0x57
  float (*pm_y)[2] = nullptr;      // PM only: each entry's y range (its 12-byte entries, +4 and +8)
  void* pm_clutter = nullptr;      // PM only: the cell's clutter records (pm/gamez/zclass/cls_clutter), count +0x40
  uint8_t* fades = nullptr;        // +0x44  0x44-byte entries, count +0x40 (run time only)
};

struct World : Raw<0xbc> {
  uint32_t** area_pending = nullptr;  // +0x0c  count +0x04
  float pm_area2[4] = {};          // PM only, +0x4c..+0x58 (a second area: left, bottom, right, top); PM's +0x5c on is MW3's +0x4c on
  Partition** rows = nullptr;      // +0x90  +0x8c rows of +0x88
  Node** lights = nullptr;         // +0xa4  count +0xa0
  void** light_data = nullptr;     // +0xa8
  Node** sounds = nullptr;         // +0xb0  count +0xac
  void** sound_data = nullptr;     // +0xb4
};

struct Window : Raw<0xf8> {};
struct Display : Raw<0x1c> {};     // clear colour floats at +0x10
struct Object3d : Raw<0x90> {};
struct Lod : Raw<0x50> {
  uint8_t pm_tail[0xc] = {};       // PM only, its +0x34..+0x3f (its +0x40.. are MW3's +0x34..)
};

struct Light : Raw<0xd0> {
  Node** nodes = nullptr;          // +0xcc  count +0xc8
  float pm_colors[12] = {};        // PM only, +0xb0..+0xdf (ambient, diffuse mixed, ambient mixed, combined); PM's +0xe0 on is MW3's +0xb0 on
};

struct Sound : Raw<0x94> {
  void* sound = nullptr;           // +0x00  zSound's sound, from the name at +0x08
  void* voice = nullptr;           // +0x04  zSound's voice while it plays
  Node** nodes = nullptr;          // +0x90  count +0x8c
};

// Seq.c: a sequence (made by the scripts' NewSEQ, 0x004fdc70; none in the
// files): +0x00 active, +0x04 repeat, +0x08 loop (0 bounces), +0x0c pause,
// +0x10 step (1 or -1), +0x14 index, +0x18 time, +0x1c count, then the
// entries {node, time} from +0x20.
struct Seq : Raw<0x20> {
  struct Entry {
    Node* node;
    float time;
  };
  Entry* entries = nullptr;        // +0x20  count +0x1c
};

// Animate.c (class 8; none in the files, nothing in MW3 makes one): +0x00
// matrix dirty (bit 0), +0x04 bit 2 playing, +0x08 its matrix, then a
// motion record at +0x68: +0x70 the rotation, +0x7c position, +0x88 scale
// sampled from the keys, +0x94.. their nine multipliers, +0xb8 duration,
// +0xc0 time, +0xc4 the restart time, +0xc8 the loop end, i16 +0xcc state
// (2 stopped), i16 +0xd0 key count, i16 +0xd2 -1 for no loop.
struct Animate : Raw<0xd4> {
  float* keys = nullptr;           // +0x6c  0x24 bytes each: rotation, position, scale
};

}  // namespace zcls
