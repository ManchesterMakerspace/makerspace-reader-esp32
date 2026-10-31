#pragma once
#include "Arduino.h"
#include "esp_netif.h"
constexpr int ETH_PHY_W5500 = 0;
struct Ethernet {
  bool available=false, link=false, ip=false, defaultOK=true;
  bool begin(int,int,int,int,int,int) { calls.push_back("eth-start"); return available; }
  void end() { calls.push_back("eth-end"); }
  bool linkUp() { return link; }
  bool hasIP() { return ip; }
  IPAddress localIP() { return IPAddress(ip ? ethernetAddress : 0U); }
  esp_netif_t *netif() { return &ethernetNetif; }
  bool config(IPAddress,IPAddress,IPAddress) { sharedDns=false; return true; }
  bool setDefault() { calls.push_back("eth-default"); return defaultOK; }
};
extern Ethernet ETH;
