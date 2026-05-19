#+
#  Copyright (c) 2023-2025 John Franks
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
#  MoistureController CMake build system (2025 rewrite)
#
#  This file provides practical, working build support for the project.
#
#  Primary target:  firmware/avr-ultra/avr-ultra.ino   (modern Ultra low-power reference)
#                     Note: The .ino file is deliberately named to match the folder
#                     so that `arduino-cli` works cleanly.
#  Secondary target: legacy root MoistureController.ino (for historical / CI compatibility)
#
#  Recommended usage:
#
#    # Using arduino-cli (easiest and most reliable for real hardware)
#    cmake -B build -DUSE_ARDUINO_CLI=ON ..
#    cmake --build build          # Builds both "ultra" (firmware/avr-ultra/avr-ultra.ino) and "legacy" by default
#
#    # Native avr-gcc build (for CI, headless, or advanced users)
#    cmake -B build -DUSE_NATIVE_AVR=ON -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake ..
#    cmake --build build          # Builds both "ultra" and "legacy" by default
#
#    # Explicit targets:
#    cmake --build build --target ultra
#    cmake --build build --target legacy
#    cmake --build build --target build-all
#
#  The old 2023 ExternalProject / simavr / mock mess has been removed.
#  It was non-functional and referenced libraries no longer used by the project.
#-

cmake_minimum_required(VERSION 3.16)

project(MoistureController LANGUAGES C CXX)

# -------------------------------------------------------------------
# Options
# -------------------------------------------------------------------
option(USE_ARDUINO_CLI  "Use arduino-cli to compile sketches (recommended)" ON)
option(USE_NATIVE_AVR   "Build with native avr-gcc (requires toolchain file)" OFF)

# If user explicitly asks for native AVR, turn off arduino-cli
if(USE_NATIVE_AVR)
    set(USE_ARDUINO_CLI OFF CACHE BOOL "" FORCE)
endif()

if(USE_ARDUINO_CLI AND USE_NATIVE_AVR)
    message(FATAL_ERROR "Cannot enable both USE_ARDUINO_CLI and USE_NATIVE_AVR at the same time.")
endif()

# Common compile flags for our own code
add_compile_options(-Wall -Wextra)

# -------------------------------------------------------------------
# Helper: Arduino .ino → .cpp conversion (for native builds)
# -------------------------------------------------------------------
function(arduino_ino_to_cpp ino_path out_cpp_var)
    get_filename_component(ino_name "${ino_path}" NAME)
    set(out_cpp "${CMAKE_CURRENT_BINARY_DIR}/${ino_name}.cpp")
    file(READ "${ino_path}" ino_content)
    file(WRITE "${out_cpp}"
"#include <Arduino.h>
#line 1 \"${ino_path}\"
${ino_content}")
    set(${out_cpp_var} "${out_cpp}" PARENT_SCOPE)
endfunction()

# -------------------------------------------------------------------
# arduino-cli based builds (preferred path)
# -------------------------------------------------------------------
if(USE_ARDUINO_CLI)

    find_program(ARDUINO_CLI arduino-cli REQUIRED
                 DOC "Path to arduino-cli executable")

    # Legacy root sketch
    add_custom_target(legacy
        COMMAND ${ARDUINO_CLI} compile
                --fqbn arduino:avr:nano:cpu=atmega328
                --warnings all
                --build-path "${CMAKE_BINARY_DIR}/legacy"
                "${CMAKE_SOURCE_DIR}/MoistureController.ino"
        COMMENT "Compiling legacy root sketch with arduino-cli"
        VERBATIM
    )

    # New Ultra target (recommended)
    add_custom_target(ultra
        COMMAND ${ARDUINO_CLI} compile
                --fqbn arduino:avr:nano:cpu=atmega328
                --warnings all
                --build-path "${CMAKE_BINARY_DIR}/ultra"
                "${CMAKE_SOURCE_DIR}/firmware/avr-ultra/avr-ultra.ino"
        COMMENT "Compiling firmware/avr-ultra/ (Ultra low-power target)"
        VERBATIM
    )

    add_custom_target(build-all DEPENDS ultra legacy)

    # Default target builds everything (both legacy and ultra)
    add_custom_target(default ALL DEPENDS build-all)

    message(STATUS "Using arduino-cli build mode (recommended)")
    message(STATUS "Default target is 'build-all' (builds both 'ultra' and 'legacy').")
    return()
endif()

# -------------------------------------------------------------------
# Native avr-gcc build (for CI / advanced use)
# -------------------------------------------------------------------
if(USE_NATIVE_AVR)

    if(NOT CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR
            "Native AVR build requires -DCMAKE_TOOLCHAIN_FILE=cmake/avr-toolchain.cmake "
            "(or your own avr-gcc toolchain file).")
    endif()

    # Legacy root sketch (converted)
    arduino_ino_to_cpp("${CMAKE_SOURCE_DIR}/MoistureController.ino" LEGACY_CPP)
    add_executable(legacy "${LEGACY_CPP}")
    set_target_properties(legacy PROPERTIES
        OUTPUT_NAME "MoistureController-legacy"
        SUFFIX ".elf")

    # Ultra target (converted)
    arduino_ino_to_cpp("${CMAKE_SOURCE_DIR}/firmware/avr-ultra/avr-ultra.ino" ULTRA_CPP)
    add_executable(ultra
        "${ULTRA_CPP}"
        "${CMAKE_SOURCE_DIR}/firmware/avr-ultra/Config.h"
        "${CMAKE_SOURCE_DIR}/firmware/avr-ultra/Types.h"
        "${CMAKE_SOURCE_DIR}/firmware/avr-ultra/Power.h"
    )
    set_target_properties(ultra PROPERTIES
        OUTPUT_NAME "MoistureController-ultra"
        SUFFIX ".elf")

    # Generate .hex files (very useful)
    add_custom_command(TARGET ultra POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom
                "$<TARGET_FILE:ultra>" "$<TARGET_FILE_DIR:ultra>/$<TARGET_NAME:ultra>.hex"
        COMMENT "Generating ultra.hex")

    add_custom_command(TARGET legacy POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom
                "$<TARGET_FILE:legacy>" "$<TARGET_FILE_DIR:legacy>/$<TARGET_NAME:legacy>.hex"
        COMMENT "Generating legacy.hex")

    # Build both targets by default
    add_custom_target(build-all ALL DEPENDS ultra legacy)
    add_custom_target(default ALL DEPENDS build-all)

    message(STATUS "Using native avr-gcc build mode")
    message(STATUS "Default target is 'build-all' (builds both 'ultra' and 'legacy').")
    return()
endif()

# -------------------------------------------------------------------
# Default / helpful error message
# -------------------------------------------------------------------
message(FATAL_ERROR
"MoistureController CMake configuration requires one of:

  -DUSE_ARDUINO_CLI=ON     (recommended - uses arduino-cli)
  -DUSE_NATIVE_AVR=ON      (requires -DCMAKE_TOOLCHAIN_FILE=...)

Default target is 'build-all' (builds both the modern 'ultra' and the legacy sketch).

Example:
  cmake -B build -DUSE_ARDUINO_CLI=ON ..
  cmake --build build                 # builds everything (default)
  cmake --build build --target ultra
  cmake --build build --target legacy
")
