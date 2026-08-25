#+
#  Copyright (c) 2023-2026 John Franks
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  the rights to use, copy, modify, merge, publish, distribute, sublicense,
#  and/or sell copies of the Software, and to permit persons to whom the
#  Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included
#  in all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#  SOFTWARE.
#
#  MoistureController CMake build
#
#  Primary firmware: firmware/avr-ultra/   (arduino-cli)
#  Host tests:       tests/host/           (make test / ctest)
#  Native avr-gcc:   cmake/native_smoke.cpp with cmake/avr-toolchain.cmake
#
#    cmake -S . -B build -DUSE_ARDUINO_CLI=ON
#    cmake --build build
#    ctest --test-dir build --output-on-failure
#
#    cmake -S . -B build-native -DUSE_NATIVE_AVR=ON \
#          -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake
#    cmake --build build-native
#-

cmake_minimum_required(VERSION 3.16)

project(MoistureController LANGUAGES C CXX)

option(USE_ARDUINO_CLI "Use arduino-cli to compile sketches (recommended)" ON)
option(USE_NATIVE_AVR  "Build Logic smoke test with avr-gcc toolchain file" OFF)

if(USE_NATIVE_AVR)
    set(USE_ARDUINO_CLI OFF CACHE BOOL "" FORCE)
endif()

if(USE_ARDUINO_CLI AND USE_NATIVE_AVR)
    message(FATAL_ERROR "Cannot enable both USE_ARDUINO_CLI and USE_NATIVE_AVR.")
endif()

enable_testing()

# Host tests use the native compiler. Skip when the AVR toolchain is loaded.
if(NOT CMAKE_CROSSCOMPILING)
    add_executable(host_tests tests/host/test_main.cpp)
    target_include_directories(host_tests PRIVATE
        "${CMAKE_SOURCE_DIR}/firmware/avr-ultra")
    target_compile_definitions(host_tests PRIVATE HOST_TEST)
    target_compile_features(host_tests PRIVATE cxx_std_11)
    if(MSVC)
        target_compile_options(host_tests PRIVATE /W4)
    else()
        target_compile_options(host_tests PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME host_tests COMMAND host_tests)
endif()

if(USE_ARDUINO_CLI)
    find_program(ARDUINO_CLI arduino-cli REQUIRED
                 DOC "Path to arduino-cli executable")

    add_custom_target(legacy
        COMMAND ${ARDUINO_CLI} compile
                --fqbn arduino:avr:nano:cpu=atmega328
                --warnings all
                --build-path "${CMAKE_BINARY_DIR}/legacy"
                "${CMAKE_SOURCE_DIR}"
        COMMENT "Compiling legacy root sketch with arduino-cli"
        VERBATIM
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    add_custom_target(ultra
        COMMAND ${ARDUINO_CLI} compile
                --fqbn arduino:avr:nano:cpu=atmega328
                --warnings all
                --build-path "${CMAKE_BINARY_DIR}/ultra"
                "${CMAKE_SOURCE_DIR}/firmware/avr-ultra"
        COMMENT "Compiling firmware/avr-ultra (Ultra low-power target)"
        VERBATIM
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    )

    add_custom_target(build-all ALL DEPENDS ultra)
    if(TARGET host_tests)
        add_dependencies(build-all host_tests)
    endif()

    message(STATUS "Using arduino-cli build mode")
    message(STATUS "Default target builds 'ultra' (+ host_tests when not cross-compiling)")
endif()

if(USE_NATIVE_AVR)
    if(NOT CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR
            "Native AVR build requires -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake")
    endif()

    add_executable(ultra_logic_smoke cmake/native_smoke.cpp)
    target_include_directories(ultra_logic_smoke PRIVATE
        "${CMAKE_SOURCE_DIR}/firmware/avr-ultra")
    set_target_properties(ultra_logic_smoke PROPERTIES
        OUTPUT_NAME "MoistureController-ultra-logic"
        SUFFIX ".elf")

    add_custom_command(TARGET ultra_logic_smoke POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom
                "$<TARGET_FILE:ultra_logic_smoke>"
                "$<TARGET_FILE_DIR:ultra_logic_smoke>/$<TARGET_NAME:ultra_logic_smoke>.hex"
        COMMENT "Generating ultra-logic hex")

    message(STATUS "Using native avr-gcc (Logic smoke test)")
    message(STATUS "Firmware sketches: use arduino-cli / `make ultra`")
endif()

if(NOT USE_ARDUINO_CLI AND NOT USE_NATIVE_AVR)
    message(FATAL_ERROR
"Enable one of:
  -DUSE_ARDUINO_CLI=ON     (recommended; compiles sketches)
  -DUSE_NATIVE_AVR=ON      (requires -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake)

Or just run:  make ultra && make test
")
endif()
