// The 1.2 data layer's force_eff.ifr (docs/patch12.md): 1.2 softens the
// water's damper, takes the jets' second shake out of phase, renames 1.0's
// "impact fall" (the landing bump) "impact land", and adds a real fall and
// a death shake. The new effects are written here with our own IDs.
#include <cstring>

#include "mech3/patch12.h"

namespace mech3::patch12 {
namespace {

void put32(std::vector<uint8_t>& d, uint32_t v) {
  for (int i = 0; i < 4; ++i) d.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

void put(std::vector<uint8_t>& d, const std::string& s) {
  d.insert(d.end(), s.begin(), s.end());
  d.push_back(0);
}

std::string* find(IfrRecord* r, const char* key) {
  for (auto& kv : r->values)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

// Sets `key` from `was` to `now`; false when it is not `was`.
bool set(IfrRecord* r, const char* key, const char* was, const char* now) {
  std::string* v = r ? find(r, key) : nullptr;
  if (!v || *v != was) return false;
  *v = now;
  return true;
}

IfrRecord* named(std::vector<IfrRecord>& rs, const char* name) {
  for (IfrRecord& r : rs)
    if (r.name == name) return &r;
  return nullptr;
}

// A periodic effect's values (microseconds; magnitudes and levels 0..10000;
// directions in hundredths of a degree).
IfrRecord periodic(const char* name, const char* id, const char* period, const char* magnitude,
                   const char* duration, const char* direction, const char* attack_level,
                   const char* attack_duration, const char* fade_level, const char* fade_duration) {
  return {name, "Periodic", {0, 0x2e, 0, 0, 0},
          {{"ID", id}, {"Period", period}, {"Magnitude", magnitude}, {"Waveform", "0"}, {"Duration", duration},
           {"Phase", "0"}, {"FadeDuration", fade_duration}, {"FadeLevel", fade_level}, {"Direction", direction},
           {"AttackLevel", attack_level}, {"InfiniteDuration", "0"}, {"AttackDuration", attack_duration}}};
}

}  // namespace

bool read_ifr(const std::vector<uint8_t>& d, std::vector<IfrRecord>* out) {
  out->clear();
  if (d.size() < 8 || std::memcmp(d.data(), "ifpr", 4) != 0) return false;
  size_t o = 8;
  while (o + 4 <= d.size()) {
    uint32_t size;
    std::memcpy(&size, d.data() + o, 4);
    if (size < 8 || o + size > d.size()) break;
    const size_t end = o + size;
    size_t p = o + 4;
    auto str = [&] {
      std::string s;
      while (p < end && d[p]) s += static_cast<char>(d[p++]);
      ++p;
      return s;
    };
    IfrRecord r;
    r.name = str(), r.type = str();
    for (int i = 0; i < 5 && p < end; ++i) r.head[i] = d[p++];
    while (p < end) {
      std::string k = str();
      if (p > end) break;
      r.values.emplace_back(std::move(k), str());
    }
    out->push_back(std::move(r));
    o = end;
  }
  return !out->empty();
}

std::vector<uint8_t> write_ifr(const std::vector<IfrRecord>& rs) {
  std::vector<uint8_t> d = {'i', 'f', 'p', 'r'};
  put32(d, static_cast<uint32_t>(rs.size()));
  for (const IfrRecord& r : rs) {
    std::vector<uint8_t> body;
    put(body, r.name), put(body, r.type);
    body.insert(body.end(), r.head, r.head + 5);
    for (const auto& kv : r.values) put(body, kv.first), put(body, kv.second);
    put32(d, static_cast<uint32_t>(body.size() + 4));
    d.insert(d.end(), body.begin(), body.end());
  }
  return d;
}

bool apply_force(std::vector<uint8_t>& data) {
  std::vector<IfrRecord> rs;
  if (!read_ifr(data, &rs) || named(rs, "impact land")) return false;
  IfrRecord *jets2 = named(rs, "jets2"), *water = named(rs, "water"), *fall = named(rs, "impact fall");
  bool ok = set(jets2, "Phase", "9000", "0");
  for (const char* k : {"PosSaturation", "NegSaturation"}) ok = ok && set(water, k, "10000", "5000");
  for (const char* k : {"PosCoefficient", "NegCoefficient", "PosCoefficient2", "NegCoefficient2"})
    ok = ok && set(water, k, "9500", "10000");
  ok = ok && fall && set(fall, "Magnitude", "6124", "6124");
  if (!ok) return false;
  fall->name = "impact land";  // 1.0's landing bump keeps its values
  rs.push_back(periodic("impact fall", "{6D573301-0012-4C4D-8001-000000000001}", "850000", "10000", "2750000",
                        "0", "1472", "1702325", "8294", "362790"));
  rs.push_back({"dead spring", "Spring", {0, 0x2e, 0, 0, 0},
                {{"ID", "{6D573301-0012-4C4D-8001-000000000002}"}, {"Offset", "0"}, {"PosSaturation", "10000"},
                 {"Axes", "2"}, {"DeadBand2", "1000"}, {"NegCoefficient", "-4000"}, {"PosCoefficient2", "-4000"},
                 {"NegSaturation", "10000"}, {"BothAxes", "0"}, {"InfiniteDuration", "1"}, {"DeadBand", "1000"},
                 {"Axis", "0"}, {"Symmetry", "0"}, {"NegCoefficient2", "-4000"}, {"PosCoefficient", "-4000"}}});
  rs.push_back({"dead dance", "Compound", {0, 0x8d, 0, 0, 0},
                {{"ContainedObjects", "{6D573301-0012-4C4D-8001-000000000004};{6D573301-0012-4C4D-8001-000000000005}"},
                 {"ID", "{6D573301-0012-4C4D-8001-000000000003}"}}});
  rs.push_back(periodic("dead dance2", "{6D573301-0012-4C4D-8001-000000000004}", "310000", "10000", "4000000", "0",
                        "0", "632558", "0", "1562790"));
  rs.push_back(periodic("dead dance1", "{6D573301-0012-4C4D-8001-000000000005}", "250000", "8139", "4000000",
                        "9000", "0", "706976", "77", "1637209"));
  data = write_ifr(rs);
  return true;
}

bool force_effects(std::vector<uint8_t>& data) {
  return is_10("force_eff.ifr") && apply_force(data);
}

}  // namespace mech3::patch12
