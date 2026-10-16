import pathlib, sys
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "maestro"))
from paquete_spi import control, telemetria


def test_control_layout_and_checksum():
    f = control(400, -300, paso=20, habilitar=True)
    assert len(f) == 16
    assert f[:7] == bytes([0x90, 0x01, 0xD4, 0xFE, 0x14, 0x00, 0x01])
    chk = 0
    for b in f[:7]:
        chk ^= b
    assert f[7] == chk and f[8:] == bytes(8)


def test_control_clamps():
    f = control(5000, -5000)
    assert int.from_bytes(f[0:2], "little", signed=True) == 1023
    assert int.from_bytes(f[2:4], "little", signed=True) == -1023


def test_telemetry_matches_firmware_test_vector():
    # mismos bytes que verifica test_firmware.cpp
    frame = bytes([4, 0, 0, 0, 0xFB, 0xFF, 0xFF, 0xFF, 0x41, 0x01, 0x85, 0xFF, 0, 0, 0, 0])
    assert telemetria(frame) == {"encoder1": 4, "encoder2": -5, "pwm_m1": 321, "pwm_m2": -123}
