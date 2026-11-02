#include "pm/gamez/zmodel/gmod_const.h"

#include <cstdlib>

#include "crt/msvc_math.h"
#include "gamez/zerror/zerr.h"
#include "gamez/zvideo/zvid_format.h"

namespace pm::zmod {

namespace {
const char kFile[] = "C:\\zipper\\branchep1\\gamez\\zModel\\gmod_const.c";
ModelPool g_pool;

// malloc and an unchecked fread, as the exe; zeroed, so a short read leaves
// zeros (the tests' allocator gives the exe zeroed memory too).
template <typename T>
T* read_array(std::FILE* f, size_t bytes) {
  T* p = static_cast<T*>(std::calloc(bytes ? bytes : 1, 1));
  std::fread(p, bytes, 1, f);
  return p;
}

::zmod::Material* take(int32_t index, int32_t base) {
  if (index >= 0) index += base;
  return ::zmod::take_material(index);  // 0x00555a90: flag 0x10
}
}  // namespace

ModelPool& models() { return g_pool; }

// Pirate's Moon 1.0 @ 0x005587e0. MW3's 0x00538a70 with PM's polygons: the
// material lists, UVs per material, the model's material refs.
void read_model_data(std::FILE* f, Model& m, int32_t base) {
  if (m.count(0x14) > 0) m.vertices = read_array<float>(f, static_cast<size_t>(m.count(0x14)) * 12);
  if (m.count(0x18) > 0) m.normals = read_array<float>(f, static_cast<size_t>(m.count(0x18)) * 12);
  if (m.count(0x1c) > 0) m.morphs = read_array<float>(f, static_cast<size_t>(m.count(0x1c)) * 12);
  if (const int32_t n = m.count(0x20); n > 0) {
    m.lights = static_cast<::zmod::Light*>(std::calloc(static_cast<size_t>(n), sizeof(::zmod::Light)));
    for (int32_t i = 0; i < n; ++i) std::fread(m.lights[i].raw, 0x4c, 1, f);  // one read of n * 0x4c in the exe
    for (int32_t i = 0; i < n; ++i) {
      ::zmod::Light& l = m.lights[i];
      float rgb[3];
      std::memcpy(rgb, l.raw + 0x1c, 12);
      const uint8_t b = static_cast<uint8_t>(crt::msvc_ftol(rgb[2] + 0.5f));  // [0x005de818] 0.5: rounds
      const uint8_t g = static_cast<uint8_t>(crt::msvc_ftol(rgb[1] + 0.5f));
      const uint8_t r = static_cast<uint8_t>(crt::msvc_ftol(rgb[0] + 0.5f));
      l.color = zvid::pack_bytes(r, g, b);  // 0x005983e0
      int32_t k;
      std::memcpy(&k, l.raw + 0x0c, 4);
      if (k > 0) l.vertices = read_array<float>(f, static_cast<size_t>(k) * 12);
    }
  }
  const int32_t polys = m.count(0x10);
  if (polys > 0) {
    uint8_t* raw = read_array<uint8_t>(f, static_cast<size_t>(polys) * 0x28);
    m.polygons = new Polygon[static_cast<size_t>(polys)]{};
    for (int32_t i = 0; i < polys; ++i) {
      const uint8_t* p = raw + i * 0x28;
      Polygon& q = m.polygons[i];
      std::memcpy(&q.flags, p, 4), std::memcpy(&q.priority, p + 4, 4);
      std::memcpy(&q.material_count, p + 0x10, 4), std::memcpy(&q.file_uvs, p + 0x18, 4);
      std::memcpy(&q.zone_set, p + 0x24, 4);
    }
    std::free(raw);
    std::vector<std::vector<int32_t>> indices(static_cast<size_t>(polys));
    for (int32_t i = 0; i < polys; ++i) {
      Polygon& q = m.polygons[i];
      const uint32_t k = q.flags & 0x3ff;
      if (k) {
        q.vertex_indices = read_array<uint32_t>(f, k * 4);
        if (q.flags & 0x1000) q.normal_indices = read_array<uint32_t>(f, k * 4);
      }
      const size_t n = static_cast<size_t>(q.material_count > 0 ? q.material_count : 0);
      indices[static_cast<size_t>(i)].assign(n, 0);
      std::fread(indices[static_cast<size_t>(i)].data(), static_cast<size_t>(q.material_count) * 4, 1, f);
      if (q.file_uvs) {
        q.uvs = static_cast<float**>(std::calloc(n ? n : 1, sizeof(float*)));
        for (size_t j = 0; j < n; ++j) q.uvs[j] = read_array<float>(f, k * 8);
      }
      q.colors = read_array<float>(f, k * 12);
      q.scratch = static_cast<uint32_t*>(std::calloc(k ? k : 1, 4));
    }
    for (int32_t i = 0; i < polys; ++i) {
      Polygon& q = m.polygons[i];
      const std::vector<int32_t>& idx = indices[static_cast<size_t>(i)];
      q.materials = static_cast<::zmod::Material**>(std::calloc(idx.empty() ? 1 : idx.size(), sizeof(void*)));
      for (size_t j = 0; j < idx.size(); ++j) q.materials[j] = take(idx[j], base);
    }
  }
  if (const int32_t n = m.count(0x5c); n > 0) {
    uint8_t* raw = read_array<uint8_t>(f, static_cast<size_t>(n) * 12);
    m.refs = new MaterialRef[static_cast<size_t>(n)]{};
    int32_t first = 0;
    for (int32_t i = 0; i < n; ++i) {
      int32_t index;
      std::memcpy(&index, raw + i * 12, 4), std::memcpy(&m.refs[i].usage_count, raw + i * 12 + 4, 4);
      m.refs[i].material = take(index, base);
      m.refs[i].polygon = m.polygons ? m.polygons + first : nullptr;  // the exe: +0x34 plus first * 0x28
      first += m.refs[i].usage_count;
    }
    std::free(raw);
  }
  // The exe then converts the model for the renderer: under Direct3D
  // (0x00572ae0) one without flag 0x80 into strips (0x00570b70), in software
  // one with it back (0x005726f0). Left out: the file's form is kept
  // (docs/pm-map.md, the models' form).
}

// Pirate's Moon 1.0 @ 0x00558ba0, MW3's 0x00538e30 appending.
int read_models(std::FILE* f, int32_t material_base) {
  ModelPool& p = g_pool;
  const int32_t base = p.capacity;
  int32_t size, count, free_head;
  int line = 0;
  if (std::fread(&size, 4, 1, f) != 1) line = 0x2e1;
  else if (std::fread(&count, 4, 1, f) != 1) line = 0x2ee;
  else if (std::fread(&free_head, 4, 1, f) != 1) line = 0x2fb;
  if (line) {
    zerr::report(0x200, kFile, line, "Error reading GameZ Model3D buffer data.");
    zerr::report(0x200, kFile, 0x594, "Error reading GameZ Model3D buffer header data.");
    return -1;
  }
  if (size == 0) return 0;
  if (base == 0) p.count = count, p.free_head = free_head;
  for (int32_t done = 0; done < size; done += p.block) {
    const int32_t b = static_cast<int32_t>(p.blocks.size());
    p.blocks.emplace_back(new Model[static_cast<size_t>(p.block)]{});
    p.capacity += p.block;
    const int32_t n = size < done + p.block ? size - done : p.block;
    const size_t bytes = static_cast<size_t>(n) * 0x68;
    std::unique_ptr<uint8_t[]> raw(new uint8_t[bytes]());
    const bool ok = std::fread(raw.get(), 1, bytes, f) == bytes;  // a short read keeps what it read
    for (int32_t k = 0; k < n; ++k) {
      Model& m = p.blocks[static_cast<size_t>(b)][k];
      std::memcpy(m.header, raw.get() + k * 0x68, 0x64);
      std::memcpy(&m.link, raw.get() + k * 0x68 + 0x64, 4);
    }
    if (!ok) {
      zerr::report(0x200, kFile, 0x5c9, "Error reading GameZ Model3D buffer data.");
      return -1;
    }
    if (base == 0) continue;
    for (int32_t k = 0; k < n; ++k) {  // a later file's empty slots onto the free list
      Model& m = p.blocks[static_cast<size_t>(b)][k];
      if (m.count(0xc) != 0) continue;
      m.link = p.free_head;
      p.free_head = b * p.block + k;
    }
  }
  for (int32_t i = size + base; i < p.capacity; ++i) {  // the last block's rest
    Model& m = p.at(i);
    m = Model{};
    m.link = p.free_head;
    p.free_head = i;
  }
  static const uint8_t kZero[0x64] = {};
  for (int32_t i = base; i < size + base; ++i) {
    Model& m = p.at(i);
    if (m.count(0xc) < 1 && std::memcmp(m.header, kZero, 0x64) != 0)
      zerr::report(0x200, kFile, 0x606, "Reading GameZ Model3D buffer data for model that has no parents.");
    read_model_data(f, m, material_base);
  }
  return base;
}

}  // namespace pm::zmod
