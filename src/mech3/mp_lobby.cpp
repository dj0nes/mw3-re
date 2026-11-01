#include "mech3/mp_lobby.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zreader/zreader.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "mech3/pilot.h"

namespace mech3 {

int32_t mech_name_message(int32_t mech);  // mech3/mech_names.h (its Engine clashes with app_hooks.h's)

uint8_t g_lobby_dirty = 0;  // [0x006099cc]
uint8_t g_lobby_back = 0;   // [0x006099c8]

namespace {
LobbyScreen* g_lobby = nullptr;  // [0x006099d4]
int32_t g_order_next = 1;        // [0x005e7920]

void set_labels(ztwod::Control& c, const char* text) {  // 0x00495240
  for (std::vector<ztwod::Text*>* l : {&c.activate_labels, &c.disable_labels, &c.rollover_labels, &c.labels})
    if (!l->empty() && l->front()) l->front()->set_text_plain(text);
}
void set_enabled(ztwod::Control& c, int32_t on) {
  c.enabled = on;
  c.refresh();  // +0x80
}
std::string lower(std::string s) {  // CString::MakeLower
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
// A skin's name as the lists show it: " {gunmetal} " (0xdd7.., the format
// [0x005e7c3c] or the teams' "  {%s} " [0x005e7c50]).
std::string skin_tag(Engine& e, const char* format, int32_t skin) {
  char buf[96];
  std::snprintf(buf, sizeof buf, format, e.message(0xdd6 + skin));
  return lower(buf);
}

// A player's data as the lobby reads it (0x00499030, 0x0049ab30): more than
// a word or nothing. Kind 1 (the lobby's): its skin, its state, then the
// add-on worlds it has (their count, the values); the launch's kind 2
// (0x0046da80) replaces it.
std::vector<uint32_t> player_data(uint32_t id) {
  uint32_t size = znet::player_data_size(id);
  if (size <= 7) return {};
  std::vector<uint32_t> words((size + 3) / 4, 0);
  if (!znet::get_player_data(id, words.data(), &size)) return {};
  return words;
}

// 0x00499030: each network player's record, from its data (none: skin 1,
// state 2), its place in the count unless kept, its score line.
void add_player(uint32_t id, uint32_t team, bool host, const char* name, void*) {
  MultiplayerGame* g = g_lobby ? g_lobby->game : nullptr;
  if (!g) return;
  int32_t state = 2, skin = 1;
  const std::vector<uint32_t> d = player_data(id);
  if (!d.empty()) {
    if (d[0] == 1 && d.size() > 2) state = static_cast<int32_t>(d[2]);
    skin = static_cast<int32_t>(d[1]);
  }
  auto p = std::make_unique<LobbyPlayer>();
  p->id = id, p->team = team, p->host = host ? 1 : 0, p->name = name ? name : "";
  p->skin = skin, p->state = state;
  if (!g_lobby->keep_order) p->order = g_order_next++;
  auto line = std::make_unique<ScoreLine>();
  line->kind = 1;
  p->score = line.get();
  g->score_lines.push_back(std::move(line));
  g->lobby_players.push_back(std::move(p));
}

// 0x00499280: each network team's record, its data the owner and skin.
void add_team(uint32_t id, uint32_t, bool, const char* name, void*) {
  MultiplayerGame* g = g_lobby ? g_lobby->game : nullptr;
  if (!g) return;
  auto t = std::make_unique<LobbyTeam>();
  t->id = id, t->name = name ? name : "";
  uint32_t data[2] = {}, size = sizeof data;
  znet::get_team_data(id, data, &size);
  t->owner = data[0], t->skin = static_cast<int32_t>(data[1]);
  auto line = std::make_unique<ScoreLine>();
  line->kind = 2;
  t->score = line.get();
  g->score_lines.push_back(std::move(line));
  g->lobby_teams.push_back(std::move(t));
}
}  // namespace

LobbyScreen* lobby_screen() { return g_lobby; }

// ---- the controls ----

// Mech3.exe 1.2 @ 0x00496880
void WorldCycle::step(int32_t by) {
  CycleButton::step(by);
  if (count() > 1 && index == 0) CycleButton::step(by);
}

void LobbyList::empty() {
  clear_items();
  records.clear();
  layout();  // +0xa4
}

void LobbyList::add(const std::string& text, LobbyEntry* e) {
  add_item(text.c_str(), static_cast<int32_t>(records.size()));
  records.push_back(e);
  layout();
}

LobbyEntry* LobbyList::chosen() {
  if (selected < 0 || selected >= item_count()) return nullptr;
  const int32_t v = items[static_cast<size_t>(selected)]->value;
  return v >= 0 && static_cast<size_t>(v) < records.size() ? records[static_cast<size_t>(v)] : nullptr;
}

// ---- the screen ----

// Mech3.exe 1.2 @ 0x00495f90
LobbyScreen::LobbyScreen(Engine& e, Handle s)
    : MenuScreen(e, s), chat_edit(e, this), cancel_btn(e, 1), start_btn(e, 0), mechlab_btn(e, 10), lock_btn(e, 0xb),
      worlds(e), kills_list(e, 0xd), time_list(e, 0xe), weight_list(e, 0xf), join_team_btn(e, 4), remove_team_btn(e, 5),
      kick_btn(e, 6), new_team_btn(e, 7), pick_skin_btn(e, 8), kick2_btn(e, 9), chat_up(e, -1), chat_down(e, -1),
      scores_list(e, -1), teams_list(e, -1), players_list(e, 0x17), player_name(e, -1), session_name(e, -1),
      internet_line(e), lan_line(e) {
  chat_edit.set_size(0x27);
  int32_t id = 0x10;
  for (InvertCheck* c : {&regen_box, &mobile_box, &stock_box, &ammo_box, &select_box}) c->id = id++;
  ready_box.id = 0x15;
  const bool moving = !e.fast_menu();
  if (load_from(e.main_menu_definition(), "MULTIPLAYER_HOST_DIALOG", moving)) {
    zimg::Texture* img = nullptr;
    uint8_t bg_moving = 0;
    e.main_menu_background(&img, &bg_moving);
    background.set_image(img);
    background.moving = bg_moving;
    if (!bg_moving) {
      background.set_pos(0, 0);
      background.src = &background.view;
    }
    add(&background);
    bind_primitive(&lines1, "LINES1");
    if (!moving) {
      bake();
      remove(&background);
      remove(&lines1);
    }
    bind_button(&session_name, "SESSION_NAME");
    bind_button(&cancel_btn, "CANCEL_BUTTON");
    bind_button(&start_btn, "START_BUTTON");
    bind_button(&mechlab_btn, "MECHLAB_BUTTON");
    bind_button(&lock_btn, "LOCK_OPTIONS_BUTTON");
    bind_button(&worlds, "DATABASE_LIST");
    bind_button(&kills_list, "KILLS_LIST");
    bind_button(&time_list, "TIME_LIST");
    bind_button(&weight_list, "WEIGHT_LIST");
    bind_button(&regen_box, "REGEN_CHECKBOX");
    bind_button(&mobile_box, "MOBILE_CHECKBOX");
    bind_button(&stock_box, "STOCK_CHECKBOX");
    bind_button(&ammo_box, "AMMO_CHECKBOX");
    bind_button(&select_box, "SELECT_CHECKBOX");
    bind_button(&player_name, "PLAYER_NAME");
    bind_button(&chat_edit, "CHAT_ENTER_EDIT");
    bind_button(&chat_team, "CHAT_TEAM_CHECKBOX");
    bind_button(&join_team_btn, "JOIN_TEAM_BUTTON");
    bind_button(&remove_team_btn, "REMOVE_TEAM_BUTTON");
    bind_button(&kick_btn, "KICK_PLAYER_BUTTON");
    bind_button(&new_team_btn, "NEW_TEAM_BUTTON");
    bind_button(&pick_skin_btn, "PICK_SKIN_BUTTON");
    bind_button(&kick2_btn, "KICK_PLAYER2_BUTTON");
    bind_button(&scores_list, "SCORES_LIST");
    bind_button(&teams_list, "TP_LIST");
    bind_button(&players_list, "PLAYERS_LIST");
    bind_button(&ready_box, "READY_CHECKBOX");
    bind_primitive(&chat_log, "CHAT_REC_LIST");
    bind_button(&chat_up, "CHAT_UP");
    bind_button(&chat_down, "CHAT_DOWN");
    teams_list.id = 0x16;
  }
  // Ours (piece 9c): the host's two address lines, under CANCEL and LOCK.
  const Handle font = ztwod::host().font_find("ia_small_green");
  int32_t y = 460;
  for (CopyLine* l : {&internet_line, &lan_line}) {
    if (font) l->FontText::set_font(font);
    l->set_pos(20, y);
    y += 10;
    add(l);
    l->hide();
  }
  set_flags_all(0);
  set_active(1);
  g_lobby = this;
}

// Mech3.exe 1.2 @ 0x00496b90
LobbyScreen::~LobbyScreen() {
  if (handlers_on_) handlers(false);
  if (g_lobby == this) g_lobby = nullptr;
}

void LobbyScreen::take_pilot(Handle p, MultiplayerGame* g) {
  pilot = p;
  game = g;
}

// Mech3.exe 1.2 @ 0x00497720: the names; the scores; the local player's data
// made (kind 1: its skin, the first free one in a game without teams, and
// each add-on world it has, multiplayer-port.md 11); the rules' lists filled
// and set from the game; the lists and buttons cleared; the handlers; the
// game open again (its fourth word 1); the network's players and teams.
void LobbyScreen::enter() {
  Engine& e = engine();
  if (!game) return;
  const uint32_t me = znet::local_id();
  const char* my_name = znet::player_name(me);
  set_labels(player_name, my_name && *my_name ? my_name : "Unknown");  // [0x005e56f0]
  const std::string& game_name = znet::session().desc.name;
  set_labels(session_name, game_name.empty() ? "Some Game" : game_name.c_str());  // [0x005e7c30]
  scores(true);
  std::vector<uint32_t> data(5, 0);  // 0x0046dd90(1, 0x14)
  data[0] = 1;
  data[1] = static_cast<uint32_t>(game->net_skin(me));
  keep_order = 0;
  refresh_lists();
  const bool team = multiplayer_team_play();
  if (data[1] == 0 && !team)
    for (int32_t i = 0; i < 8; ++i)
      if (free_skin(i + 1, -1) == 0) {
        data[1] = static_cast<uint32_t>(i + 1);
        break;
      }
  worlds.clear_items();
  worlds.add_item(e.message(0xfdf), 0);  // "UNAVAILABLE"
  worlds.add_item(e.message(0x320), 0x100);  // the shipped eight: (number << 8) | folder
  static const int32_t kShipped[7][2] = {{0, 2}, {1, 1}, {1, 2}, {2, 1}, {2, 2}, {3, 1}, {3, 2}};
  for (int32_t i = 0; i < 7; ++i) worlds.add_item(e.message(0x321 + i), kShipped[i][1] << 8 | kShipped[i][0]);
  // The add-on worlds (readermp3 on): each folder's numbered readers, named
  // by their mpinfo.zrd's db_name, recorded in the data.
  for (int32_t dir = 0; dir < 6; ++dir) {
    const std::string folder = e.mission_dir(static_cast<uint32_t>(dir));
    for (const std::string& file : e.list_files(("zbd\\" + folder + "\\readermp*.zbd").c_str())) {
      if (file.size() < 8) continue;
      const int32_t n = std::atoi(file.substr(8, file.find('.') == std::string::npos ? std::string::npos : file.find('.') - 8).c_str());
      if (n <= 2) continue;
      char path[160];
      std::snprintf(path, sizeof path, "zbd\\%s\\readermp%d.zbd", folder.c_str(), n);
      if (!zrdr::open_archive(path, false)) continue;
      zrdr::Node* info = zrdr::load("mpinfo.zrd");
      if (const char* s = zrdr::get_string(info, "db_name")) {
        const int32_t v = n << 8 | dir;
        worlds.add_item(s, v);
        data[3] += 1;  // 0x00498160
        data.resize(4 + data[3]);
        data[3 + data[3]] = static_cast<uint32_t>(v);
      }
      if (info) zrdr::free_node(info);  // FIX (bug): the exe kept each tree (a leak per entry)
      zrdr::close_archives(false);
    }
  }
  if (data.size() < 5) data.resize(5);
  znet::set_player_data(me, data.data(), static_cast<uint32_t>(data.size() * 4));
  // KILLS_LIST: 1, 5, 10, 20, 40 (doubled with teams) and unlimited (100).
  const int32_t unit = team ? 2 : 1;
  kills_list.clear_items();
  kills_list.add_item(unit < 2 ? e.message(0x2b1) : zutl::format_message_f(0x2b2, unit).c_str(), unit);
  for (int32_t m : {5, 10, 20, 40}) kills_list.add_item(zutl::format_message_f(0x2b2, unit * m).c_str(), unit * m);
  kills_list.add_item(e.message(0x2b5), 100);
  kills_list.select_value(5);
  time_list.clear_items();
  static const int32_t kMinutes[4] = {5, 10, 20, 30};
  for (int32_t i = 0; i < 4; ++i) time_list.add_item(e.message(0x2b6 + i), kMinutes[i]);
  time_list.add_item(e.message(0x2ba), 100);
  weight_list.clear_items();
  for (int32_t t = 30; t < 0x69; t += 5) weight_list.add_item(zutl::format_message_f(0x2bb, t).c_str(), t);
  weight_list.select_value(0x37);
  if (worlds.select_value(game->world()) == -1) {
    worlds.select(0);  // both, as the exe: BEACHHEAD in the end
    worlds.select(1);
  }
  kills_list.select_value(game->x70);
  time_list.select_value(static_cast<int32_t>(game->x74));
  weight_list.select_value(game->x78);
  regen_box.set_checked(game->b8c);
  const int32_t w = worlds.value_at(worlds.index);
  const bool mobile_world = !((w & 0xff) == 4 && (w >> 8) > 2);
  mobile_box.set_checked(game->ba8 && mobile_world ? 1 : 0);
  stock_box.set_checked(game->b8f);
  ammo_box.set_checked(game->b8d);
  select_box.set_checked(game->b8e);
  chat_team.set_checked(0);
  teams_list.empty();
  players_list.empty();
  // 0x00499680: the chat's lines cleared (piece 12).
  chat_edit.set_text("");
  chat_edit.set_focus(0);
  chat_edit.set_focus(1);
  locked = 0, all_ready = 0, start_pending = 0;
  host_controls();
  for (ztwod::Control* c : std::initializer_list<ztwod::Control*>{&join_team_btn, &remove_team_btn, &kick_btn,
                                                                  &new_team_btn, &pick_skin_btn, &kick2_btn,
                                                                  &mechlab_btn, &start_btn})
    set_enabled(*c, 0);
  ready_box.set_checked(0);
  set_enabled(ready_box, 0);
  if (team) chat_team.show_for(-1.0f);
  else chat_team.hide();
  g_lobby_dirty = 1;
  g_lobby_back = 0;
  handlers(true);
  znet::GameInfo info;
  if (znet::is_host() && znet::game_info(-1, &info)) {
    info.user[3] = 1;  // open (2 in progress, 3 locked)
    znet::set_game_info(info);  // 0x00586f20
  }
  znet::enum_players();
  if (team) {
    znet::enum_teams();
    znet::enum_team_players();
  }
}

// Mech3.exe 1.2 @ 0x004984e0. Ours: the host's addresses each half second.
void LobbyScreen::update(float dt) {
  if (g_lobby_back) {
    on_button(1, 0, 0);
    g_lobby_back = 0;
  }
  if (g_lobby_dirty) {
    // 0x00499b10: the host's rules to everyone (piece 11b).
    refresh_lists();
    refresh_buttons();
    g_lobby_dirty = 0;
  }
  // start_pending: 0x0049b790 (piece 11c).
  if ((reach_timer -= dt) <= 0.0f) {
    show_reach();
    reach_timer = 0.5f;
  }
  Window::update(dt);
}

// Mech3.exe 1.2 @ 0x00497410. The rules' (0xb..0x14), the chat's (2, 3),
// START's (0), the teams' (4, 5, 7, 8), MECHLAB's (10) and READY's (0x15)
// come with pieces 11b, 11c and 12.
void LobbyScreen::on_button(int32_t id, int32_t a, int32_t b) {
  (void)a, (void)b;
  Engine& e = engine();
  switch (id) {
    case 1:  // CANCEL: the handlers off, back, the network off, the records gone
      handlers(false);
      e.pop_state(1);
      e.network_set(0);
      g_lobby_dirty = 0;
      if (game) game->clear_lobby();
      return;
    case 6: kick(teams_list); return;
    case 9: kick(players_list); return;
    case 0x16: team_chosen(); return;
    case 0x17: player_chosen(); return;
    default: return;
  }
}

// Mech3.exe 1.2 @ 0x00498550. FIX (A): with `report` and a lobby's launch
// ([0x00608e24]) the scores went to the game service's COM object
// ([0x007307e0], 0x00589d00..0x00589e30); there is no such service.
void LobbyScreen::scores(bool report) {
  (void)report;
  scores_list.clear_items();
  scores_list.layout();
  if (!game) return;
  if (game->lobby_players.empty()) {
    scores_list.add_item(engine().message(0x2bd), 0);  // "  No Scores to report"
    return;
  }
  game->score_texts(0x15, 7);
  for (const auto& l : game->score_lines) scores_list.add_item(l->text.c_str(), 0);
}

// Mech3.exe 1.2 @ 0x00498710: the records made again from the network's
// players (and teams); TP_LIST each team ("name  {skin} ") with its players
// under it, PLAYERS_LIST the players without a team: the host's "++", "**"
// for one missing the chosen add-on world (PLAYERS_LIST only), "In MechLab"
// or "Ready!" (any other: not all ready), the skin outside team play, our
// own mech's name. Our slot's skin is our side's; a changed player count
// asks the session and the players again.
void LobbyScreen::refresh_lists() {
  Engine& e = engine();
  if (!game) return;
  game->clear_lobby();
  all_ready = 1;
  g_order_next = 1;
  znet::each_player(add_player, nullptr);
  const bool team = multiplayer_team_play();
  if (team) {
    znet::each_team(add_team, nullptr);
    teams_list.empty();
    for (auto& t : game->lobby_teams) {
      t->count = 0;
      teams_list.add(t->name + skin_tag(e, "  {%s} ", t->skin), t.get());
      for (auto& p : game->lobby_players) {
        if (p->team != t->id) continue;
        std::string s = "   " + p->name;  // [0x005e7c4c]
        if (p->host) s += "++";
        s += " ";
        if (p->state == 1) s += e.message(0x2bf);  // "In MechLab"
        if (p->state == 3) s += e.message(0x2c0);  // "Ready!"
        else all_ready = 0;
        teams_list.add(s, p.get());
        ++t->count;
      }
    }
  }
  players_list.empty();
  const int32_t chosen_world = worlds.value_at(worlds.index);
  for (auto& p : game->lobby_players) {
    GameSlot* mine = p->id == znet::local_id() ? game->slot_at(0) : nullptr;
    if (p->team != 0) continue;
    std::string s = p->name;
    if (p->host) s += "++";
    if ((chosen_world >> 8) > 2 && !has_world(p->id, chosen_world)) s += "**";
    s += " ";
    if (p->state == 1) s += e.message(0x2bf);
    if (p->state == 3) s += e.message(0x2c0);
    else all_ready = 0;
    if (!team && p->skin != 0) s += skin_tag(e, " {%s} ", p->skin);
    if (mine) {
      const int32_t m = mech_name_message(mine->mech);
      s += m < 0 ? "notmech" : e.message(m);  // 0x00484670
    }
    players_list.add(s, p.get());
  }
  game->slot_at(0)->skin = game->net_skin(znet::local_id()) - 1;  // 0x005a2b90
  znet::GameInfo info;
  if (znet::game_info(-1, &info) && game->lobby_players.size() != info.players) {
    znet::refresh_session();  // 0x00586e10
    znet::enum_players();
  }
}

// Mech3.exe 1.2 @ 0x004993f0: once the options are locked, READY for a
// player with a skin (a team in team play), MECHLAB until ready; START for
// the host when everyone is; ready, the lists' choices and their buttons
// off; NEW_TEAM for the host while there are under 8 teams.
void LobbyScreen::refresh_buttons() {
  const uint32_t me = znet::local_id();
  const bool team = multiplayer_team_play();
  const bool placed = locked && (team ? game->lobby_team_id(me) != 0 : game->lobby_skin(me) != 0);
  set_enabled(ready_box, placed ? 1 : 0);
  const bool in = team ? locked && game->lobby_team_id(me) != 0 : locked != 0;
  set_enabled(mechlab_btn, in && !ready_box.checked ? 1 : 0);
  set_enabled(start_btn, znet::is_host() && all_ready ? 1 : 0);
  if (ready_box.checked) {
    teams_list.select_index(-1);
    players_list.select_index(-1);
    for (ztwod::Control* c : std::initializer_list<ztwod::Control*>{&pick_skin_btn, &join_team_btn, &remove_team_btn,
                                                                    &kick_btn, &kick2_btn})
      set_enabled(*c, 0);
  }
  if (team && znet::is_host())
    set_enabled(new_team_btn, game->lobby_teams.size() < 8 && !ready_box.checked ? 1 : 0);
}

// Mech3.exe 1.2 @ 0x00498260: the rules the host's (until locked): the
// lists' arrows, the boxes, LOCK and (team play) NEW_TEAM. "UNAVAILABLE"
// chosen becomes BEACHHEAD; a world without MOBILE (an add-on of folder 4)
// clears and disables it.
void LobbyScreen::host_controls() {
  const bool on = znet::is_host() && !locked;
  for (CycleButton* c : {static_cast<CycleButton*>(&worlds), &kills_list, &time_list, &weight_list})
    c->set_arrows(on, on);
  for (ztwod::Control* c : std::initializer_list<ztwod::Control*>{&regen_box, &mobile_box, &stock_box, &ammo_box,
                                                                  &select_box, &lock_btn})
    set_enabled(*c, on ? 1 : 0);
  set_enabled(new_team_btn, on && multiplayer_team_play() ? 1 : 0);
  if (!on) return;
  if (worlds.item(worlds.index) && worlds.item(worlds.index)->value == 0) {
    worlds.select(1);
    g_lobby_dirty = 1;
  }
  const int32_t w = worlds.item(worlds.index) ? worlds.item(worlds.index)->value : -1;
  if ((w & 0xff) == 4 && (w >> 8) > 2 && mobile_box.checked) {
    mobile_box.set_checked(0);
    set_enabled(mobile_box, 0);
    g_lobby_dirty = 1;
  }
}

// Mech3.exe 1.2 @ 0x00497fbf..0x004980b6 (on), 0x00499a30 (off). The chat's
// 0x1f (0x00499610), the rules' 0x20 (0x00499cf0), the start's 0x1e
// (0x0049bcb0) and the scores' 0x14 (0x0049b150) come with 11b, 11c, 12.
void LobbyScreen::handlers(bool on) {
  static const uint16_t kChange[] = {3, 4, 9, 0xa, 5, 6, 8, 2};
  for (uint16_t t : kChange) on ? (void)znet::on(t, on_change) : (void)znet::off(t, on_change);
  if (on) {
    znet::on(7, on_kicked);
    znet::on(0xd, on_host);
  } else {
    znet::off(7, on_kicked);
    znet::off(0xd, on_host);
  }
  handlers_on_ = on;
}

// Mech3.exe 1.2 @ 0x00499600
void LobbyScreen::on_change(uint32_t, const uint8_t*, size_t) { g_lobby_dirty = 1; }

// Mech3.exe 1.2 @ 0x0049a1d0: "You have been kicked out of this game.", then
// back to the connection screen (the update's CANCEL).
void LobbyScreen::on_kicked(uint32_t, const uint8_t*, size_t) {
  if (!g_lobby) return;
  g_lobby->cursor_obj.erase();  // [+0x58] vt+8
  g_lobby->engine().message_box(0x30, 0x2a8, 0x2c3);
  g_lobby_back = 1;
}

// Mech3.exe 1.2 @ 0x0049a200: a new host takes the rules.
void LobbyScreen::on_host(uint32_t, const uint8_t*, size_t) {
  if (g_lobby && znet::is_host()) g_lobby->host_controls();
  g_lobby_dirty = 1;
}

// Mech3.exe 1.2 @ 0x0049a150, 0x0049a7a0: the chosen player out of the game;
// not ourselves (0x2c1).
void LobbyScreen::kick(LobbyList& from) {
  LobbyEntry* en = from.chosen();
  if (!en || en->kind() != 1) return;
  if (en->id == znet::local_id()) {
    cursor_obj.erase();
    engine().message_box(0x30, 0x2a8, 0x2c1);  // "You can't kick yourself out of the game!"
    return;
  }
  znet::kick(en->id);
}

// Mech3.exe 1.2 @ 0x0049a3a0: a team chosen gives PICK_SKIN to the host while
// there are under 8 teams, JOIN when we are not in it, REMOVE to the host
// for an empty one; a player chosen KICK to the host (not ourselves). Ready,
// nothing is chosen.
void LobbyScreen::team_chosen() {
  LobbyEntry* en = teams_list.chosen();
  if (!en) return;
  if (ready_box.checked) {
    teams_list.select_index(-1);
    return;
  }
  for (ztwod::Control* c : std::initializer_list<ztwod::Control*>{&join_team_btn, &remove_team_btn, &kick_btn,
                                                                  &pick_skin_btn})
    set_enabled(*c, 0);
  const uint32_t me = znet::local_id();
  if (en->kind() == 2) {
    if (znet::is_host()) set_enabled(pick_skin_btn, game->lobby_teams.size() < 8 ? 1 : 0);
    if (!game->in_team(me, en->id)) set_enabled(join_team_btn, 1);
    if (znet::is_host() && static_cast<LobbyTeam*>(en)->count == 0) set_enabled(remove_team_btn, 1);
  } else if (en->kind() == 1) {
    if (znet::is_host() && en->id != me) set_enabled(kick_btn, 1);
  }
}

// Mech3.exe 1.2 @ 0x0049a6a0: KICK2 to the host for another player, PICK_SKIN
// for ourselves outside team play.
void LobbyScreen::player_chosen() {
  LobbyEntry* en = players_list.chosen();
  if (!en) return;
  if (ready_box.checked) {
    players_list.select_index(-1);
    return;
  }
  set_enabled(kick2_btn, 0);
  set_enabled(pick_skin_btn, 0);
  if (en->kind() != 1) return;
  const uint32_t me = znet::local_id();
  if (znet::is_host() && en->id != me) set_enabled(kick2_btn, 1);
  if (!multiplayer_team_play() && en->id == me) set_enabled(pick_skin_btn, 1);
}

// Mech3.exe 1.2 @ 0x0049bd10: in team play, the team with that skin (not
// `except`), 0 none; outside team play always 0.
int32_t LobbyScreen::free_skin(int32_t skin, int32_t except) {
  if (!multiplayer_team_play() || !game || skin == except) return 0;
  for (auto& t : game->lobby_teams)
    if (t->skin == skin) return static_cast<int32_t>(t->id);
  return 0;
}

// Mech3.exe 1.2 @ 0x0049ab30
bool LobbyScreen::has_world(uint32_t player, int32_t world) {
  const std::vector<uint32_t> d = player_data(player);
  if (d.size() < 4 || d[0] != 1) return false;
  for (uint32_t k = 0; k < d[3] && 4 + k < d.size(); ++k)
    if (static_cast<int32_t>(d[4 + k]) == world) return true;
  return false;
}

// The host's lines (the connection screen's, mp_connect.cpp); none for a
// player who joined.
void LobbyScreen::show_reach() {
  if (!znet::is_host()) {
    internet_line.show("", "");
    lan_line.show("", "");
    return;
  }
  const Engine::NetReach r = engine().network_reach();
  const std::string line = reach_line(r);
  if (line != internet_line.line) internet_line.show(line, reach_copy(r));
  const std::string lan = r.lan.empty() ? "" : "LAN: " + r.lan + "  - click to copy";
  if (lan != lan_line.line) lan_line.show(lan, r.lan);
}

}  // namespace mech3
