CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -Wno-unused-parameter -I mocks -I ../firmware/controlador_motores
test: test_firmware
	./test_firmware
test_firmware: test_firmware.cpp ../firmware/controlador_motores/controlador_motores.ino ../firmware/controlador_motores/protocolo.h $(wildcard mocks/*.h mocks/*/*.h)
	$(CXX) $(CXXFLAGS) -x c++ test_firmware.cpp -o $@
clean:
	rm -f test_firmware
.PHONY: test clean
