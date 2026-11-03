// The movies' shapes on a wide screen (mech3/movie_shape, docs/fmv.md
// section 8): a frame's lit rows, the runs a movie is cut into, the play's
// rule while a shape is not known yet, the cache's lines, the store (the
// built-in table, the cache over it, measuring only between plays).
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "mech3/movie_shape.h"

namespace {
using mech3::MovieFrameScan;
using mech3::MovieShapeRun;

// A frame lit on rows top..bottom (-1: black), its grid a flat `scene` value.
MovieFrameScan frame(int top, int bottom, int scene) {
  MovieFrameScan f;
  f.top = static_cast<int16_t>(top), f.bottom = static_cast<int16_t>(bottom);
  for (uint8_t& g : f.grid) g = static_cast<uint8_t>(top < 0 ? 0 : scene);
  return f;
}
void add(std::vector<MovieFrameScan>& v, int n, int top, int bottom, int scene) {
  for (int i = 0; i < n; ++i) v.push_back(frame(top, bottom, scene));
}
MovieShapeRun run(int start, int top = -1, int bottom = -1) {
  MovieShapeRun r;
  r.start = start;
  r.whole = top < 0;
  r.top = static_cast<int16_t>(top < 0 ? 0 : top), r.bottom = static_cast<int16_t>(top < 0 ? 479 : bottom);
  return r;
}
std::string line(const std::vector<MovieShapeRun>& runs) { return mech3::movie_shape_line("m", runs); }
}  // namespace

TEST(movie_shape_frame_rows_from_the_pixels) {
  // 64 x 48, RGB 565: rows 10..37 grey (mean luma about 128), a dim row 9
  // (a few lit pixels: mean under 20), the rest black.
  const int w = 64, h = 48;
  std::vector<uint16_t> px(static_cast<size_t>(w) * h, 0);
  for (int y = 10; y <= 37; ++y)
    for (int x = 0; x < w; ++x) px[static_cast<size_t>(y) * w + x] = 0x8410;
  for (int x = 0; x < 4; ++x) px[static_cast<size_t>(9) * w + x] = 0xffff;
  const MovieFrameScan s = mech3::movie_scan_frame(px.data(), w, h, w * 2, 0xf800, 0x07e0, 0x001f);
  EXPECT(s.top == 10 && s.bottom == 37);
  EXPECT(s.grid[0] == 0 && s.grid[8 * 2 + 3] > 100);  // the first band black, the third grey
  std::vector<uint16_t> dark(static_cast<size_t>(w) * h, 0);
  const MovieFrameScan d = mech3::movie_scan_frame(dark.data(), w, h, w * 2, 0xf800, 0x07e0, 0x001f);
  EXPECT(d.top == -1 && d.bottom == -1);
  // The same picture as 555 under its own masks.
  for (uint16_t& p : px) p = p ? static_cast<uint16_t>(0x4210 | (p == 0xffff ? 0x7fff : 0)) : 0;
  const MovieFrameScan s5 = mech3::movie_scan_frame(px.data(), w, h, w * 2, 0x7c00, 0x03e0, 0x001f);
  EXPECT(s5.top == 10 && s5.bottom == 37);
}

TEST(movie_shape_runs_follow_the_movie) {
  const int w = 640, h = 480, fps = 15;
  // Black at the start, then a letterboxed movie whose shots light different
  // rows: one run from frame 0, its rows the union (nothing cropped off).
  std::vector<MovieFrameScan> a;
  add(a, 20, -1, -1, 0);
  add(a, 60, 88, 392, 40);
  add(a, 60, 79, 400, 120);
  add(a, 5, -1, -1, 0);
  add(a, 60, 100, 380, 60);
  EXPECT(line(mech3::movie_shape_runs(a, w, h, fps)) == line({run(0, 79, 400)}));
  // A 4:3 movie: whole.
  std::vector<MovieFrameScan> b;
  add(b, 100, 0, 479, 50);
  EXPECT(line(mech3::movie_shape_runs(b, w, h, fps)) == line({run(0)}));
  // Letterboxed, then a full-frame shot at a cut, then letterboxed again:
  // three runs, each starting at its cut.
  std::vector<MovieFrameScan> c;
  add(c, 60, 120, 359, 40);
  add(c, 45, 21, 454, 140);
  add(c, 60, 120, 359, 40);
  EXPECT(line(mech3::movie_shape_runs(c, w, h, fps)) == line({run(0, 120, 359), run(60), run(105, 120, 359)}));
  // A taller picture with no cut before it (one shot) is the shot's: whole
  // from its start, never cropped mid-shot.
  std::vector<MovieFrameScan> d;
  add(d, 60, 120, 359, 40);
  add(d, 30, 21, 454, 40);
  EXPECT(line(mech3::movie_shape_runs(d, w, h, fps)) == line({run(0)}));
  // A letterboxed stretch under 2 s is shown whole (no shape for a moment).
  std::vector<MovieFrameScan> e;
  add(e, 60, 0, 479, 40);
  add(e, 20, 120, 359, 140);
  add(e, 60, 0, 479, 40);
  EXPECT(line(mech3::movie_shape_runs(e, w, h, fps)) == line({run(0)}));
  // Bars under 8 rows, or a picture over 7/8 of the height: whole.
  std::vector<MovieFrameScan> f;
  add(f, 60, 6, 470, 40);
  EXPECT(line(mech3::movie_shape_runs(f, w, h, fps)) == line({run(0)}));
  std::vector<MovieFrameScan> g;
  add(g, 60, 24, 449, 40);  // 426 rows of 480
  EXPECT(line(mech3::movie_shape_runs(g, w, h, fps)) == line({run(0)}));
  // All black, and nothing: whole, and no runs.
  std::vector<MovieFrameScan> k;
  add(k, 30, -1, -1, 0);
  EXPECT(line(mech3::movie_shape_runs(k, w, h, fps)) == line({run(0)}));
  EXPECT(mech3::movie_shape_runs({}, w, h, fps).empty());
}

TEST(movie_shape_play_never_switches_mid_shot) {
  const std::vector<MovieShapeRun> runs = {run(0, 120, 359), run(60), run(105, 120, 359)};
  int16_t t = 0, b = 0;
  // Known from the first frame: each frame its run's.
  mech3::MovieShapePlay known;
  EXPECT(known.rows(&runs, 0, &t, &b) && t == 120 && b == 359);
  EXPECT(!known.rows(&runs, 70, &t, &b));
  EXPECT(known.rows(&runs, 110, &t, &b) && t == 120);
  // Not known at the start, known at frame 10: whole up to the next run (60),
  // which is whole anyway, then the third run's rows.
  mech3::MovieShapePlay late;
  EXPECT(!late.rows(nullptr, 0, &t, &b));
  EXPECT(!late.rows(nullptr, 5, &t, &b));
  EXPECT(!late.rows(&runs, 10, &t, &b));
  EXPECT(!late.rows(&runs, 59, &t, &b));
  EXPECT(!late.rows(&runs, 60, &t, &b));
  EXPECT(late.rows(&runs, 105, &t, &b) && t == 120 && b == 359);
  // Known in the whole run: the letterboxed run after it is shown cropped.
  mech3::MovieShapePlay later;
  EXPECT(!later.rows(nullptr, 0, &t, &b));
  EXPECT(!later.rows(&runs, 70, &t, &b));
  EXPECT(later.rows(&runs, 106, &t, &b) && t == 120);
}

TEST(movie_shape_cache_lines) {
  const std::vector<MovieShapeRun> runs = {run(0, 98, 378), run(1022)};
  const std::string l = mech3::movie_shape_line("c4win/1083/640x480", runs);
  EXPECT(l == "c4win/1083/640x480 0:98-378 1022:whole");
  std::string key;
  std::vector<MovieShapeRun> back;
  EXPECT(mech3::movie_shape_parse(l, &key, &back) && key == "c4win/1083/640x480" && back == runs);
  for (const char* bad : {"", "k", "k 5:whole", "k 0:whole 0:whole", "k 0:9-3", "k 0:wide", "k 0:-4-9", "k 0"})
    EXPECT(!mech3::movie_shape_parse(bad, &key, &back));
}

namespace {
// A fresh cache path in the test's temp dir.
std::string shapes_cache(const char* name, const char* text = nullptr) {
  const std::string dir = std::string(MW3_TEST_TMP) + "/movie_shapes";
  std::filesystem::create_directories(dir);
  const std::string path = dir + "/" + name + ".txt";
  std::filesystem::remove(path);
  if (text) std::ofstream(path) << text;
  return path;
}
// A measurement of `frames` frames, each through the gate; counts what it did.
struct FakeMeasure {
  std::atomic<int> calls{0}, frames_done{0};
  int frames = 30;
  mech3::MovieShapeStore::Measure fn() {
    return [this](const mech3::MovieScanGate& gate, std::vector<MovieFrameScan>* f, int32_t* w, int32_t* h) {
      ++calls;
      *w = 640, *h = 480;
      for (int i = 0; i < frames; ++i) {
        if (!gate()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        f->push_back(frame(80, 400, 50));
        ++frames_done;
      }
      return true;
    };
  }
};
}  // namespace

// Every shipped movie is in the table (19, the same in 1.0 and 1.2 and on
// the disc): opened, it is known at once and nothing is measured.
TEST(movie_shape_stock_movies_start_with_no_measurement) {
  EXPECT(mech3::movie_shape_table().size() == 19);
  mech3::MovieShapeStore store(shapes_cache("stock"));
  FakeMeasure m;
  for (const char* key : {"intro/2737/640x480", "c4win/1083/640x480", "c3m5/751/640x480", "c1m1/690/640x480"}) {
    store.open(key, 15, m.fn());
    EXPECT(store.known(key) && !store.measuring());
    store.play_end();
  }
  EXPECT(m.calls == 0);
  mech3::MovieShapePlay play;
  int16_t t = 0, b = 0;
  EXPECT(store.rows("intro/2737/640x480", &play, 0, &t, &b) && t == 79 && b == 400);
  mech3::MovieShapePlay c4;
  EXPECT(store.rows("c4win/1083/640x480", &c4, 0, &t, &b) && t == 98 && b == 378);
  EXPECT(!std::filesystem::exists(shapes_cache("stock")));  // nothing written
}

// A movie the table does not have plays at once, whole: the measurement
// waits while it plays and runs after, cached for the next play.
TEST(movie_shape_unknown_movie_is_measured_after_its_play) {
  const std::string cache = shapes_cache("unknown");
  const std::string key = "mymod/30/640x480";
  {
    mech3::MovieShapeStore store(cache);
    FakeMeasure m;
    const auto t0 = std::chrono::steady_clock::now();
    store.open(key, 15, m.fn());
    const auto opened = std::chrono::steady_clock::now() - t0;
    EXPECT(opened < std::chrono::milliseconds(30));  // the measurement alone takes 60 ms
    mech3::MovieShapePlay play;
    int16_t t = 0, b = 0;
    for (int f = 0; f < 20; ++f) {
      EXPECT(!store.rows(key, &play, f, &t, &b));  // whole while measuring
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT(m.frames_done == 0 && store.measuring());  // nothing decoded beside the play
    store.play_end();
    store.wait_measured();
    EXPECT(m.frames_done == 30 && store.known(key));
  }
  std::ifstream in(cache);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  EXPECT(text.find(key + " 0:80-400") != std::string::npos);
  // The next start knows it from the cache.
  mech3::MovieShapeStore again(cache);
  FakeMeasure m2;
  again.open(key, 15, m2.fn());
  EXPECT(again.known(key) && m2.calls == 0);
  again.play_end();
}

// The write dir's cache wins over the table (a player's own measurement).
TEST(movie_shape_cache_wins_over_the_table) {
  mech3::MovieShapeStore store(shapes_cache("over", "# x\nintro/2737/640x480 0:whole\n"));
  mech3::MovieShapePlay play;
  int16_t t = 0, b = 0;
  EXPECT(store.known("intro/2737/640x480") && !store.rows("intro/2737/640x480", &play, 0, &t, &b));
  mech3::MovieShapePlay c4;
  EXPECT(store.rows("c4win/1083/640x480", &c4, 0, &t, &b) && t == 98);  // the table's still
}

// Stopped while waiting for a play to end (the program quitting mid-movie):
// nothing is cached, and the store's end does not hang.
TEST(movie_shape_store_stops_while_waiting) {
  const std::string cache = shapes_cache("stop");
  FakeMeasure m;
  {
    mech3::MovieShapeStore store(cache);
    store.open("quit/30/640x480", 15, m.fn());
  }
  EXPECT(m.frames_done == 0 && !std::filesystem::exists(cache));
}

