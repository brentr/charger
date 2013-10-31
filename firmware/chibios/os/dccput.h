/***************************************************************************
 *   Copyright (C) 2008 by Dominic Rath                                    *
 *   Dominic.Rath@gmx.de                                                   *
 *   Copyright (C) 2008 by Spencer Oliver                                  *
 *   spen@spen-soft.co.uk                                                  *
 *   revised by:  brent@mbari.org  10/27/13                                *
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

#ifndef DCCPUT_H
#define DCCPUT_H

#include "ch.h"

void DCCputU32(const uint32_t *val, size_t len);
void DCCputU16(const uint16_t *val, size_t len);
void DCCputByte(const uint8_t *val, size_t len);

void DCCputs(const char *msg);
void DCCputc(const int msg);

#define DCCmaxBusy    S2ST(5)     //max # of tics to busy wait
#define DCCbusyDelay  MS2ST(200)  //tics to wait between retries after maxBusy

/*
  like DCCputs, but uses the DDCfetcher function to retrieve each
  character output.  String length must be known ahead of time.
*/
typedef uint8_t (*DDCfetcher)(void *link);

void DCCputsQ(DDCfetcher fetch, void *link, size_t len);

#endif /* DCCPUT_H */
