#+
#  Copyright (c) 2023 John Franks
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
# This CMake system is used for automated build environments that leverage the
# Arduino IDE cross compilers. It allows us to build Arduino sketches from the
# command line, which is useful for continuous integration and automated testing.
#
# To install Cmake build system:
#    sudo apt-get update
#    sudo apt-get install arduino-mk
#
# To install the Arduino build system on an Ubuntu system, you can use the
# following commands:
#
#    sudo apt-get update
#    sudo apt-get install -y arduino arduino-mk
#
# After installing these packages, you can use this CMake system to compile
# and test your Arduino sketches.
#
# Testing uses test ctest environment along with simavr and mock libraries.
# SimAVR is an AVR simulator for linux. It uses avr-gcc's own register
# definition to simplify creating new targets for supported AVR devices.
# This allows compilation without source modification, fully working GDB
# support, output of waveforms to view with gtkwave, and full device
# simulation including 
#
# To install SimAVR for testing use the following commands:
#    sudo apt-get update
#    sudo apt-get install simavr
#-
cmake_minimum_required(VERSION 3.10)


# Set the compilers based on the BUILD_TYPE option
if("$ENV{BUILD_TYPE}" STREQUAL "test")
	# Use local compiler for test build
	set(CMAKE_C_COMPILER "clang")
	set(CMAKE_CXX_COMPILER "clang++")
	set(ARDUINO_PATH $ENV{ARDUINO_TOOL_PATH})
	set(ARDUINO_LIB_PATH $ENV{ARDUINO_LIB_PATH})
elseif("$ENV{BUILD_TYPE}" STREQUAL "release")
	# Use cross compiler for Arduino release builds
	set(CMAKE_C_COMPILER "avr-gcc")
	set(CMAKE_CXX_COMPILER "avr-g++")
	set(ARDUINO_PATH $ENV{ARDUINO_TOOL_PATH})
	set(ARDUINO_LIB_PATH $ENV{ARDUINO_LIB_PATH})
endif()
add_compile_options(-Wall -Wextra -Werror)

# Set the project name
project(MoistureController)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Enable testing
enable_testing()

# Find the arduino and simavr libraries and add the executables, libraries, and include paths
find_package(ArduinoPipeline)

# Fetch arduino sources from github
set(ArduinoCore_ROOT ${CMAKE_BINARY_DIR}/src/AnalogComp)
ExternalProject_Add(ArduinoMock
	GIT_REPOSITORY https://github.com/berrak/Mock.git
	GIT_TAG master
	PREFIX ${ArduinoCore_ROOT}
	CONFIGURE_COMMAND ""
	BUILD_COMMAND     ""
)
set(Arduino_INCLUDE_DIRS ${LowPower_ROOT}/include)
set(ArduinoCore_INCLUDE_DIRS xxx)

set(LowPower_ROOT ${CMAKE_BINARY_DIR}/src/AnalogComp)
ExternalProject_Add(LowPower
	GIT_REPOSITORY https://github.com/rocketscream/Low-Power.git
	GIT_TAG master
	PREFIX ${ArduinoCore_ROOT}
	CONFIGURE_COMMAND ""
	BUILD_COMMAND     ""
)
set(LowPower_INCLUDE_DIRS ${LowPower_ROOT}/include)

set(AnalogComp_ROOT ${CMAKE_BINARY_DIR}/src/AnalogComp)
ExternalProject_Add(AnalogComp
	GIT_REPOSITORY https://github.com/leomil72/analogComp.git
	GIT_TAG master
	PREFIX ${AnalogComp_ROOT}
	CONFIGURE_COMMAND ""
	BUILD_COMMAND     ""
)
set(ArduinoCore_INCLUDE_DIRS ${AnalogComp_ROOT}/include)

# Preprocess Arduino .ino files for c++ compiler
function(convert_ino_to_cpp ino_file)
    file(READ ${ino_file} CONTENTS)
    file(WRITE ${ino_file}.cpp "#include <Arduino.h>\n#line 1 "${ino_file}"\n${CONTENTS}")
endfunction()

# Preprocess Arduino sketch
convert_ino_to_cpp(MoistureController.ino)

# Define MoisterController parameters
add_executable(MoistureController MoistureController.ino.cpp)
target_link_libraries(MoistureController ${ARDSIM_LIB})
target_include_directories(MoistureController PUBLIC "${ARDUINO_LIB_PATH}/include")

# Define MoisterController.hex parameters
add_custom_command(
  OUTPUT arduino.out
  COMMAND arduino --verbose --verify --board arduino:avr:uno MoistureController.ino
  DEPENDS my_source.cpp
  COMMENT "Building build .ino using arduino IDE"
)

add_custom_target(
  MyExecutable MoisureController.hex
  DEPENDS MoistureController.ino
)
add_executable(MoistureControllerTest.hex MoistureController.ino)
add_custom_command(TARGET MoistureControllerTest POST_BUILD COMMAND MoistureControllerTest)

# Link the ardsim library to the test executable
target_link_libraries(MoistureControllerTest ${ARDSIM_LIB})

# Include the Arduino libraries for the test executable
target_include_directories(MoistureControllerTest PUBLIC "${ARDUINO_LIB_PATH}/include")

# Add a custom command to run the tests after the build
add_custom_command(TARGET MoistureControllerTest POST_BUILD COMMAND MoistureControllerTest)

# Add a custom target to run code analysis tools
add_custom_target(CodeAnalysis ALL
                  COMMAND your-code-analysis-tool ${CMAKE_SOURCE_DIR}/*.cpp
                  COMMAND your-style-check-tool ${CMAKE_SOURCE_DIR}/*.cpp)

# Add a custom target to create an approval file if all checks pass
add_custom_target(Approval ALL
                  COMMAND echo "All checks passed" > approval.txt
                  COMMAND sha1sum ${CMAKE_SOURCE_DIR}/*.cpp ${CMAKE_SOURCE_DIR}/*.h > approval.txt
                  DEPENDS CodeAnalysis)

add_test(NAME checkin COMMAND tests/checkin-tests)
add_test(NAME release COMMAND tests/release-tests)
add_test(NAME lint COMMAND tests/lint-tests)
