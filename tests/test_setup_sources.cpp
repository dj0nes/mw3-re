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

#else

TEST(setup_sources_no_libarchive_in_this_build) { EXPECT(!mech3::source::archives_supported()); }

#endif
