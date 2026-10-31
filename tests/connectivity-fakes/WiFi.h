#pragma once
#include "Arduino.h"
constexpr int WIFI_OFF=0, WIFI_STA=1, WL_CONNECTED=3;
struct Wireless {
  bool on=false, connected=false, startOK=true, stopOK=true, persistentOn=true, reconnect=false;
  struct Station {
    bool ip=false;
    bool hasIP() { return ip; }
    bool setDefault() { calls.push_back("wifi-default"); return true; }
  } STA;
  void persistent(bool p) { persistentOn=p; }
  void setAutoReconnect(bool r) { reconnect=r; }
  bool mode(int m) {
    assert(!entropy);
    calls.push_back(m == WIFI_OFF ? "wifi-off" : "wifi-on");
    if (m == WIFI_OFF ? !stopOK : !startOK) return false;
    on=m != WIFI_OFF;
    if (!on) { connected=false; STA.ip=false; }
    return true;
  }
  void begin(const char *,const char *) { assert(on && !persistentOn); calls.push_back("wifi-begin"); }
  void disconnect(bool off,bool erase) { assert(!off && !erase); connected=false; calls.push_back("wifi-disconnect"); }
  int status() { return connected ? WL_CONNECTED : 0; }
  IPAddress localIP() { return IPAddress(STA.ip ? 1U : 0U); }
};
extern Wireless WiFi;
