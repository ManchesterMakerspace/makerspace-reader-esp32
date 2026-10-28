#include "../MakerspaceReader/NfcProtocol.h"
#include "../MakerspaceReader/Pn532.h"
#include <cassert>
#include <iostream>
#include <vector>
static TwoWire wire;
static bool irq = true;
static bool ready(uint32_t) {
  return irq && !wire.incoming.empty();
}
static std::vector<uint8_t> frame(uint8_t command, std::vector<uint8_t> payload) {
  uint8_t n = uint8_t(payload.size() + 2);
  std::vector<uint8_t> result = {0, 0, 0xFF, n, uint8_t(-n), 0xD5, uint8_t(command + 1)};
  result.insert(result.end(), payload.begin(), payload.end());
  uint8_t sum = 0;
  for (size_t i = 5; i < result.size(); ++i)
    sum += result[i];
  result.push_back(uint8_t(-sum));
  result.push_back(0);
  return result;
}
static void ack() {
  wire.incoming.push_back({1, 0, 0, 0xFF, 0, 0xFF, 0});
}
static void reply(uint8_t command, std::vector<uint8_t> payload) {
  auto result = frame(command, payload);
  result.insert(result.begin(), 1);
  wire.incoming.push_back(result);
}
static void exchange(uint8_t command, std::vector<uint8_t> payload) {
  ack();
  reply(command, payload);
}
int main() {
  using namespace nfcProtocol;
  assert(select(true, true) == Controller::pn7160);
  assert(select(true, false) == Controller::pn7160);
  assert(select(false, true) == Controller::pn532);
  assert(select(false, false) == Controller::none);
  assert(nfcProtocol::ready(Controller::pn7160, true));
  assert(!nfcProtocol::ready(Controller::pn7160, false));
  assert(nfcProtocol::ready(Controller::pn532, false));
  assert(!nfcProtocol::ready(Controller::pn532, true));
  assert(!nfcProtocol::ready(Controller::none, false) &&
         !nfcProtocol::ready(Controller::none, true));

  uint8_t output[56] = {};
  size_t length = 0;
  auto good = frame(0x40, {0, 1, 2, 3});
  assert(response(good.data(), good.size(), 0x40, output, sizeof(output), length));
  assert(length == 4 && output[3] == 3);
  for (size_t n = 0; n < good.size(); ++n)
    assert(!response(good.data(), n, 0x40, output, sizeof(output), length));
  for (size_t i = 0; i < good.size(); ++i) {
    auto broken = good;
    broken[i] ^= 1;
    assert(!response(broken.data(), broken.size(), 0x40, output, sizeof(output), length));
  }
  assert(!response(good.data(), good.size(), 0x4A, output, sizeof(output), length));
  assert(!response(good.data(), good.size(), 0x40, output, 3, length));
  uint8_t uid[10] = {}, uidLength = 0;
  bool classic = false;
  std::vector<uint8_t> target4 = {1, 1, 0, 4, 8, 4, 1, 2, 3, 4};
  assert(target(target4.data(), target4.size(), uid, uidLength, classic) && classic &&
         uidLength == 4);
  target4[5] = 255;
  assert(!target(target4.data(), target4.size(), uid, uidLength, classic));
  assert(uidLength == 0 && !classic);
  target4[5] = 4;
  target4[4] = 0;
  assert(target(target4.data(), target4.size(), uid, uidLength, classic) && !classic);
  target4[4] = 8;
  std::vector<uint8_t> target7 = {1, 1, 0, 0x42, 0x18, 7, 4, 5, 6, 7, 8, 9, 10};
  assert(target(target7.data(), target7.size(), uid, uidLength, classic) && classic &&
         uidLength == 7);

  Pn532 reader(wire, ready);
  exchange(0x14, {});
  exchange(0x02, {0x32, 1, 6, 7});
  exchange(0x32, {});
  assert(reader.begin());
  // Starting discovery reads only its ACK; absent cards do not block or cause I2C polling.
  ack();
  assert(reader.startDetection());
  size_t reads = wire.reads;
  assert(!reader.readDetection(uid, uidLength, classic));
  assert(wire.reads == reads);
  reply(0x4A, target7);
  irq = false;
  assert(!reader.readDetection(uid, uidLength, classic));
  assert(wire.reads == reads); // Stale notifications cannot trigger a read with inactive IRQ.
  irq = true;
  assert(reader.readDetection(uid, uidLength, classic) && classic && uidLength == 7);
  const uint8_t key[] = {1, 2, 3, 4, 5, 6};
  exchange(0x40, {0});
  assert(reader.authenticate(uid, uidLength, key, false));
  auto sent = wire.sent.back();
  assert(sent[6] == 0x40 && sent[7] == 1 && sent[8] == 0x60 && sent[9] == 4);
  assert(memcmp(sent.data() + 10, key, 6) == 0);
  assert(memcmp(sent.data() + 16, uid + 3, 4) == 0);
  exchange(0x40, {0x14});
  assert(!reader.authenticate(uid, uidLength, key, true));
  assert(wire.sent.back()[8] == 0x61);
  uint8_t data[16] = {};
  exchange(0x40, {0x14});
  assert(!reader.writeBlock(7, data)); // Command ACK is not RF write success.
  exchange(0x40, {0});
  assert(reader.writeBlock(7, data));
  exchange(0x40, {0, 1, 2}); // Short read must not copy stale data.
  memset(data, 0xAA, sizeof(data));
  assert(!reader.readBlock(4, data) && data[0] == 0xAA);
  std::vector<uint8_t> block(17, 0x55);
  block[0] = 0;
  exchange(0x40, block);
  assert(reader.readBlock(4, data) && data[0] == 0x55 && data[15] == 0x55);
  exchange(0x44, {0});
  exchange(0x54, {0});
  assert(reader.reactivate());
  exchange(0x52, {0});
  assert(reader.release());
  ack();
  reply(0x4A, target4); // A different/stale command reply fails authentication.
  assert(!reader.authenticate(uid, uidLength, key, false));
  wire.incoming.push_back({1, 0, 0, 0xFF}); // Short ACK.
  assert(!reader.startDetection());
  wire.writeStatus = 2;
  reads = wire.reads;
  assert(!reader.startDetection() && wire.reads == reads);
  assert(wire.incoming.empty());
  std::cout << "NFC selection, IRQ polarity, PN532 framing, discovery and Classic exchange checks "
               "passed\n";
}
