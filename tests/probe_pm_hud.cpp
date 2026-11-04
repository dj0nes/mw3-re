// Probes for test_pm_hud_objectives.py: PM's objectives box (HudObjectives::fill,
// src/mech3/hud.cpp on the pm branch) on a made-up mission, its lines read back.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "gamez/ztwod/twod_host.h"
#include "gamez/zutil/zfix.h"
#include "gamez/zutil/zutl_msg.h"
#include "mech3/controls_cfg.h"
#include "mech3/hud.h"
#include "mech3/mech_objective.h"

namespace {
std::map<int32_t, std::string> g_msgs;
const char* get_text(int32_t id) {
  auto it = g_msgs.find(id);
  return it == g_msgs.end() ? nullptr : it->second.c_str();
}

// zTwoD's host: a font by its name's index, a fixed glyph size.
struct Host : ztwod::Host {
  std::vector<std::string> fonts;
  ztwod::Handle font_find(const char* name) override {
    for (size_t i = 0; i < fonts.size(); ++i)
      if (fonts[i] == name) return static_cast<ztwod::Handle>(0x100 + i);
    fonts.push_back(name);
    return static_cast<ztwod::Handle>(0x100 + fonts.size() - 1);
  }
  ztwod::NamedFont font_fields(ztwod::Handle) override { return {}; }
  void font_addref(ztwod::Handle) override {}
  void font_release(ztwod::Handle) override {}
  void font_measure(const char* text, ztwod::Handle, int32_t* w, int32_t* h) override {
    *w = 7 * static_cast<int32_t>(std::strlen(text));
    *h = 14;
  }
  // Just enough GDI for a line's height: 14 a line, 7 a character.
  ztwod::Handle CreateCompatibleDC(ztwod::Handle) override { return 1; }
  ztwod::Handle SelectObject(ztwod::Handle, ztwod::Handle) override { return 0; }
  void DeleteDC(ztwod::Handle) override {}
  int32_t DrawTextA(ztwod::Handle, const char* text, int32_t, ztwod::Rect* r, uint32_t) override {
    r->left = r->top = 0;
    r->right = 7 * static_cast<int32_t>(std::strlen(text));
    r->bottom = 14;
    return 1;
  }
  ztwod::Handle image_dc(zimg::Texture*) override { return 0; }
  uint32_t unported(const char*, std::initializer_list<zgame::Arg>) override { return 0; }
};

struct Hooks : mech3::HudHooks {
  ObjectivesSource src;
  bool multiplayer = false;
  std::vector<std::string> scores;
  ObjectivesSource objectives_source() override { return src; }
  const char* message(int32_t id) override { return get_text(id) ? get_text(id) : ""; }
  std::string command_key(int32_t cmd) override { return "K" + std::to_string(cmd); }
  uint32_t multiplayer_game() override { return multiplayer ? 1 : 0; }
  std::vector<std::string> multiplayer_scores() override { return scores; }
  const char* nav_name(uint32_t) override { return ""; }
  uint32_t unported(const char*, std::initializer_list<zgame::Arg>) override { return 0; }
};

std::string unescape(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == 'n') o += '\n', ++i;
    else o += s[i];
  }
  return o;
}
std::vector<std::string> fields(const std::string& l) {
  std::vector<std::string> f;
  std::stringstream ss(l);
  std::string x;
  while (std::getline(ss, x, '|')) f.push_back(x);
  return f;
}
std::string g_out;
}  // namespace

extern "C" {

void probe_pm_obj_msg(int id, const char* text) { g_msgs[id] = text; }

// The mission, one line each ('|' between fields):
//   G|type|mission|name|aborting|multiplayer   D|desc
//   S|score line                               P|S|F|X|group's text|done
//   I|done|failed|visible|text (\n for a line break), into the last group
// Returns the box's lines (text and font id), then the scores', then the
// panel's height over the box's line height.
const char* probe_pm_obj_fill(const char* spec) {
  zfix::options().bugs = true;  // the 0x236 line (FIX (bug)): the test's stub passes the mission as %2 too
  zutl::messages().get_text = get_text;
  // PM's command numbers, as the exe pushes them (its table is not ported yet).
  mech3::controls_setup().names["CMD_MISSION_COMPLETE"] = 0x79;
  mech3::controls_setup().names["CMD_OBJECTIVES"] = 0x44;
  static Host host;
  ztwod::set_host(&host);
  static Hooks hooks;
  hooks = Hooks{};
  mech3::set_hud_hooks(&hooks);
  auto obj = std::make_unique<mech3::Objectives>();
  std::unique_ptr<mech3::ObjectiveGroup>* group = nullptr;
  std::stringstream in(spec);
  std::string line;
  while (std::getline(in, line)) {
    std::vector<std::string> f = fields(line);
    if (f.empty()) continue;
    if (f[0] == "G") {
      hooks.src.game_type = std::stoi(f[1]);
      hooks.src.mission = std::stoi(f[2]);
      static std::string name;
      name = f[3];
      hooks.src.game_name = name.c_str();
      hooks.src.aborting = static_cast<uint8_t>(std::stoi(f[4]));
      hooks.multiplayer = std::stoi(f[5]) != 0;
    } else if (f[0] == "D") {
      obj->desc = f[1];
    } else if (f[0] == "S") {
      hooks.scores.push_back(f[1]);
    } else if (f[0] == "P" || f[0] == "Q" || f[0] == "F" || f[0] == "X") {
      group = f[0] == "P" ? &obj->primary : f[0] == "Q" ? &obj->secondary : f[0] == "F" ? &obj->final : &obj->fail;
      *group = std::make_unique<mech3::ObjectiveGroup>(f[1].empty() ? nullptr : f[1].c_str());
      (*group)->done = static_cast<uint8_t>(std::stoi(f[2]));
    } else if (f[0] == "I" && group) {
      const std::string t = unescape(f.size() > 4 ? f[4] : "");
      auto it = std::make_unique<mech3::Objective>(mech3::Objective::kGroup, t.empty() ? nullptr : t.c_str());
      it->done = static_cast<uint8_t>(std::stoi(f[1]));
      it->failed = static_cast<uint8_t>(std::stoi(f[2]));
      it->visible = static_cast<uint8_t>(std::stoi(f[3]));
      (*group)->items.push_back(std::move(it));
    }
  }
  hooks.src.objectives = obj.get();
  auto box = std::make_unique<mech3::HudObjectives>();
  box->set_text_plain("Ay");  // the box's own text, for its line height (0x005a1380): 14
  box->fill();
  std::ostringstream o;
  auto dump = [&](ztwod::TextBox& b, const char* tag) {
    for (int32_t i = 0; i <= b.current && static_cast<size_t>(i) < b.lines.size(); ++i)
      o << tag << " " << b.lines[static_cast<size_t>(i)].named << " " << b.lines[static_cast<size_t>(i)].text << "\n";
  };
  dump(*box, "box");
  dump(box->scores, "scores");
  const int32_t h = box->text_height();
  o << "panel " << (box->panel.rect.right - box->panel.rect.left) << " "
    << (h ? (box->panel.rect.bottom - box->panel.rect.top) / h : -1) << "\n";
  for (size_t i = 0; i < host.fonts.size(); ++i) o << "font " << (0x100 + i) << " " << host.fonts[i] << "\n";
  g_out = o.str();
  mech3::set_hud_hooks(nullptr);
  ztwod::set_host(nullptr);
  return g_out.c_str();
}

}  // extern "C"
