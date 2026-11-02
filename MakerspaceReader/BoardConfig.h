#pragma once
#include <cstdint>
#ifdef ARDUINO
#include <sdkconfig.h>
#endif

#define READER_BOARD_WAVESHARE 1
#define READER_BOARD_OLIMEX_POE 2
#define READER_BOARD_QTPY_S3 3
#ifndef READER_BOARD
#if defined(ARDUINO_ESP32_POE)
#define READER_BOARD READER_BOARD_OLIMEX_POE
#elif defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_N4R2)
#define READER_BOARD READER_BOARD_QTPY_S3
#elif defined(ARDUINO_ESP32S3_DEV) || !defined(ARDUINO)
#define READER_BOARD READER_BOARD_WAVESHARE
#else
#error "Select a supported reader board or define READER_BOARD globally"
#endif
#endif
#if READER_BOARD < 1 || READER_BOARD > 3
#error "Unsupported READER_BOARD"
#endif
#if (defined(ARDUINO_ESP32_POE) && READER_BOARD != READER_BOARD_OLIMEX_POE) || (defined(ARDUINO_ADAFRUIT_QTPY_ESP32S3_N4R2) && READER_BOARD != READER_BOARD_QTPY_S3)
#error "Reader override conflicts with the native Arduino board"
#endif
#ifndef READER_PROFILE_MB
#if READER_BOARD == READER_BOARD_WAVESHARE
#define READER_PROFILE_MB 16
#else
#define READER_PROFILE_MB 4
#endif
#endif
// Consumed only by the Ethernet adapter after ETH.h is available.
#if READER_BOARD == READER_BOARD_OLIMEX_POE
#define READER_ETH_PHY ETH_PHY_LAN8720
#define READER_ETH_CLOCK ETH_CLOCK_GPIO17_OUT
#else
#define READER_ETH_PHY ETH_PHY_W5500
#endif
#define READER_HAS_ETHERNET (READER_BOARD != READER_BOARD_QTPY_S3)
#define READER_ETHERNET_SPI (READER_BOARD == READER_BOARD_WAVESHARE)
#if defined(ARDUINO)
#if READER_BOARD == READER_BOARD_OLIMEX_POE
#if !defined(CONFIG_IDF_TARGET_ESP32) || defined(BOARD_HAS_PSRAM)
#error "Olimex reader requires ESP32 WROOM without PSRAM"
#endif
#else
#if !defined(CONFIG_IDF_TARGET_ESP32S3) || !defined(BOARD_HAS_PSRAM)
#error "This reader board requires ESP32-S3 with PSRAM enabled"
#endif
#endif
#endif
namespace readerBoard {
constexpr bool olimex = READER_BOARD == READER_BOARD_OLIMEX_POE;
constexpr bool qtpy = READER_BOARD == READER_BOARD_QTPY_S3;
constexpr bool hasPsram = !olimex;
constexpr bool ethernet = READER_HAS_ETHERNET;
constexpr bool configOnWifi = qtpy;
constexpr bool rmii = olimex;
constexpr bool spi = READER_ETHERNET_SPI;
constexpr const char *name = olimex ? "Olimex ESP32-PoE" : qtpy ? "Adafruit QT Py S3 N4R2" : "Waveshare ESP32-S3-ETH";
constexpr unsigned expectedPsramMiB = olimex ? 0 : qtpy ? 2 : 8;
constexpr bool memoryCompatible(uint32_t flashBytes,uint32_t psramBytes) {
 return (olimex || qtpy ? flashBytes==4*1024*1024UL : flashBytes>=READER_PROFILE_MB*1024*1024UL) &&
   psramBytes==expectedPsramMiB*1024*1024UL;
}
constexpr int sda = olimex ? 13 : qtpy ? 41 : 1;
constexpr int scl = olimex ? 16 : qtpy ? 40 : 2;
constexpr int relay = olimex ? 33 : qtpy ? 9 : 21;
constexpr int relayOn = 1, relayOff = 0;
constexpr int nfcIrq = olimex ? 4 : 17, nfcReset = olimex ? 14 : 18;
constexpr int mosi = spi ? 11 : -1, miso = spi ? 12 : -1, sclk = spi ? 13 : -1;
constexpr int ethCs = spi ? 14 : -1, ethIrq = spi ? 10 : -1, ethReset = spi ? 9 : -1;
constexpr int ethMdc = olimex ? 23 : -1, ethMdio = olimex ? 18 : -1;
constexpr int ethPower = olimex ? 12 : -1, ethClock = olimex ? 17 : -1;
constexpr int ethAddress = olimex ? 0 : 1;
constexpr uint32_t serialBaud = 115200, i2cHz = 100000;
constexpr unsigned i2cTimeoutMs = 50;
constexpr bool constrained = !hasPsram || READER_PROFILE_MB == 4;
constexpr unsigned cards = !hasPsram ? 32 : constrained ? 64 : 300;
constexpr unsigned grants = !hasPsram ? 32 : constrained ? 64 : 150;
constexpr unsigned recentCards = !hasPsram ? 16 : constrained ? 32 : 0;
constexpr uint32_t snapshotBytes = !hasPsram ? 32*1024 : constrained ? 64*1024 : 320*1024;
constexpr uint32_t eventBytes = constrained ? 64*1024 : READER_PROFILE_MB == 8 ? 2*1024*1024 : 3*1024*1024;
constexpr unsigned reservationRecords = 16;
constexpr unsigned reservationOperations = hasPsram ? 64 : 16;
constexpr unsigned reservationReferences = hasPsram ? 64 : 16;
constexpr uint32_t reservationJournalBytes = hasPsram ? 64*1024 : 16*1024;
static_assert(READER_BOARD == READER_BOARD_WAVESHARE || READER_PROFILE_MB == 4, "New board profiles require 4 MB flash");
constexpr bool outputPin(int pin) { return pin>=0 && (olimex ? pin<=33 && pin!=6 && pin!=7 && pin!=8 && pin!=9 && pin!=10 && pin!=11 : pin<=48 && pin!=46 && (pin<=21 || pin>=38)); }
constexpr int pins[] = {sda,scl,relay,nfcIrq,nfcReset,mosi,miso,sclk,ethCs,ethIrq,ethReset,ethMdc,ethMdio,ethPower,ethClock};
constexpr bool distinct(unsigned a,unsigned b) { return a>=15 ? true : b>=15 ? distinct(a+1,a+2) : (pins[a]<0 || pins[b]<0 || pins[a]!=pins[b]) && distinct(a,b+1); }
static_assert(distinct(0,1), "Board pins conflict");
static_assert(!spi || (outputPin(mosi) && outputPin(sclk) && outputPin(ethCs) && outputPin(ethReset)), "SPI output pin unavailable");
static_assert(!rmii || (outputPin(ethMdc) && outputPin(ethMdio) && outputPin(ethPower) && outputPin(ethClock)), "RMII output pin unavailable");
static_assert(outputPin(sda) && outputPin(scl) && outputPin(relay) && outputPin(nfcReset), "Output pin unavailable");
static_assert(sda != scl && relay != sda && relay != scl && relay != nfcIrq && relay != nfcReset && nfcIrq != nfcReset, "Conflicting peripheral pins");
}
