#pragma once
#include <functional>
#include <cstdint>
#include <deque>

#include "gamez/zgame/unported.h"

namespace platform { class MessagePump; }

// C:\zipper\GameZMech3\zGame\zGameApp.cpp: the engine's application object. In the
// original it is an MFC CWinApp whose Run() (0x0058fd70) pumps Windows messages
// and drives a stack of game states (docs/game-loop.md). The loop is ported as
// written; the Windows message calls go through platform::MessagePump.
namespace zgame {

// A game state (front end, briefing, MechLab, mission, ...). The base class is the
// original's (vtable 0x005c36a8): each method's default is what the base does.
class State {
 public:
  virtual ~State() = default;
  virtual void on_activate(uint32_t active) { (void)active; }  // +0x04 window (de)activated
  virtual void on_queued() {}                                   // +0x08 queued to be entered
  virtual bool enter() { return true; }                         // +0x0c false: refused
  virtual bool update() { return false; }                       // +0x10 true: quit the game
  virtual void on_leaving() {}                                  // +0x14 a Pop or Replace is queued
  virtual void leave() {}                                       // +0x18
  virtual void suspend(uint32_t arg) { (void)arg; }             // +0x1c a state is pushed on top
  virtual void resume(uint32_t arg) { (void)arg; }              // +0x20 the state above was popped
  virtual uint32_t message(uint32_t wparam, uint32_t lparam) {  // +0x24 forwarded window message
    (void)wparam;
    (void)lparam;
    return 1;
  }
};

enum class Command : int { Pop = 1, Push = 2, Replace = 3 };  // command +0x4

// What zGameApp calls outside this file.
class AppHooks : public Unported {
 public:
  virtual void timer_update();                                  // 0x00575a00, ported: ztimer::update
  virtual void network_poll(int32_t a, uint8_t b);              // 0x00588930, ported: znet::poll
  virtual void cd_notify(uint32_t status, uint32_t device);     // 0x00572bf0, ported: zsnd_cd::notify
  // The frame window's OnActivate (0x00590c00) around the app's (de)activation.
  virtual void input_unacquire() { unported("0x0051b050", {}); }    // zInput, release the devices
  virtual void input_acquire() { unported("0x0051b080", {}); }      // zInput, take them back
  virtual void video_restore_window() { unported("0x00579f20", {}); }  // zVideo, OpenIcon if minimised
  // Ours (W45): run on while inactive (a network game), else the original's wait.
  virtual bool run_while_inactive() { return false; }
};

class App {
 public:
  // Ours, for checks (the campaign chain): after each state command run,
  // the state then current, however briefly it stays.
  std::function<void(const State*)> on_state_change;
  static constexpr int kMaxDepth = 16;

  explicit App(AppHooks* hooks = nullptr);                 // 0x0058fcd0 (the zGameApp part)
  virtual ~App() = default;

  // Queue a state change; it runs on a later pass of run(), one per pass, in order.
  State* replace(State* state, uint32_t arg);              // 0x00590250
  State* push(State* state, uint32_t arg);                 // 0x00590400
  State* pop(uint32_t arg);                                // 0x005905a0

  int run(platform::MessagePump& pump);                    // 0x0058fd70, +0x5c; returns ExitInstance's value
  bool start(uint32_t window);                             // 0x0058fc70
  uint32_t message(uint32_t wparam, uint32_t lparam);      // 0x00590740, +0xb4
  void on_window_activate(uint32_t state);                 // 0x00590c00, the frame's OnActivate

  State* current() const;                                  // 0x00590230
  int depth() const { return depth_; }
  int pending() const { return static_cast<int>(pending_.size()); }
  bool active() const { return active_ != 0; }
  State* stack(int i) const { return stack_[i]; }
  // The pump run() drives (null outside it): the message box's own loop
  // pumps it too (FIX (A), docs/twod.md, The message box).
  platform::MessagePump* message_pump() const { return pump_; }

  struct Pending {
    Command type;
    State* state;
    uint32_t arg;
  };
  const std::deque<Pending>& pending_list() const { return pending_; }

  // The CWinApp virtuals Run() and start() call; the game's app overrides them.
  virtual bool pump_message(platform::MessagePump& pump);  // +0x64 CWinThread::PumpMessage
  virtual int exit_instance() { return 0; }                // +0x70
  virtual uint32_t activate();                             // +0xa4 0x0058faa0, returns the old flag
  virtual uint32_t deactivate();                           // +0xa8 0x0058fa80
  virtual bool start_game(uint32_t window) { (void)window; return true; }  // +0xac
  virtual void stop_game() {}                              // +0xb0
  virtual void idle(uint32_t first) { (void)first; }       // +0xb8, nothing in MW3
  virtual uint32_t main_window() { return 0; }             // [0x0047fff0()+0x20], the frame's HWND

  State* first_state_ = nullptr;   // +0x0c4  entered by start()
  uint32_t idle_wait_ms_ = 100;    // +0x118  MsgWaitForMultipleObjects timeout while inactive

 protected:
  AppHooks* hooks_;

 private:
  void run_command(const Pending& cmd, State* cur);

  int depth_ = -1;                 // +0x0c8
  uint32_t active_ = 0;            // +0x0d0
  State* stack_[kMaxDepth] = {};   // +0x0d8
  std::deque<Pending> pending_;    // +0x120..+0x148, an MSVC std::deque
  platform::MessagePump* pump_ = nullptr;  // (ours)
};

}  // namespace zgame
