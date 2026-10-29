#include "Pn532.h"
#include "NfcProtocol.h"
#include <cstring>
bool Pn532::send(const uint8_t *command, size_t length) {
  if (length == 0 || length > 32)
    return false;
  uint8_t frame[40] = {0, 0, 0xFF, uint8_t(length + 1), uint8_t(-(length + 1)), 0xD4};
  memcpy(frame + 6, command, length);
  uint8_t sum = 0xD4;
  for (size_t i = 0; i < length; ++i)
    sum += command[i];
  frame[length + 6] = uint8_t(-sum);
  wire.beginTransmission(address);
  bool written = wire.write(frame, length + 8) == length + 8;
  uint8_t status = wire.endTransmission();
  memset(frame, 0, sizeof(frame));
  if (!written || status != 0 || !waitReady(1000))
    return false;
  uint8_t ack[7] = {};
  size_t count = wire.requestFrom(address, uint8_t(sizeof(ack)));
  size_t got = 0;
  while (wire.available() && got < sizeof(ack))
    ack[got++] = wire.read();
  const uint8_t expected[] = {0, 0, 0xFF, 0, 0xFF, 0};
  return count == sizeof(ack) && got == sizeof(ack) && (ack[0] & 1) &&
         memcmp(ack + 1, expected, sizeof(expected)) == 0;
}
bool Pn532::receive(uint8_t command, uint8_t *payload, size_t capacity, size_t &length,
                    uint32_t timeout) {
  length = 0;
  if (!waitReady(timeout))
    return false;
  // One I2C transaction must consume the complete frame before STOP.
  // 64 frame bytes cover every response in this Classic-only implementation.
  uint8_t frame[65] = {};
  size_t count = wire.requestFrom(address, uint8_t(sizeof(frame)));
  size_t got = 0;
  while (wire.available() && got < sizeof(frame))
    frame[got++] = wire.read();
  bool ok = count == got && got > 1 && (frame[0] & 1) &&
            nfcProtocol::response(frame + 1, got - 1, command, payload, capacity, length);
  memset(frame, 0, sizeof(frame));
  return ok;
}
bool Pn532::exchange(const uint8_t *command, size_t commandLength, uint8_t *payload,
                     size_t capacity, size_t &length) {
  length = 0;
  return send(command, commandLength) && receive(command[0], payload, capacity, length);
}
bool Pn532::begin() {
  uint8_t answer[8];
  size_t length = 0;
  // Normal SAM mode with IRQ enabled; sending this also wakes I2C LowVbat mode.
  const uint8_t sam[] = {0x14, 0x01, 0x14, 0x01}, version[] = {0x02};
  const uint8_t retries[] = {0x32, 0x05, 0xFF, 0x01, 0xFF};
  return exchange(sam, sizeof(sam), answer, sizeof(answer), length) && length == 0 &&
         exchange(version, sizeof(version), answer, sizeof(answer), length) && length == 4 &&
         answer[0] == 0x32 && (answer[3] & 1) &&
         exchange(retries, sizeof(retries), answer, sizeof(answer), length) && length == 0;
}
bool Pn532::startDetection() {
  const uint8_t command[] = {0x4A, 1, 0}; // One ISO14443A target, 106 kbit/s.
  return send(command, sizeof(command));  // ACK only; card response arrives on IRQ later.
}
bool Pn532::readDetection(uint8_t *uid, uint8_t &length, bool &classic) {
  uint8_t answer[56];
  size_t count = 0;
  length = 0;
  classic = false;
  return receive(0x4A, answer, sizeof(answer), count, 0) &&
         nfcProtocol::target(answer, count, uid, length, classic);
}
bool Pn532::statusCommand(uint8_t command) {
  const uint8_t request[] = {command, 1};
  uint8_t answer[1];
  size_t length = 0;
  return exchange(request, sizeof(request), answer, sizeof(answer), length) && length == 1 &&
         answer[0] == 0;
}
bool Pn532::release() {
  return statusCommand(0x52);
}
bool Pn532::reactivate() {
  // Keep the selected UID in the controller; do not switch to another nearby card.
  return statusCommand(0x44) && statusCommand(0x54);
}
bool Pn532::authenticate(const uint8_t *uid, uint8_t uidLength, const uint8_t *key, bool keyB) {
  if (uidLength != 4 && uidLength != 7)
    return false;
  uint8_t command[14] = {0x40, 1, uint8_t(keyB ? 0x61 : 0x60), 4};
  memcpy(command + 4, key, 6);
  // Classic uses the final four UID bytes for Crypto1 authentication.
  memcpy(command + 10, uid + uidLength - 4, 4);
  uint8_t answer[1];
  size_t length = 0;
  bool ok = exchange(command, sizeof(command), answer, sizeof(answer), length) && length == 1 &&
            answer[0] == 0;
  memset(command, 0, sizeof(command));
  return ok;
}
bool Pn532::readBlock(uint8_t block, uint8_t *data) {
  const uint8_t command[] = {0x40, 1, 0x30, block};
  uint8_t answer[17];
  size_t length = 0;
  if (!exchange(command, sizeof(command), answer, sizeof(answer), length) ||
      length != sizeof(answer) || answer[0] != 0)
    return false;
  memcpy(data, answer + 1, 16);
  memset(answer, 0, sizeof(answer));
  return true;
}
bool Pn532::writeBlock(uint8_t block, const uint8_t *data) {
  uint8_t command[20] = {0x40, 1, 0xA0, block}, answer[1];
  memcpy(command + 4, data, 16);
  size_t length = 0;
  bool ok = exchange(command, sizeof(command), answer, sizeof(answer), length) && length == 1 &&
            answer[0] == 0;
  memset(command, 0, sizeof(command));
  return ok;
}
