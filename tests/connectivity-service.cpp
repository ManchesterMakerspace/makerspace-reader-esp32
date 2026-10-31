#include "connectivity-fakes/Reader.h"
#include "connectivity-fakes/ETH.h"
#include "connectivity-fakes/WiFi.h"
#include <algorithm>
#include <iostream>
uint32_t testNow=1000;
bool entropy=true;
std::vector<std::string> calls;
std::atomic<bool> refreshNeeded(false), apiOK(true);
esp_netif_t ethernetNetif;
int dhcpStopResult=ESP_OK, dhcpStartResult=ESP_OK, setIpResult=ESP_OK;
bool sharedDns=true;
uint32_t ethernetAddress=1;
Ethernet ETH;
Wireless WiFi;
#include "../.test-build/Connectivity.cpp"
static bool called(const char *call) { return std::find(calls.begin(),calls.end(),call)!=calls.end(); }
static void tick(uint32_t elapsed=1000) { calls.clear(); testNow+=elapsed; serviceConnectivity(); }
int main() {
  connectivitySetup();
  assert(called("eth-start") && called("eth-end"));
#ifdef TEST_WIFI
  assert(called("entropy-off") && called("wifi-on") && called("wifi-begin"));
  assert(wifiRadioActive() && !networkHasIP() && !entropy);
  WiFi.connected=true; WiFi.STA.ip=true;
  tick();
  assert(networkHasIP() && called("wifi-default") && refreshNeeded && !apiOK);
  assert(std::string(networkTransport())=="wifi" && networkAddress()=="192.0.2.1");
#else
  assert(!called("wifi-on") && !wifiRadioActive() && entropy && !networkHasIP());
#endif
  ETH.available=true;
  tick(30000);
  assert(called("eth-start"));
  ETH.link=true;
  tick();
  assert(!called("dhcp"));
  tick(30000);
  assert(called("dhcp-stop") && called("ip-reset") && called("dhcp") && sharedDns);
  dhcpStopResult=-1;
  tick(30000);
  assert(called("dhcp-stop") && !called("dhcp") && sharedDns);
  dhcpStopResult=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED;
  tick(30000);
  assert(called("dhcp") && sharedDns);
  setIpResult=-1;
  tick(30000);
  assert(!called("dhcp") && sharedDns);
  setIpResult=ESP_OK; dhcpStopResult=ESP_OK;
  ETH.ip=true; // A stale GOT_IP bit with a reset address is not usable Ethernet.
  assert(ethernetAddress==0);
  tick();
  assert(!called("eth-default") && !called("wifi-off"));
  ethernetAddress=1; ETH.defaultOK=false;
  tick();
  assert(!called("wifi-off")); // Do not drop the working path if route selection fails.
#ifdef TEST_WIFI
  assert(std::string(networkTransport())=="wifi"); // Statistics follow the selected route.
#else
  assert(std::string(networkTransport())=="offline");
#endif
  ETH.defaultOK=true;
#ifdef TEST_WIFI
  WiFi.stopOK=false;
  tick();
  assert(wifiRadioActive() && !entropy && called("wifi-off"));
  WiFi.stopOK=true;
#endif
  tick();
  assert(networkHasIP() && !wifiRadioActive() && entropy);
  assert(std::string(networkTransport())=="ethernet");
#ifdef TEST_WIFI
  auto route=std::find(calls.begin(),calls.end(),"eth-default");
  auto off=std::find(calls.begin(),calls.end(),"wifi-off");
  auto random=std::find(calls.begin(),calls.end(),"entropy-on");
  assert(route < off && off < random);
  assert(!WiFi.on && !WiFi.reconnect);
  calls.clear(); startWifi(); // A newly arrived Ethernet lease cancels a stale fallback action.
  assert(!called("wifi-on") && !wifiRadioActive() && entropy);
  ETH.link=false; // Even with a stale Ethernet IP, loss of link starts fallback.
  tick();
  assert(called("wifi-begin") && wifiRadioActive() && !networkHasIP());
  ETH.link=true;
  tick();
  assert(!wifiRadioActive() && entropy);
  ETH.link=false; WiFi.startOK=false;
  tick();
  assert(called("wifi-on") && called("wifi-off") && !wifiRadioActive() && entropy);
  WiFi.startOK=true;
  tick(30000);
  assert(called("wifi-begin") && wifiRadioActive() && !entropy);
#endif
  std::cout << "Production connectivity routing, retries and radio/entropy handoff passed\n";
}
