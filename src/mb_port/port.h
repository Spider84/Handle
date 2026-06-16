/*
 * FreeModbus Libary: Atmel AT91SAM3S Demo Application
 * Copyright (C) 2010 Christian Walter <cwalter@embedded-solutions.at>
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * IF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * File: $Id$
 */

#ifndef _PORT_H
#define _PORT_H

/* ----------------------- Platform includes --------------------------------*/
#include <stdint.h>
#include <assert.h>

#include <FreeRTOS.h>
#include <task.h>

/* ----------------------- Defines ------------------------------------------*/
#ifndef INLINE
#define	INLINE                      inline
#endif

#ifndef PR_BEGIN_EXTERN_C
#define PR_BEGIN_EXTERN_C           extern "C" {
#endif

#ifndef PR_END_EXTERN_C
#define	PR_END_EXTERN_C             }
#endif

#ifndef ENTER_CRITICAL_SECTION
#define ENTER_CRITICAL_SECTION( )   taskENTER_CRITICAL()
#endif

#ifndef EXIT_CRITICAL_SECTION
#define EXIT_CRITICAL_SECTION( )    taskEXIT_CRITICAL()
#endif

#ifndef TRUE
#define TRUE                        1
#endif

#ifndef FALSE
#define FALSE                       0
#endif

typedef char    BOOL;

typedef unsigned char UCHAR;
typedef char    CHAR;

typedef unsigned short USHORT;
typedef short   SHORT;

typedef unsigned long ULONG;
typedef long    LONG;

/* ----------------------- Defines ------------------------------------------*/
#ifndef INLINE
#define	INLINE                      inline
#endif

#ifndef PR_BEGIN_EXTERN_C
#define PR_BEGIN_EXTERN_C           extern "C" {
#endif

#ifndef PR_END_EXTERN_C
#define	PR_END_EXTERN_C             }
#endif

#ifndef ENTER_CRITICAL_SECTION
#define ENTER_CRITICAL_SECTION( )   taskENTER_CRITICAL()
#endif

#ifndef EXIT_CRITICAL_SECTION
#define EXIT_CRITICAL_SECTION( )    taskEXIT_CRITICAL()
#endif

#ifndef TRUE
#define TRUE                        1
#endif

#ifndef FALSE
#define FALSE                       0
#endif

#define MB_PORT_HAS_CLOSE                      1
#define MB_ASCII_TIMEOUT_WAIT_BEFORE_SEND_MS    2

/* ----------------------- Prototypes ---------------------------------------*/

#endif
