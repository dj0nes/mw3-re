#include "mech3/chat_panel.h"

#include <cstring>
#include <string>
#include <vector>

#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "gamez/zutil/zutl_msgdll.h"
#include "mech3/mech.h"
#include "mech3/mech_net.h"

namespace mech3 {

bool multiplayer_team_play();  // [0x00608e2c] (mech3/pilot.h)

namespace {
void printable(std::string& s) {
  for (char& c : s)
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) c = '?';
}
}  // namespace

// Mech3.exe 1.2 @ 0x0041e5a0
ChatPanel::ChatPanel(bool all, Mech* m) : to_all(all ? 1 : 0), mech(m) {
  set_text("%s", zutl::format_message(all ? 0x11e : 0x11f, nullptr).c_str());  // "CHAT ALL", "CHAT TEAM"
  if (editor) editor->resize(0x29);
}

ChatPanel::ChatPanel(const char* title, Mech* m) : to_all(1), mech(m) {
  set_text("%s", title);
  if (editor) editor->resize(0x29);
}

// Mech3.exe 1.2 @ 0x0041e660: the text after the prompt's '>', trimmed; a
// new line; sent unless empty (and kept, " %s"); the panel closed.
void ChatPanel::line_entered() {
  std::string t = line_text();
  const size_t gt = t.find('>');
  t = gt == std::string::npos ? t : t.substr(gt + 1);
  const size_t a = t.find_first_not_of(" \t\r\n");
  t = a == std::string::npos ? "" : t.substr(a, t.find_last_not_of(" \t\r\n") - a + 1);
  new_line();
  if (!t.empty()) {
    std::snprintf(kept, sizeof kept, " %s", t.c_str());  // [0x005e2370]
    net_chat_send(t.c_str(), to_all != 0);
  }
  if (mech) mech_chat_close(*mech);  // deletes this
}

// Mech3.exe 1.2 @ 0x0041e4e0
void mech_chat_open(Mech& m, bool to_all) {
  const NetCalls& c = net_calls();
  if (!(c.game && c.game())) return;  // 0x00479480: a network game's
  delete m.chat;
  m.chat = nullptr;
  m.chat = new ChatPanel(to_all || !multiplayer_team_play(), &m);
}

// Mech3.exe 1.2 @ 0x0041e580
void mech_chat_close(Mech& m) {
  ChatPanel* p = m.chat;
  m.chat = nullptr;
  delete p;
}

// Mech3.exe 1.2 @ 0x0041ab90 (0x005a4410)
void mech_chat_draw(Mech& m) {
  if (m.chat) m.chat->draw();
}

// Mech3.exe 1.2 @ 0x00470380: 0x1f {flags (1 to the team), the text and its
// 0}, its size the length and 0xc, unguaranteed, to our team or to all;
// shown here as the others see it.
void net_chat_send(const char* text, bool to_all) {
  const std::string t = text ? text : "";
  const uint32_t size = static_cast<uint32_t>(t.size()) + 0xc;
  std::vector<uint8_t> m(size, 0);
  m[0] = 0x1f, m[2] = static_cast<uint8_t>(size), m[3] = static_cast<uint8_t>(size >> 8);
  std::memcpy(m.data() + 8, t.c_str(), t.size() + 1);
  uint32_t to = 0;
  if (!to_all) {
    m[4] |= 1;
    const NetCalls& c = net_calls();
    to = c.lobby_team_id ? c.lobby_team_id(znet::local_id()) : 0;  // 0x00495b60
  }
  znet::send(m.data(), size, false, to);
  net_chat_in(nullptr, znet::local_id(), m.data(), m.size());
}

// Mech3.exe 1.2 @ 0x00470490
void net_chat_in(NetMech*, uint32_t from, const uint8_t* msg, size_t size) {
  if (size < 9) return;
  const char* who = znet::player_name(from);
  std::string line = std::string("<") + (who ? who : "") + " " +
                     zutl::format_message((msg[4] & 1) ? 0x899 : 0x89a, nullptr) + "> ";
  const char* text = reinterpret_cast<const char*>(msg + 8);
  std::string body(text, strnlen(text, size - 8));
  printable(body);
  line += body;
  const NetCalls& c = net_calls();
  if (c.hud_line) c.hud_line(5.0f, line.c_str());  // 0x0040fe20
}

}  // namespace mech3
