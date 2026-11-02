#include "gamez/zfmv/fmv_script.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "crt/msvc_math.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zvideo/zvid_format.h"

#pragma STDC FP_CONTRACT OFF

namespace zfmv {

namespace {

Host g_default;
Host* g_host = &g_default;

uint32_t dbl_lo(double d) {
  uint64_t q;
  std::memcpy(&q, &d, sizeof q);
  return static_cast<uint32_t>(q);
}
uint32_t dbl_hi(double d) {
  uint64_t q;
  std::memcpy(&q, &d, sizeof q);
  return static_cast<uint32_t>(q >> 32);
}
// A rect argument as the hooks report it: present, then its four words.
struct RectArgs {
  uint32_t on, l, t, r, b;
  explicit RectArgs(const Rect* p)
      : on(p != nullptr),
        l(p ? static_cast<uint32_t>(p->left) : 0),
        t(p ? static_cast<uint32_t>(p->top) : 0),
        r(p ? static_cast<uint32_t>(p->right) : 0),
        b(p ? static_cast<uint32_t>(p->bottom) : 0) {}
};

}  // namespace

Host& host() { return *g_host; }
void set_host(Host* h) { g_host = h ? h : &g_default; }

// The default Host: every call reports itself as unported.
Handle Host::target_base() { return unported("0x00576d80", {}); }
Handle Host::target_width() { return unported("0x00576d90", {}); }
Handle Host::target_height() { return unported("0x00576da0", {}); }
Handle Host::target_pitch() { return unported("0x00576db0", {}); }
Handle Host::render_base() { return unported("0x00576ca0", {}); }
Handle Host::render_pitch() { return unported("0x00576cd0", {}); }
Handle Host::screen_width() { return unported("0x00576cb0", {}); }
Handle Host::screen_height() { return unported("0x00576cc0", {}); }
int32_t Host::video_mode() { return static_cast<int32_t>(unported("[0x0072f348]", {})); }
void Host::video_lock() { unported("0x00576de0", {}); }
void Host::video_unlock() { unported("0x00576e70", {}); }
void Host::lock_3d() { unported("0x00576d00", {}); }
void Host::unlock_3d() { unported("0x00576d60", {}); }
void Host::video_flip(Handle a, Handle b, int32_t c, int32_t d) { unported("0x00576ea0", {a, b, c, d}); }
Handle Host::video_grab(int32_t buffer) { return unported("0x00579500", {buffer}); }
void Host::copy_to_second(const Rect* src, const Rect* dst) {
  const RectArgs s(src), d(dst);
  unported("[0x0072f358]", {s.on, s.l, s.t, s.r, s.b, d.on, d.l, d.t, d.r, d.b});
}
void Host::copy_from_second(const Rect* src, const Rect* dst) {
  const RectArgs s(src), d(dst);
  unported("[0x0072f35c]", {s.on, s.l, s.t, s.r, s.b, d.on, d.l, d.t, d.r, d.b});
}
void Host::image_stretch(Handle image, int32_t key, const Rect* src, const Rect* dst) {
  const RectArgs s(src), d(dst);
  unported("[0x0072f360]", {image, key, s.on, s.l, s.t, s.r, s.b, d.on, d.l, d.t, d.r, d.b});
}
void Host::scene_begin() { unported("0x00579af0", {}); }
void Host::draw_tint_quad() { unported("[0x0072f3ec]", {}); }
void Host::scene_end() { unported("0x00579b10", {}); }
void Host::set_finish_target(Handle base, Handle width, Handle height, Handle pitch) {
  unported("0x00558290", {base, width, height, pitch});
}
void Host::render_target(int32_t* w, int32_t* h, int32_t* depth, int32_t* pitch) {
  *w = static_cast<int32_t>(unported("0x00558080", {}));
  *h = static_cast<int32_t>(unported("0x00558080.h", {}));
  (void)depth;
  (void)pitch;
}
void Host::image_draw(Handle image, int32_t x, int32_t y, int32_t key, const Rect* src) {
  const RectArgs s(src);
  unported("0x0055b1f0", {image, x, y, key, s.on, s.l, s.t, s.r, s.b});
}
void Host::tint(uint16_t colour, const Rect* rect, double alpha) {
  const RectArgs r(rect);
  unported("0x00558540", {colour, r.on, r.l, r.t, r.r, r.b, dbl_lo(alpha), dbl_hi(alpha)});
}
void Host::finish() { unported("0x00558610", {}); }
void Host::blur(const Rect* rect, int32_t kind) {
  const RectArgs r(rect);
  unported("0x0055a540", {r.on, r.l, r.t, r.r, r.b, kind});
}
Handle Host::image_get(const char* name) { return unported("0x005179d0", {name}); }
void Host::image_release(Handle image) { unported("0x00517b10", {image}); }
void Host::image_path(const char* path) { unported("0x00516f50", {path}); }
void Host::sound_load_set(const char* name) { unported("0x0056f520", {name}); }
Handle Host::sound_snapshot() { return unported("0x0056eb10", {}); }
void Host::sound_stop_snapshot(Handle snapshot) { unported("0x0056f0a0", {snapshot}); }
Handle Host::sound_find(const char* name) { return unported("0x0056f6f0", {name}); }
Handle Host::sound_play(Handle sound, float volume) { return unported("0x0056e3d0", {sound, zgame::bits(volume)}); }
void Host::sound_stop(Handle voice) { unported("0x0056e9f0", {voice}); }
void Host::input_clear() { unported("0x0051a300", {}); }
uint32_t Host::key_state(int32_t dik) { return unported("0x0051a9b0", {dik}); }
int32_t Host::file_stat(const char* path) { return static_cast<int32_t>(unported("_stat", {path})); }
double Host::now() { return zgame::from_bits(unported("timeGetTime", {})); }
void Host::avi_file_init() { unported("AVIFileInit", {}); }
void Host::avi_file_exit() { unported("AVIFileExit", {}); }
int32_t Host::avi_open(Handle* stream, const char* path, uint32_t type) {
  *stream = 0;
  return static_cast<int32_t>(unported("AVIStreamOpenFromFileA", {path, type}));
}
int32_t Host::avi_read_format(Handle stream, void* format, int32_t* size) {
  return static_cast<int32_t>(unported("AVIStreamReadFormat", {stream, format != nullptr, format ? *size : 0}));
}
int32_t Host::avi_length(Handle stream) { return static_cast<int32_t>(unported("AVIStreamLength", {stream})); }
int32_t Host::avi_info(Handle stream, AviStreamInfo*) { return static_cast<int32_t>(unported("AVIStreamInfoA", {stream})); }
int32_t Host::avi_read(Handle stream, int32_t start, int32_t samples, void*, int32_t bytes) {
  return static_cast<int32_t>(unported("AVIStreamRead", {stream, start, samples, bytes}));
}
int32_t Host::avi_find_sample(Handle stream, int32_t pos, uint32_t flags) {
  return static_cast<int32_t>(unported("AVIStreamFindSample", {stream, pos, flags}));
}
void Host::avi_release(Handle stream) { unported("AVIStreamRelease", {stream}); }
Handle Host::ic_locate(uint32_t type, uint32_t handler, const void*, const void* out, uint32_t mode) {
  // The output format's size, depth, compression and image size.
  uint32_t w[6] = {};
  if (out) std::memcpy(w, out, sizeof w);
  const uint16_t bits = static_cast<uint16_t>(w[3] >> 16);
  return unported("ICLocate", {type, handler, mode, w[0], bits, w[4], w[5]});
}
int32_t Host::ic_send(Handle hic, uint32_t message, const void* a, const void* b) {
  return static_cast<int32_t>(unported("ICSendMessage", {hic, message, a != nullptr, b != nullptr}));
}
int32_t Host::ic_decompress(Handle hic, uint32_t flags, const void*, const void*, const void*, void*) {
  return static_cast<int32_t>(unported("ICDecompress", {hic, flags}));
}
void Host::ic_close(Handle hic) { unported("ICClose", {hic}); }
uint32_t Host::display_bits() { return unported("0x00576c70", {}); }
Handle Host::stream_image(Stream*) { return unported("@stream_image", {}); }
Handle Host::sound_stream_create(const void*, const void*, uint32_t bytes) {
  return unported("0x005758c0", {bytes});
}
int32_t Host::sound_stream_lock(Handle sound, uint32_t offset, uint32_t bytes, void** p1, uint32_t* n1, void** p2,
                                uint32_t* n2) {
  *p1 = *p2 = nullptr, *n1 = *n2 = 0;
  return static_cast<int32_t>(unported("0x00575550", {sound, offset, bytes}));
}
void Host::sound_stream_unlock(Handle sound, void*, uint32_t n1, void*, uint32_t n2) {
  unported("0x00575600", {sound, n1, n2});
}
uint32_t Host::sound_stream_position(Handle sound) { return unported("0x00575690", {sound}); }
void Host::sound_stream_free(Handle sound) { unported("0x00575980", {sound}); }

// ---------------------------------------------------------------------------
// The images

// Mech3.exe 1.2 @ 0x00599760: drawn at x, y. The rect's other two words are a
// static the exe never sets (0).
ImageAction::ImageAction(const char* name, bool flip, int32_t x, int32_t y)
    : name(name), flip(flip), placed(true), rect{x, y, 0, 0} {}

// Mech3.exe 1.2 @ 0x00599820: stretched over the target, its size taken now.
ImageAction::ImageAction(const char* name, bool flip) : name(name), flip(flip), placed(false) {
  int32_t w = 0, h = 0, depth = 0, pitch = 0;
  host().render_target(&w, &h, &depth, &pitch);
  rect = {0, 0, w, h};
}

// Mech3.exe 1.2 @ 0x005998d0
ImageAction::~ImageAction() { ImageAction::end(); }

// Mech3.exe 1.2 @ 0x00599930
void ImageAction::start(double) { image = host().image_get(name.c_str()); }

// Mech3.exe 1.2 @ 0x00599950: once in software, twice in Direct3D (both pages
// of the flip chain); a placed image (or any, in mode 2) through the locked
// buffer, else stretched. Done at once.
bool ImageAction::step(double) {
  Host& h = host();
  int32_t pages = (h.video_mode() != 0) + 1;
  if (!image) return false;
  do {
    if (!placed && h.video_mode() != 2) {
      h.image_stretch(image, 0, nullptr, &rect);
    } else {
      h.video_lock();
      h.image_draw(image, rect.left, rect.top, 0, nullptr);
      h.video_unlock();
    }
    if (flip) h.video_flip(0, 0, 1, 1);
  } while (--pages);
  return false;
}

// Mech3.exe 1.2 @ 0x005999d0
void ImageAction::end() {
  if (image) {
    host().image_release(image);
    image = 0;
  }
}

// ---------------------------------------------------------------------------
// The wait

// Mech3.exe 1.2 @ 0x0059a460
void WaitAction::start(double t) { started = static_cast<float>(t); }

// Mech3.exe 1.2 @ 0x0059a470: the end time a float sum (the x87 at 24 bits).
bool WaitAction::step(double t) { return t < static_cast<double>(started + seconds); }

// Mech3.exe 1.2 @ 0x0059a490: the page shown.
void WaitAction::end() { host().video_flip(0, 0, 1, 1); }

// ---------------------------------------------------------------------------
// The fades

// Mech3.exe 1.2 @ 0x005999f0
FadeAction::FadeAction(uint8_t r, uint8_t g, uint8_t b, float seconds, int32_t out, float alpha)
    : out(out), colour(zvid::pack_bytes(r, g, b)), seconds(seconds), alpha(alpha) {}

// Mech3.exe 1.2 @ 0x00599a40: the screen grabbed (buffer 1).
void FadeAction::start(double t) {
  image = host().video_grab(1);
  started = t;
}

// Mech3.exe 1.2 @ 0x00599ab0: the still, then the colour over it. Time-based.
bool FadeAction::step(double t) {
  if (!image) return false;
  Host& h = host();
  // The x87 at 24 bits: each result a float.
  float f = static_cast<float>(static_cast<float>(t - started) / seconds);
  bool going = true;
  if (out == 0) {
    f = 1.0f - f;
    if (!(f > 0.0f)) {  // x87 "not above": NaN ends it too
      f = 0.0f;
      going = false;
    }
  } else if (f > 1.0f) {
    f = 1.0f;
    going = false;
  }
  const bool d3d = h.video_mode() != 0;
  if (d3d) h.lock_3d();
  else h.video_lock();
  h.image_draw(image, 0, 0, 0, nullptr);
  if (d3d) h.unlock_3d();
  h.tint(colour, nullptr, static_cast<double>(alpha * f));
  if (h.video_mode() != 0) {
    h.scene_begin();
    h.draw_tint_quad();
    h.scene_end();
    h.video_flip(0, 0, 0, 0);
  } else {
    h.finish();
    h.video_unlock();
    h.video_flip(0, 0, 1, 1);
  }
  return going;
}

// Mech3.exe 1.2 @ 0x00599be0: the still released through zImage (0x00517b10).
void FadeAction::end() {
  if (image) {
    host().image_release(image);
    image = 0;
  }
}

// ---------------------------------------------------------------------------
// The movies

// Mech3.exe 1.2 @ 0x00599c00: "<dir>\<name>", dropped when not found.
namespace {
// Ours, for diagnosis (MW3_MOVIE_TRACE): each new frame's times, in ms since
// the movie's first step.
bool movie_trace() {
  static const bool on = std::getenv("MW3_MOVIE_TRACE") != nullptr;
  return on;
}
double now_ms() {
  using C = std::chrono::steady_clock;
  return std::chrono::duration<double, std::milli>(C::now().time_since_epoch()).count();
}
}  // namespace

// FIX (A): the original then looked for the file on a CD-ROM drive (0x00575d40);
// the movies come from the imported folder (docs/protection.md).
AviAction::AviAction(const char* dir, const char* name, bool flag) : flag(flag) {
  path = std::string(dir ? dir : "(null)") + "\\" + (name ? name : "(null)");
  if (host().file_stat(path.c_str()) == -1) path.clear();
}

// Mech3.exe 1.2 @ 0x00599d80: the path only. Quirk kept: a movie deleted
// without its end (the script rewound and deleted mid-way) keeps its stream,
// and its sound buffer loops on (docs/fmv.md).
AviAction::~AviAction() {}

// Mech3.exe 1.2 @ 0x00599ea0: the stream opened (new(0x1e4)), the rect the
// render target's size.
void AviAction::start(double) {
  if (path.empty()) return;
  stream = new Stream(path.c_str(), flag);
  rect = {};
  int32_t w = 0, h = 0, depth = 0, pitch = 0;
  host().render_target(&w, &h, &depth, &pitch);
  rect.right = w, rect.bottom = h;
  frame = -1;
  if (movie_trace())
    std::printf("mw3: movie trace: %s opened at %.1f ms (%d frames, %u a second)\n", path.c_str(), now_ms(),
                stream ? stream->length : 0, stream ? stream->fps : 0u);
}


// Mech3.exe 1.2 @ 0x00599de0: the frame for the time since the first step
// (x87 at 24 bits, truncated), drawn and shown when it changes; done once the
// stream has passed its last frame. In mode 2 the decode is outside the 2D
// lock and the flip is without the copy.
bool AviAction::step(double t) {
  if (path.empty()) return false;
  Host& h = host();
  bool going = true;
  if (frame < 0) started = t;
  const float since = static_cast<float>(t - started);
  const int32_t f = crt::msvc_ftol(since * static_cast<float>(stream ? stream->fps : 0));
  // FIX (bug): with its sound playing the picture is the sound's frame, but
  // the exe asked for it only when this clock's frame changed: two 15 fps
  // clocks started together, so each ask fell on the sound's own frame
  // change and a frame repeated and one was skipped every few (fmv.md
  // section 5). Asked each step, a frame is shown once the sound reaches it.
  // From the clock's frame 1 on: the cursor still at 0 just after the start
  // reads as the end (the stream's quirk).
  if (zfix::options().bugs && stream && f >= 1 && frame >= 0) {
    const int32_t s = stream->sound_frame();
    if (s >= 0) {
      if (s == sound_seen) return going;
      sound_seen = s;
    } else if (f == frame) {
      return going;
    }
  } else if (f == frame) {
    return going;
  }
  const bool trace = movie_trace();
  const double t0 = trace ? now_ms() : 0;
  const int32_t before = stream ? stream->shown : -1;
  double t1 = 0;
  if (h.video_mode() == 2) {
    if (!(stream ? stream->frame(f) : 0)) going = false;
    if (trace) t1 = now_ms();
    h.image_stretch(h.stream_image(stream), 0, nullptr, &rect);
    h.video_flip(0, 0, 1, 0);
  } else {
    h.video_lock();
    if (!(stream ? stream->frame(f) : 0)) going = false;
    if (trace) t1 = now_ms();
    h.video_unlock();
    h.image_stretch(h.stream_image(stream), 0, nullptr, &rect);
    h.video_flip(0, 0, 1, 1);
  }
  if (trace) {
    // The clock's frame, the one decoded (the sound's, with sound), how many
    // the decode passed over since the last shown, the decode and the flip.
    if (!trace_t0) trace_t0 = t0, trace_last = t0;
    const int32_t got = stream ? stream->shown : -1;
    const int32_t skipped = before >= 0 && got > before + 1 ? got - before - 1 : 0;
    trace_skipped += skipped;
    const double t2 = now_ms();
    std::printf("mw3: movie trace %.1f ms: clock frame %d, shown %d (skipped %d, %d so far), gap %.1f, decode %.1f, flip %.1f\n",
                t0 - trace_t0, f, got, skipped, trace_skipped, t0 - trace_last, t1 - t0, t2 - t1);
    trace_last = t0;
  }
  frame = f;
  return going;
}

// Mech3.exe 1.2 @ 0x00599f40
void AviAction::end() {
  delete stream;
  stream = nullptr;
}

// Mech3.exe 1.2 @ 0x0059a220. Not ported (no script plays one): ends at once.
MciAction::MciAction(uint32_t hwnd, const char* dir, const char* name) : hwnd(hwnd) {
  path = std::string(dir ? dir : "(null)") + "\\" + (name ? name : "(null)");
}

// ---------------------------------------------------------------------------
// The blurs

// Mech3.exe 1.2 @ 0x00599f70, with the vtable the loader puts on it.
BlurAction::BlurAction(int32_t steps, int32_t passes, int32_t kind) : steps(steps), passes(passes), kind(kind) {}

// Mech3.exe 1.2 @ 0x00599f90: the finish target on the screen's size; the
// picture copied between the back buffer and the second surface.
void BlurAction::start(double) {
  Host& h = host();
  screen = {0, 0, static_cast<int32_t>(h.screen_width()), static_cast<int32_t>(h.screen_height())};
  target.left = target.top = 0;
  target.right = static_cast<int32_t>(h.target_width());
  target.bottom = static_cast<int32_t>(h.target_height());
  if (h.video_mode() != 0) {
    const Handle pitch = h.target_pitch();
    h.set_finish_target(h.target_base(), static_cast<Handle>(screen.right), static_cast<Handle>(screen.bottom),
                        pitch);
    h.copy_to_second(&target, &screen);
    return;
  }
  const Handle pitch = h.render_pitch();
  h.set_finish_target(h.render_base(), static_cast<Handle>(screen.right), static_cast<Handle>(screen.bottom), pitch);
  h.copy_from_second(&target, &screen);
}

// Mech3.exe 1.2 @ 0x0059a070 (both), 0x0059a100 (along), 0x0059a190 (down):
// in Direct3D on the locked buffer, in software on the renderer's, copied to
// the second surface; shown each step.
bool BlurAction::step(double) {
  Host& h = host();
  --steps;
  if (h.video_mode() != 0) {
    h.video_lock();
    for (int32_t n = passes; n; --n) h.blur(nullptr, kind);
    h.video_unlock();
  } else {
    h.lock_3d();
    for (int32_t n = passes; n; --n) h.blur(nullptr, kind);
    h.unlock_3d();
    h.copy_to_second(&screen, &target);
  }
  h.video_flip(0, 0, 1, 1);
  return steps != 0;
}

// Mech3.exe 1.2 @ 0x0059a040: the finish target back on the 2D target.
void BlurAction::end() {
  Host& h = host();
  const Handle pitch = h.target_pitch();
  const Handle height = h.target_height();
  const Handle width = h.target_width();
  h.set_finish_target(h.target_base(), width, height, pitch);
}

// Mech3.exe 1.2 @ 0x00599a70
void BlurAction::run() {
  start(0.0);
  while (step(0.0)) {
  }
  end();
}

// ---------------------------------------------------------------------------
// The sound

// The loader's (0x005994a2): the name strncpy'd into 0x32 bytes.
SoundAction::SoundAction(const char* name) : name(name ? std::string(name).substr(0, 0x32) : std::string()) {}

// Mech3.exe 1.2 @ 0x00599d10: found by name, its last voice stopped, played at full volume.
void SoundAction::start(double) {
  Host& h = host();
  sound = h.sound_find(name.c_str());
  if (voice) h.sound_stop(voice);
  if (sound) voice = h.sound_play(sound, 1.0f);
}

// ---------------------------------------------------------------------------
// The script

// Mech3.exe 1.2 @ 0x00598cc0. With no window the exe takes the main one
// ([0x00608f80]); the program sets it after (movie_set_window).
Script::Script(const char* file, const char* name, uint32_t hwnd) : hwnd(hwnd) {
  if (file && name) load(file, name);
}

// Mech3.exe 1.2 @ 0x00598d00
Script::~Script() { clear(true); }

// Mech3.exe 1.2 @ 0x00598d30. Rewinding does not end the current action (a
// fade's still stays held until the script is deleted).
void Script::clear(bool all) {
  if (!all) {
    current = head;
    return;
  }
  for (Action* a = head; a;) {
    Action* n = a->next;
    delete a;
    a = n;
  }
  tail = current = head = nullptr;
}

// Mech3.exe 1.2 @ 0x00599570
bool Script::append(Action* a) {
  if (!a) return false;
  a->next = nullptr;
  if (!tail) {
    tail = head = current = a;
    return true;
  }
  tail->next = a;
  tail = a;
  return true;
}

// Mech3.exe 1.2 @ 0x00598d80
int32_t Script::load(const char* file, const char* name) {
  zrdr::Node* root = zrdr::load(file);
  if (!root) return -1;  // "Failed to find FMV definitions (fmv.zrd)" to the empty report
  const int32_t n = load_tree(root, name);
  zrdr::free_node(root);  // the original leaks the tree when the block is missing
  return n;
}

namespace {
// An entry's word i. FIX (bug): past a short entry the original reads the heap.
const zrdr::Node* item(const zrdr::Node* e, int32_t i) {
  static const zrdr::Node none{zrdr::Int, {0}};
  return i < zrdr::list_count(e) ? &e->list[i] : &none;
}
}  // namespace

int32_t Script::load_tree(zrdr::Node* root, const char* name) {
  if (const char* p = zrdr::get_string(root, "FMV_PATH")) path = p;
  host().image_path(zrdr::get_string(root, "IMAGE_PATH"));
  zrdr::Node* block = zrdr::find(root, name);
  if (!block) return 0;
  const int32_t entries = zrdr::list_count(block) - 1;
  for (int32_t i = 1; i <= entries; ++i) {
    const zrdr::Node* e = &block->list[i];
    if (e->type != zrdr::List) return 0;  // "Error in parsing fmv actions: file=%s, tag=%s"
    const zrdr::Node* entry = e;
    const char* verb = entry->list[1].s;
    const int32_t count = zrdr::list_count(entry);
    const char* arg = item(entry, 2)->s;
    auto word = [&](int32_t k) { return item(entry, k)->i; };
    auto real = [&](int32_t k) { return item(entry, k)->f; };
    Action* a = nullptr;
    bool known = true;
    if (!std::strcmp(verb, "SHOWIMAGE")) {
      a = count == 5 ? new ImageAction(arg, true, word(3), word(4)) : new ImageAction(arg, true);
    } else if (!std::strcmp(verb, "BLITIMAGE")) {
      a = new ImageAction(arg, true, word(3), word(4));
    } else if (!std::strcmp(verb, "LOADIMAGE")) {
      a = count == 5 ? new ImageAction(arg, false, word(3), word(4)) : new ImageAction(arg, false);
    } else if (!std::strcmp(verb, "WAIT")) {
      a = new WaitAction(real(2));
    } else if (!std::strcmp(verb, "FADEIN") || !std::strcmp(verb, "FADEOUT")) {
      const zrdr::Node* rgb = item(entry, 2);
      auto c = [&](int32_t k) { return static_cast<uint8_t>(rgb->type == zrdr::List ? item(rgb, k)->i : 0); };
      a = new FadeAction(c(1), c(2), c(3), real(3), verb[4] == 'O', real(4));
    } else if (!std::strcmp(verb, "PLAYAVI")) {
      a = new AviAction(path.c_str(), arg, count > 3 ? word(3) != 0 : true);
    } else if (!std::strcmp(verb, "PLAYMCI")) {
      a = new MciAction(hwnd, path.c_str(), arg);
    } else if (!std::strcmp(verb, "BLUR")) {
      a = new BlurAction(1, word(2), 3);
    } else if (!std::strcmp(verb, "BLURH")) {
      a = new BlurAction(1, word(2), 1);
    } else if (!std::strcmp(verb, "BLURV")) {
      a = new BlurAction(1, word(2), 2);
    } else if (!std::strcmp(verb, "PLAYSOUND")) {
      a = new SoundAction(arg);
    } else {
      known = false;  // skipped
    }
    if (known) append(a);
  }
  return entries;
}

// Mech3.exe 1.2 @ 0x005995b0: the finish target on the 2D target, the FMV
// sound set loaded, the keys cleared, the first action started at 0.
bool Script::play_at(double t) {
  if (!current) return false;
  Host& h = host();
  const Handle pitch = h.target_pitch();
  const Handle height = h.target_height();
  const Handle width = h.target_width();
  h.set_finish_target(h.target_base(), width, height, pitch);
  h.sound_load_set("FMV");
  h.input_clear();
  started = t;
  current->start(0.0);
  return true;
}

// Mech3.exe 1.2 @ 0x00599620: Space, Escape, Enter or the keypad's Enter
// (held, pressed or let go) skips the rest and stops the sounds playing; else
// the current action steps, and the next starts when it is done. True while
// it runs, and once more on the poll the last action ends.
bool Script::playing_at(double t) {
  if (!current) return false;
  Host& h = host();
  for (int32_t key : {0x39, 0x01, 0x1c, 0x9c}) {
    if (h.key_state(key) != 0) {
      current->end();
      h.sound_stop_snapshot(h.sound_snapshot());
      current = nullptr;
      return false;
    }
  }
  t = static_cast<double>(static_cast<float>(t - started));  // the x87 at 24 bits
  if (!current->step(t)) {
    current->end();
    current = current->next;
    if (current) current->start(t);
  }
  return true;
}

}  // namespace zfmv
