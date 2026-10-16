// Pruebas del firmware en la PC: se compila el .ino real contra simulaciones de la API del ESP32.
//   make -C test
#include "Arduino.h"
#include "../firmware/controlador_motores/controlador_motores.ino"
#include <cassert>
#include <cstdio>
#include <vector>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; std::printf("  FALLA línea %d: %s\n", __LINE__, #c); } } while (0)

static std::vector<uint8_t> packet(int16_t m1, int16_t m2, uint16_t step, uint8_t en, bool corrupt = false) {
    SpiControlPacket_t p{m1, m2, step, en, 0};
    p.checksum = xor_checksum((uint8_t *)&p, 7) ^ (corrupt ? 0x5A : 0);
    std::vector<uint8_t> b(16, 0); memcpy(b.data(), &p, 8); return b;
}
static void reset() {
    target_m1 = target_m2 = 0; current_m1 = current_m2 = 0; ramp_step_size = 12; motors_enabled = false;
    enc1_count = enc2_count = 0; checksum_errors = 0; mock::now_ms = 0; last_valid_ms = 0;
}
static void run(int cycles) { for (int i = 0; i < cycles; i++) { mock::now_ms += 5; controlStep(mock::now_ms); } }

int main() {
    setup();
    std::printf("setup\n");
    CHECK(mock::freq[M1_RPWM_PIN] == 20000 && mock::res[M1_RPWM_PIN] == 10);
    CHECK(mock::freq[M2_LPWM_PIN] == 20000);
    CHECK(mock::spi_host == SPI2_HOST && mock::spi_dma == SPI_DMA_CH_AUTO && mock::spi_cfg.mode == 0);
    CHECK(mock::isr_mode[ENC1_A_PIN] == RISING && mock::isr[ENC2_A_PIN] != nullptr);
    CHECK(mock::tasks_created == 2);
    CHECK(mock::digital[M1_EN_PIN] == LOW && mock::digital[M2_EN_PIN] == LOW);

    std::printf("checksum\n"); reset();
    CHECK(handleControlPacket(packet(400, -300, 20, 1).data(), 0));
    CHECK(target_m1 == 400 && target_m2 == -300 && ramp_step_size == 20 && motors_enabled);
    CHECK(!handleControlPacket(packet(900, 900, 5, 1, true).data(), 0));
    CHECK(target_m1 == 400 && checksum_errors == 1);

    std::printf("límites\n"); reset();
    handleControlPacket(packet(5000, -5000, 0, 1).data(), 0);
    CHECK(target_m1 == 1023 && target_m2 == -1023);
    CHECK(ramp_step_size == 12);                       // 0 se ignora
    handleControlPacket(packet(0, 0, 250, 1).data(), 0);
    CHECK(ramp_step_size == 12);                       // >= 200 se ignora

    std::printf("rampa\n"); reset();
    handleControlPacket(packet(500, -500, 12, 1).data(), 0);
    float prev = 0; bool mono = true;
    for (int i = 0; i < 41; i++) { mock::now_ms += 5; controlStep(mock::now_ms); if (current_m1 < prev || current_m1 - prev > 12.0001f) mono = false; prev = current_m1; last_valid_ms = mock::now_ms; }
    CHECK(mono);
    CHECK(current_m1 == 492);                          // 41 ciclos x 12
    mock::now_ms += 5; controlStep(mock::now_ms); last_valid_ms = mock::now_ms;
    CHECK(current_m1 == 500 && current_m2 == -500);    // llega exacto, sin pasarse
    CHECK(mock::duty[M1_RPWM_PIN] == 500 && mock::duty[M1_LPWM_PIN] == 0);
    CHECK(mock::duty[M2_RPWM_PIN] == 0 && mock::duty[M2_LPWM_PIN] == 500);
    CHECK(mock::digital[M1_EN_PIN] == HIGH);

    std::printf("corte de potencia\n");
    handleControlPacket(packet(500, -500, 12, 0).data(), mock::now_ms);
    mock::now_ms += 5; controlStep(mock::now_ms);
    CHECK(mock::digital[M1_EN_PIN] == LOW && mock::duty[M1_RPWM_PIN] == 0 && mock::duty[M2_LPWM_PIN] == 0);

    std::printf("paro por pérdida de comunicación\n"); reset();
    handleControlPacket(packet(600, 600, 50, 1).data(), 0);
    run(20);                                           // 100 ms: sigue la consigna
    CHECK(target_m1 == 600 && current_m1 == 600);
    run(30);                                           // 250 ms sin paquetes: consigna a 0 y rampa de bajada
    CHECK(target_m1 == 0 && current_m1 < 600);
    run(20);
    CHECK(current_m1 == 0 && mock::duty[M1_RPWM_PIN] == 0);

    std::printf("encoders\n"); reset();
    mock::gpio_level[ENC1_B_PIN] = 1; for (int i = 0; i < 7; i++) mock::isr[ENC1_A_PIN]();
    mock::gpio_level[ENC1_B_PIN] = 0; for (int i = 0; i < 3; i++) mock::isr[ENC1_A_PIN]();
    mock::gpio_level[ENC2_B_PIN] = 0; for (int i = 0; i < 5; i++) mock::isr[ENC2_A_PIN]();
    CHECK(enc1_count == 4 && enc2_count == -5);

    std::printf("telemetría\n");
    current_m1 = 321; current_m2 = -123; prepareTelemetry();
    int32_t e1; int16_t p2; memcpy(&e1, spi_tx_buf, 4); memcpy(&p2, spi_tx_buf + 10, 2);
    CHECK(e1 == 4 && p2 == -123);
    CHECK(spi_tx_buf[12] == 0 && spi_tx_buf[15] == 0);
    const uint8_t expect[12] = {4,0,0,0, 0xFB,0xFF,0xFF,0xFF, 0x41,0x01, 0x85,0xFF};
    CHECK(memcmp(spi_tx_buf, expect, 12) == 0);

    std::printf("\n%d comprobaciones, %d fallas\n", checks, fails);
    return fails ? 1 : 0;
}
