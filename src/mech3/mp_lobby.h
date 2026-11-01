#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "mech3/controls_menu.h"
#include "mech3/menu_widgets.h"
#include "mech3/mp_connect.h"

// The multiplayer lobby (docs/multiplayer-port.md, piece 11):
// MULTIPLAYER_HOST_DIALOG of the main menu's dialog.zrd, the Multiplayer
// State's second screen. The players and teams, the host's rules, ready and
// launch, the scores between games. Original file unknown: 0x00495a50..
// 0x0049bd80, between the connection screen and the new team's dialog;
// its records' lists are the multiplayer game's (pilot.h).
namespace mech3 {

class MultiplayerGame;
struct LobbyEntry;
class LobbyScreen;

// DATABASE_LIST (vtable 0x005c7788): the worlds; a step passes over
// "UNAVAILABLE" (the first) when there is more.
class WorldCycle : public CycleButton {
 public:
  explicit WorldCycle(Engine& e) : CycleButton(e, 0xc) {}
  void step(int32_t by) override;    // +0xa4  0x00496880
};

// TP_LIST (0x16) and PLAYERS_LIST (0x17): the teams with their players, and
// the players without one. Each item's value is its record (the exe's
// pointer; an index into `records` here).
class LobbyList : public FileList {
 public:
  LobbyList(Engine& e, int32_t id) : FileList(e, id) {}
  void empty();                      // the items freed (0x0044ed90 each) and the rows laid out
  void add(const std::string& text, LobbyEntry* e);  // 0x00499010 and the push (0x0049ccc0 / 0x0049ced0)
  LobbyEntry* chosen();              // the selected item's record, null none
  std::vector<LobbyEntry*> records;
};

// The chat's edit bar, CHAT_ENTER_EDIT (vtable 0x005c7a70, 0x27 characters):
// Enter sends (0x00499880, piece 12).
class ChatBar : public ShellEditBar {
 public:
  ChatBar(Engine& e, LobbyScreen* s) : ShellEditBar(e), screen(s) {}
  LobbyScreen* screen;               // +0x38c
};

// The screen (vtable 0x005c6aa0), 0x1505c bytes.
class LobbyScreen : public MenuScreen {
 public:
  LobbyScreen(Engine& e, Handle state);  // 0x00495f90
  ~LobbyScreen() override;           // 0x00496b90 (deleting 0x00496b70)
  void update(float dt) override;    // +0x00  0x004984e0
  void on_button(int32_t id, int32_t a, int32_t b) override;  // +0x14  0x00497410

  void enter();                      // 0x00497720: the screen made ready for a game
  void take_pilot(Handle pilot, MultiplayerGame* game);  // 0x0049cc30's two stores
  void scores(bool report);          // 0x00498550: SCORES_LIST
  void refresh_lists();              // 0x00498710: the records from the network, TP_LIST and PLAYERS_LIST
  void refresh_buttons();            // 0x004993f0: READY, MECHLAB, START, NEW_TEAM as things stand
  void host_controls();              // 0x00498260: the rules' controls the host's to change
  void handlers(bool on);            // 0x00497fbf's registrations, 0x00499a30's removals
  void kick(LobbyList& from);        // 0x0049a150 (TP_LIST), 0x0049a7a0 (PLAYERS_LIST)
  void team_chosen();                // 0x0049a3a0: TP_LIST's choice
  void player_chosen();              // 0x0049a6a0: PLAYERS_LIST's
  int32_t free_skin(int32_t skin, int32_t except);  // 0x0049bd10: the team with that skin, 0 none
  bool has_world(uint32_t player, int32_t world);   // 0x0049ab30: an add-on world in its data
  // Ours (piece 9c, the lead's plan for piece 11): the host's addresses, as
  // on the connection screen, while it waits for players.
  void show_reach();

  // Message handlers (the exe's fastcall functions, on [0x006099d4]).
  static void on_change(uint32_t from, const uint8_t* msg, size_t size);  // 0x00499600: 2..6, 8..0xa
  static void on_kicked(uint32_t from, const uint8_t* msg, size_t size);  // 0x0049a1d0: 7
  static void on_host(uint32_t from, const uint8_t* msg, size_t size);    // 0x0049a200: 0xd

  InvertCheck chat_team;             // +0x111f0  CHAT_TEAM_CHECKBOX
  ChatBar chat_edit;                 // +0x1135c  CHAT_ENTER_EDIT
  MultiplayerGame* game = nullptr;   // +0x116ec  the pilot's
  HelpButton cancel_btn;             // +0x1182c  CANCEL_BUTTON (1)
  HelpButton start_btn;              // +0x119a8  START_BUTTON (0)
  HelpButton mechlab_btn;            // +0x11b24  MECHLAB_BUTTON (10)
  HelpButton lock_btn;               // +0x11ca0  LOCK_OPTIONS_BUTTON (0xb)
  WorldCycle worlds;                 // +0x11e1c  DATABASE_LIST (0xc)
  CycleButton kills_list;            // +0x12240  KILLS_LIST (0xd)
  CycleButton time_list;             // +0x12664  TIME_LIST (0xe)
  CycleButton weight_list;           // +0x12a88  WEIGHT_LIST (0xf)
  InvertCheck regen_box;             // +0x12eac  REGEN_CHECKBOX (0x10)
  InvertCheck mobile_box;            // +0x13018  MOBILE_CHECKBOX (0x11)
  InvertCheck stock_box;             // +0x13184  STOCK_CHECKBOX (0x12)
  InvertCheck ammo_box;              // +0x132f0  AMMO_CHECKBOX (0x13)
  InvertCheck select_box;            // +0x1345c  SELECT_CHECKBOX (0x14)
  ShellButton join_team_btn;         // +0x135c8  JOIN_TEAM_BUTTON (4)
  ShellButton remove_team_btn;       // +0x13720  REMOVE_TEAM_BUTTON (5)
  ShellButton kick_btn;              // +0x13878  KICK_PLAYER_BUTTON (6)
  ShellButton new_team_btn;          // +0x139d0  NEW_TEAM_BUTTON (7)
  ShellButton pick_skin_btn;         // +0x13b28  PICK_SKIN_BUTTON (8)
  ShellButton kick2_btn;             // +0x13c80  KICK_PLAYER2_BUTTON (9)
  ztwod::TextBox chat_log{0xe};      // +0x13dd8  CHAT_REC_LIST (piece 12)
  ShellButton chat_up;               // +0x142a0  CHAT_UP (piece 12)
  ShellButton chat_down;             // +0x143fc  CHAT_DOWN
  FileList scores_list;              // +0x14578  SCORES_LIST
  LobbyList teams_list;              // +0x14778  TP_LIST (0x16)
  LobbyList players_list;            // +0x14978  PLAYERS_LIST (0x17)
  ShellButton player_name;           // +0x14b78  PLAYER_NAME: the label
  ShellButton session_name;          // +0x14cd0  SESSION_NAME
  InvertCheck ready_box;             // +0x14e28  READY_CHECKBOX (0x15)
  ztwod::Image lines1{0};            // +0x14f94  LINES1
  uint8_t locked = 0;                // +0x15050  the options locked (LOCK_OPTIONS): READY then
  uint8_t all_ready = 0;             // +0x15051  every player ready (START)
  uint8_t start_pending = 0;         // +0x15052  START asked; the update starts (piece 11c)
  uint8_t keep_order = 0;            // +0x15053  a refresh keeps the players' places
  Handle pilot = 0;                  // +0x15058
  CopyLine internet_line, lan_line;  // ours (piece 9c)
  float reach_timer = 0;

 private:
  bool handlers_on_ = false;
};

// [0x006099d4]: the lobby, its handlers' screen; [0x006099cc] its lists'
// refresh asked, [0x006099c8] the way back asked (after a kick).
LobbyScreen* lobby_screen();
extern uint8_t g_lobby_dirty;
extern uint8_t g_lobby_back;

}  // namespace mech3
