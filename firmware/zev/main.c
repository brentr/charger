#include <ch.h>
#include <hal.h>
#include <chprintf.h>

#include "debugput.h"

//#define debugPrint(fmt,...) chprintf(&SD1, fmt, __VA_ARGS__)
//#define debugPuts(str) debugPrint("%d/r/n", str)

char debugOutput[300];  //debugging output awaiting transmission to host

/* Total number of channels to be sampled by a single ADC operation.*/
#define ADC_GRP1_NUM_CHANNELS   3

/* Depth of the conversion buffer, channels are sampled four times each.*/
#define ADC_GRP1_BUF_DEPTH      4

/*
 * ADC samples buffer.
 */
static adcsample_t samples[ADC_GRP1_NUM_CHANNELS * ADC_GRP1_BUF_DEPTH];

/*
 * ADC conversion group.
 * Mode:        Linear buffer, 4 samples of 2 channels, SW triggered.
 * Channels:    IN10, IN11   (48 cycles sample time)
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
  ADC_SMPR2_SMP_AN10(ADC_SAMPLE_48) | ADC_SMPR2_SMP_AN11(ADC_SAMPLE_48) |
   ADC_SMPR2_SMP_AN12(ADC_SAMPLE_48),
  0,
  ADC_SQR1_NUM_CH(ADC_GRP1_NUM_CHANNELS),
  0,
  0,
  0,
  ADC_SQR5_SQ1_N(ADC_CHANNEL_IN10) | ADC_SQR5_SQ2_N(ADC_CHANNEL_IN11) |
  ADC_SQR5_SQ3_N(ADC_CHANNEL_IN12)
};


int main(void) {
  halInit();
  chSysInit();

  /*
   *  Disable Power Supply
   */
  const char *power = "off";
  palSetPadMode(GPIOC, 8, PAL_MODE_OUTPUT_OPENDRAIN);
  palClearPad(GPIOC, 8);

  debugPrintInit(debugOutput);
  const char signon[] = "ZEV Charger v0.04 -- 11/16/13 brent@mbari.org";
  debugPuts(signon);

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
   * The pins PC0 and PC1 on the port GPIOC are programmed as analog input.
   */
  palSetPadMode(GPIOC, 0, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOC, 1, PAL_MODE_INPUT_ANALOG);
  palSetPadMode(GPIOC, 2, PAL_MODE_INPUT_ANALOG);
  adcStart(&ADCD1, NULL);
  adcSTM32EnableTSVREFE();

  /*
   * Enable DAC channel 1
   */
  palSetPadMode(GPIOA, 4, PAL_MODE_INPUT_ANALOG);
  RCC->APB1ENR |= RCC_APB1ENR_DACEN;
  DAC->CR = DAC_CR_EN1;

  /*
   *  Piezo buzzer output
   */
  palSetPadMode(GPIOC, 9, PAL_MODE_OUTPUT_OPENDRAIN);
  

  while (1) {
    int key;

    while ((key = chnGetTimeout(&SD1, TIME_IMMEDIATE)) != Q_TIMEOUT)
      if (!(key & ~0x7f)) {
        switch (key) {
          case '0':  //turn off power supply
            palClearPad(GPIOC, 8);
            power = "off";
            break;
          case '1':  //turn on power supply
            if (chTimeNow() > 5000) {
              palSetPad(GPIOC, 8);
              power = "ON ";
            }
            break;
        }
      }

    DAC->DHR12R1 = (DAC->DOR1+1) & 0xfff;

    palSetPad(GPIOB, GPIOB_LED3);  //Green
    palSetPad(GPIOC, 9);
    chThdSleepMilliseconds(25);
    palClearPad(GPIOB, GPIOB_LED3);
    palClearPad(GPIOC, 9);
    chThdSleepMilliseconds(35);

    msg_t err = adcConvert(&ADCD1, &adcgrpcfg, samples, ADC_GRP1_BUF_DEPTH);
    if (!err) {
      adcsample_t avg_ch1, avg_ch2, avg_ch3;

      /* Calculates the average values from the ADC samples.*/
      avg_ch1 = (samples[0] + samples[3] + samples[6] + samples[9]) / 4;
      avg_ch2 = (samples[1] + samples[4] + samples[7] + samples[10]) / 4;
      avg_ch3 = (samples[2] + samples[5] + samples[8] + samples[11]) / 4;

      debugPrint("%s:Vcmd=%d,Vin=%d,VcmdIn=%d,Thres=%d",
	  	 power, DAC->DOR1, avg_ch1, avg_ch2, avg_ch3);
      chprintf(&SD1, "%s: Vcmd=%d, Vin=%d, VcmdIn=%d, Thres=%d\r\n",
	  	 power, DAC->DOR1, avg_ch1, avg_ch2, avg_ch3);
    }else
      debugPrint("adcConvert returned err #%d", err);
  }
}
