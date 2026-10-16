// Simulación mínima de la API de Arduino-ESP32 3.x para probar el firmware en la PC.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <map>
#include <cstdio>
#define IRAM_ATTR
#define WORD_ALIGNED_ATTR __attribute__((aligned(4)))
#define HIGH 1
#define LOW 0
#define INPUT 0x01
#define OUTPUT 0x03
#define RISING 0x01
namespace mock {
  inline std::map<int,int> digital, mode, gpio_level;
  inline std::map<int,uint32_t> duty, freq, res;
  inline std::map<int,void(*)()> isr;
  inline std::map<int,int> isr_mode;
  inline uint32_t now_ms = 0;
  inline int tasks_created = 0;
}
inline void pinMode(uint8_t p, uint8_t m) { mock::mode[p] = m; }
inline void digitalWrite(uint8_t p, uint8_t v) { mock::digital[p] = v; }
inline bool ledcAttach(uint8_t pin, uint32_t f, uint8_t r) { mock::freq[pin] = f; mock::res[pin] = r; mock::duty[pin] = 0; return true; }
inline bool ledcWrite(uint8_t pin, uint32_t d) { if (!mock::freq.count(pin)) return false; mock::duty[pin] = d; return true; }
inline int digitalPinToInterrupt(int p) { return p; }
inline void attachInterrupt(int p, void (*f)(), int m) { mock::isr[p] = f; mock::isr_mode[p] = m; }
inline uint32_t millis() { return mock::now_ms; }
struct MockSerial { void begin(long) {} void println(const char *) {} };
inline MockSerial Serial;
