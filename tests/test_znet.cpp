// zNetwork's session over the in-process hub (docs/multiplayer-port.md,
// piece 1): hosting and joining, the checks, sends to all and to one, the
// system events, the shutdown.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "check.h"
#include "gamez/znetwork/znet_dplay.h"
#include "gamez/znetwork/znet_session.h"
#include "mech3/mech_net.h"

namespace {
// The next event a transport gives that is not a send done.
bool next_event(znet::Transport* t, znet::Packet* p) {
  while (t->receive(p))
    if (!(p->from == 0 && p->sys.kind == znet::kSysSendComplete)) return true;
  return false;
}
uint32_t word(const std::vector<uint8_t>& d, size_t at) {
  uint32_t v = 0;
  if (d.size() >= at + 4) std::memcpy(&v, d.data() + at, 4);
  return v;
}
}  // namespace

TEST(znet_session_checks_without_a_session) {
  znet::session() = znet::Session{};
  EXPECT(!znet::active() && znet::local_id() == 0 && !znet::is_host());
  const uint8_t m[4] = {0x1f, 0, 4, 0};
  EXPECT(znet::send(m, 4, true, 0) == 0);  // no local player: nothing sent (the exe reads through null)
  znet::shutdown();                         // not initialised: nothing to do
  EXPECT(!znet::session().initialised);
}

TEST(znet_session_host_join_send_and_leave) {
  znet::Hub hub;
  znet::Transport* a = hub.endpoint();
  znet::Transport* b = hub.endpoint();
  // The host, through zNetwork's globals.
  znet::session() = znet::Session{};
  znet::initialise(a);
  EXPECT(znet::session().initialised && !znet::active());  // up, no session yet
  znet::SessionDesc desc;
  desc.name = "test";
  EXPECT(znet::host_session(desc, "Host"));
  EXPECT(znet::active() && znet::local_id() == 1 && znet::is_host());
  EXPECT(!znet::host_session(desc, "Host"));  // one session at a time
  // A second machine joins through its own transport (the hub, not zNetwork:
  // one process holds one zNetwork).
  EXPECT(b->join("loopback") && b->create_player("Bob", "Bob") == 2);
  znet::Packet p;
  EXPECT(next_event(a, &p) && p.from == 0 && word(p.data, 0) == znet::kSysCreatePlayerOrGroup && word(p.data, 4) == 2);
  EXPECT(!a->receive(&p));
  // The joiner hears nothing of those already there: it enumerates them (0x005886c0).
  EXPECT(!b->receive(&p));
  // To all from the host (async: its send's id, and the send-done event).
  const uint8_t m[8] = {0x14, 0, 8, 0, 9, 9, 9, 9};
  const uint32_t id = znet::send(m, 8, true, 0);
  EXPECT(id != 0);
  EXPECT(a->receive(&p) && p.from == 0 && word(p.data, 0) == znet::kSysSendComplete && word(p.data, 4) == id);
  EXPECT(b->receive(&p) && p.from == 1 && p.to == 0 && p.data.size() == 8 && p.data[0] == 0x14);
  // To one: only that one gets it; the sender never hears its own.
  EXPECT(b->send(1, m, 4, false, nullptr));
  EXPECT(a->receive(&p) && p.from == 2 && p.data.size() == 4);
  EXPECT(!b->receive(&p));
  // b leaves: the host hears it.
  b->close();
  EXPECT(a->receive(&p) && p.from == 0 && word(p.data, 0) == znet::kSysDestroyPlayerOrGroup && word(p.data, 4) == 2);
  // The host's shutdown clears zNetwork.
  znet::shutdown();
  const znet::Session& s = znet::session();
  EXPECT(!s.initialised && !s.open && !s.local && s.host_id == 0 && s.transport == nullptr);
  EXPECT(!znet::active() && znet::local_id() == 0);
}

TEST(znet_session_join_needs_a_host) {
  znet::Hub hub;
  znet::session() = znet::Session{};
  znet::initialise(hub.endpoint());
  EXPECT(!znet::join_session("loopback", "Me") && !znet::active());  // nobody hosts yet
  znet::Transport* h = hub.endpoint();
  znet::SessionDesc d;
  EXPECT(h->host(d) && h->create_player("Host", "Host") == 1);
  EXPECT(znet::join_session("loopback", "Me") && znet::active() && znet::local_id() == 2 && !znet::is_host());
  znet::shutdown();
}

// Piece 2: the dispatch's own cases the exe comparison leaves out (a handler
// removed while a dispatch runs, which read a deleted node in the exe; one
// added meanwhile, which the exe's walk reaches), and the poll over the hub.
namespace {
std::vector<int> g_calls;
void h_a(uint32_t, const uint8_t*, size_t) { g_calls.push_back(1); }
void h_b(uint32_t, const uint8_t*, size_t) { g_calls.push_back(2); }
void h_removes_a(uint32_t, const uint8_t*, size_t) {
  g_calls.push_back(3);
  znet::off(0x1f, &h_a);
}
void h_adds_b(uint32_t, const uint8_t*, size_t) {
  g_calls.push_back(4);
  znet::on(0x1f, &h_b);
}
uint32_t g_from = 0;
size_t g_size = 0;
void h_keep(uint32_t from, const uint8_t*, size_t size) { g_from = from, g_size = size; }
int g_sys = 0;
}  // namespace

TEST(znet_dispatch_while_handlers_change) {
  znet::session() = znet::Session{};
  const uint8_t m[4] = {0x1f, 0, 4, 0};
  g_calls.clear();
  znet::on(0x1f, &h_removes_a);
  znet::on(0x1f, &h_a);  // removed by the first before its turn: not called
  znet::on(0x1f, &h_adds_b);
  znet::dispatch(9, m, 4);
  EXPECT((g_calls == std::vector<int>{3, 4, 2}));  // b, added meanwhile at the end, is reached
  EXPECT(znet::session().handlers.size() == 3);  // a erased after the dispatch
  g_calls.clear();
  znet::dispatch(9, m, 4);
  EXPECT((g_calls == std::vector<int>{3, 4, 2, 2}));  // a second b added this time
  znet::clear_handlers();
  EXPECT(znet::session().handlers.empty());
}

TEST(znet_poll_over_the_hub) {
  znet::Hub hub;
  znet::session() = znet::Session{};
  znet::initialise(hub.endpoint());
  znet::SessionDesc d;
  EXPECT(znet::host_session(d, "Host"));
  EXPECT(znet::poll(-1) == 1);  // our 0xd's send done (the host says so on making its player)
  znet::Transport* b = hub.endpoint();
  EXPECT(b->join("loopback") && b->create_player("B", "B") == 2);
  znet::session().system_message = [](const znet::Packet&) { ++g_sys; };
  znet::on(0x1f, &h_keep);
  const uint8_t chat[12] = {0x1f, 0, 12, 0, 0, 0, 0, 0, 'h', 'i', 0, 0};
  const uint8_t tiny[3] = {0x1f, 0, 3};
  EXPECT(b->send(0, chat, 12, true, nullptr) && b->send(0, tiny, 3, true, nullptr) && b->send(1, chat, 8, false, nullptr));
  g_sys = 0, g_from = 0, g_size = 0;
  EXPECT(znet::poll(2) == 2);  // the join's event, then the chat; the rest left for the next poll
  EXPECT(g_sys == 1 && g_from == 2 && g_size == 12);
  EXPECT(znet::poll(-1) == 1);  // the 3-byte one dropped, the 8-byte one handled
  EXPECT(g_size == 8);
  znet::shutdown();  // the handlers cleared with it
  EXPECT(znet::session().handlers.empty() && !znet::active());
  EXPECT(znet::poll(-1) == 0);
}

// Piece 3a: the lists through the translation, over the hub. One process
// holds one zNetwork, so the host is zNetwork's and the others the hub's;
// then the roles swap (a joiner's zNetwork with the host on the hub).
namespace {
std::vector<int> g_types;
void h_types(uint32_t, const uint8_t* m, size_t) { g_types.push_back(m[0]); }
}  // namespace

TEST(znet_players_as_the_host) {
  znet::Hub hub;
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Transport* mine = hub.endpoint();
  znet::initialise(mine);
  znet::SessionDesc d;
  EXPECT(znet::host_session(d, "Host") && znet::is_host());
  EXPECT(znet::player(1) && znet::player(1)->short_name == "Host" && znet::player(1)->is_host);  // our own record
  for (uint16_t t = 3; t <= 7; ++t) znet::on(t, &h_types);
  znet::Transport* b = hub.endpoint();
  EXPECT(b->join("loopback") && b->create_player("Bob", "Bob the pilot") == 2);
  g_types.clear();
  // Our first 0xd's send done, Bob's arrival, our second 0xd's (the sends are async).
  EXPECT(znet::poll(-1) == 3);
  // A record for Bob, his names, not the host; types 3 then 4.
  const znet::PlayerRecord* r = znet::player(2);
  EXPECT(r && r->short_name == "Bob" && r->long_name == "Bob the pilot" && !r->is_host && r->team == 0);
  EXPECT(znet::player_name(2) && std::string(znet::player_name(2)) == "Bob");
  EXPECT((g_types == std::vector<int>{3, 4}));
  // The host says so (0xd, guaranteed, to all) on each arrival: Bob gets it.
  znet::Packet p;
  bool told = false;
  while (b->receive(&p))
    if (p.from == 1 && p.data.size() == 4 && p.data[0] == 0x0d) told = true;
  EXPECT(told);
  // Bob leaves: type 6, his record gone.
  b->close();
  g_types.clear();
  znet::poll(-1);
  EXPECT((g_types == std::vector<int>{6}) && !znet::player(2) && znet::lists().players.size() == 1);
  znet::shutdown();
}

TEST(znet_players_as_a_joiner) {
  znet::Hub hub;
  znet::Transport* h = hub.endpoint();
  znet::SessionDesc d;
  EXPECT(h->host(d) && h->create_player("Host", "The host") == 1);
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Transport* mine = hub.endpoint();
  znet::initialise(mine);
  EXPECT(znet::join_session("loopback", "Me") && znet::local_id() == 2 && !znet::is_host());
  znet::poll(-1);
  // The host's record (its caps say host), the host id still unknown.
  const znet::PlayerRecord* r = znet::player(1);
  EXPECT(r && r->short_name == "Host" && r->is_host && znet::session().host_id == 0);
  // The host's 0xd names it (0x00586d50).
  const uint8_t m[4] = {0x0d, 0, 4, 0};
  EXPECT(h->send(0, m, 4, true, nullptr));
  znet::poll(-1);
  EXPECT(znet::session().host_id == 1);
  // The team walk and the players' walk.
  int n = 0;
  EXPECT(znet::each_player([](uint32_t id, uint32_t team, bool host, const char* name, void* c) {
    if (host && id == 1 && team == 0 && std::string(name) == "Host") ++*static_cast<int*>(c);
  }, &n) == 2);  // the host and us
  EXPECT(n == 1 && znet::each_team([](uint32_t, uint32_t, bool, const char*, void*) {}, nullptr) == 0);
  // The shutdown empties the lists.
  znet::shutdown();
  EXPECT(znet::lists().players.empty() && znet::lists().teams.empty());
}

// Piece 3b over the hub: the host (the hub's) makes a team and puts us in
// it, sets its data and ours, and kicks us; zNetwork (ours) sees each.
TEST(znet_teams_data_and_a_kick) {
  znet::Hub hub;
  znet::Transport* h = hub.endpoint();
  znet::SessionDesc d;
  EXPECT(h->host(d) && h->create_player("Host", "The host") == 1);
  znet::session() = znet::Session{};
  znet::clear_lists();
  znet::Transport* mine = hub.endpoint();
  znet::initialise(mine);
  EXPECT(znet::join_session("loopback", "Me"));
  for (uint16_t t = 2; t <= 0xc; ++t) znet::on(t, &h_types);
  znet::poll(-1);
  // Our own record, as the session's opening enumerates (0x005886c0, piece 4).
  EXPECT(znet::enum_players() == 2 && znet::player(2) && znet::player(2)->short_name == "Me");
  // A team, and us in it: type 4 (the team made), then 5, our record's team.
  const uint32_t team = h->create_group("Red");
  EXPECT(team != 0 && h->add_to_group(team, 2));
  g_types.clear();
  znet::poll(-1);
  EXPECT((g_types == std::vector<int>{4, 5}));
  EXPECT(znet::team(team) && znet::team(team)->name == "Red" && znet::player(2) && znet::player(2)->team == team);
  // Our own calls: a second team, joined and left (our record follows the events).
  const uint32_t blue = znet::create_team("Blue");
  EXPECT(blue != 0 && znet::team(blue) && znet::add_to_team(blue, 2));
  znet::poll(-1);
  EXPECT(znet::player(2)->team == blue);
  EXPECT(znet::remove_from_team(blue, 2) && znet::player(2)->team == 0);
  // The enumerations made again from the transport: both teams, Red's member.
  EXPECT(znet::enum_teams() == 2 && znet::enum_team_players());
  EXPECT(znet::player(2)->team == team);
  EXPECT(znet::enum_players() == 2);
  // Data: the host's, set there; type 9 here, and read back (its size first).
  znet::poll(-1);  // our removal's type 2 first
  const uint8_t rec[6] = {1, 2, 3, 4, 5, 6};
  EXPECT(h->set_data(false, 1, rec, 6));
  g_types.clear();
  znet::poll(-1);
  EXPECT((g_types == std::vector<int>{9}));
  EXPECT(znet::player_data_size(1) == 6);
  uint8_t got[8] = {};
  uint32_t size = 4;
  EXPECT(!znet::get_player_data(1, got, &size) && size == 6);  // too short: its size
  size = 8;
  EXPECT(znet::get_player_data(1, got, &size) && size == 6 && got[5] == 6);
  // A failed call is a network error (type 0xc).
  g_types.clear();
  EXPECT(!znet::destroy_team(999));
  EXPECT((g_types == std::vector<int>{0xc}));
  // The host kicks us: type 7.
  EXPECT(h->destroy_player(2));
  g_types.clear();
  znet::poll(-1);
  EXPECT((g_types == std::vector<int>{7}));
  znet::shutdown();
}

// Piece 5a: the mission's network side over the hub. A joiner's first
// state makes its mech (a stand-in for 0x0046d450) and is applied; the
// other messages reach it; its leaving takes the mech away.
namespace {
int g_states, g_craters, g_freed;
std::string g_left_name;
float g_first_x;
}  // namespace

TEST(net_mission_over_the_hub) {
  znet::Hub hub;
  znet::session() = znet::Session{};
  znet::initialise(hub.endpoint());
  EXPECT(znet::host_session(znet::SessionDesc{}, "Host"));
  znet::Transport* b = hub.endpoint();
  EXPECT(b->join("loopback") && b->create_player("B", "B") == 2);
  mech3::NetCalls& c = mech3::net_calls();
  const mech3::NetCalls kept = c;
  c = mech3::NetCalls{};
  c.create = [](mech3::NetMech*, uint32_t from, const uint8_t*, size_t) {
    auto* m = new mech3::NetMech();
    m->owner = from;
    mech3::net_game().mechs.push_back(m);
  };
  c.set_position = [](mech3::NetMech&, const float p[3]) { g_first_x = p[0]; };
  c.state = [](mech3::NetMech* m, uint32_t from, const uint8_t*, size_t) { g_states += m && m->owner == from; };
  c.crater = [](mech3::NetMech* m, uint32_t, const uint8_t*, size_t) { g_craters += m == nullptr; };
  c.hud_message = [](float, int32_t id, const char* name) { if (id == 0x26a && name) g_left_name = name; };
  c.free = [](mech3::NetMech* m) {
    mech3::net_game().mechs.remove(m);
    delete m;
    ++g_freed;
  };
  c.now_ms = [] { return 1234u; };
  g_states = g_craters = g_freed = 0;
  const size_t before = znet::session().handlers.size();  // zNetwork's own (0xd)
  mech3::net_start();
  EXPECT(mech3::net_game().running && znet::session().handlers.size() == before + 18);
  uint8_t state[16] = {0x0f, 0, 16, 0};
  const float x = 42.5f;
  std::memcpy(state + 4, &x, 4);
  const uint8_t crater[4] = {0x19, 0, 4, 0}, unused[4] = {0x15, 0, 4, 0}, ping[4] = {0x10, 0, 4, 0};
  EXPECT(b->send(1, ping, 4, true, nullptr));  // no mech for 2 yet: dropped
  EXPECT(b->send(1, state, 16, true, nullptr) && b->send(1, state, 16, true, nullptr));
  EXPECT(b->send(1, unused, 4, true, nullptr) && b->send(1, crater, 4, true, nullptr));
  znet::poll(-1);
  EXPECT(mech3::net_game().mechs.size() == 1 && mech3::net_mech(2) && mech3::net_mech(2)->heard == 1234);
  EXPECT(g_first_x == 42.5f && g_states == 2 && g_craters == 1);
  b->close();  // 2 leaves: its mech goes, its name on the HUD
  znet::poll(-1);
  EXPECT(mech3::net_game().mechs.empty() && g_freed == 1 && g_left_name == "B");
  mech3::net_end();
  EXPECT(!mech3::net_game().running && znet::session().handlers.size() == before);
  mech3::net_end();  // a second time: nothing
  znet::shutdown();
  c = kept;
}
