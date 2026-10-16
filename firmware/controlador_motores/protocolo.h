// Protocolo SPI entre el maestro (Raspberry Pi / PC) y el ESP32 esclavo.
// Sin dependencias de Arduino: se compila igual en el ESP32 y en la PC para las pruebas.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Maestro -> ESP32 (8 bytes, little-endian)
typedef struct __attribute__((packed)) {
    int16_t  target_pwm_m1;   // -1023 .. +1023
    int16_t  target_pwm_m2;   // -1023 .. +1023
    uint16_t max_accel_step;  // cambio máximo de PWM por ciclo de 5 ms (1 .. 199)
    uint8_t  enable_motors;   // 1 = habilitado, 0 = corte de potencia
    uint8_t  checksum;        // XOR de los 7 bytes anteriores
} SpiControlPacket_t;

// ESP32 -> maestro (12 bytes, little-endian)
typedef struct __attribute__((packed)) {
    int32_t encoder1_ticks;
    int32_t encoder2_ticks;
    int16_t current_pwm_m1;
    int16_t current_pwm_m2;
} SpiTelemetryPacket_t;

static_assert(sizeof(SpiControlPacket_t) == 8, "el paquete de control debe medir 8 bytes");
static_assert(sizeof(SpiTelemetryPacket_t) == 12, "el paquete de telemetría debe medir 12 bytes");

static const int16_t  PWM_MAX = 1023;
static const uint16_t ACCEL_STEP_MAX = 200;

inline uint8_t xor_checksum(const uint8_t *data, size_t n) {
    uint8_t c = 0;
    for (size_t i = 0; i < n; i++) c ^= data[i];
    return c;
}

inline bool packet_is_valid(const SpiControlPacket_t &p) {
    return xor_checksum(reinterpret_cast<const uint8_t *>(&p), sizeof(p) - 1) == p.checksum;
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Un paso de la rampa: acerca `current` a `target` sin cambiar más de `step` por ciclo.
inline float ramp_step(float current, float target, float step) {
    float err = target - current;
    if (err <= step && err >= -step) return target;
    return current + (err > 0.0f ? step : -step);
}
