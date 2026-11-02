#include "mech3/movie_shape.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace mech3 {

namespace {
// A channel's top bits under its mask, as 8 bits.
int channel(uint32_t px, uint32_t mask) {
  if (!mask) return 0;
  int lo = 0, bits = 0;
  while (!(mask >> lo & 1)) ++lo;
  while (mask >> (lo + bits) & 1) ++bits;
  const uint32_t v = (px & mask) >> lo;
  if (bits >= 8) return static_cast<int>(v >> (bits - 8));
  return static_cast<int>(v << (8 - bits) | (bits >= 4 ? v >> (2 * bits - 8) : 0));  // the top bits again below
}
constexpr int kGridW = 8, kGridH = 6;
constexpr int kSceneChange = 28;  // a mean change of the grid's luma this big between two frames is a cut
constexpr int kBar = 8;           // the least bar above and below a letterboxed picture
}  // namespace

MovieFrameScan movie_scan_frame(const uint16_t* pixels, int32_t width, int32_t height, int32_t pitch, uint32_t r,
                                uint32_t g, uint32_t b) {
  MovieFrameScan s;
  if (!pixels || width <= 0 || height <= 0) return s;
  std::vector<uint8_t> luma(static_cast<size_t>(width) * height);
  int64_t cells[kGridW * kGridH] = {};
  for (int32_t y = 0; y < height; ++y) {
    const auto* row = reinterpret_cast<const uint16_t*>(reinterpret_cast<const uint8_t*>(pixels) + static_cast<size_t>(y) * pitch);
    uint8_t* out = &luma[static_cast<size_t>(y) * width];
    int64_t sum = 0;
    const int cy = y * kGridH / height;
    for (int32_t x = 0; x < width; ++x) {
      const uint32_t p = row[x];
      out[x] = static_cast<uint8_t>((channel(p, r) * 77 + channel(p, g) * 150 + channel(p, b) * 29) >> 8);
      sum += out[x];
      cells[cy * kGridW + x * kGridW / width] += out[x];
    }
    if (sum / width > kMovieLit) {
      if (s.top < 0) s.top = static_cast<int16_t>(y);
      s.bottom = static_cast<int16_t>(y);
    }
  }
  const int64_t per = static_cast<int64_t>(width / kGridW) * (height / kGridH);
  for (int i = 0; i < kGridW * kGridH; ++i) s.grid[i] = static_cast<uint8_t>(std::min<int64_t>(255, per ? cells[i] / per : 0));
  return s;
}

std::vector<MovieShapeRun> movie_shape_runs(const std::vector<MovieFrameScan>& frames, int32_t width, int32_t height,
                                            int32_t fps) {
  (void)width;
  struct Shot {
    int32_t start = 0;
    bool lit = false;
    int16_t top = 0, bottom = 0;
  };
  const int32_t n = static_cast<int32_t>(frames.size());
  if (n == 0) return {};
  auto black = [&](int32_t i) { return frames[static_cast<size_t>(i)].top < 0; };
  // The shots: a new one at a scene change and where black starts or ends;
  // each its frames' lit rows together.
  std::vector<Shot> shots;
  for (int32_t i = 0; i < n; ++i) {
    bool cut = i == 0 || black(i) != black(i - 1);
    if (!cut) {
      int diff = 0;
      for (int k = 0; k < kGridW * kGridH; ++k)
        diff += std::abs(frames[static_cast<size_t>(i)].grid[k] - frames[static_cast<size_t>(i - 1)].grid[k]);
      cut = diff / (kGridW * kGridH) > kSceneChange;
    }
    if (cut) {
      Shot s;
      s.start = i;
      shots.push_back(s);
    }
    const MovieFrameScan& f = frames[static_cast<size_t>(i)];
    Shot& s = shots.back();
    if (f.top < 0) continue;
    s.top = s.lit ? std::min(s.top, f.top) : f.top;
    s.bottom = s.lit ? std::max(s.bottom, f.bottom) : f.bottom;
    s.lit = true;
  }
  // Letterboxed rows: bars of at least kBar rows and the picture no more
  // than 7/8 of the height (a picture nearly 4:3 is shown as it is).
  auto boxed = [&](int16_t t, int16_t b) { return t >= kBar && b <= height - 1 - kBar && (b - t + 1) * 8 <= height * 7; };
  // The runs: a shot joins the run before it when the run's rows with its
  // own are still letterboxed (or both are whole); else a new run starts at
  // it. A black shot joins whatever is on.
  std::vector<MovieShapeRun> runs;
  for (const Shot& s : shots) {
    if (!s.lit) continue;  // leading black goes with the first lit shot's run, which starts at 0
    if (!runs.empty()) {
      MovieShapeRun& r = runs.back();
      if (!r.whole && boxed(std::min(r.top, s.top), std::max(r.bottom, s.bottom))) {
        r.top = std::min(r.top, s.top), r.bottom = std::max(r.bottom, s.bottom);
        continue;
      }
      if (r.whole && !boxed(s.top, s.bottom)) continue;
    }
    MovieShapeRun r;
    r.start = runs.empty() ? 0 : s.start;
    r.whole = !boxed(s.top, s.bottom);
    r.top = r.whole ? 0 : s.top, r.bottom = r.whole ? static_cast<int16_t>(height - 1) : s.bottom;
    runs.push_back(r);
  }
  if (runs.empty()) {  // all black
    MovieShapeRun r;
    r.bottom = static_cast<int16_t>(height - 1);
    return {r};
  }
  // A letterboxed run shorter than 2 s is shown whole (no shape for a
  // moment); whole runs then side by side are one.
  std::vector<MovieShapeRun> held;
  for (size_t i = 0; i < runs.size(); ++i) {
    MovieShapeRun r = runs[i];
    const int32_t end = i + 1 < runs.size() ? runs[i + 1].start : n;
    if (!r.whole && end - r.start < 2 * std::max(fps, 1)) r.whole = true, r.top = 0, r.bottom = static_cast<int16_t>(height - 1);
    if (!held.empty() && held.back().whole && r.whole) continue;
    held.push_back(r);
  }
  return held;
}

int32_t movie_shape_run_of(const std::vector<MovieShapeRun>& runs, int32_t f) {
  int32_t at = -1;
  for (size_t i = 0; i < runs.size() && runs[i].start <= f; ++i) at = static_cast<int32_t>(i);
  return at;
}

bool MovieShapePlay::rows(const std::vector<MovieShapeRun>* runs, int32_t f, int16_t* top, int16_t* bottom) {
  if (!started_) started_ = true, waiting_ = !runs;
  if (!runs) return false;
  const int32_t r = movie_shape_run_of(*runs, f);
  if (r < 0) return false;
  if (waiting_) {
    if (wait_run_ == -2) wait_run_ = r;
    if (r == wait_run_) return false;  // whole until the next shape change
    waiting_ = false;
  }
  const MovieShapeRun& run = (*runs)[static_cast<size_t>(r)];
  if (run.whole) return false;
  *top = run.top, *bottom = run.bottom;
  return true;
}

std::string movie_shape_line(const std::string& key, const std::vector<MovieShapeRun>& runs) {
  std::ostringstream o;
  o << key;
  for (const MovieShapeRun& r : runs) {
    o << ' ' << r.start << ':';
    if (r.whole) o << "whole";
    else o << r.top << '-' << r.bottom;
  }
  return o.str();
}

namespace {
// A whole decimal number, nothing else; -1 when it is not one.
int32_t number(const std::string& s) {
  if (s.empty() || s.size() > 9) return -1;
  for (char c : s)
    if (c < '0' || c > '9') return -1;
  return std::atoi(s.c_str());
}
}  // namespace

bool movie_shape_parse(const std::string& line, std::string* key, std::vector<MovieShapeRun>* runs) {
  std::istringstream in(line);
  if (!(in >> *key)) return false;
  runs->clear();
  for (std::string w; in >> w;) {
    MovieShapeRun r;
    const size_t c = w.find(':');
    if (c == std::string::npos || (r.start = number(w.substr(0, c))) < 0) return false;
    const std::string s = w.substr(c + 1);
    const size_t d = s.find('-');
    if (s == "whole") {
      r.whole = true;
    } else if (d != std::string::npos) {
      const int32_t t = number(s.substr(0, d)), b = number(s.substr(d + 1));
      if (t < 0 || b < t || b > 32767) return false;
      r.whole = false, r.top = static_cast<int16_t>(t), r.bottom = static_cast<int16_t>(b);
    } else {
      return false;
    }
    if (!runs->empty() && r.start <= runs->back().start) return false;
    runs->push_back(r);
  }
  return !runs->empty() && runs->front().start == 0;
}

}  // namespace mech3
