#pragma once
#include <cstdint>
#include <cstdio>

#include "gamez/zclass/cls_data.h"

// zEffect\zeff_anim_init.c: reading anim.zbd, the mission's animation
// definitions (docs/anim.md).
namespace zeff {

using zcls::Node;
using zcls::Raw;

struct SeqDef : Raw<0x40> {
  uint8_t* cur = nullptr;          // +0x34  the current event (run time; the file's value is stale)
  const char* name() const { return reinterpret_cast<const char*>(raw); }
  uint8_t* events = nullptr;       // +0x38  size +0x3c
};

struct NodeRef40 : Raw<0x28> { Node* node = nullptr; };    // +0x24
struct ObjectRef : Raw<0x60> { Node* node = nullptr; };    // +0x24
struct CreatedRef : Raw<0x2c> { void* made = nullptr; };   // +0x24  light, puffer or sound node
struct StaticSoundRef : Raw<0x24> { void* sound = nullptr; };  // +0x20
struct EffectRef : Raw<0x24> {};   // +0x20 effect index
struct AnimDef;
struct Prerequisite : Raw<0x30> {
  AnimDef* def = nullptr;          // +0x28  type 1
  Node* node = nullptr;            // +0x2c  types 2, 3
};
struct AnimDef;
struct AnimRefRec : Raw<0x48> { AnimDef* ref = nullptr; };  // +0x44  the def a call started

struct AnimDef : Raw<0x13c> {
  // ECX def, EDX the argument set with it, stack a value (0 when the anim
  // stops, the callback event's +0x0c otherwise).
  using Callback = void (*)(AnimDef* def, void* arg, int32_t value);
  Node* node = nullptr;            // +0x40  found by the name at +0x20
  Node* root = nullptr;            // +0x64  found by the name at +0x44
  Node* object = nullptr;          // +0x68  its own object3d `_<name>`, whose action ticks it
  Callback callback = nullptr;     // +0x6c  set by 0x00508f00 with +0x70
  void* callback_arg = nullptr;    // +0x70
  Node* at_node = nullptr;         // +0x74  the node it was started at (event node -200), +0x78 offset
  Node* at_node2 = nullptr;        // +0x84  a connector's other end, +0x88 offset
  AnimDef* instance = nullptr;     // +0x138 the next copy of it (started at another node)
  SeqDef* seqs = nullptr;          // +0xc4  count +0x108
  uint8_t* reset_events = nullptr; // +0x100 size +0x104
  SeqDef reset_seq;                // +0xc8..+0x107 at run time: the reset sequence (its bytes
                                   // copied from raw after the read, events = reset_events)
  ObjectRef* objects = nullptr;    // +0x114 count +0x109
  NodeRef40* nodes = nullptr;      // +0x118 count +0x10a
  CreatedRef* lights = nullptr;    // +0x11c count +0x10b
  CreatedRef* puffers = nullptr;   // +0x120 count +0x10c
  CreatedRef* sounds = nullptr;    // +0x124 count +0x10d
  StaticSoundRef* static_sounds = nullptr;  // +0x128 count +0x10e
  EffectRef* effects = nullptr;    // +0x12c count +0x10f
  Prerequisite* prereqs = nullptr; // +0x130 count +0x110
  AnimRefRec* anim_refs = nullptr; // +0x134 count +0x112
  const char* name() const { return reinterpret_cast<const char*>(raw); }
  uint8_t count(int off) const { return raw[off]; }
};

struct AnimMessage : Raw<0x24> {};  // +0x20 result of 0x575f90

struct AnimState {
  Raw<0x44> block{};               // 0x0073f720, read whole from the file
  AnimDef* defs = nullptr;         // 0x0073f72c  count s16 0x0073f72a
  AnimMessage* messages = nullptr; // 0x0073f734  count 0x0073f730
  void* world = nullptr;           // 0x0073f738  set by 0x005090e0, kept over the read
  char path[0x80] = {};            // 0x0060c570
  int32_t loading = 0;             // 0x005ebcbc
  int32_t copy_trees = 0;          // 0x0060c5f0  never set: the copy path is dead
  bool fix_prerequisites = true;   // FIX (bug) on a missing prerequisite node, with zfix bugs (false: the exe)
  bool fix_instance_tree = true;   // FIX (bug): an instance at a node does not inherit +0x94 bit 17 (false: the exe)
  float random[200] = {};          // 0x0073f400  rand() / 32767, filled at init (0x00508fc0)
  int32_t random_index = 0;        // 0x0060c5f4
  uint8_t stagger = 0;             // 0x0060c1d8  1..10, spreads the range/zone tests over frames
  int32_t stopped_elapsed = 0;     // 0x0060c1d4  0x00502d20: a stopped anim keeps its reset clock
  int32_t level = 0;               // 0x0060c1d0  0x00508fb0, the "if" event's level test
  int32_t sounds_on = 1;           // 0x005ebbf0
  int32_t save_on = 1;             // 0x005ebcc0  0x005090d0: the registrants added at the start
  zcls::Node* player_node = nullptr;  // 0x0073f740 (block +0x20; the files hold 0, the setter is unknown)
  int16_t def_count() const { return block.get<int16_t>(0x0a); }
  int32_t message_count() const { return block.get<int32_t>(0x10); }
  // The globals the block covers (0x0073f720 + offset), kept in the block:
  // +0x24 player position known, +0x28 zone known, +0x2c player position,
  // +0x38 zone list, +0x3c the "if" event's flag 0x40, +0x40 the time the
  // running sequence still has this frame.
  float& time_left() { return *block.floats(0x40); }
};
AnimState& anim();

// The calls into engine parts not ported yet; the front end or a test sets them.
struct AnimHooks {
  Node* (*copy_tree)(Node* node);                          // 0x004f9b50(node, 1, 1): a running def's instance (the post-load copy is dead)
  void* (*create_puffer)();                                // 0x0054a710, gmod_puff
  void* (*find_sound)(const char* name);                   // 0x0056f6f0, zSound
  int32_t (*effect_index)(const char* name);               // 0x0050d9e0, zEffect
};
AnimHooks& anim_hooks();

void anim_set_world(void* world);         // 0x005090e0
void anim_set_zbd_path(const char* path); // 0x005090f0
void anim_allow_loading(int32_t on);      // 0x00509150
int anim_read_zbd();                      // 0x0050a0e0
Node* anim_find_in_tree(Node* node, const char* name);    // 0x005096d0
int32_t anim_light_index(AnimDef* def, const char* name); // 0x005091e0
int32_t anim_sound_index(AnimDef* def, const char* name); // 0x00509160
Node* anim_find_node(AnimDef* def, const char* name);     // 0x00509640
AnimDef* anim_find(const char* name);                     // 0x0050b450
AnimDef* anim_next_with_callbacks(AnimDef* after);        // 0x0050b570
Node* anim_node(const AnimDef* d);                        // 0x0050b5e0
void anim_free(AnimDef* def, bool instances);             // 0x0050b0e0
int32_t anim_free_all();                                  // 0x0050b380
int32_t anim_freed_defs();  // ours: how many defs anim_free_all has freed in all (the tests)
void anim_free_copies();                                  // 0x0050b2f0
int32_t anim_shutdown();                                  // 0x0050b430
int32_t anim_init();                                      // 0x00508fc0
void anim_enable_save(int32_t on);                        // 0x005090d0
void anim_register_savers();                              // the three zSave registrants (in 0x00508fc0)

}  // namespace zeff
