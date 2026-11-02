#include "Reader.h"
#include "HardwareDiagnostics.h"
#include <Wire.h>
namespace {
struct Device { const char *name; uint8_t address; bool responding; int initialized; };
Device devices[] = {{"PN532",0x24,false,-1},{"PN7160",0x28,false,-1},
 {"Qwiic buzzer",0x34,false,-1},{"PCF8523 RTC",0x68,false,-1},{"SerLCD",0x72,false,-1}};
bool busInitialized=false;
portMUX_TYPE inventoryLock = portMUX_INITIALIZER_UNLOCKED;
}
void hardwareProbe(bool busReady) {
 busInitialized=busReady;
 pinMode(readerBoard::nfcReset, OUTPUT);
 digitalWrite(readerBoard::nfcReset, LOW); delay(2);
 digitalWrite(readerBoard::nfcReset, HIGH); delay(10);
 for (auto &device : devices) {
  bool responding=false;
  { Lock bus(i2cMutex);
   for (unsigned attempt=0; busReady && attempt<3 && !responding; ++attempt) {
    Wire.beginTransmission(device.address);
    responding=Wire.endTransmission()==0;
    if(!responding) delay(2);
   }
  }
  portENTER_CRITICAL(&inventoryLock); device.responding=responding; portEXIT_CRITICAL(&inventoryLock);
 }
 Serial.println(hardwareReport());
}
void hardwareResponding(uint8_t address,bool responding) {
 portENTER_CRITICAL(&inventoryLock);
 for(auto &device:devices) if(device.address==address) device.responding=responding;
 portEXIT_CRITICAL(&inventoryLock);
}
void hardwareInitialized(uint8_t address,bool ready,bool selected) {
 const char *changed=nullptr;
 const int status=selected?(ready?1:0):-2;
 portENTER_CRITICAL(&inventoryLock);
 for(auto &device:devices) if(device.address==address) {
  if(device.initialized!=status) changed=device.name;
  device.initialized=status; if(selected && ready) device.responding=true;
 }
 portEXIT_CRITICAL(&inventoryLock);
 if(changed) Serial.printf("I2C %s 0x%02X driver: %s\n",changed,address,!selected?"not selected":ready?"ready":"failed");
}
String hardwareReport() {
 using namespace readerBoard;
 String out="Board: "; out+=name;
 out+="\nFlash configured/detected: "+String(READER_PROFILE_MB)+" MiB / "+String(ESP.getFlashChipSize()/1024/1024)+" MiB";
 out+="\nPSRAM expected/detected: "+String(expectedPsramMiB)+" MiB / "+String(ESP.getPsramSize()/1024/1024)+" MiB";
 out+="\nSerial baud="+String(serialBaud);
 out+="\nI2C SDA="+String(sda)+" SCL="+String(scl)+" Hz="+String(i2cHz)+" timeout ms="+String(i2cTimeoutMs);
 out+=busInitialized?" (bus initialized)":" (BUS INITIALIZATION FAILED)";
 out+="\nRelay GPIO="+String(relay)+" active="+(readerBoard::relayOn?"HIGH":"LOW");
 out+="\nNFC IRQ="+String(nfcIrq)+" reset="+String(nfcReset);
 if(spi) out+="\nSPI MOSI="+String(mosi)+" MISO="+String(miso)+" SCLK="+String(sclk)+" CS="+String(ethCs)+" IRQ="+String(ethIrq)+" reset="+String(ethReset)+" PHY address="+String(ethAddress);
 else out+="\nSPI: unused";
 if(rmii) out+="\nRMII LAN8710A MDC="+String(ethMdc)+" MDIO="+String(ethMdio)+" power="+String(ethPower)+" clock="+String(ethClock)+" PHY="+String(ethAddress);
 else out+="\nRMII: unused";
 if(!ethernet) out+="\nEthernet: unused; station Wi-Fi required";
 if(rmii) out+="\nSD interface: unused";
 out+="\nCards/checkouts: "+String(readerBoard::cards)+"/"+String(readerBoard::grants)+"; snapshot bytes="+String(snapshotBytes)+"; audit bytes="+String(eventBytes);
#if READER_ENABLE_RESERVATIONS
 out+="\nReservation summaries/operations/references: "+String(reservationRecords)+"/"+String(reservationOperations)+"/"+String(reservationReferences)+"; journal bytes="+String(reservationJournalBytes);
#else
 out+="\nReservations: compiled out";
#endif
 Device snapshot[5];
 portENTER_CRITICAL(&inventoryLock);
 for(unsigned i=0;i<5;++i) snapshot[i]=devices[i];
 portEXIT_CRITICAL(&inventoryLock);
 for(const auto &d:snapshot) {
  char address[8]; snprintf(address,sizeof(address),"0x%02X",d.address);
  out+="\n"+String(d.name)+" "+address+": "+(d.responding?"responding":"not detected")+", driver "+(d.initialized==-2?"not selected":d.initialized<0?"not initialized":d.initialized?"ready":"failed");
 }
 return out;
}
