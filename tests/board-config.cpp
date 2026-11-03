#include "../MakerspaceReader/BoardConfig.h"
#include <cassert>
int main() {
 using namespace readerBoard;
 assert(distinct(0,1));
 assert(!outputPin(-1) && !outputPin(49));
 if(olimex) assert(!outputPin(34) && !outputPin(6));
 else assert(!outputPin(22) && !outputPin(26) && !outputPin(46));
 assert(memoryCompatible(READER_PROFILE_MB*1024*1024UL,expectedPsramMiB*1024*1024UL));
 assert(!memoryCompatible(2*1024*1024,expectedPsramMiB*1024*1024UL));
 if(olimex || qtpy) assert(!memoryCompatible(16*1024*1024,expectedPsramMiB*1024*1024UL));
 assert(!memoryCompatible(READER_PROFILE_MB*1024*1024UL,(expectedPsramMiB+1)*1024*1024UL));
 assert(serialBaud==115200 && i2cHz==100000 && i2cTimeoutMs==50);
 assert(relayOn==1 && relayOff==0 && reservationRecords==16);
 if(olimex) { assert(!hasPsram && !spi && ethernet && rmii); assert(sda==13 && scl==16 && relay==33); assert(cards==32 && grants==32 && recentCards==16 && snapshotBytes==32768 && reservationOperations==16 && reservationJournalBytes==16384); }
 if(qtpy) { assert(hasPsram && !spi && !ethernet && configOnWifi); assert(sda==41 && scl==40 && relay==9); assert(cards==64 && grants==64 && expectedPsramMiB==2); }
 if(constrained) assert(eventBytes==65536);
}
