#include "frontend/sdl/net_enet.h"

// FIX (A): enet.h brings in <windows.h> on Windows; without NOMINMAX its
// min/max macros break std::min and std::max below.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <enet/enet.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <random>

namespace sdlfe {
namespace {

// Our frames' first byte (docs/multiplayer-port.md section 2).
enum Kind : uint8_t {
  kGame = 1,      // from, to, then the MW3 message
  kHello = 2,     // joiner -> host: our protocol
  kWelcome = 3,   // host -> joiner: the session, its players, teams and data
  kCreate = 4,    // joiner -> host: a player's names
  kId = 5,        // host -> joiner: its id
  kPlayer = 6,    // host -> the others: a new player (id, names, address)
  kMesh = 7,      // a new player -> each other: which player this link is
  kGroupMade = 8, kGroupGone = 9, kGroupAdd = 10, kGroupDel = 11,
  kData = 12,     // a player's or a team's data
  kKick = 13,     // the host -> a player: leave
  kRefused = 14,  // host -> joiner: another protocol, or full
  kDesc = 15,     // host -> the others: the session's description changed (SetSessionDesc)
};
constexpr uint32_t kProtocol = 2;  // 2: the search's answer carries the user words

// Little-endian fields by offset (section 3: no structs on the wire).
struct Writer {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) { u8(v & 0xff), u8(v >> 8); }
  void u32(uint32_t v) { u16(v & 0xffff), u16(v >> 16); }
  void bytes(const void* p, size_t n) {
    u32(static_cast<uint32_t>(n));
    b.insert(b.end(), static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
  }
  void str(const std::string& s) { bytes(s.data(), s.size()); }
};
struct Reader {
  const uint8_t* p;
  size_t n, at = 0;
  bool bad = false;
  Reader(const uint8_t* d, size_t size) : p(d), n(size) {}
  bool need(size_t k) {
    if (at + k > n) bad = true;
    return !bad;
  }
  uint8_t u8() { return need(1) ? p[at++] : 0; }
  uint16_t u16() {
    const uint16_t lo = u8();
    return static_cast<uint16_t>(lo | u8() << 8);
  }
  uint32_t u32() {
    const uint32_t lo = u16();
    return lo | static_cast<uint32_t>(u16()) << 16;
  }
  std::vector<uint8_t> bytes() {
    const uint32_t k = u32();
    if (!need(k)) return {};
    std::vector<uint8_t> v(p + at, p + at + k);
    at += k;
    return v;
  }
  std::string str() {
    const std::vector<uint8_t> v = bytes();
    return std::string(v.begin(), v.end());
  }
};

uint32_t now_ms() { return enet_time_get(); }

bool parse_address(const std::string& text, uint16_t default_port, ENetAddress* out) {
  std::string h = text;
  uint16_t port = default_port;
  const size_t colon = text.rfind(':');
  if (colon != std::string::npos) {
    h = text.substr(0, colon);
    port = static_cast<uint16_t>(std::atoi(text.c_str() + colon + 1));
  }
  if (h.empty()) h = "127.0.0.1";
  if (enet_address_set_host(out, h.c_str()) != 0) return false;
  out->port = port;
  return true;
}

std::string address_text(const ENetAddress& a) {
  char ip[64] = {};
  enet_address_get_host_ip(&a, ip, sizeof ip);
  return std::string(ip) + ":" + std::to_string(a.port);
}

// The search (ours): "MW3R?" asks, "MW3R!" answers with the protocol, the
// game port, the session's name, its players and limit and its four words.
constexpr char kAsk[] = "MW3R?";
constexpr char kAnswer[] = "MW3R!";
constexpr size_t kNameMax = 63;    // DirectPlay's session names; longer answers are cut
constexpr uint32_t kPlayersMax = 64;
// A host answers probes from anywhere on its game port: so that it cannot
// be a reflector for amplified traffic, a probe is padded to kAskSize (no
// answer is larger: 102 bytes at most) and shorter ones are dropped, and
// each source address gets at most kAnswersPerSecond answers.
constexpr size_t kAskSize = 128;
constexpr int kAnswersPerSecond = 4;
const uint8_t* ask_frame() {
  static uint8_t f[kAskSize] = {'M', 'W', '3', 'R', '?'};
  return f;
}
bool is_probe(const uint8_t* d, size_t n) { return n >= 5 && std::memcmp(d, kAsk, 5) == 0; }
bool probe_ok(size_t n) { return n >= kAskSize; }

std::vector<uint8_t> answer_frame(const znet::SessionDesc& desc, uint32_t players, uint16_t game_port) {
  Writer w;
  w.b.assign(kAnswer, kAnswer + 5);
  w.u32(kProtocol), w.u16(game_port), w.str(desc.name.substr(0, kNameMax)), w.u32(players), w.u32(desc.max_players);
  for (uint32_t u : desc.user) w.u32(u);
  return w.b;
}

// An answer from `from` (untrusted: a peer's packet, piece 8c's rule): the
// protocol ours, the name cut at its first NUL and at kNameMax, control
// characters as '?', the counts within the limits. False: dropped.
bool parse_answer(const uint8_t* d, size_t n, const ENetAddress& from, znet::FoundGame* out) {
  if (n < 5 || std::memcmp(d, kAnswer, 5) != 0) return false;
  Reader r(d + 5, n - 5);
  if (r.u32() != kProtocol) return false;
  ENetAddress game = from;
  game.port = r.u16();
  std::string name = r.str();
  out->players = r.u32(), out->desc.max_players = r.u32();
  for (uint32_t& u : out->desc.user) u = r.u32();
  if (r.bad || game.port == 0) return false;
  if (out->desc.max_players == 0 || out->desc.max_players > kPlayersMax || out->players > out->desc.max_players) return false;
  name = name.substr(0, std::min(name.find('\0'), kNameMax));
  for (char& ch : name)
    if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f) ch = '?';
  out->desc.name = name;
  out->address = address_text(game);
  return true;
}

}  // namespace

// Each source address's answers in the last second (the host's probes).
struct AnswerLimit {
  struct Window {
    uint32_t start = 0;
    int count = 0;
  };
  std::map<uint32_t, Window> by_host;
  bool allow(const ENetAddress& from) {
    const uint32_t now = now_ms();
    if (by_host.size() > 256)  // forget the quiet ones
      for (auto it = by_host.begin(); it != by_host.end();)
        it = static_cast<int32_t>(now - it->second.start) >= 1000 ? by_host.erase(it) : std::next(it);
    Window& w = by_host[from.host];
    if (w.count == 0 || static_cast<int32_t>(now - w.start) >= 1000) w.start = now, w.count = 0;
    return ++w.count <= kAnswersPerSecond;
  }
};

struct EnetTransport::Impl {
  struct Player {
    uint32_t id = 0;
    std::string short_name, long_name;
    ENetPeer* link = nullptr;
    ENetAddress address{};
    bool announced = true;  // false: linked to us (kMesh) before the host told us of it (kPlayer)
    std::vector<uint8_t> data;
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> pending;  // channel, frame: before the link is up
  };
  struct Group {
    uint32_t id = 0;
    std::string name;
    std::vector<uint32_t> members;
    std::vector<uint8_t> data;
  };
  struct Held {  // the shim's delay
    uint32_t due = 0;
    znet::Packet p;
  };

  NetOptions o;
  ENetHost* host = nullptr;
  bool opened = false, hosting = false;
  uint32_t local_id = 0, host_id = 0, next_id = 1, next_msg = 1, groups_made = 0;
  znet::SessionDesc desc;
  std::map<uint32_t, Player> players;
  std::vector<Group> groups;
  std::deque<Held> inbox;
  ENetPeer* to_host = nullptr;
  bool welcomed = false, refused = false;
  uint32_t given_id = 0;
  std::mt19937 rng{1999};
  // The LAN's answer while hosting, and the game list's search.
  ENetSocket lan = ENET_SOCKET_NULL;
  ENetSocket search = ENET_SOCKET_NULL;
  std::string search_for;
  uint32_t search_sent = 0;
  std::vector<znet::FoundGame> found;
  AnswerLimit limit;

  std::vector<uint8_t> answer() const {
    uint32_t n = 0;
    for (const auto& [id, pl] : players) n += pl.announced ? 1 : 0;
    ENetAddress a{};
    if (host) enet_socket_get_address(host->socket, &a);
    return answer_frame(desc, n, a.port);
  }
  // The answers waiting on a socket: probes answered (hosting), answers kept.
  void drain(ENetSocket s, bool answer_probes, bool keep_answers);

  Group* group(uint32_t id) {
    for (Group& g : groups)
      if (g.id == id) return &g;
    return nullptr;
  }
  Player* by_link(ENetPeer* p) {
    for (auto& [id, pl] : players)
      if (pl.link == p) return &pl;
    return nullptr;
  }
  void event(const znet::SystemEvent& e) {
    znet::Packet p;
    p.sys = e;
    const uint32_t about = e.kind == znet::kSysSendComplete ? e.msg_id : e.id;
    p.data.resize(8);
    std::memcpy(p.data.data(), &e.kind, 4);
    std::memcpy(p.data.data() + 4, &about, 4);
    hold(std::move(p));
  }
  void hold(znet::Packet&& p) { inbox.push_back(Held{now_ms() + o.delay_ms, std::move(p)}); }

  void put(ENetPeer* to, uint8_t channel, const std::vector<uint8_t>& frame) {
    if (!to) return;
    ENetPacket* pk = enet_packet_create(frame.data(), frame.size(),
                                        channel == 0 ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED);
    enet_peer_send(to, channel, pk);
  }
  void to_player(Player& pl, uint8_t channel, const std::vector<uint8_t>& frame) {
    if (pl.link && pl.link->state == ENET_PEER_STATE_CONNECTED) put(pl.link, channel, frame);
    else pl.pending.emplace_back(channel, frame);
  }
  void to_everyone(uint8_t channel, const std::vector<uint8_t>& frame) {
    for (auto& [id, pl] : players)
      if (id != local_id) to_player(pl, channel, frame);
  }

  void wait() {
    service(o.while_waiting ? 1 : 10);
    if (o.while_waiting) o.while_waiting();
  }
  void on_frame(ENetPeer* from, uint8_t channel, const uint8_t* d, size_t n);
  void on_disconnect(ENetPeer* p);
  void service(uint32_t ms);
};

namespace {
// Each ENet host's transport, for the intercept (ENet keeps no user data).
std::vector<std::pair<ENetHost*, EnetTransport::Impl*>>& intercepts() {
  static std::vector<std::pair<ENetHost*, EnetTransport::Impl*>> v;
  return v;
}
}  // namespace

EnetTransport::EnetTransport(NetOptions o) : impl_(std::make_unique<Impl>()) {
  static const bool up = enet_initialize() == 0;
  if (!up) return;
  impl_->o = o;
  if (const char* l = std::getenv("MW3_NET_LOSS")) impl_->o.loss = std::atof(l);
  if (const char* d = std::getenv("MW3_NET_DELAY")) impl_->o.delay_ms = static_cast<uint32_t>(std::atoi(d));
  ENetAddress a{};
  if (enet_address_set_host(&a, o.bind.c_str()) != 0) return;
  a.port = o.port;
  impl_->host = enet_host_create(&a, 16, 2, 0, 0);  // 8 players, and joins in progress
  host_ = impl_->host;
  if (!impl_->host) return;
  // A search's probe to our game port: answered here, before ENet reads it.
  impl_->host->intercept = [](ENetHost* h, ENetEvent*) -> int {
    if (!is_probe(h->receivedData, h->receivedDataLength)) return 0;
    Impl* m = nullptr;
    for (auto& [host, impl] : intercepts())
      if (host == h) m = impl;
    if (m && m->hosting && probe_ok(h->receivedDataLength) && m->limit.allow(h->receivedAddress)) {
      const std::vector<uint8_t> f = m->answer();
      ENetBuffer out{};
      out.data = const_cast<uint8_t*>(f.data()), out.dataLength = f.size();
      enet_socket_send(h->socket, &h->receivedAddress, &out, 1);
    }
    return 1;
  };
  intercepts().emplace_back(impl_->host, impl_.get());
}

EnetTransport::~EnetTransport() {
  close();
  stop_search();
  auto& ic = intercepts();
  ic.erase(std::remove_if(ic.begin(), ic.end(), [&](const auto& e) { return e.second == impl_.get(); }), ic.end());
  if (impl_->host) {
    enet_host_flush(impl_->host);
    enet_host_destroy(impl_->host);
  }
}

uint16_t EnetTransport::port() const {
  if (!impl_->host) return 0;
  ENetAddress a{};
  enet_socket_get_address(impl_->host->socket, &a);
  return a.port;
}

bool EnetTransport::host(const znet::SessionDesc& d) {
  Impl& m = *impl_;
  if (!m.host || m.opened) return false;
  m.opened = m.hosting = true;
  m.desc = d;
  m.next_id = 1;
  // The LAN's search answered on the discovery port (a second host on the
  // same machine may not get it: then only probes to its game port reach it).
  if (m.o.discovery_port >= 0) {
    const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s != ENET_SOCKET_NULL) {
      enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
      enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1);
      ENetAddress a{};
      enet_address_set_host(&a, m.o.bind.c_str());
      a.port = static_cast<uint16_t>(m.o.discovery_port);
      if (enet_socket_bind(s, &a) == 0) m.lan = s;
      else enet_socket_destroy(s);
    }
  }
  return true;
}

bool EnetTransport::join(const std::string& address) {
  Impl& m = *impl_;
  if (!m.host || m.opened) return false;
  ENetAddress a{};
  if (!parse_address(address, kDefaultPort, &a)) return false;
  m.to_host = enet_host_connect(m.host, &a, 2, 0);
  if (!m.to_host) return false;
  m.welcomed = m.refused = false;
  const uint32_t until = now_ms() + 5000;
  while (!m.welcomed && !m.refused && m.to_host && static_cast<int32_t>(until - now_ms()) > 0) m.wait();
  if (!m.welcomed) {
    if (m.to_host) enet_peer_reset(m.to_host);
    m.to_host = nullptr;
    m.players.clear(), m.groups.clear();
    return false;
  }
  m.opened = true;
  return true;
}

uint32_t EnetTransport::create_player(const std::string& s, const std::string& l) {
  Impl& m = *impl_;
  if (!m.opened || m.local_id) return 0;
  if (m.hosting) {
    m.local_id = m.next_id++;
    if (!m.host_id) m.host_id = m.local_id;
  } else {
    Writer w;
    w.u8(kCreate), w.str(s), w.str(l);
    m.put(m.to_host, 0, w.b);
    m.given_id = 0;
    const uint32_t until = now_ms() + 5000;
    while (!m.given_id && m.to_host && static_cast<int32_t>(until - now_ms()) > 0) m.wait();
    if (!m.given_id) return 0;
    m.local_id = m.given_id;
    // Linked to each other player, who learn which player we are.
    for (auto& [id, pl] : m.players)
      if (id != m.local_id && !pl.link) pl.link = enet_host_connect(m.host, &pl.address, 2, 0);
  }
  Impl::Player& me = m.players[m.local_id];
  me.id = m.local_id, me.short_name = s, me.long_name = l;
  return m.local_id;
}

znet::SessionDesc EnetTransport::session_desc() const { return impl_->desc; }

bool EnetTransport::is_host(uint32_t id) const { return id && id == impl_->host_id; }

void EnetTransport::close() {
  Impl& m = *impl_;
  if (!m.host) return;
  // The disconnect sent now (DirectPlay's Close told the others at once): a
  // _later one waits for a service that never comes, as the host is freed
  // next, and the others only timed the player out (playtest W46).
  for (ENetPeer* p = m.host->peers; p < &m.host->peers[m.host->peerCount]; ++p)
    if (p->state != ENET_PEER_STATE_DISCONNECTED) enet_peer_disconnect(p, 0);
  enet_host_flush(m.host);
  m.players.clear(), m.groups.clear(), m.inbox.clear();
  m.opened = m.hosting = false;
  m.local_id = m.host_id = 0;
  m.to_host = nullptr;
  if (m.lan != ENET_SOCKET_NULL) enet_socket_destroy(m.lan);
  m.lan = ENET_SOCKET_NULL;
}

uint16_t EnetTransport::discovery_port() const {
  ENetAddress a{};
  if (impl_->lan == ENET_SOCKET_NULL || enet_socket_get_address(impl_->lan, &a) != 0) return 0;
  return a.port;
}

void EnetTransport::Impl::drain(ENetSocket s, bool answer_probes, bool keep_answers) {
  for (;;) {
    uint8_t buf[512];
    ENetBuffer b{};
    b.data = buf, b.dataLength = sizeof buf;
    ENetAddress from{};
    const int n = enet_socket_receive(s, &from, &b, 1);
    if (n <= 0) return;
    if (is_probe(buf, static_cast<size_t>(n))) {
      if (!answer_probes || !probe_ok(static_cast<size_t>(n)) || !limit.allow(from)) continue;
      const std::vector<uint8_t> f = answer();
      ENetBuffer out{};
      out.data = const_cast<uint8_t*>(f.data()), out.dataLength = f.size();
      enet_socket_send(s, &from, &out, 1);
      continue;
    }
    znet::FoundGame g;
    if (!keep_answers || !parse_answer(buf, static_cast<size_t>(n), from, &g)) continue;
    auto same = std::find_if(found.begin(), found.end(), [&](const znet::FoundGame& x) { return x.address == g.address; });
    if (same != found.end()) *same = g;
    else found.push_back(g);
  }
}

// The game list's search (ours, for 0x00587140).
int32_t EnetTransport::find_sessions(const std::string& address, std::vector<znet::FoundGame>* out) {
  Impl& m = *impl_;
  if (m.search == ENET_SOCKET_NULL || address != m.search_for) {
    stop_search();
    const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
    if (s == ENET_SOCKET_NULL) {
      out->clear();
      return znet::kErrGeneric;
    }
    enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
    enet_socket_set_option(s, ENET_SOCKOPT_BROADCAST, 1);
    m.search = s, m.search_for = address, m.search_sent = 0, m.found.clear();
  }
  if (m.search_sent == 0 || static_cast<int32_t>(now_ms() - m.search_sent) >= 1000) {
    ENetAddress to{};
    bool ok;
    if (address.empty() && lan_address.empty()) to.host = ENET_HOST_BROADCAST, to.port = search_port, ok = true;
    else if (address.empty()) ok = parse_address(lan_address, search_port, &to);
    else ok = parse_address(address, kDefaultPort, &to);
    if (!ok) {
      out->clear();
      return znet::kErrNoConnection;  // not an address
    }
    ENetBuffer ask{};
    ask.data = const_cast<uint8_t*>(ask_frame()), ask.dataLength = kAskSize;
    enet_socket_send(m.search, &to, &ask, 1);
    m.search_sent = now_ms() | 1;
  }
  m.drain(m.search, false, true);
  *out = m.found;
  return znet::kOk;
}

void EnetTransport::stop_search() {
  Impl& m = *impl_;
  if (m.search != ENET_SOCKET_NULL) enet_socket_destroy(m.search);
  m.search = ENET_SOCKET_NULL;
  m.search_for.clear();
  m.found.clear();
}

bool EnetTransport::send(uint32_t to, const void* data, size_t size, bool guaranteed, uint32_t* id) {
  Impl& m = *impl_;
  if (!m.opened || !m.local_id) return false;
  Writer w;
  w.u8(kGame), w.u32(m.local_id), w.u32(to);
  w.b.insert(w.b.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
  const uint8_t ch = guaranteed ? 0 : 1;
  if (to == 0) {
    m.to_everyone(ch, w.b);
  } else if (Impl::Group* g = m.group(to)) {  // a team (team chat)
    for (uint32_t pid : g->members)
      if (pid != m.local_id && m.players.count(pid)) m.to_player(m.players[pid], ch, w.b);
  } else if (m.players.count(to) && to != m.local_id) {
    m.to_player(m.players[to], ch, w.b);
  } else {
    return false;
  }
  if (id) {  // ENet queues at once: the send is done now (its time for 0xe's latency)
    *id = m.next_msg++;
    znet::SystemEvent e;
    e.kind = znet::kSysSendComplete, e.id = m.local_id, e.msg_id = *id, e.send_time = now_ms();
    m.event(e);
  }
  return true;
}

bool EnetTransport::receive(znet::Packet* out) {
  Impl& m = *impl_;
  m.service(0);
  if (m.inbox.empty() || static_cast<int32_t>(m.inbox.front().due - now_ms()) > 0) return false;
  *out = std::move(m.inbox.front().p);
  m.inbox.pop_front();
  return true;
}

void EnetTransport::service(uint32_t ms) { impl_->service(ms); }

void EnetTransport::Impl::service(uint32_t ms) {
  if (!host) return;
  if (lan != ENET_SOCKET_NULL) drain(lan, hosting, false);
  ENetEvent ev;
  bool first = true;
  while (enet_host_service(host, &ev, first ? ms : 0) > 0) {
    first = false;
    switch (ev.type) {
      case ENET_EVENT_TYPE_CONNECT: {
        if (ev.peer == to_host) {  // our join's link to the host
          Writer w;
          w.u8(kHello), w.u32(kProtocol);
          put(ev.peer, 0, w.b);
        } else if (Player* pl = by_link(ev.peer)) {  // our link to another player, as a new one
          Writer w;
          w.u8(kMesh), w.u32(local_id);
          put(ev.peer, 0, w.b);
          for (auto& [ch, f] : pl->pending) put(ev.peer, ch, f);
          pl->pending.clear();
        }
        break;
      }
      case ENET_EVENT_TYPE_RECEIVE:
        on_frame(ev.peer, ev.channelID, ev.packet->data, ev.packet->dataLength);
        enet_packet_destroy(ev.packet);
        break;
      case ENET_EVENT_TYPE_DISCONNECT:
        on_disconnect(ev.peer);
        break;
      default:
        break;
    }
  }
  enet_host_flush(host);
}

void EnetTransport::Impl::on_frame(ENetPeer* from, uint8_t channel, const uint8_t* d, size_t n) {
  if (n < 1) return;
  // The shim: unreliable messages lost.
  if (channel == 1 && o.loss > 0 && std::uniform_real_distribution<double>(0, 1)(rng) < o.loss) return;
  Reader r(d, n);
  const uint8_t kind = r.u8();
  switch (kind) {
    case kGame: {
      znet::Packet p;
      p.from = r.u32(), p.to = r.u32();
      if (r.bad || !players.count(p.from)) return;
      p.data.assign(d + r.at, d + n);
      hold(std::move(p));
      return;
    }
    case kHello: {  // a joiner, at the host
      if (!hosting) return;
      // Ours (piece 11b): a locked game (its fourth word 3) is shut at the
      // door; the exe's connection screen only greyed JOIN for it.
      if (r.u32() != kProtocol || players.size() >= desc.max_players || desc.user[3] == 3) {
        Writer w;
        w.u8(kRefused);
        put(from, 0, w.b);
        return;
      }
      Writer w;
      w.u8(kWelcome), w.str(desc.name), w.u32(desc.max_players);
      for (uint32_t u : desc.user) w.u32(u);
      w.u32(host_id);
      w.u32(static_cast<uint32_t>(players.size()));
      for (auto& [id, pl] : players) {
        const ENetAddress a = pl.link ? pl.link->address : ENetAddress{};
        w.u32(id), w.str(pl.short_name), w.str(pl.long_name), w.u32(a.host), w.u16(a.port), w.bytes(pl.data.data(), pl.data.size());
      }
      w.u32(static_cast<uint32_t>(groups.size()));
      for (const Group& g : groups) {
        w.u32(g.id), w.str(g.name), w.bytes(g.data.data(), g.data.size());
        w.u32(static_cast<uint32_t>(g.members.size()));
        for (uint32_t m : g.members) w.u32(m);
      }
      put(from, 0, w.b);
      return;
    }
    case kWelcome: {  // the host's answer, at the joiner
      if (from != to_host) return;
      desc.name = r.str(), desc.max_players = r.u32();
      for (uint32_t& u : desc.user) u = r.u32();
      host_id = r.u32();
      const uint32_t np = r.u32();
      for (uint32_t i = 0; i < np && !r.bad; ++i) {
        Player pl;
        pl.id = r.u32(), pl.short_name = r.str(), pl.long_name = r.str();
        pl.address.host = r.u32(), pl.address.port = r.u16();
        pl.data = r.bytes();
        if (pl.id == host_id) pl.link = to_host;
        players[pl.id] = pl;
      }
      const uint32_t ng = r.u32();
      for (uint32_t i = 0; i < ng && !r.bad; ++i) {
        Group g;
        g.id = r.u32(), g.name = r.str(), g.data = r.bytes();
        const uint32_t k = r.u32();
        for (uint32_t j = 0; j < k && !r.bad; ++j) g.members.push_back(r.u32());
        groups.push_back(g);
      }
      welcomed = !r.bad;
      return;
    }
    case kRefused:
      if (from == to_host) refused = true;
      return;
    case kCreate: {  // at the host: a joiner's player
      if (!hosting) return;
      Player pl;
      pl.id = next_id++, pl.short_name = r.str(), pl.long_name = r.str();
      pl.link = from, pl.address = from->address;
      if (r.bad) return;
      players[pl.id] = pl;
      // Its id, and the players made since its welcome (two joining at once:
      // the other's was not in it).
      Writer id;
      id.u8(kId), id.u32(pl.id), id.u32(static_cast<uint32_t>(players.size() - 1));
      for (auto& [oid, op] : players) {
        if (oid == pl.id) continue;
        const ENetAddress oa = op.link ? op.link->address : ENetAddress{};
        id.u32(oid), id.str(op.short_name), id.str(op.long_name), id.u32(oa.host), id.u16(oa.port);
      }
      put(from, 0, id.b);
      Writer told;
      told.u8(kPlayer), told.u32(pl.id), told.str(pl.short_name), told.str(pl.long_name), told.u32(from->address.host),
          told.u16(from->address.port);
      for (auto& [oid, op] : players)
        if (oid != local_id && oid != pl.id) to_player(op, 0, told.b);
      znet::SystemEvent e;
      e.kind = znet::kSysCreatePlayerOrGroup, e.player_type = 1, e.id = pl.id, e.short_name = pl.short_name,
      e.long_name = pl.long_name;
      event(e);
      return;
    }
    case kId: {
      if (from != to_host) return;
      const uint32_t id = r.u32(), n = r.u32();
      for (uint32_t i = 0; i < n && !r.bad; ++i) {
        Player pl;
        pl.id = r.u32(), pl.short_name = r.str(), pl.long_name = r.str();
        pl.address.host = r.u32(), pl.address.port = r.u16();
        if (r.bad || players.count(pl.id)) continue;
        players[pl.id] = pl;  // linked to by create_player, as the welcome's
        znet::SystemEvent e;
        e.kind = znet::kSysCreatePlayerOrGroup, e.player_type = 1, e.id = pl.id, e.short_name = pl.short_name,
        e.long_name = pl.long_name;
        event(e);
      }
      given_id = id;
      return;
    }
    case kPlayer: {  // a new player, told by the host; it links to us
      Player pl;
      pl.id = r.u32(), pl.short_name = r.str(), pl.long_name = r.str();
      pl.address.host = r.u32(), pl.address.port = r.u16();
      if (r.bad) return;
      if (players.count(pl.id)) {  // its link came first: now its names, and its event
        Player& was = players[pl.id];
        if (was.announced) return;
        was.short_name = pl.short_name, was.long_name = pl.long_name, was.address = pl.address, was.announced = true;
      } else {
        players[pl.id] = pl;
      }
      znet::SystemEvent e;
      e.kind = znet::kSysCreatePlayerOrGroup, e.player_type = 1, e.id = pl.id, e.short_name = pl.short_name,
      e.long_name = pl.long_name;
      event(e);
      return;
    }
    case kMesh: {  // a new player's link to us
      const uint32_t id = r.u32();
      if (r.bad || !id) return;
      if (!players.count(id)) {  // before the host's word of it: held until then
        Player ph;
        ph.id = id, ph.announced = false;
        players[id] = ph;
      }
      Player& pl = players[id];
      pl.link = from;
      for (auto& [ch, f] : pl.pending) put(from, ch, f);
      pl.pending.clear();
      return;
    }
    case kGroupMade: {
      Group g;
      g.id = r.u32(), g.name = r.str();
      if (r.bad || group(g.id)) return;
      groups.push_back(g);
      znet::SystemEvent e;
      e.kind = znet::kSysCreatePlayerOrGroup, e.player_type = 0, e.id = g.id, e.short_name = e.long_name = g.name;
      event(e);
      return;
    }
    case kGroupGone: {
      const uint32_t id = r.u32();
      groups.erase(std::remove_if(groups.begin(), groups.end(), [&](const Group& g) { return g.id == id; }), groups.end());
      znet::SystemEvent e;
      e.kind = znet::kSysDestroyPlayerOrGroup, e.player_type = 0, e.id = id;
      event(e);
      return;
    }
    case kGroupAdd:
    case kGroupDel: {
      const uint32_t gid = r.u32(), pid = r.u32();
      Group* g = group(gid);
      if (r.bad || !g) return;
      auto it = std::find(g->members.begin(), g->members.end(), pid);
      if (kind == kGroupAdd && it == g->members.end()) g->members.push_back(pid);
      if (kind == kGroupDel && it != g->members.end()) g->members.erase(it);
      znet::SystemEvent e;
      e.kind = kind == kGroupAdd ? znet::kSysAddPlayerToGroup : znet::kSysDeletePlayerFromGroup, e.group = gid, e.id = pid;
      event(e);
      return;
    }
    case kData: {
      const bool is_group = r.u8() != 0;
      const uint32_t id = r.u32();
      std::vector<uint8_t> v = r.bytes();
      if (r.bad) return;
      if (is_group) {
        if (Group* g = group(id)) g->data = std::move(v);
      } else if (players.count(id)) {
        players[id].data = std::move(v);
      }
      znet::SystemEvent e;
      e.kind = znet::kSysSetPlayerOrGroupData, e.player_type = is_group ? 0 : 1, e.id = id;
      event(e);
      return;
    }
    case kDesc: {  // the host's new description
      if (from != to_host) return;
      znet::SessionDesc d;
      d.name = r.str(), d.max_players = r.u32();
      for (uint32_t& u : d.user) u = r.u32();
      if (r.bad || d.max_players == 0 || d.max_players > kPlayersMax) return;
      desc = d;
      znet::SystemEvent e;
      e.kind = znet::kSysSetSessionDesc, e.desc = d;
      event(e);
      return;
    }
    case kKick: {  // we are out: our own end, then the links go
      znet::SystemEvent e;
      e.kind = znet::kSysDestroyPlayerOrGroup, e.player_type = 1, e.id = local_id;
      const uint32_t was = local_id;
      for (ENetPeer* p = host->peers; p < &host->peers[host->peerCount]; ++p)
        if (p->state != ENET_PEER_STATE_DISCONNECTED) enet_peer_disconnect_later(p, 0);
      players.clear(), groups.clear();
      opened = hosting = false;
      local_id = host_id = 0;
      to_host = nullptr;
      e.id = was;
      event(e);
      return;
    }
    default:
      return;
  }
}

// A player's link gone: it left (or was kicked, or lost). Its end for all;
// the host gone, the lowest id left takes over (DPSYS_HOST to it).
void EnetTransport::Impl::on_disconnect(ENetPeer* p) {
  if (p == to_host && !welcomed) {
    to_host = nullptr;
    return;
  }
  Player* pl = by_link(p);
  if (!pl) return;
  const uint32_t gone = pl->id;
  players.erase(gone);
  for (Group& g : groups) g.members.erase(std::remove(g.members.begin(), g.members.end(), gone), g.members.end());
  if (p == to_host) to_host = nullptr;
  znet::SystemEvent e;
  e.kind = znet::kSysDestroyPlayerOrGroup, e.player_type = 1, e.id = gone;
  event(e);
  if (gone == host_id && opened) {
    uint32_t lowest = local_id;
    for (auto& [id, q] : players) lowest = std::min(lowest, id);
    host_id = lowest;
    if (lowest == local_id) {
      hosting = true;
      next_id = 1;
      for (auto& [id, q] : players) next_id = std::max(next_id, id + 1);
      znet::SystemEvent h;
      h.kind = znet::kSysHost;
      event(h);
    }
  }
}

uint32_t EnetTransport::create_group(const std::string& name) {
  Impl& m = *impl_;
  if (!m.local_id) return 0;
  Impl::Group g;
  g.id = 0x10000u * m.local_id + ++m.groups_made;  // unique without asking the host
  g.name = name;
  m.groups.push_back(g);
  Writer w;
  w.u8(kGroupMade), w.u32(g.id), w.str(name);
  m.to_everyone(0, w.b);
  return g.id;  // DirectPlay does not tell its maker
}

bool EnetTransport::destroy_group(uint32_t gid) {
  Impl& m = *impl_;
  if (!m.local_id || !m.group(gid)) return false;
  Writer w;
  w.u8(kGroupGone), w.u32(gid);
  m.to_everyone(0, w.b);
  m.on_frame(nullptr, 0, w.b.data(), w.b.size());  // ours as the others': its event to us too
  return true;
}

bool EnetTransport::add_to_group(uint32_t gid, uint32_t pid) {
  Impl& m = *impl_;
  if (!m.local_id || !m.group(gid) || !m.players.count(pid)) return false;
  Writer w;
  w.u8(kGroupAdd), w.u32(gid), w.u32(pid);
  m.to_everyone(0, w.b);
  m.on_frame(nullptr, 0, w.b.data(), w.b.size());
  return true;
}

bool EnetTransport::delete_from_group(uint32_t gid, uint32_t pid) {
  Impl& m = *impl_;
  Impl::Group* g = m.group(gid);
  if (!m.local_id || !g || std::find(g->members.begin(), g->members.end(), pid) == g->members.end()) return false;
  Writer w;
  w.u8(kGroupDel), w.u32(gid), w.u32(pid);
  m.to_everyone(0, w.b);
  m.on_frame(nullptr, 0, w.b.data(), w.b.size());
  return true;
}

bool EnetTransport::destroy_player(uint32_t pid) {
  Impl& m = *impl_;
  if (!m.local_id) return false;
  if (pid == m.local_id) {
    close();
    return true;
  }
  if (!m.players.count(pid) || !m.players[pid].link) return false;
  Writer w;
  w.u8(kKick);
  m.put(m.players[pid].link, 0, w.b);  // it leaves; everyone sees its link go
  return true;
}

bool EnetTransport::players(std::vector<Member>* out) {
  out->clear();
  for (auto& [id, pl] : impl_->players) out->push_back(Member{id, pl.short_name, pl.long_name});
  return impl_->opened;
}

bool EnetTransport::groups(std::vector<Member>* out) {
  out->clear();
  for (const Impl::Group& g : impl_->groups) out->push_back(Member{g.id, g.name, g.name});
  return impl_->opened;
}

bool EnetTransport::group_players(uint32_t gid, std::vector<uint32_t>* out) {
  out->clear();
  const Impl::Group* g = impl_->group(gid);
  if (!g) return false;
  *out = g->members;
  return true;
}

bool EnetTransport::set_data(bool is_group, uint32_t id, const void* data, uint32_t size) {
  Impl& m = *impl_;
  std::vector<uint8_t>* into = nullptr;
  if (is_group) {
    if (Impl::Group* g = m.group(id)) into = &g->data;
  } else if (m.players.count(id)) {
    into = &m.players[id].data;
  }
  if (!m.local_id || !into) return false;
  Writer w;
  w.u8(kData), w.u8(is_group ? 1 : 0), w.u32(id), w.bytes(data, size);
  m.to_everyone(0, w.b);
  // The setter hears it too, as with the teams' changes: the lobby's own
  // READY refreshes its lines by it (0x00499600 on 9; multiplayer-port.md 11c).
  m.on_frame(nullptr, 0, w.b.data(), w.b.size());
  return true;
}

// The host's (DirectPlay's SetSessionDesc): kept, and the others told; the
// search's answers carry it from now on.
bool EnetTransport::set_session_desc(const znet::SessionDesc& d) {
  Impl& m = *impl_;
  if (!m.hosting || !m.local_id) return false;
  m.desc.name = d.name.substr(0, kNameMax), m.desc.max_players = d.max_players;
  for (int i = 0; i < 4; ++i) m.desc.user[i] = d.user[i];
  Writer w;
  w.u8(kDesc), w.str(m.desc.name), w.u32(m.desc.max_players);
  for (uint32_t u : m.desc.user) w.u32(u);
  m.to_everyone(0, w.b);
  return true;
}

bool EnetTransport::get_data(bool is_group, uint32_t id, void* data, uint32_t* size) {
  Impl& m = *impl_;
  const std::vector<uint8_t>* from = nullptr;
  if (is_group) {
    if (Impl::Group* g = m.group(id)) from = &g->data;
  } else if (m.players.count(id)) {
    from = &m.players[id].data;
  }
  if (!from) {
    *size = 0;
    return false;
  }
  const uint32_t have = *size;
  *size = static_cast<uint32_t>(from->size());
  if (!data || have < from->size()) return false;
  std::memcpy(data, from->data(), from->size());
  return true;
}

// ---------------------------------------------------------------------------
// LAN discovery: "MW3R?" asks, "MW3R!" answers with the session.

DiscoveryAnswer::DiscoveryAnswer(uint16_t port, const std::string& bind) {
  static const bool up = enet_initialize() == 0;
  if (!up) return;
  const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (s == ENET_SOCKET_NULL) return;
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  enet_socket_set_option(s, ENET_SOCKOPT_REUSEADDR, 1);
  ENetAddress a{};
  enet_address_set_host(&a, bind.c_str());
  a.port = port;
  if (enet_socket_bind(s, &a) != 0) {
    enet_socket_destroy(s);
    return;
  }
  enet_socket_get_address(s, &a);
  socket_ = static_cast<intptr_t>(s);
  port_ = a.port;
  limit_ = std::make_shared<AnswerLimit>();
}

DiscoveryAnswer::~DiscoveryAnswer() {
  if (socket_ >= 0) enet_socket_destroy(static_cast<ENetSocket>(socket_));
}

void DiscoveryAnswer::poll(const znet::SessionDesc& desc, uint32_t players, uint16_t game_port) {
  if (socket_ < 0) return;
  for (;;) {
    uint8_t buf[256];
    ENetBuffer b{};
    b.data = buf, b.dataLength = sizeof buf;
    ENetAddress from{};
    const int n = enet_socket_receive(static_cast<ENetSocket>(socket_), &from, &b, 1);
    if (n <= 0) return;
    if (!is_probe(buf, static_cast<size_t>(n)) || !probe_ok(static_cast<size_t>(n)) || !limit_->allow(from)) continue;
    const std::vector<uint8_t> f = answer_frame(desc, players, game_port);
    ENetBuffer out{};
    out.data = const_cast<uint8_t*>(f.data()), out.dataLength = f.size();
    enet_socket_send(static_cast<ENetSocket>(socket_), &from, &out, 1);
  }
}

std::vector<FoundSession> browse(uint32_t ms, uint16_t port, const std::vector<std::string>& direct) {
  std::vector<FoundSession> found;
  static const bool up = enet_initialize() == 0;
  if (!up) return found;
  const ENetSocket s = enet_socket_create(ENET_SOCKET_TYPE_DATAGRAM);
  if (s == ENET_SOCKET_NULL) return found;
  enet_socket_set_option(s, ENET_SOCKOPT_NONBLOCK, 1);
  enet_socket_set_option(s, ENET_SOCKOPT_BROADCAST, 1);
  ENetBuffer ask{};
  ask.data = const_cast<uint8_t*>(ask_frame()), ask.dataLength = kAskSize;
  if (direct.empty()) {
    ENetAddress all{};
    all.host = ENET_HOST_BROADCAST, all.port = port;
    enet_socket_send(s, &all, &ask, 1);
  }
  for (const std::string& d : direct) {
    ENetAddress a{};
    if (parse_address(d, port, &a)) enet_socket_send(s, &a, &ask, 1);
  }
  const uint32_t until = now_ms() + ms;
  while (static_cast<int32_t>(until - now_ms()) > 0) {
    enet_uint32 wait = ENET_SOCKET_WAIT_RECEIVE;
    if (enet_socket_wait(s, &wait, 10) != 0 || !(wait & ENET_SOCKET_WAIT_RECEIVE)) continue;
    uint8_t buf[512];
    ENetBuffer b{};
    b.data = buf, b.dataLength = sizeof buf;
    ENetAddress from{};
    const int n = enet_socket_receive(s, &from, &b, 1);
    znet::FoundGame g;
    if (n <= 0 || !parse_answer(buf, static_cast<size_t>(n), from, &g)) continue;
    FoundSession f;
    f.name = g.desc.name, f.address = g.address, f.players = g.players, f.max_players = g.desc.max_players;
    for (int i = 0; i < 4; ++i) f.user[i] = g.desc.user[i];
    if (std::none_of(found.begin(), found.end(), [&](const FoundSession& x) { return x.address == f.address; }))
      found.push_back(f);
  }
  enet_socket_destroy(s);
  return found;
}

}  // namespace sdlfe
