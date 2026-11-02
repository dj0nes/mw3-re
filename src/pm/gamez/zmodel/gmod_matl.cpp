#include "pm/gamez/zmodel/gmod_matl.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "gamez/zerror/zerr.h"
#include "gamez/zvideo/zvid_format.h"
#include "pm/gamez/zclass/cls_zbd.h"

namespace pm::zmod {

namespace {
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zModel\\gmod_matl.c";
using ::zmod::kMaterialBlock;
using ::zmod::Material;

template <typename T>
T at(const uint8_t* p, int off) {
  T v;
  std::memcpy(&v, p + off, sizeof v);
  return v;
}

int error(int line, const char* msg) {
  zerr::report(0x200, kFile, line, msg);
  return -1;
}

void parse(Material& m, const uint8_t* raw) {
  m.alpha = raw[0], m.flags = raw[1], m.rgb = at<uint16_t>(raw, 2);
  m.r = at<float>(raw, 4), m.g = at<float>(raw, 8), m.b = at<float>(raw, 0xc);
  m.texture_index = at<uint32_t>(raw, 0x10);
  for (int k = 0; k < 3; ++k) m.unk_14[k] = at<float>(raw, 0x14 + 4 * k);
  m.soil = at<uint32_t>(raw, 0x20);
  m.file_cycle = at<uint32_t>(raw, 0x24);
  m.prev = at<int16_t>(raw, 0x28), m.next = at<int16_t>(raw, 0x2a);
}

// On the front of a list (+0x28 back, +0x2a on), as the exe does inline.
void push_front(::zmod::MaterialTable& t, int32_t& head, int32_t i) {
  if (head >= 0) t.at(head).prev = static_cast<int16_t>(i);
  t.at(i).next = static_cast<int16_t>(head);
  t.at(i).prev = -1;
  head = i;
}
}  // namespace

// Pirate's Moon 1.0 @ 0x00555e00, MW3's 0x005363d0 appending. FIX (A): owned
// blocks, as zmod's.
int read_materials(std::FILE* f) {
  ::zmod::MaterialTable& t = ::zmod::materials();
  const int32_t base = t.capacity;
  ::zmod::material_sweep_reset();  // [0x006114f8] = -1
  int32_t size, count, free_head, used_head;
  if (std::fread(&size, 4, 1, f) != 1) return error(0x1dc, "Error reading GameZ Material buffer data.");
  if (std::fread(&count, 4, 1, f) != 1) return error(0x1e9, "Error reading GameZ Material buffer data.");
  if (std::fread(&free_head, 4, 1, f) != 1) return error(0x1f6, "Error reading GameZ Material buffer data.");
  if (std::fread(&used_head, 4, 1, f) != 1) return error(0x203, "Error reading GameZ Material buffer data.");
  if (size == 0) return 0;
  if (base == 0) t.count = count, t.free_head = free_head, t.used_head = used_head;
  for (int32_t done = 0; done < size; done += kMaterialBlock) {
    const int32_t block = static_cast<int32_t>(t.blocks.size());
    t.blocks.emplace_back(new Material[kMaterialBlock]{});
    t.capacity += kMaterialBlock;
    const int32_t n = size < done + kMaterialBlock ? size - done : kMaterialBlock;
    // A short read leaves what it read in the (zeroed) block, as the exe's fread.
    const size_t bytes = static_cast<size_t>(n) * 0x2c;
    std::unique_ptr<uint8_t[]> raw(new uint8_t[bytes]());
    const bool ok = std::fread(raw.get(), 1, bytes, f) == bytes;
    for (int32_t k = 0; k < n; ++k) parse(t.blocks[static_cast<size_t>(block)][k], raw.get() + k * 0x2c);
    if (!ok) return error(0x23a, "Error reading GameZ Material buffer data.");
    if (base == 0) continue;
    for (int32_t k = 0; k < n; ++k) {
      const int32_t i = block * kMaterialBlock + k;
      push_front(t, (t.at(i).flags & 0x20) ? t.free_head : t.used_head, i);
    }
  }
  for (int32_t i = size + base; i < t.capacity; ++i) {  // the last block's rest
    t.at(i).flags |= 0x20;
    push_front(t, t.free_head, i);
  }
  for (int32_t i = base; i < t.capacity; ++i) {
    Material& m = t.at(i);
    if (m.flags & 0x20) continue;
    if (m.flags & 1) {
      m.texture = zcls::texture_for(static_cast<int32_t>(m.texture_index));
    } else {
      const float rgb[3] = {m.r, m.g, m.b};
      m.rgb = zvid::pack_rgb(rgb);
    }
    if (!(m.flags & 4)) continue;
    auto* c = static_cast<::zmod::Cycle*>(std::calloc(1, sizeof(::zmod::Cycle)));
    m.cycle = c;
    uint8_t raw[0x1c];
    if (std::fread(raw, 0x1c, 1, f) != 1) return error(0x278, "Error reading GameZ Material cycle texture data.");
    c->looping = at<uint32_t>(raw, 0), c->frame = at<uint32_t>(raw, 4);
    c->index = at<float>(raw, 8), c->speed = at<float>(raw, 0xc);
    c->texture_count = at<uint32_t>(raw, 0x10), c->added = at<uint32_t>(raw, 0x14);
    std::unique_ptr<int32_t[]> idx(new int32_t[c->texture_count ? c->texture_count : 1]);
    c->textures = static_cast<::zimg::TextureEntry**>(std::malloc(c->texture_count * sizeof(void*)));
    if (std::fread(idx.get(), static_cast<size_t>(c->texture_count) * 4, 1, f) != 1)
      return error(0x289, "Error reading GameZ Material cycle texture data.");
    for (uint32_t k = 0; k < c->texture_count; ++k) c->textures[k] = zcls::texture_for(idx[k]);
  }
  return base;
}

}  // namespace pm::zmod
