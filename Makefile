# MoistureController — convenience wrapper around arduino-cli and host tests.
#
#   make          compile firmware/avr-ultra (default)
#   make test     host unit tests (Logic.h / CRC / decisions)
#   make legacy   compile the 2023 root sketch
#   make check    ultra + legacy + host tests
#   make cmake    configure+build via CMake + arduino-cli
#
# Native avr-gcc (optional, advanced):
#   cmake -S . -B build-native -DUSE_NATIVE_AVR=ON \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake

FQBN        ?= arduino:avr:nano:cpu=atmega328
ARDUINO_CLI ?= arduino-cli
BUILD_DIR   ?= build
CMAKE       ?= cmake

.PHONY: all ultra legacy test host-test check cmake cmake-build clean help

all: ultra

help:
	@echo "Targets:"
	@echo "  make          - compile firmware/avr-ultra"
	@echo "  make ultra    - same"
	@echo "  make legacy   - compile MoistureController.ino"
	@echo "  make test     - host unit tests"
	@echo "  make check    - ultra + legacy + test"
	@echo "  make cmake    - cmake -S . -B $(BUILD_DIR) && cmake --build"
	@echo "  make clean    - remove build dirs and host test binary"

ultra:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --warnings all firmware/avr-ultra

legacy:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --warnings all .

host-test test:
	$(MAKE) -C tests/host test

check: ultra legacy host-test

cmake: cmake-build

cmake-build:
	$(CMAKE) -S . -B $(BUILD_DIR) -DUSE_ARDUINO_CLI=ON
	$(CMAKE) --build $(BUILD_DIR)

clean:
	$(MAKE) -C tests/host clean
	rm -rf $(BUILD_DIR) build-native xxx
