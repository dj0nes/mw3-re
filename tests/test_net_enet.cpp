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

#include "check.h"
#include "frontend/sdl/net_enet.h"

namespace {
using sdlfe::EnetTransport;
using sdlfe::NetOptions;

NetOptions loopback() {
  NetOptions o;
  o.bind = "127.0.0.1";
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
  EXPECT(!c.has_event(znet::kSysSetPlayerOrGroupData, 3));  // not told of its own
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
