"""Arma y lee las tramas SPI del controlador de motores (lado del maestro).

Trama de 16 bytes en cada transacción, modo SPI 0.
  Maestro -> ESP32: int16 pwm_m1, int16 pwm_m2, uint16 paso_rampa, uint8 habilitar, uint8 checksum (XOR), 8 bytes de relleno
  ESP32 -> maestro: int32 encoder1, int32 encoder2, int16 pwm_actual_m1, int16 pwm_actual_m2, 4 bytes de relleno

Ejemplo con spidev en una Raspberry Pi:
    import spidev
    spi = spidev.SpiDev(); spi.open(0, 0); spi.mode = 0; spi.max_speed_hz = 1_000_000
    rx = spi.xfer2(list(control(400, -400, paso=12, habilitar=True)))
    print(telemetria(bytes(rx)))
"""
import struct

FRAME = 16
PWM_MAX = 1023


def control(pwm_m1: int, pwm_m2: int, paso: int = 12, habilitar: bool = True) -> bytes:
    pwm_m1 = max(-PWM_MAX, min(PWM_MAX, int(pwm_m1)))
    pwm_m2 = max(-PWM_MAX, min(PWM_MAX, int(pwm_m2)))
    body = struct.pack("<hhHB", pwm_m1, pwm_m2, int(paso), 1 if habilitar else 0)
    chk = 0
    for b in body:
        chk ^= b
    return (body + bytes([chk])).ljust(FRAME, b"\0")


def telemetria(frame: bytes) -> dict:
    e1, e2, p1, p2 = struct.unpack_from("<iihh", frame)
    return {"encoder1": e1, "encoder2": e2, "pwm_m1": p1, "pwm_m2": p2}
