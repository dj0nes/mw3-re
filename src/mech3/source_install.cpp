#include "mech3/source_install.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>

#include "mech3/source_archive.h"

namespace fs = std::filesystem;

namespace mech3::source {

namespace {

std::string lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

uint64_t g_spare = 64u << 20;  // left free beside what is written

void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// A RIFF header for 44100 Hz 16-bit stereo, as platform/cue_audio writes it.
void wav_header(uint8_t* h, uint32_t bytes) {
  std::memcpy(h, "RIFF", 4), put32(h + 4, 36 + bytes), std::memcpy(h + 8, "WAVEfmt ", 8);
  put32(h + 16, 16), put32(h + 20, 1 | (2u << 16)), put32(h + 24, 44100), put32(h + 28, 44100 * 4);
  put32(h + 32, 4 | (16u << 16)), std::memcpy(h + 36, "data", 4), put32(h + 40, bytes);
}

// Whether `p` resolves to a place inside `base` (links and all followed).
bool under(const fs::path& base, const fs::path& p) {
  std::error_code ec;
  const fs::path b = fs::weakly_canonical(base, ec), q = fs::weakly_canonical(p, ec);
  if (ec) return false;
  const fs::path rel = q.lexically_relative(b);
  return !rel.empty() && rel != "." && *rel.begin() != "..";
}

struct Pass {
  const Found& f;
  const StageProgress& progress;
  Staged& s;
  ArchiveReader* top = nullptr;
  uint64_t top_size = 1;
  std::vector<uint8_t> buf = std::vector<uint8_t>(1 << 20);

  bool fail(Staged::Failure why, const std::string& what) {
    if (s.failure == Staged::kNone) s.failure = why, s.error = what;
    return false;
  }
  bool tick(const std::string& entry) {
    const double at = static_cast<double>(top->position()) / static_cast<double>(top_size);
    if (!progress || progress(std::min(1.0, at), entry)) return true;
    return fail(Staged::kCancelled, "cancelled");
  }
  bool room(const fs::path& dir, int64_t size) {
    std::error_code ec;
    const fs::space_info sp = fs::space(dir, ec);
    if (ec || size < 0 || sp.available >= static_cast<uint64_t>(size) + g_spare) return true;
    s.space_needed = static_cast<uint64_t>(size) + g_spare, s.space_free = sp.available, s.space_where = dir.string();
    return fail(Staged::kNoSpace, "not enough free space in " + dir.string() + ": " +
                                      std::to_string((static_cast<uint64_t>(size) + g_spare) >> 20) + " MB needed, " +
                                      std::to_string(sp.available >> 20) + " MB free");
  }

  // The current entry of `r` to `to`, `skip` bytes left out and, for a WAV,
  // a header first. Its size checked against the archive's.
  bool write(ArchiveReader& r, const ArchiveReader::Entry& e, const fs::path& base, const std::string& rel, uint64_t skip,
             bool wav) {
    std::error_code ec;
    const fs::path to = base / rel;
    fs::create_directories(base, ec);
    if (rel.empty() || !under(base, to)) return fail(Staged::kUnsafePath, e.path + ": its path leaves " + base.string());
    fs::create_directories(to.parent_path(), ec);
    if (!room(to.parent_path(), e.size)) return false;
    std::FILE* o = std::fopen(to.string().c_str(), "wb");
    if (!o) return fail(Staged::kCannotWrite, "cannot write " + to.string());
    uint8_t head[44] = {};
    bool ok = !wav || std::fwrite(head, 1, sizeof head, o) == sizeof head;
    uint64_t got = 0, kept = 0;
    int64_t n = 0;
    while (ok && (n = r.read(buf.data(), buf.size())) > 0) {
      const uint64_t from = got >= skip ? 0 : std::min<uint64_t>(n, skip - got);
      got += n;
      if (from < static_cast<uint64_t>(n)) {
        ok = std::fwrite(buf.data() + from, 1, n - from, o) == n - from;
        kept += n - from;
      }
      if (ok && !tick(e.path)) break;
    }
    if (ok && wav) {
      kept &= ~uint64_t{3};  // whole stereo frames
      wav_header(head, static_cast<uint32_t>(kept));
      ok = std::fseek(o, 0, SEEK_SET) == 0 && std::fwrite(head, 1, sizeof head, o) == sizeof head;
    }
    const bool closed = std::fclose(o) == 0;
    if (s.failure != Staged::kNone) return false;
    if (!ok || !closed) return fail(Staged::kCannotWrite, "cannot write " + to.string());
    if (n < 0 || (e.size >= 0 && got != static_cast<uint64_t>(e.size)))
      return fail(Staged::kDamaged, e.path + ": " + (n < 0 ? r.error() : "cut short"));
    s.bytes += kept;
    return true;
  }

  // A disc's files from `r`, under `root`: DATA1.HDR, DATA1.CAB, VIDEO\.
  bool disc_files(ArchiveReader& r, const std::string& root, const fs::path& disc, const fs::path& video) {
    ArchiveReader::Entry e;
    const std::string lroot = lower(root);
    while (r.next(&e)) {
      if (e.dir) continue;
      const std::string l = lower(e.path);
      if (l.compare(0, lroot.size(), lroot) != 0) continue;
      const std::string rel = l.substr(lroot.size());
      if (rel == "data1.hdr" || rel == "data1.cab") {
        if (!write(r, e, disc, rel == "data1.hdr" ? "DATA1.HDR" : "DATA1.CAB", 0, false)) return false;
        ++s.files;
      } else if (rel.rfind("video/", 0) == 0 && rel.find('/', 6) == std::string::npos) {
        const std::string name = safe_relative(e.path.substr(e.path.rfind('/') + 1));
        if (name.empty()) continue;
        if (!write(r, e, video, name, 0, false)) return false;
        ++s.movies;
      } else if (!tick(e.path)) {
        return false;
      }
    }
    if (!r.error().empty()) return fail(Staged::kDamaged, r.error());
    return true;
  }
};

}  // namespace

uint64_t& space_spare() { return g_spare; }

std::string safe_relative(const std::string& entry) {
  std::string out;
  size_t at = 0;
  std::string p = entry;
  std::replace(p.begin(), p.end(), '\\', '/');
  if (p.empty() || p[0] == '/' || (p.size() >= 2 && p[1] == ':')) return "";  // absolute, a drive, a share
  while (at <= p.size()) {
    const size_t end = std::min(p.find('/', at), p.size());
    const std::string part = p.substr(at, end - at);
    at = end + 1;
    if (part.empty() || part == ".") continue;
    if (part == "..") return "";
    out += (out.empty() ? "" : "/") + part;
  }
  return out;
}

namespace {

Staged stage_inner(const Found& f, const std::string& staging, const std::string& install_to,
                   const StageProgress& progress) {
  Staged s;
  Pass pass{f, progress, s};
  std::error_code ec;
  if (f.kind == Found::kNothing || f.on_disk) {
    s.failure = Staged::kUnreadable, s.error = "nothing to read from an archive";
    return s;
  }
  // Down the steps to the container holding the game (or its image).
  std::vector<std::unique_ptr<ArchiveReader>> chain;
  chain.push_back(std::make_unique<ArchiveReader>());
  if (!chain.back()->open_file(f.path)) {
    s.failure = Staged::kUnreadable, s.error = f.path + ": " + chain.back()->error();
    return s;
  }
  pass.top = chain.front().get();
  pass.top_size = std::max<uint64_t>(1, fs::file_size(f.path, ec));
  // Before anything is written: at least the source's own size free (what
  // is unpacked from it is no smaller, but for what is passed over).
  const fs::path into = f.kind == Found::kInstall ? fs::path(install_to) : fs::path(staging);
  fs::create_directories(into, ec);
  if (!pass.room(into, static_cast<int64_t>(pass.top_size))) return s;
  const Step* image = !f.steps.empty() && f.steps.back().kind != Step::kArchive ? &f.steps.back() : nullptr;
  for (const Step& st : f.steps) {
    if (&st == image) break;
    ArchiveReader::Entry e;
    bool found = false;
    while (!found && chain.back()->next(&e)) found = e.path == st.entry;
    auto inner = std::make_unique<ArchiveReader>();
    if (!found || !inner->open_stream(chain.back()->entry_stream())) {
      s.failure = Staged::kUnreadable, s.error = st.entry + ": " + (found ? inner->error() : "not found again");
      return s;
    }
    chain.push_back(std::move(inner));
  }
  ArchiveReader& r = *chain.back();
  const fs::path disc = fs::path(staging) / "disc", video = fs::path(staging) / "video", music = fs::path(staging) / "music";

  if (f.kind == Found::kInstall) {
    // Every file under the copy's folder, straight into install_to.
    ArchiveReader::Entry e;
    while (r.next(&e)) {
      if (e.dir || e.path.compare(0, f.root.size(), f.root) != 0) {
        if (!pass.tick(e.path)) return s;
        continue;
      }
      const std::string rel = safe_relative(e.path.substr(f.root.size()));
      if (!pass.write(r, e, install_to, rel, 0, false)) return s;
      ++s.files;
    }
    if (!r.error().empty()) {
      pass.fail(Staged::kDamaged, r.error());
      return s;
    }
    s.log.push_back(std::to_string(s.files) + " files, " + std::to_string(s.bytes >> 20) + " MB from " + f.path);
    s.ok = true;
    return s;
  }

  s.disc_dir = disc.string(), s.video_dir = video.string();
  if (!image) {
    // A disc's files in an archive, as on the disc.
    if (!pass.disc_files(r, f.root, disc, video)) return s;
  } else {
    // An image, with the cue's audio tracks beside it in the same container.
    std::set<std::string> audio(f.audio.begin(), f.audio.end());
    bool read_image = false;
    ArchiveReader::Entry e;
    while (r.next(&e)) {
      if (e.path == image->entry) {
        ArchiveReader iso;
        ArchiveReader::ReadFn sectors = r.entry_stream();
        if (image->kind == Step::kRawImage) sectors = cooked_sectors(sectors, image->raw_offset);
        if (!iso.open_iso_stream(sectors)) {
          pass.fail(Staged::kDamaged, e.path + ": " + iso.error());
          return s;
        }
        if (!pass.disc_files(iso, "", disc, video)) return s;
        read_image = true;
      } else if (audio.count(e.path)) {
        const size_t i = std::find(f.audio.begin(), f.audio.end(), e.path) - f.audio.begin();
        char name[16];
        std::snprintf(name, sizeof name, "track%02d.wav", f.audio_track[i]);
        if (!pass.write(r, e, music, name, static_cast<uint64_t>(f.audio_start[i]), true)) return s;
        ++s.music;
      } else if (!pass.tick(e.path)) {
        return s;
      }
    }
    if (!r.error().empty() || !read_image) {
      pass.fail(Staged::kDamaged, read_image ? r.error() : image->entry + ": not found again");
      return s;
    }
    if (s.music) s.music_dir = music.string();
  }
  if (!fs::exists(disc / "DATA1.HDR", ec) || !fs::exists(disc / "DATA1.CAB", ec)) {
    pass.fail(Staged::kDamaged, f.path + ": DATA1.HDR or DATA1.CAB was not found again");
    return s;
  }
  s.log.push_back("staged DATA1.HDR, DATA1.CAB, " + std::to_string(s.movies) + " movies, " + std::to_string(s.music) +
                  " music tracks (" + std::to_string(s.bytes >> 20) + " MB) from " + f.path);
  s.ok = true;
  return s;
}

}  // namespace

Staged stage_source(const Found& f, const std::string& staging, const std::string& install_to,
                    const StageProgress& progress) {
  Staged s = stage_inner(f, staging, install_to, progress);
  if (!s.ok) {  // nothing half-staged left to look like a good copy
    std::error_code ec;
    fs::remove_all(staging, ec);
    if (!install_to.empty()) fs::remove_all(install_to, ec);
    s.disc_dir.clear(), s.video_dir.clear(), s.music_dir.clear();
  }
  return s;
}

}  // namespace mech3::source

