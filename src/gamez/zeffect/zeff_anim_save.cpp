#include "gamez/zeffect/zeff_anim_save.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zclass/sound.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zmodel/gmod_const.h"
#include "gamez/zsave/zsave.h"

namespace zeff {

namespace {
AnimLog g_log;

uint32_t bits(float f) {
  uint32_t u;
  std::memcpy(&u, &f, 4);
  return u;
}

// Whether a call is kept (savegame) and sent (network): a def's own setting
// (flags 0x1000 / 0x400, with 0x2000 / 0x800 the value) wins over the
// defaults; otherwise a def added to the world when started (0x100), a
// nameless one, or one at a node outside the pool is never logged.
struct Want {
  uint32_t save, net;
};
Want want(const AnimDef* def, bool node_ok, bool check_defaults) {
  const uint32_t f = def->get<uint32_t>(0x94);
  const bool base = (f & 0x100) == 0 && def->raw[0] != 0 && node_ok;
  Want w;
  w.save = (f & 0x1000) ? (f >> 13) & 1 : (base && (!check_defaults || g_log.save_on)) ? 1 : 0;
  w.net = (f & 0x400) ? (f >> 11) & 1 : (base && (!check_defaults || g_log.net_on)) ? 1 : 0;
  return w;
}

AnimLogRecord* begin(const AnimDef* def, int32_t type, int32_t node) {
  AnimLogRecord* r = log_new_record();
  r->type = type;
  std::strncpy(r->name, def->name(), 0x20);
  r->node = node;
  return r;
}

AnimLogRecord* finish(AnimLogRecord* r, Want w) {
  AnimLogRecord* sent = nullptr;
  if (w.net) {
    sent = r;
    if (g_log.net) g_log.net(r);
  }
  if (!w.save) log_drop_record();
  return sent;
}

void put_vec(uint32_t* out, const float* v) {
  for (int i = 0; i < 3; ++i) out[i] = v ? bits(v[i]) : 0;
}
}  // namespace

AnimLog& anim_log() { return g_log; }

// Mech3.exe 1.2 @ 0x004fe100
int32_t node_index(const zcls::Node* node) {
  if (!node) return -1;
  const zcls::NodePool& p = zcls::nodes();
  for (size_t b = 0; b < p.blocks.size(); ++b) {
    const zcls::Node* base = p.blocks[b].get();
    if (node >= base && node <= base + p.block_size - 1)
      return static_cast<int32_t>(b) * p.block_size + static_cast<int32_t>(node - base);
  }
  return -1;  // the exe returns a stray value here; every caller passes pool nodes
}

// Mech3.exe 1.2 @ 0x004fe180
int32_t live_node_index(const zcls::Node* node) {
  const int32_t i = node_index(node);
  if (i >= 0 && (zcls::node_at(i)->link & zcls::kNodeLive)) return i;
  return -1;
}

// Mech3.exe 1.2 @ 0x0050bd50. 1000 records at first, doubled when full.
AnimLogRecord* log_new_record() {
  AnimLog& l = g_log;
  if (l.records.empty()) {
    l.records.resize(1000);
    l.capacity = 1000;
    l.count = 0;
  }
  if (l.capacity <= l.count) {
    l.records.resize(static_cast<size_t>(l.capacity) * 2);
    l.capacity *= 2;
  }
  AnimLogRecord& r = l.records[l.count];
  r.serial = l.mask & static_cast<uint32_t>(l.count) & 0xffffff;
  ++l.count;
  return &r;
}

// Mech3.exe 1.2 @ 0x0050ccd0
void log_drop_record() { --g_log.count; }

// Mech3.exe 1.2 @ 0x0050cbb0
AnimLogRecord* log_start_at(AnimDef* def, zcls::Node* node, const float pos[3], const float rot[3],
                            const float vec[3]) {
  const int32_t n = live_node_index(node);
  const Want w = want(def, !node || n >= 1, true);
  if (!w.save && !w.net) return nullptr;
  AnimLogRecord* r = begin(def, 1, n);
  put_vec(r->args, pos), put_vec(r->args + 3, rot), put_vec(r->args + 6, vec);
  return finish(r, w);
}

// Mech3.exe 1.2 @ 0x0050cce0
AnimLogRecord* log_start(AnimDef* def, zcls::Node* node, float x, float y, float z) {
  const int32_t n = live_node_index(node);
  const Want w = want(def, !node || n >= 1, true);
  if (!w.save && !w.net) return nullptr;
  AnimLogRecord* r = begin(def, 2, n);
  r->args[0] = bits(x), r->args[1] = bits(y), r->args[2] = bits(z);
  return finish(r, w);
}

// Mech3.exe 1.2 @ 0x0050cde0. The only logger that ignores the defaults.
AnimLogRecord* log_connector(AnimDef* def, zcls::Node* node, zcls::Node* from, const float* from_pos,
                             const float* vec) {
  const int32_t n = live_node_index(node), m = live_node_index(from);
  const Want w = want(def, (!node || n >= 1) && (!from || m >= 1), false);
  if (!w.save && !w.net) return nullptr;
  AnimLogRecord* r = begin(def, 3, n);
  r->args[0] = static_cast<uint32_t>(m);
  put_vec(r->args + 1, from_pos), put_vec(r->args + 4, vec);
  return finish(r, w);
}

// Mech3.exe 1.2 @ 0x0050cf40
AnimLogRecord* log_connector_at(AnimDef* def, zcls::Node* node, zcls::Node* from, const float* from_pos,
                                zcls::Node* to, const float* to_pos) {
  const int32_t n = live_node_index(node), m = live_node_index(from), k = live_node_index(to);
  const Want w = want(def, (!node || n >= 1) && (!from || m >= 1) && (!to || k >= 1), true);
  if (!w.save && !w.net) return nullptr;
  AnimLogRecord* r = begin(def, 4, n);
  r->args[0] = static_cast<uint32_t>(m);
  put_vec(r->args + 1, from_pos);
  r->args[4] = static_cast<uint32_t>(k);
  put_vec(r->args + 5, to_pos);
  return finish(r, w);
}

// Mech3.exe 1.2 @ 0x0050d0f0
AnimLogRecord* log_reset(AnimDef* def, zcls::Node* node) {
  const int32_t n = live_node_index(node);
  const Want w = want(def, !node || n >= 1, true);
  if (!w.save && !w.net) return nullptr;
  return finish(begin(def, 5, n), w);
}

// ---------------------------------------------------------------------------
// The savegame's registrants (docs/save.md section 3). Their records are
// built on the stack in the exe; bytes a record does not set keep what the
// last one left there (zeros at the call's start here: the exe's are stack
// garbage, never read back).

static_assert(sizeof(AnimLogRecord) == 0x50);

namespace {
const char kSaveFile[] = "C:\\zipper\\GameZMech3\\zEffect\\zeff_anim_save.c";

uint32_t flags(const AnimDef* d) { return d->get<uint32_t>(0x94); }
void set_flags(AnimDef* d, uint32_t f) { d->put<uint32_t>(0x94, f); }
uint8_t& status(AnimDef* d) { return d->raw[0x98]; }
AnimDef* def_at(int32_t i) { return &anim().defs[i]; }

template <typename T> T rd(const uint8_t* p, int off) {
  T v;
  std::memcpy(&v, p + off, sizeof v);
  return v;
}
template <typename T> void wr(uint8_t* p, int off, T v) { std::memcpy(p + off, &v, sizeof v); }

// A def's own setting (0x1000, with 0x2000 the value) can keep it out of the save.
bool saved(const AnimDef* d) {
  const uint32_t f = flags(d);
  return !((f & 0x1000) && !(f & 0x2000)) && g_log.save_on;
}

// An object's node state, 0x44 bytes: its live index (-1: none, the rest
// left as it was), whether active, whether it keeps a matrix, then the matrix
// or the translation, rotation and scale; with `model` (the activations') its
// model's flag 8 in bit 0 of +0x3c and the model's +0x24 at +0x40.
void save_node(uint8_t* p, zcls::Node* n, bool model) {
  if (!n || n->cls() != 5) {
    wr<int32_t>(p, 0, -1);
    return;
  }
  wr<int32_t>(p, 0, live_node_index(n));
  wr<uint32_t>(p, 4, (n->flags() >> 2) & 1);
  const uint32_t matrix = (static_cast<zcls::Object3d*>(n->data)->get<uint32_t>(0) >> 4) & 1;
  wr<uint32_t>(p, 8, matrix);
  auto* f = reinterpret_cast<float*>(p + 0xc);
  if (matrix) {
    std::memcpy(p + 0xc, zcls::object3d_matrix(n), 0x30);
  } else {
    float v[9];
    zcls::object3d_get_translation(n, &v[0], &v[1], &v[2]);
    zcls::object3d_get_rotation(n, &v[3], &v[4], &v[5]);
    zcls::object3d_get_scale(n, &v[6], &v[7], &v[8]);
    std::memcpy(f, v, sizeof v);
  }
  if (!model) return;
  uint32_t bit = rd<uint32_t>(p, 0x3c) & ~1u;
  if (n->model) {
    bit |= (rd<uint32_t>(n->model->header, 8) >> 3) & 1;
    wr<uint32_t>(p, 0x40, rd<uint32_t>(n->model->header, 0x24));
  }
  wr<uint32_t>(p, 0x3c, bit);
}

// FIX (bug): a node index read from a saved game, as 0x004fe1d0 takes it,
// but one past the pool (a damaged save) is no node instead of memory past it;
// FIX (A): a peer's effect record (0x1a) names them too (fixes.md). Below 0
// none, as 0x004fe1d0 answers any negative one.
zcls::Node* saved_node(int32_t i) {
  const zcls::NodePool& p = zcls::nodes();
  if (i < 0 || i >= static_cast<int32_t>(p.blocks.size()) * p.block_size) return nullptr;
  return zcls::node_at(i);
}

void load_node(const uint8_t* p, bool model) {
  zcls::Node* n = saved_node(rd<int32_t>(p, 0));
  if (!n || n->cls() != 5) return;
  const int32_t active = rd<int32_t>(p, 4);
  zerr::reportf(0x100, kSaveFile, model ? 0x208 : 0x41f, "Restore node: %s %d", n->name(), active);
  zcls::node_set_active(n, active != 0);
  if (active) {
    if (rd<int32_t>(p, 8)) {
      zcls::object3d_set_matrix(n, reinterpret_cast<const float*>(p + 0xc));
    } else {
      zcls::object3d_set_translation(n, rd<float>(p, 0xc), rd<float>(p, 0x10), rd<float>(p, 0x14));
      zcls::object3d_set_rotation(n, rd<float>(p, 0x18), rd<float>(p, 0x1c), rd<float>(p, 0x20));
      zcls::object3d_set_scale(n, rd<float>(p, 0x24), rd<float>(p, 0x28), rd<float>(p, 0x2c));
    }
  }
  // The model's flag is only ever set back, never cleared.
  if (model && n->model && (rd<uint32_t>(p, 0x3c) & 1)) {
    wr<uint32_t>(n->model->header, 8, rd<uint32_t>(n->model->header, 8) | 8);
    wr<uint32_t>(n->model->header, 0x24, rd<uint32_t>(p, 0x40));
  }
}

// The objects' states after a 0x58-byte head (+0x54 the status, +0x55 the count).
int32_t states_size(const uint8_t* b) { return 0x58 + 0x44 * b[0x55]; }
void save_objects(uint8_t* b, AnimDef* d, bool model) {
  b[0x55] = d->count(0x109);
  for (int k = 0; k < d->count(0x109); ++k) save_node(b + 0x58 + 0x44 * k, d->objects[k].node, model);
}
// FIX (bug): an entry shorter than its count says (a damaged save) had the exe
// read past it; only the states it holds are restored.
void load_objects(const uint8_t* b, uint32_t size, bool model) {
  for (int k = 0; k < b[0x55] && 0x58u + 0x44u * (k + 1) <= size; ++k) load_node(b + 0x58 + 0x44 * k, model);
}
}  // namespace

// Mech3.exe 1.2 @ 0x0050ca20: a record's bytes as sent, by its type.
int32_t log_record_size(const AnimLogRecord* r) {
  switch (r->type) {
    case 2: return 0x38;
    case 3: return 0x48;
    case 4: return 0x4c;
    default: return 0x50;
  }
}

// Mech3.exe 1.2 @ 0x0050d200 (ECX the name, 0x0050d260): bit 0 kept for the
// save, bit 1 sent; the def's own settings win over the defaults; 0 for a
// name no def has.
uint32_t log_flags(const AnimLogRecord* r) {
  const AnimDef* d = anim_find(r->name);  // 0x0050b450
  if (!d) return 0;
  const uint32_t f = d->get<uint32_t>(0x94);
  const uint32_t save = (f & 0x1000) ? (f >> 13 & 1) : static_cast<uint32_t>(g_log.save_on);
  const uint32_t net = (f & 0x400) ? (f >> 11 & 1) : static_cast<uint32_t>(g_log.net_on);
  return (save != 0 ? 1u : 0u) | (net != 0 ? 2u : 0u);
}

// Mech3.exe 1.2 @ 0x0050b680: a record with the same node and name logged.
bool log_has(const AnimLogRecord* r) {
  for (int32_t i = 0; i < g_log.count; ++i) {
    const AnimLogRecord& o = g_log.records[static_cast<size_t>(i)];
    if (o.node == r->node && std::strncmp(o.name, r->name, 0x20) == 0) return true;
  }
  return false;
}

// Mech3.exe 1.2 @ 0x0050d1d0 (ECX fn, EDX tag)
void log_set_net(void (*fn)(AnimLogRecord* rec), uint32_t tag) {
  g_log.net = fn;
  g_log.mask = tag << 24;
}
// Mech3.exe 1.2 @ 0x0050b640
void log_clear() { g_log.count = 0; }
// Mech3.exe 1.2 @ 0x0050b6f0
int32_t log_count() { return g_log.count; }
// Mech3.exe 1.2 @ 0x0050b700
AnimLogRecord* log_record(int32_t i) { return &g_log.records[i]; }

// Mech3.exe 1.2 @ 0x0050ca60
void log_reset_def(const AnimLogRecord* rec) {
  anim_reset(anim_find(rec->name), saved_node(rec->node), 1);
}

// Mech3.exe 1.2 @ 0x0050ca90. The def started again as logged (which logs it
// again); a reset answers null like an unknown type.
AnimDef* log_replay(const AnimLogRecord* rec) {
  AnimDef* d = anim_find(rec->name);
  if (!d) return nullptr;
  const auto* f = reinterpret_cast<const float*>(rec->args);
  zcls::Node* node = saved_node(rec->node);
  switch (rec->type) {
    case 1: return anim_start_at(d, node, f, f + 3, f + 6);  // 0x00508aa0
    case 2: return anim_start(d, node, f[0], f[1], f[2]);    // 0x00508c10
    case 3:                                                  // 0x00508da0
      return anim_start_connector(d, node, saved_node(static_cast<int32_t>(rec->args[0])), f + 1, f + 4);
    case 4:                                                  // 0x00508ee0
      return anim_start_connector_at(d, node, saved_node(static_cast<int32_t>(rec->args[0])), f + 1,
                                     saved_node(static_cast<int32_t>(rec->args[4])), f + 5);
    case 5: anim_restart(d, node); return nullptr;           // 0x00508460
    default: return nullptr;
  }
}

// Mech3.exe 1.2 @ 0x0050b710. Each logged call as "Activation%04d": the
// record, the def's status, and (unless a reset) its objects' states. The def
// is bound to the record's node first.
void activation_save(zsave::Context& ctx, void* /*user*/) {
  std::vector<uint8_t> buf(0x4484);
  uint8_t* b = buf.data();
  for (int32_t i = 0; i < g_log.count; ++i) {
    const AnimLogRecord& rec = g_log.records[i];
    AnimDef* d = anim_find(rec.name);
    zerr::reportf(0x100, kSaveFile, 0x120, "_AnimActivationSaver: saving %s", rec.name);
    std::memcpy(b, &rec, 0x50);
    if (!d) {
      b[0x54] = 0, b[0x55] = 0;
    } else {
      zcls::Node* node = zcls::node_at(rec.node);
      b[0x54] = status(d);
      anim_bind(d, node);
      if (d->objects && rec.type != 5) save_objects(b, d, true);
      else b[0x55] = 0;
    }
    char sub[0x14];
    std::snprintf(sub, sizeof sub, "Activation%04d", i);
    zsave::write(ctx, sub, b, static_cast<uint32_t>(states_size(b)));
  }
}

// Mech3.exe 1.2 @ 0x0050b970. The first entry resets every def the new
// mission has logged and empties the log; each then makes its call again
// (the def marked restored, 0x4000) and brings the def to the saved status.
void activation_load(zsave::Context& /*ctx*/, const char* sub, const void* data, uint32_t size, void* /*user*/) {
  const auto* b = static_cast<const uint8_t*>(data);
  // FIX (bug): an entry shorter than its head (a damaged save) was read past.
  if (size < 0x58) return;
  const auto& rec = *reinterpret_cast<const AnimLogRecord*>(b);
  AnimDef* d = anim_find(rec.name);
  if (std::strcmp(sub, "Activation0000") == 0) {
    for (int32_t i = 0; i < log_count(); ++i) {
      AnimLogRecord* r = log_record(i);
      log_reset_def(r);
      zerr::reportf(0x100, kSaveFile, 0x1a2, "zEffResetActivationRecord: %s", r->name);
    }
    log_clear();
    for (int32_t j = 1; j < anim().def_count(); ++j)
      for (AnimDef* c = def_at(j); c; c = c->instance) set_flags(c, flags(c) & ~0x4000u);
  }
  if (rec.type == 5) {
    log_replay(&rec);
    return;
  }
  if (!d) return;
  if (rec.node >= 0) {
    zcls::Node* n = saved_node(rec.node);
    while (d->instance && d->node != n) d = d->instance;
    anim_reset(d, n, 1);
    d = log_replay(&rec);
    if (!d) return;
  }
  set_flags(d, flags(d) | 0x4000);
  const uint8_t want = b[0x54];
  if (want == 2 && status(d) != 2) {
    anim_reset(d, saved_node(rec.node), 1);
    d = log_replay(&rec);
    zerr::report(0x100, kSaveFile, 0x1d3, "zEffProcessActivationRecord");
  }
  if (want == 3 && status(d) != 3) {
    anim_stop(d);
    status(d) = 3;
    *log_new_record() = rec;
    zerr::report(0x100, kSaveFile, 0x1df, "Set anim_state = ANIM_STATE_EXECUTED");
  }
  if (want == 1 && status(d) != 1) {
    zcls::Node* n = saved_node(rec.node);
    if (status(d) == 4) status(d) = 3;
    anim_reset(d, n, 1);
    zerr::report(0x100, kSaveFile, 0x1e9, "_zEffAnimReset");
    *log_new_record() = rec;
  }
  if (want == 6 && status(d) != 6) {
    zcls::Node* n = saved_node(rec.node);
    if (status(d) == 4) {
      status(d) = 3;
      anim_reset(d, n, 1);
      d = log_replay(&rec);
      zerr::report(0x100, kSaveFile, 0x1f7, "zEffProcessActivationRecord");
    }
  }
  if (want == 4 && status(d) != 4) {
    anim_stop(d);
    status(d) = 4;
    zerr::report(0x100, kSaveFile, 0x1ff, "Set anim_state = ANIM_STATE_INVALID");
    *log_new_record() = rec;
  }
  load_objects(b, size, true);
}

// Mech3.exe 1.2 @ 0x0050c190. Each def running (2) or resting (6), and its
// running instances, unless its setting keeps it out; an instance is saved
// only with its def.
void running_save(zsave::Context& ctx, void* /*user*/) {
  for (int32_t j = 1; j < anim().def_count(); ++j) {
    AnimDef* d = def_at(j);
    if (status(d) != 2 && status(d) != 6) continue;
    if (!saved(d)) continue;
    zerr::reportf(0x100, kSaveFile, 0x2ed, "_RunningAnimSaver: saving %s", d->name());
    running_write(ctx, d, j, 1);
    for (AnimDef* c = d->instance; c; c = c->instance)
      if (status(c) == 2) running_write(ctx, c, j, 0);
  }
}

// Mech3.exe 1.2 @ 0x0050be30 (ECX ctx, EDX def; index, main). One run as
// "Running%03d" (the def's index: a def and its instances share the name),
// made in a temporary file: a 0x68-byte head, the sequences (0x40 each, the
// current event as an offset, then the events), the lights (0x38) and the
// sounds (0x3c) with their positions and parents.
void running_write(zsave::Context& ctx, AnimDef* d, int32_t index, int32_t main) {
  uint8_t h[0x68] = {}, r[0x40] = {};
  wr<int32_t>(h, 0, index);
  wr<int32_t>(h, 4, main);
  std::strncpy(reinterpret_cast<char*>(h + 8), d->name(), 0x20);
  wr<int32_t>(h, 0x28, live_node_index(d->node));
  wr<int32_t>(h, 0x2c, live_node_index(d->at_node));
  std::memcpy(h + 0x30, d->raw + 0x78, 0xc);
  wr<int32_t>(h, 0x3c, live_node_index(d->at_node2));
  std::memcpy(h + 0x40, d->raw + 0x88, 0xc);
  h[0x4c] = status(d);
  std::memcpy(h + 0x50, d->raw + 0xa8, 4);
  std::memcpy(h + 0x54, d->raw + 0xb0, 4);
  std::memcpy(h + 0x58, d->raw + 0xb8, 0xc);
  h[0x64] = d->count(0x108), h[0x65] = d->count(0x10b), h[0x66] = d->count(0x10d);
  char sub[0x14];
  std::snprintf(sub, sizeof sub, "Running%03d", index);
  std::FILE* f = zsave::temp();
  if (!f) return;
  std::fwrite(h, 0x68, 1, f);
  for (int k = 0; k < d->count(0x108); ++k) {
    const SeqDef& s = d->seqs[k];
    const int32_t size = s.get<int32_t>(0x3c);
    std::memcpy(r, s.raw, 0x40);
    // The exe writes cur - events and the events' address; an event outside
    // the sequence (one never started) is -1 here, the address 0.
    const bool inside = s.events && s.cur >= s.events && s.cur <= s.events + (size > 0 ? size : 0);
    wr<int32_t>(r, 0x34, inside ? static_cast<int32_t>(s.cur - s.events) : -1);
    wr<uint32_t>(r, 0x38, 0);
    std::fwrite(r, 0x40, 1, f);
    if (size > 0) std::fwrite(s.events, static_cast<size_t>(size), 1, f);
  }
  for (int k = 0; k < d->count(0x10b); ++k) {
    const CreatedRef& e = d->lights[k];
    std::strncpy(reinterpret_cast<char*>(r), reinterpret_cast<const char*>(e.raw), 0x24);
    wr<int32_t>(r, 0x24, e.get<int32_t>(0x28));
    if (auto* n = static_cast<zcls::Node*>(e.made)) {
      float v[3];
      zcls::sound_get_translation(n, &v[0], &v[1], &v[2]);  // 0x004fd620 on a light: its +0x30
      std::memcpy(r + 0x28, v, 0xc);
      wr<int32_t>(r, 0x34, n->parent_count() > 0 ? live_node_index(n->parents[0]) : -1);
    }
    std::fwrite(r, 0x38, 1, f);
  }
  for (int k = 0; k < d->count(0x10d); ++k) {
    const CreatedRef& e = d->sounds[k];
    std::strncpy(reinterpret_cast<char*>(r), reinterpret_cast<const char*>(e.raw), 0x24);
    wr<int32_t>(r, 0x24, e.get<int32_t>(0x28));
    if (auto* n = static_cast<zcls::Node*>(e.made)) {
      wr<uint32_t>(r, 0x28, (static_cast<zcls::Sound*>(n->data)->get<uint32_t>(0x2c) >> 1) & 1);
      float v[3];
      zcls::sound_get_translation(n, &v[0], &v[1], &v[2]);
      std::memcpy(r + 0x2c, v, 0xc);
      wr<int32_t>(r, 0x38, n->parent_count() > 0 ? live_node_index(n->parents[0]) : -1);
    }
    std::fwrite(r, 0x3c, 1, f);
  }
  zsave::write_temp(f, ctx, sub);
}

// Mech3.exe 1.2 @ 0x0050c250. The run put back on its def: for a def's own
// record the instance at the saved node (with none, a running def gets a new
// instance that is left idle: the state goes on the def itself), for an
// instance's record the def itself. Its lights and sounds made then but not
// here are made where they were, and its object ticks it again.
void running_load(zsave::Context& /*ctx*/, const char* /*sub*/, const void* data, uint32_t size, void* /*user*/) {
  std::FILE* f = zsave::temp_from(data, size);
  if (!f) return;
  uint8_t h[0x68] = {}, r[0x40] = {};
  std::fread(h, 0x68, 1, f);
  const int32_t index = rd<int32_t>(h, 0);
  // FIX (bug): an index outside the defs (a damaged save) was used as is.
  if (index < 1 || index >= anim().def_count()) {
    zsave::temp_done();
    return;
  }
  AnimDef* d = def_at(index);
  zcls::Node* n = saved_node(rd<int32_t>(h, 0x28));
  if (rd<int32_t>(h, 4)) {
    while (d->instance && d->node != n) d = d->instance;
    if (d->node != n) {
      while (d->instance && status(d) == 2) d = d->instance;
      if (status(d) == 2) d->instance = anim_instance(d, n);
    }
  }
  set_flags(d, flags(d) | 0x4000);
  d->at_node = saved_node(rd<int32_t>(h, 0x2c));
  std::memcpy(d->raw + 0x78, h + 0x30, 0xc);
  d->at_node2 = saved_node(rd<int32_t>(h, 0x3c));
  std::memcpy(d->raw + 0x88, h + 0x40, 0xc);
  status(d) = h[0x4c];
  std::memcpy(d->raw + 0xa8, h + 0x50, 4);
  std::memcpy(d->raw + 0xb0, h + 0x54, 4);
  std::memcpy(d->raw + 0xb8, h + 0x58, 0xc);
  // FIX (bug): more sequences than the def has (a damaged save) overran them.
  const uint8_t seqs = d->count(0x108);
  d->raw[0x108] = h[0x64] < seqs ? h[0x64] : seqs;
  for (int k = 0; k < d->count(0x108); ++k) {
    SeqDef& s = d->seqs[k];
    delete[] s.events;
    s.events = nullptr;
    s.cur = nullptr;
    std::fread(s.raw, 0x40, 1, f);
    const int32_t size = s.get<int32_t>(0x3c);
    if (size > 0) {
      s.events = new uint8_t[static_cast<size_t>(size)]();
      const int32_t at = s.get<int32_t>(0x34);
      if (at >= 0 && at <= size) s.cur = s.events + at;
      std::fread(s.events, static_cast<size_t>(size), 1, f);
    }
  }
  for (int k = 0; k < h[0x65]; ++k) {
    std::fread(r, 0x38, 1, f);
    if (rd<int32_t>(r, 0x24) == 0) continue;  // not made when saved
    const int32_t parent = rd<int32_t>(r, 0x34);
    char name[0x25] = {};
    std::memcpy(name, r, 0x24);
    const int32_t i = anim_light_ref(*d, name);
    if (i < 0) continue;
    CreatedRef& e = d->lights[i];
    auto* light = static_cast<zcls::Node*>(e.made);
    if (e.get<int32_t>(0x28) != 0 || !light) continue;
    zcls::node_set_active(light, 1);
    if (parent >= 0 && light->parent_count() == 0)
      if (zcls::Node* at = saved_node(parent)) zcls::node_add_child(at, light);
    zcls::light_set_position(light, rd<float>(r, 0x28), rd<float>(r, 0x2c), rd<float>(r, 0x30));
    zcls::world_add_light(static_cast<zcls::Node*>(anim().world), light);
    e.put<int32_t>(0x28, 1);
  }
  for (int k = 0; k < h[0x66]; ++k) {
    std::fread(r, 0x3c, 1, f);
    if (rd<int32_t>(r, 0x24) == 0) continue;  // not made when saved
    const int32_t parent = rd<int32_t>(r, 0x38);
    char name[0x25] = {};
    std::memcpy(name, r, 0x24);
    const int32_t i = anim_sound_ref(*d, name);
    if (i < 0) continue;
    CreatedRef& e = d->sounds[i];
    auto* sound = static_cast<zcls::Node*>(e.made);
    if (e.get<int32_t>(0x28) != 0 || !sound) continue;
    zcls::node_set_active(sound, 1);
    if (parent >= 0 && sound->parent_count() == 0)
      if (zcls::Node* at = saved_node(parent)) zcls::node_add_child(at, sound);
    if (rd<int32_t>(r, 0x28)) zcls::sound_set_translation(sound, rd<float>(r, 0x2c), rd<float>(r, 0x30), rd<float>(r, 0x34));
    zcls::world_add_sound(static_cast<zcls::Node*>(anim().world), sound);
    e.put<int32_t>(0x28, 1);
  }
  d->object->owner = d;
  zcls::node_set_action_back(d->object, anim_tick);
  zsave::temp_done();
}

// Mech3.exe 1.2 @ 0x0050c640. Each def not in error (5), unless its setting
// keeps it out, as "Anim%04d" numbered after the activations: its index, its
// status and its objects' states.
void anim_save(zsave::Context& ctx, void* /*user*/) {
  std::vector<uint8_t> buf(0x4480);
  uint8_t* b = buf.data();
  for (int32_t j = 1; j < anim().def_count(); ++j) {
    AnimDef* d = def_at(j);
    wr<int32_t>(b, 0, 0);
    if (status(d) == 5 || !saved(d)) continue;
    zerr::reportf(0x100, kSaveFile, 0x3bc, "_AnimSaver: saving %s", d->name());
    wr<int32_t>(b, 0x50, j);
    std::strncpy(reinterpret_cast<char*>(b + 8), d->name(), 0x20);
    if (d->objects) {
      b[0x54] = status(d);
      save_objects(b, d, false);
    } else {
      b[0x55] = 0;  // +0x54 keeps the last def's
    }
    char sub[0x14];
    std::snprintf(sub, sizeof sub, "Anim%04d", j + g_log.count);
    zsave::write(ctx, sub, b, static_cast<uint32_t>(states_size(b)));
  }
}

// Mech3.exe 1.2 @ 0x0050c890. A def the activations did not restore: reset
// if it was reset (1), its running copies reset, its objects as they were.
void anim_load(zsave::Context& /*ctx*/, const char* /*sub*/, const void* data, uint32_t size, void* /*user*/) {
  const auto* b = static_cast<const uint8_t*>(data);
  // FIX (bug): a short entry or an index outside the defs (a damaged save)
  // was read past or used as is.
  if (size < 0x58) return;
  const int32_t index = rd<int32_t>(b, 0x50);
  if (index < 1 || index >= anim().def_count()) return;
  AnimDef* d = def_at(index);
  if (flags(d) & 0x4000) return;
  if (b[0x54] == 1 && status(d) != 1) {
    if (status(d) == 4) status(d) = 3;
    anim_reset(d, d->node, 1);
    zerr::reportf(0x100, kSaveFile, 0x412, "_zEffAnimReset: %s", d->name());
  }
  for (AnimDef* c = d; c; c = c->instance) {
    if ((flags(c) & 0x4000) || status(c) != 2) continue;
    anim_reset(c, c->node, 1);
    zerr::reportf(0x100, kSaveFile, 0x418, "_zEffAnimReset: %s", c->name());
  }
  load_objects(b, size, false);
}

}  // namespace zeff
