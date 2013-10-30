/**********************  debugput.c  ************************
*
*  Display debug output on host via ARM DCC
*
*  A low priority thread empties the debugOutput queue to the
*  host debugger communication port
*
*  Data printed to the queue when full are silently discarded
*  (It never blocks)
*
*  Any ChibiOS panic messages are output to the host
*
***************************************************************/

#include "debugput.h"

#include <string.h>
#include <memstreams.h>
#include <chprintf.h>

#include "dccput.h"

/*
 * Small working area for the debug output thread
 */
static Thread *debugReader;
static WORKING_AREA(debugReaderArea, 32);
static OutputQueue debugOutQ;
static MUTEX_DECL(debugOutLock);

static void resumeReader(void)
/*
  called whenever debug data is written
  to resume the queue reader
*/
{
  chSysLock();
  if(debugReader->p_state == THD_STATE_WTQUEUE)
    chSchWakeupS(debugReader, 0);
  chSysUnlock();
}


static uint8_t fetcher(void *outQ)
/*
  return next byte in queue
  block if empty
*/
{
  OutputQueue *q = outQ;
  msg_t b;
  chSysLock();
  while(TRUE) {
    b = chOQGetI(q);
    if (b != Q_EMPTY)
      break;
    chSchGoSleepS(THD_STATE_WTQUEUE);
  }
  chSysUnlock();
  return b;
}

/*
 * This thread empties the debug output queue
 */
__attribute__((noreturn))
static msg_t debugReaderMain(void *arg)
{
  (void) arg;
  while (TRUE) {
    size_t len = fetcher(&debugOutQ);
    if (len)
      DCCputsQ(fetcher, &debugOutQ, len);
    else
      DCCputc(fetcher(&debugOutQ));
  }
}


Thread *debugPutInit(char *outq, size_t outqSize)
/*
  allocate output queue of outqSize bytes and start background thread
  return background thread
*/
{
  chOQInit(&debugOutQ, (uint8_t *)outq, outqSize, NULL, NULL);  
  return debugReader = 
    chThdCreateStatic(debugReaderArea, sizeof(debugReaderArea),
                          LOWPRIO, debugReaderMain, NULL);
}


int debugPutc(int c)
{
  chMtxLock(&debugOutLock);
  if (chOQGetEmptyI(&debugOutQ) > 1) {
    chOQPutTimeout( &debugOutQ, 0, TIME_IMMEDIATE);
    chOQPutTimeout( &debugOutQ, c, TIME_IMMEDIATE);
  }else
    c = -1;
  resumeReader();
  chMtxUnlock();
  return c;
}


size_t debugPut(const uint8_t *block, size_t n)
/*
  truncate any block > 255 bytes
*/
{
  if (chOQIsFullI(&debugOutQ))
    return -1;
  if (n) {
    chMtxLock(&debugOutLock);
    size_t space = chOQGetEmptyI(&debugOutQ);
    if (space) {
      if (n >= space)
        n = space - 1;
      if (n) {
        if (n > 255)
          n = 255;
        chOQPutTimeout( &debugOutQ, n, TIME_IMMEDIATE);
        chOQWriteTimeout( &debugOutQ, block, n, TIME_IMMEDIATE);
        resumeReader();
      }
    }
    chMtxUnlock();
  }else
    debugPutc('\n');
  return n;
}

size_t debugPuts(const char *str)
{
  return debugPut( (const uint8_t *)str, strlen(str) );
}

size_t debugPrint(const char *fmt, ...)
/*
  printf style debugging output
  outputs a trailing newline
*/
{
  uint8_t buf[255];
  MemoryStream stream;
  va_list ap;
  va_start(ap, fmt);
  msObjectInit(&stream, buf, sizeof(buf), 0);
  chvprintf((BaseSequentialStream *) &stream, fmt, ap);
  va_end(ap);
  debugPut(buf, stream.eos);
  return stream.eos;
}


void logPanic(const char *panicTxt)
/*
  intended to be called from the SYSTEM_HALT_HOOK
*/
{
  if (!panicTxt)
    panicTxt = "<stack crash>";
  debugPuts("PANIC!");
  debugPuts(panicTxt);
  chSysLock();
  chSchGoSleepS(THD_STATE_FINAL);
}

