// SETUP's sources (docs/setup-sources.md): the archive layer read against
// archives this test writes with libarchive's own writer (nothing committed):
// zip, 7z, a zip inside a zip, an ISO 9660 image, the same as raw 2352-byte
// sectors, an ISO inside a 7z, and a damaged archive.
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "check.h"
#include "mech3/source_archive.h"
#include "mech3/source_map.h"
#include "mech3/disc_install.h"
#include <filesystem>

#if MW3_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>

namespace {

using Files = std::map<std::string, std::string>;  // path -> bytes

// `files` written in `format` ("zip", "7zip", "iso9660") into memory.
std::string write_archive(const char* format, const Files& files) {
  archive* a = archive_write_new();
  archive_write_set_format_by_name(a, format);
  if (std::strcmp(format, "7zip") == 0) archive_write_set_format_option(a, "7zip", "compression", "lzma2");
  std::vector<char> buf(64 << 20);
  size_t used = 0;
  archive_write_open_memory(a, buf.data(), buf.size(), &used);
  for (const auto& [path, data] : files) {
    archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, path.c_str());
    archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    archive_write_data(a, data.data(), data.size());
    archive_entry_free(e);
  }
  archive_write_close(a);
  archive_write_free(a);
  return std::string(buf.data(), used);
}

// A ReadFn over bytes in memory.
mech3::source::ArchiveReader::ReadFn memory_stream(const std::string& bytes) {
  auto at = std::make_shared<size_t>(0);
  return [bytes, at](uint8_t* buf, size_t n) -> int64_t {
    const size_t k = std::min(n, bytes.size() - *at);
    std::memcpy(buf, bytes.data() + *at, k);
    *at += k;
    return static_cast<int64_t>(k);
  };
}

// Every file of a reader, read whole.
Files read_all(mech3::source::ArchiveReader& r) {
  Files out;
  mech3::source::ArchiveReader::Entry e;
  while (r.next(&e)) {
    if (e.dir) continue;
    std::string data;
    uint8_t buf[4096];
    for (int64_t n; (n = r.read(buf, sizeof buf)) > 0;) data.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    out[e.path] = data;
  }
  return out;
}

// Test files: a big one (several compressed blocks), a small one, one in a folder.
Files sample() {
  std::string big;
  for (int i = 0; i < 300000; ++i) big += static_cast<char>("MW3 rebuild "[i % 12] + (i / 997) % 7);
  return {{"zbd/reader.zbd", big}, {"Mech3.exe", "MZ fake exe"}, {"video/intro.avi", std::string(70000, 'v')}};
}

std::string temp_file(const char* name, const std::string& bytes) {
  const std::string p = std::string(MW3_TEST_TMP) + "/" + name;
  std::FILE* f = std::fopen(p.c_str(), "wb");
  std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  return p;
}

// An ISO image's 2048-byte sectors as a raw MODE1/2352 track: sync, header,
// the data at 16, the EDC/ECC area left zero (the reader skips it).
std::string raw_track(const std::string& iso) {
  std::string out;
  for (size_t at = 0; at < iso.size(); at += 2048) {
    std::string s(2352, '\0');
    s[0] = 0;
    for (int i = 1; i < 11; ++i) s[static_cast<size_t>(i)] = static_cast<char>(0xff);
    s[15] = 1;  // MODE1
    std::memcpy(&s[16], iso.data() + at, std::min<size_t>(2048, iso.size() - at));
    out += s;
  }
  return out;
}

}  // namespace

TEST(setup_sources_read_zip_and_7z_from_files) {
  const Files want = sample();
  for (const char* fmt : {"zip", "7zip"}) {
    const std::string path = temp_file(std::strcmp(fmt, "zip") == 0 ? "src.zip" : "src.7z", write_archive(fmt, want));
    mech3::source::ArchiveReader r;
    EXPECT(r.open_file(path));
    const Files got = read_all(r);
    EXPECT(got == want);
    std::printf("  %s: %zu files, format %s\n", fmt, got.size(), r.format().c_str());
  }
}

TEST(setup_sources_read_a_zip_inside_a_zip_without_unpacking) {
  const Files want = sample();
  const std::string inner = write_archive("zip", want);
  const std::string outer =
      write_archive("zip", {{"MechWarrior 3 (USA).zip", inner}, {"scans/box front.jpg", std::string(5000, 'j')}});
  mech3::source::ArchiveReader r;
  EXPECT(r.open_stream(memory_stream(outer)));  // a stream: no seeking at all
  mech3::source::ArchiveReader::Entry e;
  bool found = false;
  while (r.next(&e)) {
    if (!mech3::source::archive_name(e.path)) continue;
    mech3::source::ArchiveReader inner_r;
    EXPECT(inner_r.open_stream(r.entry_stream()));
    found = read_all(inner_r) == want;
  }
  EXPECT(found);
}

TEST(setup_sources_read_iso_images_cooked_raw_and_inside_7z) {
  const Files want = sample();
  // ISO 9660 keeps names in upper case 8.3 unless told otherwise; Rock Ridge
  // (libarchive's default) keeps them as written.
  const std::string iso = write_archive("iso9660", want);
  EXPECT(iso.size() % 2048 == 0 && iso.size() > 300000);
  {
    mech3::source::ArchiveReader r;
    EXPECT(r.open_iso_stream(memory_stream(iso)));
    EXPECT(read_all(r) == want);
  }
  {  // the same image as a .bin's raw sectors
    mech3::source::ArchiveReader r;
    EXPECT(r.open_iso_stream(mech3::source::cooked_sectors(memory_stream(raw_track(iso)))));
    EXPECT(read_all(r) == want);
  }
  {  // an .iso inside a 7z, read through the 7z's entry
    const std::string path = temp_file("iso.7z", write_archive("7zip", {{"MW3.iso", iso}}));
    mech3::source::ArchiveReader outer;
    EXPECT(outer.open_file(path));
    mech3::source::ArchiveReader::Entry e;
    EXPECT(outer.next(&e) && e.path == "MW3.iso");
    mech3::source::ArchiveReader r;
    EXPECT(r.open_iso_stream(outer.entry_stream()));
    EXPECT(read_all(r) == want);
  }
}

TEST(setup_sources_refuse_damaged_and_unknown_files) {
  std::string zip = write_archive("zip", sample());
  zip.resize(zip.size() / 2);  // cut short: no central directory
  mech3::source::ArchiveReader r;
  const bool opened = r.open_file(temp_file("cut.zip", zip));
  Files got;
  if (opened) got = read_all(r);
  EXPECT(!opened || got != sample());
  EXPECT(!r.error().empty() || !opened || got.size() < 3);
  mech3::source::ArchiveReader t;
  EXPECT(!t.open_file(temp_file("plain.txt", "not an archive at all, just text\n")));
  EXPECT(mech3::source::archive_name("x.ZIP") && mech3::source::archive_name("a.tar.gz") && !mech3::source::archive_name("track.bin"));
}


namespace {

std::string slurp(const std::string& path) {
  std::string d;
  if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
    char buf[1 << 16];
    for (size_t n; (n = std::fread(buf, 1, sizeof buf, f)) > 0;) d.append(buf, n);
    std::fclose(f);
  }
  return d;
}
const std::string kGame = std::string(MW3_SOURCE_DIR) + "/game";
bool have_games() {
  return std::filesystem::exists(kGame + "/mw3-1.0/zbd/reader.zbd") && std::filesystem::exists(kGame + "/mw3-1.2/zbd/reader.zbd") &&
         std::filesystem::exists(kGame + "/pm/zbd/reader.zbd") && std::filesystem::exists(kGame + "/pm-disc/pm.iso") &&
         std::filesystem::exists(kGame + "/patch-1.2/MechWarrior-3_Patch_Win_EN_Patch-12-US/data1.hdr");
}
// Pirate's Moon's disc header, from its image.
std::string pm_hdr() {
  mech3::Disc d;
  std::string err;
  if (!d.open(kGame + "/pm-disc/pm.iso", &err)) return "";
  const mech3::Disc::File* f = d.find("DATA1.HDR");
  if (!f) return "";
  std::string out(static_cast<size_t>(f->size), '\0');
  d.read(*f, 0, out.size(), reinterpret_cast<uint8_t*>(&out[0]));
  return out;
}
// A raw MODE1/2352 track of an image, with an audio track and a cue as
// Redump zips them (the .bins before the .cue).
Files redump(const std::string& iso, const std::string& name) {
  Files f;
  f[name + " (Track 1).bin"] = raw_track(iso);
  f[name + " (Track 2).bin"] = std::string(2352 * 200, '\x11');  // audio: no sync pattern
  f[name + ".cue"] = "FILE \"" + name + " (Track 1).bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\nFILE \"" +
                     name + " (Track 2).bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 00 00:00:00\n    INDEX 01 00:02:00\n";
  return f;
}

}  // namespace

// The map: installed copies on disk (1.0, 1.2, Pirate's Moon refused).
TEST(setup_map_installed_copies_on_disk) {
  if (!have_games()) return;
  const mech3::source::Found a = mech3::source::map_source(kGame + "/mw3-1.0");
  EXPECT(a.kind == mech3::source::Found::kInstall && a.game == mech3::source::Found::kMw3 && a.version == "1.0" && a.on_disk);
  const mech3::source::Found b = mech3::source::map_source(kGame + "/mw3-1.2/Mech3.exe");  // its exe
  EXPECT(b.kind == mech3::source::Found::kInstall && b.version == "1.2");
  const mech3::source::Found c = mech3::source::map_source(kGame + "/pm");
  EXPECT(c.game == mech3::source::Found::kPiratesMoon && c.summary.find("Pirate's Moon") != std::string::npos);
  const mech3::source::Found d = mech3::source::map_source(kGame + "/pm-disc/pm.iso");
  EXPECT(d.kind == mech3::source::Found::kDisc && d.game == mech3::source::Found::kPiratesMoon);
  std::printf("  %s | %s | %s\n", a.summary.c_str(), b.summary.c_str(), c.summary.c_str());
}

// The map in archives: an installed copy zipped, the same zipped again, a
// disc image (2048 and raw, an unknown label, Redump's cue and audio order)
// in a zip and a 7z, Pirate's Moon's refused, and an archive with no game.
TEST(setup_map_finds_the_game_inside_archives) {
  if (!have_games()) return;
  using mech3::source::Found;
  const Files install = {{"MechWarrior 3/zbd/reader.zbd", slurp(kGame + "/mw3-1.0/zbd/reader.zbd")},
                         {"MechWarrior 3/Mech3.exe", slurp(kGame + "/mw3-1.0/Mech3.exe")},
                         {"MechWarrior 3/readme.txt", "hello"}};
  const std::string zip = write_archive("zip", install);
  Found a = mech3::source::map_source(temp_file("install.zip", zip));
  EXPECT(a.kind == Found::kInstall && a.game == Found::kMw3 && a.version == "1.0" && a.root == "MechWarrior 3/" &&
         a.steps.empty() && !a.on_disk);
  Found b = mech3::source::map_source(temp_file("nested.zip", write_archive("zip", {{"inner/mw3.zip", zip}, {"box.jpg", "j"}})));
  EXPECT(b.kind == Found::kInstall && b.steps.size() == 1 && b.steps[0].entry == "inner/mw3.zip");

  // A disc: the 1.2 patch's installer header stands in for the disc's (it
  // lists zbd/reader.zbd at 1.2's size); libarchive labels the image
  // "CDROM", a label no pressing has: content decides.
  const std::string hdr = slurp(kGame + "/patch-1.2/MechWarrior-3_Patch_Win_EN_Patch-12-US/data1.hdr");
  const std::string iso = write_archive("iso9660", {{"DATA1.HDR", hdr}, {"DATA1.CAB", std::string(300000, 'c')}});
  Found c = mech3::source::map_source(temp_file("disc.7z", write_archive("7zip", {{"MW3.iso", iso}})));
  EXPECT(c.kind == Found::kDisc && c.game == Found::kMw3 && c.version == "1.2" && c.steps.size() == 1 &&
         c.steps[0].kind == mech3::source::Step::kImage && c.label == "CDROM");
  Found d = mech3::source::map_source(temp_file("redump.zip", write_archive("zip", redump(iso, "MechWarrior 3 (USA)"))));
  EXPECT(d.kind == Found::kDisc && d.game == Found::kMw3 && d.raw && d.raw_offset == 16 && d.audio.size() == 1 && d.audio[0] == "MechWarrior 3 (USA) (Track 2).bin" &&
         d.data_track == "MechWarrior 3 (USA) (Track 1).bin" && d.audio_track == std::vector<int>{2} &&
         d.audio_start == std::vector<int64_t>{2 * 75 * 2352});
  std::printf("  redump: %s (%s)\n", d.summary.c_str(), d.detail.c_str());
  // The same on disk: its cue read for the music.
  std::string cue;
  for (const auto& [name, bytes] : redump(iso, "On Disk")) {
    const std::string p = temp_file(name.c_str(), bytes);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".cue") == 0) cue = p;
  }
  Found g = mech3::source::map_source(cue);
  EXPECT(g.kind == Found::kDisc && g.on_disk && g.game == Found::kMw3 && g.audio == std::vector<std::string>{"On Disk (Track 2).bin"} &&
         g.audio_track == std::vector<int>{2} && g.audio_start == std::vector<int64_t>{2 * 75 * 2352});

  // Pirate's Moon's disc in a zip in a zip: named, not installed.
  const std::string pm_iso = write_archive("iso9660", {{"DATA1.HDR", pm_hdr()}});
  Found e = mech3::source::map_source(
      temp_file("pm.zip", write_archive("zip", {{"pm/disc.zip", write_archive("zip", {{"PM.iso", pm_iso}})}})));
  EXPECT(e.game == Found::kPiratesMoon && e.summary.find("comes later") != std::string::npos);
  // No game at all.
  Found f = mech3::source::map_source(temp_file("other.zip", write_archive("zip", {{"notes.txt", "x"}, {"data/a.bin", "abc"}})));
  EXPECT(f.kind == Found::kNothing && !f.summary.empty());
}

// The real thing, when the backups are here (a long read: MW3_SETUP_LONG=1):
// Redump's MechWarrior 3 zip, its data track 626 MB and two audio tracks.
TEST(setup_map_the_real_redump_zip) {
  const char* zip = "/Volumes/iO/backups/win98/MechWarrior 3 (USA).zip";
  if (!std::getenv("MW3_SETUP_LONG") || !std::filesystem::exists(zip)) return;
  const mech3::source::Found f = mech3::source::map_source(zip);
  std::printf("  %s\n%s", f.summary.c_str(), f.detail.c_str());
  EXPECT(f.kind == mech3::source::Found::kDisc && f.game == mech3::source::Found::kMw3 && f.version == "1.0" &&
         f.label == "MW3" && f.audio.size() == 2);
}

#else

TEST(setup_sources_no_libarchive_in_this_build) { EXPECT(!mech3::source::archives_supported()); }

#endif
