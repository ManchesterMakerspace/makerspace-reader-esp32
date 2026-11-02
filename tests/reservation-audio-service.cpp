#include "../MakerspaceReader/Audio.h"
#include "audio-fakes/Reader.h"
#include <functional>
#include <iostream>
bool busBusy=false,busHeld=false,criticalHeld=false;
static void (*entry)(void*)=nullptr;
static std::function<void(TickType_t)> waited;
struct Finished {};
int xTaskCreate(void (*fn)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*) {entry=fn;return pdPASS;}
TaskHandle_t xTaskGetCurrentTaskHandle(){return reinterpret_cast<void*>(1);}
void xTaskNotifyGive(TaskHandle_t){assert(!criticalHeld);}
uint32_t ulTaskNotifyTake(int,TickType_t ticks){assert(!criticalHeld);waited(ticks);return 0;}
struct Device:audio::Device {
  int64_t now=0; bool busy=false; audio::Note last={0,0,0}; unsigned notes=0;
  audio::Result begin() override{return audio::Result::Ok;}
  audio::Result note(const audio::Note &n,uint8_t) override {if(busy)return audio::Result::Busy;last=n;++notes;return audio::Result::Ok;}
  audio::Result off() override{return busy?audio::Result::Busy:audio::Result::Ok;}
  int64_t nowMs() const override{return now;}
} device;
namespace audio {Device &qwiicDevice(){return ::device;}}
int main(){
  using namespace audio;
  setup(); assert(playReservation(MelodyId::Jeopardy));
  int phase=0;
  waited=[&](TickType_t ticks){
    if(phase==0){
      assert(device.last.frequencyHz==523 && reservationPlaying()==MelodyId::Jeopardy);
      device.now+=100; assert(play(MelodyId::Coin)); device.busy=true; phase=1;return;
    }
    if(phase==1){ assert(ticks==1); device.now+=10; device.busy=false;phase=2;return; }
    if(phase==2){
      assert(reservationPlaying()==MelodyId::None);
      assert(device.last.frequencyHz==1318); phase=3;
    }else if(phase==3 && device.last.frequencyHz==523){
      assert(reservationPlaying()==MelodyId::Jeopardy);
      assert(device.last.durationMs==330); // 110 ms elapsed before successful pause.
      cancelReservation();phase=4;return;
    }else if(phase==4){
      assert(ticks==portMAX_DELAY && reservationPlaying()==MelodyId::None);
      assert(play(MelodyId::Coin));phase=5;return;
    }else if(phase==5){
      assert(device.last.frequencyHz==1318);assert(playReservation(MelodyId::Jeopardy));phase=6;
    }else if(phase==6){
      assert(ticks!=portMAX_DELAY); // A warning queued during scan feedback must wake at scan completion.
      if(reservationPlaying()==MelodyId::Jeopardy){
        cancelReservation(MelodyId::Jeopardy);
        assert(playReservation(MelodyId::DonkeyKongStart));phase=7;return;
      }
    }else if(phase==7){
      assert(reservationPlaying()==MelodyId::DonkeyKongStart && device.last.frequencyHz==262);
      device.now+=100;assert(play(MelodyId::Coin));phase=8;return;
    }else if(phase==8){
      assert(reservationPlaying()==MelodyId::None && device.last.frequencyHz==1318);phase=9;
    }else if(phase==9 && reservationPlaying()==MelodyId::DonkeyKongStart){
      assert(device.last.frequencyHz==262 && device.last.durationMs==780);phase=10;
    }else if(phase==10 && ticks==portMAX_DELAY){
      assert(reservationPlaying()==MelodyId::None && device.last.frequencyHz==523);
      phase=11;throw Finished{};
    }
    device.now+=int64_t(ticks)*portTICK_PERIOD_MS;
  };
  try{entry(nullptr);}catch(const Finished&){}
  assert(phase==11);
  std::cout<<"Reservation playback, scan preemption, busy-bus retry, resumption and cancellation passed\n";
}
