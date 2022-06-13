/*
 * BufferTesting.h
 */

#ifndef	BUFFERTESTING_H

#ifdef __cplusplus
extern "C" {
#endif

extern BaseType_t xBufferTestingPrepare( size_t uxSize );

extern size_t xBufferSendCounts[ 2 ], xBufferRecvCounts[ 2 ];

extern BaseType_t xBufferTestingContentsCheck;
extern BaseType_t xBufferTestingStop;

#ifdef __cplusplus
}	/* extern "C" */
#endif

#endif
