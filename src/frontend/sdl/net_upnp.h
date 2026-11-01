#pragma once
#include <cstdint>
#include <memory>
#include <string>

// Internet hosting at Owen's "level 1, between friends" (ours, not original
// code: docs/multiplayer-port.md, piece 9c). The router is found over UPnP
// (miniupnpc) as the multiplayer screen opens and asked for its public
// address; the game's UDP port is mapped on it while hosting, and the
// mapping removed when hosting ends. No router, a refusal or an address the
// internet cannot reach fall back quietly: LAN games still work. The work
// runs on a thread of its own: a router may take seconds, or never answer.
namespace sdlfe {

struct UpnpStatus {
  enum State {
    kLooking,   // still asking
    kNoRouter,  // no UPnP router answered
    kReady,     // a router, its public address known; nothing mapped yet
    kMapped,    // the port is open on it
    kRefused,   // the router would not map it
    kBlocked,   // its "public" address is carrier-grade NAT (100.64/10): the provider's, nobody reaches us
    kDoubleNat, // its "public" address is private: another router in front; the port mapped on ours when hosting
  };
  State state = kLooking;
  std::string lan_ip;     // our address on the LAN (the one the router sees)
  std::string public_ip;  // the router's external address
};

// An address the internet cannot reach: private (10/8, 172.16/12,
// 192.168/16), carrier-grade NAT (100.64/10), loopback, link-local or none.
bool unreachable_address(const std::string& ip);
// This computer's address on its LAN (the interface a packet to the
// internet would leave by; nothing is sent), "" when it has none.
std::string local_lan_address();

class PortMapper {
 public:
  // `port` the game's UDP port. `description_url` skips the discovery (the
  // tests' fake router); empty: SSDP on the LAN, waiting `discover_ms`.
  PortMapper(uint16_t port, std::string description_url = "", int discover_ms = 2000);
  // The mapping removed, best effort: a router still busy after `quit_ms`
  // (discovery, or a SOAP call it stalls) is left to the worker, which
  // finishes on its own; the game's close does not wait on it.
  ~PortMapper();
  void map();     // hosting: the port opened (when a router is ready)
  void unmap();   // hosting ended: the mapping removed
  UpnpStatus status() const;
  bool idle() const;  // nothing asked of it is still being done (the tests)
  static constexpr int quit_ms = 1000;

  struct Impl;

 private:
  std::shared_ptr<Impl> impl_;  // shared with its worker, which may outlive us
};

}  // namespace sdlfe
