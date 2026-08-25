# Host unit tests for firmware/avr-ultra/Logic.h (no Arduino, no AVR).

CXX ?= g++
CXXFLAGS ?= -std=c++11 -Wall -Wextra -Werror
INCLUDES := -I../../firmware/avr-ultra
CPPFLAGS := -DHOST_TEST

.PHONY: test clean

test: run
	./run

run: test_main.cpp ../../firmware/avr-ultra/Logic.h ../../firmware/avr-ultra/Types.h ../../firmware/avr-ultra/Policy.h ../../firmware/avr-ultra/Config.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(INCLUDES) -o $@ test_main.cpp

clean:
	rm -f run
