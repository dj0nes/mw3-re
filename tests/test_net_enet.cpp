// The ENet transport in one process (docs/multiplayer-port.md, piece 4):
// three machines on 127.0.0.1, each on a port the system picks (parallel
// runs never meet; loopback only, no firewall prompt), pumped in turn.
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <functional>
#include <string>
#include <vector>

// enet.h brings in <windows.h> on Windows: without NOMINMAX its max macro
// breaks std::max (as net_enet.cpp).
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <enet/enet.h>

#include "check.h"
#include "frontend/sdl/net_enet.h"

namespace {
using sdlfe::EnetTransport;
using sdlfe::NetOptions;

NetOptions loopback() {
  NetOptions o;
  o.bind = "127.0.0.1";
  o.discovery_port = 0;  // a port the system picks: parallel runs never meet
  return o;
}
std::string at(const EnetTransport& t) { return "127.0.0.1:" + std::to_string(t.port()); }

// Each machine's packets as they come, kept by machine.
struct Machine {
  std::unique_ptr<EnetTransport> t;
  std::vector<znet::Packet> got;
  explicit Machine(NetOptions o = loopback()) : t(std::make_unique<EnetTransport>(o)) {}
  void drain() {
    znet::Packet p;
    while (t->receive(&p)) got.push_back(std::move(p));
  }
  bool has_event(uint32_t kind, uint32_t id) const {
    for (const znet::Packet& p : got)
      if (p.from == 0 && p.sys.kind == kind && p.sys.id == id) return true;
    return false;
  }
  int messages_from(uint32_t from, uint8_t type) const {
    int n = 0;
    for (const znet::Packet& p : got)
      if (p.from == from && !p.data.empty() && p.data[0] == type) ++n;
    return n;
  }
};
// Pumps every machine until `done` or 3 s.
bool pump(std::vector<Machine*> ms, const std::function<bool()>& done) {
  for (int i = 0; i < 300; ++i) {
    for (Machine* m : ms) m->t->service(1), m->drain();
    if (done()) return true;
  }
  return false;
}
}  // namespace

TEST(net_enet_three_machines_join_talk_and_part) {
  Machine a;
  NetOptions o = loopback();
  o.while_waiting = [&] { a.t->service(1), a.drain(); };  // the host runs while the others wait on it
  Machine b(o), c(o);
  EXPECT(a.t->ok() && b.t->ok() && c.t->ok() && a.t->port() != 0 && a.t->port() != b.t->port());
  znet::SessionDesc d;
  d.name = "Loop";
  d.user[2] = 77;
  EXPECT(a.t->host(d) && a.t->create_player("Ann", "Ann the host") == 1 && a.t->is_host(1));
  // b joins: the session's description from the host, then its player.
  EXPECT(b.t->join(at(*a.t)));
  EXPECT(b.t->session_desc().name == "Loop" && b.t->session_desc().user[2] == 77);
  std::vector<znet::Transport::Member> ps;
  EXPECT(b.t->players(&ps) && ps.size() == 1 && ps[0].id == 1 && ps[0].short_name == "Ann");  // enumerated, before its player
  EXPECT(b.t->create_player("Bob", "Bob") == 2 && b.t->is_host(1) && !b.t->is_host(2));
  EXPECT(pump({&a, &b}, [&] { return a.has_event(znet::kSysCreatePlayerOrGroup, 2); }));
  // c joins: the others hear of it, and it links to b directly (the mesh).
  EXPECT(c.t->join(at(*a.t)) && c.t->create_player("Cy", "Cy") == 3);
  EXPECT(pump({&a, &b, &c}, [&] {
    return a.has_event(znet::kSysCreatePlayerOrGroup, 3) && b.has_event(znet::kSysCreatePlayerOrGroup, 3);
  }));
  const uint8_t hello[8] = {0x1f, 0, 8, 0, 'h', 'i', 0, 0};
  EXPECT(c.t->send(2, hello, 8, true, nullptr));  // to b alone
  const uint8_t all[4] = {0x14, 0, 4, 0};
  uint32_t id = 0;
  EXPECT(b.t->send(0, all, 4, true, &id) && id != 0);  // to everyone; the send done at once
  EXPECT(pump({&a, &b, &c}, [&] {
    return b.messages_from(3, 0x1f) == 1 && a.messages_from(2, 0x14) == 1 && c.messages_from(2, 0x14) == 1;
  }));
  EXPECT(b.has_event(znet::kSysSendComplete, 2) && a.messages_from(3, 0x1f) == 0);  // only b got c's
  // A team, made by b: the others hear of it; c joins it; data set on c.
  const uint32_t team = b.t->create_group("Red");
  EXPECT(team != 0 && b.t->add_to_group(team, 3));
  const uint8_t rec[3] = {7, 8, 9};
  EXPECT(c.t->set_data(false, 3, rec, 3));
  EXPECT(pump({&a, &b, &c}, [&] {
    return a.has_event(znet::kSysCreatePlayerOrGroup, team) && c.has_event(znet::kSysAddPlayerToGroup, 3) &&
           a.has_event(znet::kSysSetPlayerOrGroupData, 3);
  }));
  std::vector<uint32_t> members;
  EXPECT(a.t->group_players(team, &members) && members.size() == 1 && members[0] == 3);
  uint8_t got[4] = {};
  uint32_t size = 4;
  EXPECT(a.t->get_data(false, 3, got, &size) && size == 3 && got[2] == 9);
  EXPECT(c.has_event(znet::kSysSetPlayerOrGroupData, 3));  // told of its own too (the lobby's READY, piece 11c)
  // Team chat: to the team, only its members (c) get it.
  const uint8_t team_chat[4] = {0x1f, 0, 4, 0};
  EXPECT(a.t->send(team, team_chat, 4, true, nullptr));
  EXPECT(pump({&a, &b, &c}, [&] { return c.messages_from(1, 0x1f) == 1; }));
  EXPECT(b.messages_from(1, 0x1f) == 0);
  // The host kicks c: c hears its own end, a and b its going.
  EXPECT(a.t->destroy_player(3));
  EXPECT(pump({&a, &b, &c}, [&] {
    return c.has_event(znet::kSysDestroyPlayerOrGroup, 3) && a.has_event(znet::kSysDestroyPlayerOrGroup, 3) &&
           b.has_event(znet::kSysDestroyPlayerOrGroup, 3);
  }));
  // The host leaves: b, the lowest left, is the host now (DPSYS_HOST).
  a.t->close();
  EXPECT(pump({&a, &b}, [&] { return b.has_event(znet::kSysDestroyPlayerOrGroup, 1); }));
  bool told_host = false;
  for (const znet::Packet& p : b.got) told_host |= p.from == 0 && p.sys.kind == znet::kSysHost;
  EXPECT(told_host && b.t->is_host(2));
}

// Two joining at once (the soak test's find): both welcomed before either
// has its player, so the second's welcome lists no first. The host's answer
// to its player (kId) lists the players made since: it links to the first
// and hears of it, the first hears of it, and they talk directly.
// The host's session description (pieces 11a, 11b): its change reaches the
// players (DPSYS_SETSESSIONDESC) and is the host's alone; a locked game (its
// fourth word 3) turns a joiner away at the door.
TEST(net_enet_session_desc_and_the_locked_door) {
  Machine a;
  NetOptions o = loopback();
  o.while_waiting = [&] { a.t->service(1), a.drain(); };
  Machine b(o), c(o);
  znet::SessionDesc d;
  d.name = "Door", d.max_players = 8;
  EXPECT(a.t->host(d) && a.t->create_player("Ann", "Ann") == 1);
  EXPECT(b.t->join(at(*a.t)) && b.t->create_player("Bob", "Bob") == 2);
  d.user[3] = 1;
  EXPECT(a.t->set_session_desc(d) && !b.t->set_session_desc(d));
  EXPECT(pump({&a, &b}, [&] { return b.has_event(znet::kSysSetSessionDesc, 0); }));
  EXPECT(b.t->session_desc().user[3] == 1 && b.t->session_desc().name == "Door");
  d.user[3] = 3;
  EXPECT(a.t->set_session_desc(d));
  EXPECT(pump({&a, &b}, [&] { return b.t->session_desc().user[3] == 3; }));
  EXPECT(!c.t->join(at(*a.t)));  // locked
  d.user[3] = 1;
  EXPECT(a.t->set_session_desc(d));
  EXPECT(c.t->join(at(*a.t)) && c.t->create_player("Cy", "Cy") == 3);
}

TEST(net_enet_two_joining_at_once_meet) {
  Machine a;
  NetOptions o = loopback();
  o.while_waiting = [&] { a.t->service(1), a.drain(); };
  Machine b(o), c(o);
  znet::SessionDesc d;
  d.name = "Race";
  EXPECT(a.t->host(d) && a.t->create_player("Ann", "Ann") == 1);
  EXPECT(b.t->join(at(*a.t)) && c.t->join(at(*a.t)));  // both welcomed with Ann alone
  EXPECT(b.t->create_player("Bob", "Bob") == 2 && c.t->create_player("Cy", "Cy") == 3);
  EXPECT(pump({&a, &b, &c}, [&] {
    return b.has_event(znet::kSysCreatePlayerOrGroup, 3) && c.has_event(znet::kSysCreatePlayerOrGroup, 2);
  }));
  std::vector<znet::Transport::Member> ps;
  EXPECT(c.t->players(&ps) && ps.size() == 3);
  const uint8_t hi[4] = {0x1f, 0, 4, 0};
  EXPECT(c.t->send(2, hi, 4, true, nullptr) && b.t->send(3, hi, 4, true, nullptr));
  EXPECT(pump({&a, &b, &c}, [&] { return b.messages_from(3, 0x1f) == 1 && c.messages_from(2, 0x1f) == 1; }));
}

TEST(net_enet_lan_discovery_and_the_shim) {
  Machine a;
  znet::SessionDesc d;
  d.name = "Found me";
  d.max_players = 6;
  EXPECT(a.t->host(d) && a.t->create_player("Ann", "Ann") == 1);
  sdlfe::DiscoveryAnswer answer(0, "127.0.0.1");  // a port the system picks
  EXPECT(answer.ok() && answer.port() != 0);
  // The browser asks it directly (no broadcast in a test); the host answers
  // as it polls, here on a thread while the browser waits.
  std::atomic<bool> stop{false};
  std::thread host_side([&] {
    while (!stop) {
      answer.poll(d, 1, a.t->port());
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });
  const std::vector<sdlfe::FoundSession> found =
      sdlfe::browse(500, answer.port(), {"127.0.0.1:" + std::to_string(answer.port())});
  stop = true;
  host_side.join();
  EXPECT(found.size() == 1 && found[0].name == "Found me" && found[0].players == 1 && found[0].max_players == 6 &&
         found[0].address == at(*a.t));
  // The shim: b drops every unreliable message, the guaranteed ones still come.
  NetOptions lossy = loopback();
  lossy.loss = 1.0;
  lossy.while_waiting = [&] { a.t->service(1), a.drain(); };
  Machine b(lossy);
  EXPECT(b.t->join(at(*a.t)) && b.t->create_player("Bob", "Bob") == 2);
  EXPECT(pump({&a, &b}, [&] { return a.has_event(znet::kSysCreatePlayerOrGroup, 2); }));
  const uint8_t state[4] = {0x0f, 0, 4, 0}, chat[4] = {0x1f, 0, 4, 0};
  for (int i = 0; i < 5; ++i) EXPECT(a.t->send(2, state, 4, false, nullptr));
  EXPECT(a.t->send(2, chat, 4, true, nullptr));
  EXPECT(pump({&a, &b}, [&] { return b.messages_from(1, 0x1f) == 1; }));
  EXPECT(b.messages_from(1, 0x0f) == 0);
}

// The game list's search (multiplayer-port.md, piece 9a): a host found on
// the LAN (the probe to its discovery port; here to loopback, as a socket
// bound there hears no broadcast, and to the port it bound) and
// by its address (a probe to its game port, answered before ENet reads
// it), with its name, players, limit and words; a silent address finds
// nothing, and the search never blocks.
TEST(net_enet_search_finds_a_host_on_the_lan_and_by_address) {
  Machine a;
  znet::SessionDesc d;
  d.name = "Lan game";
  d.max_players = 6;
  d.user[0] = 1, d.user[1] = 0x1234;
  EXPECT(a.t->host(d) && a.t->create_player("Ann", "Ann") == 1 && a.t->discovery_port() != 0);
  Machine b;
  b.t->search_port = a.t->discovery_port();
  b.t->lan_address = "127.0.0.1";  // the broadcast's place: a socket bound to loopback hears no broadcast
  std::vector<znet::FoundGame> lan, direct, silent;
  for (int i = 0; i < 200 && lan.empty(); ++i) {
    EXPECT(b.t->find_sessions("", &lan) == znet::kOk);
    a.t->service(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT(lan.size() == 1 && lan[0].desc.name == "Lan game" && lan[0].players == 1 && lan[0].desc.max_players == 6 &&
         lan[0].desc.user[1] == 0x1234 && lan[0].address == at(*a.t));
  for (int i = 0; i < 200 && direct.empty(); ++i) {
    EXPECT(b.t->find_sessions(at(*a.t), &direct) == znet::kOk);
    a.t->service(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  EXPECT(direct.size() == 1 && direct[0].desc.name == "Lan game" && direct[0].address == at(*a.t));
  Machine quiet;  // a port with nothing hosting on it
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 30; ++i) {
    EXPECT(b.t->find_sessions(at(*quiet.t), &silent) == znet::kOk);
    quiet.t->service(1);
  }
  EXPECT(silent.empty());
  EXPECT(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2));
  EXPECT(b.t->find_sessions("no such host:x", &silent) != znet::kOk);
  b.t->stop_search();
}

// A search's answers are a peer's packets, untrusted (piece 8c's rule): a
// fake host answers each probe with broken answers and one good one; only
// the good one is kept, its name cut at its NUL and its control characters
// shown as '?'.
TEST(net_enet_search_drops_broken_answers) {
  EXPECT(enet_initialize() == 0);
  const ENetSocket fake = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  EXPECT(fake != ENET_SOCKET_NULL);
  enet_socket_set_option(fake, ENET_SOCKOPT_NONBLOCK, 1);
  ENetAddress a{};
  enet_address_set_host(&a, "127.0.0.1");
  a.port = 0;
  EXPECT(enet_socket_bind(fake, &a) == 0);
  enet_socket_get_address(fake, &a);
  auto frame = [](uint32_t protocol, uint16_t port, const std::string& name, uint32_t players, uint32_t max, size_t cut) {
    std::vector<uint8_t> b = {'M', 'W', '3', 'R', '!'};
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); };
    u32(protocol);
    b.push_back(static_cast<uint8_t>(port)), b.push_back(static_cast<uint8_t>(port >> 8));
    u32(static_cast<uint32_t>(name.size()));
    b.insert(b.end(), name.begin(), name.end());
    u32(players), u32(max);
    for (int i = 0; i < 4; ++i) u32(7);
    if (cut) b.resize(b.size() - cut);
    return b;
  };
  const std::string evil = std::string("Good\x01name", 9) + std::string(1, '\0') + std::string(300, 'X');
  const std::vector<std::vector<uint8_t>> answers = {
      frame(2, 40001, "Too many", 9, 8, 0),          // more players than its limit
      frame(2, 40002, "No limit", 0, 0, 0),          // a limit of 0
      frame(2, 40003, "Huge limit", 1, 1000, 0),     // past 64
      frame(1, 40004, "Old protocol", 1, 8, 0),      // another protocol
      frame(2, 40005, "Cut short", 1, 8, 6),         // truncated
      frame(2, 0, "No port", 1, 8, 0),               // port 0
      frame(2, 40007, evil, 2, 8, 0),                // the good one
  };
  Machine b;
  std::vector<znet::FoundGame> got;
  const std::string where = "127.0.0.1:" + std::to_string(a.port);
  for (int i = 0; i < 200; ++i) {
    b.t->find_sessions(where, &got);
    uint8_t buf[256];
    ENetBuffer in{};
    in.data = buf, in.dataLength = sizeof buf;
    ENetAddress from{};
    while (enet_socket_receive(fake, &from, &in, 1) > 0)
      for (const std::vector<uint8_t>& f : answers) {
        ENetBuffer out{};
        out.data = const_cast<uint8_t*>(f.data()), out.dataLength = f.size();
        enet_socket_send(fake, &from, &out, 1);
      }
    if (!got.empty()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  for (int i = 0; i < 5; ++i) b.t->find_sessions(where, &got), std::this_thread::sleep_for(std::chrono::milliseconds(5));
  EXPECT(got.size() == 1);
  if (!got.empty()) {
    EXPECT(got[0].desc.name == "Good?name" && got[0].players == 2 && got[0].desc.max_players == 8);
    EXPECT(got[0].address == "127.0.0.1:40007");
  }
  b.t->stop_search();
  enet_socket_destroy(fake);
}

// zNetwork's side (0x00586ff0, 0x00586ea0, 0x00586fb0): the search through
// the connection's address, the game list's entry, and the join by its
// index, then the local player made.
TEST(net_enet_znet_finds_and_joins_a_game) {
  Machine a;
  znet::SessionDesc d;
  d.name = "Zgame";
  d.max_players = 4;
  d.user[1] = 77;
  EXPECT(a.t->host(d) && a.t->create_player("Ann", "Ann") == 1);
  NetOptions o = loopback();
  o.while_waiting = [&] { a.t->service(1), a.drain(); };
  Machine b(o);
  znet::session() = znet::Session{};
  znet::initialise(b.t.get());
  znet::session().address = at(*a.t);
  uint32_t count = 0;
  for (int i = 0; i < 200 && count == 0; ++i) {
    EXPECT(znet::find_games(&count) == znet::kOk);
    a.t->service(1), a.drain();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  znet::GameInfo g;
  EXPECT(count == 1 && znet::game_info(0, &g) && g.name == "Zgame" && g.max_players == 4 && g.players == 1 &&
         g.user[1] == 77);
  EXPECT(!znet::game_info(1, &g) && !znet::game_info(-1, &g));  // no second entry, no session yet
  g.index = 0;
  EXPECT(znet::open_join(g) == znet::kOk && znet::create_player("Bob") == 2);
  znet::GameInfo now;
  EXPECT(znet::game_info(-1, &now) && now.name == "Zgame");
  znet::shutdown();
}

// The host answers probes from anywhere, so it must not amplify (the lead's
// review of 9a): a probe shorter than 128 bytes gets no answer, on the game
// port or the LAN's; a full one does, no larger than it; a burst from one
// address gets four answers a second, then the next second more.
TEST(net_enet_probes_are_padded_and_limited) {
  Machine a;
  znet::SessionDesc d;
  d.name = std::string(80, 'N');  // the longest name an answer carries
  EXPECT(a.t->host(d) && a.t->create_player("Ann", "Ann") == 1);
  EXPECT(enet_initialize() == 0);
  const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  ENetAddress game{}, lan{};
  enet_address_set_host(&game, "127.0.0.1"), enet_address_set_host(&lan, "127.0.0.1");
  game.port = a.t->port(), lan.port = a.t->discovery_port();
  auto probe = [&](const ENetAddress& to, size_t size) {
    std::vector<uint8_t> f(size, 0);
    std::memcpy(f.data(), "MW3R?", 5);
    ENetBuffer b{};
    b.data = f.data(), b.dataLength = f.size();
    enet_socket_send(s, &to, &b, 1);
  };
  auto answers = [&](int ms, size_t* largest) {
    int n = 0;
    for (int i = 0; i < ms; ++i) {
      a.t->service(1);
      uint8_t buf[512];
      ENetBuffer b{};
      b.data = buf, b.dataLength = sizeof buf;
      ENetAddress from{};
      int k;
      while ((k = enet_socket_receive(s, &from, &b, 1)) > 0) {
        ++n;
        if (largest) *largest = std::max(*largest, static_cast<size_t>(k));
      }
    }
    return n;
  };
  probe(game, 5), probe(lan, 5), probe(game, 127), probe(lan, 127);
  EXPECT(answers(150, nullptr) == 0);  // short probes: nothing
  size_t largest = 0;
  probe(game, 128), probe(lan, 128);
  EXPECT(answers(150, &largest) == 2 && largest <= 128);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  for (int i = 0; i < 20; ++i) probe(game, 128);
  const int burst = answers(200, nullptr);
  std::printf("  a burst of 20 probes: %d answers\n", burst);
  EXPECT(burst == 4);
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  probe(game, 128);
  EXPECT(answers(150, nullptr) == 1);  // the next second: answered again
  enet_socket_destroy(s);
}
