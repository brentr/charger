#include <ch.h>
#include <hal.h>

#include "debugput.h"

char debugOutput[300];  //debugging output awaiting transmission to host


int main(void) {
  unsigned count = 0;
  halInit();
  chSysInit();
  debugPrintInit(debugOutput);

  palSetPadMode(GPIOB, 7, PAL_MODE_OUTPUT_PUSHPULL);

  palSetPad(GPIOB, 7);
  chThdSleepMilliseconds(400);
  palClearPad(GPIOB, 7);
  chThdSleepMilliseconds(250);

  debugPuts("ChiDemo Blinky v1.1 -- 10/27/13 brent@mbari.org");

  while (1) {
    palSetPad(GPIOB, 7);
    debugPutc('+');
    chThdSleepMilliseconds(500);
    palClearPad(GPIOB, 7);
    debugPutc('-');
    chThdSleepMilliseconds(250);
    if (!(++count % 32))
      debugPutc('\n');
  }
}
