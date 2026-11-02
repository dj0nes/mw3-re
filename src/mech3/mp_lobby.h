#pragma once
#include <cstdint>
#include <list>
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
// Enter sends, to our team with CHAT_TEAM_CHECKBOX in team play, else to all
// (0x00499880, piece 12).
class ChatBar : public ShellEditBar {
 public:
  ChatBar(Engine& e, LobbyScreen* s) : ShellEditBar(e), screen(s) {}
  void on_return() override;         // +0x90  0x00499880
  LobbyScreen* screen;               // +0x38c
};

class ChatLog;
// CHAT_UP and CHAT_DOWN (0x004968d0 vtable 0x005c7ba0, 0x00496910 vtable
// 0x005c7c30): a press scrolls the log a line.
class ChatScroll : public ShellButton {
 public:
  ChatScroll(Engine& e, ChatLog* l, bool up) : ShellButton(e, -1), log(l), up(up) {}
  void on_left_up() override;        // +0x30  0x004997c0 (up), 0x00499820 (down)
  ChatLog* log;                      // +0x158
  bool up;
};

// CHAT_REC_LIST (vtable 0x005c6e70): a text box of 14 lines over the chat's
// last 100 (a list of strings), the window between `top` and `bottom`.
class ChatLog : public ztwod::TextBox {
 public:
  ChatLog(Engine& e, LobbyScreen* s);  // the lobby's constructor's, 0x005a18f0(0xe) and the two buttons
  void clear_log();                  // 0x00499680: one line of three spaces
  void add(const char* line);        // 0x004996d0
  bool at_top() const;               // 0x004997a0
  bool at_bottom() const;            // 0x00499760
  void scroll_up();                  // 0x004997c0
  void scroll_down();                // 0x00499820

  ChatScroll up_btn;                 // +0x4c8  CHAT_UP
  ChatScroll down_btn;               // +0x624  CHAT_DOWN
  LobbyScreen* owner;                // +0x780
  int32_t visible = 0xe;             // +0x784
  std::list<std::string> history;      // +0x78c, its count +0x790 (at +0x788)
  int32_t max_lines = 100;           // +0x794
  std::list<std::string>::iterator top, bottom;  // +0x798, +0x79c
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
  // The rules (piece 11b): the host's to everyone (0x00499b10, message 0x20:
  // the flags 1 respawn, 2 mobile bases, 4 stock mechs, 8 unlimited ammo,
  // 0x10 first person, 0x20 locked, then the four lists' values as int16);
  // LOCK_OPTIONS (0x0049aca0); the lists' values and the boxes into the
  // game's rules block (inline twice in the exe); the lock's mech (0x0049b360).
  void send_rules();                 // 0x00499b10
  void lock();                       // 0x0049aca0
  void keep_rules(bool seeded);      // the game's +0x60 block, its CRC at +0x88
  void lock_mech();                  // 0x0049b360
  void world_changed();              // 0x00497410 case 0xc
  // The game's start and the players' own (piece 11c): START (0x0049b790,
  // the host's message 0x1e with the players' places, then everyone the
  // game's settings from the lobby and the Load state), READY (0x0049a970),
  // MECHLAB and back (0x0049b500, 0x0049b660); the teams' (JOIN 0x0049a550,
  // REMOVE 0x0049a630, a player into a team 0x0049a220, a new team
  // 0x0049a300) and the skin's (0x0049a850; the new team's dialog, piece 13,
  // calls these two).
  void start();                      // 0x0049b790
  void ready();                      // 0x0049a970
  void mech_lab();                   // 0x0049b500
  void resume();                     // 0x0049b660
  void join_team();                  // 0x0049a550
  void remove_team();                // 0x0049a630
  bool add_to_team(uint32_t team, uint32_t player);  // 0x0049a220
  void new_team(const char* name, int32_t skin, bool join);  // 0x0049a300
  void set_skin(int32_t skin);       // 0x0049a850
  void open_new_team();              // 0x0049a2b0: NEW_TEAM (7)
  void open_pick_skin();             // 0x0049a820: PICK_SKIN (8)
  int32_t free_skin(int32_t skin, int32_t except);  // 0x0049bd10: the team with that skin, 0 none
  bool has_world(uint32_t player, int32_t world);   // 0x0049ab30: an add-on world in its data
  // Ours (the mods check): a player's gameplay mods from its data (the
  // block after the add-on worlds; none: hash 0), and the text naming two
  // sides' mods ("" when they match).
  static bool mods_of(const std::vector<uint32_t>& data, uint64_t* hash, std::vector<std::string>* names);
  static std::string mods_text(const std::vector<std::string>& names);
  uint8_t mods_checked = 0;          // ours: the joiner compared its mods with the host's
  // Ours (piece 9c, the lead's plan for piece 11): the host's addresses, as
  // on the connection screen, while it waits for players.
  void show_reach();

  // Message handlers (the exe's fastcall functions, on [0x006099d4]).
  static void on_change(uint32_t from, const uint8_t* msg, size_t size);  // 0x00499600: 2..6, 8..0xa
  static void on_kicked(uint32_t from, const uint8_t* msg, size_t size);  // 0x0049a1d0: 7
  static void on_host(uint32_t from, const uint8_t* msg, size_t size);    // 0x0049a200: 0xd
  static void on_rules(uint32_t from, const uint8_t* msg, size_t size);   // 0x00499cf0: 0x20
  // The chat (piece 12): ours to `team` (0 all) as 0x1f, unguaranteed, and
  // shown here (0x004998b0); a player's shown, "< name to all >" or "< name
  // to team >" over the text (0x00499610).
  void chat_send(uint32_t team);     // 0x004998b0 (0x004999f0 all, 0x00499a00 our team)
  static void on_chat(uint32_t from, const uint8_t* msg, size_t size);    // 0x00499610: 0x1f
  static void on_start(uint32_t from, const uint8_t* msg, size_t size);   // 0x0049bcb0: 0x1e
  static void on_scores(uint32_t from, const uint8_t* msg, size_t size);  // 0x0049b150: 0x14

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
  ChatLog chat_log;                  // +0x13dd8  CHAT_REC_LIST, its CHAT_UP (+0x142a0) and CHAT_DOWN (+0x143fc)
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
  int32_t saved_state = 0;           // +0x15054  our data's state while in the mech lab
  uint8_t team_new = 0;              // +0x111ec  the team dialog: a new team (its name)
  uint8_t team_skin = 0;             // +0x111ed  ... its skin chosen
  uint8_t team_join = 0;             // +0x111ee  ... JOIN_TEAM_CHECKBOX shown and checked
  Handle pilot = 0;                  // +0x15058
  CopyLine internet_line, lan_line;  // ours (piece 9c)
  float reach_timer = 0;
  std::vector<std::string> shown_scores;  // ours: SCORES_LIST's lines as last filled (the tests)

 private:
  bool handlers_on_ = false;
};

// MULTIPLAYER_NEW_TEAM_DIALOG (0x0049bd80, vtable 0x005c7cd0), 0x11f3c
// bytes, the Multiplayer State's third page, over a still of the lobby: a
// new team's name and skin (and whether we join it), or our skin alone, by
// the lobby's three flags (piece 13).
class TeamScreen;
// NEW_TEAM_NAME_EDIT (vtable 0x005c7e30, 0x19 characters, a file name's
// rejects): Enter is OK (0x0049bfe0).
class TeamNameBar : public ShellEditBar {
 public:
  TeamNameBar(Engine& e, TeamScreen* s) : ShellEditBar(e), screen(s) {}
  void on_enter() override {}        // +0x3c  0x0049bfd0
  void on_leave() override {}        // +0x40  0x0049bfd0
  void on_return() override;         // +0x90  0x0049bfe0
  TeamScreen* screen;                // +0x38c
};
class TeamScreen : public MenuScreen {
 public:
  TeamScreen(Engine& e, Handle state);  // 0x0049bd80
  ~TeamScreen() override;            // 0x0049c170 (deleting 0x0049c150)
  void on_button(int32_t id, int32_t a, int32_t b) override;  // +0x14  0x0049c550
  void enter(LobbyScreen* lobby);    // 0x0049c2c0
  void ok();                         // 0x0049c6c0
  void back();                       // 0x0049c680
  void show_skin();                  // the SKIN_TEXTURES image of SKIN_LIST's skin ("popup_box%d")

  ShellButton cancel_btn;            // +0x111ec  CANCEL_BUTTON (1)
  ShellButton ok_btn;                // +0x11344  OK_BUTTON (0)
  TeamNameBar name_edit;             // +0x1149c  NEW_TEAM_NAME_EDIT
  InvertCheck join_box;              // +0x1182c  JOIN_TEAM_CHECKBOX
  CycleButton skins;                 // +0x11998  SKIN_LIST (2)
  ztwod::Image skin_image{0};        // +0x11dbc  SKIN_TEXTURES
  ztwod::Image backdrop{0};          // +0x11e78  the screen under it (0x00579500(2))
  zimg::Texture* still_tex = nullptr;  // +0x11f34
  LobbyScreen* lobby = nullptr;      // +0x11f38
};

// [0x006099d4]: the lobby, its handlers' screen; [0x006099cc] its lists'
// refresh asked, [0x006099c8] the way back asked (after a kick).
LobbyScreen* lobby_screen();
extern uint8_t g_lobby_dirty;
extern uint8_t g_lobby_back;

}  // namespace mech3
