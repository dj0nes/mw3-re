#include "frontend/sdl/debug_states.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "frontend/debug_world.h"
#include "frontend/sdl/sdl_input.h"
#include "gamez/zinput/zinput.h"
#include "frontend/sdl/sdl_render3d.h"
#include "gamez/zimage/zimg_library.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zsound/zsnd.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/ztwod/twod.h"
#include "gamez/zutil/ztimer.h"
#include "gamez/zutil/zutl_path.h"
#include "mech3/app.h"
#include "mech3/app_engine.h"
#include "platform/platform.h"

namespace sdlfe {

namespace {
constexpr uint8_t kEscape = 0x01, kW = 0x11, kS = 0x1f, kA = 0x1e, kD = 0x20, kQ = 0x10, kE = 0x12, kP = 0x19,
                  kLShift = 0x2a, kRShift = 0x36, kUp = 0xc8, kDown = 0xd0, kLeft = 0xcb, kRight = 0xcd;
}  // namespace

// The mission's start as far as it is ported: the world (mission load 0x48faf0
// is not), the sound sets, then the Core state's start (0x00480c70,
// 0x00480df0): "mech_powerup" and the CD track picked as it picks it.
bool DebugMissionState::enter() {
  if (!loaded_) {
    const std::string err = frontend::world_load(o_.mission, o_.variant, render_, engine_->buffer_width(),
                                                 engine_->buffer_height());
    if (!err.empty()) {
      std::fprintf(stderr, "mw3: --mission %s: %s\n", o_.mission.c_str(), err.c_str());
      return false;
    }
    loaded_ = true;
    frontend::world_sounds_on();
    zsnd::load_set("common");
    zsnd::load_set("mech");
  }
  if (zsnd::Object* s = zsnd::find("mech_powerup")) zsnd::play(s, 1.0f);
  else std::fprintf(stderr, "mw3: mech_powerup is not in the loaded sound sets\n");
  // 0x00480df0's track: -cdN when 0 < N < the count; on a disc of 2..5 tracks
  // the mission number's; on a bigger one a random one.
  const int32_t tracks = engine_->cd_track_count();
  const int32_t forced = mech3::app().cd_track();
  int32_t track = 0;
  if (forced > 0 && forced < tracks) track = forced;
  else if (tracks > 1 && tracks <= 5) track = o_.number % (tracks - 1) + 2;
  else if (tracks > 5) track = static_cast<int32_t>(engine_->rand()) % tracks + 1;
  if (track > 0) engine_->music_play(track, 6);
  if (track > 0) std::printf("mw3: music track %d: %s\n", track, zsnd_cd::track_path(track).c_str());
  else std::printf("mw3: no music (put the disc's tracks in music/trackNN.ogg, docs/program.md)\n");
  std::printf("mw3: mission %s%s%s running: W/S/A/D move, Q/E down/up, arrows turn, Shift fast, "
              "right mouse button looks, P prints the camera, Escape quits\n",
              o_.mission.c_str(), o_.variant.empty() ? "" : " ", o_.variant.c_str());
  return true;
}

// One frame: the camera from the keyboard and the mouse (DirectInput's DIK
// codes and DIMOUSESTATE counts, as the game reads them), zSound's frame
// (0x0056dfb0(0, 1), as the Core frame calls it), the world drawn.
bool DebugMissionState::update() {
  const float dt = ztimer::state().dt;
  if (input_->key_down(kEscape)) return true;
  // The mouse straight from SDL: the free camera is ours, and zInput need
  // not hold the mouse here.
  float mx = 0, my = 0;
  input_->take_motion(&mx, &my);
  if (input_->mouse_buttons & 2) {
    o_.yaw += mx * 0.004f;
    o_.pitch = std::fmax(-1.5f, std::fmin(1.5f, o_.pitch + my * 0.004f));
  }
  const float turn = 1.5f * dt;
  o_.yaw += (input_->key_down(kRight) - input_->key_down(kLeft)) * turn;
  o_.pitch = std::fmax(-1.5f, std::fmin(1.5f, o_.pitch + (input_->key_down(kDown) - input_->key_down(kUp)) * turn));
  const bool fast = input_->key_down(kLShift) || input_->key_down(kRShift);
  const float speed = (fast ? 2000.0f : 300.0f) * dt;
  // The view's pitch is positive looking down (the render's convention).
  const float fwd[3] = {std::cos(o_.pitch) * std::sin(o_.yaw), -std::sin(o_.pitch), std::cos(o_.pitch) * std::cos(o_.yaw)};
  const float right[3] = {-std::cos(o_.yaw), 0, std::sin(o_.yaw)};
  for (int i = 0; i < 3; ++i) {
    o_.eye[i] += (input_->key_down(kW) - input_->key_down(kS)) * fwd[i] * speed;
    o_.eye[i] += (input_->key_down(kD) - input_->key_down(kA)) * right[i] * speed;
  }
  o_.eye[1] += (input_->key_down(kE) - input_->key_down(kQ)) * speed;
  if (input_->key_down(kP) && frame_ % 15 == 0)
    std::printf("mw3: --pos %.1f %.1f %.1f --look %.1f %.1f\n", o_.eye[0], o_.eye[1], o_.eye[2],
                o_.yaw / 0.0174533f, o_.pitch / 0.0174533f);
  engine_->sound_update(0, 1);
  ++frame_;
  const bool shot = !o_.shot.empty() && frame_ == o_.shot_frame;
  if (shot) render_->capture_next(o_.shot.c_str());
  frontend::world_frame(o_.eye, o_.yaw, o_.pitch, render_, -1.0f);
  if (shot) {
    std::printf("mw3: wrote %s (frame %d)\n", o_.shot.c_str(), frame_);
    return true;
  }
  return o_.frames > 0 && frame_ >= o_.frames;
}

// As the Core state leaves (0x00481fc0): the CD stopped, the sounds stopped.
void DebugMissionState::leave() {
  engine_->cd_stop();
  const zgame::Handle s = engine_->sound_listener();
  engine_->sound_stop_all(s);
  engine_->sound_release_all(s);
}

DebugDialogState::~DebugDialogState() {
  for (ztwod::Object2D* o : bound_) delete o;
}

// The dialog loaded as the shell's screens load theirs (twod.md section 3),
// its images from the image library, every BUTTONS and PRIMITIVES entry bound.
bool DebugDialogState::enter() {
  if (!dialog_) {
    zimg::state() = zimg::State{};
    zimg::state().display_format = 6;
    const std::vector<std::string> roots = zutl::mounts().read;
    for (const std::string& r : roots) zutl::mount(r + "/zbd");
    for (int l = 0; l < 2; ++l) zimg::library_list(l).clear();
    zimg::register_image_library(1);
    engine_->video_lock();
    engine_->video_clear(0);
    dialog_ = std::make_unique<ztwod::Dialog>();
    if (!dialog_->load(file_.c_str(), name_.c_str(), false)) {
      std::fprintf(stderr, "mw3: --dialog %s %s: not found\n", file_.c_str(), name_.c_str());
      dialog_.reset();
      return false;
    }
    auto bind_all = [&](const char* key, bool buttons) {
      zrdr::Node* list = dialog_->node ? zrdr::find(dialog_->node, key) : nullptr;
      if (!list || list->type != zrdr::List) return;
      for (int32_t i = 1; i + 1 < zrdr::list_count(list); i += 2) {
        if (list->list[i].type != zrdr::String) continue;
        zrdr::Node* entry = &list->list[i + 1];
        const char* n = list->list[i].s;
        if (buttons) {
          ztwod::Control* c = zrdr::find(entry, "CHECKED") ? new ztwod::CheckButton() : new ztwod::Control();
          bound_.push_back(c);
          dialog_->bind_button(c, n);
        } else {
          ztwod::Object2D* o = zrdr::find(entry, "TEXT") ? static_cast<ztwod::Object2D*>(new ztwod::Text())
                                                        : new ztwod::Image(0);
          bound_.push_back(o);
          dialog_->bind_primitive(o, n);
        }
      }
    };
    bind_all("BUTTONS", true);
    bind_all("PRIMITIVES", false);
    std::printf("mw3: dialog %s %s: %zu objects bound; Escape quits\n", file_.c_str(), name_.c_str(), bound_.size());
  }
  dialog_->set_active(1);
  return true;
}

// The shell's frame (0x0048ed30): lock, the screen's update, unlock, flip.
bool DebugDialogState::update() {
  if (input_->key_down(kEscape)) return true;
  zin::poll(false);  // the shell's frame polls zInput (0x0048ed30)
  engine_->sound_update(0, 0);
  engine_->video_lock();
  dialog_->update(ztimer::state().dt);
  engine_->video_unlock();
  ++frame_;
  const bool shot = !shot_.empty() && frame_ == shot_frame_;
  if (shot) render_->capture_next(shot_.c_str());
  engine_->video_flip(0, 0, 1, 1);
  if (shot) {
    std::printf("mw3: wrote %s (frame %d)\n", shot_.c_str(), frame_);
    return true;
  }
  return false;
}

void DebugDialogState::leave() {
  if (dialog_) dialog_->set_active(0);
}

bool DebugMovieState::enter() {
  movie_ = engine_->movie_new();
  const zgame::Handle frame = mech3::app().frame_;
  if (const zgame::Handle hwnd = frame ? engine_->frame_hwnd(frame) : 0) engine_->movie_set_window(movie_, hwnd);
  if (engine_->movie_load(movie_, "fmv.zrd", name_.c_str()) == -1) {
    std::fprintf(stderr, "mw3: --movie %s: no fmv.zrd\n", name_.c_str());
    return false;
  }
  shown_at_start_ = engine_->frames_presented;
  engine_->movie_stats = {};
  engine_->movie_shape_first = true;  // its shape known from the first frame (docs/fmv.md section 8)
  engine_->movie_play(movie_);
  return true;
}

bool DebugMovieState::update() {
  engine_->input_poll(0);
  const int shown = engine_->frames_presented - shown_at_start_;
  if (!shot_.empty() && shown == shot_frame_ - 1) render_->capture_next(shot_.c_str());
  const bool playing = movie_ && engine_->movie_playing(movie_);
  if (!shot_.empty() && engine_->frames_presented - shown_at_start_ >= shot_frame_) {
    std::printf("mw3: wrote %s (frame %d)\n", shot_.c_str(), shot_frame_);
    return true;
  }
  if (playing) return false;
  const mech3::GameEngine::MovieStats& st = engine_->movie_stats;
  std::printf("mw3: movie %s: %d frames shown, %d drawn, the last %d of %d\n", name_.c_str(),
              engine_->frames_presented - shown_at_start_, st.drawn, st.last, st.length);
  return true;
}

void DebugMovieState::leave() {
  if (!movie_) return;
  engine_->movie_stop(movie_, 0);
  engine_->movie_delete(movie_);
  movie_ = 0;
}

}  // namespace sdlfe
