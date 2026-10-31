#include "Reader.h"
#include "Connectivity.h"
#include "ConnectivityPolicy.h"
#include "secrets.h"
#include <ETH.h>
#include <SPI.h>
#include <esp_netif.h>
#include <bootloader_random.h>
#if defined(WIFI_SSID) != defined(WIFI_PSK)
#error "Define both WIFI_SSID and WIFI_PSK, or neither"
#endif
#if defined(WIFI_SSID) && defined(WIFI_PSK)
#include <WiFi.h>
static_assert(sizeof(WIFI_SSID)>1 && sizeof(WIFI_SSID)<=33, "WIFI_SSID must contain 1-32 bytes");
static_assert(sizeof(WIFI_PSK)>=9 && sizeof(WIFI_PSK)<=65, "WIFI_PSK must contain 8-63 characters or a 64-digit hex key");
constexpr bool wifiConfigured = true;
#else
constexpr bool wifiConfigured = false;
#endif
namespace {
std::atomic<bool> ethernetStarted(false);
std::atomic<bool> radio(false);
connectivity::Policy policy;
uint32_t lastCheck=0;
std::atomic<int> transport(0);
bool ethernetReady() {
  return ethernetStarted && ETH.linkUp() && ETH.hasIP() && uint32_t(ETH.localIP()) != 0;
}
bool wirelessReady() {
#if defined(WIFI_SSID) && defined(WIFI_PSK)
  return radio && WiFi.STA.hasIP() && WiFi.status()==WL_CONNECTED;
#else
  return false;
#endif
}
void startEthernet() {
  ethernetStarted=ETH.begin(ETH_PHY_W5500,1,14,10,9,SPI);
  if(!ethernetStarted) { ETH.end(); log_e("Ethernet initialization failed; retrying in 30 seconds"); }
}
void retryEthernetDhcp() {
  // ETH.config(0,0,0) clears the SDK's shared DNS servers, breaking Wi-Fi.
  // Restart only this DHCP client while leaving the working interface's DNS intact.
  if (ethernetReady()) return; // A DHCP reply may have arrived since the policy check.
  auto *netif = ETH.netif();
  if (!netif) return;
  esp_err_t rc = esp_netif_dhcpc_stop(netif);
  if (rc != ESP_OK && rc != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
    log_e("Ethernet DHCP stop failed");
    return;
  }
  esp_netif_ip_info_t empty = {};
  if (esp_netif_set_ip_info(netif, &empty) != ESP_OK) {
    log_e("Ethernet IP reset failed");
    return;
  }
  rc = esp_netif_dhcpc_start(netif);
  if (rc != ESP_OK && rc != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
    log_e("Ethernet DHCP restart failed");
}
void startWifi() {
#if defined(WIFI_SSID) && defined(WIFI_PSK)
  if (ethernetReady()) return; // Avoid starting RF if DHCP completed during this tick.
  if(!radio.exchange(true)) bootloader_random_disable();
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  if (!WiFi.mode(WIFI_STA)) {
    log_e("Wi-Fi station initialization failed");
    if (WiFi.mode(WIFI_OFF)) { bootloader_random_enable(); radio=false; }
    return;
  }
  WiFi.begin(WIFI_SSID,WIFI_PSK);
  log_i("Connecting Wi-Fi fallback");
#endif
}
void stopWifi() {
#if defined(WIFI_SSID) && defined(WIFI_PSK)
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false,false);
  if (WiFi.mode(WIFI_OFF)) {
    bootloader_random_enable();
    radio=false;
    log_i("Ethernet restored; Wi-Fi radio off");
  } else log_e("Wi-Fi shutdown failed; will retry");
#endif
}
}
void connectivitySetup() { startEthernet(); serviceConnectivity(); }
void serviceConnectivity() {
  uint32_t now=millis();
  if (lastCheck && uint32_t(now-lastCheck)<1000) return;
  lastCheck=now;
  bool ethernet=ethernetReady(), wireless=wirelessReady();
  auto action=policy.step(now,ethernetStarted,ethernetStarted && ETH.linkUp(),ethernet,wifiConfigured,radio,wireless);
  if(action.retryEthernet) startEthernet();
  if(action.retryDhcp) retryEthernetDhcp();
  if(action.startWifi) startWifi();
  int next=0;
  if(ethernet && ETH.setDefault()) { next=1; if(action.stopWifi) stopWifi(); }
#if defined(WIFI_SSID) && defined(WIFI_PSK)
  else if(wireless && WiFi.STA.setDefault()) next=2;
#endif
  if(next!=transport) {
    transport=next;refreshNeeded=true;apiOK=false;
    log_i("Network transport: %s", networkTransport());
  }
}
bool networkHasIP() { return ethernetReady() || wirelessReady(); }
bool wifiRadioActive() { return radio; }
String networkAddress() {
  if(transport == 1 && ethernetReady()) return ETH.localIP().toString();
#if defined(WIFI_SSID) && defined(WIFI_PSK)
  if(transport == 2 && wirelessReady()) return WiFi.localIP().toString();
#endif
  return "0.0.0.0";
}
const char *networkTransport() {
  if (transport == 1 && ethernetReady()) return "ethernet";
  if (transport == 2 && wirelessReady()) return "wifi";
  return "offline";
}
