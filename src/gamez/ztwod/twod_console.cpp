#include "gamez/ztwod/twod_console.h"

#include <cstdio>
#include <cstring>

#include "gamez/zinput/zinput.h"
#include "gamez/zrender/zrndr_tint.h"
#include "gamez/ztwod/twod_host.h"

namespace ztwod {

namespace {
Console* g_console = nullptr;  // [0x00736128]
int32_t g_click = 0;           // [0x00736324]: "snd_ricochet1", each key's click
constexpr const char* kPrompt = "> ";  // [0x005e56f8]

// 0x005a4090: the keyboard's character to the console (its +0xc4).
void char_to_console(int32_t ch, int32_t key, uintptr_t ctx) {
  if (auto* c = reinterpret_cast<Console*>(ctx)) c->key(ch, key);
}
}  // namespace

Console* console() { return g_console; }
WeatherLayer& console_layer() {
  static WeatherLayer layer;  // 0x005a4000 at static init (0x005a4590)
  return layer;
}

// Mech3.exe 1.2 @ 0x005a4660, 0x005a4670
void ConsoleLine::on_enter() { owner->line_entered(); }
void ConsoleLine::on_escape() { owner->on_escape(); }

// Mech3.exe 1.2 @ 0x005a4680, 0x005a4690
void ConsoleLine::backspace() {
  if (cursor > 2) LineEditor::backspace();
}
void ConsoleLine::left() {
  if (cursor > 2) LineEditor::left();
}

// Mech3.exe 1.2 @ 0x004b5280
void ConsoleBox::draw_in_rect() { zrndr::dim(reinterpret_cast<const int32_t*>(rect)); }

// Mech3.exe 1.2 @ 0x005a40b0: 25 lines; the box on the layer; the line; the
// click; the game's commands off (a copy of the map, +0x3f58 0); the
// keyboard's characters; Arial 10 in white at (160, 100); the prompt.
Console::Console() : TextBox(0x19) {
  g_console = this;
  editor = new ConsoleLine(this);
  g_click = host().sound_find("snd_ricochet1");  // [0x005f4a44]
  zin::push_map(nullptr);
  map = zin::current_map();
  if (map) map->enabled = 0;
  console_layer().add(&box);
  host().set_char_callback(&char_to_console, reinterpret_cast<uintptr_t>(this));
  create_font("Arial", 10, 400, 5, 0, 0, 2);
  set_color(0xffffff);
  set_pos(0xa0, 100);
  std::snprintf(kept, sizeof kept, "%s", kPrompt);
}

// Mech3.exe 1.2 @ 0x005a4230
Console::~Console() {
  host().set_char_callback(nullptr, 0);
  console_layer().remove(&box);
  console_layer().rects[0] = nullptr;  // 0x005a66b0(0, 0)
  zin::pop_map();
  map = nullptr;
  delete editor;
  editor = nullptr;
  if (g_console == this) g_console = nullptr;
}

// Mech3.exe 1.2 @ 0x005a4410: the box drawn on the layer's update (the
// back buffer its target, 0x005a66d0), then the lines.
void Console::draw() {
  console_layer().update(1.0f);  // 0x005a6600
  TextBox::update(0.0f);         // 0x005a1ae0
}

// Mech3.exe 1.2 @ 0x0041eb20, 0x0041eb50 (the chat panel's, the same here)
void Console::set_pos(int32_t px, int32_t py) {
  TextBox::set_pos(px, py);
  layout();
}
void Console::create_font(const char* face, int32_t height, int32_t weight, int32_t width, uint32_t italic,
                          uint32_t charset, uint32_t pitch) {
  TextBox::create_font(face, height, weight, width, italic, charset, pitch);
  layout();
}

// Mech3.exe 1.2 @ 0x005a4330: 320 wide from the lines' place, their height.
void Console::layout() {
  const int32_t h = total_height();
  area.top = get_y();
  area.left = get_x();
  area.bottom = h + area.top;
  area.right = area.left + 0x140;
  console_layer().rects[0] = &area;  // 0x005a66b0(0, rect)
  area_ptr = &area;
}

// Mech3.exe 1.2 @ 0x005a44c0
void Console::advance() {
  if (std::strcmp(line_text(), kPrompt) != 0) TextBox::advance();
}

// Mech3.exe 1.2 @ 0x005a4510
void Console::after_write(int32_t moved) {
  if (moved) TextBox::advance();
  append(kPrompt);
  editor->set_text(kPrompt);
  editor->set_cursor(2);
}

// Mech3.exe 1.2 @ 0x005a4390: DIK_UP (200) the line again, 0x42b its own;
// else the click and the key into the line, shown unless it was Escape or
// Enter.
bool Console::key(int32_t ch, int32_t code) {
  if (code == 200) {
    recall();
    return false;
  }
  if (code == 0x42b) {
    on_code_42b();
    return false;
  }
  if (g_click) host().sound_play(g_click, 0.2f);
  const int32_t action = editor->key(ch);
  if (action != 3 && action != 4) append(editor->text());
  return true;
}

// Mech3.exe 1.2 @ 0x005a4460
void Console::line_entered() {
  std::snprintf(kept, sizeof kept, "%s", line_text());
  new_line();
}

// Mech3.exe 1.2 @ 0x005a4490
void Console::on_escape() {
  append(kPrompt);
  editor->set_text(kPrompt);
  editor->set_cursor(2);
}

// Mech3.exe 1.2 @ 0x005a4550
void Console::recall() {
  append(kept);
  editor->set_text(kept);
  editor->set_cursor(static_cast<int32_t>(std::strlen(kept)));
}

}  // namespace ztwod
