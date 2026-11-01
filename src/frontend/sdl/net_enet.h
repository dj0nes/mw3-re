#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gamez/znetwork/znet_session.h"

// The multiplayer transport over ENet (FIX (A): DirectPlay 4 is gone;
// docs/multiplayer-port.md section 2). One ENet host per machine, used to
// listen and to connect; a full mesh of players as DirectPlay's peer to
// peer was. Channel 0 is reliable and ordered (the guaranteed messages and
// ours), channel 1 unreliable. The session's host answers joiners (the
// session's description, its players, teams and data) and gives out player
// ids; teams and data changes go from whoever makes them to everyone.
namespace sdlfe {

struct NetOptions {
  std::string bind = "0.0.0.0";  // 127.0.0.1 in the tests (no firewall prompt)
  uint16_t port = 0;             // 0: the system picks (the tests); a host's default kDefaultPort
  // The loss and lag shim (ours, for the tests): a share of the unreliable
  // messages dropped, every message held this long (MW3_NET_LOSS, MW3_NET_DELAY).
  double loss = 0;
  uint32_t delay_ms = 0;
  // Called each round while a join or a player's creation waits for the
  // host (ours: the tests run their other machines in the same process).
  std::function<void()> while_waiting;
};
constexpr uint16_t kDefaultPort = 27930;

class EnetTransport : public znet::Transport {
 public:
  explicit EnetTransport(NetOptions o = {});
  ~EnetTransport() override;
  bool ok() const { return host_ != nullptr; }  // the ENet host made (its port bound)
  uint16_t port() const;                       // the bound port

  bool host(const znet::SessionDesc& desc) override;
  bool join(const std::string& address) override;  // "host[:port]"
  uint32_t create_player(const std::string& short_name, const std::string& long_name) override;
  znet::SessionDesc session_desc() const override;
  void close() override;
  bool send(uint32_t to, const void* data, size_t size, bool guaranteed, uint32_t* id) override;
  bool receive(znet::Packet* out) override;
  bool async_sends() const override { return true; }
  bool is_host(uint32_t id) const override;
  uint32_t create_group(const std::string& name) override;
  bool destroy_group(uint32_t group) override;
  bool add_to_group(uint32_t group, uint32_t player) override;
  bool delete_from_group(uint32_t group, uint32_t player) override;
  bool destroy_player(uint32_t player) override;
  bool players(std::vector<Member>* out) override;
  bool groups(std::vector<Member>* out) override;
  bool group_players(uint32_t group, std::vector<uint32_t>* out) override;
  bool set_data(bool group, uint32_t id, const void* data, uint32_t size) override;
  bool get_data(bool group, uint32_t id, void* data, uint32_t* size) override;

  // Ours: the network's work between polls (receive() does it too); waits
  // up to `ms` for something to arrive.
  void service(uint32_t ms = 0);

  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
  void* host_ = nullptr;  // ENetHost*
};

// LAN discovery (ours): a host answers probes on a UDP port with its
// session; a browser asks every machine on the LAN (and any address given)
// and collects the answers.
struct FoundSession {
  std::string name, address;  // address "ip:port" to join
  uint32_t players = 0, max_players = 0;
};
constexpr uint16_t kDiscoveryPort = 27931;
class DiscoveryAnswer {  // the host's side, polled each frame
 public:
  DiscoveryAnswer(uint16_t port, const std::string& bind = "0.0.0.0");
  ~DiscoveryAnswer();
  bool ok() const { return socket_ >= 0; }
  uint16_t port() const { return port_; }
  void poll(const znet::SessionDesc& desc, uint32_t players, uint16_t game_port);

 private:
  intptr_t socket_ = -1;
  uint16_t port_ = 0;
};
// Probes for `ms` milliseconds: the LAN's broadcast on `port`, and each of
// `direct` ("ip:port" of a discovery port).
std::vector<FoundSession> browse(uint32_t ms, uint16_t port = kDiscoveryPort,
                                 const std::vector<std::string>& direct = {});

}  // namespace sdlfe
