/*
    FreeRTOS V8.2.3 - Copyright (C) 2015 Real Time Engineers Ltd.
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>>> AND MODIFIED BY <<<< the FreeRTOS exception.

    ***************************************************************************
    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes FreeRTOS without being   !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of the FreeRTOS kernel.                                   !<<
    ***************************************************************************

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available on the following
    link: http://www.freertos.org/a00114.html

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that is more than just the market leader, it     *
     *    is the industry's de facto standard.                               *
     *                                                                       *
     *    Help yourself get started quickly while simultaneously helping     *
     *    to support the FreeRTOS project by purchasing a FreeRTOS           *
     *    tutorial book, reference manual, or both:                          *
     *    http://www.FreeRTOS.org/Documentation                              *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org/FAQHelp.html - Having a problem?  Start by reading
    the FAQ page "My application does not run, what could be wrong?".  Have you
    defined configASSERT()?

    http://www.FreeRTOS.org/support - In return for receiving this top quality
    embedded software for free we request you assist our global community by
    participating in the support forum.

    http://www.FreeRTOS.org/training - Investing in training allows your team to
    be as productive as possible as early as possible.  Now you can receive
    FreeRTOS training directly from Richard Barry, CEO of Real Time Engineers
    Ltd, and the world's leading authority on the world's leading RTOS.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.FreeRTOS.org/labs - Where new FreeRTOS products go to incubate.
    Come and try FreeRTOS+TCP, our new open source TCP/IP stack for FreeRTOS.

    http://www.OpenRTOS.com - Real Time Engineers ltd. license FreeRTOS to High
    Integrity Systems ltd. to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and commercial middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

	1 tab == 4 spaces!
*/

/* High resolution timer functions - used for logging only. */

/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "hr_gettime.h"

#include "eventLogging.h"

/* The TC interrupt will become active as little as possible: every half
minute. */
#define hrSECONDS_PER_INTERRUPT		30ul
#define hrMICROSECONDS_PER_SECOND	1000000ull

#ifndef ARRAY_SIZE
#	define	ARRAY_SIZE( x )	( int )( sizeof( x ) / sizeof( x )[ 0 ] )
#endif

int lUDPLoggingPrintf( const char *pcFormatString, ... );

/*-----------------------------------------------------------*/

void vStartHighResolutionTimer( void )
{
	/* Timer initialisation. */
}
/*-----------------------------------------------------------*/

uint64_t ullGetHighResolutionTime( void )
{
uint64_t ullCurrentTime, ullReturn;

	ullCurrentTime = alt_globaltmr_get64();
	if( ullCurrentTime != 0ull )
	{
		ullReturn = ullCurrentTime / 200ull;
//		ullReturn = ullCurrentTime >> 8;
	}
	else
	{
		ullReturn = 0ull;
	}
	return ullReturn;
}

void vTask_init( TaskGuard_t *pxTask, uint32_t ulMaxDiff )
{
	memset( pxTask, '\0', sizeof *pxTask );
	pxTask->ulMaxDifference = ulMaxDiff;
	pxTask->ullLastTime = ullGetHighResolutionTime();
}

void vTask_finish( TaskGuard_t *pxTask )
{
uint64_t ullCurrentTime = ullGetHighResolutionTime();
	if( pxTask->cStarted != pdFALSE )
	{
	uint32_t ullDifferenceUS;
	uint32_t ulDifferenceMS;
	int index;

		pxTask->cStarted = pdFALSE;
		ullDifferenceUS = ullCurrentTime - pxTask->ullLastTime;
		ulDifferenceMS = ( uint32_t ) ( ( ullDifferenceUS + 500ull ) / 1000ull );
		index = ( int )( ullDifferenceUS / 100ull );
		if( index < 0 || index >= ARRAY_SIZE( pxTask->ulTimes ) )
		{
			index = ARRAY_SIZE( pxTask->ulTimes ) - 1;
		}
		pxTask->ulTimes[ index ]++;

		if( ulDifferenceMS > pxTask->ulMaxDifference )
		{
			eventLogAdd("%s: %lu ms", pcTaskGetTaskName( NULL ), ulDifferenceMS );
			lUDPLoggingPrintf( "SlowLoop: %s: %lu ms\n", pcTaskGetTaskName( NULL ), ulDifferenceMS );
		}
	}
	pxTask->ullLastTime = ullCurrentTime;
}

void vTask_start( TaskGuard_t *pxTask )
{
	pxTask->cStarted = pdTRUE;
	pxTask->ullLastTime = ullGetHighResolutionTime();
}
