#include <stdint.h>
#include "config.h"

typedef enum {
  rttyZero = 0,
  rttyOne = 1,
  rttyEnd = 2
} rttyStates;
static const uint8_t RTTY_PRE_START_BITS = 1; // No pre-start bits, as it throws off timing estimation in FSK demods.

rttyStates send_rtty(char *);
extern uint8_t start_bits;
