#pragma once
#include <cstdint>
#include <cstring>

namespace connectivity {
constexpr uint32_t retryMs = 30000;
struct Actions {
  bool retryEthernet = false, retryDhcp = false, startWifi = false, stopWifi = false;
};
class Policy {
  bool linked = false, triedWifi = false;
  uint32_t linkAt = 0, ethRetry = 0, dhcpRetry = 0, wifiRetry = 0;
public:
  Actions step(uint32_t now, bool started, bool link, bool ethIP, bool configured,
               bool radio, bool wifiIP) {
    Actions a;
    if (link && !linked) { linkAt = now; dhcpRetry = now; }
    linked = link;
    if (!started && uint32_t(now - ethRetry) >= retryMs) {
      a.retryEthernet = true;
      ethRetry = now;
    }
    if (started && link && !ethIP && uint32_t(now - dhcpRetry) >= retryMs) {
      a.retryDhcp = true;
      dhcpRetry = now;
    }
    if (started && link && ethIP) {
      a.stopWifi = radio;
      triedWifi = false;
      return a;
    }
    bool fallback = !started || !link || uint32_t(now - linkAt) >= retryMs;
    if (configured && fallback && !wifiIP &&
        (!triedWifi || uint32_t(now - wifiRetry) >= retryMs)) {
      a.startWifi = true;
      triedWifi = true;
      wifiRetry = now;
    }
    return a;
  }
};
inline bool provisioningPath(const char *p) {
  // AsyncWebServer's default matcher accepts both /config and /config/....
  return !std::strcmp(p, "/config") || !std::strncmp(p, "/config/", 8) ||
         !std::strncmp(p, "/provision", 10) ||
         !std::strncmp(p, "/api/local/provision", 20) || !std::strncmp(p, "/recovery", 9) ||
         !std::strncmp(p, "/api/local/recovery", 19);
}
inline bool manualProvisioningAllowed(bool radio, bool ethernetReady, bool receivedOnEthernet) {
  return !radio && ethernetReady && receivedOnEthernet;
}
}
