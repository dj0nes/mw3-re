#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Where the game is in whatever a player points SETUP at (ours, not original
// code; docs/setup-sources.md, the map): an installed copy (its folder or its
// Mech3.exe), the disc or its drive, a disc image (.iso, or a .cue with its
// .bin tracks), or any of those inside archives at any depth. Which game it
// is comes from its content: a disc's installer file list (DATA1.HDR), an
// installed copy's zbd/reader.zbd (its size, CRC and zArchive version); the
// disc's volume label is only a first guess, since other pressings and
// regions may label differently.
namespace mech3::source {

// One way into the next container, from the picked path: an archive entry
// opened as an archive, or as a disc image (raw 2352-byte sectors or 2048).
struct Step {
  enum Kind { kArchive, kImage, kRawImage } kind = kArchive;
  std::string entry;     // the entry's path in the container before it
  int raw_offset = 16;   // kRawImage: the user data's place in a sector (MODE1 16, MODE2 24)
};

struct Found {
  enum Kind { kNothing, kInstall, kDisc } kind = kNothing;
  enum Game { kUnknown, kMw3, kPiratesMoon } game = kUnknown;
  std::string path;            // what was picked
  std::vector<Step> steps;     // from it to the container holding the game
  std::string root;            // the game's folder in that container ("" its top)
  bool on_disk = false;        // the game is in a plain folder or image file: no archive to stream
  std::string version;         // "1.0", "1.2" (by reader.zbd's bytes), else "" (unknown, MW3 still)
  std::string label;           // a disc's volume label
  std::vector<std::string> audio;  // a cue's AUDIO tracks' entries (or files), in track order
  std::vector<int> audio_track;    // their track numbers (the music's trackNN)
  std::vector<int64_t> audio_start;  // their INDEX 01, in bytes into their file (the pregap left out)
  std::string data_track;      // a cue's data track's entry (or file)
  int raw_offset = 16;         // the data track's sectors (2352: 16 or 24; 2048: 0)
  bool raw = false;
  std::string summary;         // what to tell the player
  std::string detail;          // what was read, for the log
  bool cancelled = false;      // the look was stopped (MapProgress)
};

// The share of the picked file read so far (0..1, an archive's) and the
// entry being looked at; false stops the look (map_source then finds nothing,
// `cancelled` set).
using MapProgress = std::function<bool(double fraction, const std::string& entry)>;

// Walks `path` (a folder, a file, an archive) and says what it holds. Reads
// archives forward once; a disc image inside one is read up to its installer
// header (DATA1.HDR, about 200 MB in on the MW3 disc) and no further.
Found map_source(const std::string& path, const MapProgress& progress = nullptr);

// Plain words for a game found, for the player.
std::string game_words(const Found& f);

}  // namespace mech3::source
