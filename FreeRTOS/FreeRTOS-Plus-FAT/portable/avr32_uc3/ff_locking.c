/*
 * FreeRTOS+FAT Labs Build 160919 (C) 2016 Real Time Engineers ltd.
 * Authors include James Walmsley, Hein Tibosch and Richard Barry
 *
 *******************************************************************************
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 ***                                                                         ***
 ***                                                                         ***
 ***   FREERTOS+FAT IS STILL IN THE LAB:                                     ***
 ***                                                                         ***
 ***   This product is functional and is already being used in commercial    ***
 ***   products.  Be aware however that we are still refining its design,    ***
 ***   the source code does not yet fully conform to the strict coding and   ***
 ***   style standards mandated by Real Time Engineers ltd., and the         ***
 ***   documentation and testing is not necessarily complete.                ***
 ***                                                                         ***
 ***   PLEASE REPORT EXPERIENCES USING THE SUPPORT RESOURCES FOUND ON THE    ***
 ***   URL: http://www.FreeRTOS.org/contact  Active early adopters may, at   ***
 ***   the sole discretion of Real Time Engineers Ltd., be offered versions  ***
 ***   under a license other than that described below.                      ***
 ***                                                                         ***
 ***                                                                         ***
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 *******************************************************************************
 *
 * FreeRTOS+FAT can be used under two different free open source licenses.  The
 * license that applies is dependent on the processor on which FreeRTOS+FAT is
 * executed, as follows:
 *
 * If FreeRTOS+FAT is executed on one of the processors listed under the Special
 * License Arrangements heading of the FreeRTOS+FAT license information web
 * page, then it can be used under the terms of the FreeRTOS Open Source
 * License.  If FreeRTOS+FAT is used on any other processor, then it can be used
 * under the terms of the GNU General Public License V2.  Links to the relevant
 * licenses follow:
 *
 * The FreeRTOS+FAT License Information Page: http://www.FreeRTOS.org/fat_license
 * The FreeRTOS Open Source License: http://www.FreeRTOS.org/license
 * The GNU General Public License Version 2: http://www.FreeRTOS.org/gpl-2.0.txt
 *
 * FreeRTOS+FAT is distributed in the hope that it will be useful.  You cannot
 * use FreeRTOS+FAT unless you agree that you use the software 'as is'.
 * FreeRTOS+FAT is provided WITHOUT ANY WARRANTY; without even the implied
 * warranties of NON-INFRINGEMENT, MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. Real Time Engineers Ltd. disclaims all conditions and terms, be they
 * implied, expressed, or statutory.
 *
 * 1 tab == 4 spaces!
 *
 * http://www.FreeRTOS.org
 * http://www.FreeRTOS.org/plus
 * http://www.FreeRTOS.org/labs
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "ff_headers.h"
#include "event_groups.h"


/* Scheduler include files. */
#ifdef	__WIN32__
	#include "MyMalloc.h"
#else
	#include "FreeRTOS.h"
	#include "task.h"
	#include "semphr.h"
	#include "tcpLogging.h"
#endif

#include "logbuf.h"
#include <string.h>
#include "bitops.h"
#if USE_SOFT_WDT
	#include "softWdt.h"
#endif

#include "thread_mutex.h"

#include "event_groups.h"

#if( FF_DO_TRACE_SEMAPHORE != 0 )
	#include "eventLogging.h"
#endif

/* There are two areas which are protected with a semaphore:
Directories and the FAT area.
The masks below are used when calling Group Event functions. */
#define FF_FAT_LOCK_EVENT_BITS    ( ( const EventBits_t ) FF_FAT_LOCK )
#define FF_DIR_LOCK_EVENT_BITS    ( ( const EventBits_t ) FF_DIR_LOCK )

/* This is not a real lock: it is a bit (or semaphore) will will be given
each time when a sector buffer is released. */
#define FF_BUF_LOCK_EVENT_BITS    ( ( const EventBits_t ) FF_BUF_LOCK )

extern void myTaskDelay (unsigned aTime);

static char cMutexWasCreated = 0;
static pthread_mutex_t xFATMutex;

static void vCreateFATMutex ()
{
	cMutexWasCreated = 1;
	pthread_mutex_init ("FreeRTOS+FAT", &xFATMutex, 0);
}

BaseType_t FF_HasSemaphore (void *pxSemaphore)
{
	// Return pdTRUE if the calling task owns the semaphore
	if (!cMutexWasCreated) vCreateFATMutex ();
	return pthread_has_mutex (&xFATMutex);
}


BaseType_t FF_SemaphoreTaken (void *pxSemaphore)
{
	// Return pdTRUE if the calling task owns the semaphore
	if (!cMutexWasCreated) vCreateFATMutex ();
	return pthread_mutex_islocked (&xFATMutex);
}


#if( FF_DO_TRACE_SEMAPHORE != 0 )
static char mutex_owner[32];
#endif

BaseType_t FF_TrySemaphore( void *pxSemaphore, uint32_t ulTime_ms)
{
BaseType_t rc;
	#if( FF_DO_TRACE_SEMAPHORE != 0 )
	{
		eventLogAdd("Pend_%s\n", pcName);
	}
	#endif /* FF_DO_TRACE_SEMAPHORE */
	if (!cMutexWasCreated) vCreateFATMutex ();
	rc = pthread_mutex_lock (&xFATMutex, ulTime_ms);
	#if( FF_DO_TRACE_SEMAPHORE != 0 )
	if (rc > 0) {
		if(mutex_owner[0] != 0) {
			logPrintf("Pend Try: %s overruled\n", mutex_owner);
		}
		snprintf(mutex_owner, sizeof mutex_owner, pcName);
	}
	#endif /* FF_DO_TRACE_SEMAPHORE */
	return rc;
}
/*-----------------------------------------------------------*/

void FF_PendSemaphore( void *pxSemaphore )
{
	#if( FF_DO_TRACE_SEMAPHORE != 0 )
	{
		eventLogAdd("Pend_%s\n", pcName);
	}
	#endif /* FF_DO_TRACE_SEMAPHORE */

	if (!cMutexWasCreated) vCreateFATMutex ();
	pthread_mutex_lock (&xFATMutex, 120000);
	#if( FF_DO_TRACE_SEMAPHORE != 0 )
	{
		if(mutex_owner[0] != 0) {
			logPrintf("Pend Enter: %s overruled by %s\n", mutex_owner, pcName);
		}
		snprintf(mutex_owner, sizeof mutex_owner, pcName);
	}
	#endif /* FF_DO_TRACE_SEMAPHORE */
}
/*-----------------------------------------------------------*/

void FF_ReleaseSemaphore( void *pxSemaphore )
{
	#if( FF_DO_TRACE_SEMAPHORE != 0 )
	{
		if(strcmp (pcName, mutex_owner) != 0) {
			// FF_GetBuffer2 != FF_GetBuffer
			logPrintf("Pend Exit: %s owned by %s\n", pcName, mutex_owner);
		}
		eventLogAdd("Exit_%s\n", pcName);
		mutex_owner[0] = '\0';
	}
	#endif /* FF_DO_TRACE_SEMAPHORE */

	if (cMutexWasCreated)  {
		pthread_mutex_unlock (&xFATMutex);
	}
}
/*-----------------------------------------------------------*/

void FF_Sleep( uint32_t ulTime_ms )
{
	myTaskDelay (ulTime_ms);
}
/*-----------------------------------------------------------*/

void FF_DeleteEvents( FF_IOManager_t *pxIOManager )
{
	if( pxIOManager->xEventGroup != NULL )
	{
		vEventGroupDelete( pxIOManager->xEventGroup );
	}
}
/*-----------------------------------------------------------*/

BaseType_t FF_CreateEvents( FF_IOManager_t *pxIOManager )
{
BaseType_t xResult;

	pxIOManager->xEventGroup = xEventGroupCreate();
	if( pxIOManager->xEventGroup != NULL )
	{
		xEventGroupSetBits( pxIOManager->xEventGroup,
			FF_FAT_LOCK_EVENT_BITS | FF_DIR_LOCK_EVENT_BITS | FF_BUF_LOCK_EVENT_BITS );
		xResult = pdTRUE;
	}
	else
	{
		xResult = pdFALSE;
	}

	return xResult;
}
/*-----------------------------------------------------------*/

void FF_LockDirectory( FF_IOManager_t *pxIOManager )
{
	EventBits_t xBits;

	for( ;; )
	{
		/* Called when a task want to make changes to a directory.
		First it waits for the desired bit to come high. */
		xEventGroupWaitBits( pxIOManager->xEventGroup,
			FF_DIR_LOCK_EVENT_BITS, /* uxBitsToWaitFor */
			( EventBits_t )0,       /* xClearOnExit */
			pdFALSE,                /* xWaitForAllBits n.a. */
			pdMS_TO_TICKS( 10000UL ) );

		/* The next operation will only succeed for 1 task at a time,
		because it is an atomary test & set operation: */
		xBits = xEventGroupClearBits( pxIOManager->xEventGroup, FF_DIR_LOCK_EVENT_BITS );

		if( ( xBits & FF_DIR_LOCK_EVENT_BITS ) != 0 )
		{
			/* This task has cleared the desired bit.
			It now 'owns' the resource. */
			break;
		}
	}
}
/*-----------------------------------------------------------*/

void FF_UnlockDirectory( FF_IOManager_t *pxIOManager )
{
	configASSERT( ( xEventGroupGetBits( pxIOManager->xEventGroup ) & FF_DIR_LOCK_EVENT_BITS ) == 0 );
	xEventGroupSetBits( pxIOManager->xEventGroup, FF_DIR_LOCK_EVENT_BITS );
}
/*-----------------------------------------------------------*/

int FF_Has_Lock( FF_IOManager_t *pxIOManager, uint32_t aBits )
{
int iReturn;

	void *handle = xTaskGetCurrentTaskHandle();
	if( ( aBits & FF_FAT_LOCK_EVENT_BITS ) != 0 )
	{
		if( ( pxIOManager->pvFATLockHandle != NULL ) && ( pxIOManager->pvFATLockHandle == handle ) )
		{
			iReturn = pdTRUE;
		}
		else
		{
			iReturn = pdFALSE;
		}
	}
	else
	{
		iReturn = pdFALSE;
	}
	return iReturn;
}

void FF_Assert_Lock( FF_IOManager_t *pxIOManager, uint32_t aBits )
{
	void *handle = xTaskGetCurrentTaskHandle();

	if( ( aBits & FF_FAT_LOCK_EVENT_BITS ) != 0 )
	{
		configASSERT( pxIOManager->pvFATLockHandle != NULL && pxIOManager->pvFATLockHandle == handle );

		/* In case configASSERT() is not defined. */
		( void ) pxIOManager; 
		( void ) handle;
	}
}

void FF_LockFAT( FF_IOManager_t *pxIOManager )
{
EventBits_t xBits;

	configASSERT( FF_Has_Lock( pxIOManager, FF_FAT_LOCK ) == pdFALSE );
	if (FF_Has_Lock( pxIOManager, FF_FAT_LOCK ) != pdFALSE )
	{
		return;
	}

	for( ;; )
	{
		/* Called when a task want to make changes to the FAT area.
		First it waits for the desired bit to come high. */
		xEventGroupWaitBits( pxIOManager->xEventGroup,
			FF_FAT_LOCK_EVENT_BITS, /* uxBitsToWaitFor */
			( EventBits_t )0,       /* xClearOnExit */
			pdFALSE,                /* xWaitForAllBits n.a. */
			pdMS_TO_TICKS( 10000UL ) );

		/* The next operation will only succeed for 1 task at a time,
		because it is an atomary test & set operation: */
		xBits = xEventGroupClearBits( pxIOManager->xEventGroup, FF_FAT_LOCK_EVENT_BITS );

		if( ( xBits & FF_FAT_LOCK_EVENT_BITS ) != 0 )
		{
			/* This task has cleared the desired bit.
			It now 'owns' the resource. */
			pxIOManager->pvFATLockHandle = xTaskGetCurrentTaskHandle();
			break;
		}
	}
}
/*-----------------------------------------------------------*/

void FF_UnlockFAT( FF_IOManager_t *pxIOManager )
{
	configASSERT( ( xEventGroupGetBits( pxIOManager->xEventGroup ) & FF_FAT_LOCK_EVENT_BITS ) == 0 );
	if (FF_Has_Lock( pxIOManager, FF_FAT_LOCK ) != pdFALSE )
	{
		pxIOManager->pvFATLockHandle = NULL;
		xEventGroupSetBits( pxIOManager->xEventGroup, FF_FAT_LOCK_EVENT_BITS );
	}
	else
	{
		FF_PRINTF("FF_UnlockFAT: wasn't locked by me\n");
	}
}
/*-----------------------------------------------------------*/

BaseType_t FF_BufferWait( FF_IOManager_t *pxIOManager, uint32_t xWaitMS )
{
EventBits_t xBits;
BaseType_t xReturn;

	/* This function is called when a task is waiting for a sector buffer
	to become available. */
	xBits = xEventGroupWaitBits( pxIOManager->xEventGroup,
		FF_BUF_LOCK_EVENT_BITS, /* uxBitsToWaitFor */
		FF_BUF_LOCK_EVENT_BITS, /* xClearOnExit */
		pdFALSE,                /* xWaitForAllBits n.a. */
		pdMS_TO_TICKS( xWaitMS ) );
	if( ( xBits & FF_BUF_LOCK_EVENT_BITS ) != 0 )
	{
		xReturn = pdTRUE;
	}
	else
	{
		xReturn = pdFALSE;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

void FF_BufferProceed( FF_IOManager_t *pxIOManager )
{
	/* Wake-up all tasks that are waiting for a sector buffer to become available. */
	xEventGroupSetBits( pxIOManager->xEventGroup, FF_BUF_LOCK_EVENT_BITS );
}
/*-----------------------------------------------------------*/
