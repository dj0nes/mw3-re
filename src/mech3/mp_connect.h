#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "mech3/controls_menu.h"
#include "mech3/menu_widgets.h"

// The multiplayer connection screen (docs/multiplayer-port.md, piece 9b):
// MULTIPLAYER_DIALOG of the main menu's dialog.zrd, the Multiplayer State's
// first screen. The service provider and its connection, the games found
// through it, JOIN and HOST, then the lobby (the state's page 2). Original
// file unknown: 0x00493260..0x00495a50, between mech3\mech_waynet.cpp and the
// lobby (0x00495f90).
namespace mech3 {

class ConnectScreen;

// The connection screen's globals the lobby reads.
struct MultiplayerSetup {
  int32_t kind = -1;          // [0x00608e28]  the connection's kind, -1 none (0x004949d0)
  uint8_t team_play = 0;      // [0x00608e2c]  the host's TEAM_CHECKBOX
  int32_t max_players = 0;    // [0x00608e30]  the game's limit
};
MultiplayerSetup& multiplayer_setup();

// The game's version in a session's second word ([0x005e55e8]): a join to a
// host with another one is refused (0xfdd, 0xfde).
constexpr uint32_t kGameVersion = 0x28;

// The connection for the chosen provider (0x0058a110: kind 2 TCP/IP's,
// vtable 0x005cefa8 family; the modem's, IPX's and serial's are gone, FIX
// (A)): its address set (+0xc), opened to search (+4 with 0) or to host
// (+4 with 1), closed (+8). Over our transport: the address is the search's.
class NetConnection {
 public:
  explicit NetConnection(int32_t kind) : kind(kind) {}
  bool open(bool host);                       // +0x04
  void close();                               // +0x08
  void set_address(const std::string& a);     // +0x0c
  int32_t kind;                               // the provider's +0x10
};

// CONNECTION_LIST (vtable 0x005c6958): the service providers, one by design.
class ConnectionCycle : public CycleButton {
 public:
  explicit ConnectionCycle(Engine& e) : CycleButton(e, -1) {}
  void fill();                       // 0x00494420: each provider's name (its +0x14)
  int32_t chosen_kind() const;       // the chosen provider's kind, -1 none
};

// SESSION_LIST (vtable 0x005c68b0), id 7: the games found.
class SessionList : public FileList {
 public:
  SessionList(Engine& e, ConnectScreen* s) : FileList(e, 7), screen(s) {}
  void refill(int32_t count);        // 0x00494510: each game's line; the one chosen kept
  ConnectScreen* screen;             // +0x200
};

// CONNECT_CHECKBOX (vtable 0x005c6520): its press connects or disconnects.
class ConnectBox : public ztwod::CheckButton {
 public:
  explicit ConnectBox(ConnectScreen* s) : screen(s) {}
  void on_left_up() override;        // +0x30  0x004938f0
  ConnectScreen* screen;             // +0x164
};

// The three edit bars: the mouse over one gives it the keyboard (0x00495310).
class FocusBar : public ShellEditBar {
 public:
  FocusBar(Engine& e, ConnectScreen* s, int32_t which) : ShellEditBar(e), screen(s), which(which) {}
  void on_enter() override;          // +0x3c  0x004938c0 (1), 0x00493940 (2), 0x00493930 (3)
  void on_return() override;         // +0x90  CONNECT_INFO_EDIT's 0x00493960: FIND GAME
  ConnectScreen* screen;
  int32_t which;                     // 1 PLAYER_NAME, 2 CONNECT_INFO_EDIT, 3 NEW_SESSION_NAME
  uint8_t no_focus = 0;              // +0x38c  CONNECT_INFO_EDIT's: set while it searches
};

// Ours (piece 9c): one of the host's addresses shown under the host setup;
// a click copies it to the clipboard.
class CopyLine : public ztwod::Text {
 public:
  explicit CopyLine(Engine& e) : e_(e) {}
  ztwod::Rect* get_rect() override { return &fixed_rect; }
  void on_left_up() override;
  void show(const std::string& line, const std::string& to_copy);  // "" hides it
  std::string copy;                  // what the click copies, "" nothing
  std::string line;                  // the line last given (a copy's note shows over it until it changes)

 private:
  Engine& e_;
};

// Ours (piece 9c): the internet line's text for the platform's UPnP state,
// and the address a click copies ("" none); the lobby shows them too.
std::string reach_line(const Engine::NetReach& r);
std::string reach_copy(const Engine::NetReach& r);

// The screen (vtable 0x005c6168), 0x13b04 bytes.
class ConnectScreen : public MenuScreen {
 public:
  ConnectScreen(Engine& e, Handle state);  // 0x00493260
  ~ConnectScreen() override;         // 0x00493a70 (deleting 0x00493a50)
  void update(float dt) override;    // +0x00  0x00494470: the search asked again
  void on_button(int32_t id, int32_t a, int32_t b) override;  // +0x14  0x004953a0

  uint8_t connect(bool on);          // 0x004949f0: the chosen provider's connection, or none
  void focus(int32_t which);         // 0x00495310: 0 none
  void join();                       // 0x00495490
  void host();                       // 0x00495600
  void find();                       // 0x004957c0: FIND GAME, or Stop Search
  void chosen();                     // 0x00495410: a game chosen, JOIN for a joinable one
  void take_pilot(Handle pilot, const char* name);  // 0x00494920: its name, the lists, the reset
  void forget_kind();                // 0x004949d0
  // Ours (Owen): the one connection connected and the LAN searched when the
  // screen opens, no CONNECT or FIND needed.
  void search_lan();
  // Ours (piece 9c, Owen's "level 1": plain addresses, a copy button, the
  // forward line): the host's addresses under the host setup, from the
  // platform's UPnP, refreshed each half second.
  void show_reach();
  CopyLine internet_line;            // the public address, or what stands in its way
  CopyLine lan_line;                 // this computer's on the LAN
  float reach_timer = 0;

  SlideButton join_btn;              // +0x111ec  JOIN (2)
  ConnectionCycle connections;       // +0x11514  CONNECTION_LIST
  SessionList sessions;              // +0x1193c  SESSION_LIST (7)
  SlideButton cancel_btn;            // +0x11b40  CANCEL (1)
  SlideButton host_btn;              // +0x11d2c  HOST (3)
  SlideButton answer_btn;            // +0x11f18  ANSWER (4): the modem's, hidden (FIX (A))
  ShellButton dial_btn;              // +0x12104  DIAL (5): the modem's, hidden
  ShellButton find_btn;              // +0x1225c  FIND_GAME_BUTTON (6)
  ConnectBox connect_box;            // +0x123b4  CONNECT_CHECKBOX
  InvertCheck team_box;              // +0x1251c  TEAM_CHECKBOX
  ztwod::CheckButton call_box;       // +0x12688  CALL_BUTTON: the modem's, hidden
  FocusBar player_name;              // +0x127f0  PLAYER_NAME (16 characters)
  CycleButton max_players;           // +0x12b7c  MAX_PLAYERS_LIST
  FocusBar session_name;             // +0x12fa4  NEW_SESSION_NAME (25)
  FocusBar address;                  // +0x13330  CONNECT_INFO_EDIT (25)
  ShellButton info1;                 // +0x136c0  INFO_TEXT1
  ShellButton info2;                 // +0x13818  INFO_TEXT2
  ztwod::Image lines1{0};            // +0x1398c  LINES1
  ztwod::Image lines2{0};            // +0x13a48  LINES2
  Handle pilot = 0;                  // +0x13974
  float timer = 0;                   // +0x13978  to the search's next ask
  uint8_t team = 0;                  // +0x1397d
  uint8_t find_shows_find = 1;       // +0x1397e  FIND GAME (1) or Stop Search (0)
  std::unique_ptr<NetConnection> conn;  // +0x13980
  std::string name;                  // +0x13984  the player's, trimmed
  std::string game_name;             // +0x13988  the new game's, trimmed
  std::string searching_for;         // ours: the address being searched ("" the LAN)

 private:
  void set_info(int32_t first, int32_t second);  // the two lines (0x00495240 on each)
  bool constructed_ = false;         // [0x006099d8] set: the reset gives up the bars' focus
};

}  // namespace mech3
