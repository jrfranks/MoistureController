/* Native avr-gcc smoke: compile Logic.h without the Arduino core. */
#include "Logic.h"

int main() {
  const uint8_t s[] = {1, 2, 3, 4};
  volatile uint16_t c = crc16_ccitt(s, 4);
  (void)c;
  return 0;
}
