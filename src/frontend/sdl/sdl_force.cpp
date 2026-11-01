#include "frontend/sdl/sdl_force.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gamez/zutil/zutl_path.h"
#include "mech3/patch12.h"

namespace sdlfe {

bool SdlForceDevice::parse(const std::vector<uint8_t>& d, std::vector<Record>* out) {
  out->clear();
  if (d.size() < 8 || std::memcmp(d.data(), "ifpr", 4) != 0) return false;
  size_t o = 8;
  auto u32 = [&](size_t at) {
    uint32_t v;
    std::memcpy(&v, d.data() + at, 4);
    return v;
  };
  while (o + 4 <= d.size()) {
    const uint32_t size = u32(o);
    if (size < 8 || o + size > d.size()) break;
    // name, type, 5 bytes, then key / value strings to the record's end
    std::vector<std::string> s;
    size_t p = o + 4;
    const size_t end = o + size;
    auto str = [&]() {
      std::string r;
      while (p < end && d[p]) r += static_cast<char>(d[p++]);
      ++p;
      return r;
    };
    Record r;
    r.name = str(), r.type = str();
    p += 5;
    while (p < end) {
      const std::string k = str();
      if (p > end) break;
      r.values[k] = str();
    }
    r.id = r.values["ID"];
    const std::string& c = r.values["ContainedObjects"];
    for (size_t a = 0; a < c.size();) {
      const size_t b = c.find(';', a);
      r.contains.push_back(c.substr(a, b == std::string::npos ? std::string::npos : b - a));
      if (b == std::string::npos) break;
      a = b + 1;
    }
    out->push_back(std::move(r));
    o += size;
  }
  return !out->empty();
}

bool SdlForceDevice::load(const std::string& path) {
  const std::string found = zutl::find(path.c_str());
  std::FILE* f = found.empty() ? nullptr : std::fopen(found.c_str(), "rb");
  if (!f) return false;
  std::vector<uint8_t> d;
  uint8_t buf[4096];
  for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.insert(d.end(), buf, buf + n);
  std::fclose(f);
  mech3::patch12::force_effects(d);  // 1.0's file: 1.2's effects (docs/patch12.md)
  return parse(d, &records_);
}

std::vector<int32_t> SdlForceDevice::create(const char* name) {
  std::string want = name ? name : "";
  std::replace(want.begin(), want.end(), '_', ' ');
  auto part_of = [this](const Record& r) {
    Part p;
    auto num = [&](const char* k) { auto it = r.values.find(k); return it == r.values.end() ? 0 : std::atoi(it->second.c_str()); };
    p.rumbles = r.type == "Periodic" || r.type == "Vector Force" || r.type == "Constant Force";
    p.fast = r.type == "Periodic" && num("Period") > 0 && num("Period") <= 100000;
    p.magnitude = std::abs(num("Magnitude"));
    p.infinite = num("InfiniteDuration") != 0;
    p.ms = static_cast<uint32_t>(std::max(0, num("Duration")) / 1000);
    parts_.push_back(p);
    return static_cast<int32_t>(parts_.size() - 1);
  };
  std::vector<int32_t> out;
  for (const Record& r : records_) {
    if (r.name != want) continue;
    if (r.contains.empty()) {
      out.push_back(part_of(r));
    } else {
      for (const std::string& id : r.contains)
        for (const Record& c : records_)
          if (c.id == id) out.push_back(part_of(c));
    }
    break;
  }
  return out;
}

void SdlForceDevice::set_gain(int32_t part, int32_t gain) {
  if (part >= 0 && part < static_cast<int32_t>(parts_.size())) parts_[static_cast<size_t>(part)].gain = gain;
}
void SdlForceDevice::set_direction(int32_t, int32_t) {}  // a rumble has no direction

void SdlForceDevice::start(int32_t part) {
  if (part < 0 || part >= static_cast<int32_t>(parts_.size())) return;
  Part& p = parts_[static_cast<size_t>(part)];
  p.until = p.infinite ? UINT64_MAX : now_ms() + p.ms;
  apply();
}

void SdlForceDevice::stop(int32_t part) {
  if (part < 0 || part >= static_cast<int32_t>(parts_.size())) return;
  parts_[static_cast<size_t>(part)].until = 0;
  apply();
}

// The parts still playing: the strongest on each motor, for as long as the
// longest has left.
void SdlForceDevice::apply() {
  const uint64_t now = now_ms();
  Rumble r;
  uint64_t until = now;
  for (Part& p : parts_) {
    if (p.until && p.until <= now) p.until = 0;
    if (!p.until || !p.rumbles) continue;
    const auto level = static_cast<uint16_t>(std::min<int64_t>(0xffff, int64_t(p.magnitude) * p.gain / 10000 * 0xffff / 10000));
    (p.fast ? r.high : r.low) = std::max(p.fast ? r.high : r.low, level);
    until = std::max(until, p.until);
  }
  r.ms = until == UINT64_MAX ? 0xffffffffu : static_cast<uint32_t>(std::min<uint64_t>(until - now, 0xffffffffu));
  if (!r.low && !r.high) r.ms = 0;
  last_ = r;
  if (SDL_Joystick* j = joystick_ ? joystick_() : nullptr) SDL_RumbleJoystick(j, r.low, r.high, r.ms);
}

}  // namespace sdlfe
