#pragma once
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// The movies' shapes on a wide screen (ours, docs/fmv.md section 8; Owen,
// 2026-11-01): some movies carry black bars baked into their 4:3 frame
// (INTRO 640x320, most of C4WIN 640x240). Measured from the pictures, not
// known by name: each frame's lit rows and a coarse picture of it; the
// frames cut into shots at the movie's own scene changes and black frames;
// runs of letterboxed shots shown cropped to the run's rows at the screen's
// width, anything else as its whole 4:3 frame. Nothing is ever cropped off:
// a run's rows are the union of its frames'.
namespace mech3 {

// One frame, measured.
struct MovieFrameScan {
  int16_t top = -1, bottom = -1;  // the first and last lit row (a row's mean luma over kLit), -1 for a black frame
  uint8_t grid[48] = {};          // the mean luma of an 8 x 6 grid, for the scene changes
};
constexpr int kMovieLit = 20;     // a row or column is picture when its mean luma is above this

// 16-bit pixels under the masks (`pitch` bytes a row) measured.
MovieFrameScan movie_scan_frame(const uint16_t* pixels, int32_t width, int32_t height, int32_t pitch, uint32_t r,
                                uint32_t g, uint32_t b);

// A stretch of the movie from `start` (a frame) to the next's: its rows
// `top`..`bottom` shown at the screen's width, or (`whole`) its 4:3 frame.
struct MovieShapeRun {
  int32_t start = 0;
  bool whole = true;
  int16_t top = 0, bottom = 0;
  bool operator==(const MovieShapeRun& o) const {
    return start == o.start && whole == o.whole && (whole || (top == o.top && bottom == o.bottom));
  }
};

// The runs of a whole movie of `frames` (width x height, `fps` frames a
// second): a letterboxed run (bars of at least 8 rows above and below, the
// picture no more than 7/8 of the height) held for at least 2 s, else
// whole; runs start only where a shot does (the first at 0).
std::vector<MovieShapeRun> movie_shape_runs(const std::vector<MovieFrameScan>& frames, int32_t width, int32_t height,
                                            int32_t fps);
// The run frame `f` is in (an index into `runs`), -1 without one.
int32_t movie_shape_run_of(const std::vector<MovieShapeRun>& runs, int32_t f);

// One play of a movie: the rows to show for each frame drawn. While its
// runs are not known (`runs` null: still being measured) every frame is
// whole; a play that started so stays whole until the first shape change
// after they are, never switching mid-shot.
class MovieShapePlay {
 public:
  // The rows of frame `f` to show in `top`, `bottom`; false for the whole frame.
  bool rows(const std::vector<MovieShapeRun>* runs, int32_t f, int16_t* top, int16_t* bottom);

 private:
  bool started_ = false, waiting_ = false;
  int32_t wait_run_ = -2;
};

// A movie's key: its file name lower-cased without folder or extension
// (spaces as _), its frames and its picture's size: "intro/2737/640x480".
std::string movie_shape_key(const std::string& path, uint32_t frames, int32_t width, int32_t height);

// The cache's line for one movie: its key (name, frames, size), then the runs.
std::string movie_shape_line(const std::string& key, const std::vector<MovieShapeRun>& runs);
bool movie_shape_parse(const std::string& line, std::string* key, std::vector<MovieShapeRun>* runs);

// The shipped movies' shapes, as cache lines: every movie of MW3's 1.0 and
// 1.2 discs and installs, measured by tools/analysis/movie_shapes.py
// (movie_shape_table.cpp). Our numbers, not the game's data.
const std::vector<std::string>& movie_shape_table();

// A measurement asks this between frames: it waits while a movie plays
// (so a play never shares the decoder with it) and is false to stop.
using MovieScanGate = std::function<bool()>;

// The shapes the program knows: the table, then `cache` (the write dir's
// movie-shapes.txt, whose lines win), then what is measured. A movie not
// known is measured on a thread of its own, which runs only while no movie
// plays: its first play is whole, never delayed, and the result is cached
// for the next. One is measured at a time; another opened meanwhile is
// measured on a later play.
class MovieShapeStore {
 public:
  // Every frame scanned through `gate`; the picture's size in `w`, `h`.
  using Measure = std::function<bool(const MovieScanGate& gate, std::vector<MovieFrameScan>* frames, int32_t* w,
                                     int32_t* h)>;
  explicit MovieShapeStore(std::string cache) : cache_(std::move(cache)) {}
  ~MovieShapeStore();
  // A movie's picture opened to play (`key`: name/frames/WxH), measured
  // unless known; measuring waits until every play has ended. With `now`
  // (the --movie debug start) it is measured before this returns.
  void open(const std::string& key, int32_t fps, Measure measure, bool now = false);
  void play_end();  // a picture stream released
  // The rows of `key`'s frame `f` for `play` (MovieShapePlay::rows).
  bool rows(const std::string& key, MovieShapePlay* play, int32_t f, int16_t* top, int16_t* bottom);
  bool known(const std::string& key);
  bool measuring();
  void wait_measured();  // for the tests

 private:
  struct Shape {
    bool ready = false;
    std::vector<MovieShapeRun> runs;
  };
  void load();
  void save(const std::string& key, const std::vector<MovieShapeRun>& runs);
  std::string cache_;
  std::mutex mu_;
  std::condition_variable cv_;
  std::map<std::string, Shape> shapes_;
  bool loaded_ = false, scanning_ = false, stop_ = false;
  int plays_ = 0;
  std::thread scan_;
};

}  // namespace mech3
