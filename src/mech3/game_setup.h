#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mech3/source_map.h"

// The first-start setup's operations for a UI (ours, not original code;
// docs/assets.md): install from the disc, apply the 1.2 patch, add the music,
// check an install. Each takes a progress callback and returns what a player
// is told in plain words; the command-line flags (--install-from, --patch)
// are wrappers over these. Each runs to the end on the calling thread: a UI
// runs it on a worker thread and cancels by returning false from `progress`.
namespace mech3::setup {

struct Progress {
  std::string stage;      // "Reading the disc", "Copying the game files", "Copying the movies", ...
  double fraction = 0;    // of the whole operation, 0..1
  std::string message;    // the current file or step, for a second line
};
// False cancels: the operation stops as soon as it can and changes nothing.
using ProgressFn = std::function<bool(const Progress&)>;

struct Result {
  bool ok = false;
  bool cancelled = false;
  std::string summary;                // one or two sentences for the player
  std::string detail;                 // the technical reason (a log, a "details" box)
  std::vector<std::string> warnings;  // plain words, each a sentence
  std::string install_dir;            // install_from_disc and check_install: the folder
  std::string version;                // "1.0", "1.2", "1.0 with 1.2 files", "unknown"
  int files = 0, movies = 0, music_tracks = 0;
  uint64_t bytes = 0;
};

// The disc (a drive or a folder holding its files), a .iso, or a .cue/.bin,
// installed into `out_dir` (to `out_dir`.new, then renamed: a failure or a
// cancel leaves nothing). With `patch` (the patch's ZIP, folder, data1.hdr or
// loose files) its data becomes 1.2's in the same step. A .cue's CD audio
// becomes music_dir/trackNN.wav when `music_dir` is given.
Result install_from_disc(const std::string& source, const std::string& out_dir, const std::string& patch,
                         const std::string& music_dir, const ProgressFn& progress, const std::string& movies_from = "");

// Whatever map_source found (source_map.h), installed. An installed copy on
// disk is used where it is (check_install); a disc or image on disk goes to
// install_from_disc; anything in an archive is read out in one forward pass
// (source_install.h): an installed copy's files into `out_dir`.new, a disc's
// cabinet and movies staged in `write_dir`/setup-staging (removed after) and
// installed from there, its CD audio as music_dir/trackNN.wav. `out_dir`
// appears only once complete, its mw3-install.txt written last.
Result install_from_source(const source::Found& found, const std::string& out_dir, const std::string& patch,
                           const std::string& music_dir, const std::string& write_dir, const ProgressFn& progress);

// The official 1.2 patch's 28 data files into `write_dir`/patch12, mounted in
// front of the install at every start (the install is never written).
Result apply_patch(const std::string& source, const std::string& write_dir, const ProgressFn& progress);

// A disc image's CD audio (its .cue) as music_dir/trackNN.wav.
Result add_music(const std::string& cue, const std::string& music_dir, const ProgressFn& progress);

// The disc's movies (its VIDEO\*.AVI) from a drive, the disc's folder, an
// .iso or a .cue/.bin into `video_dir` (the write dir's video\, found before
// the install's): for an install whose movies stayed on the CD. Written to
// `video_dir`.new first; a cancel or a failure changes nothing.
Result add_movies(const std::string& source, const std::string& video_dir, const ProgressFn& progress);

// What an install lacks, from its disc: the movies (add_movies) when
// `video_dir` is given, the music (add_music, a .cue only) when `music_dir` is.
Result add_from_disc(const std::string& source, const std::string& video_dir, const std::string& music_dir,
                     const ProgressFn& progress);

// A quick look at a picked drive, folder, .iso or .cue, writing nothing:
// whether it is the MechWarrior 3 disc (ok), its version ("1.0" by the
// cabinet's reader.zbd, or an installed folder's), its movies and a cue's
// audio tracks; a summary to show before the long copy, or why it is not one.
Result probe_source(const std::string& path);

struct DiscSource {
  std::string path;   // the drive's root: "D:\\", "/Volumes/MW3", "/media/<user>/MW3"
  std::string label;  // its volume name, else the folder's
};
// The mounted drives holding an MW3 disc (DATA1.HDR at the root): Windows'
// CD drives, macOS's /Volumes, Linux's /media/$USER and /run/media/$USER.
std::vector<DiscSource> find_discs();

// Whether `dir` holds MechWarrior 3 (zbd/reader.zbd), and which data
// version: 1.2 with `write_dir`/patch12 in front of it when that exists.
Result check_install(const std::string& dir, const std::string& write_dir);

}  // namespace mech3::setup
