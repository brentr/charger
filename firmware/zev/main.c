#include <ch.h>
#include <hal.h>
#include <chprintf.h>

#include "debugput.h"

char debugOutput[300];  //debugging output awaiting transmission to host

//#define debugPrint(fmt,...) chprintf(&SD1, fmt, __VA_ARGS__)
//#define debugPuts(str) debugPrint("%d/r/n", str)

#define clearPad(...) palClearPad(__VA_ARGS__)
#define setPad(...) palSetPad(__VA_ARGS__)
#define togglePad(...) palTogglePad(__VA_ARGS__)
#define configurePad(...)  palSetPadMode(__VA_ARGS__)
#define configureGroup(...)  palSetGroupMode(__VA_ARGS__)

/*  Discrete Digital Outputs  */
#define GREEN_LED   GPIOB,GPIOB_LED3
#define BLUE_LED    GPIOB,GPIOB_LED4
#define BUZZER      GPIOC,9
#define CHARGER     GPIOC,8

/* Only PA4 and PA5 can be used for analog output
    PA4 = Charger voltage set point
*/
#define ANALOGOUTS    GPIOA, 0x1, 4


/* Total number of channels to be sampled by a single ADC operation.*/
#define ADC_GRP1_NUM_CHANNELS   3

/* The pins PC0 - 2 on the port GPIOC are analog inputs
    PC0 = High Voltage
    PC1 = Charger voltage setpoint feedback
    PC2 = Overvoltage from celltops
*/
#define ANALOGINS    GPIOC, 0x7, 0

/* Depth of the conversion buffer, channels are sampled four times each.*/
#define ADC_GRP1_BUF_DEPTH      4

/*
 * ADC samples buffer.
 */
static adcsample_t samples[ADC_GRP1_NUM_CHANNELS * ADC_GRP1_BUF_DEPTH];

/*
 * ADC conversion group.
 * Mode:        Linear buffer, 4 samples of 2 channels, SW triggered.
 * Channels:    IN10, IN11, IN12   (16 cycles sample time)
 */
#define adcSampleTime  ADC_SAMPLE_16

static const ADCConversionGroup adcgrpcfg = {
  FALSE,
  ADC_GRP1_NUM_CHANNELS,
  NULL,
  NULL,
  /* HW dependent part.*/
  0,                        /* CR1 */
  ADC_CR2_SWSTART,          /* CR2 */
  0,
  ADC_SMPR2_SMP_AN10(adcSampleTime) | ADC_SMPR2_SMP_AN11(adcSampleTime) |
   ADC_SMPR2_SMP_AN12(adcSampleTime),
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
  configurePad(CHARGER, PAL_MODE_OUTPUT_OPENDRAIN);
  clearPad(CHARGER);  //turn off charger ASAP

  debugPrintInit(debugOutput);
  const char signon[] = "ZEV Charger v0.05 -- 11/20/13 brent@mbari.org";
  debugPuts(signon);

  /*
   * Activate serial driver 1 using the driver default configuration.
   * PA9 through PA12 are routed to USART1.
   */
  sdStart(&SD1, NULL);
  palSetGroupMode(GPIOA, 0xf, 9, PAL_MODE_ALTERNATE(7)); //TX,RX,CTS,RTS

  chprintf(&SD1, "\r\n%s\r\n", signon);

  /*
   *  Piezo buzzer output
   */
  configurePad(BUZZER, PAL_MODE_OUTPUT_OPENDRAIN);

  /*
   * Initializes the ADC driver 1
   */
  configureGroup(ANALOGINS, PAL_MODE_INPUT_ANALOG);
  adcStart(&ADCD1, NULL);

  /*
   * Enable DAC channel 1 on PA4
   */
  configureGroup(ANALOGOUTS, PAL_MODE_INPUT_ANALOG);
  RCC->APB1ENR |= RCC_APB1ENR_DACEN;
  DAC->CR = DAC_CR_EN1;

  while (1) {
    int key;

    while ((key = chnGetTimeout(&SD1, TIME_IMMEDIATE)) != Q_TIMEOUT)
      if (!(key & ~0x7f)) {
        switch (key) {
          case '0':  //turn off power supply
            clearPad(CHARGER);
            power = "off";
            break;
          case '1':  //turn on power supply
            if (chTimeNow() > 5000) {
              setPad(CHARGER);
              power = "ON ";
            }
            break;
        }
      }

    DAC->DHR12R1 = (DAC->DOR1+1) & 0xfff;

    setPad(GREEN_LED);
    setPad(BUZZER);
    chThdSleepMilliseconds(30);
    clearPad(GREEN_LED);
    clearPad(BUZZER);
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
