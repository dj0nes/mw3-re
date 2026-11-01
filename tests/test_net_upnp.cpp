// Internet hosting's UPnP (docs/multiplayer-port.md, piece 9c) against a
// fake router on loopback: its description, GetExternalIPAddress,
// AddPortMapping and DeletePortMapping over HTTP and SOAP, as miniupnpc asks
// them. POSIX sockets: not on Windows.
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "frontend/sdl/net_upnp.h"

namespace {

// The router: answers until stopped; what it was asked kept as lines.
struct FakeRouter {
  std::string external = "203.0.113.7";
  bool refuse = false;
  std::atomic<bool> stall{false};  // SOAP calls held unanswered until stopped
  int listener = -1;
  uint16_t port = 0;
  std::atomic<bool> stop{false};
  std::thread thread;
  std::mutex mu;
  std::vector<std::string> asked;

  FakeRouter() {
    listener = socket(AF_INET, SOCK_STREAM, 0);
    int on = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in a{};
    a.sin_family = AF_INET, a.sin_addr.s_addr = htonl(INADDR_LOOPBACK), a.sin_port = 0;
    bind(listener, reinterpret_cast<sockaddr*>(&a), sizeof a);
    listen(listener, 8);
    socklen_t n = sizeof a;
    getsockname(listener, reinterpret_cast<sockaddr*>(&a), &n);
    port = ntohs(a.sin_port);
    thread = std::thread([this] { serve(); });
  }
  ~FakeRouter() {
    stop = true;
    thread.join();
    close(listener);
  }
  std::string url() const { return "http://127.0.0.1:" + std::to_string(port) + "/rootDesc.xml"; }
  std::vector<std::string> got() {
    std::lock_guard<std::mutex> l(mu);
    return asked;
  }
  static std::string tag(const std::string& body, const char* name) {
    const std::string open = std::string("<") + name + ">";
    const size_t a = body.find(open);
    if (a == std::string::npos) return "";
    const size_t b = body.find("</", a);
    return body.substr(a + open.size(), b - a - open.size());
  }
  void serve() {
    while (!stop) {
      pollfd pf{listener, POLLIN, 0};
      if (poll(&pf, 1, 100) <= 0) continue;  // 0.1 s, to see the stop
      const int c = accept(listener, nullptr, nullptr);
      if (c < 0) continue;
      timeval tv{2, 0};
      setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
      std::string req;
      char buf[4096];
      size_t need = std::string::npos;
      for (;;) {
        const ssize_t k = recv(c, buf, sizeof buf, 0);
        if (k <= 0) break;
        req.append(buf, static_cast<size_t>(k));
        const size_t h = req.find("\r\n\r\n");
        if (h != std::string::npos && need == std::string::npos) {
          const size_t cl = req.find("Content-Length:");
          need = h + 4 + (cl == std::string::npos || cl > h ? 0 : std::strtoul(req.c_str() + cl + 15, nullptr, 10));
        }
        if (need != std::string::npos && req.size() >= need) break;
      }
      if (stall && req.find("SOAPAction") != std::string::npos) {
        while (!stop) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        close(c);
        continue;
      }
      std::string status = "200 OK", body;
      const std::string soap_head =
          "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
          "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body>";
      const std::string soap_tail = "</s:Body></s:Envelope>";
      const std::string svc = "urn:schemas-upnp-org:service:WANIPConnection:1";
      if (req.rfind("GET /rootDesc.xml", 0) == 0) {
        body =
            "<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major>"
            "<minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:InternetGatewayDevice:1"
            "</deviceType><friendlyName>Fake router</friendlyName><deviceList><device><deviceType>"
            "urn:schemas-upnp-org:device:WANDevice:1</deviceType><serviceList><service><serviceType>"
            "urn:schemas-upnp-org:service:WANCommonInterfaceConfig:1</serviceType><serviceId>"
            "urn:upnp-org:serviceId:WANCommonIFC1</serviceId><controlURL>/ctl/CmnIfCfg</controlURL><eventSubURL>"
            "/evt/CmnIfCfg</eventSubURL><SCPDURL>/WANCfg.xml</SCPDURL></service></serviceList><deviceList><device>"
            "<deviceType>urn:schemas-upnp-org:device:WANConnectionDevice:1</deviceType><serviceList><service>"
            "<serviceType>" + svc + "</serviceType><serviceId>urn:upnp-org:serviceId:WANIPConn1</serviceId>"
            "<controlURL>/ctl/IPConn</controlURL><eventSubURL>/evt/IPConn</eventSubURL><SCPDURL>/WANIPCn.xml"
            "</SCPDURL></service></serviceList></device></deviceList></device></deviceList></device></root>";
      } else if (req.find("#GetExternalIPAddress") != std::string::npos) {
        body = soap_head + "<u:GetExternalIPAddressResponse xmlns:u=\"" + svc + "\"><NewExternalIPAddress>" + external +
               "</NewExternalIPAddress></u:GetExternalIPAddressResponse>" + soap_tail;
      } else if (req.find("#AddPortMapping") != std::string::npos) {
        std::lock_guard<std::mutex> l(mu);
        asked.push_back("add " + tag(req, "NewProtocol") + " " + tag(req, "NewExternalPort") + " to " +
                        tag(req, "NewInternalClient") + ":" + tag(req, "NewInternalPort"));
        if (refuse) {
          status = "500 Internal Server Error";
          body = soap_head + "<s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail>"
                 "<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>718</errorCode>"
                 "<errorDescription>ConflictInMappingEntry</errorDescription></UPnPError></detail></s:Fault>" +
                 soap_tail;
        } else {
          body = soap_head + "<u:AddPortMappingResponse xmlns:u=\"" + svc + "\"/>" + soap_tail;
        }
      } else if (req.find("#DeletePortMapping") != std::string::npos) {
        std::lock_guard<std::mutex> l(mu);
        asked.push_back("delete " + tag(req, "NewProtocol") + " " + tag(req, "NewExternalPort"));
        body = soap_head + "<u:DeletePortMappingResponse xmlns:u=\"" + svc + "\"/>" + soap_tail;
      } else {
        status = "404 Not Found";
      }
      const std::string out = "HTTP/1.1 " + status + "\r\nContent-Type: text/xml\r\nContent-Length: " +
                              std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
      send(c, out.data(), out.size(), 0);
      close(c);
    }
  }
};

// Until the mapper has finished what it was asked (5 s at most).
void settle(const sdlfe::PortMapper& m) {
  for (int i = 0; i < 500 && (m.status().state == sdlfe::UpnpStatus::kLooking || !m.idle()); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

}  // namespace

// The public addresses the internet cannot reach (the lead: say so, do not
// offer them to share).
TEST(net_upnp_unreachable_addresses) {
  for (const char* ip : {"10.0.0.5", "100.64.0.1", "100.127.255.254", "172.16.0.1", "172.31.9.9", "192.168.1.1",
                         "127.0.0.1", "169.254.3.3", "0.0.0.0", "", "not an address"})
    EXPECT(sdlfe::unreachable_address(ip));
  for (const char* ip : {"203.0.113.7", "100.63.255.255", "100.128.0.1", "172.15.0.1", "172.32.0.1", "8.8.8.8"})
    EXPECT(!sdlfe::unreachable_address(ip));
}

// A router that maps: ready with its public address, the port mapped on
// map() (UDP, to our LAN address), removed on unmap() and when the mapper
// goes (hosting ended, or the game closed).
TEST(net_upnp_maps_and_removes_the_port) {
  FakeRouter r;
  {
    sdlfe::PortMapper m(27930, r.url());
    settle(m);
    sdlfe::UpnpStatus s = m.status();
    EXPECT(s.state == sdlfe::UpnpStatus::kReady && s.public_ip == "203.0.113.7" && s.lan_ip == "127.0.0.1");
    EXPECT(r.got().empty());  // nothing mapped before hosting
    m.map();
    settle(m);
    EXPECT(m.status().state == sdlfe::UpnpStatus::kMapped);
    m.unmap();
    settle(m);
    EXPECT(m.status().state == sdlfe::UpnpStatus::kReady);
    m.map();
    settle(m);
  }
  const std::vector<std::string> g = r.got();
  EXPECT(g.size() == 4 && g[0] == "add UDP 27930 to 127.0.0.1:27930" && g[1] == "delete UDP 27930" &&
         g[2] == "add UDP 27930 to 127.0.0.1:27930" && g[3] == "delete UDP 27930");
  for (const std::string& line : g) std::printf("  the router was asked: %s\n", line.c_str());
}

// A router that refuses, one behind carrier-grade NAT (nothing asked of it:
// a mapping there reaches nobody), one behind another router (its "public"
// address private: the port mapped on it, the outer router left to the
// player), and none at all: each falls back quietly.
TEST(net_upnp_falls_back_quietly) {
  {
    FakeRouter r;
    r.refuse = true;
    sdlfe::PortMapper m(27930, r.url());
    settle(m);
    m.map();
    settle(m);
    EXPECT(m.status().state == sdlfe::UpnpStatus::kRefused);
  }
  {
    FakeRouter r;
    r.external = "100.72.4.1";
    sdlfe::PortMapper m(27930, r.url());
    settle(m);
    EXPECT(m.status().state == sdlfe::UpnpStatus::kBlocked && m.status().public_ip == "100.72.4.1");
    m.map();
    settle(m);
    EXPECT(m.status().state == sdlfe::UpnpStatus::kBlocked && r.got().empty());
  }
  {
    FakeRouter r;
    r.external = "192.168.1.20";
    sdlfe::PortMapper m(27930, r.url());
    settle(m);
    EXPECT(m.status().state == sdlfe::UpnpStatus::kDoubleNat);
    m.map();
    settle(m);
    EXPECT(m.status().state == sdlfe::UpnpStatus::kDoubleNat && r.got().size() == 1);
  }
  {
    int port;
    {
      FakeRouter gone;  // a port with nothing listening on it after
      port = gone.port;
    }
    const auto t0 = std::chrono::steady_clock::now();
    sdlfe::PortMapper m(27930, "http://127.0.0.1:" + std::to_string(port) + "/rootDesc.xml");
    settle(m);
    m.map();
    settle(m);
    EXPECT(m.status().state == sdlfe::UpnpStatus::kNoRouter);
    EXPECT(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5));
  }
}
// A router that stalls a SOAP call (the lead's review of 9c): quitting the
// game does not wait on it; the mapper's end is bounded to about a second,
// and the worker left behind finishes once the router lets go.
TEST(net_upnp_quit_does_not_wait_on_a_stalled_router) {
  FakeRouter r;
  std::chrono::steady_clock::duration took{};
  {
    auto m = std::make_unique<sdlfe::PortMapper>(27930, r.url());
    settle(*m);
    EXPECT(m->status().state == sdlfe::UpnpStatus::kReady);
    r.stall = true;
    m->map();  // AddPortMapping, held unanswered
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    EXPECT(!m->idle());
    const auto t0 = std::chrono::steady_clock::now();
    m.reset();
    took = std::chrono::steady_clock::now() - t0;
  }
  std::printf("  the mapper's end took %lld ms\n",
              static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(took).count()));
  EXPECT(took < std::chrono::milliseconds(sdlfe::PortMapper::quit_ms + 500));
}
#endif

// The lead's real-router check (MW3_UPNP_REAL=1 MW3_TEST=net_upnp_real_router;
// skipped without it): the mapper finds the LAN's router, maps UDP 27930, the
// mapping is read back from the router itself (GetSpecificPortMappingEntry,
// a look of our own), then removed and looked for again. It prints the state
// and the external address's class, never the address. Runs anywhere, no
// window: over ssh on the PC as well as here.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>

#include "check.h"
#include "frontend/sdl/net_upnp.h"

namespace {
const char* state_name(sdlfe::UpnpStatus::State s) {
  switch (s) {
    case sdlfe::UpnpStatus::kLooking: return "still looking";
    case sdlfe::UpnpStatus::kNoRouter: return "no UPnP router answered";
    case sdlfe::UpnpStatus::kReady: return "router found, nothing mapped";
    case sdlfe::UpnpStatus::kMapped: return "mapped";
    case sdlfe::UpnpStatus::kRefused: return "the router refused the mapping";
    case sdlfe::UpnpStatus::kBlocked: return "carrier-grade NAT (100.64/10)";
    case sdlfe::UpnpStatus::kDoubleNat: return "another router in front (a private external address)";
  }
  return "?";
}
const char* address_class(const std::string& ip) {
  unsigned a = 0, b = 0;
  if (ip.empty() || std::sscanf(ip.c_str(), "%u.%u", &a, &b) != 2) return "none given";
  if (a == 100 && b >= 64 && b <= 127) return "carrier-grade NAT";
  if (sdlfe::unreachable_address(ip)) return "private";
  return "public";
}
// Until the mapper has done what it was asked, `ms` at most.
void wait_idle(const sdlfe::PortMapper& m, int ms) {
  for (int i = 0; i < ms / 20 && (m.status().state == sdlfe::UpnpStatus::kLooking || !m.idle()); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}
}  // namespace

TEST(net_upnp_real_router) {
  const char* on = std::getenv("MW3_UPNP_REAL");
  if (!on || std::strcmp(on, "1") != 0) return;
  const uint16_t port = 27930;
  const std::string route_lan = sdlfe::local_lan_address();  // (Winsock up on Windows)
  std::printf("  this computer's LAN address: %s\n", route_lan.empty() ? "none" : "found");
  sdlfe::PortMapper m(port, "", 3000);
  wait_idle(m, 10000);
  sdlfe::UpnpStatus s = m.status();
  std::printf("  found: %s; external address: %s\n", state_name(s.state), address_class(s.public_ip));
  EXPECT(s.state != sdlfe::UpnpStatus::kLooking && s.state != sdlfe::UpnpStatus::kNoRouter);
  if (s.state == sdlfe::UpnpStatus::kLooking || s.state == sdlfe::UpnpStatus::kNoRouter) return;
  // Our own look at the router, to read the mapping back.
  int err = 0;
  UPNPUrls urls{};
  IGDdatas data{};
  char lan[64] = {}, wan[64] = {};
  UPNPDev* devs = upnpDiscover(3000, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &err);
  const int igd = devs ? UPNP_GetValidIGD(devs, &urls, &data, lan, sizeof lan, wan, sizeof wan) : 0;
  if (devs) freeUPNPDevlist(devs);
  EXPECT(igd != 0);
  if (!igd) return;
  const std::string p = std::to_string(port);
  auto look = [&](char* client, char* iport, char* desc) {
    char enabled[8] = {}, lease[16] = {};
    return UPNP_GetSpecificPortMappingEntry(urls.controlURL, data.first.servicetype, p.c_str(), "UDP", nullptr, client,
                                           iport, desc, enabled, lease);
  };
  char client[40] = {}, iport[8] = {}, desc[96] = {};
  const int before = look(client, iport, desc);
  std::printf("  before: %s\n", before == UPNPCOMMAND_SUCCESS ? "UDP 27930 already mapped (by something else?)" : "UDP 27930 not mapped");
  if (s.state == sdlfe::UpnpStatus::kBlocked) {
    std::printf("  carrier-grade NAT: the mapper asks nothing of the router (as designed); done\n");
    FreeUPNPUrls(&urls);
    return;
  }
  m.map();
  wait_idle(m, 10000);
  s = m.status();
  std::printf("  after map(): %s\n", state_name(s.state));
  std::memset(client, 0, sizeof client), std::memset(iport, 0, sizeof iport), std::memset(desc, 0, sizeof desc);
  const int mapped = look(client, iport, desc);
  const bool ours = mapped == UPNPCOMMAND_SUCCESS && std::string(client) == s.lan_ip && std::string(iport) == p;
  std::printf("  the router's entry: %s%s%s\n",
              mapped == UPNPCOMMAND_SUCCESS ? (ours ? "to this computer, port 27930" : "to another address or port")
                                            : strupnperror(mapped) ? strupnperror(mapped) : "none",
              mapped == UPNPCOMMAND_SUCCESS ? ", described as " : "", mapped == UPNPCOMMAND_SUCCESS ? desc : "");
  EXPECT(ours && std::string(desc) == "MechWarrior 3");
  m.unmap();
  wait_idle(m, 10000);
  const int after = look(client, iport, desc);
  std::printf("  after unmap(): %s; the router's entry %s\n", state_name(m.status().state),
              after == UPNPCOMMAND_SUCCESS ? "still there" : "gone");
  EXPECT(after != UPNPCOMMAND_SUCCESS);
  FreeUPNPUrls(&urls);
}
