#include "../MakerspaceReader/Display.h"
#include "display-fakes/Wire.h"
#include <cstring>
#include <iostream>
bool busBusy = true, busHeld = false, lcdPresent = false;
unsigned begins = 0, colors = 0, rows = 0;
uint32_t lastColor = 0;
char lastRows[2][17] = {};
TwoWire Wire;
int main() {
  using namespace display;
  assert(detectBackend() == nullptr);
  busBusy = false;
  Backend *absent = detectBackend();
  Model model; Status s;
  Frame f = model.frame(s, 0);
  assert(absent && absent->draw(f) && begins == 0 && rows == 0);
  lcdPresent = true;
  Backend *lcd = detectBackend();
  assert(lcd && begins == 1);
  busBusy = true;
  assert(!lcd->draw(f) && rows == 0 && colors == 0);
  busBusy = false;
  assert(!lcd->draw(f) && colors == 1 && rows == 0 && !busHeld);
  assert(!lcd->draw(f) && rows == 1 && !busHeld);
  assert(!lcd->draw(f) && rows == 2 && !busHeld);
  assert(lcd->draw(f));
  assert(lcd->draw(f) && colors == 1 && rows == 2);
  std::memset(f.row2, ' ', 16);
  assert(!lcd->draw(f) && rows == 3 && colors == 1);
  assert(lcd->draw(f));
  assert(std::strcmp(lastRows[1], "                ") == 0);
  f.color = green;
  assert(!lcd->draw(f) && colors == 2 && rows == 3 && lastColor == green);
  assert(lcd->draw(f));
  std::cout << "SerLCD absent, bus contention, bounded differential writes and colors passed\n";
}
