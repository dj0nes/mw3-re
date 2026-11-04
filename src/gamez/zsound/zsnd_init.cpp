// C:\zipper\branchep1\gamez\zSound\zsnd_init.cpp: start-up, sounds.zrd and the
// DirectSound device (docs/sound.md section 1). The sounds.zrd reader is
// Pirate's Moon's, rewritten smaller (docs/sound.md, Pirate's Moon).
#include <cstdio>
#include <cstring>
#include <vector>

#include "gamez/zerror/zerr.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/zfix.h"

namespace zsnd {

namespace {
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zSound\\zsnd_init.cpp";
State g_state;
Hooks g_hooks;
platform::SoundOutput* out() { return g_hooks.output; }

uint32_t raw(const zrdr::Node& n) { return static_cast<uint32_t>(n.i); }
bool has(zrdr::Node* n, const char* key) { return zrdr::find(n, key) != nullptr; }

// FIX (bug), as MW3's reader: the exe asks for "MED", which never matches the data's "MEDIUM".
const char* med() { return zfix::options().bugs ? "MEDIUM" : "MED"; }

// Pirate's Moon 1.0 @ 0x00592060: a format entry, a file name or a list of
// rate, bits and channels (at least three items); false leaves `f` as it was.
bool read_spec(Format* f, zrdr::Node* n) {
  if (!n) return false;
  if (n->type == zrdr::String) {
    *f = Format{};
    f->file = n->s, f->has_file = true;
    return true;
  }
  if (n->type == zrdr::List && n->list[0].i > 3) {
    f->file.clear(), f->has_file = false;
    f->rate = static_cast<int32_t>(raw(n->list[1]));
    f->bits = static_cast<int32_t>(raw(n->list[2]));
    f->channels = static_cast<int32_t>(raw(n->list[3]));
    return true;
  }
  return false;
}

void read_specs(Format* out, zrdr::Node* parent) {
  read_spec(&out[0], zrdr::find(parent, "HIGH"));
  read_spec(&out[1], zrdr::find(parent, med()));
  read_spec(&out[2], zrdr::find(parent, "LOW"));
}

// Pirate's Moon 1.0 @ 0x005917c0 (ECX the list): a set INCLUDE_SETS names
// (strcmp), or any set when it names none.
bool included(const std::vector<const char*>& include, const char* name) {
  if (!name) return false;
  if (include.empty()) return true;
  for (const char* i : include)
    if (i && std::strcmp(i, name) == 0) return true;
  return false;
}
}  // namespace

State& state() { return g_state; }
Hooks& hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x005700f0 (ECX window). Also the CD state's start
// (0x0072c350 = 2, zsnd_cd) and the bindings to the engine variables.
int32_t init(uint32_t window) {
  State& st = g_state;
  if (st.initialized) return 0;
  st.initialized = 1;
  st.open = 0;
  if (st.api == 0) {
    st.primary = 0;
    st.has_primary_format = false;
    for (bool& b : st.has_playback) b = false;
    st.playback_current = -1;
  }
  st.queue_clock = 0.0f;
  zsnd_cd::state().flags &= ~3u;
  st.queue_next = 0.0f;
  st.has_search = false, st.search.clear();
  zsnd_cd::state().mode = 2;
  st.window = window;
  st.lod_default = 0;
  st.lod = g_hooks.find_var ? static_cast<int32_t*>(g_hooks.find_var("SoundLOD")) : nullptr;
  if (!st.lod) st.lod = &st.lod_default;
  st.mute_default = 0;
  st.mute = g_hooks.find_var ? static_cast<int32_t*>(g_hooks.find_var("MuteSound")) : nullptr;
  if (!st.mute) st.mute = &st.mute_default;
  st.muted = *st.mute;
  st.cue_sound = nullptr, st.cue_voice = nullptr, st.cue_index = 0, st.cue_limit = 999;
  st.volume_default = 1.0f;
  st.volume = g_hooks.find_var ? static_cast<float*>(g_hooks.find_var("SoundVolume")) : nullptr;
  // null: the State's own volume_default (master())
  st.voices_on = 1;
  st.queue = new std::list<Queued*>;
  st.queued = nullptr;
  st.subtitle_hook = nullptr;
  st.can_play_hook = nullptr;
  return 1;
}

// Mech3.exe 1.2 @ 0x00570280
int32_t quality(int32_t lod) { return lod == 1 ? 1 : lod == 2 ? 2 : 0; }

// Mech3.exe 1.2 @ 0x005702a0 (ECX the quality, null for SoundLOD's). The
// primary buffer's format: the PLAYBACK_FORMAT entry, else mono 22050 Hz at
// 16 bits if the card has them. Returns the last SetCooperativeLevel's result.
int32_t set_primary_format(const int32_t* q) {
  State& st = g_state;
  const int32_t quality_ = q ? *q : quality(*st.lod);
  if (st.api != 0 || !st.has_primary_format) return 0;
  if (quality_ >= 0 && quality_ <= 2) st.playback_current = st.has_playback[quality_] ? quality_ : -1;
  platform::WaveFormat& f = st.primary_format;
  if (st.playback_current >= 0) {
    const Format& p = st.playback[st.playback_current];
    f.channels = static_cast<uint16_t>(p.channels);
    f.bits_per_sample = static_cast<uint16_t>(p.bits);
    f.samples_per_sec = static_cast<uint32_t>(p.rate);
  } else {
    f.channels = 1;
    f.bits_per_sample = (st.caps & platform::SoundOutput::kCapsPrimary16Bit) ? 16 : 8;
    f.samples_per_sec = 22050;
  }
  f.block_align = static_cast<uint16_t>((f.bits_per_sample >> 3) * f.channels);
  f.avg_bytes_per_sec = static_cast<uint32_t>(f.block_align) * f.samples_per_sec;
  int32_t hr = out()->set_cooperative_level(st.window, platform::SoundOutput::kExclusive);
  if (hr != 0) return ds_error(hr, kFile, 0xf0);
  hr = out()->set_format(st.primary, f);
  if (hr != 0) return ds_error(hr, kFile, 0xf3);
  return out()->set_cooperative_level(st.window, platform::SoundOutput::kNormal);
}

// Mech3.exe 1.2 @ 0x00570410 (ECX: stop the line playing). Empties the voice
// queue.
void stop_queue(int32_t stop_playing) {
  State& st = g_state;
  if (stop_playing && st.queued && status(st.queued) != 0) {
    stop(st.queued);
    st.queue_next = st.queue_clock - -0.5f;
  }
  st.queued = nullptr;
  if (st.queue) {
    for (Queued* q : *st.queue) delete q;
    st.queue->clear();
  }
}

// Mech3.exe 1.2 @ 0x00570490: forgets the queued lines of one sound.
void drop_queued(Sound* sound) {
  State& st = g_state;
  if (st.queued && (st.queued->sound == nullptr || st.queued->sound == sound)) st.queued = nullptr;
  if (!st.queue) return;
  for (auto it = st.queue->begin(); it != st.queue->end();) {
    if ((*it)->sound == sound) {
      delete *it;
      it = st.queue->erase(it);
    } else {
      ++it;
    }
  }
}

// Pirate's Moon 1.0 @ 0x00591700: MW3's 0x00570510 without its second round
// of frees: the CD, the groups and the sets go only with the device (0x00591f30),
// so not at all when it was never opened.
int32_t shutdown() {
  State& st = g_state;
  stop_queue(0);
  delete st.queue;
  st.queue = nullptr;
  close_device(nullptr);
  fades_shutdown();
  if (st.root) {
    zrdr::free_node(st.root);
    st.root = nullptr;
  }
  st.has_search = false, st.search.clear();
  st.has_primary_format = false;
  for (bool& b : st.has_playback) b = false;
  st.playback_current = -1;
  return 1;
}

// Pirate's Moon 1.0 @ 0x00591840 (the window, the file, the load callback):
// as MW3's 0x005705e0, then the reader whatever SYNTAX says.
int32_t load(uint32_t window, const char* name, LoadFn fn, int32_t /*demo*/) {
  State& st = g_state;
  if (st.open || !st.initialized) return 0;
  if (window) st.window = window;
  if (st.api == 0 && !open_device()) return 0;
  st.open = 1;
  st.root = zrdr::load(name);
  if (!st.root) {
    char msg[300];
    std::snprintf(msg, sizeof msg, "Failed to find menu sounds (%s)", name);
    zerr::report(0x200, kFile, 0x31c, msg);
    return 0;
  }
  read_sounds(st.root, fn);
  return 1;
}

// Pirate's Moon 1.0 @ 0x00591830
void set_range_scale(float scale) { g_state.range_scale = scale; }

// Pirate's Moon 1.0 @ 0x00591900: CD_TRACKS, SOUND_PATH, COMPILE_FORMAT,
// DEFAULT_QUEUE_PRIORITY, SPEED_OF_SOUND, PLAYBACK_FORMAT, the SETS that
// INCLUDE_SETS names (all when it names none; the data's EXCLUDE_SETS is not
// read), then SOUND_GROUPS (no DEMO_GROUPS).
int32_t read_sounds(zrdr::Node* root, LoadFn fn) {
  State& st = g_state;
  if (g_hooks.cd_open) g_hooks.cd_open(zrdr::find(root, "CD_TRACKS"));  // 0x00593190
  if (const char* sp = zrdr::get_string(root, "SOUND_PATH")) {
    if (!st.has_search) st.search = sp, st.has_search = true;  // 0x00596f10
    else st.search = st.search + ";" + sp;                      // 0x00596f50
  }
  if (zrdr::Node* cf = zrdr::find(root, "COMPILE_FORMAT")) read_specs(st.compile, cf);
  // The value's first item, raw (a value not a list crashes the exe).
  if (zrdr::Node* q = zrdr::find(root, "DEFAULT_QUEUE_PRIORITY"); q && q->type == zrdr::List)
    st.default_qpriority = q->list[0].i > 1 ? q->list[1].i : 0;
  float sos = 0.0f;
  if (zrdr::get_float(root, "SPEED_OF_SOUND", &sos)) set_speed_of_sound(sos);
  if (zrdr::Node* pf = zrdr::find(root, "PLAYBACK_FORMAT")) {
    // Each as the data has it, channels too (MW3's forced mono).
    const char* const keys[3] = {"HIGH", med(), "LOW"};
    for (int i = 0; i < 3; ++i) {
      Format f;
      if (!read_spec(&f, zrdr::find(pf, keys[i]))) continue;
      st.has_playback[i] = true;  // calloc'd at 0x0061328c + 4i
      st.playback[i] = f;
    }
  }
  std::vector<const char*> include;
  if (zrdr::Node* inc = zrdr::find(root, "INCLUDE_SETS"); inc && inc->type == zrdr::List)
    for (int32_t i = 1; i < inc->list[0].i; ++i)
      if (inc->list[i].type == zrdr::String) include.push_back(inc->list[i].s);
  if (zrdr::Node* sets = zrdr::find(root, "SETS"); sets && sets->type == zrdr::List) {
    zrdr::Node* items = sets->list;
    for (int32_t i = 1; i < items[0].i; i += 2)
      if (items[i].type == zrdr::String && included(include, items[i].s)) read_set(&items[i], fn);
  }
  if (zrdr::Node* g = zrdr::find(root, "SOUND_GROUPS")) read_groups(g, nullptr, fn);  // 0x00594150
  return 1;
}

// Pirate's Moon 1.0 @ 0x005920e0 (the set's name node, its list after it):
// the set of that name, new or not, its SUB_SET names (made if not read yet)
// and its sounds. A keyword item steps over three: itself, its value and the
// item after (MW3 steps two); the shipped sets put SUB_SET last.
Set* read_set(zrdr::Node* name_node, LoadFn fn) {
  Set* set = make_set(name_node->s);
  zrdr::Node* body = name_node + 1;
  if (zrdr::Node* sub = zrdr::find(body, "SUB_SET"); sub && sub->type == zrdr::List)
    for (int32_t j = 1; j < sub->list[0].i; ++j)
      if (sub->list[j].type == zrdr::String) set->subsets.push_back(make_set(sub->list[j].s));  // 0x00590d20
  zrdr::Node* items = body->list;
  const int32_t n = items[0].i;
  for (int32_t i = 1; i < n; ++i) {
    if (items[i].type == zrdr::List) read_sound(set, &items[i], fn);
    else i += 2;
  }
  return set;
}

// Pirate's Moon 1.0 @ 0x00592170 (ECX the set, the sound's list): as MW3's
// (0x00570af0's loop), with RANGE through the vec2 getter and times the range
// scale, the formats from COMPILE_FORMAT, QPRIORITY, and the volume clamped
// after the callbacks. The sound goes to the set's end.
Sound* read_sound(Set* set, zrdr::Node* node, LoadFn fn) {
  State& st = g_state;
  set->sounds.emplace_back();  // calloc(0xd0) (0x00592040)
  Sound& s = set->sounds.back();
  s.kind = 0;
  s.queue_life = 5.0f;
  s.volume = 1.0f;
  s.a3d_dist = 1.0f;
  s.range_min = st.range_scale * 50.0f;
  s.range_max = st.range_scale * 400.0f;
  for (int f = 0; f < 3; ++f) s.formats[f] = st.compile[f];
  s.freq_max = 90000.0f;
  s.freq_min = 20000.0f;
  s.subtitle = nullptr;
  s.user = nullptr;
  const zrdr::Node* e = node->list;
  s.name = e[1].s;
  s.file = e[2].s;
  s.flags = has(node, "3D") ? (s.flags | k3D) : (s.flags & ~k3D);
  s.flags = has(node, "LOOPED") ? (s.flags | kLooped) : (s.flags & ~kLooped);
  s.flags = has(node, "FREQUENCY") ? (s.flags | kFrequency) : (s.flags & ~kFrequency);
  s.flags = has(node, "HARDWARE") ? (s.flags | kHardware) : (s.flags & ~kHardware);
  s.flags = has(node, "PURGEABLE") ? (s.flags | kPurgeable) : (s.flags & ~kPurgeable);
  s.flags = has(node, "VOICE") ? (s.flags | kVoice) : (s.flags & ~kVoice);
  if (zrdr::Node* q = zrdr::find(node, "QUEUE")) {
    s.flags |= kQueue;
    zrdr::get_float_value(q, &s.queue_life);  // 0x00578fc0
  } else {
    s.flags &= ~kQueue;
  }
  // The default, or the value's first item, raw; 0..7 only (a value not a
  // list crashes the exe).
  if (zrdr::Node* q = zrdr::find(node, "QPRIORITY"); q && q->type == zrdr::List) {
    const int32_t v = q->list[0].i > 1 ? q->list[1].i : st.default_qpriority;
    if (v >= 0 && v < 8) s.flags = (s.flags & ~kQPriority) | (static_cast<uint32_t>(v) << 10);
  }
  zrdr::get_float(node, "VOLUME", &s.volume);
  zrdr::get_float(node, "A3DDIST", &s.a3d_dist);
  float r[2];
  if (zrdr::get_vec2(node, "RANGE", r)) {  // 0x005790f0
    s.flags |= k3D;
    s.range_min = st.range_scale * r[0];
    s.range_max = st.range_scale * r[1];
  }
  read_specs(s.formats, node);
  if (st.subtitle_hook) s.subtitle = st.subtitle_hook(node);
  if (fn) s.user = fn(&s, node);
  if (s.volume > 1.0f) s.volume = 1.0f;
  else if (!(s.volume >= 0.0f)) s.volume = 0.0f;
  return &s;
}

// Mech3.exe 1.2 @ 0x00571460: DirectSoundCreate, normal cooperation, the
// card's caps, the primary buffer and its format.
int32_t open_device() {
  State& st = g_state;
  int32_t hr = out()->create();
  if (hr != 0) return ds_error(hr, kFile, 0x3e0);
  hr = out()->set_cooperative_level(st.window, platform::SoundOutput::kNormal);
  if (hr != 0) return ds_error(hr, kFile, 0x3e3);
  hr = out()->get_caps(&st.caps);
  if (hr != 0) return ds_error(hr, kFile, 0x3e7);
  platform::BufferDesc desc;
  desc.flags = 0xc1;
  hr = out()->create_buffer(desc, &st.primary);
  if (hr != 0) return ds_error(hr, kFile, 0x403);
  st.has_primary_format = true;
  hr = out()->get_format(st.primary, &st.primary_format);
  if (hr != 0) return ds_error(hr, kFile, 0x417);
  return 1;
}

}  // namespace zsnd
