#include "gamez/zeffect/zeff_anim_init.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <type_traits>
#include <vector>

#include "gamez/zclass/light.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zclass/list.h"
#include "gamez/zclass/sound.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zutil/zutl_msg.h"
#include "gamez/zclass/object3d.h"
#include "gamez/zeffect/zeff_anim_run.h"
#include "gamez/zmodel/gmod_puff.h"
#include "gamez/zweapon/zweapon.h"
#include "crt/msvc_rand.h"
#include "gamez/zclass/cls_util.h"
#include "gamez/zeffect/zeff_anim_save.h"
#include "gamez/zsave/zsave.h"

namespace zeff {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zEffect\\zeff_anim_init.c";
int32_t g_freed_defs = 0;  // ours: the defs anim_free_all has freed in all (the tests)
AnimState g_anim;
AnimHooks g_hooks{};

// Ours (docs/modding.md, M3): the bytes left in the file, so a count or a
// size from a broken copy is refused before it is allocated.
long g_file_end = -1;
bool fits(std::FILE* f, uint64_t bytes) {
  return g_file_end < 0 || (std::ftell(f) <= g_file_end && bytes <= static_cast<uint64_t>(g_file_end - std::ftell(f)));
}

// `count` records of T read with one fread, as the exe does.
template <typename T>
T* read_records(std::FILE* f, uint32_t count, bool* ok) {
  if (!fits(f, static_cast<uint64_t>(count) * sizeof(T{}.raw))) {
    *ok = false;
    return nullptr;
  }
  T* out = new T[count]{};
  std::vector<uint8_t> buf(count * sizeof out->raw);
  *ok = std::fread(buf.data(), sizeof out->raw, count, f) == count;
  for (uint32_t i = 0; i < count; ++i) std::memcpy(out[i].raw, buf.data() + i * sizeof out->raw, sizeof out->raw);
  return out;
}

bool read_blob(std::FILE* f, int32_t size, uint8_t** out) {
  if (size <= 0) return true;
  if (!fits(f, static_cast<uint64_t>(size))) return false;
  *out = new uint8_t[size];
  return std::fread(*out, size, 1, f) == 1;
}

// One def and its lists, blobs and sequences, in file order.
bool read_def(std::FILE* f, AnimDef& d) {
  if (std::fread(d.raw, sizeof d.raw, 1, f) != 1) return false;
  std::memset(d.raw + 0x114, 0, 0x24);
  std::memset(d.raw + 0xc4, 0, 4), std::memset(d.raw + 0x100, 0, 4);
  bool ok = true;
  if (const uint8_t n = d.count(0x109); n && (d.objects = read_records<ObjectRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x10a); n && (d.nodes = read_records<NodeRef40>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x10b); n && (d.lights = read_records<CreatedRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x10c); n && (d.puffers = read_records<CreatedRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x10d); n && (d.sounds = read_records<CreatedRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x10e); n && (d.static_sounds = read_records<StaticSoundRef>(f, n, &ok), !ok))
    return false;
  if (const uint8_t n = d.count(0x10f); n && (d.effects = read_records<EffectRef>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x110); n && (d.prereqs = read_records<Prerequisite>(f, n, &ok), !ok)) return false;
  if (const uint8_t n = d.count(0x112); n) {
    if ((d.anim_refs = read_records<AnimRefRec>(f, n, &ok), !ok)) return false;
    for (uint8_t i = 0; i < n; ++i) d.anim_refs[i].put<uint32_t>(0x44, 0);
  }
  const uint8_t seqs = d.count(0x108);
  if (seqs) d.seqs = new SeqDef[seqs]{};
  // The reset sequence again, over the copy in the record.
  if (std::fread(d.raw + 0xc8, 0x40, 1, f) != 1) return false;
  if (!read_blob(f, d.get<int32_t>(0x104), &d.reset_events)) return false;
  std::memcpy(d.reset_seq.raw, d.raw + 0xc8, 0x40);
  d.reset_seq.events = d.reset_events;
  for (uint8_t i = 0; i < seqs; ++i) {
    SeqDef& s = d.seqs[i];
    if (std::fread(s.raw, sizeof s.raw, 1, f) != 1) return false;
    if (!read_blob(f, s.get<int32_t>(0x3c), &s.events)) return false;
  }
  return true;
}

// Binds one def to the scene: false on any lookup failure. Def 0 and corrupt
// (status 5) defs are skipped with their light, puffer and sound counts left 0.
bool fix_def(int32_t index, AnimDef& d, const char** prev_name) {
  const AnimHooks& h = g_hooks;
  const uint8_t lights = d.count(0x10b), puffers = d.count(0x10c), sounds = d.count(0x10d);
  d.raw[0x10b] = d.raw[0x10c] = d.raw[0x10d] = 0;
  if (index == 0 || d.raw[0x98] == 5) return true;
  const char* node_name = reinterpret_cast<const char*>(d.raw + 0x20);
  // A run of defs with one node name takes that name's nodes in turn.
  d.node = nullptr;
  if (*prev_name && std::strcmp(*prev_name, node_name) == 0) d.node = zcls::list_walk_by_name(nullptr, 0);
  if (!d.node) {
    *prev_name = node_name;
    zcls::list_walk_by_name(node_name, zcls::kAllNodes);
    d.node = zcls::list_walk_by_name(nullptr, 0);
  }
  if (!d.node) return false;
  Node* root = zcls::node_root(d.node);
  if (g_anim.copy_trees && root && root->cls() != 2 && root->cls() != 1) {
    if (!(d.node = h.copy_tree(d.node))) return false;
    d.put<uint32_t>(0x94, d.get<uint32_t>(0x94) | 0x20000);
  }
  d.root = nullptr;
  if (!(d.root = anim_find_node(&d, reinterpret_cast<const char*>(d.raw + 0x44)))) return false;
  const auto name = [](const auto& rec) { return reinterpret_cast<const char*>(rec.raw); };
  d.raw[0x10b] = lights;
  for (int k = 1; k < d.count(0x10b); ++k) {
    Node* n = zcls::light_create();
    if (!(d.lights[k].made = n)) return false;
    zcls::node_set_name(n, name(d.lights[k]));
  }
  d.raw[0x10c] = puffers;
  for (int k = 1; k < d.count(0x10c); ++k)
    if (!(d.puffers[k].made = h.create_puffer())) return false;
  d.raw[0x10d] = sounds;
  for (int k = 1; k < d.count(0x10d); ++k) {
    Node* n = zcls::sound_create();
    if (!(d.sounds[k].made = n)) return false;
    zcls::node_set_name(n, name(d.sounds[k]));
    zcls::sound_set_name(n, name(d.sounds[k]));
  }
  for (int k = 1; k < d.count(0x109); ++k)
    if (!(d.objects[k].node = anim_find_node(&d, name(d.objects[k])))) return false;
  for (int k = 1; k < d.count(0x10a); ++k)
    if (!(d.nodes[k].node = anim_find_node(&d, name(d.nodes[k])))) return false;
  for (int k = 1; k < d.count(0x10e); ++k) d.static_sounds[k].sound = h.find_sound(name(d.static_sounds[k]));
  for (int k = 1; k < d.count(0x10f); ++k) {
    const int32_t e = h.effect_index(name(d.effects[k]));
    d.effects[k].put(0x20, e);
    if (e == -1) return false;
  }
  // Prerequisites: type 1 names a def; types 2/3 name a node, 3 under the
  // node found before it. FIX (bug), behind bugs: the exe's def search stops
  // one short and never finds the last def; this searches them all
  // (docs/fixes.md).
  Node* last = nullptr;
  for (int k = 0; k < d.count(0x110); ++k) {
    Prerequisite& p = d.prereqs[k];
    const uint8_t type = p.raw[4];
    if (type == 1) {
      p.def = nullptr;
      const char* want = reinterpret_cast<const char*>(p.raw + 8);
      const int32_t defs = zfix::options().bugs ? g_anim.def_count() : g_anim.def_count() - 1;
      for (int32_t j = 0; j < defs; ++j) {
        if (std::strcmp(g_anim.defs[j].name(), want) == 0) {
          p.def = &g_anim.defs[j];
          break;
        }
      }
    } else if (type == 2 || type == 3) {
      const char* want = reinterpret_cast<const char*>(p.raw + 0xc);
      Node* n = last ? zcls::node_find_in_tree(last, want) : anim_find_node(&d, want);
      if (!n) {
        char msg[200];
        std::snprintf(msg, sizeof msg,
                      "ACTIVATION_PREREQUISITE error; couldn't find node.\n  Animation: %s; node: %s\n",
                      d.name(), want);
        zerr::report(0x400, kFile, 0x33a0, msg);
        if (!g_anim.fix_prerequisites || !zfix::options().bugs) {
          d.raw[0x110] = 0;  // the exe: the def loses every prerequisite
          break;
        }
        // FIX (bug): only this entry goes; the def keeps waiting on the rest
        // (decided with Owen, docs/fixes.md). The count to meet comes down if
        // fewer entries that count are left.
        for (int j = k; j + 1 < d.count(0x110); ++j) d.prereqs[j] = d.prereqs[j + 1];
        d.raw[0x110] = static_cast<uint8_t>(d.raw[0x110] - 1);
        int counting = 0;
        for (int j = 0; j < d.count(0x110); ++j)
          if (d.prereqs[j].get<uint32_t>(0) && (d.prereqs[j].raw[4] == 1 || d.prereqs[j].raw[4] == 2)) ++counting;
        if (d.raw[0x111] > counting) d.raw[0x111] = static_cast<uint8_t>(counting);
        last = nullptr;
        --k;
        continue;
      }
      p.node = n;
      last = type == 2 ? nullptr : n;
    }
  }
  return true;
}
}  // namespace

AnimState& anim() { return g_anim; }
AnimHooks& anim_hooks() { return g_hooks; }

// Mech3.exe 1.2 @ 0x005090e0
void anim_set_world(void* world) { g_anim.world = world; }

// Mech3.exe 1.2 @ 0x005090f0. A path of 0x80 characters or more is refused.
void anim_set_zbd_path(const char* path) {
  if (std::strlen(path) > 0x7f) {
    char msg[200];
    std::snprintf(msg, sizeof msg, "Animation ZBD filename too long: %s\n", path);
    zerr::report(0x400, kFile, 0x100, msg);
    return;
  }
  std::strcpy(g_anim.path, path);
}

// Mech3.exe 1.2 @ 0x00509150
void anim_allow_loading(int32_t on) { g_anim.loading = on; }

// Mech3.exe 1.2 @ 0x0050a0e0. A failure part way leaves what was read in
// place; the caller (0x50ae60) ignores the result.
int anim_read_zbd() {
  AnimState& a = g_anim;
  if (!a.loading || a.path[0] == 0) return -1;
  std::FILE* f = zutl::open(a.path, "rb");  // FIX (A): the mounts (zutl_path.h)
  if (!f) return -1;
  std::fseek(f, 0, SEEK_END);
  g_file_end = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  // Ours: a broken copy is refused whole (the log names it) and the world has
  // no animations; the exe left what it had read in place, half bound.
  bool header_ok = false;
  const auto fail = [f, &a, &header_ok] {
    std::fclose(f);
    g_file_end = -1;
    if (header_ok) {
      std::printf("mw3: %s is broken (a count, size or name it cannot hold), its animations not read\n", a.path);
      delete[] a.defs, a.defs = nullptr;
      a.block.put<int16_t>(0x0a, 0), a.block.put<int32_t>(0x10, 0);
      delete[] a.messages, a.messages = nullptr;
    }
    return -1;
  };
  uint32_t header[3];
  if (std::fread(header, sizeof header, 1, f) != 1 || header[0] != 0x08170616 || header[1] != 39) return fail();
  header_ok = true;
  if (!fits(f, static_cast<uint64_t>(header[2]) * 0x54)) return fail();
  // The source file list (0x0060c5f8 / 0x0060c5fc) is read and dropped.
  std::vector<uint8_t> sources(static_cast<size_t>(header[2]) * 0x54);
  if (std::fread(sources.data(), 0x54, header[2], f) != header[2]) return fail();
  if (std::fread(a.block.raw, sizeof a.block.raw, 1, f) != 1) return fail();
  a.block.put<uint32_t>(0x14, 0);
  a.messages = nullptr;
  const int16_t count = a.def_count();
  a.defs = new AnimDef[count > 0 ? count : 0]{};
  for (int32_t i = 0; i < count; ++i)
    if (!read_def(f, a.defs[i])) return fail();
  const char* prev_name = nullptr;
  for (int32_t i = 0; i < count; ++i)
    if (!fix_def(i, a.defs[i], &prev_name)) {  // a lookup, not a broken file: the original's way
      std::fclose(f);
      g_file_end = -1;
      return -1;
    }
  if (const int32_t n = a.message_count(); n > 0) {
    bool ok;
    a.messages = read_records<AnimMessage>(f, n, &ok);
    if (!ok) return fail();
    for (int32_t i = 0; i < n; ++i)
      a.messages[i].put(0x20, zutl::msg_id(reinterpret_cast<const char*>(a.messages[i].raw)));
  }
  std::fclose(f);
  g_file_end = -1;
  return 0;
}

// Mech3.exe 1.2 @ 0x005096d0. The node or a descendant by name, children first to last.
Node* anim_find_in_tree(Node* node, const char* name) {
  if (!node) return nullptr;
  if (std::strcmp(node->name(), name) == 0) return node;
  for (int32_t i = 0; i < node->child_count(); ++i)
    if (Node* hit = anim_find_in_tree(node->children[i], name)) return hit;
  return nullptr;
}

namespace {
int32_t created_index(const CreatedRef* refs, uint8_t count, const char* name) {
  for (int32_t k = 0; k < count; ++k) {
    const auto* n = static_cast<const Node*>(refs[k].made);
    if (n && std::strcmp(n->name(), name) == 0) return k;
  }
  return -1;
}
}  // namespace

// Mech3.exe 1.2 @ 0x005091e0. By the created light node's name.
int32_t anim_light_index(AnimDef* def, const char* name) {
  return def->count(0x10b) ? created_index(def->lights, def->count(0x10b), name) : -1;
}

// Mech3.exe 1.2 @ 0x00509160
int32_t anim_sound_index(AnimDef* def, const char* name) {
  return def->count(0x10d) ? created_index(def->sounds, def->count(0x10d), name) : -1;
}

namespace {
// 0x00509400 / 0x005092e0: a new entry at the end of a created-node list,
// its first 0x24 bytes the new node's name buffer. A list made here starts
// with an empty entry 0. 0xff entries are full (reported; the node leaks).
int32_t add_created(AnimDef& def, CreatedRef*& list, int count_off, Node* made, int line, const char* what) {
  if (!list) {
    list = new CreatedRef[1]{};
    def.raw[count_off] = static_cast<uint8_t>(def.raw[count_off] + 1);
  }
  const uint8_t count = def.raw[count_off];
  if (count == 0xff) {
    char msg[96];
    std::snprintf(msg, sizeof msg, "%s list overflow.\n  Animation: %s\n", what, def.name());
    zerr::report(0x400, kFile, line, msg);
    return -1;
  }
  auto* grown = new CreatedRef[count + 1]{};
  for (int i = 0; i < count; ++i) grown[i] = list[i];
  delete[] list;
  list = grown;
  CreatedRef& r = grown[count];
  std::memcpy(r.raw, made->raw, 0x24);
  r.made = made;
  r.put<int32_t>(0x28, 0);
  def.raw[count_off] = static_cast<uint8_t>(count + 1);
  return count;
}
}  // namespace

// Mech3.exe 1.2 @ 0x00509400. Found by the light node's name, or a new light
// named so.
int32_t anim_light_ref(AnimDef& def, const char* name) {
  const int32_t found = anim_light_index(&def, name);
  if (found > 0) return found;
  Node* n = zcls::light_create();
  if (!n) return -1;
  zcls::node_set_name(n, name);
  return add_created(def, def.lights, 0x10b, n, 0x249, "Light");
}

// Mech3.exe 1.2 @ 0x005092e0. The same for a dynamic sound node.
int32_t anim_sound_ref(AnimDef& def, const char* name) {
  const int32_t found = anim_sound_index(&def, name);
  if (found > 0) return found;
  Node* n = zcls::sound_create();
  if (!n) return -1;
  zcls::node_set_name(n, name);
  zcls::sound_set_name(n, name);
  return add_created(def, def.sounds, 0x10d, n, 0x200, "Sound");
}

// Mech3.exe 1.2 @ 0x00509eb0. The def follows `node`: its node, and its
// root when that was the node, else the root found again under the node
// (missing: status 5, 0). The object and node refs and the node
// prerequisites are looked up again (a missing ref is only reported).
AnimDef* anim_bind(AnimDef* def, Node* node) {
  if (!def || !node || def->raw[0x98] == 5) return nullptr;
  Node* old = def->node;
  def->node = node;
  if (def->root == old) {
    std::strcpy(reinterpret_cast<char*>(def->raw + 0x20), node->name());
    def->root = node;
    std::strcpy(reinterpret_cast<char*>(def->raw + 0x44), node->name());
  } else {
    std::strcpy(reinterpret_cast<char*>(def->raw + 0x20), node->name());
    Node* root = anim_find_in_tree(def->node, reinterpret_cast<const char*>(def->raw + 0x44));
    if (!root) {
      zerr::report(0x400, kFile, 0x2f19, "Animation node not found.");
      def->raw[0x98] = 5;
      return nullptr;
    }
    def->root = root;
  }
  const auto name = [](const auto& rec) { return reinterpret_cast<const char*>(rec.raw); };
  if (def->count(0x109) > 1)
    for (int k = 1; k < def->count(0x109); ++k)
      if (!(def->objects[k].node = anim_find_node(def, name(def->objects[k]))))
        zerr::report(0x400, kFile, 0x2f44, "Animation node not found.");
  if (def->count(0x10a) > 1)
    for (int k = 1; k < def->count(0x10a); ++k)
      if (!(def->nodes[k].node = anim_find_node(def, name(def->nodes[k]))))
        zerr::report(0x400, kFile, 0x2f6e, "Animation node not found.");
  Node* last = nullptr;
  for (int k = 0; k < def->count(0x110); ++k) {
    Prerequisite& p = def->prereqs[k];
    if (p.raw[4] != 3 && p.raw[4] != 2) continue;
    const char* want = reinterpret_cast<const char*>(p.raw + 0xc);
    last = last ? zcls::node_find_in_tree(last, want) : anim_find_node(def, want);
    if (p.raw[4] == 2) {
      p.node = last;
      last = nullptr;
    }
  }
  return def;
}

// Mech3.exe 1.2 @ 0x005097c0. A copy of the def for another node (or, with
// none, a copy of its node's tree, the dead path), with its own object,
// lights, puffers, sounds and lists; its sequences' current events still
// point into the original's (until restarted, as in the exe).
AnimDef* anim_instance(AnimDef* def, Node* node) {
  if (!def) return nullptr;
  auto* c = new AnimDef(*def);  // FIX (A): calloc + memcpy of the 0x13c bytes
  c->object = zcls::object3d_create();
  char buf[0x24];
  std::snprintf(buf, sizeof buf, "_%s", def->name());
  zcls::node_set_name(c->object, buf);
  zcls::node_set_priority(c->object, c->raw[0x9a]);
  c->instance = nullptr;
  c->put<uint32_t>(0x94, c->get<uint32_t>(0x94) & ~0x100u);
  // FIX (bug): made from an instance that copied its tree, the exe keeps its
  // bit 17 on one at a borrowed node, whose tree the end then destroys
  // (0x0050b2f0, 0x0050b0e0). fixes.md.
  if (g_anim.fix_instance_tree) c->put<uint32_t>(0x94, c->get<uint32_t>(0x94) & ~0x20000u);
  if (!node) {
    c->node = g_hooks.copy_tree ? g_hooks.copy_tree(def->node) : nullptr;
    if (!c->node) {
      zerr::report(0x400, kFile, 0x2dc4, "ERROR: Copying Animation Node Tree.");
      return nullptr;
    }
    c->put<uint32_t>(0x94, c->get<uint32_t>(0x94) | 0x20000);
  } else {
    c->node = node;
    std::strcpy(reinterpret_cast<char*>(c->raw + 0x20), node->name());
  }
  if (def->root == def->node) {
    c->root = c->node;
    std::strcpy(reinterpret_cast<char*>(c->raw + 0x44), c->node->name());
  } else {
    c->root = anim_find_in_tree(c->node, reinterpret_cast<const char*>(def->raw + 0x44));
  }
  if (!c->root) {
    zerr::report(0x400, kFile, 0x2ddb, "Animation node not found.");
    c->raw[0x98] = 5;
    return nullptr;
  }
  const auto copy_list = [](auto*& list, uint8_t n, const auto* from) {
    using T = std::remove_reference_t<decltype(*list)>;
    if (!n) return;
    list = new T[n];
    for (uint8_t i = 0; i < n; ++i) list[i] = from[i];
  };
  if (const uint8_t n = c->count(0x10b)) {
    copy_list(c->lights, n, def->lights);
    for (int i = 0; i < n; ++i) {
      if (def->lights[i].made) {
        Node* l = zcls::light_create();
        c->lights[i].made = l;
        zcls::node_set_name(l, reinterpret_cast<const char*>(c->lights[i].raw));
      }
      c->lights[i].put<int32_t>(0x28, 0);
    }
  }
  if (const uint8_t n = c->count(0x10c)) {
    copy_list(c->puffers, n, def->puffers);
    for (int i = 0; i < n; ++i) {
      c->puffers[i].put<int32_t>(0x28, 1);
      if (def->puffers[i].made) c->puffers[i].made = g_hooks.create_puffer ? g_hooks.create_puffer() : nullptr;
    }
  }
  if (const uint8_t n = c->count(0x10d)) {
    copy_list(c->sounds, n, def->sounds);
    for (int i = 0; i < n; ++i) {
      if (def->sounds[i].made) {
        Node* s = zcls::sound_create();
        c->sounds[i].made = s;
        zcls::node_set_name(s, reinterpret_cast<const char*>(c->sounds[i].raw));
        zcls::sound_set_name(s, reinterpret_cast<const char*>(c->sounds[i].raw));
      }
      c->sounds[i].put<int32_t>(0x28, 0);
    }
  }
  if (const uint8_t n = c->count(0x109)) {
    copy_list(c->objects, n, def->objects);
    for (int i = 0; i < n; ++i)
      if (def->objects[i].node)
        c->objects[i].node = anim_find_node(c, reinterpret_cast<const char*>(c->objects[i].raw));
  }
  if (const uint8_t n = c->count(0x10a)) {
    copy_list(c->nodes, n, def->nodes);
    for (int i = 0; i < n; ++i)
      if (def->nodes[i].node) c->nodes[i].node = anim_find_node(c, reinterpret_cast<const char*>(c->nodes[i].raw));
  }
  if (const uint8_t n = c->count(0x108)) {
    c->seqs = new SeqDef[n];
    for (int i = 0; i < n; ++i) {
      c->seqs[i] = def->seqs[i];
      const int32_t size = def->seqs[i].get<int32_t>(0x3c);
      if (size > 0) {
        c->seqs[i].events = new uint8_t[size];
        std::memcpy(c->seqs[i].events, def->seqs[i].events, size);
      }
    }
  }
  if (const int32_t size = def->get<int32_t>(0x104); size > 0) {
    c->reset_events = new uint8_t[size];
    std::memcpy(c->reset_events, def->reset_events, size);
    c->reset_seq.events = c->reset_events;
  }
  copy_list(c->static_sounds, c->count(0x10e), def->static_sounds);
  copy_list(c->effects, c->count(0x10f), def->effects);
  copy_list(c->prereqs, c->count(0x110), def->prereqs);
  if (const uint8_t n = c->count(0x112)) {
    copy_list(c->anim_refs, n, def->anim_refs);
    for (int i = 0; i < n; ++i) c->anim_refs[i].ref = nullptr;
  }
  return c;
}

// Mech3.exe 1.2 @ 0x00509750. A def flagged 0x8000 gets a copy of its node's
// tree (none shipped).
int32_t anim_copy_node_tree(AnimDef* def) {
  if (!def) return 0;
  if (def->get<uint32_t>(0x94) & 0x8000) {
    def->node = g_hooks.copy_tree ? g_hooks.copy_tree(def->node) : nullptr;
    if (!def->node) return 0;
    def->put<uint32_t>(0x94, def->get<uint32_t>(0x94) | 0x20000);
    anim_bind(def, def->node);
    def->put<uint32_t>(0x94, def->get<uint32_t>(0x94) & ~0x8000u);
  }
  return 1;
}

// Mech3.exe 1.2 @ 0x0050ae60 (ECX "anim.zrd", unused). Once per mission:
// the puffers' tick node, the file, then four passes over defs 1..: (1) the
// billboard flag 0x200, the def's own object `_<name>` at its priority, the
// damage callbacks on its root; (2) RESET_TIME defs run their reset
// sequence; (3) every def saves its objects' states (after the reset: the
// reset defines the start); (4) the corrupt are reported, startup anims start.
int32_t anim_load() {
  AnimState& a = g_anim;
  if (a.block.get<int32_t>(0) != 0) return 0;
  zmod::puff_tick_reset();
  anim_read_zbd();  // the result is not checked
  for (int32_t i = 1; i < a.def_count(); ++i) {
    AnimDef* d = &a.defs[i];
    if (d->raw[0x98] == 5 || d->raw[0x98] == 4) continue;
    if (zcls::node_tree_any(d->node, zcls::node_is_billboard)) d->put<uint32_t>(0x94, d->get<uint32_t>(0x94) | 0x200);
    if (!d->node) d->node = d->root = zcls::object3d_create();  // dead: the walk above read it
    if (d->get<uint32_t>(0x94) & 0x8000) anim_copy_node_tree(d);
    d->object = zcls::object3d_create();
    char buf[0x24];
    std::snprintf(buf, sizeof buf, "_%s", d->name());
    zcls::node_set_name(d->object, buf);
    zcls::node_set_priority(d->object, d->raw[0x9a]);
    const int32_t near = (d->get<uint32_t>(0x94) >> 20) & 1;
    if (d->raw[0x99] == 1) zwep::set_touch_handler(d, d->root, &anim_on_collide);
    if (d->raw[0x99] == 0) zwep::set_damage_handler(d, d->root, &anim_on_weapon_hit, near);
    if (d->raw[0x99] == 2) {
      zwep::set_touch_handler(d, d->root, &anim_on_collide);
      zwep::set_damage_handler(d, d->root, &anim_on_weapon_hit, near);
    }
  }
  for (int32_t i = 1; i < a.def_count(); ++i) {
    AnimDef* d = &a.defs[i];
    const uint8_t s = d->raw[0x98];
    if (s == 5 || s == 4 || !(d->get<uint32_t>(0x94) & 0x20)) continue;
    if (s == 0 || s == 1) anim_reset(d, nullptr, 0);
  }
  for (int32_t i = 1; i < a.def_count(); ++i) {
    AnimDef* d = &a.defs[i];
    if (d->raw[0x98] != 5 && d->raw[0x98] != 4) anim_save_states(d);
  }
  for (int32_t i = 1; i < a.def_count(); ++i) {
    AnimDef* d = &a.defs[i];
    if (d->raw[0x98] == 5) {
      char msg[96];
      std::snprintf(msg, sizeof msg, "Corrupt animation loaded: %s\n", d->name());
      zerr::report(0x400, kFile, 0x3595, msg);
    } else if (d->raw[0x98] != 2 && d->raw[0x99] == 4) {
      anim_start(d, nullptr, 0.0f, 0.0f, 0.0f);
    }
  }
  a.block.put<int32_t>(0, 1);
  return 0;
}

// Mech3.exe 1.2 @ 0x00509640. Under the def's root, under its node, its own
// lights and sounds (entry 0 never matches), then, unless flag 0x80000 keeps
// it local, any node in the world.
Node* anim_find_node(AnimDef* def, const char* name) {
  Node* hit = anim_find_in_tree(def->root, name);
  if (!hit) hit = anim_find_in_tree(def->node, name);
  if (hit) return hit;
  if (const int32_t i = anim_light_index(def, name); i > 0) hit = static_cast<Node*>(def->lights[i].made);
  if (hit) return hit;
  if (const int32_t i = anim_sound_index(def, name); i > 0) hit = static_cast<Node*>(def->sounds[i].made);
  if (!hit && (def->get<uint32_t>(0x94) & 0x80000) == 0) hit = zcls::list_find_by_name(zcls::kAllNodes, name);
  return hit;
}

// Mech3.exe 1.2 @ 0x0050b450: a def by its name, not the first one (index 0)
// and none whose status (+0x98) is 5. Weapons and others find their
// animations this way.
AnimDef* anim_find(const char* name) {
  if (!name) return nullptr;
  for (int32_t j = 1; j < g_anim.def_count(); ++j) {
    AnimDef& d = g_anim.defs[j];
    if (d.raw[0x98] != 5 && std::strcmp(d.name(), name) == 0) return &d;
  }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0050b570: the next def after `after` (none: from index
// 1) in use (status not 5) with HAS_CALLBACKS (+0x94 bit 0x10).
AnimDef* anim_next_with_callbacks(AnimDef* after) {
  int32_t i = after ? static_cast<int32_t>(after - g_anim.defs) : 0;
  while (++i < g_anim.def_count()) {
    AnimDef& d = g_anim.defs[i];
    if (d.raw[0x98] != 5 && (d.raw[0x94] & 0x10)) return &d;
  }
  return nullptr;
}

// Mech3.exe 1.2 @ 0x0050b5e0: the def's node (+0x40), none once freed (status 5).
Node* anim_node(const AnimDef* d) { return d && d->raw[0x98] != 5 ? d->node : nullptr; }

// Mech3.exe 1.2 @ 0x0050b0e0 (EDX: the instances too). The def's object
// node released, its damage handlers off its root (kinds 0..2), every list
// freed and counted 0, a copied tree destroyed.
void anim_free(AnimDef* d, bool instances) {
  if (d->object) {
    zcls::node_set_action(d->object, nullptr);
    zcls::node_release(d->object);
  }
  const uint8_t kind = d->raw[0x99];
  if (kind == 0 || kind == 1 || kind == 2) zwep::remove_handlers(d->root);  // 0x00583400
  delete[] d->reset_events;
  d->reset_events = nullptr;
  d->put<int32_t>(0x104, 0);
  for (int i = 0; i < d->count(0x108); ++i) delete[] d->seqs[i].events;
  d->raw[0x108] = 0;
  delete[] d->seqs, d->seqs = nullptr;
  delete[] d->objects, d->objects = nullptr, d->raw[0x109] = 0;
  delete[] d->nodes, d->nodes = nullptr, d->raw[0x10a] = 0;
  delete[] d->lights, d->lights = nullptr, d->raw[0x10b] = 0;
  delete[] d->puffers, d->puffers = nullptr, d->raw[0x10c] = 0;
  delete[] d->sounds, d->sounds = nullptr, d->raw[0x10d] = 0;
  delete[] d->static_sounds, d->static_sounds = nullptr, d->raw[0x10e] = 0;
  delete[] d->effects, d->effects = nullptr, d->raw[0x10f] = 0;
  delete[] d->prereqs, d->prereqs = nullptr, d->raw[0x110] = 0;
  delete[] d->anim_refs, d->anim_refs = nullptr, d->raw[0x112] = 0;
  if (d->get<uint32_t>(0x94) & 0x20000) {
    zcls::tree_destroy(d->node);  // 0x004f9050
    d->put<uint32_t>(0x94, d->get<uint32_t>(0x94) & ~0x20000u);
  }
  if (instances && d->instance) {
    anim_free(d->instance, true);
    delete d->instance;
    d->instance = nullptr;
  }
}

// Mech3.exe 1.2 @ 0x0050b2f0: the mission's end: every def's instances that
// copied their own node tree (+0x94 bit 17: started with no node) unlinked
// and freed with it; the def and its other instances stay.
void anim_free_copies() {
  AnimState& a = g_anim;
  for (int32_t i = 0; i < a.def_count(); ++i) {
    AnimDef* prev = &a.defs[i];
    for (AnimDef* c = prev->instance; c; c = prev->instance) {
      if (c->get<uint32_t>(0x94) & 0x20000) {
        prev->instance = c->instance;
        anim_free(c, false);
        delete c;
      } else {
        prev = c;
      }
    }
  }
}

// Mech3.exe 1.2 @ 0x0050b380: every def (with its instances), the defs and
// messages, the animation log (0x0050b650), the block marked unread, the
// puffers (0x005495a0).
int32_t anim_free_all() {
  AnimState& a = g_anim;
  a.block.put<int32_t>(0x04, 0);  // 0x0073f724: the source list, dropped at the read (none kept)
  a.block.put<int16_t>(0x08, 0);  // a word: the def count beside it (+0x0a) is the loop's
  for (int32_t i = 0; i < a.def_count(); ++i) anim_free(&a.defs[i], true);
  g_freed_defs += a.def_count();
  delete[] a.defs;
  a.defs = nullptr;
  a.block.put<int16_t>(0x0a, 0);
  delete[] a.messages;
  a.messages = nullptr;
  a.block.put<int32_t>(0x10, 0);
  anim_log().records = {};  // 0x0050b650 with 0x0050b640
  anim_log().count = 0;
  a.block.put<int32_t>(0x00, 0);
  zmod::puff_shutdown();
  return 0;
}

int32_t anim_freed_defs() { return g_freed_defs; }

// Mech3.exe 1.2 @ 0x0050b430
int32_t anim_shutdown() {
  if (g_anim.block.get<int32_t>(0) != 0) anim_free_all();
  return 0;
}

// Mech3.exe 1.2 @ 0x00508fc0: zEffect's start (and its reset from the effect
// table's 0x0050d270): what was loaded freed, the state cleared, gravity
// -9.8, the random table filled from a seed of the time; the three anim
// registrants given to zSave (docs/save.md).
int32_t anim_init() {
  AnimState& a = g_anim;
  if (a.block.get<int32_t>(0) != 0) anim_free_all();
  a.path[0] = 0;
  for (int off : {0x00, 0x04, 0x08, 0x0c, 0x10, 0x14, 0x18, 0x20, 0x24, 0x28}) a.block.put<int32_t>(off, 0);
  a.defs = nullptr, a.messages = nullptr, a.world = nullptr, a.player_node = nullptr;
  a.block.put<int32_t>(0x3c, 1);
  a.block.put<float>(0x1c, -9.8f);  // 0xc11ccccd
  crt::msvc_srand(static_cast<uint32_t>(std::time(nullptr)));
  for (float& r : a.random) r = static_cast<float>(crt::msvc_rand()) * 3.051851e-05f;
  if (a.save_on) anim_register_savers();
  return 0;
}

// 0x00509079..0x005090c1, inside zEffect's start.
void anim_register_savers() {
  zsave::add("AnimActivation", activation_save, activation_load, 0x32, nullptr);
  zsave::add("RunningAnim", running_save, running_load, 0x33, nullptr);
  zsave::add("Anim", anim_save, anim_load, 0x34, nullptr);
}

// Mech3.exe 1.2 @ 0x005090d0
void anim_enable_save(int32_t on) { g_anim.save_on = on; }

}  // namespace zeff
