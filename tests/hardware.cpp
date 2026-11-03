#include "hardware-fakes/Reader.h"
#include "hardware-fakes/Wire.h"
#include "../MakerspaceReader/HardwareDiagnostics.h"
#include <iostream>
SerialPort Serial;
Chip ESP;
TwoWire Wire;
int i2cMutex=0;
unsigned pinWrites=0,waitMs=0;
unsigned Chip::getFlashChipSize(){return READER_PROFILE_MB*1024*1024;}
unsigned Chip::getPsramSize(){return readerBoard::expectedPsramMiB*1024*1024;}
void pinMode(int pin,int mode){assert(pin==readerBoard::nfcReset && mode==OUTPUT);}
void digitalWrite(int pin,int value){assert(pin==readerBoard::nfcReset && value==int(pinWrites%2));++pinWrites;}
int main(){
 Wire.present={0x24,0x28,0x68};hardwareProbe(true);
 assert(Wire.probes.size()==5 && Wire.probes[0x24]==1 && Wire.probes[0x28]==1 && Wire.probes[0x34]==3 && Wire.probes[0x72]==3);
 assert(pinWrites==2 && waitMs<=42 && i2cMutex==0);
 auto before=Wire.probes;
 auto report=hardwareReport();assert(report.find(readerBoard::name)!=std::string::npos);
 assert(report.find("PN532 0x24: responding, driver not initialized")!=std::string::npos);
 hardwareInitialized(0x28,false);assert(hardwareReport().find("PN7160 0x28: responding, driver failed")!=std::string::npos);
 hardwareInitialized(0x28,true);assert(hardwareReport().find("PN7160 0x28: responding, driver ready")!=std::string::npos);
 hardwareInitialized(0x28,true,false);assert(hardwareReport().find("PN7160 0x28: responding, driver not selected")!=std::string::npos);
 hardwareResponding(0x28,false);assert(hardwareReport().find("PN7160 0x28: not detected")!=std::string::npos);
 hardwareInitialized(0x99,true);assert(Wire.probes==before);
 assert(Serial.text.find("driver: failed")!=std::string::npos && Serial.text.find("driver: ready")!=std::string::npos);
 hardwareProbe(false);assert(Wire.probes==before);
 assert(hardwareReport().find("BUS INITIALIZATION FAILED")!=std::string::npos);
#if READER_ENABLE_RESERVATIONS
 assert(report.find("Reservation summaries/operations/references: 16/")!=std::string::npos);
#else
 assert(report.find("Reservations: compiled out")!=std::string::npos);
#endif
 std::cout<<"Bounded hardware inventory, shared diagnostics, failed bus and driver recovery passed\n";
}
