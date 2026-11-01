// The 1.2 data layer's c3 world (docs/patch12.md, sitting 6): 1.0's
// gamez.zbd rebuilt with what 1.2 changed, in 1.0's own order. The table
// (patch12_world.inc, tools/analysis/patch12_world.py) holds the models whose
// faces changed (1.0's polygons where 1.2 kept them, 1.2's new ones), one
// material's soil, one added material, and node record and data words.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gamez/zclass/cls_zbd.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/patch12.h"

namespace mech3::patch12 {
namespace {

struct WorldModel {
  int32_t slot;
  const char* header;  // 0x5c bytes, hex
  const char* points;  // everything before the polygons, hex; null: 1.0's
  const char* polys;   // @k or *hex, space separated
};
struct WorldMaterial {
  int32_t index, offset, old, now;
};
struct WorldWord {
  int32_t node, part, offset;  // part 0 the record, 1 the data
  uint32_t value;
};

#include "mech3/patch12_world.inc"

int32_t i32(const std::vector<uint8_t>& b, size_t at) {
  int32_t v;
  std::memcpy(&v, b.data() + at, 4);
  return v;
}
void put_i32(std::vector<uint8_t>& b, size_t at, int32_t v) { std::memcpy(b.data() + at, &v, 4); }
void put_i16(std::vector<uint8_t>& b, size_t at, int16_t v) { std::memcpy(b.data() + at, &v, 2); }

std::vector<uint8_t> unhex(const char* h) {
  std::vector<uint8_t> out;
  auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (; h && h[0] && h[1]; h += 2) out.push_back(static_cast<uint8_t>(nib(h[0]) << 4 | nib(h[1])));
  return out;
}

bool g_busy = false;

}  // namespace

bool parse_world(const std::vector<uint8_t>& b, ParsedWorld* w) {
  *w = ParsedWorld{};
  if (b.size() < 0x24 || i32(b, 0) != 0x02971222) return false;
  const int32_t mo = i32(b, 0x10), mdo = i32(b, 0x14), nas = i32(b, 0x18), no = i32(b, 0x20);
  w->materials = static_cast<size_t>(mo), w->models_at = static_cast<size_t>(mdo);
  w->nodes_at = static_cast<size_t>(no);
  const int32_t asz = i32(b, static_cast<size_t>(mo));
  std::vector<bool> textured(static_cast<size_t>(asz));
  for (int32_t i = 0; i < asz; ++i) textured[static_cast<size_t>(i)] = b[static_cast<size_t>(mo) + 16 + i * 0x2c + 1] & 1;
  size_t o = static_cast<size_t>(mdo);
  const int32_t masz = i32(b, o);
  o += 12;
  const size_t hdrs = o;
  o += static_cast<size_t>(masz) * 0x60;
  for (int32_t i = 0; i < masz; ++i) {
    ParsedWorld::Model m;
    m.header = hdrs + static_cast<size_t>(i) * 0x60;
    m.start = o;
    const int32_t poly = i32(b, m.header + 0x10), vert = i32(b, m.header + 0x14), nrm = i32(b, m.header + 0x18),
                  morph = i32(b, m.header + 0x1c), light = i32(b, m.header + 0x20);
    o += 12 * static_cast<size_t>(vert) + 12 * static_cast<size_t>(nrm) + 12 * static_cast<size_t>(morph);
    if (light > 0) {
      const size_t at = o;
      o += 0x4c * static_cast<size_t>(light);
      for (int32_t j = 0; j < light; ++j) o += 12 * static_cast<size_t>(i32(b, at + j * 0x4c + 0xc));
    }
    if (poly > 0) {
      const size_t recs = o;
      o += 0x24 * static_cast<size_t>(poly);
      for (int32_t j = 0; j < poly; ++j) {
        ParsedWorld::Poly p;
        p.record = recs + static_cast<size_t>(j) * 0x24;
        const uint32_t fl = static_cast<uint32_t>(i32(b, p.record));
        const int32_t k = static_cast<int32_t>(fl & 0xff), mi = i32(b, p.record + 0x1c);
        p.data = o;
        if (k) {
          o += 4 * static_cast<size_t>(k);
          if (fl & 0x200) o += 4 * static_cast<size_t>(k);
        }
        if (mi >= 0 && mi < asz && textured[static_cast<size_t>(mi)]) o += 8 * static_cast<size_t>(k);
        o += 12 * static_cast<size_t>(k);
        if (o > b.size()) return false;
        p.end = o;
        m.polys.push_back(p);
      }
    }
    m.end = o;
    w->models.push_back(m);
  }
  if (o != static_cast<size_t>(no)) return false;
  o = static_cast<size_t>(no) + static_cast<size_t>(nas) * 0xd4;
  static const int32_t kData[11] = {0, 0x1e8, 0xbc, 0xf8, 0x1c, 0x90, 0x50, 0, 0, 0xd0, 0x94};
  for (int32_t i = 0; i < nas; ++i) {
    ParsedWorld::NodeAt n;
    n.record = static_cast<size_t>(no) + static_cast<size_t>(i) * 0xd4;
    const int32_t t = i32(b, n.record + 0x34), pc = i32(b, n.record + 0x54), cc = i32(b, n.record + 0x5c);
    n.start = o;
    if (t) {
      if (t < 0 || t > 10 || !kData[t]) return false;
      const size_t d = o;
      o += static_cast<size_t>(kData[t]);
      if (o > b.size()) return false;
      if (t == 2) {
        const int32_t lc = i32(b, d + 0xa0), sc = i32(b, d + 0xac), xc = i32(b, d + 0x88), zc = i32(b, d + 0x8c);
        o += 4 * static_cast<size_t>(lc > 0 ? lc : 0) + 4 * static_cast<size_t>(sc > 0 ? sc : 0);
        for (int32_t p = 0; p < zc * xc; ++p) {
          int16_t nc;
          std::memcpy(&nc, b.data() + o + 0x3a, 2);
          o += 0x48 + (nc > 0 ? 4 * static_cast<size_t>(nc) : 0);
        }
      }
      if (t == 9 && i32(b, d + 200) > 0) o += 4 * static_cast<size_t>(i32(b, d + 200));
      if (t == 10 && i32(b, d + 0x8c) > 0) o += 4 * static_cast<size_t>(i32(b, d + 0x8c));
    }
    if (pc > 0) o += 4 * static_cast<size_t>(pc);
    if (cc > 0) o += 4 * static_cast<size_t>(cc);
    if (o > b.size()) return false;
    n.end = o;
    w->nodes.push_back(n);
  }
  return o == b.size();
}

bool rebuild_c3_world(std::vector<uint8_t>& data) {
  ParsedWorld w;
  if (!parse_world(data, &w)) return false;
  std::vector<uint8_t> b = data;
  // Materials: the soil fix, then 1.2's added one in the first free slot,
  // linked at the head of the used list (the reader's order, gamez.md 2).
  const size_t mats = w.materials + 16;
  for (const WorldMaterial& m : kWorldMaterials) {
    const size_t at = mats + static_cast<size_t>(m.index) * 0x2c + static_cast<size_t>(m.offset);
    if (i32(b, at) != m.old) return false;
    put_i32(b, at, m.now);
  }
  for (const char* hex : kWorldAddedMaterials) {
    if (!hex) continue;
    const int32_t size = i32(b, w.materials), count = i32(b, w.materials + 4), free_head = i32(b, w.materials + 8),
                  used_head = i32(b, w.materials + 12);
    if (free_head != count || count >= size) return false;
    const std::vector<uint8_t> m = unhex(hex);
    const size_t at = mats + static_cast<size_t>(count) * 0x2c;
    const int16_t next_free = static_cast<int16_t>(count + 1 < size ? count + 1 : -1);
    std::memcpy(b.data() + at, m.data(), 0x28);
    put_i16(b, at + 0x28, -1), put_i16(b, at + 0x2a, static_cast<int16_t>(used_head));
    put_i16(b, mats + static_cast<size_t>(used_head) * 0x2c + 0x28, static_cast<int16_t>(count));
    if (next_free >= 0) put_i16(b, mats + static_cast<size_t>(next_free) * 0x2c + 0x28, -1);
    put_i32(b, w.materials + 4, count + 1), put_i32(b, w.materials + 8, next_free);
    put_i32(b, w.materials + 12, count);
  }
  // Models: each in its slot, its payload rebuilt.
  std::vector<std::vector<uint8_t>> payload(w.models.size());
  std::vector<bool> replaced(w.models.size());
  for (const WorldModel& m : kWorldModels) {
    if (m.slot < 0 || static_cast<size_t>(m.slot) >= w.models.size()) return false;
    const ParsedWorld::Model& old = w.models[static_cast<size_t>(m.slot)];
    std::vector<uint8_t> out = m.points ? unhex(m.points)
                                        : std::vector<uint8_t>(data.begin() + static_cast<long>(old.start),
                                                               data.begin() + static_cast<long>(old.polys.empty() ? old.end : old.polys[0].record));
    std::vector<std::vector<uint8_t>> recs, datas;
    for (const char* p = m.polys; *p;) {
      while (*p == ' ') ++p;
      const char* end = p;
      while (*end && *end != ' ') ++end;
      if (*p == '@') {
        const size_t k = std::strtoul(p + 1, nullptr, 10);
        if (k >= old.polys.size()) return false;
        const ParsedWorld::Poly& q = old.polys[k];
        recs.emplace_back(data.begin() + static_cast<long>(q.record), data.begin() + static_cast<long>(q.record) + 0x24);
        datas.emplace_back(data.begin() + static_cast<long>(q.data), data.begin() + static_cast<long>(q.end));
      } else if (*p == '*') {
        const std::vector<uint8_t> x = unhex(std::string(p + 1, end).c_str());
        if (x.size() < 0x24) return false;
        recs.emplace_back(x.begin(), x.begin() + 0x24);
        datas.emplace_back(x.begin() + 0x24, x.end());
      }
      p = end;
    }
    for (const auto& r : recs) out.insert(out.end(), r.begin(), r.end());
    for (const auto& d : datas) out.insert(out.end(), d.begin(), d.end());
    const std::vector<uint8_t> h = unhex(m.header);
    if (h.size() != 0x5c) return false;
    std::memcpy(b.data() + old.header, h.data(), 0x5c);
    payload[static_cast<size_t>(m.slot)] = std::move(out);
    replaced[static_cast<size_t>(m.slot)] = true;
  }
  // Node words.
  for (const WorldWord& x : kWorldWords) {
    if (x.node < 0 || static_cast<size_t>(x.node) >= w.nodes.size()) return false;
    const ParsedWorld::NodeAt& n = w.nodes[static_cast<size_t>(x.node)];
    const size_t at = (x.part == 0 ? n.record : n.start) + static_cast<size_t>(x.offset);
    if (x.part == 1 && at + 4 > n.end) return false;
    put_i32(b, at, static_cast<int32_t>(x.value));
  }
  // The file again: up to the model payloads as it was (headers patched),
  // the payloads (offsets at +0x5c), then the nodes.
  const int32_t used = i32(b, w.models_at + 4);
  std::vector<uint8_t> out(b.begin(), b.begin() + static_cast<long>(w.models.empty() ? w.nodes_at : w.models[0].start));
  for (size_t i = 0; i < w.models.size(); ++i) {
    const ParsedWorld::Model& m = w.models[i];
    if (static_cast<int32_t>(i) < used) put_i32(out, m.header, static_cast<int32_t>(out.size()));
    if (replaced[i]) out.insert(out.end(), payload[i].begin(), payload[i].end());
    else out.insert(out.end(), b.begin() + static_cast<long>(m.start), b.begin() + static_cast<long>(m.end));
  }
  put_i32(out, 0x20, static_cast<int32_t>(out.size()));
  out.insert(out.end(), b.begin() + static_cast<long>(w.nodes_at), b.end());
  ParsedWorld check;
  if (!parse_world(out, &check)) return false;
  data = std::move(out);
  return true;
}

std::FILE* world_hook(const char* path, std::FILE* f) {
  if (g_busy || !path || !f) return f;
  std::string p;
  for (const char* c = path; *c; ++c) p += *c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
  if (p != "zbd/c3/gamez.zbd") return f;
  g_busy = true;
  const bool ten = is_10(p);
  g_busy = false;
  if (!ten) return f;
  std::vector<uint8_t> d;
  uint8_t buf[1 << 16];
  std::fseek(f, 0, SEEK_SET);
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
  if (!rebuild_c3_world(d)) {
    std::fseek(f, 0, SEEK_SET);
    return f;
  }
  std::FILE* t = std::tmpfile();
  if (!t) t = zutl::open("patch12_c3_gamez.tmp", "w+b");  // tmpfile can fail on Windows (the drive's root)
  if (!t || std::fwrite(d.data(), 1, d.size(), t) != d.size()) {
    if (t) std::fclose(t);
    std::fseek(f, 0, SEEK_SET);
    return f;
  }
  std::fclose(f);
  std::rewind(t);
  return t;
}

}  // namespace mech3::patch12
