#pragma once
#include <cstdint>
#include <vector>

#include "gamez/zclass/cls_node.h"

namespace zsave { struct Context; }

// zEffect\zeff_anim_save.c: the log of animation calls (a start, a start at
// a node, a connector, a reset) that savegames and network play replay
// (docs/anim-postload.md, start path). The loggers sit right after the file,
// before zeff_init.c (0x0050cbb0..0x0050d0f0): kept here.
namespace zeff {

struct AnimDef;

struct AnimLogRecord {             // 0x50 bytes
  int32_t type = 0;                // +0x00  1 start at, 2 start, 3 connector, 4 connector at, 5 reset
  uint32_t serial = 0;             // +0x04  mask (0x0060c718) & index & 0xffffff
  char name[0x20] = {};            // +0x08  the def's name, strncpy'd
  int32_t node = -1;               // +0x28  node index (0x004fe180), -1 for none
  uint32_t args[9] = {};           // +0x2c  floats or node indices, by type
};

struct AnimLog {
  std::vector<AnimLogRecord> records;  // 0x0060c708, grown by doubling (FIX (A): a vector)
  int32_t capacity = 0;            // 0x0060c70c  1000 on first use
  int32_t count = 0;               // 0x0060c710
  uint32_t mask = 0;               // 0x0060c718
  int32_t save_on = 1;             // 0x005ebf0c  defaults: keep records for the savegame
  int32_t net_on = 1;              // 0x005ebf10  and hand them to the network
  void (*net)(AnimLogRecord* rec) = nullptr;  // [0x0060c714]
};
AnimLog& anim_log();

AnimLogRecord* log_new_record();   // 0x0050bd50
void log_drop_record();            // 0x0050ccd0, the last one

// ECX def, EDX node. Each returns the record handed to the network, else null.
AnimLogRecord* log_start_at(AnimDef* def, zcls::Node* node, const float pos[3], const float rot[3],
                            const float vec[3]);                          // 0x0050cbb0 (nine floats)
AnimLogRecord* log_start(AnimDef* def, zcls::Node* node, float x, float y, float z);  // 0x0050cce0
AnimLogRecord* log_connector(AnimDef* def, zcls::Node* node, zcls::Node* from, const float* from_pos,
                             const float* vec);                           // 0x0050cde0
AnimLogRecord* log_connector_at(AnimDef* def, zcls::Node* node, zcls::Node* from, const float* from_pos,
                                zcls::Node* to, const float* to_pos);      // 0x0050cf40
AnimLogRecord* log_reset(AnimDef* def, zcls::Node* node);               // 0x0050d0f0

// The network's (0x1a, multiplayer-port.md piece 7b): a record's size as
// sent, whether it is kept (bit 0) and sent (bit 1), whether one like it is
// logged.
int32_t log_record_size(const AnimLogRecord* r);  // 0x0050ca20
uint32_t log_flags(const AnimLogRecord* r);       // 0x0050d200
bool log_has(const AnimLogRecord* r);             // 0x0050b680
// The network's hook and the serials' top byte (the network game's 0xc).
void log_set_net(void (*fn)(AnimLogRecord* rec), uint32_t tag);  // 0x0050d1d0

// The log's accessors and its replay, as the savegame uses them.
void log_clear();                  // 0x0050b640: the count to 0
int32_t log_count();               // 0x0050b6f0
AnimLogRecord* log_record(int32_t i);  // 0x0050b700
void log_reset_def(const AnimLogRecord* rec);  // 0x0050ca60: the def reset at the record's node
AnimDef* log_replay(const AnimLogRecord* rec);  // 0x0050ca90: the call made again

// zSave's three anim registrants (docs/save.md section 3), registered by
// zEffect's start (0x00508fc0) when [0x005ebcc0].
void activation_save(zsave::Context& ctx, void* user);  // 0x0050b710 "AnimActivation"
void activation_load(zsave::Context& ctx, const char* sub, const void* data, uint32_t size, void* user);  // 0x0050b970
void running_save(zsave::Context& ctx, void* user);     // 0x0050c190 "RunningAnim"
void running_write(zsave::Context& ctx, AnimDef* def, int32_t index, int32_t main);  // 0x0050be30
void running_load(zsave::Context& ctx, const char* sub, const void* data, uint32_t size, void* user);  // 0x0050c250
void anim_save(zsave::Context& ctx, void* user);        // 0x0050c640 "Anim"
void anim_load(zsave::Context& ctx, const char* sub, const void* data, uint32_t size, void* user);  // 0x0050c890

int32_t node_index(const zcls::Node* node);       // 0x004fe100, -1 for null or not in the pool
int32_t live_node_index(const zcls::Node* node);  // 0x004fe180, -1 unless live

}  // namespace zeff
