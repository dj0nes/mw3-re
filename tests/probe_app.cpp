// Probes for test_app_*.py (docs/game-loop.md): the application, its main loop
// and the game states. Every call the ported code makes outside itself goes to
// one Python callback, which records it and answers; the same callback answers
// the original x86 code's stubbed calls, so the two call traces can be compared.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "gamez/zerror/zerr.h"
#include "gamez/zgame/zgameapp.h"
#include "platform/platform.h"
#include "probe_app.h"

namespace {
using Callback = uint32_t (*)(const char* key, const uint32_t* vals, const char* const* strs,
                               int32_t n);
Callback g_cb = nullptr;
}  // namespace

namespace probe_app {
namespace {
int g_mute = 0;
}  // namespace
Mute::Mute() { ++g_mute; }
Mute::~Mute() { --g_mute; }

uint32_t call(const char* key, std::initializer_list<zgame::Arg> args) {
  if (g_mute) return 0;
  std::vector<uint32_t> v;
  std::vector<const char*> s;
  for (const zgame::Arg& a : args) {
    v.push_back(a.v);
    s.push_back(a.s);
  }
  return g_cb ? g_cb(key, v.data(), s.data(), static_cast<int32_t>(v.size())) : 0;
}
}  // namespace probe_app

namespace {
using probe_app::call;

struct RecordingHooks : zgame::AppHooks {
  uint32_t unported(const char* key, std::initializer_list<zgame::Arg> args) override {
    return call(key, args);
  }
  void timer_update() override { call("0x00575a00", {}); }
  void cd_notify(uint32_t status, uint32_t device) override { call("0x00572bf0", {status, device}); }
  // Ported (znet::poll) but kept a boundary here, as the test stubs it in the exe.
  void network_poll(int32_t a, uint8_t b) override { call("0x00588930", {static_cast<uint32_t>(a), b}); }
};

struct Pump : platform::MessagePump {
  bool peek() override { return call("PeekMessageA", {}) != 0; }
  bool pump() override { return true; }  // the app's +0x64 records it
  void wait(uint32_t ms) override { call("MsgWaitForMultipleObjects", {ms}); }
  void post_quit() override { call("PostQuitMessage", {0}); }
  void raise_priority() override { call("SetThreadPriority", {2}); }
};

// The loop test's states and app: each virtual reports "<name>.vt+<slot>".
struct LoopApp;
LoopApp* g_app = nullptr;
std::vector<zgame::State*> g_states;

std::string slot(const std::string& name, int off) {
  char buf[16];
  std::snprintf(buf, sizeof buf, ".vt+%#x", off);
  return name + buf;
}

struct LoopApp : zgame::App {
  explicit LoopApp(zgame::AppHooks* h) : App(h) {}
  bool pump_message(platform::MessagePump&) override;
  int exit_instance() override { return static_cast<int>(call("app.vt+0x70", {})); }
  bool start_game(uint32_t window) override { return call("app.vt+0xac", {window}) != 0; }
  void stop_game() override { call("app.vt+0xb0", {}); }
  void idle(uint32_t first) override { call("app.vt+0xb8", {first}); }
  uint32_t main_window() override { return 0x1234; }
};

// An answer can ask the probe to do what the original's stub thunk does: queue a
// state change (bits 8-11 type, 12-19 state, 20-31 arg) or, from PumpMessage,
// deliver a window event (bits 8-11: 1 activate, 2 message; 12-31 its value).
void act(uint32_t r) {
  const uint32_t type = (r >> 8) & 0xf, target = (r >> 12) & 0xff, arg = r >> 20;
  if (type == 1) g_app->pop(arg);
  if (type == 2) g_app->push(g_states[target], arg);
  if (type == 3) g_app->replace(g_states[target], arg);
}

bool LoopApp::pump_message(platform::MessagePump&) {
  const uint32_t r = call("app.vt+0x64", {});
  const uint32_t ev = (r >> 8) & 0xf, value = r >> 12;
  if (ev == 1) on_window_activate(value);
  if (ev == 2) message(value & 0xff, value >> 8);
  return (r & 0xff) != 0;
}

struct LoopState : zgame::State {
  std::string name;
  explicit LoopState(std::string n) : name(std::move(n)) {}
  uint32_t k(int off, std::initializer_list<zgame::Arg> a = {}) { return call(slot(name, off).c_str(), a); }
  void on_activate(uint32_t a) override { k(0x04, {a}); }
  void on_queued() override { k(0x08); }
  bool enter() override { return k(0x0c) != 0; }
  bool update() override {
    call("@depth", {static_cast<uint32_t>(g_app->depth()), static_cast<uint32_t>(g_app->pending())});
    const uint32_t r = k(0x10);
    act(r);
    return (r & 0xff) != 0;
  }
  void on_leaving() override { k(0x14); }
  void leave() override { k(0x18); }
  void suspend(uint32_t a) override { k(0x1c, {a}); }
  void resume(uint32_t a) override { k(0x20, {a}); }
  uint32_t message(uint32_t w, uint32_t l) override { return k(0x24, {w, l}); }
};

void zerr_sink(int level, const char*, int line, const char*) {
  call("0x0049bfd0", {static_cast<uint32_t>(level), static_cast<uint32_t>(line)});
}

}  // namespace

extern "C" {

void probe_app_set_callback(Callback cb) { g_cb = cb; }

// start() then run() with `count` states; writes depth, active, pending and the
// stack as state indices (-1 empty) into out[0..18]. Returns run()'s value.
int32_t probe_app_loop(int32_t count, int32_t first, int32_t* out) {
  RecordingHooks hooks;
  LoopApp app(&hooks);
  g_app = &app;
  std::vector<LoopState> states;
  states.reserve(count);
  g_states.clear();
  for (int i = 0; i < count; ++i) {
    states.emplace_back("state" + std::to_string(i));
    g_states.push_back(&states.back());
  }
  zerr::set_sink(zerr_sink);
  app.first_state_ = g_states[first];
  Pump pump;
  int32_t ret = -1;
  if (app.start(app.main_window())) ret = app.run(pump);
  zerr::set_sink(nullptr);
  out[0] = app.depth();
  out[1] = app.active() ? 1 : 0;
  out[2] = app.pending();
  for (int i = 0; i < zgame::App::kMaxDepth; ++i) {
    int32_t idx = -1;
    for (int j = 0; j < count; ++j)
      if (app.stack(i) == g_states[j]) idx = j;
    out[3 + i] = idx;
  }
  g_app = nullptr;
  return ret;
}

}  // extern "C"
