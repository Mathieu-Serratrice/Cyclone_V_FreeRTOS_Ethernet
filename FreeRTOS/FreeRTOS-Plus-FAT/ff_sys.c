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
#include <string.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "portable.h"

#include "ff_headers.h"
#include "ff_sys.h"

#ifndef ARRAY_SIZE
#	define	ARRAY_SIZE(x)	( int )( sizeof( x ) / sizeof( x )[ 0 ] )
#endif

/*
 * Define a collection of 'file systems' as a simple array
 */
typedef struct xSYSTEM
{
	FF_SubSystem_t xSystems[ ffconfigMAX_FILE_SYS ];
	volatile BaseType_t xFileSystemCount;
} ff_sys_t;


static ff_sys_t file_systems;
static const char rootDir[] = "/";

int FF_FS_Count( void )
{
	return ( int )file_systems.xFileSystemCount;
}
/*-----------------------------------------------------------*/

void FF_FS_Init( void )
{
	memset( &file_systems, '\0', sizeof( file_systems ) );

	/* There is always a root file system, even if it doesn't have a
	IO manager. */
	file_systems.xFileSystemCount = ( BaseType_t ) 1;
	/* Set to "/", second byte is already zero. */
	file_systems.xSystems[ 0 ].pcPath[ 0 ] = ( char ) '/';

	file_systems.xSystems[ 0 ].xPathlen = 1;
}
/*-----------------------------------------------------------*/

int FF_FS_Add( const char *pcPath, FF_Disk_t *pxDisk )
{
int iReturn = pdFALSE;

	configASSERT( pxDisk );

	if( *pcPath != ( char ) '/' )
	{
		FF_PRINTF( "FF_FS_Add: Need a \"/\": '%s'\n", pcPath );
	}
	else
	{
	BaseType_t xUseIndex = -1;
	size_t uxPathLength = strlen( pcPath );

		vTaskSuspendAll();
		{
			if( file_systems.xFileSystemCount == ( BaseType_t ) 0 )
			{
				FF_FS_Init();
			}

			if( uxPathLength == ( size_t ) 1u )
			{
				/* This is the "/" path
				 * and will always be put at index 0 */
				xUseIndex = ( BaseType_t ) 0;
			}
			else
			{
			BaseType_t xIndex, xFreeIndex = -1;
			FF_SubSystem_t *pxSubSystem = file_systems.xSystems + 1;	/* Skip the root entry */

				for( xIndex = ( BaseType_t ) 1; xIndex < file_systems.xFileSystemCount; xIndex++, pxSubSystem++ )
				{
					if( ( pxSubSystem->xPathlen == ( BaseType_t )uxPathLength ) &&
						( memcmp( pxSubSystem->pcPath, pcPath, uxPathLength ) == 0 ) )
					{
						/* A system is updated with a new handler. */
						xUseIndex = xIndex;
						break;
					}
					if( ( pxSubSystem->pxManager == NULL ) && ( xFreeIndex < 0 ) )
					{
						/* Remember the first free slot. */
						xFreeIndex = xIndex;
					}
				}
				if( xUseIndex < ( BaseType_t ) 0 )
				{
					if( xFreeIndex >= ( BaseType_t ) 0 )
					{
						/* Use the first free slot. */
						xUseIndex = xFreeIndex;
					}
					else if( file_systems.xFileSystemCount < ARRAY_SIZE( file_systems.xSystems ) )
					{
						/* Fill a new entry. */
						xUseIndex = file_systems.xFileSystemCount++;
					}
				}
			} /* uxPathLength != 1 */
			if( xUseIndex >= ( BaseType_t ) 0 )
			{
				iReturn = pdTRUE;
				strncpy( file_systems.xSystems[ xUseIndex ].pcPath, pcPath, sizeof( file_systems.xSystems[ xUseIndex ].pcPath ) );
				file_systems.xSystems[ xUseIndex ].xPathlen = uxPathLength;
				file_systems.xSystems[ xUseIndex ].pxManager = pxDisk->pxIOManager;
			}
		}
		xTaskResumeAll( );
		if( iReturn == pdFALSE )
		{
			FF_PRINTF( "FF_FS_Add: Table full '%s' (max = %d)\n", pcPath, (int)ARRAY_SIZE( file_systems.xSystems ) );
		}
	}

	return iReturn;
}
/*-----------------------------------------------------------*/

void FF_FS_Remove( const char *pcPath )
{
BaseType_t xUseIndex, xIndex;
size_t uxPathLength;

	if( pcPath[ 0 ] == ( char ) '/' )
	{
		xUseIndex = -1;
		uxPathLength = strlen( pcPath );
		/* Is it the "/" path ? */
		if( uxPathLength == ( size_t ) 1u )
		{
			xUseIndex = 0;
		}
		else
		{
			FF_SubSystem_t *pxSubSystem = file_systems.xSystems + 1;
			for( xIndex = 1; xIndex < file_systems.xFileSystemCount; xIndex++, pxSubSystem++ )
			{
				if( ( pxSubSystem->xPathlen == ( BaseType_t ) uxPathLength ) &&
					( memcmp( pxSubSystem->pcPath, pcPath, uxPathLength ) == 0 ) )
				{
					xUseIndex = xIndex;
					break;
				}
			}
		}
		if( xUseIndex >= 0 )
		{
			vTaskSuspendAll();
			{
				file_systems.xSystems[ xUseIndex ].pxManager = NULL;
				file_systems.xSystems[ xUseIndex ].xPathlen = ( BaseType_t )0;
				for( xIndex = file_systems.xFileSystemCount - 1; xIndex > 0; xIndex-- )
				{
					if( file_systems.xSystems[ xIndex ].pxManager != NULL )
					{
						/* The slot at 'xIndex' is still in use. */
						break;
					}
				}
				file_systems.xFileSystemCount = xIndex + 1;
			}
			xTaskResumeAll( );
		}
	}
}
/*-----------------------------------------------------------*/

int FF_FS_Find( const char *pcPath, FF_DirHandler_t *pxHandler )
{
FF_SubSystem_t *pxSubSystem;
size_t uxPathLength;
BaseType_t xUseIndex;
int iReturn;

	pxSubSystem = file_systems.xSystems + 1;
	uxPathLength = strlen( pcPath );

	memset( pxHandler, '\0', sizeof( *pxHandler ) );
	for( xUseIndex = 1; xUseIndex < file_systems.xFileSystemCount; xUseIndex++, pxSubSystem++ )
	{
		if( ( uxPathLength >= ( size_t ) pxSubSystem->xPathlen ) &&
			( memcmp( pxSubSystem->pcPath, pcPath, ( size_t ) pxSubSystem->xPathlen ) == 0 ) &&
			( ( pcPath[ pxSubSystem->xPathlen ] == '\0' ) || ( pcPath[ pxSubSystem->xPathlen ] == '/') ) )	/* System "/ram" should not match with "/ramc/etc". */
		{
			if( pcPath[ pxSubSystem->xPathlen ] == '\0')
			{
				pxHandler->pcPath = rootDir;
			}
			else
			{
				pxHandler->pcPath = pcPath + pxSubSystem->xPathlen;
			}

			pxHandler->pxManager = pxSubSystem->pxManager;
			break;
		}
	}

	if( xUseIndex == file_systems.xFileSystemCount )
	{
		pxHandler->pcPath = pcPath;
		pxHandler->pxManager = file_systems.xSystems[ 0 ].pxManager;
	}

	if( FF_Mounted( pxHandler->pxManager ) )
	{
		iReturn = pdTRUE;
	}
	else
	{
		iReturn = pdFALSE;
	}

	return iReturn;
}
/*-----------------------------------------------------------*/

int FF_FS_Get( int xIndex, FF_SubSystem_t *pxSystem )
{
int iReturn;

	/* Get a copy of a fs info. */
	if( ( xIndex < 0 ) || ( xIndex >= file_systems.xFileSystemCount ) )
	{
		iReturn = pdFALSE;
	}
	else
	{
		/* Note: it will copy the contents of 'FF_SubSystem_t'. */
		*pxSystem = file_systems.xSystems[ xIndex ];
		iReturn = pdTRUE;
	}

	return iReturn;
}
/*-----------------------------------------------------------*/

