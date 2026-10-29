#include "Display.h"
#include "Reader.h"
#include <SerLCD.h>
#include <Wire.h>
#include <cstring>

namespace display {
class SerLcdBackend : public Backend {
  SerLCD lcd;
  Frame applied;
  bool colorKnown = false, rowKnown[2] = {false, false};
public:
  void begin() { lcd.begin(Wire, 0x72); } // One startup clear in the library only.
  bool draw(const Frame &f) override {
    if (xSemaphoreTake(i2cMutex, 0) != pdTRUE) return false;
    // At most one color or row per pass, then release the bus for NFC.
    bool done = false;
    if (!colorKnown || applied.color != f.color) {
      lcd.setFastBacklight(static_cast<unsigned long>(f.color));
      applied.color = f.color;
      colorKnown = true;
    } else if (!rowKnown[0] || std::strcmp(applied.row1, f.row1)) {
      lcd.setCursor(0, 0);
      lcd.write(reinterpret_cast<const uint8_t *>(f.row1), 16);
      std::memcpy(applied.row1, f.row1, 17);
      rowKnown[0] = true;
    } else if (!rowKnown[1] || std::strcmp(applied.row2, f.row2)) {
      lcd.setCursor(0, 1);
      lcd.write(reinterpret_cast<const uint8_t *>(f.row2), 16);
      std::memcpy(applied.row2, f.row2, 17);
      rowKnown[1] = true;
    } else done = true;
    xSemaphoreGive(i2cMutex);
    return done;
  }
};
Backend *detectBackend() {
  static NullBackend absent;
  static SerLcdBackend present;
  if (xSemaphoreTake(i2cMutex, 0) != pdTRUE) return nullptr;
  Wire.beginTransmission(0x72);
  bool found = Wire.endTransmission() == 0;
  if (found) {
    present.begin();
    log_i("I2C device found: SerLCD at 0x72");
  }
  xSemaphoreGive(i2cMutex);
  return found ? static_cast<Backend *>(&present) : &absent;
}
} // namespace display
