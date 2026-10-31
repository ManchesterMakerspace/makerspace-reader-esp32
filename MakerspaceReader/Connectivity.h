#pragma once
#include <Arduino.h>
void connectivitySetup();
void serviceConnectivity();
bool networkHasIP();
bool wifiRadioActive();
String networkAddress();
const char *networkTransport();
