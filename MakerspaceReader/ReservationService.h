#pragma once
#include "ReservationConfig.h"
#if READER_ENABLE_RESERVATIONS
#include "Reader.h"
#include "Reservation.h"
namespace display { struct Status; }
namespace reservation {
void setup();
void service();
void network();
void refreshHint();
bool subscribing();
std::vector<String> hintTopics();
void displayStatus(display::Status &);
void stats(JsonObject); // stateMutex held
void sessionEnded(JsonVariantConst event); // stateMutex held; never performs network I/O
void captureSession(Session &); // stateMutex held; implemented by ToolSessionService
bool attendanceReady(); // stateMutex held; no undelivered ending remains in the session journal
void recoverSessionEnd(); // replay the last durable logoff if reservation persistence was interrupted
}
#endif
