#include "pm/gamez/zclass/cls_zbd.h"

#include <vector>

#include "gamez/zclass/camera.h"
#include "gamez/zclass/cls_data.h"
#include "gamez/zclass/cls_node.h"
#include "gamez/zclass/cls_world.h"
#include "gamez/zclass/display.h"
#include "gamez/zclass/list.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zimage/zimg_texture.h"
#include "pm/gamez/zmodel/gmod_const.h"

namespace pm::zcls {

namespace {
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zClass\\cls_zbd.c";
ZbdState g_state;
}  // namespace

ZbdState& zbd_state() { return g_state; }

// Pirate's Moon 1.0 @ 0x00516ab0
zimg::TextureEntry* texture_for(int32_t index) {
  if (g_state.own_textures) return ::zimg::texture_at(index);  // 0x00531840
  return file_texture(index);
}

// Pirate's Moon 1.0 @ 0x00516ae0. The name is the entry's +0xc on, as the exe
// passes it (a 20-letter name runs on into the state's bytes).
zimg::TextureEntry* file_texture(int32_t index) {
  if (!g_state.file || g_state.current < 0 || index < 0) return nullptr;
  const ZbdFile& z = g_state.files[static_cast<size_t>(g_state.current)];
  if (index >= z.get(0xc)) return nullptr;
  const long at = std::ftell(g_state.file);
  std::fseek(g_state.file, z.get(0x10) + index * 0x2c, SEEK_SET);
  uint8_t raw[0x2c + 1] = {};
  const bool ok = std::fread(raw, 0x2c, 1, g_state.file) == 1;
  std::fseek(g_state.file, at, SEEK_SET);
  if (!ok) {
    zerr::report(0x200, kFile, 0x3b8, "Error reading GameZ Texture directory data.");
    return nullptr;
  }
  return ::zimg::add_texture(reinterpret_cast<const char*>(raw + 0xc));
}

namespace {
using ::zcls::Node;

int read_error(int line, const char* msg) {
  zerr::report(0x200, kFile, line, msg);
  return -1;
}

template <typename T>
uint32_t i32(const uint8_t* p, int off) {
  T v;
  std::memcpy(&v, p + off, sizeof v);
  return static_cast<uint32_t>(v);
}

// PM's 0xd4-byte slot into the shared Node (MW3's layout; docs/pm-map.md).
// Its +0x70 box index is returned: read_nodes makes it a pointer later.
int32_t translate_node(Node& n, const uint8_t* pm) {
  std::memcpy(n.raw, pm, 0x4c);                     // name, flags, class, data, model, action: as MW3
  const int32_t ax = static_cast<int16_t>(i32<int16_t>(pm, 0x4c)), az = static_cast<int16_t>(i32<int16_t>(pm, 0x4e));
  n.put<int32_t>(0x4c, ax), n.put<int32_t>(0x50, az);  // the area partition: PM int16 x, z at +0x4c, +0x4e
  n.pm_virtual[0] = static_cast<int16_t>(i32<int16_t>(pm, 0x50)), n.pm_virtual[1] = static_cast<int16_t>(i32<int16_t>(pm, 0x52));
  n.put<int32_t>(0x54, static_cast<int16_t>(i32<int16_t>(pm, 0x54)));  // the parents: PM int16 +0x54
  n.put<uint32_t>(0x58, i32<uint32_t>(pm, 0x58));
  n.put<int32_t>(0x5c, static_cast<int16_t>(i32<int16_t>(pm, 0x56)));  // the children: PM int16 +0x56
  n.put<uint32_t>(0x60, i32<uint32_t>(pm, 0x5c));
  std::memcpy(n.raw + 0x64, pm + 0x60, 0x10);       // the box centre and diagonal: PM +0x60..+0x6f
  std::memcpy(n.raw + 0x74, pm + 0x74, 0xd0 - 0x74);  // the three boxes on: as MW3
  std::memcpy(&n.link, pm + 0xd0, 4);
  return static_cast<int32_t>(i32<int32_t>(pm, 0x70));
}

// 0x00517960: `count` 12-byte entries (a node index, a y range).
void read_partition_nodes(::zcls::Partition& p, int32_t count, std::FILE* f, int32_t base) {
  std::vector<uint8_t> raw(static_cast<size_t>(count) * 12);
  if (std::fread(raw.data(), raw.size(), 1, f) != 1)
    zerr::report(0x200, kFile, 0x40d, "Error reading GameZ Node list.");
  p.nodes = new Node*[static_cast<size_t>(count)]();
  p.pm_y = new float[static_cast<size_t>(count)][2]();
  for (int32_t i = 0; i < count; ++i) {
    int32_t index = static_cast<int32_t>(i32<int32_t>(raw.data(), i * 12));
    if (index >= 0) index += base;
    p.nodes[i] = ::zcls::node_at(index);
    std::memcpy(p.pm_y[i], raw.data() + i * 12 + 4, 8);
  }
}

Node** read_list(int32_t count, std::FILE* f, int32_t base) {
  if (count < 1) return nullptr;
  Node** out = new Node*[static_cast<size_t>(count)]();
  read_node_list(out, count, f, base);
  return out;
}

// The world (PM's 0xcc: MW3's 0xbc with a second area at +0x4c).
int read_world(Node* node, std::FILE* f, int32_t base) {
  auto* w = new ::zcls::World{};
  node->data = w;
  uint8_t pm[0xcc] = {};
  const bool ok = std::fread(pm, sizeof pm, 1, f) == 1;
  std::memcpy(w->raw, pm, 0x4c);
  std::memcpy(w->pm_area2, pm + 0x4c, 0x10);
  std::memcpy(w->raw + 0x4c, pm + 0x5c, 0xbc - 0x4c);
  if (!ok) return read_error(0x51d, "Error reading node world data.");
  const int32_t lights = w->get<int32_t>(0xa0), sounds = w->get<int32_t>(0xac);  // PM +0xb0, +0xbc
  if ((w->lights = read_list(lights, f, base))) w->light_data = new void*[static_cast<size_t>(lights)]();
  if ((w->sounds = read_list(sounds, f, base))) w->sound_data = new void*[static_cast<size_t>(sounds)]();
  const int32_t rows = w->get<int32_t>(0x8c), cols = w->get<int32_t>(0x88);  // PM +0x9c, +0x98
  w->rows = new ::zcls::Partition*[rows > 0 ? rows : 0]();
  for (int32_t z = 0; z < rows; ++z) w->rows[z] = new ::zcls::Partition[cols > 0 ? cols : 0]();
  for (int32_t z = 0; z < rows; ++z) {
    for (int32_t x = 0; x < cols; ++x) {
      ::zcls::Partition& p = w->rows[z][x];
      uint8_t pr[0x58];
      if (std::fread(pr, sizeof pr, 1, f) != 1) return read_error(0x56c, "Error reading world area partition data.");
      std::memcpy(p.raw, pr, 0x48);
      std::memcpy(p.pm_tail, pr + 0x48, 0x10);
      if (const int16_t n = p.get<int16_t>(0x3a); n > 0) read_partition_nodes(p, n, f, base);
    }
  }
  w->put<int32_t>(4, 0), w->put<int32_t>(8, 0), w->put<int32_t>(0xc, 0);
  w->area_pending = nullptr;
  ::zcls::push_front(::zcls::kAllNodes, node), ::zcls::push_front(::zcls::kWorlds, node);
  ::zcls::world_set_fog_type(node, w->get<uint32_t>(0x10));
  ::zcls::world_set_fog_color(node, w->get<float>(0x14), w->get<float>(0x18), w->get<float>(0x1c));
  ::zcls::world_set_fog_altitude(node, w->get<float>(0x2c), w->get<float>(0x28));
  ::zcls::world_set_fog_range(node, w->get<float>(0x20), w->get<float>(0x24));
  ::zcls::world_set_fog_density(node, w->get<float>(0x30));
  ::zcls::world_push_fog(node);
  return 1;
}

template <typename T>
T* read_data(Node* node, std::FILE* f, bool* ok) {
  auto* d = new T{};
  node->data = d;
  *ok = std::fread(d->raw, sizeof d->raw, 1, f) == 1;
  return d;
}
}  // namespace

// Pirate's Moon 1.0 @ 0x005178e0
void read_node_list(Node** out, int32_t count, std::FILE* f, int32_t base) {
  if (count == 0) return;
  std::vector<int32_t> index(static_cast<size_t>(count > 0 ? count : 0));
  if (std::fread(index.data(), static_cast<size_t>(count) * 4, 1, f) != 1)
    zerr::report(0x200, kFile, 0x3e3, "Error reading GameZ Node list.");
  for (int32_t i = 0; i < count; ++i) out[i] = ::zcls::node_at(index[static_cast<size_t>(i)] >= 0 ? index[static_cast<size_t>(i)] + base : -1);
}

// Pirate's Moon 1.0 @ 0x005171a0, MW3's 0x004feaf0 with the node base and
// PM's sizes (docs/pm-map.md, the jump table at 0x005178ac).
int read_node(Node* node, std::FILE* f, int32_t base) {
  using namespace ::zcls;
  int result = 0;
  bool ok;
  switch (node->cls()) {
    case 0:
      break;
    case 1: {
      result = 1;
      Camera* c = read_data<Camera>(node, f, &ok);
      if (!ok) return read_error(0x4b2, "Error reading node camera data.");
      for (int i = 0; i < 4; ++i) {
        const int32_t k = c->get<int32_t>(4 * i);
        c->nodes[i] = node_at(k >= 0 ? k + base : -1);
      }
      push_front(kAllNodes, node), push_front(kCameras, node);
      camera_set_clip(node, c->get<float>(0xb0), c->get<float>(0xb4));
      camera_set_fov(node, c->get<float>(0xd8), c->get<float>(0xdc));
      break;
    }
    case 2:
      result = read_world(node, f, base);
      if (result < 0) return result;
      break;
    case 3:
      result = 1;
      read_data<Window>(node, f, &ok);
      if (!ok) return read_error(0x500, "Error reading node window data.");
      push_front(kAllNodes, node), push_front(kWindows, node);
      break;
    case 4: {
      result = 1;
      Display* d = read_data<Display>(node, f, &ok);
      if (!ok) return read_error(0x4e3, "Error reading node display data.");
      push_front(kAllNodes, node), push_front(kDisplays, node);
      display_set_clear_color(node, d->get<float>(0x10), d->get<float>(0x14), d->get<float>(0x18));
      break;
    }
    case 5:
      result = 1;
      read_data<Object3d>(node, f, &ok);
      if (!ok) return read_error(0x464, "Error reading node object3d data.");
      push_front(kAllNodes, node);
      break;
    case 6: {
      result = 1;
      auto* l = new Lod{};
      node->data = l;
      uint8_t pm[0x5c] = {};
      ok = std::fread(pm, sizeof pm, 1, f) == 1;
      std::memcpy(l->raw, pm, 0x50);
      std::memcpy(l->pm_tail, pm + 0x50, 0xc);
      if (!ok) return read_error(0x479, "Error reading node lod data.");
      push_front(kAllNodes, node);
      break;
    }
    case 9: {
      result = 1;
      auto* l = new Light{};
      node->data = l;
      uint8_t pm[0x100] = {};
      ok = std::fread(pm, sizeof pm, 1, f) == 1;
      std::memcpy(l->raw, pm, 0xb0);
      std::memcpy(l->pm_colors, pm + 0xb0, 0x30);
      std::memcpy(l->raw + 0xb0, pm + 0xe0, 0x20);
      if (!ok) return read_error(0x48e, "Error reading node light data.");
      l->nodes = read_list(l->get<int32_t>(0xc8), f, base);  // PM +0xf8
      push_front(kAllNodes, node), push_front(kLights, node);
      break;
    }
    case 10: {
      result = 1;
      zerr::report(0x200, kFile, 0x432, "Reading sound node data: Must complete software.");
      Sound* s = read_data<Sound>(node, f, &ok);
      if (!ok) return read_error(0x43d, "Error reading node sound data.");
      s->put<int32_t>(0, 0), s->put<int32_t>(4, 0);
      s->nodes = read_list(s->get<int32_t>(0x8c), f, base);
      push_front(kAllNodes, node), push_front(kSounds, node);
      break;
    }
    default: {
      char msg[160];
      std::snprintf(msg, sizeof msg,
                    "gClsReadNode(): Unrecognized node class type:\n  node = %s class_type = %d\n",
                    node->name(), node->cls());
      zerr::report(0x400, kFile, 0x5a6, msg);
      return -1;
    }
  }
  if ((node->parents = read_list(node->parent_count(), f, base))) result = 1;  // PM's int16 +0x54
  if ((node->children = read_list(node->child_count(), f, base))) return 1;  // PM's int16 +0x56
  return result;
}

// Pirate's Moon 1.0 @ 0x00516e90, MW3's 0x004ff200 appending.
int read_nodes(int32_t size, std::FILE* f, int32_t model_base) {
  ::zcls::NodePool& p = ::zcls::nodes();
  const int32_t bs = p.block_size;
  const int32_t base = static_cast<int32_t>(p.blocks.size()) * bs;
  std::vector<int32_t> boxes;  // each node's +0x70 box index
  for (int32_t done = 0; done < size; done += bs) {
    const int32_t b = static_cast<int32_t>(p.blocks.size());
    p.blocks.emplace_back(new Node[static_cast<size_t>(bs)]{});
    p.capacity += bs;
    const int32_t n = size < done + bs ? size - done : bs;
    std::vector<uint8_t> raw(static_cast<size_t>(n) * 0xd4);  // a short read keeps what it read, as the exe's
    const bool ok = std::fread(raw.data(), 1, raw.size(), f) == raw.size();
    for (int32_t i = 0; i < n; ++i) boxes.push_back(translate_node(p.blocks[static_cast<size_t>(b)][i], raw.data() + i * 0xd4));
    if (!ok) return read_error(0x607, "Error reading GameZ Node buffer.");
  }
  for (int32_t i = base + size; i < p.capacity; ++i) {  // the last block's rest: zeroed and freed (0x0050c9d0)
    *::zcls::node_at(i) = Node{};
    ::zcls::free_node(i);
  }
  p.live = 0;  // [0x006393cc] = 0 on every file (kept)
  for (int32_t i = base; i < base + size; ++i) {
    Node* node = ::zcls::node_at(i);
    // PM's +0x70: which box (0 the node's, 1 the model's, 2 the children's)
    // made a pointer to it; any other value is left (the shared Node has none).
    const int32_t box = boxes[static_cast<size_t>(i - base)];
    node->pm_box = box == 0 ? reinterpret_cast<const float*>(node->raw + 0x74)
                   : box == 1 ? reinterpret_cast<const float*>(node->raw + 0x8c)
                   : box == 2 ? reinterpret_cast<const float*>(node->raw + 0xa4) : nullptr;
    int32_t model = node->get<int32_t>(0x3c);
    if (model >= 0) model += model_base;
    node->pm_model = pm::zmod::model_at(model);
    node->model = nullptr;
    node->action = nullptr;
    // PM sets and clears bits 24 and 25 (MW3: 24, kNodeLive); 25's use is unknown.
    if (read_node(node, f, base) < 1) {
      node->link &= ~0x3000000u;
    } else {
      ++p.live;
      node->link |= 0x3000000u;
    }
  }
  for (::zcls::ListItem* item = ::zcls::lists().list[::zcls::kWorlds].head; item; item = item->next) {
    if (!item->node) continue;
    auto* w = static_cast<::zcls::World*>(item->node->data);
    for (int32_t i = 0; i < w->get<int32_t>(0xa0); ++i) w->light_data[i] = w->lights[i]->data;
    for (int32_t i = 0; i < w->get<int32_t>(0xac); ++i) w->sound_data[i] = w->sounds[i]->data;
  }
  return p.capacity;
}

}  // namespace pm::zcls
