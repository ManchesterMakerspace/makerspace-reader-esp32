#include "../MakerspaceReader/ConnectivityPolicy.h"
#include <cassert>
#include <iostream>
using namespace connectivity;
int main() {
 Policy p;
 auto a=p.step(0,false,false,false,true,false,false); assert(a.startWifi && !a.stopWifi);
 a=p.step(29999,false,false,false,true,true,false);assert(!a.startWifi && !a.retryEthernet);
 a=p.step(30000,false,false,false,true,true,false);assert(a.startWifi && a.retryEthernet);
 a=p.step(31000,true,true,false,true,true,true);assert(!a.stopWifi && !a.startWifi);
 a=p.step(61000,true,true,false,true,true,true);assert(a.retryDhcp && !a.stopWifi);
 a=p.step(62000,true,true,true,true,true,true);assert(a.stopWifi && !a.startWifi);
 a=p.step(63000,true,false,true,true,false,false);assert(a.startWifi); // stale IP is not link recovery
 Policy dhcp;
 assert(!dhcp.step(100,true,true,false,true,false,false).startWifi);
 assert(!dhcp.step(30099,true,true,false,true,false,false).startWifi);
 a=dhcp.step(30100,true,true,false,true,false,false);assert(a.startWifi && a.retryDhcp);
 Policy none;assert(!none.step(0,false,false,false,false,false,false).startWifi);
 assert(none.step(30000,false,false,false,false,false,false).retryEthernet);
 Policy wrap;
 wrap.step(UINT32_MAX-10000,true,true,false,true,false,false);
 assert(!wrap.step(10000,true,true,false,true,false,false).startWifi);
 assert(wrap.step(20000,true,true,false,true,false,false).startWifi);
 assert(provisioningPath("/config") && provisioningPath("/provision") && provisioningPath("/api/local/provisioning") && provisioningPath("/api/local/recovery"));
 assert(provisioningPath("/config/") && provisioningPath("/config/alternate"));
 assert(!provisioningPath("/configuration"));
 assert(!provisioningPath("/stats") && !provisioningPath("/update"));
 assert(!manualProvisioningAllowed(true,true,true)); // fail closed during handoff
 assert(!manualProvisioningAllowed(false,true,false)); // old Wi-Fi HTTP connection
 assert(!manualProvisioningAllowed(false,false,true));
 assert(manualProvisioningAllowed(false,true,true));
 std::cout<<"Ethernet priority, Wi-Fi fallback, DHCP retry, timer wrap and provisioning guards passed\n";
}
