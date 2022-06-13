/*
 * eventlogging.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "FreeRTOS.h"
#include "task.h"

#include "eventlogging.h"

void vUDPLoggingFlush( void );

#if USE_CLOCK
#include "setclock.h"
#endif

#if USE_MYMALLOC
#	include "myMalloc.h"
#else
#	include "portable.h"
#endif

#if( __SAM4E16E__ )
void vUDPLoggingFlush( void );
#define	vFlushLogging	vUDPLoggingFlush
#endif

#if USE_LOG_EVENT

extern int lUDPLoggingPrintf( const char *pcFormatString, ... );

SEventLogging xEventLogs;

int iEventLogInit()
{
	if( xEventLogs.hasInit == pdFALSE )
	{
		xEventLogs.hasInit = pdTRUE;
#if USE_MYMALLOC
		xEventLogs.events = (SLogEvent *)myMalloc (sizeof xEventLogs.events[0] * LOG_EVENT_COUNT, pdFALSE);
#else
		xEventLogs.events = (SLogEvent *)pvPortMalloc (sizeof xEventLogs.events[0] * LOG_EVENT_COUNT);
#endif
		if( xEventLogs.events != NULL )
		{
			memset (xEventLogs.events, '\0', sizeof xEventLogs.events[0] * LOG_EVENT_COUNT);
			xEventLogs.initOk = pdTRUE;
		}
	}
	return xEventLogs.initOk;
}

int iEventLogClear ()
{
int rc;
	if (!iEventLogInit ()) {
		rc = 0;
	} else {
		rc = xEventLogs.writeIndex;
		xEventLogs.writeIndex = 0;
		xEventLogs.wrapped = pdFALSE;
		xEventLogs.onhold = pdFALSE;
	}
	return rc;
}

#include "hr_gettime.h"

static uint32_t getTCTime (uint32_t *puSeconds)
{
uint64_t ullUsec;

	ullUsec = ullGetHighResolutionTime( );
	*puSeconds = ullUsec / 1000000ULL;
	return ( uint32_t ) ( ullUsec % 1000000ULL );
}

void eventLogAdd (const char *apFmt, ...)
{
int writeIndex;
SLogEvent *pxEvent;
va_list args;

	if (!iEventLogInit () || xEventLogs.onhold)
		return;
	writeIndex = xEventLogs.writeIndex++;
	if (xEventLogs.writeIndex >= LOG_EVENT_COUNT) {
#if( EVENT_MAY_WRAP == 0 )
		xEventLogs.writeIndex--;
		return;
#endif
		xEventLogs.writeIndex = 0;
		xEventLogs.wrapped = pdTRUE;
	}
	pxEvent = &xEventLogs.events[writeIndex];

	va_start (args, apFmt);
	vsnprintf (pxEvent->pcMessage, sizeof pxEvent->pcMessage, apFmt, args);
	va_end (args);

	pxEvent->ullTimestamp = ullGetHighResolutionTime( );
}

void eventFreeze()
{
	xEventLogs.onhold = pdTRUE;
}

void vFlushLogging();

void eventLogDump ()
{
char sec_str[5];
char ms_str[5];
int count;
int index;
int i;
uint64_t ullLastTime;

#if USE_CLOCK
	unsigned cpuTicksSec = clk_getFreq (&clk_cpu);
#elif defined(__AVR32__)
	unsigned cpuTicksSec = 48000000;
#else
	unsigned cpuTicksSec = 1000000;
#endif
unsigned cpuTicksMs = cpuTicksSec / 1000;

eventLogAdd ("now");
xEventLogs.onhold = 1;

count = xEventLogs.wrapped ? LOG_EVENT_COUNT : xEventLogs.writeIndex;
index = xEventLogs.wrapped ? xEventLogs.writeIndex : 0;
ullLastTime = xEventLogs.events[index].ullTimestamp;

	lUDPLoggingPrintf("Nr:    s   ms  us  %d events\n", count);
//192.168.2.114     12.680.802 [SvrWork   ] Nr:    s   ms  us  8 events
//192.168.2.114     12.680.899 [SvrWork   ]    0:       0.000 PHY reset 1 ports
//192.168.2.114     12.704.773 [SvrWork   ]    1:       0.271 adv: 01E1 config 1200
//192.168.2.114     12.728.584 [SvrWork   ]    2:       5.151 AN start
//192.168.2.114     12.752.391 [SvrWork   ]    3: 003.570.839 AN done 00
//192.168.2.114     12.776.203 [SvrWork   ]    4:     123.856 PHY LS now 01
//192.168.2.114     12.800.011 [SvrWork   ]    5:       0.087 AN start
//192.168.2.114     12.823.819 [SvrWork   ]    6: 003.571.298 AN done 00
//192.168.2.114     12.847.629 [SvrWork   ]    7: 005.348.585 now

	for( i = 0; i < count; i++ )
	{
	SLogEvent *pxEvent;
	unsigned delta, secs, msec, usec;

		pxEvent = xEventLogs.events + index;
		delta = ( unsigned ) ( pxEvent->ullTimestamp - ullLastTime );

		if (delta > 0xFFFF0000)
			delta = ~0u - delta;

		secs = delta / cpuTicksSec;
		delta %= cpuTicksSec;
		msec = delta / cpuTicksMs;
		delta %= cpuTicksMs;
		usec = (1000 * delta) / cpuTicksMs;

		if (secs > 60) {
			secs = msec = 999;
			usec = 9;
		}
		sprintf( ms_str, "%03u.", msec );
		if( secs == 0u )
		{
			if( secs == 0)
			{
				sprintf( sec_str, "    " );
				sprintf( ms_str, "%3u.", msec );
			}
			else
			{
				sprintf( sec_str, "%3u.", secs );
			}
		}
		else
		{
			sprintf( sec_str, "%3u.", secs );
		}
		lUDPLoggingPrintf("%4d: %s%s%03u %s\n", i, sec_str, ms_str, usec, pxEvent->pcMessage);
		if( ++index >= LOG_EVENT_COUNT )
		{
			index = 0;
		}
		ullLastTime = pxEvent->ullTimestamp;
		//if ((i % 8) == 0)
			vUDPLoggingFlush();
	}
	vTaskDelay( 200 );
	iEventLogClear ();
}

#endif /* USE_LOG_EVENT */
