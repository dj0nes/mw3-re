#pragma once
#include <cstdint>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "gamez/zfmv/fmv_stream.h"
#include "mech3/movie_shape.h"

// The importer's movies as the movie player's Video for Windows streams
// (docs/fmv.md, The imported movies). Our addition, not original code: FIX
// (A), standing for AVIFIL32 and the Indeo 5 codec. A movie is three files in
// a mounted video folder: <name>.fmv (the AVI's stream headers and key
// frames), <name>.m1v (the picture, MPEG-1 at 30 fps, every frame twice) and
// <name>.wav (the AVI's sound, byte for byte).
struct plm_video_t;

namespace mech3 {

// What mw3import wrote in <name>.fmv.
struct MovieInfo {
  zfmv::AviStreamInfo video, audio;
  std::vector<uint8_t> video_format, audio_format;  // BITMAPINFOHEADER, WAVEFORMAT
  std::vector<int32_t> keys;                         // the video's key frames
  bool has_audio = false;
};
bool read_movie_info(const std::string& path, MovieInfo* out);

// "<dir>\<name>.avi" (the script's path) to the imported files' base path
// ("<found dir>/<name>", lower case), empty when the .fmv is not mounted.
std::string movie_base(const char* avi_path);

// One opened stream (AVIStreamOpenFromFileA): the picture or the sound.
class MovieStream {
 public:
  ~MovieStream();
  static std::unique_ptr<MovieStream> open(const std::string& base, uint32_t type);
  bool video() const { return video_; }
  const MovieInfo& info() const { return info_; }
  // AVIStreamRead: the sound's bytes (silence past the end); for the picture
  // only which frame the next decompress shows.
  int32_t read(int32_t start, int32_t samples, void* buffer, int32_t bytes);
  int32_t find_sample(int32_t pos, uint32_t flags) const;
  // The frame read last, as 16-bit pixels under the masks, `pitch` bytes a row.
  int32_t decompress(uint16_t* out, int32_t width, int32_t height, int32_t pitch, uint32_t r, uint32_t g,
                     uint32_t b);
  int32_t frames_decoded() const { return decoded_; }  // MPEG frames, for the length test

 private:
  bool video_ = false;
  MovieInfo info_;
  std::string base_;
  std::vector<uint8_t> sound_;  // the .wav's data
  plm_video_t* plm_ = nullptr;
  int32_t chunk_ = -1;          // the frame AVIStreamRead asked for
  int32_t decoded_ = 0;         // MPEG frames decoded so far
  std::vector<uint8_t> rgb_;    // the last decoded MPEG frame, RGB
  bool have_rgb_ = false;
};

// Ours (movie_shape.h): every frame of the imported movie at `base`
// measured, in order, its picture's size in `w`, `h`; false when it cannot
// be read or `gate` (asked before each frame) says stop.
bool scan_imported_movie(const std::string& base, std::vector<MovieFrameScan>* out, int32_t* w, int32_t* h,
                         const MovieScanGate& gate = {});
// The imported movie at `base` measured: its cache line (movie_shape.h, the
// key as the program makes it); "" when it cannot be read.
std::string measure_imported_movie(const std::string& base);

}  // namespace mech3
