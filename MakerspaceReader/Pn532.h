#pragma once
#include <Wire.h>
#include <cstddef>
#include <cstdint>
// Focused I2C initiator transport for NXP UM0701-02. Only the NFC task calls it,
// with the shared I2C mutex held. Every read is gated by active-LOW IRQ.
class Pn532 {
public:
  static constexpr uint8_t address = 0x24;
  Pn532(TwoWire &wire, bool (*waitReady)(uint32_t)) : wire(wire), waitReady(waitReady) {}
  bool begin();
  bool startDetection();
  bool readDetection(uint8_t *uid, uint8_t &length, bool &classic);
  bool release();
  bool reactivate();
  bool authenticate(const uint8_t *uid, uint8_t uidLength, const uint8_t *key, bool keyB);
  bool readBlock(uint8_t block, uint8_t *data);
  bool writeBlock(uint8_t block, const uint8_t *data);

private:
  TwoWire &wire;
  bool (*waitReady)(uint32_t);
  bool send(const uint8_t *command, size_t length);
  bool receive(uint8_t command, uint8_t *payload, size_t capacity, size_t &length,
               uint32_t timeout = 1000);
  bool exchange(const uint8_t *command, size_t commandLength, uint8_t *payload, size_t capacity,
                size_t &length);
  bool statusCommand(uint8_t command);
};
