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
*  Does not support output from interrupt handlers
*
*  Any ChibiOS panic messages are output to the host
*
*  This code is an example of using the openocd debug message system.
*
*  Before the message output is seen in the debug window, the functionality
*  will need enabling:
*
** GDB **
*  From the gdb prompt: monitor target_request debugmsgs enable
*
** Telnet **
*From the Telnet prompt: target_request debugmsgs enable
*
*Spen
*spen@spen-soft.co.uk
*
***************************************************************/

#include <ch.h>

//# bytes to reserve for debugging print buffer.  0 omits debugPrint()
#define debugPrintBufSize 250  

Thread *debugPutInit(char *outq, size_t outqSize);
/*
  allocate output queue of outqSize bytes and start background thread
  return background thread
*/

#define debugPrintInit(q)  debugPutInit(q, sizeof q)

int debugPutc(int c);

size_t debugPut(const uint8_t *block, size_t n);
/*
  truncate any block > 255 bytes
  returns # of characters actually sent to host
*/

size_t debugPuts(const char *str);

size_t debugPrint(const char *fmt, ...);
/*
  printf style debugging output
  outputs a trailing newline
*/

#if debugPrintBufSize > 0
size_t debugPrint(const char *fmt, ...);
/*
  printf style debugging output
  outputs a trailing newline
*/
#endif

