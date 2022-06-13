/*
 * FreeRTOS+TCP Labs Build 150825 (C) 2015 Real Time Engineers ltd.
 * Authors include Hein Tibosch and Richard Barry
 *
 *******************************************************************************
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 ***                                                                         ***
 ***                                                                         ***
 ***   FREERTOS+TCP IS STILL IN THE LAB:                                     ***
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
 * - Open source licensing -
 * While FreeRTOS+TCP is in the lab it is provided only under version two of the
 * GNU General Public License (GPL) (which is different to the standard FreeRTOS
 * license).  FreeRTOS+TCP is free to download, use and distribute under the
 * terms of that license provided the copyright notice and this text are not
 * altered or removed from the source files.  The GPL V2 text is available on
 * the gnu.org web site, and on the following
 * URL: http://www.FreeRTOS.org/gpl-2.0.txt.  Active early adopters may, and
 * solely at the discretion of Real Time Engineers Ltd., be offered versions
 * under a license other then the GPL.
 *
 * FreeRTOS+TCP is distributed in the hope that it will be useful.  You cannot
 * use FreeRTOS+TCP unless you agree that you use the software 'as is'.
 * FreeRTOS+TCP is provided WITHOUT ANY WARRANTY; without even the implied
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

/*
 *	FreeRTOS_Stream_Buffer.h
 *
 *	A cicular character buffer
 *	An implementation of a circular buffer without a length field
 *	If LENGTH defines the size of the buffer, a maximum of (LENGT-1) bytes can be stored
 *	In order to add or read data from the buffer, memcpy() will be called at most 2 times
 */

#ifndef FREERTOS_STREAM_BUFFER_H
#define	FREERTOS_STREAM_BUFFER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xSTREAM_BUFFER {
	volatile size_t uxTail;		/* next item to read */
	volatile size_t uxMid;		/* iterator within the valid items */
	volatile size_t uxHead;		/* next position store a new item */
	volatile size_t uxFront;	/* iterator within the free space */
	size_t LENGTH;				/* const value: number of reserved elements */
	uint8_t ucArray[ sizeof( size_t ) ];
} Buffer_t;

static portINLINE void vStream_BufferClear( Buffer_t *pxBuffer )
{
	/* Make the circular buffer empty */
	pxBuffer->uxHead = 0;
	pxBuffer->uxTail = 0;
	pxBuffer->uxFront = 0;
	pxBuffer->uxMid = 0;
}
/*-----------------------------------------------------------*/

static portINLINE size_t uxBufferSpace( const Buffer_t *pxBuffer, size_t uxLower, size_t uxUpper )
{
/* Returns the space between lLower and lUpper, which equals to the distance minus 1 */
size_t uxCount;

	uxCount = pxBuffer->LENGTH + uxUpper - uxLower - 1;
	if( uxCount >= pxBuffer->LENGTH )
	{
		uxCount -= pxBuffer->LENGTH;
	}

	return uxCount;
}
/*-----------------------------------------------------------*/

static portINLINE size_t uxBufferDistance( const Buffer_t *pxBuffer, size_t uxLower, size_t uxUpper )
{
/* Returns the distance between lLower and lUpper */
size_t uxCount;

	uxCount = pxBuffer->LENGTH + uxUpper - uxLower;
	if ( uxCount >= pxBuffer->LENGTH )
	{
		uxCount -= pxBuffer->LENGTH;
	}

	return uxCount;
}
/*-----------------------------------------------------------*/

static portINLINE size_t uxBufferGetSpace( const Buffer_t *pxBuffer )
{
	/* Returns the number of items which can still be added to lHead
	before hitting on lTail */
	return uxBufferSpace( pxBuffer, pxBuffer->uxHead, pxBuffer->uxTail );
}
/*-----------------------------------------------------------*/

static portINLINE size_t uxBufferFrontSpace( const Buffer_t *pxBuffer )
{
	/* Distance between lFront and lTail
	or the number of items which can still be added to lFront,
	before hitting on lTail */
	return uxBufferSpace( pxBuffer, pxBuffer->uxFront, pxBuffer->uxTail );
}
/*-----------------------------------------------------------*/

static portINLINE size_t uxBufferGetSize( const Buffer_t *pxBuffer )
{
	/* Returns the number of items which can be read from lTail
	before reaching lHead */
	return uxBufferDistance( pxBuffer, pxBuffer->uxTail, pxBuffer->uxHead );
}
/*-----------------------------------------------------------*/

static portINLINE size_t uxBufferMidSpace( const Buffer_t *pxBuffer )
{
	/* Returns the distance between lHead and lMid */
	return uxBufferDistance( pxBuffer, pxBuffer->uxMid, pxBuffer->uxHead );
}
/*-----------------------------------------------------------*/

static portINLINE void vStream_BufferMoveMid( Buffer_t *pxBuffer, size_t uxCount )
{
/* Increment lMid, but no further than lHead */
size_t uxSize = uxBufferMidSpace( pxBuffer );

	if( uxCount > uxSize )
	{
		uxCount = uxSize;
	}
	pxBuffer->uxMid += uxCount;
	if( pxBuffer->uxMid >= pxBuffer->LENGTH )
	{
		pxBuffer->uxMid -= pxBuffer->LENGTH;
	}
}
/*-----------------------------------------------------------*/

static portINLINE BaseType_t xStream_BufferIsEmpty( const Buffer_t *pxBuffer )
{
BaseType_t xReturn;

	/* True if no item is available */
	if( pxBuffer->uxHead == pxBuffer->uxTail )
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

static portINLINE BaseType_t xBufferIsFull( const Buffer_t *pxBuffer )
{
	/* True if the available space equals zero. */
	return uxBufferGetSpace( pxBuffer ) == 0;
}
/*-----------------------------------------------------------*/

static portINLINE BaseType_t xBufferLessThenEqual( const Buffer_t *pxBuffer, size_t uxLeft, size_t uxRight )
{
BaseType_t xReturn;
size_t uxTail = pxBuffer->uxTail;

	/* Returns true if ( uxLeft < uxRight ) */
	if( ( uxLeft < uxTail ) ^ ( uxRight < uxTail ) )
	{
		if( uxRight < uxTail )
		{
			xReturn = pdTRUE;
		}
		else
		{
			xReturn = pdFALSE;
		}
	}
	else
	{
		if( uxLeft <= uxRight )
		{
			xReturn = pdTRUE;
		}
		else
		{
			xReturn = pdFALSE;
		}
	}
	return xReturn;
}
/*-----------------------------------------------------------*/

static portINLINE size_t uxBufferGetPtr( Buffer_t *pxBuffer, uint8_t **ppucData )
{
size_t uxNextTail = pxBuffer->uxTail;
size_t uxSize = uxBufferGetSize( pxBuffer );

	*ppucData = pxBuffer->ucArray + uxNextTail;

	return FreeRTOS_min_uint32( uxSize, pxBuffer->LENGTH - uxNextTail );
}

/*
 * Add bytes to a stream buffer.
 *
 * pxBuffer -	The buffer to which the bytes will be added.
 * lOffset -	If lOffset > 0, data will be written at an offset from lHead
 *				while lHead will not be moved yet.
 * pucData -	A pointer to the data to be added.
 * lCount -		The number of bytes to add.
 */
size_t uxBufferAdd( Buffer_t *pxBuffer, size_t uxOffset, const uint8_t *pucData, size_t uxCount );

/*
 * Read bytes from a stream buffer.
 *
 * pxBuffer -	The buffer from which the bytes will be read.
 * lOffset -	Can be used to read data located at a certain offset from 'lTail'.
 * pucData -	A pointer to the buffer into which data will be read.
 * lMaxCount -	The number of bytes to read.
 * xPeek -		If set to pdTRUE the data will remain in the buffer.
 */
size_t uxBufferGet( Buffer_t *pxBuffer, size_t uxOffset, uint8_t *pucData, size_t uxMaxCount, BaseType_t xPeek );

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif	/* !defined( FREERTOS_STREAM_BUFFER_H ) */
