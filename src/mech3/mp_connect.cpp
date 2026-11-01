#include "mech3/mp_connect.h"

#include <cstdio>
#include <cstring>

#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zutil/zutl_msgdll.h"

namespace mech3 {

namespace {
// The bars' rejected characters (0x005a4840 with [0x005e28e8]) as a file
// name's, as the pilot screen's.
constexpr char kReject[] = ":|\\<>/?\"*\t";

std::string trimmed(const char* s) {
  std::string t = s ? s : "";
  const size_t a = t.find_first_not_of(" \t");
  if (a == std::string::npos) return "";
  const size_t b = t.find_last_not_of(" \t");
  return t.substr(a, b - a + 1);
}

// Every label of a control (its four lists: shown, rollover, activate,
// disabled) to `text` (0x00495240; 0x00495200 the first list alone).
void set_labels(ztwod::Control& c, const char* text) {
  for (std::vector<ztwod::Text*>* l : {&c.activate_labels, &c.disable_labels, &c.rollover_labels, &c.labels})
    if (!l->empty() && l->front()) l->front()->set_text_plain(text);
}
void set_enabled(ztwod::Control& c, int32_t on) {
  c.enabled = on;
  c.refresh();  // +0x80
}
}  // namespace

MultiplayerSetup& multiplayer_setup() {
  static MultiplayerSetup s;
  return s;
}
bool multiplayer_team_play() { return multiplayer_setup().team_play != 0; }

// ---- the connection ----

// The TCP/IP connection's calls over our transport: the address is the
// search's, an open to search or to host needs nothing more; the close
// stops the search.
bool NetConnection::open(bool host) {
  (void)host;
  return znet::session().initialised;
}
void NetConnection::close() { znet::stop_search(); }
void NetConnection::set_address(const std::string& a) { znet::session().address = a; }

// ---- the controls ----

// Mech3.exe 1.2 @ 0x00494420: the providers (0x005881f0), each one's name.
void ConnectionCycle::fill() {
  clear_items();
  for (const znet::Connection& c : znet::connections()) add_item(c.name.c_str(), 0);
}

int32_t ConnectionCycle::chosen_kind() const {
  const std::vector<znet::Connection>& list = znet::connections();
  if (index < 0 || static_cast<size_t>(index) >= list.size()) return -1;
  return list[static_cast<size_t>(index)].kind;
}

// Mech3.exe 1.2 @ 0x00494510: the games found as lines, "name <Player
// Count: n of m>" (0x29a), "<Game in Progress>" (0xfb9, the fourth word 2)
// or "<Game Locked>" (0xfba, 3); the game chosen before (by its name, the
// line before its '<') chosen again, JOIN as it can be joined.
void SessionList::refill(int32_t count) {
  std::string keep;
  if (selected >= 0 && selected < item_count() && items[static_cast<size_t>(selected)]->text) {
    keep = items[static_cast<size_t>(selected)]->text;
    keep = keep.substr(0, keep.find('<'));
  }
  clear_items();
  layout();  // +0xa4
  int32_t again = -1;
  for (int32_t i = 0; i < count; ++i) {
    znet::GameInfo g;
    if (!znet::game_info(i, &g)) continue;
    std::string line;
    if (g.user[3] == 2) line = zutl::format_message_f(0xfb9, g.name.c_str());
    else if (g.user[3] == 3) line = zutl::format_message_f(0xfba, g.name.c_str());
    else line = zutl::format_message_f(0x29a, g.name.c_str(), static_cast<int>(g.players), static_cast<int>(g.max_players));
    if (!keep.empty() && line.substr(0, line.find('<')) == keep) {
      again = i;
      set_enabled(screen->join_btn, g.user[3] == 3 ? 0 : 1);
    }
    add_item(line.c_str(), i);
    layout();
  }
  select(again);  // +0x98
}

// Mech3.exe 1.2 @ 0x004938f0
void ConnectBox::on_left_up() { set_checked(screen->connect(checked == 0)); }

// Mech3.exe 1.2 @ 0x004938c0, 0x00493940 (not while it searches), 0x00493930
void FocusBar::on_enter() {
  if (which == 2 && no_focus) return;
  screen->focus(which);
}

// Mech3.exe 1.2 @ 0x00493960: Enter in CONNECT_INFO_EDIT is FIND GAME.
void FocusBar::on_return() {
  if (which == 2) screen->find();
}

// ---- the screen ----

// Mech3.exe 1.2 @ 0x00493260. FIX (A): the modem's ANSWER, DIAL and CALL are
// hidden (no modem provider is listed, multiplayer-port.md piece 9's plan).
ConnectScreen::ConnectScreen(Engine& e, Handle s)
    : MenuScreen(e, s), join_btn(e, 2), connections(e), sessions(e, this), cancel_btn(e, 1), host_btn(e, 3),
      answer_btn(e, 4), dial_btn(e, 5), find_btn(e, 6), connect_box(this), player_name(e, this, 1), max_players(e, -1),
      session_name(e, this, 3), address(e, this, 2), info1(e, -1), info2(e, -1), internet_line(e), lan_line(e) {
  player_name.editor.set_reject(kReject);
  player_name.set_size(0x10);
  session_name.editor.set_reject(kReject);
  session_name.set_size(0x19);
  address.set_size(0x19);
  const Handle a = e.video_back_buffer();
  e.render_set_target2(e.video_target(), e.video_buffer_d90(), e.video_buffer_da0(), a);
  const bool moving = !e.fast_menu();
  if (load_from(e.main_menu_definition(), "MULTIPLAYER_DIALOG", moving)) {
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
    bind_primitive(&lines2, "LINES2");
    bind_button(&cancel_btn, "CANCEL_BUTTON");
    if (!moving) {
      bake();
      remove(&background);
      remove(&lines1);
      remove(&lines2);
      cancel_btn.set_baked(true);
    }
    bind_button(&sessions, "SESSION_LIST");
    bind_button(&join_btn, "JOIN_BUTTON");
    bind_button(&host_btn, "HOST_BUTTON");
    bind_button(&answer_btn, "ANSWER_BUTTON");
    bind_button(&dial_btn, "DIAL_BUTTON");
    bind_button(&find_btn, "FIND_GAME_BUTTON");
    bind_button(&connect_box, "CONNECT_CHECKBOX");
    bind_button(&team_box, "TEAM_CHECKBOX");
    bind_button(&call_box, "CALL_BUTTON");
    bind_button(&connections, "CONNECTION_LIST");
    bind_button(&player_name, "PLAYER_NAME");
    bind_button(&max_players, "MAX_PLAYERS_LIST");
    bind_button(&session_name, "NEW_SESSION_NAME");
    bind_button(&address, "CONNECT_INFO_EDIT");
    bind_button(&info1, "INFO_TEXT1");
    bind_button(&info2, "INFO_TEXT2");
  }
  // Ours (piece 9c): the host's two address lines, under the host setup.
  const Handle font = ztwod::host().font_find("ia_small_green");
  int32_t y = 344;
  for (CopyLine* l : {&internet_line, &lan_line}) {
    if (font) l->FontText::set_font(font);
    l->set_pos(180, y);
    y += 12;
    add(l);
    l->hide();
  }
  set_flags_all(0);
  constructed_ = false;  // [0x006099d8] is 0 through the first reset: no focus given
  connect(false);
  set_active(1);
  constructed_ = true;
  answer_btn.hide();
  dial_btn.hide();
  call_box.hide();
}

// Mech3.exe 1.2 @ 0x00493a70: the connection with the screen.
ConnectScreen::~ConnectScreen() = default;

// Mech3.exe 1.2 @ 0x00494470: while connected, the search asked when its
// time comes: again at once while DirectPlay is still at it, else in 3 s;
// its games into the list, a lost connection the screen reset.
void ConnectScreen::update(float dt) {
  if (conn && (timer -= dt) <= 0.0f) {  // [0x005c6158]
    uint32_t count = 0;
    const int32_t r = znet::find_games(&count);
    timer = r == znet::kErrConnecting ? 0.0f : 3.0f;  // [0x005c615c]
    if (r == znet::kOk) sessions.refill(static_cast<int32_t>(count));
    else if (r == znet::kErrNoConnection) connect(false);
  }
  if ((reach_timer -= dt) <= 0.0f) {  // ours (piece 9c)
    show_reach();
    reach_timer = 0.5f;
  }
  Window::update(dt);
}

// Mech3.exe 1.2 @ 0x004953a0
void ConnectScreen::on_button(int32_t id, int32_t a, int32_t b) {
  (void)a, (void)b;
  Engine& e = engine();
  switch (id) {
    case 1:  // CANCEL: the network off and back
      connect(false);
      e.pop_state(1);
      e.network_set(0);
      return;
    case 2: join(); return;
    case 3:
    case 4: host(); return;
    case 5:
    case 6: find(); return;
    case 7: chosen(); return;
    default: return;
  }
}

void ConnectScreen::set_info(int32_t first, int32_t second) {
  set_labels(info1, engine().message(first));
  set_labels(info2, engine().message(second));
}

// Mech3.exe 1.2 @ 0x004949f0. Off: no connection, the games' list empty,
// every button but the provider's list off, the lines asking for a
// connection, the limits 2..8 (8 chosen). On: the chosen provider's
// connection (0x0058a110), the list locked, and for TCP/IP (kind 2; its
// Winsock 2 check 0x00575e30 is moot, FIX (A)) HOST, the address and FIND
// GAME on. Whether it is connected.
uint8_t ConnectScreen::connect(bool on) {
  Engine& e = engine();
  if (!on) {
    if (constructed_) focus(0);
    sessions.clear_items();
    sessions.layout();
    connect_box.set_checked(0);
    connections.set_arrows(1, 1);
    for (ztwod::Control* c : {static_cast<ztwod::Control*>(&host_btn), static_cast<ztwod::Control*>(&answer_btn),
                              static_cast<ztwod::Control*>(&dial_btn), static_cast<ztwod::Control*>(&find_btn),
                              static_cast<ztwod::Control*>(&join_btn)})
      set_enabled(*c, 0);
    set_info(0x2a4, 0x2a5);  // "Pick a connection type", "Then check Connect Box"
    set_enabled(address, 0);
    address.no_focus = 0;
    if (!address.labels.empty() && address.labels.front()) address.labels.front()->set_text_plain("");
    address.set_text("");
    max_players.clear_items();
    for (int32_t n = 2; n <= 8; ++n) max_players.add_item(std::to_string(n).c_str(), n);
    max_players.select(6);
    set_labels(find_btn, e.message(0x2a6));  // "Find Game"
    set_labels(dial_btn, e.message(0x2a7));  // "Dial"
    find_shows_find = 1;
    searching_for.clear();
    if (conn) conn->close();
    conn.reset();
    return 0;
  }
  const int32_t kind = connections.chosen_kind();
  if (kind < 0) return 0;
  if (conn) conn->close();
  conn.reset();
  if (kind != 2) return connect(false);  // the modem's, IPX's and serial's are not made (FIX (A))
  conn = std::make_unique<NetConnection>(kind);
  connect_box.set_checked(1);
  connections.set_arrows(0, 0);
  sessions.clear_items();
  sessions.layout();
  set_enabled(host_btn, 1);
  set_enabled(address, 1);
  set_info(0x2a1, 0x2a2);  // "Enter an IP address", "Or leave blank to search"
  if (!address.labels.empty() && address.labels.front()) address.labels.front()->set_text_plain(e.message(0x2a3));
  set_enabled(find_btn, 1);
  find_shows_find = 1;
  multiplayer_setup().kind = 2;
  return 1;
}

// Mech3.exe 1.2 @ 0x00495310
void ConnectScreen::focus(int32_t which) {
  address.set_focus(0);
  session_name.set_focus(0);
  player_name.set_focus(0);
  if (which == 1) player_name.set_focus(1);
  else if (which == 2) address.set_focus(1);
  else if (which == 3) session_name.set_focus(1);
}

// Mech3.exe 1.2 @ 0x00495490. The game's version is that of the game chosen;
// another one's leaves (0xfdd the host's older, 0xfde ours). Ours: DirectPlay
// listed a game's players before the join (0x00588600, 0x00589600), ours
// knows them after it, so a name in use is found then and the game left.
void ConnectScreen::join() {
  Engine& e = engine();
  focus(0);
  znet::GameInfo g;
  const int32_t sel = sessions.selected;
  znet::game_info(sel, &g);
  if (g.user[3] == 3) return;  // locked
  name = trimmed(player_name.get_text());
  g.index = sel;
  if (znet::open_join(g) < 0) {
    timer = 0;
    return;
  }
  if (g.user[1] != kGameVersion) {
    e.message_box(0x30, 0x2a8, g.user[1] < kGameVersion ? 0xfdd : 0xfde);
    e.pop_state(1);
    e.network_set(0);
    return;
  }
  znet::enum_players();  // 0x005885c0: the session's players
  for (const auto& r : znet::lists().players)
    if (r && r->short_name == name) {
      znet::close_session();
      e.message_box(0x30, 0x2a8, 0x2a9);  // "Your name is already in use."
      return;
    }
  if (znet::create_player(name.c_str())) {
    connect(false);
    multiplayer_setup().team_play = 0;
    team = 0;
    multiplayer_setup().max_players = static_cast<int32_t>(g.max_players);
    e.multiplayer_page(state, 2, 1);  // 0x0049cb00: the lobby
  }
}

// Mech3.exe 1.2 @ 0x00495600: a game named NEW_SESSION_NAME (none: 0x2aa),
// MAX_PLAYERS_LIST's limit, TEAM_CHECKBOX's team play, the game's version in
// its second word; hosted, then our player, then the lobby.
void ConnectScreen::host() {
  Engine& e = engine();
  focus(0);
  if (!conn) return;
  game_name = trimmed(session_name.get_text());
  if (game_name.empty()) {
    e.message_box(0x30, 0x2a8, 0x2aa);  // "You must provide a session name."
    return;
  }
  const int32_t limit = max_players.selected_value();
  name = trimmed(player_name.get_text());
  multiplayer_setup().team_play = team_box.checked != 0 ? 1 : 0;
  team = multiplayer_setup().team_play;
  znet::GameInfo g;
  g.user[0] = 1;
  g.user[1] = kGameVersion;
  multiplayer_setup().max_players = limit;
  g.max_players = static_cast<uint32_t>(limit);
  g.name = game_name;
  bool made = false;
  if (conn->open(true) && znet::open_host(g) >= 0) made = znet::create_player(name.c_str()) != 0;
  if (made) {
    e.network_hosting(true);  // ours (piece 9c): the port opened on the router
    connect(false);
    e.multiplayer_page(state, 2, 1);  // 0x0049cb00: the lobby
  }
}

// Mech3.exe 1.2 @ 0x004957c0 (TCP/IP's branch): FIND GAME searches at the
// address (blank: the LAN) and becomes Stop Search, which ends the search
// and the connection (the screen reset). FIX (bug): no connection, nothing
// (the exe read it unchecked). Ours (Owen's join by address while the LAN is
// searched): Stop Search with another address searches there instead.
void ConnectScreen::find() {
  if (!conn || conn->kind != 2) return;
  const std::string at = trimmed(address.get_text());
  if (!find_shows_find) {
    if (at == searching_for) {
      timer = 10000.0f;
      conn->close();
      connect(false);
      return;
    }
  } else {
    address.no_focus = 1;
    set_labels(find_btn, engine().message(0x2ab));  // "Stop Search"
    find_shows_find = 0;
  }
  searching_for = at;
  conn->set_address(at);
  conn->open(false);
  timer = 0.5f;
}

// Mech3.exe 1.2 @ 0x00495410: JOIN for a game chosen that is not locked.
void ConnectScreen::chosen() {
  set_enabled(join_btn, 0);
  if (sessions.selected < 0) return;
  znet::GameInfo g;
  znet::game_info(sessions.selected, &g);
  if (g.user[3] != 3) set_enabled(join_btn, 1);
}

// Mech3.exe 1.2 @ 0x00494920: the pilot's name, "<name>'s Game" (0x29b),
// the providers and the limits; the reset. Ours: then the LAN searched.
void ConnectScreen::take_pilot(Handle p, const char* pilot_name) {
  pilot = p;
  connections.fill();
  if (p && pilot_name) {
    player_name.set_text(pilot_name);
    session_name.set_text(zutl::format_message_f(0x29b, pilot_name).c_str());
  }
  max_players.clear_items();
  for (int32_t n = 2; n <= 8; ++n) max_players.add_item(std::to_string(n).c_str(), n);
  max_players.select(6);
  connect(false);
  search_lan();
}

// Mech3.exe 1.2 @ 0x004949d0
void ConnectScreen::forget_kind() {
  multiplayer_setup().kind = -1;
  timer = 0;
}

void ConnectScreen::search_lan() {
  if (connections.count() == 0) return;
  connections.select(0);
  connect_box.set_checked(connect(true));
  if (conn) find();
}

// ---- the host's addresses (ours, piece 9c) ----

void CopyLine::show(const std::string& text, const std::string& to_copy) {
  copy = to_copy;
  line = text;
  if (text.empty()) {
    hide();
    return;
  }
  set_text_plain(text.c_str());
  measure();
  fixed_rect = bg_rect;
  show_for(-1.0f);
}

void CopyLine::on_left_up() {
  if (copy.empty()) return;
  e_.copy_text(copy.c_str());
  const std::string keep = line;
  show(copy + " (copied)", copy);
  line = keep;
}

// The internet line: the address to give friends when the router opened (or
// will open, when hosting) the port; else what stands in the way: no router
// (forward the port), a refusal (forward it), carrier-grade NAT (the lead:
// say so, and do not offer that address). The LAN line: this computer's.
std::string reach_line(const Engine::NetReach& r) {
  const std::string port = std::to_string(r.port ? r.port : 27930);
  switch (r.state) {
    case Engine::NetReach::kLooking: return "Internet: looking for your router...";
    case Engine::NetReach::kNoRouter: return "Internet: forward UDP " + port + " on your router to host";
    case Engine::NetReach::kRefused: return "Could not open the port: forward UDP " + port + " on your router";
    case Engine::NetReach::kBlocked: return "Your internet provider blocks hosting: use a LAN, or a VPN such as Tailscale";
    case Engine::NetReach::kDoubleNat:
      return "Another router stands in front of yours: forward UDP " + port + " on it, or use a VPN such as Tailscale";
    case Engine::NetReach::kReady:
    case Engine::NetReach::kMapped:
      if (!r.internet.empty())
        return "Internet: " + r.internet + (r.state == Engine::NetReach::kMapped ? " (port open)" : "") + "  - click to copy";
      return "Internet: forward UDP " + port + " on your router to host";
    default: return "";
  }
}

std::string reach_copy(const Engine::NetReach& r) {
  return (r.state == Engine::NetReach::kReady || r.state == Engine::NetReach::kMapped) ? r.internet : "";
}

void ConnectScreen::show_reach() {
  const Engine::NetReach r = engine().network_reach();
  const std::string line = reach_line(r);
  if (line != internet_line.line) internet_line.show(line, reach_copy(r));
  const std::string lan = r.lan.empty() ? "" : "LAN: " + r.lan + "  - click to copy";
  if (lan != lan_line.line) lan_line.show(lan, r.lan);
}

}  // namespace mech3
