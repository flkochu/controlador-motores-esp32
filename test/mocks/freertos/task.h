#pragma once
#include "freertos/FreeRTOS.h"
#include "Arduino.h"
typedef void (*TaskFunction_t)(void *);
inline TickType_t xTaskGetTickCount() { return mock::now_ms; }
inline void vTaskDelayUntil(TickType_t *, TickType_t) {}
inline void vTaskDelay(TickType_t) {}
inline int xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, int, void *, int) { mock::tasks_created++; return 1; }
