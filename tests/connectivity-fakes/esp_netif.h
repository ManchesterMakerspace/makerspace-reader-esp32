#pragma once
#include "Arduino.h"
using esp_err_t = int;
constexpr int ESP_OK=0, ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED=1,
              ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED=2;
struct esp_netif_t {};
struct esp_netif_ip_info_t { uint32_t ip, gw, netmask; };
extern esp_netif_t ethernetNetif;
extern int dhcpStopResult, dhcpStartResult, setIpResult;
extern bool sharedDns;
extern uint32_t ethernetAddress;
inline int esp_netif_dhcpc_stop(esp_netif_t *n) {
  assert(n==&ethernetNetif); calls.push_back("dhcp-stop"); return dhcpStopResult;
}
inline int esp_netif_set_ip_info(esp_netif_t *n,esp_netif_ip_info_t *i) {
  assert(n==&ethernetNetif && !i->ip && !i->gw && !i->netmask);
  ethernetAddress=0;
  calls.push_back("ip-reset"); return setIpResult;
}
inline int esp_netif_dhcpc_start(esp_netif_t *n) {
  assert(n==&ethernetNetif); calls.push_back("dhcp"); return dhcpStartResult;
}
