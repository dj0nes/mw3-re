#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "mech3/source_map.h"

// What the map found, read out of its archives in one forward pass (ours, not
// original code; docs/setup-sources.md, the install). An installed copy's
// files go straight to `install_to`; a disc's DATA1.HDR and DATA1.CAB (the
// cabinet needs its header first and random access) and its VIDEO\ files go
// to `staging`, its cue's CD audio straight to `staging`/music as trackNN.wav.
// Nothing is unpacked twice: a disc image is read sector by sector from its
// archive entry. The caller installs from the staged disc as from any disc
// and moves the movies and the music in (setup::install_from_source).
namespace mech3::source {

struct Staged {
  bool ok = false;
  enum Failure { kNone, kUnreadable, kDamaged, kNoSpace, kCannotWrite, kCancelled, kUnsafePath };
  Failure failure = kNone;
  std::string error;
  std::string disc_dir;   // a disc's: DATA1.HDR and DATA1.CAB
  std::string video_dir;  // a disc's VIDEO\ files
  std::string music_dir;  // a cue's audio tracks, trackNN.wav
  int files = 0, movies = 0, music = 0;
  uint64_t bytes = 0;
  uint64_t space_needed = 0, space_free = 0;  // kNoSpace: bytes, on the disk that holds space_where
  std::string space_where;
  std::vector<std::string> log;
};

// The fraction of the read (0..1) and the entry; false cancels.
using StageProgress = std::function<bool(double fraction, const std::string& entry)>;

// `f` from map_source, not on disk (an on-disk source installs as it is).
// `staging` and `install_to` are made here; a failure or a cancel removes
// both, so nothing half-copied is left to look like a good copy.
Staged stage_source(const Found& f, const std::string& staging, const std::string& install_to,
                    const StageProgress& progress);

// An entry's path made safe to write under a folder ('\\' read as '/', "."
// parts dropped); "" for an absolute path, a drive, or any ".." part.
std::string safe_relative(const std::string& entry);

// The free space kept beside what is written (64 MB; tests raise it).
uint64_t& space_spare();

}  // namespace mech3::source
