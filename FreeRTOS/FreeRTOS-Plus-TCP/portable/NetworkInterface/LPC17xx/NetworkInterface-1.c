/*
 * FreeRTOS+TCP Labs Build 160919 (C) 2016 Real Time Engineers ltd.
 * Authors include Hein Tibosch and Richard Barry
 *
 *******************************************************************************
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 ***                                                                         ***
 ***                                                                         ***
 ***   FREERTOS+TCP IS STILL IN THE LAB (mainly because the FTP and HTTP     ***
 ***   demos have a dependency on FreeRTOS+FAT, which is only in the Labs    ***
 ***   download):                                                            ***
 ***                                                                         ***
 ***   FreeRTOS+TCP is functional and has been used in commercial products   ***
 ***   for some time.  Be aware however that we are still refining its       ***
 ***   design, the source code does not yet quite conform to the strict      ***
 ***   coding and style standards mandated by Real Time Engineers ltd., and  ***
 ***   the documentation and testing is not necessarily complete.            ***
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
 * FreeRTOS+TCP can be used under two different free open source licenses.  The
 * license that applies is dependent on the processor on which FreeRTOS+TCP is
 * executed, as follows:
 *
 * If FreeRTOS+TCP is executed on one of the processors listed under the Special
 * License Arrangements heading of the FreeRTOS+TCP license information web
 * page, then it can be used under the terms of the FreeRTOS Open Source
 * License.  If FreeRTOS+TCP is used on any other processor, then it can be used
 * under the terms of the GNU General Public License V2.  Links to the relevant
 * licenses follow:
 *
 * The FreeRTOS+TCP License Information Page: http://www.FreeRTOS.org/tcp_license
 * The FreeRTOS Open Source License: http://www.FreeRTOS.org/license
 * The GNU General Public License Version 2: http://www.FreeRTOS.org/gpl-2.0.txt
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

/* Standard includes. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "NetworkBufferManagement.h"
#include "NetworkInterface.h"

/* LPCOpen includes. */
#include "chip.h"
#include "lpc_phy.h"

/* The size of the stack allocated to the task that handles Rx packets. */
#define nwRX_TASK_STACK_SIZE	140

#ifndef	PHY_LS_HIGH_CHECK_TIME_MS
	/* Check if the LinkSStatus in the PHY is still high after 15 seconds of not
	receiving packets. */
	#define PHY_LS_HIGH_CHECK_TIME_MS	15000
#endif

#ifndef	PHY_LS_LOW_CHECK_TIME_MS
	/* Check if the LinkSStatus in the PHY is still low every second. */
	#define PHY_LS_LOW_CHECK_TIME_MS	1000
#endif

#ifndef configUSE_RMII
	#define configUSE_RMII 1
#endif

#ifndef configNUM_RX_DESCRIPTORS
	#define configNUM_RX_DESCRIPTORS 6
#endif

#ifndef configNUM_TX_DESCRIPTORS
	#define configNUM_TX_DESCRIPTORS 2
#endif

#ifndef NETWORK_IRQHandler
	#error NETWORK_IRQHandler must be defined to the name of the function that is installed in the interrupt vector table to handle Ethernet interrupts.
#endif

#if !defined( MAC_FF_HMC )
	/* Hash for multicast. */
	#define MAC_FF_HMC     ( 1UL << 2UL )
#endif

#ifndef iptraceEMAC_TASK_STARTING
	#define iptraceEMAC_TASK_STARTING()	do { } while( 0 )
#endif

#define nwERROR_STATUS_BITS		DMA_ST_TPS /* Transmit process stopped */ 	| \
								DMA_ST_TJT /* Transmit jabber timeout */	| \
								DMA_ST_OVF /* Receive overflow */			| \
								DMA_ST_UNF /* Transmit underflow */			| \
								DMA_ST_RU  /* Receive buffer unavailable */	| \
								DMA_ST_RPS /* Received process stopped */	| \
								DMA_ST_RWT /* Receive watchdog timeout */	| \
								DMA_ST_ETI /* Early transmit interrupt */	| \
								DMA_ST_FBI /* Fatal bus error interrupt */	| \
								DMA_ST_AIE

 /* If ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES is set to 1, then the Ethernet
 driver will filter incoming packets and only pass the stack those packets it
 considers need processing. */
 #if( ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0 )
 	#define ipCONSIDER_FRAME_FOR_PROCESSING( pucEthernetBuffer ) eProcessBuffer
 #else
 	#define ipCONSIDER_FRAME_FOR_PROCESSING( pucEthernetBuffer ) eConsiderFrameForProcessing( ( pucEthernetBuffer ) )
 #endif

/*-----------------------------------------------------------*/

/*
 * Delay function passed into the library.  The implementation uses FreeRTOS
 * calls so the scheduler must be started before the driver can be used.
 */
static void prvDelay( uint32_t ulMilliSeconds );

/*
 * Initialises the Tx and Rx descriptors respectively.
 */
static void prvSetupTxDescriptors( void );
static void prvSetupRxDescriptors( void );

/*
 * A task that processes received frames.
 */
static void prvEMACHandlerTask( void *pvParameters );

/*
 * Sets up the MAC with the results of an auto-negotiation.
 */
static BaseType_t prvSetLinkSpeed( void );

/*
 * Generates a CRC for a MAC address that is then used to generate a hash index.
 */
static uint32_t prvGenerateCRC32( const uint8_t *ucAddress );

/*
 * Generates a hash index when setting a filter to permit a MAC address.
 */
static uint32_t prvGetHashIndex( const uint8_t *ucAddress );

/*
 * Update the hash table to allow a MAC address.
 */
static void prvAddMACAddress( const uint8_t* ucMacAddress );

/*
 * Sometimes the DMA will report received data as being longer than the actual
 * received from length.  This function checks the reported length and corrects
 * if if necessary.
 */
static void prvRemoveTrailingBytes( NetworkBufferDescriptor_t *pxDescriptor );

/*-----------------------------------------------------------*/

/* A copy of PHY register 1: 'PHY_REG_01_BMSR' */
static uint32_t ulPHYLinkStatus = 0;

/* Tx descriptors and index. */
static ENET_ENHTXDESC_T xDMATxDescriptors[ configNUM_TX_DESCRIPTORS ];
static uint32_t ulNextFreeTxDescriptor;

/* Rx descriptors and index. */
static ENET_ENHRXDESC_T xDMARxDescriptors[ configNUM_RX_DESCRIPTORS ];
static uint32_t ulNextRxDescriptorToProcess;

/* Must be defined externally - the demo applications define this in main.c. */
extern uint8_t ucMACAddress[ 6 ];

/* The handle of the task that processes Rx packets.  The handle is required so
the task can be notified when new packets arrive. */
static TaskHandle_t xRxHanderTask = NULL;

#if( ipconfigUSE_LLMNR == 1 )
	static const uint8_t xLLMNR_MACAddress[] = { '\x01', '\x00', '\x5E', '\x00', '\x00', '\xFC' };
#endif	/* ipconfigUSE_LLMNR == 1 */

/*-----------------------------------------------------------*/


BaseType_t xNetworkInterfaceInitialise( void )
{
BaseType_t xReturn = pdPASS;

	/* The interrupt will be turned on when a link is established. */
	NVIC_DisableIRQ( ETHERNET_IRQn );

	/* Disable receive and transmit DMA processes. */
	LPC_ETHERNET->DMA_OP_MODE &= ~( DMA_OM_ST | DMA_OM_SR );

	/* Disable packet reception. */
	LPC_ETHERNET->MAC_CONFIG &= ~( MAC_CFG_RE | MAC_CFG_TE );

	/* Call the LPCOpen function to initialise the hardware. */
	Chip_ENET_Init( LPC_ETHERNET );

	/* Save MAC address. */
	Chip_ENET_SetADDR( LPC_ETHERNET, ucMACAddress );

	/* Clear all MAC address hash entries. */
	LPC_ETHERNET->MAC_HASHTABLE_HIGH = 0;
	LPC_ETHERNET->MAC_HASHTABLE_LOW = 0;

	#if( ipconfigUSE_LLMNR == 1 )
	{
		prvAddMACAddress( xLLMNR_MACAddress );
	}
	#endif /* ipconfigUSE_LLMNR == 1 */

	/* Promiscuous flag (PR) and Receive All flag (RA) set to zero.  The
	registers MAC_HASHTABLE_[LOW|HIGH] will be loaded to allow certain
	multi-cast addresses. */
	LPC_ETHERNET->MAC_FRAME_FILTER = MAC_FF_HMC;

	#if( configUSE_RMII == 1 )
	{
		if( lpc_phy_init( pdTRUE, prvDelay ) != SUCCESS )
		{
			xReturn = pdFAIL;
		}
	}
	#else
	{
		#warning This path has not been tested.
		if( lpc_phy_init( pdFALSE, prvDelay ) != SUCCESS )
		{
			xReturn = pdFAIL;
		}
	}
	#endif

	if( xReturn == pdPASS )
	{
		/* Guard against the task being created more than once and the
		descriptors being initialised more than once. */
		if( xRxHanderTask == NULL )
		{
			xReturn = xTaskCreate( prvEMACHandlerTask, "EMAC", nwRX_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, &xRxHanderTask );
			configASSERT( xReturn );
		}

		/* Enable MAC interrupts.  At this time Tx interrupts are not used. */
		LPC_ETHERNET->DMA_INT_EN =
			DMA_IE_OVE |	/* Overflow interrupt enable */
			DMA_IE_RIE |	/* Receive interrupt enable */
			DMA_IE_NIE |	/* Normal interrupt summary enable */
			DMA_IE_AIE |	/* Abnormal interrupt summary enable */
			DMA_IE_RUE |	/* Receive buffer unavailable enable */
			DMA_IE_UNE;		/* Underflow interrupt enable. */
	}

	if( xReturn != pdFAIL )
	{
		/* Auto-negotiate was already started.  Wait for it to complete. */
		xReturn = prvSetLinkSpeed();

		if( xReturn == pdPASS )
		{
       		/* Initialise the descriptors. */
			prvSetupTxDescriptors();
			prvSetupRxDescriptors();

			/* Clear all interrupts. */
			LPC_ETHERNET->DMA_STAT = DMA_ST_ALL;

			/* Enable receive and transmit DMA processes. */
			LPC_ETHERNET->DMA_OP_MODE |= DMA_OM_ST | DMA_OM_SR;

			/* Enable packet reception. */
			LPC_ETHERNET->MAC_CONFIG |= MAC_CFG_RE | MAC_CFG_TE;

			/* Start receive polling. */
			LPC_ETHERNET->DMA_REC_POLL_DEMAND = 1;

			/* Enable interrupts in the NVIC. */
			NVIC_SetPriority( ETHERNET_IRQn, configMAC_INTERRUPT_PRIORITY );
			NVIC_EnableIRQ( ETHERNET_IRQn );
		}
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

BaseType_t xNetworkInterfaceOutput( NetworkBufferDescriptor_t * const pxDescriptor, BaseType_t bReleaseAfterSend )
{
BaseType_t xReturn = pdFAIL, x;
const BaseType_t xMaxAttempts = 15;
const TickType_t xTimeBetweenTxAttemtps = pdMS_TO_TICKS( 5 );

	/* Attempt to obtain access to a Tx descriptor. */
	for( x = 0; x < xMaxAttempts; x++ )
	{
		/* If the descriptor is still owned by the DMA it can't be used. */
		if( ( xDMATxDescriptors[ ulNextFreeTxDescriptor ].CTRLSTAT & TDES_OWN ) == 0 )
		{
			/* The ES bit of CTRLSTAT could be checked here to determin if the
			previous frame was sent successfully or not. */

			#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
			{
				/* bReleaseAfterSend should always be set when using the zero
				copy driver. */
				configASSERT( bReleaseAfterSend != pdFALSE );

				if( xDMATxDescriptors[ ulNextFreeTxDescriptor ].B1ADD != ( uint32_t ) NULL )
				{
					/* Release the buffer that the DMA's descriptor was pointing
					to as it has since been sent.  First obtain the buffer's
					descriptor. */
					vReleaseNetworkBuffer( ( uint8_t * ) ( xDMATxDescriptors[ ulNextFreeTxDescriptor ].B1ADD ) );
				}

				/* The DMA's descriptor to point directly to the data in the
				network buffer descriptor.  The data is not copied. */
				xDMATxDescriptors[ ulNextFreeTxDescriptor ].B1ADD = ( uint32_t ) pxDescriptor->pucEthernetBuffer;

				/* The data buffer is now owned by the DMA and will be freed
				the next time the DMA's descriptor is re-used.  For now set the
				pointer to the data in the network buffer descriptor to NULL so
				the network buffer can be released without the data being
				pointed to also being freed. */
				pxDescriptor->pucEthernetBuffer = NULL;
			}
			#else
			{
				/* The data is copied from the network buffer descriptor into
				the DMA's descriptor. */
				memcpy( ( void * ) xDMATxDescriptors[ ulNextFreeTxDescriptor ].B1ADD, ( void * ) pxDescriptor->pucEthernetBuffer, pxDescriptor->xDataLength );
			}
			#endif

			xDMATxDescriptors[ ulNextFreeTxDescriptor ].BSIZE = ( uint32_t ) TDES_ENH_BS1( pxDescriptor->xDataLength );

			/* This descriptor is given back to the DMA. */
			xDMATxDescriptors[ ulNextFreeTxDescriptor ].CTRLSTAT |= TDES_OWN;

            iptraceNETWORK_INTERFACE_TRANSMIT();

			/* Move onto the next descriptor, wrapping if necessary. */
			ulNextFreeTxDescriptor++;
			if( ulNextFreeTxDescriptor >= configNUM_TX_DESCRIPTORS )
			{
				ulNextFreeTxDescriptor = 0;
			}

			/* Ensure the DMA is polling Tx descriptors. */
			LPC_ETHERNET->DMA_TRANS_POLL_DEMAND = 1;

			/* The Tx has been initiated. */
			xReturn = pdPASS;
			break;
		}
		else
		{
			/* The next Tx descriptor was still in use - wait a while for it to
			become free. */
			iptraceWAITING_FOR_TX_DMA_DESCRIPTOR();
			vTaskDelay( xTimeBetweenTxAttemtps );
		}
	}

	/* The buffer has been sent so can be released. */
	if( bReleaseAfterSend != pdFALSE )
	{
		vReleaseNetworkBufferAndDescriptor( pxDescriptor );
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

static void prvDelay( uint32_t ulMilliSeconds )
{
	/* Ensure the scheduler was started before attempting to use the scheduler to
	create a delay. */
	configASSERT( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING );

	vTaskDelay( pdMS_TO_TICKS( ulMilliSeconds ) );
}
/*-----------------------------------------------------------*/

static void prvSetupTxDescriptors( void )
{
BaseType_t x;

	/* Start with Tx descriptors clear. */
	memset( ( void * ) xDMATxDescriptors, 0, sizeof( xDMATxDescriptors ) );

	/* Index to the next Tx descriptor to use. */
	ulNextFreeTxDescriptor = 0;

	for( x = 0; x < configNUM_TX_DESCRIPTORS; x++ )
	{
		#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
		{
			/* Nothing to do, B1ADD will be set when data is ready to transmit.
			Currently the memset above will have set it to NULL. */
		}
		#else
		{
			/* Allocate a buffer to the Tx descriptor.  This is the most basic
			way of creating a driver as the data is then copied into the
			buffer. */
			xDMATxDescriptors[ x ].B1ADD = ( uint32_t ) pvPortMalloc( ipTOTAL_ETHERNET_FRAME_SIZE );

			/* Use an assert to check the allocation as +TCP applications will
			often not use a malloc failed hook as the TCP stack will recover
			from allocation failures. */
			configASSERT( xDMATxDescriptors[ x ].B1ADD );
		}
		#endif

		/* Buffers hold an entire frame so all buffers are both the start and
		end of a frame. */
		xDMATxDescriptors[ x ].CTRLSTAT = TDES_ENH_TCH | TDES_ENH_CIC( 3 ) | TDES_ENH_FS | TDES_ENH_LS;
		xDMATxDescriptors[ x ].B2ADD = ( uint32_t ) &xDMATxDescriptors[ x + 1 ];
	}

	xDMATxDescriptors[ configNUM_TX_DESCRIPTORS - 1 ].CTRLSTAT |= TDES_ENH_TER;
	xDMATxDescriptors[ configNUM_TX_DESCRIPTORS - 1 ].B2ADD = ( uint32_t ) &xDMATxDescriptors[ 0 ];

	/* Point the DMA to the base of the descriptor list. */
	LPC_ETHERNET->DMA_TRANS_DES_ADDR = ( uint32_t ) xDMATxDescriptors;
}
/*-----------------------------------------------------------*/

static void prvSetupRxDescriptors( void )
{
BaseType_t x;

	/* Index to the next Rx descriptor to use. */
	ulNextRxDescriptorToProcess = 0;

	/* Clear RX descriptor list. */
	memset( ( void * )  xDMARxDescriptors, 0, sizeof( xDMARxDescriptors ) );

	for( x = 0; x < configNUM_RX_DESCRIPTORS; x++ )
	{
		/* Allocate a buffer of the largest	possible frame size as it is not
		known what size received frames will be. */
		xDMARxDescriptors[ x ].B1ADD = ( uint32_t ) pvPortMalloc( ipTOTAL_ETHERNET_FRAME_SIZE );

		/* Use an assert to check the allocation as +TCP applications will often
		not use a malloc failed hook as the TCP stack will recover from
		allocation failures. */
		configASSERT( xDMARxDescriptors[ x ].B1ADD );

		xDMARxDescriptors[ x ].CTRL = RDES_ENH_RCH | RDES_ENH_RER;
		xDMARxDescriptors[ x ].B2ADD = ( uint32_t ) &( xDMARxDescriptors[ x + 1 ] );
		xDMARxDescriptors[ x ].CTRL = ( uint32_t ) RDES_ENH_BS1( ipTOTAL_ETHERNET_FRAME_SIZE ) | RDES_ENH_RCH;

		/* The descriptor is available for use by the DMA. */
		xDMARxDescriptors[ x ].STATUS = RDES_OWN;
	}

	xDMARxDescriptors[ ( configNUM_RX_DESCRIPTORS - 1 ) ].CTRL |= RDES_ENH_RER;
	xDMARxDescriptors[ configNUM_RX_DESCRIPTORS - 1 ].B2ADD = ( uint32_t ) &( xDMARxDescriptors[ 0 ] );

	/* Point the DMA to the base of the descriptor list. */
	LPC_ETHERNET->DMA_REC_DES_ADDR = ( uint32_t ) xDMARxDescriptors;
}
/*-----------------------------------------------------------*/

static void prvRemoveTrailingBytes( NetworkBufferDescriptor_t *pxDescriptor )
{
size_t xExpectedLength;
IPPacket_t *pxIPPacket;

	pxIPPacket = ( IPPacket_t * ) pxDescriptor->pucEthernetBuffer;
	xExpectedLength = sizeof( EthernetHeader_t ) + ( size_t ) FreeRTOS_htons( pxIPPacket->xIPHeader.usLength );
#warning Remove magic numbers and try to comment what this is doing?
#warning Why does it not just set the length to that expected?
#warning And why does a usLength of 8 get swapped to be near 2000?
	if( xExpectedLength == ( pxDescriptor->xDataLength + 4 ) )
	{
		pxDescriptor->xDataLength -= 4;
	}
	else
	{
		if( pxDescriptor->xDataLength > xExpectedLength )
		{
			pxDescriptor->xDataLength = ( size_t ) xExpectedLength;
		}
	}
}
/*-----------------------------------------------------------*/

BaseType_t xGetPhyLinkStatus( void )
{
BaseType_t xReturn;

	if( ( ulPHYLinkStatus & PHY_LINK_CONNECTED ) == 0 )
	{
		xReturn = pdFALSE;
	}
	else
	{
		xReturn = pdTRUE;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

static void prvEMACHandlerTask( void *pvParameters )
{
TimeOut_t xPhyTime;
TickType_t xPhyRemTime;
UBaseType_t uxLastMinBufferCount = 0;
UBaseType_t uxCurrentCount;
BaseType_t xResult = 0;
uint32_t ulStatus, ulDataAvailable = pdFALSE;
uint16_t usLength;
static NetworkBufferDescriptor_t *pxDescriptor;
const TickType_t xDescriptorWaitTime = pdMS_TO_TICKS( 250 );
const UBaseType_t uxMinimumBuffersRemaining = 5UL;
IPStackEvent_t xRxEvent = { eNetworkRxEvent, NULL };
const TickType_t xBlockTimeWhenRxDataWaiting = pdMS_TO_TICKS( 7 );
const TickType_t xBlockTimeWhenNoRxDataWaiting = pdMS_TO_TICKS( 200 );
eFrameProcessingResult_t eResult;

	/* Remove compiler warning about unused parameter. */
	( void ) pvParameters;

	/* A possibility to set some additional task properties. */
	iptraceEMAC_TASK_STARTING();

	vTaskSetTimeOutState( &xPhyTime );
	xPhyRemTime = pdMS_TO_TICKS( PHY_LS_LOW_CHECK_TIME_MS );

	for( ;; )
	{
		uxCurrentCount = uxGetMinimumFreeNetworkBuffers();
		if( uxLastMinBufferCount != uxCurrentCount )
		{
			/* The logging produced below may be helpful
			while tuning +TCP: see how many buffers are in use. */
			uxLastMinBufferCount = uxCurrentCount;
			FreeRTOS_printf( ( "Network buffers: %lu lowest %lu\n",
				uxGetNumberOfFreeNetworkBuffers(), uxCurrentCount ) );
		}

		#if( ipconfigCHECK_IP_QUEUE_SPACE != 0 )
		{
		static UBaseType_t uxLastMinQueueSpace = 0;

			uxCurrentCount = uxGetMinimumIPQueueSpace();
			if( uxLastMinQueueSpace != uxCurrentCount )
			{
				/* The logging produced below may be helpful
				while tuning +TCP: see how many buffers are in use. */
				uxLastMinQueueSpace = uxCurrentCount;
				FreeRTOS_printf( ( "Queue space: lowest %lu\n", uxCurrentCount ) );
			}
		}
		#endif /* ipconfigCHECK_IP_QUEUE_SPACE */

		if( ulDataAvailable == pdTRUE )
		{
			/* The DMAs Rx descriptors contain data but the data cannot yet be
			consumed because it was not possible to allocate a buffer to hold
			it.  Block for a short while then try again. */
			ulTaskNotifyTake( pdTRUE, xBlockTimeWhenRxDataWaiting );
		}
		else
		{
			/* Wait until notified about new data.  No CPU time will be used
			while the task is blocked on this call. */
			ulTaskNotifyTake( pdTRUE, xBlockTimeWhenNoRxDataWaiting );
		}

		/* Process each descriptor that is not still in use by the DMA. */
		ulStatus = xDMARxDescriptors[ ulNextRxDescriptorToProcess ].STATUS;
		while( ( ulStatus & RDES_OWN ) == 0 )
		{
			/* The Rx DMA has data waiting to be processed. */
			ulDataAvailable = pdTRUE;

			/* Check for errors. */
			if( ( ulStatus & DMA_ST_AIE ) == 0 )
			{
				xResult++;
				eResult = ipCONSIDER_FRAME_FOR_PROCESSING( ( const uint8_t * const ) ( xDMARxDescriptors[ ulNextRxDescriptorToProcess ].B1ADD ) );
				if( eResult == eProcessBuffer )
				{
					if( ( ulPHYLinkStatus & PHY_LINK_CONNECTED ) == 0 )
					{
						ulPHYLinkStatus |= PHY_LINK_CONNECTED;
						FreeRTOS_printf( ( "prvEMACHandlerTask: PHY LS now %d (message received)\n", ( ulPHYLinkStatus & PHY_LINK_CONNECTED ) != 0 ) );
					}
					/* Don't consume all the buffers if there are bursts of
					traffic. */
					if( uxGetNumberOfFreeNetworkBuffers() > uxMinimumBuffersRemaining )
					{
						/* Create a buffer of exactly the required length. */
						usLength = RDES_FLMSK( ulStatus );
						pxDescriptor = pxGetNetworkBufferWithDescriptor( usLength, xDescriptorWaitTime );

						if( pxDescriptor != NULL )
						{
							pxDescriptor->xDataLength = ( size_t ) usLength;

							/* Copy the data into the allocated buffer.  This is the
							most basic way of creating a driver.  More run time
							efficient drivers will assign the newly allocated buffer
							to the DMA, and use the buffer that has already been
							filled by the DMA - the more run time efficient
							technique could however consume more RAM as full size
							buffers would be required unless descriptors are allowed
							to chain. */
							memcpy( ( void * ) pxDescriptor->pucEthernetBuffer, ( void * ) xDMARxDescriptors[ ulNextRxDescriptorToProcess ].B1ADD, usLength );

							/* It is possible that more data was copied than
							actually makes up the frame.  If this is the case
							adjust the length to remove any trailing bytes. */
							prvRemoveTrailingBytes( pxDescriptor );

							/* Pass the data to the TCP/IP task for processing. */
							xRxEvent.pvData = ( void * ) pxDescriptor;
							if( xSendEventStructToIPTask( &xRxEvent, xDescriptorWaitTime ) == pdFALSE )
							{
								/* Could not send the descriptor into the TCP/IP
								stack, it must be released. */
								vReleaseNetworkBufferAndDescriptor( pxDescriptor );
							}
							else
							{
								iptraceNETWORK_INTERFACE_RECEIVE();

								/* The data that was available at the top of this
								loop has been sent, so is no longer available. */
								ulDataAvailable = pdFALSE;
							}
						}
					}
				}
				else
				{
					/* The packet is discarded as uninteresting. */
					ulDataAvailable = pdFALSE;
				}
			}
			else
			{
				/* Clear error bits. */
				xDMARxDescriptors[ ulNextRxDescriptorToProcess ].STATUS = nwERROR_STATUS_BITS;
				ulDataAvailable = pdFALSE;
			}

			if( ulDataAvailable == pdFALSE )
			{
				/* Got here because received data was sent to the IP task or the
				data contained an error and was discarded.  Give the descriptor
				back to the DMA. */
				xDMARxDescriptors[ ulNextRxDescriptorToProcess ].STATUS |= RDES_OWN;

				/* Move onto the next descriptor. */
				ulNextRxDescriptorToProcess++;
				if( ulNextRxDescriptorToProcess >= configNUM_RX_DESCRIPTORS )
				{
					ulNextRxDescriptorToProcess = 0;
				}

				ulStatus = xDMARxDescriptors[ ulNextRxDescriptorToProcess ].STATUS;
			}
			else
			{
				/* Got into this loop because there was Rx data available, but
				the Rx data is still available - so for some reason it could
				not be sent to the IP stack.  Exit now, leaving the Rx
				descriptor untouched to attempt to receive the data again
				next time. */
				break;
			}
		}

		/* Restart receive polling. */
		LPC_ETHERNET->DMA_REC_POLL_DEMAND = 1;

		if( xResult > 0 )
		{
			/* A packet was received. No need to check for the PHY status now,
			but set a timer to check it later on. */
			vTaskSetTimeOutState( &xPhyTime );
			xPhyRemTime = pdMS_TO_TICKS( PHY_LS_HIGH_CHECK_TIME_MS );
			xResult = 0;
		}
		else if( xTaskCheckForTimeOut( &xPhyTime, &xPhyRemTime ) != pdFALSE )
		{
			ulStatus = lpcPHYStsPoll();

			if( ( ulPHYLinkStatus & PHY_LINK_CONNECTED ) != ( ulStatus & PHY_LINK_CONNECTED ) )
			{
				ulPHYLinkStatus = ulStatus;
				FreeRTOS_printf( ( "prvEMACHandlerTask: PHY LS now %d (polled PHY)\n", ( ulPHYLinkStatus & PHY_LINK_CONNECTED ) != 0 ) );
			}

			vTaskSetTimeOutState( &xPhyTime );
			if( ( ulPHYLinkStatus & PHY_LINK_CONNECTED ) != 0 )
			{
				xPhyRemTime = pdMS_TO_TICKS( PHY_LS_HIGH_CHECK_TIME_MS );
			}
			else
			{
				xPhyRemTime = pdMS_TO_TICKS( PHY_LS_LOW_CHECK_TIME_MS );
			}
		}

	}
}
/*-----------------------------------------------------------*/

void NETWORK_IRQHandler( void )
{
BaseType_t xHigherPriorityTaskWoken = pdFALSE;
uint32_t ulInterrupts;
const uint32_t ulRxInterruptMask =
	DMA_ST_RI |		/* Receive interrupt */
	DMA_ST_RU;		/* Receive buffer unavailable */

	configASSERT( xRxHanderTask );

	/* Get pending interrupts. */
	ulInterrupts = LPC_ETHERNET->DMA_STAT;

	/* RX group interrupt(s). */
	if( ( ulInterrupts & ulRxInterruptMask ) != 0x00 )
	{
		/* A packet may be waiting to be received. */
		xHigherPriorityTaskWoken = pdFALSE;
		vTaskNotifyGiveFromISR( xRxHanderTask, &xHigherPriorityTaskWoken );
	}

	/* Test for 'Abnormal interrupt summary'. */
	if( ( ulInterrupts & DMA_ST_AIE ) != 0x00 )
	{
		/* The trace macro must be written such that it can be called from
		an interrupt. */
		iptraceETHERNET_RX_EVENT_LOST();
	}

	/* Clear pending interrupts */
	LPC_ETHERNET->DMA_STAT = ulInterrupts;

	/* Context switch needed? */
	portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
}
/*-----------------------------------------------------------*/

static BaseType_t prvSetLinkSpeed( void )
{
BaseType_t xReturn = pdFAIL;
TickType_t xTimeOnEntering;
uint32_t ulPhyStatus;
const TickType_t xAutoNegotiateDelay = pdMS_TO_TICKS( 5000UL );

	/* Ensure polling does not starve lower priority tasks by temporarily
	setting the priority of this task to that of the idle task. */
	vTaskPrioritySet( NULL, tskIDLE_PRIORITY );

	xTimeOnEntering = xTaskGetTickCount();
	do
	{
		ulPhyStatus = lpcPHYStsPoll();
		if( ( ulPhyStatus & PHY_LINK_CONNECTED ) != 0x00 )
		{
			/* Set interface speed and duplex. */
			if( ( ulPhyStatus & PHY_LINK_SPEED100 ) != 0x00 )
			{
				Chip_ENET_SetSpeed( LPC_ETHERNET, 1 );
			}
			else
			{
				Chip_ENET_SetSpeed( LPC_ETHERNET, 0 );
			}

			if( ( ulPhyStatus & PHY_LINK_FULLDUPLX ) != 0x00 )
			{
				Chip_ENET_SetDuplex( LPC_ETHERNET, true );
			}
			else
			{
				Chip_ENET_SetDuplex( LPC_ETHERNET, false );
			}

			xReturn = pdPASS;
			break;
		}
	} while( ( xTaskGetTickCount() - xTimeOnEntering ) < xAutoNegotiateDelay );

	/* Reset the priority of this task back to its original value. */
	vTaskPrioritySet( NULL, ipconfigIP_TASK_PRIORITY );

	return xReturn;
}
/*-----------------------------------------------------------*/

static uint32_t prvGenerateCRC32( const uint8_t *ucAddress )
{
unsigned int j;
const uint32_t Polynomial = 0xEDB88320;
uint32_t crc = ~0ul;
const uint8_t *pucCurrent = ( const uint8_t * ) ucAddress;
const uint8_t *pucLast = pucCurrent + 6;

    /* Calculate  normal CRC32 */
    while( pucCurrent < pucLast )
    {
        crc ^= *( pucCurrent++ );
        for( j = 0; j < 8; j++ )
        {
            if( ( crc & 1 ) != 0 )
            {
                crc = (crc >> 1) ^ Polynomial;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}
/*-----------------------------------------------------------*/

static uint32_t prvGetHashIndex( const uint8_t *ucAddress )
{
uint32_t ulCrc = prvGenerateCRC32( ucAddress );
uint32_t ulIndex = 0ul;
BaseType_t xCount = 6;

    /* Take the lowest 6 bits of the CRC32 and reverse them */
    while( xCount-- )
    {
        ulIndex <<= 1;
        ulIndex |= ( ulCrc & 1 );
        ulCrc >>= 1;
    }

    /* This is the has value of 'ucAddress' */
    return ulIndex;
}
/*-----------------------------------------------------------*/

static void prvAddMACAddress( const uint8_t* ucMacAddress )
{
BaseType_t xIndex;

    xIndex = prvGetHashIndex( ucMacAddress );
    if( xIndex >= 32 )
    {
        LPC_ETHERNET->MAC_HASHTABLE_HIGH |= ( 1u << ( xIndex - 32 ) );
    }
    else
    {
        LPC_ETHERNET->MAC_HASHTABLE_LOW |= ( 1u << xIndex );
    }
}
