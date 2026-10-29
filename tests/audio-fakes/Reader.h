#pragma once
#include <cassert>
#include <cstdint>
extern bool busBusy, busHeld;
constexpr int i2cMutex = 1, pdTRUE = 1;
inline int xSemaphoreTake(int, int timeout) {
  assert(timeout == 0 && !busHeld);
  if (busBusy) return 0;
  busHeld = true; return pdTRUE;
}
inline void xSemaphoreGive(int) { assert(busHeld); busHeld = false; }
template <typename... Args> inline void fakeLog(const char *, Args...) {}
#define log_i(...) fakeLog(__VA_ARGS__)
#define log_e(...) fakeLog(__VA_ARGS__)

using portMUX_TYPE = int;
using TaskHandle_t = void *;
using TickType_t = uint32_t;
constexpr int portMUX_INITIALIZER_UNLOCKED = 0, pdPASS = 1;
constexpr TickType_t portMAX_DELAY = UINT32_MAX, portTICK_PERIOD_MS = 10;
extern bool criticalHeld;
inline void portENTER_CRITICAL(portMUX_TYPE *) { assert(!criticalHeld); criticalHeld = true; }
inline void portEXIT_CRITICAL(portMUX_TYPE *) { assert(criticalHeld); criticalHeld = false; }
int xTaskCreate(void (*entry)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
TaskHandle_t xTaskGetCurrentTaskHandle();
void xTaskNotifyGive(TaskHandle_t);
uint32_t ulTaskNotifyTake(int, TickType_t);