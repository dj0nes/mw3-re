#include "mech3/game_editor.h"

#include <cstdio>

#include "gamez/ztwod/twod_host.h"
#include "gamez/zutil/ztimer.h"

namespace mech3 {

namespace {
EditorHooks g_default_hooks;
EditorHooks* g_hooks = &g_default_hooks;
EditorState g_state;

void edit_ai_nets(int32_t) { g_hooks->not_ported("EDIT AI NETS (0x00402d60, the AI net editor)"); }
void edit_vehicles(int32_t) { g_hooks->not_ported("EDIT VEHICLES (0x004040c0 -> 0x004b86e0, the vehicle editor)"); }
void edit_weapons(int32_t) {}  // 0x004bf620: nothing in MW3
void exit_game_edit(int32_t y) { editor_exit(y); }

// 0x005e0d30.
const EditorEntry kMainRows[8] = {
    {"PICK A COMMAND", nullptr},          {" EDIT AI NETS", edit_ai_nets}, {" EDIT VEHICLES", edit_vehicles},
    {" EDIT WEAPONS", edit_weapons},       {"", nullptr},                   {"", nullptr},
    {"", nullptr},                         {" EXIT GAME EDIT", exit_game_edit},
};
const EditorMenu kMainMenu = {8, kMainRows};
}  // namespace

const EditorMenu& editor_main_menu_rows() { return kMainMenu; }
void set_editor_hooks(EditorHooks* h) { g_hooks = h ? h : &g_default_hooks; }
EditorHooks& editor_hooks() { return *g_hooks; }
EditorState& editor_state() { return g_state; }

float EditorHooks::frame_dt() { return ztimer::state().dt; }
ztwod::Handle EditorHooks::font(const char* name) { return ztwod::host().font_find(name); }
void EditorHooks::not_ported(const char* what) {
  std::printf("mw3: game editor: %s is not ported yet (docs/editor.md)\n", what);
}

// Mech3.exe 1.2 @ 0x0040e270 (5, 0x5f), the rows by 0x0040e840(0, 0, 0).
EditorPanel::EditorPanel() {
  EditorHooks& h = *g_hooks;
  const ztwod::Handle f = h.font("mech_string");
  int32_t y = top;
  for (ztwod::FontText*& r : rows) {
    r = new ztwod::FontText("", 0, 0);
    if (f) r->set_font(f);
    r->set_pos(left, y);  // vt+0x0c
    r->hide();            // vt+0x64
    layer.add(r);
    y += 12;
  }
  color_top = rows[0]->color_top, color_bottom = rows[0]->color_bottom;
}

// Mech3.exe 1.2 @ 0x0040e390
EditorPanel::~EditorPanel() {
  for (ztwod::FontText*& r : rows) {
    layer.remove(r);
    delete r;
    r = nullptr;
  }
}

EditorPanel& editor_panel() {
  if (!g_state.panel) g_state.panel = new EditorPanel;
  return *g_state.panel;
}
void editor_reset() {
  delete g_state.panel;
  g_state = EditorState{};
}

// Mech3.exe 1.2 @ 0x0040e6a0: every row emptied, then the menu's rows shown
// (a null one hidden) in the panel's own colours with their text; the
// panel's extent the widest row and the rows' height; none hovered.
void editor_fill(EditorPanel& p, const EditorMenu& m) {
  for (ztwod::FontText* r : p.rows) r->set_text("");  // vt+0x7c with "" (0x005f4c90)
  int32_t widest = 0;
  for (int32_t i = 0; i < m.count && i < 25; ++i) {
    ztwod::FontText& r = *p.rows[i];
    const EditorEntry* e = &m.rows[i];
    r.show_for(-1.0f);  // vt+0x60; the exe's null test never holds within the table
    if (p.color_top != r.color_top || p.color_bottom != r.color_bottom) {
      r.color_top = p.color_top, r.color_bottom = p.color_bottom;
      r.needs_render = 1;
    }
    r.set_text("%s", e->text);  // vt+0x7c
    if (r.needs_render) r.render();  // vt+0xac
    if (widest < r.width) widest = r.width;
  }
  p.hovered = -1;
  p.right = p.left + widest;
  p.bottom = p.top + m.count * 12;
}

// Mech3.exe 1.2 @ 0x0040e7b0 (ECX the mode, EDX the menu).
void editor_install(int32_t mode, const EditorMenu& m) {
  g_state.mode = mode;
  g_state.menu = &m;
  editor_fill(editor_panel(), m);
}

// Mech3.exe 1.2 @ 0x0040e7d0 (its title "BATTLESPORT GAME EDITOR" passed, unread).
void editor_main_menu() { editor_install(0, kMainMenu); }

// Mech3.exe 1.2 @ 0x0040e430: a copy of the key map with Space and the left
// button as 0x9b (pick), 0x439 and the right button as 0x9c (a trace in a
// debug build); the view onto the stealth object; the debug lines hidden
// when they were up; the panel's layer on; the main menu.
void editor_enter() {
  EditorHooks& h = *g_hooks;
  g_state.active = 1;
  h.push_map();
  h.bind(0x9b, 0x39, 1);
  h.bind(0x9c, 0x439, 2);
  h.view_on_stealth();
  g_state.debug_lines = h.debug_lines();
  if (g_state.debug_lines) h.debug_lines_show(false);
  editor_panel().layer.set_active(1);
  editor_main_menu();
}

// Mech3.exe 1.2 @ 0x0040e7f0 (its argument unread): the key map back, the
// panel's layer off, the debug lines back, the editor down. The exe leaves
// the view on the stealth object; ours (debug only, docs/editor.md S4) puts
// it back on the pilot's mech, as the mech swap does.
void editor_exit(int32_t) {
  EditorHooks& h = *g_hooks;
  h.pop_map();
  editor_panel().layer.set_active(0);
  if (g_state.debug_lines) h.debug_lines_show(true);
  g_state.active = 0;
  h.view_on_pilot();  // ours
}

// Mech3.exe 1.2 @ 0x0040e4b0, each frame: while the editor is up, the row
// under the mouse (by its y, 12 apart from 0x5f, and its hit test) lit
// green, the one before back in the panel's colours; 0x9b pressed on a row
// calls its entry (a hidden row or the title, the menu's first) with the
// mouse's y, the input's state cleared first for a shown row; the panel's
// layer updated (drawn); the net editor's step in its modes; 0x9c held
// calls the trace (empty in the release, 0x005ac5c0). Then the net
// editor's frame either way.
void editor_frame() {
  EditorHooks& h = *g_hooks;
  if (g_state.active) {
    EditorPanel& p = editor_panel();
    const zin::MouseInfo m = h.mouse();
    int32_t r = (m.y - 0x5f) / 12;
    if (r < 0 || r > 24 || !p.rows[r]->hit(m.x, m.y)) r = -1;
    if (p.hovered != r) {
      if (p.hovered >= 0) {
        ztwod::FontText& o = *p.rows[p.hovered];
        if (p.color_top != o.color_top || p.color_bottom != o.color_bottom) {
          o.color_top = p.color_top, o.color_bottom = p.color_bottom;
          o.needs_render = 1;
        }
      }
      p.hovered = r;
      if (r > 0) {
        ztwod::FontText& n = *p.rows[r];
        if (n.color_top != 0xff00 || n.color_bottom != 0xff00) {
          n.color_top = 0xff00, n.color_bottom = 0xff00;
          n.needs_render = 1;
        }
      }
    }
    const bool pressed = r >= 0 && (h.command(0x9b) & 4) != 0;
    const bool shown = r > 0 && !(p.rows[r]->flags & ztwod::kHidden);
    if (pressed && shown) h.input_reset();
    p.layer.update(h.frame_dt());  // 0x0059fd20
    if (g_state.mode > 0 && g_state.mode < 4) h.net_step(0.1f);
    if (pressed && g_state.menu) {
      const EditorEntry& e = g_state.menu->rows[shown ? r : 0];
      if (e.handler) e.handler(m.y);
    }
    (void)(h.command(0x9c) & 3);  // the trace, 0x005ac5c0: empty
  }
  h.net_frame(g_state.active == 1);
}

void editor_key(int32_t cmd) {
  if (cmd != kEditorCommand) return;
  if (g_state.active) editor_exit();
  else editor_enter();
}

}  // namespace mech3
