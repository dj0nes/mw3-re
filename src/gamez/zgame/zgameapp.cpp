#include "gamez/znetwork/znet_session.h"
#include "gamez/zgame/zgameapp.h"

#include "gamez/zerror/zerr.h"
#include "gamez/zsound/zsnd_cd.h"
#include "gamez/zutil/ztimer.h"
#include "platform/platform.h"

namespace zgame {

namespace {
const char kFile[] = "C:\\zipper\\GameZMech3\\zGame\\zGameApp.cpp";
AppHooks g_default_hooks;
}  // namespace

void AppHooks::network_poll(int32_t a, uint8_t b) { znet::poll(a, b != 0); }
void AppHooks::timer_update() { ztimer::update(); }

// The original checks wParam == MCI_NOTIFY_SUCCESSFUL and lParam == its device id;
// the rebuild has no MCI device, so only the status is passed on.
void AppHooks::cd_notify(uint32_t status, uint32_t device) {
  (void)device;
  zsnd_cd::notify(static_cast<int>(status));
}

App::App(AppHooks* hooks) : hooks_(hooks ? hooks : &g_default_hooks) {}

// Mech3.exe 1.2 @ 0x00590230
State* App::current() const {
  if (depth_ < 0 || depth_ > kMaxDepth - 1) return nullptr;
  return stack_[depth_];
}

// Mech3.exe 1.2 @ 0x00590250
State* App::replace(State* state, uint32_t arg) {
  State* cur = current();
  pending_.push_back(Pending{Command::Replace, state, arg});
  if (cur) cur->on_leaving();
  state->on_queued();
  return cur;
}

// Mech3.exe 1.2 @ 0x00590400: unlike replace and pop, the state below is not told.
State* App::push(State* state, uint32_t arg) {
  State* cur = current();
  pending_.push_back(Pending{Command::Push, state, arg});
  state->on_queued();
  return cur;
}

// Mech3.exe 1.2 @ 0x005905a0
State* App::pop(uint32_t arg) {
  State* cur = current();
  pending_.push_back(Pending{Command::Pop, nullptr, arg});
  if (cur) cur->on_leaving();
  return cur;
}

// Mech3.exe 1.2 @ 0x0058fa80 / 0x0058faa0
uint32_t App::deactivate() {
  const uint32_t old = active_;
  active_ = 0;
  return old;
}
uint32_t App::activate() {
  const uint32_t old = active_;
  active_ = 1;
  return old;
}

bool App::pump_message(platform::MessagePump& pump) { return pump.pump(); }

// Mech3.exe 1.2 @ 0x0058fd70, zGameApp::Run, kept as written: drain the message
// queue, then one pass of the clock, the network and either one queued state
// change or the top state's update. While inactive it idles in
// MsgWaitForMultipleObjects. FIX (A): the Windows calls go through the pump;
// the SEH catch-all around it (a "fatal error" message box and exit) is left out.
int App::run(platform::MessagePump& pump) {
  pump_ = &pump;
  pump.raise_priority();  // FIX (A): SetThreadPriority(thread, THREAD_PRIORITY_HIGHEST)
  for (;;) {
    uint32_t first_idle = 1;
    State* cur;
    for (;;) {
      for (;;) {
        while (pump.peek()) {
          if (!pump_message(pump)) return exit_instance();
        }
        hooks_->timer_update();
        hooks_->network_poll(-1, 0);
        cur = current();
        if (active_ != 0) break;
        // FIX (A), playtest W45: inactive (minimised, another window in
        // front) the original waits here, the mission frozen; in a network
        // game the others then fight a mech that neither moves nor hears
        // them, and the catch-up comes all at once. There it runs on, about
        // 30 frames a second.
        if (hooks_->run_while_inactive()) {
          if (!pump.peek()) pump.wait(33);
          break;
        }
        if (!pump.peek()) {
          idle(first_idle);
          first_idle = 0;
          pump.wait(idle_wait_ms_);
        }
      }
      first_idle = 1;
      if (!pending_.empty()) break;
      if (cur && cur->update()) {
        deactivate();
        pump.post_quit();
      }
    }
    run_command(pending_.front(), cur);
    pending_.pop_front();
  }
}

// The command half of 0x0058fd70, with what a rewrite might "fix": a refused Push
// leaves the state below suspended, Pop resumes stack[0] after an underflow, and
// Push past the top overwrites the topmost state.
void App::run_command(const Pending& cmd, State* cur) {
  switch (cmd.type) {
    case Command::Pop: {
      if (cur) cur->leave();
      // The original clears stack[depth] even at depth -1, which lands on the
      // unused field at +0x0d4; nothing reads it, so the rebuild skips the write.
      if (depth_ >= 0) stack_[depth_] = nullptr;
      depth_ = depth_ - 1;
      if (depth_ < 0) {
        zerr::report(0x400, kFile, 0xf8, "Stack Depth Underflow");
        depth_ = 0;
      }
      // The original calls resume unchecked and crashes when the stack below is
      // empty (popping the only state); nothing in the game does that.
      if (stack_[depth_]) stack_[depth_]->resume(cmd.arg);
      break;
    }
    case Command::Push: {
      if (!cmd.state) break;
      // The original calls stack[depth]->suspend() unchecked, so a Push onto an
      // empty stack would crash; the game always starts with a Replace.
      if (depth_ >= 0 && stack_[depth_]) stack_[depth_]->suspend(cmd.arg);
      if (cmd.state->enter()) {
        depth_ = depth_ + 1;
        if (depth_ > kMaxDepth - 1) {
          zerr::report(0x400, kFile, 0xe1, "Stack Depth Overflow");
          depth_ = kMaxDepth - 1;
        }
        stack_[depth_] = cmd.state;
      }
      break;
    }
    case Command::Replace: {
      if (!cmd.state) break;
      if (cur) cur->leave();
      if (depth_ < 0) depth_ = 0;
      if (depth_ > kMaxDepth - 1) depth_ = kMaxDepth - 1;
      if (cmd.state->enter()) {
        stack_[depth_] = cmd.state;
      } else if (cur) {
        cur->enter();  // the refused state's predecessor comes back
      }
      break;
    }
  }
  if (on_state_change) on_state_change(current());
}

// Mech3.exe 1.2 @ 0x0058fc70
bool App::start(uint32_t window) {
  if (!start_game(window)) {
    stop_game();
    return exit_instance() != 0;
  }
  active_ = 1;
  replace(first_state_, 0);
  return true;
}

// Mech3.exe 1.2 @ 0x00590740: MM_MCINOTIFY from the frame window (0x00590c70).
uint32_t App::message(uint32_t wparam, uint32_t lparam) {
  State* cur = current();
  hooks_->cd_notify(wparam, lparam);
  if (cur) return cur->message(wparam, lparam);
  return 0;
}

// Mech3.exe 1.2 @ 0x00590c00, after CFrameWnd::OnActivate. The original also calls
// 0x005ac5c0 on deactivation, an empty function.
void App::on_window_activate(uint32_t state) {
  if (State* cur = current()) cur->on_activate(state);
  if (state == 0) {
    deactivate();
    hooks_->input_unacquire();
  } else {
    activate();
    hooks_->input_acquire();
    hooks_->video_restore_window();
  }
}

}  // namespace zgame
