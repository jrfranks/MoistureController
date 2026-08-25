# AVR toolchain file for native avr-gcc builds.
#
# Usage (from the repository root):
#   cmake -S . -B build-native -DUSE_NATIVE_AVR=ON \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake
#   cmake --build build-native
#
# This file must live at cmake/avr-toolchain.cmake (tracked in git).
# Firmware sketches are built with arduino-cli (`make ultra`). Native mode
# compiles the host-independent Logic helpers with avr-gcc as a toolchain check.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)

set(CMAKE_C_COMPILER avr-gcc)
set(CMAKE_CXX_COMPILER avr-g++)
set(CMAKE_OBJCOPY avr-objcopy CACHE FILEPATH "avr-objcopy")
set(CMAKE_OBJDUMP avr-objdump CACHE FILEPATH "avr-objdump")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

set(AVR_MCU atmega328p CACHE STRING "AVR MCU")
set(AVR_F_CPU 16000000UL CACHE STRING "AVR F_CPU")

set(CMAKE_C_FLAGS
    "-mmcu=${AVR_MCU} -DF_CPU=${AVR_F_CPU} -Os -Wall -Wextra -ffunction-sections -fdata-sections -fshort-enums"
    CACHE STRING "")
set(CMAKE_CXX_FLAGS
    "${CMAKE_C_FLAGS} -fno-exceptions -fno-rtti"
    CACHE STRING "")
set(CMAKE_EXE_LINKER_FLAGS
    "-mmcu=${AVR_MCU} -Wl,--gc-sections"
    CACHE STRING "")
