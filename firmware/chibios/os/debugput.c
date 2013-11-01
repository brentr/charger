/**********************  debugput.c  ************************
*
*  Display debug output on host via ARM DCC
*
*  A low priority thread empties the debugOutput queue to the
*  host debugger communication port
*
*  Data printed to the queue when full are discarded
*  (Never blocks waiting for the host)
*
*  Does not support output from interrupt handlers
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
static WORKING_AREA(debugReaderArea, 128);
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
  while((b = chOQGetI(q)) == Q_EMPTY)
    chSchGoSleepS(THD_STATE_WTQUEUE);
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
/*
  returns -1 if output fails
*/
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
  returns # of characters actually output (including the trailing newline)
*/
{
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
        n++;
      }
    }else
      n = 0;
    chMtxUnlock();
  }else
    if (debugPutc('\n') >= 0)
      n = 1;
  return n;
}

size_t debugPuts(const char *str)
{
  return debugPut( (const uint8_t *)str, strlen(str) );
}


#if debugPrintBufSize < 0
/*
  printf like debug messages to host via ARM DCC
  RAM is precious, so we expand the printf string twice;
  once to determine its length and again to queue it.
*/

struct NullStreamVMT {
   _base_sequential_stream_methods
};

typedef struct  {
  const struct NullStreamVMT *vmt;
  size_t  len;
} NullStream;

static size_t nullWrites(void *ip, const uint8_t *bp, size_t n) {
  NullStream *nsp = ip;
  (void)bp;
  if (debugPrintBufSize - nsp->len < n)
    n = debugPrintBufSize - nsp->len;
  nsp->len += n;
  return n;
}

static size_t nullReads(void *ip, uint8_t *bp, size_t n) {
  (void)ip; (void) bp; (void) n;
  return RDY_RESET;
}

static msg_t nullPut(void *ip, uint8_t b) {
  NullStream *nsp = ip;
  (void)b;
  if (nsp->len >= debugPrintBufSize)
    return RDY_RESET;
  nsp->len++;
  return RDY_OK;
}

static msg_t nullGet(void *ip) {
  (void)ip;
  return RDY_RESET;
}

static const struct NullStreamVMT nullVmt =
  {nullWrites, nullReads, nullPut, nullGet};


struct qStreamVMT {
   _base_sequential_stream_methods
};


typedef struct  {
  const struct qStreamVMT *vmt;
  size_t  space;
} qStream;


static size_t qwrites(void *ip, const uint8_t *bp, size_t n) {
  qStream *qsp = ip;
  if (n > qsp->space)
    n = qsp->space;
  qsp->space -= n;
  chOQWriteTimeout( &debugOutQ, bp, n, TIME_IMMEDIATE);
  return n;
}

static msg_t qput(void *ip, uint8_t b) {
  qStream *qsp = ip;
  if (!qsp->space)
    return RDY_RESET;
  --qsp->space;
  chOQPutTimeout( &debugOutQ, b, TIME_IMMEDIATE);
  return RDY_OK;
}

static const struct qStreamVMT qVmt = {qwrites, nullReads, qput, nullGet};


size_t debugPrint(const char *fmt, ...)
/*
  printf style debugging output
  outputs a trailing newline
*/
{
  va_list ap;
  va_start(ap, fmt);
  NullStream lenStream = {&nullVmt, 0};
  chvprintf((BaseSequentialStream *) &lenStream, fmt, ap);
  size_t len = lenStream.len;
  if (len) {
    chMtxLock(&debugOutLock);
    size_t qspace = chOQGetEmptyI(&debugOutQ);
    if (qspace) {
      if (len >= qspace)
        len = qspace - 1;  //truncate string if it won't fit in queue
      if (len) {
        if (len > 255)
          len = 255;
        qStream dbgStream = {&qVmt, len};
        chOQPutTimeout(&debugOutQ, len, TIME_IMMEDIATE);
        chvprintf((BaseSequentialStream *) &dbgStream, fmt, ap);
        resumeReader();
        len++;
      }
    }else
      len=0;
    chMtxUnlock();
  }else
    if (debugPutc('\n') >= 0)
      len=1;
  va_end(ap);
  return len;
}

#elif debugPrintBufSize > 0  //use global buffer to avoid expanding printf twice

size_t debugPrint(const char *fmt, ...)
/*
  printf style debugging output
  outputs a trailing newline
*/
{
  size_t len;
  va_list ap;
  va_start(ap, fmt);
  static MUTEX_DECL(debugPrintLock);
  static uint8_t buf[debugPrintBufSize];
  static MemoryStream dbgStream;
  chMtxLock(&debugPrintLock);
  msObjectInit(&dbgStream, buf, sizeof(buf), 0);
  chvprintf((BaseSequentialStream *) &dbgStream, fmt, ap);
  va_end(ap);
  debugPut(buf, len=dbgStream.eos);
  chMtxUnlock();
  return len;
}

#endif


void logPanic(const char *panicTxt)
/*
  intended to be called from the SYSTEM_HALT_HOOK
*/
{
  if (!panicTxt)
    panicTxt = "<stack crash>";
  debugPuts("\nPANIC!");
  debugPuts(panicTxt);
  chSysLock();
  chSchGoSleepS(THD_STATE_FINAL);
}

