#pragma once
#include <cstddef>
#include <cstdint>

#include "gamez/ztwod/twod_console.h"

// The network game's chat (docs/multiplayer-port.md, piece 14): the panel
// the chat keys open on the pilot's mech (CHAT_ALL / CHAT_TEAM, commands
// 0x76 / 0x77), zTwoD's console with its own Enter; the message sent (0x1f)
// and every chat line shown on the HUD for 5 s. Original file:
// mech3\mech.cpp (0x0041e4e0..0x0041e660) and mech3\mech_net.cpp
// (0x00470380, 0x00470490).
namespace mech3 {

struct Mech;
struct NetMech;

// The panel (0x0041e5a0, vtable 0x005bd070), 0x720 bytes: "CHAT ALL" or
// "CHAT TEAM" over the prompt, 0x29 characters.
class ChatPanel : public ztwod::Console {
 public:
  ChatPanel(bool to_all, Mech* mech);  // 0x0041e5a0
  void line_entered() override;        // +0xd0  0x0041e660: sent (none when empty), then closed
  uint8_t to_all;                      // +0x718
  Mech* mech;                          // +0x71c

 protected:
  ChatPanel(const char* title, Mech* mech);  // ours: the cheat box's (mech3/cheats)
};

void mech_chat_open(Mech& m, bool to_all);  // 0x0041e4e0: in a network game; to all without team play
void mech_chat_close(Mech& m);              // 0x0041e580
void mech_chat_draw(Mech& m);               // 0x0041ab90: the HUD frame's
void net_chat_send(const char* text, bool to_all);  // 0x00470380
// 0x00470490, 0x1f's handler in the game: "<name  Send message to all> text"
// (0x899 to the team) on the HUD for 5 s. FIX (A): read only as far as the
// message goes, control characters as '?', the text never a format.
void net_chat_in(NetMech* m, uint32_t from, const uint8_t* msg, size_t size);

}  // namespace mech3
