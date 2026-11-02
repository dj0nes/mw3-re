#include "mech3/app_movie.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

#include "gamez/zutil/zutl_path.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
extern "C" {
#include "pl_mpeg/pl_mpeg.h"  // uses size_t and FILE without including them
}

namespace mech3 {

namespace {

constexpr uint32_t kVids = 0x73646976, kAuds = 0x73647561;

uint32_t fourcc(const std::string& s) {
  uint32_t v = 0;
  for (size_t i = 0; i < 4; ++i) {
    const char c = i < s.size() && s != "-" ? (s[i] == '_' ? ' ' : s[i]) : 0;
    v |= static_cast<uint32_t>(static_cast<uint8_t>(c)) << (8 * i);
  }
  return v;
}

// "video vids IV50 flags priority language initial scale rate start length
// suggested quality sample_size l t r b"
bool parse_strh(std::istringstream& in, zfmv::AviStreamInfo* s) {
  std::string type, handler;
  int32_t frame[4] = {};
  uint32_t priority = 0, language = 0;
  if (!(in >> type >> handler >> s->flags >> priority >> language >> s->initial_frames >> s->scale >> s->rate >>
        s->start >> s->length >> s->suggested_buffer >> s->quality >> s->sample_size >> frame[0] >> frame[1] >>
        frame[2] >> frame[3]))
    return false;
  s->type = fourcc(type), s->handler = fourcc(handler);
  s->priority = static_cast<uint16_t>(priority), s->language = static_cast<uint16_t>(language);
  for (int i = 0; i < 4; ++i) s->frame[i] = frame[i];
  return true;
}

std::vector<uint8_t> parse_hex(std::istringstream& in) {
  std::vector<uint8_t> out;
  std::string h;
  while (in >> h) out.push_back(static_cast<uint8_t>(std::stoul(h, nullptr, 16)));
  return out;
}

}  // namespace

bool read_movie_info(const std::string& path, MovieInfo* out) {
  std::ifstream f(path);
  if (!f) return false;
  std::string line;
  bool video = false;
  while (std::getline(f, line)) {
    std::istringstream in(line);
    std::string tag;
    if (!(in >> tag) || tag[0] == ';') continue;
    if (tag == "video") video = parse_strh(in, &out->video);
    else if (tag == "audio") out->has_audio = parse_strh(in, &out->audio);
    else if (tag == "videof") out->video_format = parse_hex(in);
    else if (tag == "audiof") out->audio_format = parse_hex(in);
    else if (tag == "keys") {
      size_t n = 0;
      in >> n;
      int32_t k;
      while (in >> k) out->keys.push_back(k);
    }
  }
  return video && !out->video_format.empty();
}

// ".\video\intro.avi" -> the mounted "video/intro" (the importer writes lower case).
std::string movie_base(const char* avi_path) {
  std::string p = avi_path ? avi_path : "";
  for (char& c : p) c = c == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  while (p.rfind("./", 0) == 0) p.erase(0, 2);
  const size_t dot = p.rfind('.');
  if (dot != std::string::npos && p.find('/', dot) == std::string::npos) p.erase(dot);
  const std::string found = zutl::find((p + ".fmv").c_str());
  return found.empty() ? std::string() : found.substr(0, found.size() - 4);
}

MovieStream::~MovieStream() {
  if (plm_) plm_video_destroy(plm_);
}

std::unique_ptr<MovieStream> MovieStream::open(const std::string& base, uint32_t type) {
  if (base.empty() || (type != kVids && type != kAuds)) return nullptr;
  auto m = std::make_unique<MovieStream>();
  m->base_ = base;
  if (!read_movie_info(base + ".fmv", &m->info_)) return nullptr;
  if (type == kAuds) {
    if (!m->info_.has_audio) return nullptr;
    // The .wav's data chunk.
    std::ifstream f(base + ".wav", std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    for (size_t p = 12; p + 8 <= d.size();) {
      uint32_t n = 0;
      std::memcpy(&n, &d[p + 4], 4);
      if (std::memcmp(&d[p], "data", 4) == 0) {
        m->sound_.assign(d.begin() + static_cast<std::ptrdiff_t>(p + 8),
                         d.begin() + static_cast<std::ptrdiff_t>(std::min(d.size(), p + 8 + n)));
        break;
      }
      p += 8 + n + (n & 1);
    }
    return m;
  }
  m->video_ = true;
  plm_buffer_t* buf = plm_buffer_create_with_filename((base + ".m1v").c_str());
  if (!buf) return nullptr;
  m->plm_ = plm_video_create_with_buffer(buf, 1);
  if (!m->plm_) return nullptr;
  plm_video_set_no_delay(m->plm_, 1);  // no B-frames: each picture as it is decoded
  return m;
}

int32_t MovieStream::read(int32_t start, int32_t samples, void* buffer, int32_t bytes) {
  if (video_) {
    chunk_ = start;
    return 0;
  }
  if (!buffer || bytes <= 0) return 0;
  const uint32_t block = info_.audio.sample_size ? info_.audio.sample_size : 1;
  const size_t from = static_cast<size_t>(start < 0 ? 0 : start) * block;
  const size_t want = std::min(static_cast<size_t>(bytes), static_cast<size_t>(samples < 0 ? 0 : samples) * block);
  const size_t have = from < sound_.size() ? std::min(want, sound_.size() - from) : 0;
  auto* out = static_cast<uint8_t*>(buffer);
  if (have) std::memcpy(out, &sound_[from], have);
  // Past the end: silence (8-bit samples centre on 0x80).
  const uint8_t quiet = info_.audio_format.size() >= 16 && info_.audio_format[14] == 8 ? 0x80 : 0;
  if (have < static_cast<size_t>(bytes)) std::memset(out + have, quiet, static_cast<size_t>(bytes) - have);
  return 0;
}

// AVIStreamFindSample on the video's key frames: FIND_NEXT (1) the first at
// or after `pos`, FIND_PREV (4) the last at or before it; -1 without one.
int32_t MovieStream::find_sample(int32_t pos, uint32_t flags) const {
  if (!(flags & 0x10)) return pos;
  if (flags & 4) {
    int32_t best = -1;
    for (int32_t k : info_.keys)
      if (k <= pos) best = k;
    return best;
  }
  for (int32_t k : info_.keys)
    if (k >= pos) return k;
  return -1;
}

int32_t MovieStream::decompress(uint16_t* out, int32_t width, int32_t height, int32_t pitch, uint32_t r, uint32_t g,
                                uint32_t b) {
  if (!plm_ || chunk_ < 0) return -1;
  // Frame n is MPEG frames 2n and 2n + 1: decoded up to 2n, or kept when the
  // last decoded is one of them (from the start again for an earlier frame).
  const int32_t want = 2 * chunk_ + 1;  // MPEG frames decoded once 2n is
  if (decoded_ > want + 1) plm_video_rewind(plm_), decoded_ = 0, have_rgb_ = false;
  const int32_t w = plm_video_get_width(plm_), h = plm_video_get_height(plm_);
  plm_frame_t* last = nullptr;
  while (decoded_ < want) {
    plm_frame_t* f = plm_video_decode(plm_);
    if (!f) break;  // a short stream holds its last picture
    ++decoded_;
    last = f;
  }
  if (last) {
    rgb_.resize(static_cast<size_t>(w) * h * 3);
    plm_frame_to_rgb(last, rgb_.data(), w * 3);
    have_rgb_ = true;
  }
  if (!have_rgb_) return 0;
  // To the display's 16 bits: each channel's top bits under its mask.
  auto put = [](uint32_t mask, uint8_t v) -> uint32_t {
    if (!mask) return 0;
    int lo = 0, bits = 0;
    while (!(mask >> lo & 1)) ++lo;
    while (mask >> (lo + bits) & 1) ++bits;
    return (static_cast<uint32_t>(v) >> (8 - bits)) << lo & mask;
  };
  const int32_t rows = std::min(height, h), cols = std::min(width, w);
  for (int32_t y = 0; y < rows; ++y) {
    uint16_t* row = reinterpret_cast<uint16_t*>(reinterpret_cast<uint8_t*>(out) + static_cast<size_t>(y) * pitch);
    const uint8_t* src = &rgb_[static_cast<size_t>(y) * w * 3];
    for (int32_t x = 0; x < cols; ++x, src += 3)
      row[x] = static_cast<uint16_t>(put(r, src[0]) | put(g, src[1]) | put(b, src[2]));
  }
  return 0;
}

bool scan_imported_movie(const std::string& base, std::vector<MovieFrameScan>* out, int32_t* w, int32_t* h,
                         const MovieScanGate& gate) {
  out->clear();
  std::unique_ptr<MovieStream> m = MovieStream::open(base, kVids);
  if (!m || m->info().video_format.size() < 12) return false;
  int32_t fw = 0, fh = 0;
  std::memcpy(&fw, &m->info().video_format[4], 4);
  std::memcpy(&fh, &m->info().video_format[8], 4);
  fh = fh < 0 ? -fh : fh;
  if (fw <= 0 || fh <= 0 || fw > 4096 || fh > 4096) return false;
  *w = fw, *h = fh;
  const int32_t pitch = ((fw + 3) & ~3) * 2;
  std::vector<uint16_t> px(static_cast<size_t>(pitch / 2) * fh);
  const int32_t n = static_cast<int32_t>(m->info().video.length);
  out->reserve(static_cast<size_t>(n > 0 ? n : 0));
  for (int32_t f = 0; f < n; ++f) {
    if (gate && !gate()) return false;
    m->read(f, 1, nullptr, 0);
    if (m->decompress(px.data(), fw, fh, pitch, 0xf800, 0x07e0, 0x001f) != 0) return false;
    out->push_back(movie_scan_frame(px.data(), fw, fh, pitch, 0xf800, 0x07e0, 0x001f));
  }
  return true;
}

std::string measure_imported_movie(const std::string& base) {
  std::unique_ptr<MovieStream> m = MovieStream::open(base, kVids);
  if (!m) return {};
  const zfmv::AviStreamInfo& info = m->info().video;
  const int32_t fps = info.scale ? static_cast<int32_t>(info.rate / info.scale) : 15;
  std::vector<MovieFrameScan> frames;
  int32_t w = 0, h = 0;
  if (!scan_imported_movie(base, &frames, &w, &h)) return {};
  const std::vector<MovieShapeRun> runs = movie_shape_runs(frames, w, h, fps);
  return runs.empty() ? std::string() : movie_shape_line(movie_shape_key(base, info.length, w, h), runs);
}

}  // namespace mech3
