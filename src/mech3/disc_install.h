#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <system_error>
#include <vector>

// Installing MechWarrior 3 straight from its disc (ours, not original code;
// docs/assets.md, Installing from the disc). No Windows installer runs: the
// disc's DATA1.HDR/DATA1.CAB (InstallShield 5) is read with patch_import's
// reader, every file group merged into one folder as unshield does, and its
// VIDEO\ copied beside it. The source may be a CD drive or any folder, an
// ISO 9660 image (2048-byte sectors or raw 2352-byte ones), or a cue sheet
// (Redump's MODE1/2352 .cue/.bin), whose AUDIO tracks become the music.
namespace mech3 {

// The files of a disc: a folder, an ISO 9660 image, or a cue sheet's first
// data track. Paths are '/'-separated as the disc has them, without ";1".
class Disc {
 public:
  struct File {
    std::string path;
    uint64_t size = 0;
    uint64_t at = 0;  // an image's: its first sector's number
  };
  ~Disc();
  bool open(const std::string& path, std::string* error);
  const std::vector<File>& files() const { return files_; }
  const File* find(const std::string& path) const;  // any case
  // `n` bytes of `f` from `off`.
  bool read(const File& f, uint64_t off, size_t n, uint8_t* out);
  std::string kind() const;     // "folder", "ISO image", "raw image" (2352-byte sectors)
  std::string cue;              // the cue sheet it was opened from, if any

 private:
  bool read_image_dir(uint64_t sector, uint64_t size, const std::string& prefix, int depth);
  bool read_sectors(uint64_t sector, uint32_t in, size_t n, uint8_t* out);
  std::string root_;            // a folder's
  std::FILE* image_ = nullptr;  // an image's
  int64_t start_ = 0;           // the data track's first byte in the image file
  uint32_t sector_ = 2048, user_ = 0;  // bytes a sector, and the 2048 user bytes' offset in it
  std::vector<File> files_;
};

struct DiscInstall {
  bool ok = false;
  bool cancelled = false;
  enum Failure { kNone, kUnreadable, kNotMw3, kDamaged, kFolderInUse, kCannotWrite, kPatch, kCancelled };
  Failure failure = kNone;        // for a plain-words message (game_setup.h)
  std::string error;              // why nothing was installed
  std::string patch_error;        // kPatch: the import's
  std::vector<std::string> log;   // what was read and found
  std::vector<std::string> warnings;
  int files = 0, movies = 0, music = 0;
  uint64_t bytes = 0;
  // "1.0" (every data file the 1.0 disc's, by size, CRC and SHA-256), "1.2"
  // (with the patch), else "unknown" with the files named in `warnings`.
  std::string version;
};

// From `source` into `out_dir` (written to `out_dir`.new, then renamed; an
// earlier disc install there is replaced, any other non-empty folder is
// refused). With `patch` (anything import_patch_12 takes) the 28 data files
// become 1.2's in the same step. With a cue sheet and `music_dir`, its AUDIO
// tracks as music_dir/trackNN.wav. `progress` hears each step; false from it
// cancels, and a cancel or a failure leaves the folder and the music as they were.
// `movies_from` (optional): a folder of movies already read out (an archive's,
// source_install.h), moved into video/ before the install is marked complete.
// The stage, the fraction of the whole (0..1) and the current file; false cancels.
using DiscProgress = std::function<bool(const std::string& stage, double fraction, const std::string& message)>;

// A finished `tmp` folder made `out`: the old `out` set aside (out.old) and
// removed only once the new one is in place, put back if the rename fails,
// so a failure leaves the old install working. False with `ec` on failure.
bool swap_in_folder(const std::string& tmp, const std::string& out, std::error_code& ec);
DiscInstall install_from_disc(const std::string& source, const std::string& out_dir, const std::string& patch,
                              const std::string& music_dir, const DiscProgress& progress,
                              const std::string& movies_from = "");

}  // namespace mech3
