#include "pm/gamez/zeffect/zeff_anim_init.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/light.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/sound.h"
#include "gamez/zeffect/zeff_anim_init.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_msg.h"
#include "gamez/zutil/zutl_path.h"

namespace pm::zeff {

using ::zcls::Node;
using ::zeff::AnimDef;
using ::zeff::AnimState;

namespace {
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zEffect\\zeff_anim_init.c";

// Ours, as MW3's reader (docs/modding.md, M3): the bytes left in the file, so a
// count or size from a broken copy is refused before it is allocated.
long g_file_end = -1;
bool fits(std::FILE* f, uint64_t bytes) {
  return g_file_end < 0 || (std::ftell(f) <= g_file_end && bytes <= static_cast<uint64_t>(g_file_end - std::ftell(f)));
}

bool read_bytes(std::FILE* f, void* out, uint64_t n) {
  return fits(f, n) && (n == 0 || std::fread(out, static_cast<size_t>(n), 1, f) == 1);
}

// `count` records of PM's `size` bytes, each put into T by `put`.
template <typename T, typename Put>
T* read_list(std::FILE* f, uint32_t count, size_t size, bool* ok, Put put) {
  std::vector<uint8_t> buf(count * size);
  if (!(*ok = read_bytes(f, buf.data(), buf.size()))) return nullptr;
  T* out = new T[count]{};
  for (uint32_t i = 0; i < count; ++i) put(out[i], buf.data() + i * size);
  return out;
}

template <typename T>
T* read_same(std::FILE* f, uint32_t count, bool* ok) {
  return read_list<T>(f, count, sizeof T{}.raw, ok, [](T& t, const uint8_t* b) { std::memcpy(t.raw, b, sizeof t.raw); });
}

// A sequence's 0x40 bytes and its events (+0x3c bytes, read when over 0).
bool read_seq(std::FILE* f, uint8_t* raw, uint8_t** events) {
  if (!read_bytes(f, raw, 0x40)) return false;
  int32_t size;
  std::memcpy(&size, raw + 0x3c, 4);
  if (size <= 0) return true;
  if (!fits(f, static_cast<uint64_t>(size))) return false;
  *events = new uint8_t[size];
  return std::fread(*events, size, 1, f) == 1;
}

uint16_t u16(const uint8_t* b) {
  uint16_t v;
  std::memcpy(&v, b, 2);
  return v;
}

// One def from PM's 0x10c record and its lists, in file order.
bool read_def(std::FILE* f, AnimDef& d) {
  uint8_t r[0x10c];
  if (!read_bytes(f, r, sizeof r)) return false;
  // MW3's +0x20..+0xc3 are PM's +0x28..+0xcb, its counts +0x108.. PM's +0xd8..,
  // its +0x138 PM's +0x108; the pointers (+0x20, +0xcc, +0xe4..) are not kept.
  std::memcpy(d.raw, r, 0x20);
  std::memcpy(d.raw + 0x20, r + 0x28, 0xa4);
  std::memcpy(d.raw + 0x108, r + 0xd8, 0xc);
  std::memcpy(d.raw + 0x138, r + 0x108, 4);
  std::memcpy(&d.pm_path_count, r + 0x24, 4);
  uint32_t reset, seq2;  // pointers in the file: set means the sequence follows
  std::memcpy(&reset, r + 0xd0, 4), std::memcpy(&seq2, r + 0xd4, 4);
  d.pm_reset = reset != 0;
  bool ok = true;
  if (d.pm_path_count > 0 && (d.pm_path = read_same<::zeff::PathRef>(f, d.pm_path_count, &ok), !ok)) return false;
  const auto parented = [](auto* list, uint8_t n) {  // ours: a parent past the list is a broken file
    for (uint8_t i = 0; i < n; ++i)
      if (list[i].pm_parent >= n) return false;
    return true;
  };
  if (const uint8_t n = d.count(0x109); n) {
    d.objects = read_list<::zeff::ObjectRef>(f, n, 0x5c, &ok, [](::zeff::ObjectRef& o, const uint8_t* b) {
      std::memcpy(o.raw, b, 0x28);
      o.pm_flags = u16(b + 0x28), o.pm_parent = u16(b + 0x2a);
      std::memcpy(o.raw + 0x30, b + 0x2c, 0x30);
    });
    if (!ok || !parented(d.objects, n)) return false;
  }
  if (const uint8_t n = d.count(0x10a); n) {
    d.nodes = read_list<::zeff::NodeRef40>(f, n, 0x2c, &ok, [](::zeff::NodeRef40& o, const uint8_t* b) {
      o.pm_flags = u16(b), o.pm_parent = u16(b + 2);
      std::memcpy(o.raw, b + 4, 0x28);
    });
    if (!ok || !parented(d.nodes, n)) return false;
  }
  if (const uint8_t n = d.count(0x10b); n && (d.lights = read_same<::zeff::CreatedRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x10c); n && (d.puffers = read_same<::zeff::CreatedRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x10d); n && (d.sounds = read_same<::zeff::CreatedRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x10e); n && (d.static_sounds = read_same<::zeff::StaticSoundRef>(f, n, &ok), !ok))
    return false;
  if (const uint8_t n = d.count(0x10f); n && (d.effects = read_same<::zeff::EffectRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x110); n && (d.prereqs = read_same<::zeff::Prerequisite>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x112); n) {
    if ((d.anim_refs = read_same<::zeff::AnimRefRec>(f, n, &ok), !ok)) return false;
    for (uint8_t i = 0; i < n; ++i) d.anim_refs[i].put<uint32_t>(0x44, 0);
  }
  const uint8_t seqs = d.count(0x108);
  if (seqs) d.seqs = new ::zeff::SeqDef[seqs]{};
  if (d.pm_reset) {
    if (!read_seq(f, d.raw + 0xc8, &d.reset_events)) return false;
    std::memcpy(d.reset_seq.raw, d.raw + 0xc8, 0x40);
    d.reset_seq.events = d.reset_events;
  }
  if (seq2) {
    d.pm_seq2 = new ::zeff::SeqDef{};
    if (!read_seq(f, d.pm_seq2->raw, &d.pm_seq2->events)) return false;
  }
  for (uint8_t i = 0; i < seqs; ++i)
    if (!read_seq(f, d.seqs[i].raw, &d.seqs[i].events)) return false;
  return true;
}

// The def node lookup's state over the defs (0x00523140's locals).
struct Lookup {
  int32_t last = -1;               // the def that last set `stale`
  const char* name = nullptr;      // the name last searched for
  int32_t stale = 0;               // that def's node pointer as the file had it
};

// Pirate's Moon 1.0 @ 0x005240d0. Defs whose file pointer and node name are
// the last searched def's take its node; else a run of one name takes that
// name's nodes in turn, as MW3's reader does.
Node* def_node(int32_t index, const char* name, int32_t stale, Lookup& s) {
  const AnimState& a = ::zeff::anim();
  Node* out = nullptr;
  if (s.stale >= 1 && stale == s.stale) {
    const AnimDef& last = a.defs[s.last];
    if (std::strcmp(name, reinterpret_cast<const char*>(last.raw + 0x20)) == 0) {
      out = last.node;
    } else if (last.pm_path_count > 0 && std::strcmp(name, reinterpret_cast<const char*>(last.pm_path[0].raw)) == 0) {
      out = last.pm_path[0].node;
    } else {
      s.last = index, s.stale = stale;
    }
  } else {
    s.last = index, s.stale = stale;
  }
  if (!out && s.name && std::strcmp(s.name, name) == 0) out = ::zcls::list_walk_by_name(nullptr, 0);
  if (!out) {
    ::zcls::list_walk_by_name(name, ::zcls::kAllNodes);
    out = ::zcls::list_walk_by_name(nullptr, 0);
    s.name = name;
  }
  return out;
}

// Pirate's Moon 1.0 @ 0x00524230: from def `from` on, every node found so far
// and every file pointer the lookup compares are cleared.
void forget_from(int32_t from) {
  AnimState& a = ::zeff::anim();
  for (int32_t j = from; j < a.def_count(); ++j) {
    AnimDef& d = a.defs[j];
    for (int32_t k = 0; k < d.pm_path_count; ++k) d.pm_path[k].node = nullptr, d.pm_path[k].put<int32_t>(0x20, 0);
    d.node = nullptr, d.put<int32_t>(0x40, 0);
    d.root = nullptr, d.put<int32_t>(0x64, 0);
    for (int k = 0; k < d.count(0x109); ++k) d.objects[k].node = nullptr, d.objects[k].put<int32_t>(0x24, 0);
    for (int k = 0; k < d.count(0x10a); ++k) d.nodes[k].node = nullptr, d.nodes[k].put<int32_t>(0x24, 0);
  }
}

// Pirate's Moon 1.0 @ 0x005224a0: under `parent`, else as anim_find_node.
Node* find_under(AnimDef* d, Node* parent, const char* name, int local) {
  return parent ? ::zeff::anim_find_in_tree(parent, name) : anim_find_node(d, name, local);
}

enum class Fix { kOk, kCorrupt, kStop };

// One def bound to the scene. kCorrupt marks it 5 and goes on (MW3 stops the
// read there); kStop ends the read, as MW3's failures do.
Fix fix_def(int32_t index, AnimDef& d, Lookup& s) {
  AnimState& a = ::zeff::anim();
  const ::zeff::AnimHooks& h = ::zeff::anim_hooks();
  const uint8_t lights = d.count(0x10b), puffers = d.count(0x10c), sounds = d.count(0x10d);
  d.raw[0x10b] = d.raw[0x10c] = d.raw[0x10d] = 0;
  if (index == 0 || d.raw[0x98] == 5) return Fix::kOk;
  if (d.pm_path_count < 1) {
    d.node = def_node(index, reinterpret_cast<const char*>(d.raw + 0x20), d.get<int32_t>(0x40), s);
  } else {
    ::zeff::PathRef* p = d.pm_path;
    p[0].node = def_node(index, reinterpret_cast<const char*>(p[0].raw), p[0].get<int32_t>(0x20), s);
    for (int32_t k = 1; k < d.pm_path_count; ++k)
      p[k].node = ::zcls::node_find_in_tree(p[k - 1].node, reinterpret_cast<const char*>(p[k].raw));  // 0x0050da50
    d.node = p[d.pm_path_count - 1].node;
  }
  if (!d.node) {
    char msg[160];
    std::snprintf(msg, sizeof msg, "Unable to initalize animation: %s [can't find node:%s]", d.name(),
                  reinterpret_cast<const char*>(d.raw + 0x20));
    zerr::report(0x200, kFile, 0x3675, msg);
    forget_from(index);
    return Fix::kCorrupt;
  }
  Node* root = ::zcls::node_root(d.node);
  if (a.copy_trees && root && root->cls() != 2 && root->cls() != 1) {
    if (!(d.node = h.copy_tree(d.node))) return Fix::kStop;
    d.put<uint32_t>(0x94, d.get<uint32_t>(0x94) | 0x20000);
  }
  d.root = nullptr;
  if (!(d.root = anim_find_node(&d, reinterpret_cast<const char*>(d.raw + 0x44), (d.get<uint32_t>(0x94) >> 19) & 1))) {
    char msg[200];
    std::snprintf(msg, sizeof msg, "Error initializing ZBD animation: %s\nUnable to initialize anim root node: %s\n",
                  d.name(), reinterpret_cast<const char*>(d.raw + 0x44));
    zerr::report(0x400, kFile, 0x3694, msg);
    return Fix::kCorrupt;
  }
  const auto name = [](const auto& rec) { return reinterpret_cast<const char*>(rec.raw); };
  d.raw[0x10b] = lights;
  for (int k = 1; k < d.count(0x10b); ++k) {
    Node* n = ::zcls::light_create();
    if (!(d.lights[k].made = n)) return Fix::kStop;
    ::zcls::node_set_name(n, name(d.lights[k]));
  }
  d.raw[0x10c] = puffers;
  for (int k = 1; k < d.count(0x10c); ++k) {
    if ((d.puffers[k].made = h.create_puffer())) continue;
    for (int j = 1; j < k; ++j) h.destroy_puffer(d.puffers[j].made);  // PM: the def goes on without puffers
    delete[] d.puffers;
    d.puffers = nullptr, d.raw[0x10c] = 0;
    break;
  }
  d.raw[0x10d] = sounds;
  for (int k = 1; k < d.count(0x10d); ++k) {
    Node* n = ::zcls::sound_create();
    if (!(d.sounds[k].made = n)) return Fix::kStop;
    ::zcls::node_set_name(n, name(d.sounds[k]));
    ::zcls::sound_set_name(n, name(d.sounds[k]));
  }
  if (d.count(0x109)) d.objects[0].node = nullptr;
  for (int k = 1; k < d.count(0x109); ++k) {
    ::zeff::ObjectRef& o = d.objects[k];
    o.node = nullptr;
    Node* parent = o.pm_parent ? d.objects[o.pm_parent].node : nullptr;
    if (!(o.node = find_under(&d, parent, name(o), (o.pm_flags >> 1) & 1))) return Fix::kStop;
  }
  if (d.count(0x10a)) d.nodes[0].node = nullptr;
  for (int k = 1; k < d.count(0x10a); ++k) {
    ::zeff::NodeRef40& o = d.nodes[k];
    o.node = nullptr;
    Node* parent = o.pm_parent ? d.nodes[o.pm_parent].node : nullptr;
    if (!(o.node = find_under(&d, parent, name(o), o.pm_flags & 1))) return Fix::kStop;
  }
  for (int k = 1; k < d.count(0x10e); ++k) d.static_sounds[k].sound = h.find_sound(name(d.static_sounds[k]));
  for (int k = 1; k < d.count(0x10f); ++k) {
    const int32_t e = h.effect_index(name(d.effects[k]));
    d.effects[k].put(0x20, e);
    if (e == -1) return Fix::kStop;
  }
  // Prerequisites as MW3's (the FIX (bug)s included), a node one under the
  // one before found children first to last (MW3: last to first).
  Node* last = nullptr;
  for (int k = 0; k < d.count(0x110); ++k) {
    ::zeff::Prerequisite& p = d.prereqs[k];
    const uint8_t type = p.raw[4];
    if (type == 1) {
      p.def = nullptr;
      const char* want = reinterpret_cast<const char*>(p.raw + 8);
      const int32_t defs = zfix::options().bugs ? a.def_count() : a.def_count() - 1;
      for (int32_t j = 0; j < defs; ++j) {
        if (std::strcmp(a.defs[j].name(), want) == 0) {
          p.def = &a.defs[j];
          break;
        }
      }
    } else if (type == 2 || type == 3) {
      const char* want = reinterpret_cast<const char*>(p.raw + 0xc);
      Node* n = find_under(&d, last, want, (p.get<uint32_t>(8) >> 1) & 1);
      if (!n) {
        char msg[200];
        std::snprintf(msg, sizeof msg,
                      "ACTIVATION_PREREQUISITE error; couldn't find node.\n  Animation: %s; node: %s\n", d.name(),
                      want);
        zerr::report(0x400, kFile, 0x376f, msg);
        if (!a.fix_prerequisites || !zfix::options().bugs) {
          d.raw[0x110] = 0;
          break;
        }
        ::zeff::anim_drop_prerequisite(d, k);
        last = nullptr;
        --k;
        continue;
      }
      p.node = n;
      last = type == 2 ? nullptr : n;
    }
  }
  return Fix::kOk;
}
}  // namespace

Node* anim_find_node(AnimDef* def, const char* name, int local) {
  Node* hit = ::zeff::anim_find_in_tree(def->root, name);  // 0x005224d0, MW3's 0x005096d0
  if (hit) return hit;
  if (def->root != def->node && (hit = ::zeff::anim_find_in_tree(def->node, name))) return hit;
  if (const int32_t i = ::zeff::anim_light_index(def, name); i > 0) hit = static_cast<Node*>(def->lights[i].made);
  if (hit) return hit;
  if (const int32_t i = ::zeff::anim_sound_index(def, name); i > 0) hit = static_cast<Node*>(def->sounds[i].made);
  if (!hit && !local) hit = ::zcls::list_find_by_name(::zcls::kAllNodes, name);
  return hit;
}

int anim_read_zbd() {
  AnimState& a = ::zeff::anim();
  if (!a.loading || a.path[0] == 0) return -1;
  std::FILE* f = zutl::open(a.path, "rb");  // FIX (A): the mounts (zutl_path.h)
  if (!f) return -1;
  std::fseek(f, 0, SEEK_END);
  g_file_end = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  // Ours, as MW3's reader: a broken copy is refused whole (the log names it).
  bool header_ok = false;
  const auto fail = [f, &a, &header_ok] {
    std::fclose(f);
    g_file_end = -1;
    if (header_ok) {
      std::printf("mw3: %s is broken (a count, size or name it cannot hold), its animations not read\n", a.path);
      delete[] a.defs, a.defs = nullptr;
      a.block.put<int16_t>(0x0a, 0), a.block.put<int32_t>(0x10, 0);
      delete[] a.messages, a.messages = nullptr;
      delete[] a.pm_scripts, a.pm_scripts = nullptr;
      a.pm_script_count = 0;
    }
    return -1;
  };
  int32_t header[4];  // signature, version, time stamp, source count
  if (std::fread(header, sizeof header, 1, f) != 1 || static_cast<uint32_t>(header[0]) != 0x08170616 ||
      header[1] != 50)
    return fail();
  header_ok = true;
  // A time stamp over 0 asks the world loader for its own (0x00515e20) and
  // drops it: nothing is compared.
  if (header[3] < 0 || !fits(f, static_cast<uint64_t>(header[3]) * 0x54)) return fail();
  std::vector<uint8_t> sources(static_cast<size_t>(header[3]) * 0x54);  // read and dropped
  if (!read_bytes(f, sources.data(), sources.size())) return fail();
  if (!read_bytes(f, a.pm_block.raw, 0x6c)) return fail();
  // MW3's block from PM's (the run-time globals matched by their users, as
  // 0x00502d40 / 0x0051b200 and 0x00502d70 / 0x0051b230; pm-map.md).
  static constexpr int kFields[][2] = {{0x18, 0x20}, {0x1c, 0x24}, {0x20, 0x28}, {0x24, 0x2c}, {0x28, 0x30},
                                       {0x2c, 0x44}, {0x30, 0x48}, {0x34, 0x4c}, {0x38, 0x64}, {0x3c, 0x3c},
                                       {0x40, 0x68}};  // MW3's offset, PM's
  const ::zcls::Raw<0x6c>& p = a.pm_block;
  a.block = {};
  std::memcpy(a.block.raw, p.raw, 0x10);
  a.block.put(0x10, p.get<int32_t>(0x18));
  for (const auto& m : kFields) a.block.put(m[0], p.get<uint32_t>(m[1]));
  a.pm_block.put<uint32_t>(0x1c, 0), a.pm_block.put<uint32_t>(0x14, 0);
  a.pm_script_count = p.get<int32_t>(0x10);
  a.messages = nullptr, a.pm_scripts = nullptr;
  const int16_t count = a.def_count();
  a.defs = new AnimDef[count > 0 ? count : 0]{};
  for (int32_t i = 0; i < count; ++i)
    if (!read_def(f, a.defs[i])) return fail();
  Lookup s;
  for (int32_t i = 0; i < count; ++i) {
    const Fix r = fix_def(i, a.defs[i], s);
    if (r == Fix::kCorrupt) a.defs[i].raw[0x98] = 5;
    if (r == Fix::kStop) {  // a lookup, not a broken file: the original's way
      std::fclose(f);
      g_file_end = -1;
      return -1;
    }
  }
  if (const int32_t n = a.pm_script_count; n > 0) {
    bool ok;
    a.pm_scripts = read_same<::zeff::SiScript>(f, n, &ok);
    if (!ok) return fail();
    for (int32_t i = 0; i < n; ++i) {
      ::zeff::SiScript& sc = a.pm_scripts[i];
      sc.name = nullptr, sc.object = nullptr, sc.data = nullptr;
      // The lengths are bytes; the exe takes them signed for the allocation
      // (one over 0x7f fails it), the port reads them as they are.
      if (const uint8_t len = sc.raw[8]; len) {
        sc.name = new char[len];
        if (!read_bytes(f, sc.name, len)) return fail();
      }
      if (const uint8_t len = sc.raw[9]; len) {
        sc.object = new char[len];
        if (!read_bytes(f, sc.object, len)) return fail();
      }
      if (const int32_t size = sc.get<int32_t>(0x14); size > 0) {
        if (!fits(f, static_cast<uint64_t>(size))) return fail();
        sc.data = new uint8_t[size];
        if (std::fread(sc.data, size, 1, f) != 1) return fail();
      }
    }
  }
  if (const int32_t n = a.message_count(); n > 0) {
    bool ok;
    a.messages = read_same<::zeff::AnimMessage>(f, n, &ok);
    if (!ok) return fail();
    for (int32_t i = 0; i < n; ++i)
      a.messages[i].put(0x20, zutl::msg_id(reinterpret_cast<const char*>(a.messages[i].raw)));  // 0x00596d90
  }
  std::fclose(f);
  g_file_end = -1;
  return 0;
}

}  // namespace pm::zeff
