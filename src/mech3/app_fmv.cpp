// The program's zFMV (gamez/zfmv, docs/fmv.md): the movie scripts the states
// play through the Engine's movie hooks, and the pause screen's blur, on
// GameEngine's 16-bit buffer and Render3D. Our addition, not original code:
// the zfmv::Host the exe's modules stood behind.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

#include "gamez/zfmv/fmv_script.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zrender/zrndr_blur.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/ztwod/twod_host.h"
#include "mech3/app.h"
#include "mech3/app_engine.h"
#include "mech3/app_movie.h"
#include "mech3/movie_shape.h"
#include "gamez/zutil/zutl_path.h"
#include "gamez/zvideo/zvid_ddd3d.h"
#include "platform/vfw.h"

namespace mech3 {

namespace {

constexpr uint32_t kVidsType = 0x73646976;  // 'vids'
constexpr const char* kShapesFile = "movie-shapes.txt";  // in the write dir (ours, docs/fmv.md section 8)

class GameFmvHost : public zfmv::Host {
 public:
  explicit GameFmvHost(GameEngine* e) : e_(e) {}
  ~GameFmvHost() override { shapes_.reset(); }  // its measuring thread stopped first
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return e_->unported(key, args);
  }

  Handle target_base() override { return e_->video_target(); }
  Handle target_width() override { return static_cast<Handle>(e_->buffer_width()); }
  Handle target_height() override { return static_cast<Handle>(e_->buffer_height()); }
  Handle target_pitch() override { return static_cast<Handle>(e_->buffer_width() * 2); }
  Handle render_base() override { return e_->video_target(); }
  Handle render_pitch() override { return static_cast<Handle>(e_->buffer_width() * 2); }
  Handle screen_width() override { return static_cast<Handle>(e_->buffer_width()); }
  Handle screen_height() override { return static_cast<Handle>(e_->buffer_height()); }
  int32_t video_mode() override { return e_->video_mode(); }
  void video_lock() override { e_->video_lock(); }
  void video_unlock() override { e_->video_unlock(); }
  void lock_3d() override { e_->video_begin_3d(); }
  void unlock_3d() override { e_->video_end_3d(); }
  void video_flip(Handle a, Handle b, int32_t c, int32_t d) override {
    e_->video_flip(static_cast<int32_t>(a), static_cast<int32_t>(b), c, d);
  }
  Handle video_grab(int32_t buffer) override { return e_->video_grab(buffer); }
  // FIX (A): one 2D buffer stands for the back buffer and the second surface,
  // so the blur's copies between them are nothing to do.
  void copy_to_second(const zfmv::Rect*, const zfmv::Rect*) override {}
  void copy_from_second(const zfmv::Rect*, const zfmv::Rect*) override {}
  // A movie's frame in its shape on a wide screen (movie_shape.h); a still
  // image kept in its own shape (GameEngine::fmv_stretch).
  void image_stretch(Handle image, int32_t, const zfmv::Rect*, const zfmv::Rect* dst) override {
    auto* t = texture(image);
    if (!t || !dst) return;
    int32_t top = -1, bottom = -1;
    if (t == shown_image_) frame_rows(shown_frame_, &top, &bottom);
    e_->fmv_stretch(t, dst->left, dst->top, dst->right, dst->bottom, top, bottom);
  }
  void scene_begin() override { e_->video_begin_scene(); }
  void draw_tint_quad() override { e_->video_draw_3d(); }
  void scene_end() override { e_->video_end_scene(); }
  // The 2D lock points the finish target at the buffer (GameEngine::video_lock).
  void set_finish_target(Handle, Handle, Handle, Handle) override {}
  void render_target(int32_t* w, int32_t* h, int32_t*, int32_t*) override {
    *w = e_->buffer_width();
    *h = e_->buffer_height();
  }
  void image_draw(Handle image, int32_t x, int32_t y, int32_t key, const zfmv::Rect* src) override {
    if (e_->is_grab(image)) return e_->image_draw(image, x, y, key, 0);
    ztwod::host().blit(texture(image), x, y, key, reinterpret_cast<const ztwod::Rect*>(src));
  }
  void tint(uint16_t colour, const zfmv::Rect*, double alpha) override { e_->screen_tint(colour, 0, alpha); }
  void finish() override { e_->render_finish(); }
  void blur(const zfmv::Rect* r, int32_t kind) override {
    zrndr::blur(r ? &r->left : nullptr, kind);
  }
  Handle image_get(const char* name) override { return e_->handle_of(ztwod::host().image_get(name)); }
  void image_release(Handle image) override {
    if (e_->is_grab(image)) return e_->image_free(image);
    ztwod::host().image_release(texture(image));
  }
  void image_path(const char* path) override { ztwod::host().image_path(path); }
  void sound_load_set(const char* name) override { e_->voice_start(name); }
  Handle sound_snapshot() override { return e_->sound_listener(); }
  void sound_stop_snapshot(Handle s) override { e_->sound_stop_all(s); }
  Handle sound_find(const char* name) override { return e_->sound_find(name); }
  Handle sound_play(Handle sound, float volume) override {
    auto* o = static_cast<zsnd::Object*>(e_->object_of(sound));
    return o ? e_->handle_of(zsnd::play(o, volume)) : 0;
  }
  void sound_stop(Handle voice) override { e_->voice_stop(voice); }
  void input_clear() override { e_->input_clear(); }
  uint32_t key_state(int32_t dik) override { return e_->key_state(dik); }
  // The movies are the importer's files (app_movie.h): "found" when the
  // The AVI through the helper's Video for Windows, else the imported .fmv.
  // FIX (A): no AVIFile or Indeo 5 here (docs/fmv.md, The Indeo helper).
  int32_t file_stat(const char* path) override { return remote_path(path).empty() && movie_base(path).empty() ? -1 : 0; }

  // Video for Windows: Windows' own and the install's codec in the 32-bit
  // helper when there is one (platform::VideoForWindows), else the imported
  // files (FIX (A), app_movie.h).
  void avi_file_init() override {}
  void avi_file_exit() override {}
  int32_t avi_open(Handle* stream, const char* path, uint32_t type) override {
    *stream = 0;
    auto o = std::make_unique<Open>();
    o->video = type == 0x73646976;  // 'vids'
    const std::string host = remote_path(path);
    std::unique_lock<std::mutex> lock(vfw_mu_);
    if (!host.empty() && vfw()->open(host, type, &o->remote) == 0 && o->remote) {
      vfw()->format(o->remote, &o->format);
      vfw()->info(o->remote, &o->info);
      lock.unlock();
    } else {
      lock.unlock();
      o->remote = 0;
      o->imported = MovieStream::open(movie_base(path), type);
      if (!o->imported) return -1;  // AVIERR_FILEOPEN
      o->format = o->video ? o->imported->info().video_format : o->imported->info().audio_format;
      o->info = o->video ? o->imported->info().video : o->imported->info().audio;
    }
    if (o->video) shape_open(*o, host.empty() ? movie_base(path) : host, !host.empty());
    *stream = e_->handle_of(o.get());
    streams_[*stream] = std::move(o);
    return 0;
  }
  int32_t avi_read_format(Handle stream, void* format, int32_t* size) override {
    Open* m = movie(stream);
    if (!m) return -1;
    const std::vector<uint8_t>& f = m->format;
    if (!format) return *size = static_cast<int32_t>(f.size()), 0;
    std::memcpy(format, f.data(), std::min(f.size(), static_cast<size_t>(*size < 0 ? 0 : *size)));
    return 0;
  }
  int32_t avi_length(Handle stream) override {
    Open* m = movie(stream);
    if (!m) return 0;
    if (!m->remote) return static_cast<int32_t>(m->info.length);
    std::lock_guard<std::mutex> g(vfw_mu_);
    return vfw()->length(m->remote);
  }
  int32_t avi_info(Handle stream, zfmv::AviStreamInfo* info) override {
    Open* m = movie(stream);
    if (!m) return -1;
    *info = m->info;
    return 0;
  }
  int32_t avi_read(Handle stream, int32_t start, int32_t samples, void* buffer, int32_t bytes) override {
    Open* m = movie(stream);
    if (!m) return -1;
    if (!m->remote) return m->imported->read(start, samples, buffer, bytes);
    std::lock_guard<std::mutex> g(vfw_mu_);
    return vfw()->read(m->remote, start, samples, buffer, bytes);
  }
  int32_t avi_find_sample(Handle stream, int32_t pos, uint32_t flags) override {
    Open* m = movie(stream);
    if (!m) return -1;
    if (!m->remote) return m->imported->find_sample(pos, flags);
    std::lock_guard<std::mutex> g(vfw_mu_);
    return vfw()->find_sample(m->remote, pos, flags);
  }
  void avi_release(Handle stream) override {
    auto it = streams_.find(stream);
    if (it == streams_.end()) return;
    if (decoding_ == it->second.get()) decoding_ = nullptr;
    if (it->second->video) shapes().play_end();
    if (it->second->remote && vfw()) {
      std::lock_guard<std::mutex> g(vfw_mu_);
      vfw()->release(it->second->remote);
    }
    e_->forget_object(it->second.get());
    streams_.erase(it);
  }
  // The Indeo 5 codec: the picture stream opened last decodes.
  Handle ic_locate(uint32_t, uint32_t, const void*, const void*, uint32_t) override {
    for (auto& [h, m] : streams_)
      if (m->video) decoding_ = m.get();
    return decoding_ ? 1 : 0;
  }
  int32_t ic_send(Handle, uint32_t, const void*, const void*) override { return 0; }
  int32_t ic_decompress(Handle, uint32_t flags, const void*, const void*, const void* out, void* image) override {
    if (!decoding_ || !out) return -1;
    int32_t w = 0, h = 0;
    uint16_t bits = 0;
    uint32_t size = 0, compression = 0;
    std::memcpy(&size, out, 4);
    std::memcpy(&w, static_cast<const uint8_t*>(out) + 4, 4);
    std::memcpy(&h, static_cast<const uint8_t*>(out) + 8, 4);
    std::memcpy(&bits, static_cast<const uint8_t*>(out) + 14, 2);
    std::memcpy(&compression, static_cast<const uint8_t*>(out) + 16, 4);
    if (bits != 16) return -1;
    const int32_t rows = h < 0 ? -h : h, pitch = ((w + 3) & ~3) * 2;
    if (decoding_->remote) {  // the header and, for BI_BITFIELDS, its three masks
      std::lock_guard<std::mutex> g(vfw_mu_);
      return vfw()->decompress(decoding_->remote, flags, out, size + (compression == 3 ? 12 : 0), image,
                               static_cast<size_t>(rows) * pitch);
    }
    const zvid::ChannelMasks& m = zvid::channel_masks();
    return decoding_->imported->decompress(static_cast<uint16_t*>(image), w, rows, pitch, m.r, m.g, m.b);
  }
  void ic_close(Handle) override { decoding_ = nullptr; }
  uint32_t display_bits() override { return 16; }
  // Each frame drawn passes here: counted for --movie (ours, for checking).
  Handle stream_image(zfmv::Stream* stream) override {
    if (!stream) return 0;
    GameEngine::MovieStats& st = e_->movie_stats;
    ++st.drawn;
    st.last = stream->shown, st.length = stream->length;
    shown_image_ = &stream->image, shown_frame_ = stream->shown;
    return e_->handle_of(&stream->image);
  }
  // zSound's streaming buffer (zsnd_create.cpp, ported).
  Handle sound_stream_create(const void* format, const void* data, uint32_t bytes) override {
    platform::WaveFormat wf;
    std::memcpy(&wf, format, sizeof wf);
    return e_->handle_of(zsnd::stream_create(&wf, data, bytes));
  }
  int32_t sound_stream_lock(Handle sound, uint32_t offset, uint32_t bytes, void** p1, uint32_t* n1, void** p2,
                            uint32_t* n2) override {
    *p1 = *p2 = nullptr, *n1 = *n2 = 0;
    auto* s = static_cast<zsnd::Sound*>(e_->object_of(sound));
    return s ? zsnd::stream_lock(s, offset, bytes, p1, n1, p2, n2) : 0;
  }
  void sound_stream_unlock(Handle sound, void* p1, uint32_t n1, void* p2, uint32_t n2) override {
    if (auto* s = static_cast<zsnd::Sound*>(e_->object_of(sound))) zsnd::stream_unlock(s, p1, n1, p2, n2);
  }
  uint32_t sound_stream_position(Handle sound) override {
    auto* s = static_cast<zsnd::Sound*>(e_->object_of(sound));
    return s ? zsnd::stream_position(s) : 0;
  }
  void sound_stream_free(Handle sound) override {
    if (auto* s = static_cast<zsnd::Sound*>(e_->object_of(sound))) {
      e_->forget_object(s);
      zsnd::stream_free(s);
    }
  }
  // timeGetTime() x 0.001f, the x87 at 24 bits (a float).
  double now() override {
    const mech3::Platform& p = e_->platform_parts();
    const platform::Clock* c = p.movie_clock ? p.movie_clock : p.clock;
    const uint32_t ms = c ? const_cast<platform::Clock*>(c)->ticks_ms() : 0;
    return static_cast<double>(static_cast<float>(ms) * 0.001f);
  }

 private:
  zimg::Texture* texture(Handle h) const { return static_cast<zimg::Texture*>(e_->object_of(h)); }
  // An opened stream: the helper's (remote, its id) or the imported files'.
  struct Open {
    bool video = false;
    uint32_t remote = 0;
    std::unique_ptr<MovieStream> imported;
    std::vector<uint8_t> format;
    zfmv::AviStreamInfo info;
    std::string shape_key;  // the picture's (movie_shape.h)
  };
  Open* movie(Handle h) const {
    auto it = streams_.find(h);
    return it == streams_.end() ? nullptr : it->second.get();
  }
  platform::VideoForWindows* vfw() const { return e_->platform_parts().vfw; }
  // The AVI's host path when the helper is up and the mounts have the file.
  std::string remote_path(const char* path) const {
    platform::VideoForWindows* v = vfw();
    if (!v || !path) return {};
    {
      std::lock_guard<std::mutex> g(vfw_mu_);
      if (!v->ready()) return {};
    }
    std::string p = path;
    std::replace(p.begin(), p.end(), '\\', '/');
    while (p.rfind("./", 0) == 0) p.erase(0, 2);
    return zutl::find(p.c_str());
  }
  // The movies' shapes (movie_shape.h, docs/fmv.md section 8): the shipped
  // ones from the table, others measured on a thread of their own while no
  // movie plays (never slowing one), kept in the write dir.
  MovieShapeStore& shapes() {
    if (!shapes_) shapes_ = std::make_unique<MovieShapeStore>(zutl::write_path(kShapesFile));
    return *shapes_;
  }
  // A picture stream opened: its key (name, frames, size); measured unless known.
  void shape_open(Open& o, const std::string& source, bool remote) {
    int32_t w = 0, h = 0;
    if (o.format.size() >= 12) std::memcpy(&w, &o.format[4], 4), std::memcpy(&h, &o.format[8], 4);
    o.shape_key = movie_shape_key(source, o.info.length, w, h);
    play_ = Play{o.shape_key, {}};
    const int32_t fps = o.info.scale ? static_cast<int32_t>(o.info.rate / o.info.scale) : 15;
    shapes().open(o.shape_key, fps, [this, source, remote](const MovieScanGate& gate, std::vector<MovieFrameScan>* f,
                                                           int32_t* fw, int32_t* fh) {
      return remote ? scan_remote(source, gate, f, fw, fh) : scan_imported_movie(source, f, fw, fh, gate);
    }, e_->movie_shape_first);
  }
  // Every frame through the helper (its own stream, under the helper's lock).
  bool scan_remote(const std::string& host, const MovieScanGate& gate, std::vector<MovieFrameScan>* out, int32_t* w,
                   int32_t* h) {
    platform::VideoForWindows* v = vfw();
    if (!v || !gate()) return false;  // not even opened while a movie plays
    uint32_t id = 0;
    std::vector<uint8_t> in;
    zfmv::AviStreamInfo info;
    int32_t len = 0;
    {
      std::lock_guard<std::mutex> g(vfw_mu_);
      if (v->open(host, kVidsType, &id) != 0 || !id) return false;
      v->format(id, &in);
      v->info(id, &info);
      len = v->length(id);
    }
    bool ok = in.size() >= 0x28;
    int32_t fw = 0, fh = 0;
    if (ok) std::memcpy(&fw, &in[4], 4), std::memcpy(&fh, &in[8], 4);
    fh = fh < 0 ? -fh : fh;
    ok = ok && fw > 0 && fh > 0 && fw <= 4096 && fh <= 4096;
    std::vector<uint8_t> fmt(std::max<size_t>(in.size(), 0x6c), 0);
    if (ok) {
      std::copy(in.begin(), in.end(), fmt.begin());
      auto w32 = [&](size_t at, uint32_t x) { std::memcpy(&fmt[at], &x, 4); };
      w32(0, static_cast<uint32_t>(fmt.size()));
      const uint16_t bits = 16;
      std::memcpy(&fmt[0xe], &bits, 2);
      w32(0x10, 3), w32(0x20, 0), w32(0x28, 0xf800), w32(0x2c, 0x07e0), w32(0x30, 0x001f), w32(0x34, 0);
      const int32_t top_down = -fh;
      std::memcpy(&fmt[8], &top_down, 4);
      w32(0x14, static_cast<uint32_t>(fh * ((fw + 3) & ~3) * 2));
    }
    const int32_t pitch = ((fw + 3) & ~3) * 2;
    std::vector<uint16_t> px(ok ? static_cast<size_t>(pitch / 2) * fh : 0);
    std::vector<uint8_t> chunk(info.suggested_buffer ? info.suggested_buffer : 1u << 20);
    for (int32_t f = 0; ok && f < len; ++f) {
      if (!gate()) ok = false;
      std::lock_guard<std::mutex> g(vfw_mu_);
      if (!ok || v->read(id, f, 1, chunk.data(), static_cast<int32_t>(chunk.size())) != 0) {
        ok = false;
        break;
      }
      const uint32_t flags = v->find_sample(id, f, 0x14) != f ? 0x08000000u : 0;  // FIND_PREV | FIND_KEY; not a key frame
      if (v->decompress(id, flags, fmt.data(), fmt.size() + 12, px.data(), px.size() * 2) != 0) ok = false;
      else out->push_back(movie_scan_frame(px.data(), fw, fh, pitch, 0xf800, 0x07e0, 0x001f));
    }
    {
      std::lock_guard<std::mutex> g(vfw_mu_);
      v->release(id);
    }
    *w = fw, *h = fh;
    return ok;
  }
  // The rows of the playing movie's frame `f` to show, or -1 for its whole frame.
  void frame_rows(int32_t f, int32_t* top, int32_t* bottom) {
    int16_t t = 0, b = 0;
    if (shapes().rows(play_.key, &play_.shape, f, &t, &b)) *top = t, *bottom = b;
  }
  GameEngine* e_;
  std::map<Handle, std::unique_ptr<Open>> streams_;
  Open* decoding_ = nullptr;
  mutable std::mutex vfw_mu_;  // the helper's calls: the player's and the shape scan's
  struct Play {
    std::string key;
    MovieShapePlay shape;
  };
  Play play_;
  const zimg::Texture* shown_image_ = nullptr;  // the movie's picture drawn last, and its frame
  int32_t shown_frame_ = -1;
  std::unique_ptr<MovieShapeStore> shapes_;  // last: its thread uses the members above
};

std::unique_ptr<GameFmvHost> g_host;

}  // namespace

void wire_fmv(GameEngine* e) {
  if (e) {
    g_host = std::make_unique<GameFmvHost>(e);
    zfmv::set_host(g_host.get());
  } else {
    zfmv::set_host(nullptr);
    g_host.reset();
  }
}

// The scripts (new(0x20) + 0x00598cc0(0, 0, 0)) by handle.
Handle GameEngine::movie_new() { return handle_of(new zfmv::Script(nullptr, nullptr, 0)); }
void GameEngine::movie_set_window(Handle m, uint32_t hwnd) {
  if (auto* s = static_cast<zfmv::Script*>(object_of(m))) s->hwnd = hwnd;
}
int32_t GameEngine::movie_load(Handle m, const char* file, const char* name) {
  auto* s = static_cast<zfmv::Script*>(object_of(m));
  return s ? s->load(file, name) : -1;
}
// FIX (A), playtest W47: the original's movies came off the disc, and a CD
// drive reading them stopped its own audio, so the menu music never played
// under the campaign's movie (the code itself never stops it there). A
// movie pauses the CD music while it plays; at its end the resume waits a
// pass and happens only in the same state with the CD still paused, so a
// state change (the briefing's, the main menu's end 0x00485780 stopping the
// CD) leaves no blip of menu music. The attract movie pauses it itself first
// (0x00482330) and is left to that.
void GameEngine::movie_play(Handle m) {
  auto* s = static_cast<zfmv::Script*>(object_of(m));
  if (!s) return;
  const int mode = zsnd_cd::state().mode;
  if (!movie_paused_cd_ && mode >= zsnd_cd::Once && zsnd_cd::pause()) movie_paused_cd_ = m, movie_cd_mode_ = mode;
  s->play();
}
void GameEngine::movie_cd_resume(Handle m) {
  if (!m || m != movie_paused_cd_) return;
  movie_paused_cd_ = 0;
  movie_cd_pending_ = app().current();
}
// Each pass (timer_update): the resume, if still in that state.
void GameEngine::movie_cd_pending_step() {
  if (!movie_cd_pending_) return;
  if (app().current() == movie_cd_pending_) zsnd_cd::resume(movie_cd_mode_);  // only while still paused
  movie_cd_pending_ = nullptr;
}
uint32_t GameEngine::movie_playing(Handle m) {
  auto* s = static_cast<zfmv::Script*>(object_of(m));
  const bool on = s && s->playing();
  if (!on) movie_cd_resume(m);
  return on;
}
void GameEngine::movie_stop(Handle m, int32_t all) {
  if (auto* s = static_cast<zfmv::Script*>(object_of(m))) s->stop(all != 0);
  movie_cd_resume(m);
}
void GameEngine::movie_delete(Handle m) {
  movie_cd_resume(m);
  auto* s = static_cast<zfmv::Script*>(object_of(m));
  forget_object(s);
  delete s;
}

// The pause screen's blur: 0x00599f70(4, 1) on the stack, its vtable the
// BLUR action's (0x005cf898), run as 0x00599a70 does.
Handle GameEngine::wait_new(int32_t steps, int32_t passes) {
  pause_blur_ = std::make_unique<zfmv::BlurAction>(steps, passes, 3);
  return 0;
}
void GameEngine::wait_start() {
  if (pause_blur_) pause_blur_->start(0.0);
}
uint32_t GameEngine::wait_poll() { return pause_blur_ && pause_blur_->step(0.0); }
void GameEngine::wait_end() {
  if (pause_blur_) pause_blur_->end();
  pause_blur_.reset();
}

// [0x0072f360] (0x005773e0): the whole image Blt-stretched over the rect of
// the 2D target. FIX (A): onto the 16-bit buffer, nearest pixel, as
// DirectDraw's stretch without filtering. Ours: over the whole of a buffer
// wider than 4:3 the picture keeps its shape (the original's screen was
// always 4:3): a movie's letterboxed rows at the buffer's width, anything
// else at its height, centred on black (docs/fmv.md section 8).
void GameEngine::fmv_stretch(zimg::Texture* image, int32_t left, int32_t top, int32_t right, int32_t bottom,
                             int32_t src_top, int32_t src_bottom) {
  if (!image || !image->pixels || image->width <= 0 || image->height <= 0) return;
  if (last_via_render_ && !buffer_is_screen_) buffer_is_screen_ = true;  // drawn over whatever was shown
  const int32_t iw = image->width, ih = image->height;
  int32_t sy0 = 0, rows = ih;
  if (width_ * 3 > height_ * 4 && left <= 0 && top <= 0 && right >= width_ && bottom >= height_) {
    if (src_top >= 0 && src_bottom >= src_top && src_bottom < ih) sy0 = src_top, rows = src_bottom - src_top + 1;
    // The picture's shape: a crop at the buffer's width, else the image's own.
    int64_t dw = width_, dh = static_cast<int64_t>(width_) * rows / iw;
    if (rows == ih || dh > height_) dh = height_, dw = static_cast<int64_t>(height_) * iw * ih / (static_cast<int64_t>(rows) * ih);
    if (dw > width_) dw = width_;
    std::fill(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(width_) * height_, uint16_t{0});
    left = static_cast<int32_t>((width_ - dw) / 2), top = static_cast<int32_t>((height_ - dh) / 2);
    right = left + static_cast<int32_t>(dw), bottom = top + static_cast<int32_t>(dh);
  }
  const int32_t x0 = left < 0 ? 0 : left, y0 = top < 0 ? 0 : top;
  const int32_t x1 = right > width_ ? width_ : right, y1 = bottom > height_ ? height_ : bottom;
  const int32_t w = right - left, h = bottom - top;
  if (w <= 0 || h <= 0) return;
  for (int32_t y = y0; y < y1; ++y) {
    const int32_t sy = sy0 + static_cast<int32_t>(static_cast<int64_t>(y - top) * rows / h);
    for (int32_t x = x0; x < x1; ++x) {
      const int32_t sx = static_cast<int32_t>(static_cast<int64_t>(x - left) * iw / w);
      const size_t i = static_cast<size_t>(sy) * iw + sx;
      const uint16_t c = image->palette_count ? image->palette[static_cast<const uint8_t*>(image->pixels)[i]]
                                              : static_cast<const uint16_t*>(image->pixels)[i];
      buffer_[static_cast<size_t>(y) * width_ + x] = c;
    }
  }
}

}  // namespace mech3
