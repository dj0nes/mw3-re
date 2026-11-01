#pragma once
#include <cstdint>

#include "gamez/ztwod/twod.h"
#include "gamez/ztwod/twod_weather.h"

namespace zin { struct CmdMap; }

// zTwoD's console (0x005a40b0..0x005a4690; vtable 0x005d01a8): 25 lines of
// Arial 10 over a dimmed box, the last a "> " prompt typed into; the game's
// commands off while it is up (a copy of the command map pushed, its
// +0x3f58 cleared) and the keyboard's characters its own. The interp debug
// console and the in-game chat panel (mech3/chat_panel) are ones. Original
// file unknown: between twod_wintext.cpp and zent_init.cpp, after the text
// box; one at a time ([0x00736128]).
namespace ztwod {

class Console;

// Its line (vtable 0x005d0178, 0x118 bytes, 0x200 characters): Enter and
// Escape the console's, no backing over the prompt.
class ConsoleLine : public LineEditor {
 public:
  explicit ConsoleLine(Console* c) : LineEditor(0x200), owner(c) {}  // 0x005a46a0(0x200)
  void on_enter() override;          // +0x0c  0x005a4660: the console's +0xd0
  void on_escape() override;         // +0x10  0x005a4670: its +0xd4
  void backspace() override;         // +0x14  0x005a4680: not into the prompt
  void left() override;              // +0x1c  0x005a4690
  Console* owner;                    // +0x114
};

// The box behind it (vtable 0x005c9120): the console's layer's rect dimmed
// (0x004b5280 -> 0x0055a740).
class ConsoleBox : public ManagerObject {
 public:
  ConsoleBox() : ManagerObject(0, 0) {}  // 0x0059fda0(0, 0), +0x34 0
  void draw_in_rect() override;      // +0x7c  0x004b5280
};

class Console : public TextBox {
 public:
  Console();                         // 0x005a40b0
  ~Console() override;               // 0x005a4230 (deleting 0x005a4210)
  void draw() override;              // +0x04  0x005a4410: the box, then the lines
  void set_pos(int32_t px, int32_t py) override;  // +0x0c  0x0041eb20: and the box with it
  void create_font(const char* face, int32_t height, int32_t weight, int32_t width, uint32_t italic,
                   uint32_t charset, uint32_t pitch) override;  // +0x88  0x0041eb50
  void advance() override;           // +0xb8  0x005a44c0: not past a bare prompt
  void after_write(int32_t moved) override;  // +0xc0  0x005a4510: the prompt again
  virtual bool key(int32_t ch, int32_t code);  // +0xc4  0x005a4390
  virtual void on_code_42b() {}      // +0xc8  0x005ac5c0
  virtual void layout();             // +0xcc  0x005a4330: the box over the lines
  // +0xd0 0x005a4460: the line kept, a new one. Not on_enter: that is the
  // mouse routing's +0x3c (Window::update), which would send a chat line.
  virtual void line_entered();
  virtual void on_escape();          // +0xd4  0x005a4490: the prompt alone
  virtual void recall();             // +0xd8  0x005a4550: the line kept, again (DIK_UP)
  const char* line_text() { return line().text; }  // 0x005a22c0

  char kept[0x200] = {};             // +0x4c8  the line last entered
  zin::CmdMap* map = nullptr;        // +0x6c8  the pushed copy
  ConsoleLine* editor = nullptr;     // +0x6cc
  ConsoleBox box;                    // +0x6d0
  const Rect* area_ptr = nullptr;    // +0x704
  Rect area;                         // +0x708
};

Console* console();            // [0x00736128]: the last made, or none
WeatherLayer& console_layer(); // [0x00736130]: the box's layer

}  // namespace ztwod
