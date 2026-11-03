// The keyboard (zin_kbd.cpp, docs/input.md section 1): DirectInput's buffered
// key events turned into a state per key code, modifiers included.
#include <cstring>

#include "gamez/zinput/zinput.h"

namespace zin {

extern Host* g_host;

namespace {
constexpr const char* kFile = "C:\\zipper\\GameZMech3\\zInput\\zin_kbd.cpp";
constexpr uint32_t kBuffer = 0x80;  // DIPROP_BUFFERSIZE, the events a read takes

// The event's code with the modifiers its data carries (0x40 Alt, 0x20 Ctrl,
// 0x10 Shift: bits kbd_read sets, DirectInput leaves them clear).
uint32_t code_of(const platform::KeyEvent& e) {
  uint32_t c = e.ofs;
  if (e.data & 0x40) c |= kAlt;
  if (e.data & 0x20) c |= kCtrl;
  if (e.data & 0x10) c |= kShift;
  return c;
}

// The modifier keys: true when the event is one (and the modifiers were updated).
bool modifier(State& s, const platform::KeyEvent& e) {
  uint32_t bit;
  switch (e.ofs) {
    case 0x1d: case 0x9d: bit = kCtrl; break;   // DIK_LCONTROL, DIK_RCONTROL
    case 0x2a: case 0x36: bit = kShift; break;  // DIK_LSHIFT, DIK_RSHIFT
    case 0x38: case 0xb8: bit = kAlt; break;    // DIK_LMENU, DIK_RMENU
    default: return false;
  }
  if (e.data & 0x80) s.modifiers |= bit;
  else s.modifiers &= ~bit;
  return true;
}

// The event's state change: down gives 1 (3 when already 1), up adds 4.
void apply(State& s, uint32_t code, bool down) {
  uint32_t& st = s.keys[code].state;
  st = down ? (st != 1 ? 1u : 3u) : (st | kReleased);
}

// Mods on a plain key: a held unmodified copy of it released, the modifier
// bits written into the event, the code moved into the modified range.
uint32_t with_modifiers(State& s, platform::KeyEvent& e) {
  uint32_t code = e.ofs;
  if (s.modifiers == 0) return code;
  if (s.keys[code].state != 0) s.keys[code].state = kReleased;
  if (s.modifiers & kAlt) e.data |= 0x40;
  if (s.modifiers & kCtrl) e.data |= 0x20;
  if (s.modifiers & kShift) e.data |= 0x10;
  return code | s.modifiers;
}

// FIX (bug): a code past the table (a newer key such as DIK_POWER 0xde..
// DIK_MEDIASELECT 0xed held with Ctrl, Alt and Shift) wrote past the key
// states into the char callback and the joystick's globals (fixes.md).
bool in_table(uint32_t code) { return code < static_cast<uint32_t>(kKeys); }

}  // namespace

// Mech3.exe 1.2 @ 0x0051a170
// FIX (A): CreateDevice(GUID_SysKeyboard), SetCooperativeLevel(window,
// DISCL_NONEXCLUSIVE | DISCL_BACKGROUND), DIPROP_BUFFERSIZE 0x80 and
// c_dfDIKeyboard are the platform's open().
uint32_t kbd_init() {
  State& s = state();
  s.kbd_ready = 0;
  s.kbd_open = false;
  s.events.clear();
  s.event_count = 0;
  s.pressed.clear();
  s.pressed_capacity = 0;
  s.modifiers = 0;
  s.char_callback = nullptr;
  s.char_ctx = 0;
  if (!s.di->open(platform::Input::kKeyboard)) {
    report(static_cast<int32_t>(0x80004005), kFile, 0x32);
    return 1;
  }
  s.kbd_open = true;
  s.di->set_cooperative_level(platform::Input::kKeyboard, 10);
  if (const int32_t hr = s.di->acquire(platform::Input::kKeyboard)) {
    report(hr, kFile, 0x53);
    return 1;
  }
  s.events.assign(kBuffer, platform::KeyEvent{});  // calloc(1, 0x800)
  s.kbd_ready = 1;
  kbd_reset();
  clear_key_callbacks();
  return 0;
}

// Mech3.exe 1.2 @ 0x0051a290
uint32_t kbd_close() {
  State& s = state();
  if (s.kbd_open) {
    s.di->unacquire(platform::Input::kKeyboard);
    s.di->close(platform::Input::kKeyboard);  // Release
  }
  s.events.clear();
  if (s.pressed_capacity != 0 || !s.pressed.empty()) {
    s.pressed.clear();
    s.pressed_capacity = 0;
  }
  s.kbd_ready = 0;
  return 0;
}

// Mech3.exe 1.2 @ 0x0051a300
// Empties DirectInput's buffer and clears every key's state and the modifiers.
void kbd_reset() {
  State& s = state();
  if (s.kbd_ready == 0) return;
  uint32_t n = kBuffer;
  s.event_count = 0;
  const int32_t hr = s.di->keyboard_data(s.events.data(), &n);
  if (hr != 0) {
    if (hr != platform::Input::kInputLost) {
      report(hr, kFile, 0xad);
      return;
    }
    s.di->acquire(platform::Input::kKeyboard);
  }
  for (KeySlot& k : s.keys) k.state = 0;
  s.modifiers = 0;
  s.frame_key = 0;  // ours: a key before the reset is not the capture's
}

// Mech3.exe 1.2 @ 0x0051a550
// Before a read: the keys of the last read age, released ones to 0 and
// pressed ones (exactly 1) to held. Keys not in the last read keep their state.
void kbd_age() {
  State& s = state();
  for (uint32_t i = 0; i < s.event_count; ++i) {
    const uint32_t c = code_of(s.events[i]);
    if (c >= static_cast<uint32_t>(kKeys)) continue;
    uint32_t& st = s.keys[c].state;
    if (st & kReleased) st = 0;
    else if (st == kPressed) st = kHeld;
  }
}

// Mech3.exe 1.2 @ 0x0051a5d0
void kbd_read(bool callbacks) {
  State& s = state();
  uint32_t n = kBuffer;
  kbd_age();
  const int32_t hr = s.di->keyboard_data(s.events.data(), &n);
  s.event_count = n;
  if (hr != 0) {
    // Any other result, DI_BUFFEROVERFLOW too, drops this read's events.
    if (hr != platform::Input::kInputLost) {
      report(hr, kFile, 0x153);
      return;
    }
    s.di->acquire(platform::Input::kKeyboard);
  }
  s.frame_key = 0;
  for (uint32_t i = 0; i < n; ++i) {
    platform::KeyEvent& e = s.events[i];
    uint32_t code = e.ofs;
    const bool plain = !modifier(s, e);
    if (plain) code = with_modifiers(s, e);
    if (!in_table(code)) continue;
    if (e.data & 0x80) {
      apply(s, code, true);
      if (plain && !s.frame_key) s.frame_key = code;
      if (s.char_callback) {
        if (const uint32_t ch = key_char(code)) s.char_callback(static_cast<int32_t>(ch), static_cast<int32_t>(code), s.char_ctx);
      }
    } else {
      apply(s, code, false);
    }
  }
  // The codes of this read, once each, down or up.
  s.pressed.clear();
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t c = code_of(s.events[i]);
    if (c >= static_cast<uint32_t>(kKeys)) continue;
    bool seen = false;
    for (int32_t p : s.pressed) seen = seen || p == static_cast<int32_t>(c);
    if (seen) continue;
    if (s.pressed.size() == s.pressed_capacity) ++s.pressed_capacity;  // realloc by one
    s.pressed.push_back(static_cast<int32_t>(c));
  }
  // Alt-F4's command (0x13e) runs whatever the flag, once, and leaves the list.
  KeySlot& alt_f4 = s.keys[0x13e];
  for (size_t i = 0; i < s.pressed.size(); ++i) {
    if (s.pressed[i] == 0x13e && alt_f4.callback && (alt_f4.state & 1)) {
      alt_f4.callback(0x13e);
      s.pressed.erase(s.pressed.begin() + static_cast<std::ptrdiff_t>(i));
      break;
    }
  }
  // The keys' commands (their callbacks), unless a text field takes the keys.
  if (callbacks && !s.char_callback) {
    const size_t count = s.pressed.size();
    for (size_t i = 0; i < count && i < s.pressed.size(); ++i) {
      const KeySlot& k = s.keys[s.pressed[i]];
      if (k.callback && (k.state & 1)) k.callback(s.pressed[i]);
    }
  }
}

// Mech3.exe 1.2 @ 0x0051a9a0
void set_char_callback(CharCallback cb, uintptr_t ctx) {
  state().char_callback = cb;
  state().char_ctx = ctx;
}

// Mech3.exe 1.2 @ 0x0051a9b0
uint32_t key_state(int32_t key) { return state().keys[key].state; }

// Mech3.exe 1.2 @ 0x0051a9c0
int32_t set_key_callback(int32_t key, KeyCallback cb) {
  KeySlot& k = state().keys[key];
  if (k.callback) return -1;
  k.callback = cb;
  return 0;
}

// Mech3.exe 1.2 @ 0x0051a9e0
void clear_key_callbacks() {
  for (KeySlot& k : state().keys) k.callback = nullptr;
}

// Mech3.exe 1.2 @ 0x0051aa00
// One event at a time until a plain or modified key goes down (or, without
// wait, once): its code. Quirk kept: the count asked for is the last read's,
// so after an empty read it asks for none (with wait, forever).
uint32_t read_key(bool wait) {
  State& s = state();
  uint32_t n = 1;
  uint32_t found = 0;
  kbd_age();
  for (;;) {
    const int32_t hr = s.di->keyboard_data(s.events.data(), &n);
    s.event_count = n;
    if (hr != 0) {
      if (hr != platform::Input::kInputLost) {
        report(hr, kFile, 0x2dd);
        return 0;
      }
      s.di->acquire(platform::Input::kKeyboard);
    }
    for (uint32_t i = 0; i < n; ++i) {
      platform::KeyEvent& e = s.events[i];
      uint32_t code = e.ofs;
      if (modifier(s, e)) {
        found = 0;
      } else {
        code = e.ofs;
        if (s.modifiers & kAlt) e.data |= 0x40;
        if (s.modifiers & kCtrl) e.data |= 0x20;
        if (s.modifiers & kShift) e.data |= 0x10;
        code |= s.modifiers;
        found = code;
      }
      if (!in_table(code)) continue;
      if (e.data & 0x80) {
        apply(s, code, true);
      } else {
        apply(s, code, false);
        found = 0;
      }
    }
    if (found != 0 || !wait) return found;
  }
}

uint32_t frame_key() { return state().frame_key; }

// Mech3.exe 1.2 @ 0x0051ab90
// The character a key types, for text fields. FIX (A): MapVirtualKeyA,
// GetKeyboardState and ToAscii are the host's. The keypad without a
// character (NumLock off) gives the cursor keys' codes the line editors use.
uint32_t key_char(uint32_t key) {
  const uint32_t scan = key & 0xff;
  switch (scan) {
    case 0xcb: return 2;     // left
    case 0xcd: return 6;     // right
    case 0xd3: return 0x7f;  // delete
    case 0xc7: return 1;     // home
    case 0xcf: return 4;     // end
    default: break;
  }
  uint8_t kb[256] = {};
  uint32_t ch = 0;
  const uint32_t vk = g_host->map_virtual_key(scan);
  if (vk != 0) {
    uint16_t out = 0;
    if (g_host->keyboard_state(kb) && g_host->to_ascii(vk, scan, kb, &out) == 1) ch = out;
    if (ch != 0) return ch;
  }
  const bool num = kb[0x90] != 0;  // VK_NUMLOCK's byte (zero when the state was not read)
  switch (scan) {
    case 0x37: return 0x2a;
    case 0x47: return num ? 0x37 : 1;
    case 0x48: return num ? 0x38 : 0;
    case 0x49: return num ? 0x39 : 0;
    case 0x4a: return 0x2d;
    case 0x4b: return num ? 0x34 : 2;
    case 0x4c: return num ? 0x35 : 0;
    case 0x4d: return num ? 0x36 : 6;
    case 0x4e: return 0x2b;
    case 0x4f: return num ? 0x31 : 4;
    case 0x50: return num ? 0x32 : 0;
    case 0x51: return num ? 0x33 : 0;
    case 0x52: return num ? 0x30 : 0;
    case 0x53: return num ? 0x2e : 0x7f;
    case 0x9c: return 0xd;
    case 0xb5: return 0x2f;
    default: return ch;
  }
}

// Mech3.exe 1.2 @ 0x0051aee0
int32_t key_by_name(const char* name) {
  if (!name) return -1;
  const State& s = state();
  for (int i = 0; i < 0xde; ++i)
    if (s.key_named[i] && s.key_names[i] == name) return i;
  return -1;
}

}  // namespace zin
