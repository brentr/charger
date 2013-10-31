#include <ch.h>
#include <hal.h>
#include <chprintf.h>

#include "debugput.h"

//#define debugPrint(fmt,...) chprintf(&SD1, fmt, __VA_ARGS__)
//#define debugPuts(str) debugPrint("%d/r/n", str)

char debugOutput[300];  //debugging output awaiting transmission to host

/* Total number of channels to be sampled by a single ADC operation.*/
#define ADC_GRP1_NUM_CHANNELS   2

/* Depth of the conversion buffer, channels are sampled four times each.*/
#define ADC_GRP1_BUF_DEPTH      4

/*
 * ADC samples buffer.
 */
static adcsample_t samples[ADC_GRP1_NUM_CHANNELS * ADC_GRP1_BUF_DEPTH];

/*
 * ADC conversion group.
 * Mode:        Linear buffer, 4 samples of 2 channels, SW triggered.
 * Channels:    IN10   (48 cycles sample time)
 *              Sensor (192 cycles sample time)
 */
static const ADCConversionGroup adcgrpcfg = {
  FALSE,
  ADC_GRP1_NUM_CHANNELS,
  NULL,
  NULL,
  /* HW dependent part.*/
  0,                        /* CR1 */
  ADC_CR2_SWSTART,          /* CR2 */
  0,
  ADC_SMPR2_SMP_AN10(ADC_SAMPLE_48) | ADC_SMPR2_SMP_SENSOR(ADC_SAMPLE_192),
  0,
  ADC_SQR1_NUM_CH(ADC_GRP1_NUM_CHANNELS),
  0,
  0,
  0,
  ADC_SQR5_SQ2_N(ADC_CHANNEL_IN10) | ADC_SQR5_SQ1_N(ADC_CHANNEL_SENSOR)
};


int main(void) {
  halInit();
  chSysInit();
  debugPrintInit(debugOutput);

  const char signon[] = "ZEV Charger v0.02 -- 10/30/13 brent@mbari.org";
  debugPuts(signon);

  palSetPadMode(GPIOB, 7, PAL_MODE_OUTPUT_PUSHPULL);

  /*
   * Activated serial driver 1 using the driver default configuration.
   * PA9 and PA10 are routed to USART1.
   */
  sdStart(&SD1, NULL);
  palSetPadMode(GPIOA, 9, PAL_MODE_ALTERNATE(7));   //TX
  palSetPadMode(GPIOA, 10, PAL_MODE_ALTERNATE(7));  //RX
  palSetPadMode(GPIOA, 11, PAL_MODE_ALTERNATE(7));  //CTS
  palSetPadMode(GPIOA, 12, PAL_MODE_ALTERNATE(7));  //RTS
  
  chprintf(&SD1, "\r\n%s\r\n", signon);

  /*
   * Initializes the ADC driver 1 and enable the thermal sensor.
   * The pin PC0 on the port GPIOC is programmed as analog input.
   */
  adcStart(&ADCD1, NULL);
  adcSTM32EnableTSVREFE();
  palSetPadMode(GPIOC, 0, PAL_MODE_INPUT_ANALOG);

  while (1) {
    int key;
    while ((key = chnGetTimeout(&SD1, TIME_IMMEDIATE)) != Q_TIMEOUT)
      if (key == (key & 0x7f) && chnPutTimeout(&SD1, key, 10) == Q_TIMEOUT)
        debugPrint("\nCan't write key code 0x%02x", key);
    palSetPad(GPIOB, 7);
    chThdSleepMilliseconds(500);
    palClearPad(GPIOB, 7);
    chThdSleepMilliseconds(250);

    msg_t err = adcConvert(&ADCD1, &adcgrpcfg, samples, ADC_GRP1_BUF_DEPTH);
    if (!err) {
      adcsample_t avg_ch1, avg_ch2;

      /* Calculates the average values from the ADC samples.*/
      avg_ch1 = (samples[0] + samples[2] + samples[4] + samples[6]) / 4;
      avg_ch2 = (samples[1] + samples[3] + samples[5] + samples[7]) / 4;

      debugPrint("ADC ch1 reads %d counts, ch2 reads %d", avg_ch1, avg_ch2);
    }else
      debugPrint("adcConvert returned err #%d", err);
  }
}
