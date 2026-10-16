// Controlador de dos motores DC con ESP32 (Arduino-ESP32 3.x / ESP-IDF 5.x)
// - Esclavo SPI con DMA: recibe consignas de PWM y devuelve encoders.
// - PWM a 20 kHz por LEDC hacia puentes H BTS7960 (señales aisladas con 4N25).
// - Encoders de cuadratura por interrupción en IRAM (conteo 1x).
// - Rampa trapezoidal de 200 Hz en el núcleo 1; comunicación en el núcleo 0.
// - Paro por pérdida de comunicación: si no llega un paquete válido en CMD_TIMEOUT_MS, la consigna vuelve a 0.
#include <Arduino.h>
#include <math.h>
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "protocolo.h"

// ---------------- Pines (ESP32 DevKit) ----------------
// Bus SPI esclavo en SPI2 (pines nativos de HSPI).
// Ojo: GPIO12 es pin de arranque (voltaje de la flash). Si algo lo jala a nivel alto al encender, el ESP32 no arranca.
#define GPIO_MOSI 13
#define GPIO_MISO 12
#define GPIO_SCLK 14
#define GPIO_CS   15

#define M1_RPWM_PIN 25
#define M1_LPWM_PIN 26
#define M1_EN_PIN   27
#define M2_RPWM_PIN 32
#define M2_LPWM_PIN 33
#define M2_EN_PIN   4

#define ENC1_A_PIN 34
#define ENC1_B_PIN 35
#define ENC2_A_PIN 36
#define ENC2_B_PIN 39

// ---------------- PWM ----------------
#define PWM_FREQ_HZ  20000   // fuera del rango audible
#define PWM_RES_BITS 10      // 0 .. 1023

// ---------------- Tiempos ----------------
#define CONTROL_PERIOD_MS 5      // 200 Hz
#define CMD_TIMEOUT_MS    200    // 0 desactiva el paro por pérdida de comunicación

// Buffers alineados a 4 bytes, como pide el DMA del ESP32
WORD_ALIGNED_ATTR uint8_t spi_rx_buf[16];
WORD_ALIGNED_ATTR uint8_t spi_tx_buf[16];
static const size_t SPI_FRAME_BYTES = 16;

volatile int32_t enc1_count = 0;
volatile int32_t enc2_count = 0;
volatile float target_m1 = 0.0f, target_m2 = 0.0f;
volatile float ramp_step_size = 12.0f;
volatile bool motors_enabled = false;
volatile uint32_t last_valid_ms = 0;
volatile uint32_t checksum_errors = 0;
float current_m1 = 0.0f, current_m2 = 0.0f;

void IRAM_ATTR isr_encoder1() {
    if (gpio_get_level((gpio_num_t)ENC1_B_PIN)) enc1_count++; else enc1_count--;
}
void IRAM_ATTR isr_encoder2() {
    if (gpio_get_level((gpio_num_t)ENC2_B_PIN)) enc2_count++; else enc2_count--;
}

// Con el 4N25 en colector común (no inversor) la lógica es directa.
// Si va en emisor común (inversor), usa (MAX - duty) en ledcWrite.
void applyMotorPWM(uint8_t rpwm_pin, uint8_t lpwm_pin, uint8_t en_pin, float pwm_val, bool enabled) {
    if (!enabled) {
        digitalWrite(en_pin, LOW);
        ledcWrite(rpwm_pin, 0);
        ledcWrite(lpwm_pin, 0);
        return;
    }
    digitalWrite(en_pin, HIGH);
    uint32_t duty = (uint32_t)clampf(fabsf(pwm_val), 0.0f, (float)PWM_MAX);
    if (pwm_val >= 0.0f) { ledcWrite(rpwm_pin, duty); ledcWrite(lpwm_pin, 0); }
    else                 { ledcWrite(rpwm_pin, 0);    ledcWrite(lpwm_pin, duty); }
}

// Un ciclo del lazo de 200 Hz (separado de la tarea para poder probarlo en la PC)
void controlStep(uint32_t now_ms) {
    if (CMD_TIMEOUT_MS > 0 && (uint32_t)(now_ms - last_valid_ms) > CMD_TIMEOUT_MS) {
        target_m1 = 0.0f;
        target_m2 = 0.0f;
    }
    current_m1 = ramp_step(current_m1, target_m1, ramp_step_size);
    current_m2 = ramp_step(current_m2, target_m2, ramp_step_size);
    applyMotorPWM(M1_RPWM_PIN, M1_LPWM_PIN, M1_EN_PIN, current_m1, motors_enabled);
    applyMotorPWM(M2_RPWM_PIN, M2_LPWM_PIN, M2_EN_PIN, current_m2, motors_enabled);
}

// Arma la telemetría que sale en la siguiente transacción
void prepareTelemetry() {
    SpiTelemetryPacket_t t;
    t.encoder1_ticks = enc1_count;
    t.encoder2_ticks = enc2_count;
    t.current_pwm_m1 = (int16_t)current_m1;
    t.current_pwm_m2 = (int16_t)current_m2;
    memset(spi_tx_buf, 0, sizeof(spi_tx_buf));
    memcpy(spi_tx_buf, &t, sizeof(t));
}

// Procesa un paquete recibido. Regresa false si el checksum no coincide.
bool handleControlPacket(const uint8_t *buf, uint32_t now_ms) {
    SpiControlPacket_t p;
    memcpy(&p, buf, sizeof(p));
    if (!packet_is_valid(p)) { checksum_errors++; return false; }
    target_m1 = clampf((float)p.target_pwm_m1, -PWM_MAX, PWM_MAX);
    target_m2 = clampf((float)p.target_pwm_m2, -PWM_MAX, PWM_MAX);
    if (p.max_accel_step > 0 && p.max_accel_step < ACCEL_STEP_MAX) ramp_step_size = (float)p.max_accel_step;
    motors_enabled = (p.enable_motors == 1);
    last_valid_ms = now_ms;
    return true;
}

void controlLoopTask(void *) {
    const TickType_t period = pdMS_TO_TICKS(CONTROL_PERIOD_MS);
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        controlStep(millis());
        vTaskDelayUntil(&last, period);
    }
}

void spiSlaveDmaTask(void *) {
    spi_slave_transaction_t trans;
    for (;;) {
        prepareTelemetry();
        memset(&trans, 0, sizeof(trans));
        trans.length = SPI_FRAME_BYTES * 8;  // en bits
        trans.tx_buffer = spi_tx_buf;
        trans.rx_buffer = spi_rx_buf;
        // La tarea se bloquea hasta que el DMA termina la transacción; no ocupa CPU mientras espera.
        if (spi_slave_transmit(SPI2_HOST, &trans, portMAX_DELAY) == ESP_OK) {
            handleControlPacket(spi_rx_buf, millis());
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(M1_EN_PIN, OUTPUT); digitalWrite(M1_EN_PIN, LOW);
    pinMode(M2_EN_PIN, OUTPUT); digitalWrite(M2_EN_PIN, LOW);

    ledcAttach(M1_RPWM_PIN, PWM_FREQ_HZ, PWM_RES_BITS);
    ledcAttach(M1_LPWM_PIN, PWM_FREQ_HZ, PWM_RES_BITS);
    ledcAttach(M2_RPWM_PIN, PWM_FREQ_HZ, PWM_RES_BITS);
    ledcAttach(M2_LPWM_PIN, PWM_FREQ_HZ, PWM_RES_BITS);

    // GPIO34-39 son solo de entrada y no tienen pull-up interno: si el encoder es de colector abierto, lleva pull-ups externos a 3.3 V.
    pinMode(ENC1_A_PIN, INPUT); pinMode(ENC1_B_PIN, INPUT);
    pinMode(ENC2_A_PIN, INPUT); pinMode(ENC2_B_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(ENC1_A_PIN), isr_encoder1, RISING);
    attachInterrupt(digitalPinToInterrupt(ENC2_A_PIN), isr_encoder2, RISING);

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = GPIO_MOSI;
    buscfg.miso_io_num = GPIO_MISO;
    buscfg.sclk_io_num = GPIO_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;

    spi_slave_interface_config_t slvcfg = {};
    slvcfg.mode = 0;             // CPOL = 0, CPHA = 0
    slvcfg.spics_io_num = GPIO_CS;
    slvcfg.queue_size = 3;
    slvcfg.flags = 0;

    gpio_set_pull_mode((gpio_num_t)GPIO_MOSI, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)GPIO_SCLK, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode((gpio_num_t)GPIO_CS, GPIO_PULLUP_ONLY);

    ESP_ERROR_CHECK(spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO));

    last_valid_ms = millis();
    xTaskCreatePinnedToCore(spiSlaveDmaTask, "spi_dma", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(controlLoopTask, "pwm_rampa", 4096, NULL, 6, NULL, 1);
    Serial.println("[OK] SPI esclavo con DMA y PWM a 20 kHz listos");
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
