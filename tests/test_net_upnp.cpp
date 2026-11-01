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
#endif
