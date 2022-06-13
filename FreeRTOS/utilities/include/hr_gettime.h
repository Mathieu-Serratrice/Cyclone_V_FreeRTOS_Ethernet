/*
*/

#ifndef HR_GETTTIME_H

#define HR_GETTTIME_H

/* SAM4E: Timer initialization function */
void vStartHighResolutionTimer( void );

/* Get the current time measured in uS. The return value is 64-bits. */
uint64_t ullGetHighResolutionTime( void );

typedef struct xTaskGuard {
	uint64_t ullLastTime;
	uint32_t ulMaxDifference;
	uint32_t ulTimes[ 15 ];
	char cStarted;
} TaskGuard_t;

void vTask_init( TaskGuard_t *pxTask, uint32_t ulMaxDiff );
void vTask_finish( TaskGuard_t *pxTask );
void vTask_start( TaskGuard_t *pxTask );


#endif /* HR_GETTTIME_H */