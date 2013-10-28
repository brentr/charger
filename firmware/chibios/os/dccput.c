/***************************************************************************
 *   Copyright (C) 2008 by Dominic Rath                                    *
 *   Dominic.Rath@gmx.de                                                   *
 *   Copyright (C) 2008 by Spencer Oliver                                  *
 *   spen@spen-soft.co.uk                                                  *
 *   Copyright (C) 2008 by Frederik Kriewtz                                *
 *   frederik@kriewitz.eu                                                  *
 *   revised by:  brent@mbari.org  10/27/13                                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "dccput.h"

#define TARGET_REQ_TRACEMSG			0x00
#define TARGET_REQ_DEBUGMSG_ASCII		0x01
#define TARGET_REQ_DEBUGMSG_HEXMSG(size)	(0x01 | ((size & 0xff) << 8))
#define TARGET_REQ_DEBUGCHAR			0x02

#if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || defined(__ARM_ARCH_6SM__)

/* we use the System Control Block DCRDR reg to simulate a arm7_9 dcc channel
 * DCRDR[7:0] is used by target for status
 * DCRDR[15:8] is used by target for write buffer
 * DCRDR[23:16] is used for by host for status
 * DCRDR[31:24] is used for by host for write buffer */

#define DCRDR		(*((volatile uint16_t *)0xE000EDF8))

#define	BUSY	1


static void awaitReady(void)
/*
  poll for data ready
  after DCCmaxBusy ticks, poll only once every DCCbusyDelay tics
*/
{
  if (DCRDR & BUSY) {
    systime_t wtStart = chTimeNow();
    while (DCRDR & BUSY)
      if (chTimeNow()-wtStart >= DCCmaxBusy) { //busy wait for first few seconds
        do  //but, if the host is not responding...
          chThdSleep(DCCbusyDelay);  //sleep between each poll of the BUSY bit
        while (DCRDR & BUSY);
        break;
      }
  }
}

static void DCCwrite(uint32_t dcc_data)
{
  awaitReady();
  /* write our data and set write flag - tell host there is data*/
  DCRDR = (uint16_t)(((dcc_data & 0xff) << 8) | BUSY);

  awaitReady();
  /* write our data and set write flag - tell host there is data*/
  DCRDR = (uint16_t)((dcc_data & 0xff00) | BUSY);

  awaitReady();
  /* write our data and set write flag - tell host there is data*/
  DCRDR = (uint16_t)(((dcc_data & 0xff0000) >> 8) | BUSY);

  awaitReady();
  /* write our data and set write flag - tell host there is data*/
  DCRDR = (uint16_t)(((dcc_data & 0xff000000) >> 16) | BUSY);
}

#elif defined(__ARM_ARCH_4T__) || defined(__ARM_ARCH_5TE__) || defined(__ARM_ARCH_5T__)

static INLINE void DCCwrite(uint32_t dcc_data)
{
    uint32_t dcc_status;

    do {
	asm volatile("mrc p14, 0, %0, c0, c0" : "=r" (dcc_status));
    } while (dcc_status & 0x2);

    asm volatile("mcr p14, 0, %0, c1, c0" : : "r" (dcc_data));
}

#else
 #error unsupported target
#endif


static void DCCwriteBytes(int type, const uint8_t *umsg, size_t len)
{
  size_t extra;
  uint32_t dcc_data;
  DCCwrite(((uint32_t)len << 16) | type);

  extra = len & 3;
  len >>= 2;
  while (len) {
    dcc_data = (uint32_t)(umsg[0])     | (uint32_t)(umsg[1])<<8
             | (uint32_t)(umsg[2])<<16 | (uint32_t)(umsg[3])<<24;
    DCCwrite(dcc_data);
    umsg += 4;
    --len;
  }
  if (extra) {
    umsg+=extra;
    dcc_data = 0;
    do {
      dcc_data <<= 8;
      dcc_data |= *--umsg;
    } while (--extra);
    DCCwrite(dcc_data);
  }
}


void DCCtracePoint(uint32_t number)
{
  DCCwrite(TARGET_REQ_TRACEMSG | (number << 8));
}

void DCCputU32(const uint32_t *val, size_t len)
{
  DCCwrite(TARGET_REQ_DEBUGMSG_HEXMSG(4) | (((uint32_t)len & 0xffff) << 16));

  while (len)
  {
    DCCwrite(*val++);
    len--;
  }
}

void DCCputU16(const uint16_t *val, size_t len)
{
  size_t odd = len & 1;
  DCCwrite(TARGET_REQ_DEBUGMSG_HEXMSG(2) | (((uint32_t)len & 0xffff) << 16));

  len >>= 1;
  while (len)
  {
    DCCwrite(val[0] | ((uint32_t)val[1] << 16));
    val += 2;
    --len;
  }
  if (odd)
    DCCwrite(*val);
}

void DCCputByte(const uint8_t *val, size_t len)
{
  DCCwriteBytes(TARGET_REQ_DEBUGMSG_HEXMSG(1), val, len);
}


void DCCputs(const char *msg)
{
  size_t len;
  for (len = 0; msg[len] && len < 65536; len++);
  DCCwriteBytes(TARGET_REQ_DEBUGMSG_ASCII, (const uint8_t *)msg, len);
}

void DCCputc(const int msg)
{
  DCCwrite(TARGET_REQ_DEBUGCHAR | ((uint32_t)(uint8_t)msg) << 16);
}


void DCCputsQ(DDCfetcher fetch, void *link, size_t len)
{
  uint32_t d[3];
  size_t extra;
  DCCwrite(((uint32_t)len << 16) | TARGET_REQ_DEBUGMSG_ASCII);
  extra = len & 3;
  len >>= 2;
  while (len) {
    d[0] = fetch(link);
    d[1] = fetch(link);
    d[2] = fetch(link);
    DCCwrite(((fetch(link)<<8 | d[2])<<8 | d[1])<<8 | d[0]);
    --len;
  }
  switch(extra) {
    case 1:
      DCCwrite(fetch(link));
      break;
    case 2:
      d[0] = fetch(link);
      DCCwrite(fetch(link)<<8 | d[0]);
      break;
    case 3:
      d[0] = fetch(link);
      d[1] = fetch(link);
      DCCwrite((fetch(link)<<8 | d[1])<<8 | d[0]);
  }
}
