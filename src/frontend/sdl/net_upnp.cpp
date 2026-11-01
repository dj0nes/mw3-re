#include "frontend/sdl/net_upnp.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <miniupnpc.h>
#include <upnpcommands.h>

namespace sdlfe {

bool unreachable_address(const std::string& ip) {
  unsigned a = 0, b = 0, c = 0, d = 0;
  if (std::sscanf(ip.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 || b > 255 || c > 255 || d > 255) return true;
  if (a == 0 || a == 10 || a == 127) return true;
  if (a == 100 && b >= 64 && b <= 127) return true;  // carrier-grade NAT
  if (a == 169 && b == 254) return true;
  if (a == 172 && b >= 16 && b <= 31) return true;
  if (a == 192 && b == 168) return true;
  return false;
}

std::string local_lan_address() {
#if defined(_WIN32)
  static const bool up = [] {
    WSADATA d;
    return WSAStartup(MAKEWORD(2, 2), &d) == 0;
  }();
  if (!up) return "";
  SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s == INVALID_SOCKET) return "";
#else
  const int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return "";
#endif
  sockaddr_in to{};
  to.sin_family = AF_INET, to.sin_port = htons(53);
  inet_pton(AF_INET, "203.0.113.1", &to.sin_addr);  // a documentation address: the route is all that is asked
  std::string ip;
  if (connect(s, reinterpret_cast<sockaddr*>(&to), sizeof to) == 0) {
    sockaddr_in me{};
    socklen_t n = sizeof me;
    char buf[INET_ADDRSTRLEN] = {};
    if (getsockname(s, reinterpret_cast<sockaddr*>(&me), &n) == 0 && inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf))
      ip = buf;
  }
#if defined(_WIN32)
  closesocket(s);
#else
  close(s);
#endif
  return ip == "0.0.0.0" ? "" : ip;
}

struct PortMapper::Impl {
  uint16_t port;
  std::string url;
  int discover_ms;
  mutable std::mutex mu;
  std::condition_variable cv;
  std::deque<int> asks;  // 1 map, 2 unmap, 3 stop
  bool busy = true;
  UpnpStatus st;
  UPNPUrls urls{};
  IGDdatas data{};
  bool have_igd = false, mapped = false;
  bool done = false;  // the worker has finished
  std::thread worker;

  void set(UpnpStatus::State s) {
    std::lock_guard<std::mutex> l(mu);
    st.state = s;
  }
  void find() {
    char lan[64] = {}, wan[64] = {};
    int found = 0;
    if (!url.empty()) {
      found = UPNP_GetIGDFromUrl(url.c_str(), &urls, &data, lan, sizeof lan);
    } else {
      int err = 0;
      UPNPDev* devs = upnpDiscover(discover_ms, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, 2, &err);
      if (devs) {
        found = UPNP_GetValidIGD(devs, &urls, &data, lan, sizeof lan, wan, sizeof wan) != 0;
        freeUPNPDevlist(devs);
      }
    }
    if (!found) {
      set(UpnpStatus::kNoRouter);
      return;
    }
    have_igd = true;
    char ext[64] = {};
    const bool ok = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, ext) == UPNPCOMMAND_SUCCESS;
    std::lock_guard<std::mutex> l(mu);
    st.lan_ip = lan;
    st.public_ip = ok ? ext : "";
    unsigned a = 0, b = 0;
    const bool cgnat = ok && std::sscanf(ext, "%u.%u", &a, &b) == 2 && a == 100 && b >= 64 && b <= 127;
    st.state = !ok ? UpnpStatus::kReady
               : cgnat ? UpnpStatus::kBlocked
               : unreachable_address(st.public_ip) ? UpnpStatus::kDoubleNat
                                                   : UpnpStatus::kReady;
  }
  void do_map() {
    UpnpStatus::State now;
    {
      std::lock_guard<std::mutex> l(mu);
      now = st.state;
    }
    if (!have_igd || mapped || now == UpnpStatus::kBlocked) return;  // a mapping behind carrier-grade NAT helps nobody
    const std::string p = std::to_string(port);
    std::string lan;
    {
      std::lock_guard<std::mutex> l(mu);
      lan = st.lan_ip;
    }
    const int r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, p.c_str(), p.c_str(), lan.c_str(),
                                      "MechWarrior 3", "UDP", nullptr, "0");
    mapped = r == UPNPCOMMAND_SUCCESS;
    if (now != UpnpStatus::kDoubleNat) set(mapped ? UpnpStatus::kMapped : UpnpStatus::kRefused);  // the outer router still waits
  }
  void do_unmap() {
    if (!have_igd || !mapped) return;
    const std::string p = std::to_string(port);
    UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, p.c_str(), "UDP", nullptr);
    mapped = false;
    std::lock_guard<std::mutex> l(mu);
    if (st.state == UpnpStatus::kMapped) st.state = UpnpStatus::kReady;
  }
  void run() {
    find();
    for (;;) {
      int ask;
      {
        std::unique_lock<std::mutex> l(mu);
        busy = false;
        cv.wait(l, [&] { return !asks.empty(); });
        ask = asks.front();
        asks.pop_front();
        busy = true;
      }
      if (ask == 1) do_map();
      else if (ask == 2) do_unmap();
      else break;
    }
    do_unmap();
    if (have_igd) FreeUPNPUrls(&urls);
    std::lock_guard<std::mutex> l(mu);
    done = true;
    cv.notify_all();
  }
  void post(int ask) {
    std::lock_guard<std::mutex> l(mu);
    asks.push_back(ask);
    busy = true;
    cv.notify_all();
  }
};

PortMapper::PortMapper(uint16_t port, std::string description_url, int discover_ms) : impl_(std::make_shared<Impl>()) {
  impl_->port = port;
  impl_->url = std::move(description_url);
  impl_->discover_ms = discover_ms;
  std::shared_ptr<Impl> m = impl_;
  impl_->worker = std::thread([m] { m->run(); });
}

// The lead's review of 9c: quitting must not hang on the router.
PortMapper::~PortMapper() {
  impl_->post(3);
  bool finished;
  {
    std::unique_lock<std::mutex> l(impl_->mu);
    finished = impl_->cv.wait_for(l, std::chrono::milliseconds(quit_ms), [this] { return impl_->done; });
  }
  if (!impl_->worker.joinable()) return;
  if (finished) impl_->worker.join();
  else impl_->worker.detach();  // it holds the Impl; the router's timeouts end it
}

void PortMapper::map() { impl_->post(1); }
void PortMapper::unmap() { impl_->post(2); }

UpnpStatus PortMapper::status() const {
  std::lock_guard<std::mutex> l(impl_->mu);
  return impl_->st;
}

bool PortMapper::idle() const {
  std::lock_guard<std::mutex> l(impl_->mu);
  return !impl_->busy && impl_->asks.empty();
}

}  // namespace sdlfe
