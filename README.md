# Controlador de motores DC con ESP32

Firmware para controlar dos motores DC con encoder desde un ESP32. Un maestro (Raspberry Pi, PC o microcontrolador) manda consignas de PWM por SPI y recibe los conteos de los encoders en la misma transacción.

| | |
|---|---|
| **Placa** | ESP32 DevKit (Xtensa LX6, doble núcleo) |
| **Potencia** | Puentes H BTS7960 / BTN7960, señales aisladas con optoacopladores 4N25 |
| **Framework** | Arduino-ESP32 3.x (ESP-IDF 5.x) |
| **Autor** | Adán López Lozano |

## Qué hace

- **Esclavo SPI con DMA** en `SPI2_HOST`: la transferencia va directo a RAM y la tarea solo despierta cuando termina.
- **PWM a 20 kHz** con LEDC, 10 bits (0 a 1023): fuera del rango audible, sin zumbido en los motores.
- **Rampa trapezoidal a 200 Hz**: la consigna nunca cambia más de `max_accel_step` por ciclo de 5 ms, lo que limita los picos de corriente al arrancar.
- **Encoders en cuadratura** por interrupción en IRAM (conteo 1x: flanco de subida del canal A, sentido por el canal B).
- **FreeRTOS en los dos núcleos**: comunicación en el núcleo 0 y lazo de PWM en el núcleo 1.
- **Paro por pérdida de comunicación**: si en 200 ms no llega un paquete válido, la consigna vuelve a 0 con la misma rampa.

El firmware aplica PWM en lazo abierto y reporta los encoders. El lazo de velocidad o posición se cierra en el maestro.

## Protocolo SPI

Modo 0, tramas de 16 bytes, little-endian.

**Maestro → ESP32**

| Bytes | Campo | Tipo | Rango |
|---|---|---|---|
| 0–1 | `target_pwm_m1` | int16 | −1023 a 1023 |
| 2–3 | `target_pwm_m2` | int16 | −1023 a 1023 |
| 4–5 | `max_accel_step` | uint16 | 1 a 199 (otro valor se ignora) |
| 6 | `enable_motors` | uint8 | 1 habilita, 0 corta potencia |
| 7 | `checksum` | uint8 | XOR de los bytes 0 a 6 |
| 8–15 | relleno | | 0 |

**ESP32 → maestro**

| Bytes | Campo | Tipo |
|---|---|---|
| 0–3 | `encoder1_ticks` | int32 |
| 4–7 | `encoder2_ticks` | int32 |
| 8–9 | `current_pwm_m1` | int16 |
| 10–11 | `current_pwm_m2` | int16 |
| 12–15 | relleno | 0 |

`maestro/paquete_spi.py` arma y lee estas tramas desde Python (con un ejemplo para `spidev`).

## Conexiones

| Señal | GPIO |
|---|---|
| SPI MOSI / MISO / SCLK / CS | 13 / 12 / 14 / 15 |
| Motor 1 RPWM / LPWM / EN | 25 / 26 / 27 |
| Motor 2 RPWM / LPWM / EN | 32 / 33 / 4 |
| Encoder 1 A / B | 34 / 35 |
| Encoder 2 A / B | 36 / 39 |

## Compilar y cargar

1. Arduino IDE con el core **esp32 de Espressif 3.x**.
2. Abre `firmware/controlador_motores/controlador_motores.ino`, placa *ESP32 Dev Module*, y carga.

## Pruebas

El `.ino` se compila en la PC contra una simulación de la API del ESP32 (`test/mocks/`) y se prueba su lógica:

```bash
make -C test          # firmware: 27 comprobaciones
python -m pytest test # trama del maestro: 3 pruebas
```

Qué se comprueba: configuración del PWM (20 kHz, 10 bits) y del SPI (SPI2, DMA automático, modo 0), validación del checksum, límites de consigna y de rampa, que la rampa suba de 12 en 12 y llegue exacto sin pasarse, sentido de giro en cada puente H, corte de potencia, paro por pérdida de comunicación, conteo de encoders en ambos sentidos y el orden de bytes de la telemetría. Las firmas de `ledcAttach`, `ledcWrite`, `spi_slave_initialize` y `spi_slave_transmit` se revisaron contra los encabezados oficiales de Arduino-ESP32 3.3.0 y ESP-IDF 5.5.

Las pruebas validan el código, no la electrónica: en el robot hay que revisar niveles de voltaje, ruido y temperatura de los drivers.

## Cambios respecto a la versión anterior

- Se valida el checksum; antes el campo existía pero nunca se revisaba.
- `ledcSetup` y `ledcAttachPin` ya no existen en Arduino-ESP32 3.x: ahora se usan `ledcAttach(pin, frecuencia, bits)` y `ledcWrite(pin, duty)`.
- `HSPI_HOST` ya no está definido en ESP-IDF 5.5: se usa `SPI2_HOST`, que son los mismos pines.
- Paro por pérdida de comunicación (`CMD_TIMEOUT_MS = 200`; 0 lo desactiva).
- El ciclo de control y el manejo de paquetes quedaron en funciones (`controlStep`, `handleControlPacket`) para poder probarlos.
- `fabsf` en lugar de `abs` con flotantes.

## Notas de hardware

- **GPIO12** es pin de arranque: si algo lo jala a nivel alto al encender, el ESP32 no arranca. Con un solo esclavo en el bus no suele pasar.
- **GPIO34 a 39** son solo de entrada y no tienen pull-up interno. Si el encoder es de colector abierto, lleva pull-ups externos a 3.3 V; si se alimenta a 5 V, adapta el nivel.
- **4N25 a 20 kHz**: con resistencias de carga típicas sus flancos tardan varios microsegundos, y el ciclo de trabajo se deforma cerca de 0 % y 100 %. Un optoacoplador rápido como el 6N137 lo resuelve.
