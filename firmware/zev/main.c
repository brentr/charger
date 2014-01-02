#include <ch.h>
#include <hal.h>
#include <chprintf.h>
#include <stm32_tim.h>

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


/* The pins PC0 - 2 are analog inputs
    PC0 = High Voltage
    PC1 = Charger voltage setpoint feedback
    PC2 = Overvoltage from celltops
*/
#define ANALOGINS_C    GPIOC, 0x7, 0

/* The pins PA1 - 2 are also analog inputs
    PA1 = Charger Current Sensor VCC/2 (nominally 2.5V)
    PA2 = Charger Current Sensor (proportional to PA1)
*/
#define ANALOGINS_A    GPIOA, 0x6, 1

/* Total number of channels to be sampled by a single ADC operation.*/
#define ADCchannels   5

/* Depth of the conversion buffer, channels are sampled sixteen times each.*/
#define ADCdepth      64

#define ADCsamples    (ADCchannels*ADCdepth)
/*
 * Raw ADC sample buffer.
 */
adcsample_t analogSample[2*ADCsamples];

/* box car averaging for current measurements */
#define boxLen     50
adcsample_t vcc2[boxLen], delta[boxLen];
unsigned boxCount;

/*
 * Generate ADCsamples pulses every 1/20second
 */
#define adcTimeBase 8000000
#define adcTimerDivisor (adcTimeBase/(20*ADCdepth))

#if adcTimerDivisor >= 1<<16
#error  adcTimerDivisor too large
#endif

/*
 * ADC conversion group.
 * Mode:        Linear buffer, 4 samples of 2 channels, SW triggered.
 * Channels:    IN10, IN11, IN12, IN13   (16 cycles sample time)
 */
#define adcSampleTime  ADC_SAMPLE_16
 //Timer 6 trigger
#define adcTrigger (ADC_CR2_EXTSEL_1 | ADC_CR2_EXTSEL_3 | ADC_CR2_EXTEN_0)
#define adcTimer STM32_TIM6
#define adcTimerClkRate  STM32_PCLK1
#define enableAdcTimer() rccEnableAPB1(RCC_APB1ENR_TIM6EN, FALSE)
#define disableAdcTimer() rccDisableAPB1(RCC_APB1ENR_TIM6EN, FALSE)

static unsigned totalSamples = 0, totalErrs = 0, count = 0;

static Thread *waitingAnalogThread = NULL;

static void adcDone(ADCDriver *adcp, adcsample_t *buffer, size_t n);

static void adcErr(ADCDriver *adcp, adcerror_t err)
{
  (void)adcp; (void)err;
  totalErrs++;  //restart app if this occurs
}

static INLINE void setAdcTimebase(uint32_t hz)
{
  adcTimer->PSC = (uint16_t)((adcTimerClkRate / hz) - 1);
}

/**
 * @brief   Starts the timer in continuous mode.
 */
static INLINE void startAdcTimer(uint16_t interval) {
  stm32_tim_t *tim = adcTimer;
  tim->ARR   = interval - 1;              /* Time constant.           */
  tim->EGR   = STM32_TIM_EGR_UG;          /* Update event.            */
  tim->CNT   = 0;                         /* Reset counter.           */
  tim->DIER  = 0;        /* no interrupts enabled */
  tim->CR2   = STM32_TIM_CR2_MMS(2);      /* output TRGO pulse on update  */
  tim->CR1   = STM32_TIM_CR1_URS | STM32_TIM_CR1_CEN;
}

static const ADCConversionGroup adcgrpcfg = {
  TRUE,
  ADCchannels,
  adcDone,
  adcErr,
  /* HW dependent part.*/
  0,                          /* CR1 */
  adcTrigger,                 /* CR2 -- trigger */
  0,
  ADC_SMPR2_SMP_AN10(adcSampleTime) | ADC_SMPR2_SMP_AN11(adcSampleTime) |
   ADC_SMPR2_SMP_AN12(adcSampleTime),
  ADC_SMPR3_SMP_AN1(adcSampleTime) | ADC_SMPR3_SMP_AN2(adcSampleTime),
  ADC_SQR1_NUM_CH(ADCchannels),
  0,
  0,
  0,
  ADC_SQR5_SQ1_N(ADC_CHANNEL_IN10) | ADC_SQR5_SQ2_N(ADC_CHANNEL_IN11) |
   ADC_SQR5_SQ3_N(ADC_CHANNEL_IN12) |
   ADC_SQR5_SQ4_N(ADC_CHANNEL_IN1) | ADC_SQR5_SQ5_N(ADC_CHANNEL_IN2)
};


static void adcDone(ADCDriver *adcp, adcsample_t *buffer, size_t n)
{
  (void)n; (void)adcp;
  DAC->DHR12R1 = (DAC->DOR1+1) & 0xfff;    //update DAC
  /* Wake any waiting analog procesing thread */
  if (waitingAnalogThread) {   //indicate which buffer to read
    chSysLockFromIsr();
    waitingAnalogThread->p_u.rdymsg = (msg_t) buffer;
    chSchReadyI(waitingAnalogThread);
    waitingAnalogThread = NULL;
    chSysUnlockFromIsr();
  }
}


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
  const char signon[] = "ZEV Charger v0.14 -- 1/1/14 brent@mbari.org";
  debugPuts(signon);

  /*
   * Activate serial driver 1 using the driver default configuration.
   * PA9 through PA12 are routed to USART1.
   */
  sdStart(&SD1, NULL);
  configureGroup(GPIOA, 0xf, 9, PAL_MODE_ALTERNATE(7)); //TX,RX,CTS,RTS

  chprintf(&SD1, "\r\n%s\r\n", signon);

  /*
   *  Piezo buzzer output
   */
  configurePad(BUZZER, PAL_MODE_OUTPUT_OPENDRAIN);

  /*
   * Enable DAC channel 1 on PA4
   */
  configureGroup(ANALOGOUTS, PAL_MODE_INPUT_ANALOG);
  rccEnableAPB1(RCC_APB1ENR_DACEN, FALSE);
  DAC->CR = DAC_CR_EN1;

  /*
   * Configure Adc Timer
   */
  enableAdcTimer();
  setAdcTimebase(adcTimeBase);
  startAdcTimer(adcTimerDivisor);

  /*
   * Initializes the ADC driver 1
   */
  configureGroup(ANALOGINS_C, PAL_MODE_INPUT_ANALOG);
  configureGroup(ANALOGINS_A, PAL_MODE_INPUT_ANALOG);
  adcStart(&ADCD1, NULL);
  adcStartConversion(&ADCD1, &adcgrpcfg, analogSample, 2*ADCdepth);

  adcsample_t *samples;
  uint32_t adc[ADCchannels];  //filtered adc inputs

  int32_t deltaOut = 0, vcc2out = 0;

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

    clearPad(GREEN_LED);
    clearPad(BUZZER);

    /* Wait for ADC conversions to complete */
    chSysLock();
    waitingAnalogThread = chThdSelf();
    chSchGoSleepS(THD_STATE_SUSPENDED);
    samples = (adcsample_t *)(chThdSelf()->p_u.rdymsg);
    chSysUnlock();

    totalSamples++;
    if (samples==analogSample) {
      setPad(GREEN_LED);
      setPad(BUZZER);
    }
    /* Calculate the sum of values from the ADC samples.*/
    unsigned chan;
    adcsample_t *end = samples + ADCsamples;
    for(chan=0; chan < ADCchannels; chan++) {
      adcsample_t *row = samples+chan;
      uint32_t *cursor = adc+chan;
      *cursor=0;
      do {
        *cursor += *row;
        row += ADCchannels;
      } while (row < end);
      *cursor /= ADCdepth;  //avg just for display for now
    }

    if (++boxCount >= boxLen)
      boxCount=0;
    deltaOut -= delta[boxCount];
    deltaOut += (delta[boxCount] = adc[3]-adc[4]);
    vcc2out -= vcc2[boxCount];
    vcc2out += (vcc2[boxCount] = adc[3]);

    float amps = 41.08 * (float)deltaOut / vcc2out - 2.925;
    chprintf(&SD1, "#%d:%s: Vcmd=%d, Vin=%d, VcmdIn=%d, Thres=%d, Vcc/2=%d, Curr=%d, A=%f\r\n",
	 totalSamples, power, DAC->DOR1, adc[0], adc[1], adc[2], vcc2out, deltaOut, amps);
    if (++count >= 10) {
      debugPrint("@%d#%d:%s:Vcmd=%d,Vin=%d,VcmdIn=%d,Thres=%d, Vcc/2=%d, Curr=%d, A=%f (%d errs)",
        chTimeNow(), totalSamples,
                      power, DAC->DOR1, adc[0], adc[1], adc[2], vcc2out, deltaOut, amps, totalErrs);
      count = 0;
    }
  }
}
