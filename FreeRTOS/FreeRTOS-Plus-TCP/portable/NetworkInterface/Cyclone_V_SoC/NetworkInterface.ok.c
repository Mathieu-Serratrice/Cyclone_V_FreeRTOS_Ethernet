/*
 * FreeRTOS+TCP Labs Build 200417 (C) 2016 Real Time Engineers ltd.
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

/* Interrupt events to process.  Currently only the Rx event is processed
although code for other events is included to allow for possible future
expansion. */
#define EMAC_IF_RX_EVENT        1UL
#define EMAC_IF_TX_EVENT        2UL
#define EMAC_IF_ERR_EVENT       4UL
#define EMAC_IF_ALL_EVENT       ( EMAC_IF_RX_EVENT | EMAC_IF_TX_EVENT | EMAC_IF_ERR_EVENT )


#include "socal/hps.h"
#include "socal/alt_rstmgr.h"
#include "alt_cache.h"

#include "cyclone_dma.h"
#include "cyclone_emac.h"

/* Provided memory configured as uncached. */
#include "uncached_memory.h"

#include "socal/alt_emac.h"

#define niBMSR_LINK_STATUS         0x0004ul

#ifndef	PHY_LS_HIGH_CHECK_TIME_MS
	/* Check if the LinkSStatus in the PHY is still high after 15 seconds of not
	receiving packets. */
	#define PHY_LS_HIGH_CHECK_TIME_MS	15000
#endif

#ifndef	PHY_LS_LOW_CHECK_TIME_MS
	/* Check if the LinkSStatus in the PHY is still low every second. */
	#define PHY_LS_LOW_CHECK_TIME_MS	1000
#endif

/* Naming and numbering of PHY registers. */
#define PHY_REG_01_BMSR			0x01	/* Basic mode status register */

#ifndef iptraceEMAC_TASK_STARTING
	#define iptraceEMAC_TASK_STARTING()	do { } while( 0 )
#endif

/* Default the size of the stack used by the EMAC deferred handler task to twice
the size of the stack used by the idle task - but allow this to be overridden in
FreeRTOSConfig.h as configMINIMAL_STACK_SIZE is a user definable constant. */
#ifndef configEMAC_TASK_STACK_SIZE
	#define configEMAC_TASK_STACK_SIZE ( 640 )
#endif

#define __DSB()		__asm volatile ( "DSB" )



/*-----------------------------------------------------------*/

/*
 * Look for the link to be up every few milliseconds until either xMaxTime time
 * has passed or a link is found.
 */
static BaseType_t prvGMACWaitLS( TickType_t xMaxTime );

/*
 * A deferred interrupt handler for all MAC/DMA interrupt sources.
 */
static void prvEMACHandlerTask( void *pvParameters );

/*-----------------------------------------------------------*/

extern int phy_detected;

/* A copy of PHY register 1: 'PHY_REG_01_BMSR' */
static uint32_t ulPHYLinkStatus = 0;

#if( ipconfigUSE_LLMNR == 1 )
	//static const uint8_t xLLMNR_MACAddress[] = { 0x01, 0x00, 0x5E, 0x00, 0x00, 0xFC };
#endif

/* ucMACAddress as it appears in main.c */
extern const uint8_t ucMACAddress[ 6 ];

/* Holds the handle of the task used as a deferred interrupt processor.  The
handle is used so direct notifications can be sent to the task for all EMAC/DMA
related interrupts. */
static TaskHandle_t xEMACTaskHandle = NULL;

static volatile uint32_t ulISREvents;

EMACInterface_t xEMACif;

static int iMacID = 1;

//__attribute__ ( ( aligned( 4096 ) ) ) __attribute__ ((section (".ddr_sdram"))) gmac_tx_descriptor_t txDescriptors[ GMAC_TX_BUFFERS ];
//__attribute__ ( ( aligned( 4096 ) ) ) __attribute__ ((section (".ddr_sdram"))) gmac_rx_descriptor_t rxDescriptors[ GMAC_RX_BUFFERS ];
__attribute__ ( ( aligned( 64 ) ) ) __attribute__ ((section (".oc_ram"))) gmac_tx_descriptor_t txDescriptors[ GMAC_TX_BUFFERS ];
__attribute__ ( ( aligned( 64 ) ) ) __attribute__ ((section (".oc_ram"))) gmac_rx_descriptor_t rxDescriptors[ GMAC_RX_BUFFERS ];

static gmac_tx_descriptor_t *pxNextTxDesc = txDescriptors;
static gmac_tx_descriptor_t *DMATxDescToClear = txDescriptors;

static gmac_rx_descriptor_t *pxNextRxDesc = rxDescriptors;

/* xTXDescriptorSemaphore is a counting semaphore with
a maximum count of GMAC_TX_BUFFERS, which is the number of
DMA TX descriptors. */
static SemaphoreHandle_t xTXDescriptorSemaphore = NULL;

/*
 * Check if a given packet should be accepted.
 */
static BaseType_t xMayAcceptPacket( uint8_t *pcBuffer );

/*!
 * The callback to use when an interrupt needs to be serviced.
 *
 * \param       icciar          The Interrupt Controller CPU Interrupt
 *                              Acknowledgement Register value (ICCIAR) value
 *                              corresponding to the current interrupt.
 *
 * \param       context         The user provided context.
 */
void vEMACInterrupthandler( uint32_t ulICCIAR, void * pvContext );
/*-----------------------------------------------------------*/

static void vClearTXBuffers()
{
gmac_tx_descriptor_t  *pxLastDescriptor = pxNextTxDesc;
size_t uxCount = ( ( UBaseType_t ) GMAC_TX_BUFFERS ) - uxSemaphoreGetCount( xTXDescriptorSemaphore );
#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
	NetworkBufferDescriptor_t *pxNetworkBuffer;
	uint8_t *ucPayLoad;
#endif

	/* This function is called after a TX-completion interrupt.
	It will release each Network Buffer used in xNetworkInterfaceOutput().
	'uxCount' represents the number of descriptors given to DMA for transmission.
	After sending a packet, the DMA will clear the own bit. */
	while( ( uxCount > 0 ) && ( DMATxDescToClear->own == 0 ) )
	{
		if( ( DMATxDescToClear == pxLastDescriptor ) && ( uxCount != GMAC_TX_BUFFERS ) )
		{
			break;
		}
		#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
		{
			ucPayLoad = ( uint8_t * )DMATxDescToClear->buf1_address;

			if( ucPayLoad != NULL )
			{
				pxNetworkBuffer = pxPacketBuffer_to_NetworkBuffer( ucPayLoad );
				if( pxNetworkBuffer != NULL )
				{
					vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer ) ;
				}
				DMATxDescToClear->buf1_address = ( uint32_t )0u;
			}
		}
		#endif /* ipconfigZERO_COPY_TX_DRIVER */

		DMATxDescToClear = ( gmac_tx_descriptor_t * )( DMATxDescToClear->next_descriptor );

		uxCount--;
		/* Tell the counting semaphore that one more TX descriptor is available. */
		xSemaphoreGive( xTXDescriptorSemaphore );
	}
}
/*-----------------------------------------------------------*/

#define RESET_MANAGER_ADDR		0xFFD05000
volatile ALT_RSTMGR_t *pxResetManager;
gmac_tx_descriptor_t *pxDmaTransmit;
gmac_rx_descriptor_t *rx_table;
gmac_tx_descriptor_t *tx_table;
EMAC_config_t *pxMAC_Config;
EMAC_interrupt_t *EMAC_int_status;
EMAC_interrupt_t *EMAC_int_mask;
ALT_EMAC_DMA_t *EMAC_DMA_regs;


SYSMGR_EMACGRP_t * sysmgr_emacgrp;
volatile uint32_t desc_copy[ sizeof( gmac_tx_descriptor_t ) / 4 ];
volatile DMA_STATUS_REG_t *dma_status_reg;
volatile DMA_STATUS_REG_t *dma_enable_reg;

BaseType_t xNetworkInterfaceInitialise( void )
{
const TickType_t xWaitLinkDelay = pdMS_TO_TICKS( 7000UL ), xWaitRelinkDelay = pdMS_TO_TICKS( 1000UL );
BaseType_t xLinkStatus;

	/* Guard against the init function being called more than once. */
	if( xEMACTaskHandle == NULL )
	{
		#warning Debugging : just checking the reset flags of all modules, need emac1 and dma
		pxResetManager = ( volatile ALT_RSTMGR_t * ) RESET_MANAGER_ADDR;
		pxResetManager->permodrst.emac1 = 1;

		pxMAC_Config = ( EMAC_config_t * )( ucFirstIOAddres( iMacID ) );
		pxMAC_Config->prelen = 1;
		EMAC_int_status = ( EMAC_interrupt_t * )( ucFirstIOAddres( iMacID ) + GMAC_INT_STATUS );
		EMAC_int_mask   = ( EMAC_interrupt_t * )( ucFirstIOAddres( iMacID ) + GMAC_INT_MASK );

		sysmgr_emacgrp = ( SYSMGR_EMACGRP_t * ) ( ALT_SYSMGR_OFST + 0x60 );
		sysmgr_emacgrp->physel_0 = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII;
		sysmgr_emacgrp->physel_1 = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII;
		dma_status_reg = ( volatile DMA_STATUS_REG_t * ) ( ucFirstIOAddres( iMacID ) + DMA_STATUS );
		dma_enable_reg = ( volatile DMA_STATUS_REG_t * ) ( ucFirstIOAddres( iMacID ) + DMA_INTR_ENA );

		EMAC_DMA_regs = (ALT_EMAC_DMA_t *)( ucFirstIOAddres( iMacID ) + DMA_BUS_MODE );

		vTaskDelay( 500ul );
		pxResetManager->permodrst.emac1 = 0;
		vTaskDelay( 500ul );

		if( xTXDescriptorSemaphore == NULL )
		{
			xTXDescriptorSemaphore = xSemaphoreCreateCounting( ( UBaseType_t ) GMAC_TX_BUFFERS, ( UBaseType_t ) GMAC_TX_BUFFERS );
			configASSERT( xTXDescriptorSemaphore );
		}

		dwmac1000_sys_init( iMacID );

		gmac_dma_stop_tx( iMacID, 0 );
		gmac_dma_stop_rx( iMacID, 0 );

		gmac_tx_descriptor_init( iMacID, txDescriptors, ( uint8_t *) NULL, GMAC_TX_BUFFERS );
		gmac_rx_descriptor_init( iMacID, rxDescriptors, ( uint8_t *) NULL, GMAC_RX_BUFFERS );


		/* Set the TxDesc pointer with the first one of the txDescriptors list */
		gmac_set_tx_table( iMacID, txDescriptors );

		/* Set the RxDesc pointer with the first one of the rxDescriptors list */
		gmac_set_rx_table( iMacID, rxDescriptors );

		gmac_dma_start_tx( iMacID, 0 );
		gmac_dma_start_rx( iMacID, 0 );
alt_cache_l2_sync();
		gmac_enable_transmission( iMacID, pdTRUE );

		gmac_dma_reception_poll( iMacID );
		gmac_dma_start_rx( iMacID, 0 );

		prvGMACWaitLS( xWaitLinkDelay );

// portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT
//#define EMAC_INT_PRIOITY		( ( 15u ) << portPRIORITY_SHIFT )
#define EMAC_INT_PRIOITY		( ( 2u ) << portPRIORITY_SHIFT )
//#define EMAC_INT_PRIOITY		( 128u )


		vRegisterIRQHandler( ALT_INT_INTERRUPT_EMAC1_IRQ, vEMACInterrupthandler, ( void *)&xEMACif );
		alt_int_dist_priority_set( ALT_INT_INTERRUPT_EMAC1_IRQ, EMAC_INT_PRIOITY );
		alt_int_dist_enable( ALT_INT_INTERRUPT_EMAC1_IRQ );
		alt_int_dist_trigger_set( ALT_INT_INTERRUPT_EMAC1_IRQ, ALT_INT_TRIGGER_AUTODETECT );

		{
			int index;
			for (index = 0; index < 1023; index++) {
				if (index == ALT_INT_INTERRUPT_PPI_TIMER_PRIVATE)
					continue;
				vRegisterIRQHandler( index, vEMACInterrupthandler, ( void *)&xEMACif );
				alt_int_dist_priority_set( index, EMAC_INT_PRIOITY );
				alt_int_dist_enable( index );
			}
		}

		/* Clear DMA interrupt status. */
		* ( ( uint32_t * ) dma_status_reg ) = 0x0001ffff;


		gmac_set_emac_interrupt_enable( iMacID, ( uint32_t )~0u );// GMAC_INT_STATUS_MMCRIS | GMAC_INT_STATUS_MMCTIS );

		//#define DMA_INTR_NORMAL	( DMA_INTR_ENA_NIE | DMA_INTR_ENA_RIE | DMA_INTR_ENA_TIE )
		gmac_set_dma_interrupt_enable( iMacID, DMA_INTR_NORMAL | DMA_INTR_ABNORMAL );//0x1ffff );//DMA_INTR_DEFAULT_MASK );

if( dma_status_reg->ulValue ) {}

		/* The deferred interrupt handler task is created at the highest
		possible priority to ensure the interrupt handler can return directly
		to it.  The task's handle is stored in xEMACTaskHandle so interrupts can
		notify the task when there is something to process. */
		xTaskCreate( prvEMACHandlerTask, "EMAC", configEMAC_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, &xEMACTaskHandle );
	}
	else
	{
		/* Initialisation was already performed, just wait for the link. */
		prvGMACWaitLS( xWaitRelinkDelay );
	}

	/* Only return pdTRUE when the Link Status of the PHY is high, otherwise the
	DHCP process and all other communication will fail. */
	xLinkStatus = xGetPhyLinkStatus();

	return ( xLinkStatus != pdFALSE );
}
/*-----------------------------------------------------------*/

volatile unsigned emac_check_count = 0;
volatile unsigned emac_rx_count = 0;
volatile unsigned emac_tx_count = 0;
void vEMACInterrupthandler( uint32_t ulICCIAR, void * pvContext )
{
uint32_t ulDMAStatus;
//uint32_t ulEMACStatus;

	/* Get DMA interrupt status and clear all bits. */
//	ulEMACStatus = gmac_get_emac_interrupt_status( iMacID, pdTRUE );
	ulDMAStatus  = gmac_get_dma_interrupt_status( iMacID, pdTRUE );
//#define DMA_STATUS_RI           0x00000040	/* Receive Interrupt */
//#define DMA_STATUS_TI           0x00000001	/* Transmit Interrupt */

	emac_check_count++;
	if( ( ulDMAStatus & ( DMA_STATUS_RI | DMA_STATUS_RU | DMA_STATUS_OVF ) ) != 0 )
	{
		emac_rx_count++;
		ulISREvents |= EMAC_IF_RX_EVENT;
	}
	if( ( ulDMAStatus & ( DMA_STATUS_TI | DMA_STATUS_TU | DMA_STATUS_TPS ) ) != 0 )
	{
		emac_tx_count++;
		ulISREvents |= EMAC_IF_TX_EVENT;
	}
	if( ( ulISREvents != 0 ) && ( xEMACTaskHandle != NULL ) )
	{
		xTaskNotifyGive( xEMACTaskHandle );
//		vTaskNotifyGiveFromISR( xEMACTaskHandle, &xHigherPriorityTaskWoken );
	}
}

#if( configUSE_IDLE_HOOK == 1 )
	/* Variable must be set by main.c as soon as +TCP is up. */
	BaseType_t xPlusTCPStarted;

	extern void vApplicationIdleHook( void );
	void vApplicationIdleHook()
	{
		if (xPlusTCPStarted) {
			vEMACInterrupthandler( ALT_INT_INTERRUPT_EMAC1_IRQ, ( void *)&xEMACif );
		}
	}
#endif

static void vWaitTxDone(void);
static void vWaitTxDone()
{
int iCount;
	/* Temporary function, will be removed. */
	for( iCount = 10; iCount; iCount-- )
	{
		if( ( uxSemaphoreGetCount( xTXDescriptorSemaphore ) < GMAC_TX_BUFFERS ) &&
			( DMATxDescToClear->own == 0 ) )
		{
			/* This will be done from within an ISR ( transmission done ). */
			ulISREvents |= EMAC_IF_TX_EVENT;
			if( xEMACTaskHandle != NULL )
			{
				xTaskNotifyGive( xEMACTaskHandle );
			}
			break;
		}
		vTaskDelay( 2 );
	}
}

BaseType_t xNetworkInterfaceOutput( NetworkBufferDescriptor_t * const pxDescriptor, BaseType_t bReleaseAfterSend )
{
BaseType_t xReturn = pdFAIL;
uint32_t ulTransmitSize = 0;
gmac_tx_descriptor_t *pxDmaTxDesc;
/* Do not wait too long for a free TX DMA buffer. */
const TickType_t xBlockTimeTicks = pdMS_TO_TICKS( 50u );

	#if( ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM != 0 )
	{
	ProtocolPacket_t *pxPacket;

		#if( ipconfigZERO_COPY_RX_DRIVER != 0 )
		{
			configASSERT( bReleaseAfterSend != 0 );
		}
		#endif /* ipconfigZERO_COPY_RX_DRIVER */

		/* If the peripheral must calculate the checksum, it wants
		the protocol checksum to have a value of zero. */
		pxPacket = ( ProtocolPacket_t * ) ( pxDescriptor->pucEthernetBuffer );

		if( pxPacket->xICMPPacket.xIPHeader.ucProtocol == ipPROTOCOL_ICMP )
		{
			pxPacket->xICMPPacket.xICMPHeader.usChecksum = ( uint16_t )0u;
		}
	}
	#endif

	/* Open a do {} while ( 0 ) loop to be able to call break. */
	do
	{
		if( xGetPhyLinkStatus() != 0 )
		{
			if( xSemaphoreTake( xTXDescriptorSemaphore, xBlockTimeTicks ) != pdPASS )
			{
				/* Time-out waiting for a free TX descriptor. */
				break;
			}

			rx_table = gmac_get_rx_table( iMacID );
			tx_table = gmac_get_tx_table( iMacID );

			/* This function does the actual transmission of the packet. The packet is
			contained in 'pxDescriptor' that is passed to the function. */
			pxDmaTxDesc = pxNextTxDesc;
			/* pxDmaTransmit is just for debugging. */
			pxDmaTransmit = pxNextTxDesc;

			/* Is this buffer available? */
			configASSERT ( pxDmaTxDesc->own == 0 );

			{
				/* Is this buffer available? */
				/* Get bytes in current buffer. */
				ulTransmitSize = pxDescriptor->xDataLength;

				if( ulTransmitSize > ETH_TX_BUF_SIZE )
				{
					ulTransmitSize = ETH_TX_BUF_SIZE;
				}

				#if( ipconfigZERO_COPY_TX_DRIVER == 0 )
				{
					/* Copy the bytes. */
					memcpy( ( void * ) pxDmaTxDesc->buf1_address, pxDescriptor->pucEthernetBuffer, ulTransmitSize );
				}
				#else
				{
					/* Move the buffer. */
					pxDmaTxDesc->buf1_address = ( uint32_t )pxDescriptor->pucEthernetBuffer;
					/* The Network Buffer has been passed to DMA, no need to release it. */
					bReleaseAfterSend = pdFALSE;
				}
				#endif /* ipconfigZERO_COPY_TX_DRIVER */

				/* Ask to set the IPv4 checksum.
				Also need an Interrupt on Completion so that 'vClearTXBuffers()' will be called.. */
				pxDmaTxDesc->int_on_completion = 1;
				pxDmaTxDesc->checksum_mode = 0x03;

				/* Prepare transmit descriptors to give to DMA. */

				/* Set LAST and FIRST segment */
				pxDmaTxDesc->first_segment = 1;
				pxDmaTxDesc->last_segment = 1;

				/* Set frame size */
				pxDmaTxDesc->buf1_byte_count = ulTransmitSize;
				/* Set Own bit of the Tx descriptor Status: gives the buffer back to ETHERNET DMA */
				pxDmaTxDesc->own = 1;
//memcpy( desc_copy, pxDmaTxDesc, sizeof desc_copy );
				/* Point to next descriptor */
				pxNextTxDesc = ( gmac_tx_descriptor_t * ) ( pxNextTxDesc->next_descriptor );
				/* Ensure completion of memory access */
				__DSB();
alt_cache_l2_sync();

				gmac_dma_start_tx( iMacID, 0 );
				/* Resume DMA transmission*/
				gmac_dma_transmit_poll( iMacID );

				iptraceNETWORK_INTERFACE_TRANSMIT();
				xReturn = pdPASS;
				/* Temporary function, will be removed. */
				//vWaitTxDone();
			}
		}
		else
		{
			/* The PHY has no Link Status, packet shall be dropped. */
		}
	} while( 0 );
	/* The buffer has been sent so can be released. */
	if( bReleaseAfterSend != pdFALSE )
	{
		vReleaseNetworkBufferAndDescriptor( pxDescriptor );
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

static inline uint32_t ulReadMDIO( uint32_t ulRegister )
{
uint32_t ulValue = gmac_mdio_read( iMacID, ulUsePHYAddress, ulRegister);

	return ulValue;
}
/*-----------------------------------------------------------*/

static inline void ulWriteMDIO( uint32_t ulRegister, uint32_t ulValue )
{
	gmac_mdio_write( iMacID, ulUsePHYAddress, ulRegister, ( uint16_t ) ulValue );
}
/*-----------------------------------------------------------*/

static BaseType_t prvGMACWaitLS( TickType_t xMaxTime )
{
TickType_t xStartTime, xEndTime;
const TickType_t xShortDelay = pdMS_TO_TICKS( 20UL );
BaseType_t xReturn;

	xStartTime = xTaskGetTickCount();

	for( ;; )
	{
		xEndTime = xTaskGetTickCount();

		if( xEndTime - xStartTime > xMaxTime )
		{
			xReturn = pdFALSE;
			break;
		}
		ulPHYLinkStatus = ulReadMDIO( PHY_REG_01_BMSR );

		if( ( ulPHYLinkStatus & niBMSR_LINK_STATUS ) != 0 )
		{
			xReturn = pdTRUE;
			break;
		}

		vTaskDelay( xShortDelay );
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

#define NETWORK_BUFFER_SIZE	1536
void vNetworkInterfaceAllocateRAMToBuffers( NetworkBufferDescriptor_t pxNetworkBuffers[ ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS ] )
{
static uint8_t __attribute__ ( ( aligned( 32 ) ) ) __attribute__ ((section (".oc_ram")))
	ucNetworkPackets[ ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS * NETWORK_BUFFER_SIZE ] ;

uint8_t *ucRAMBuffer = ucNetworkPackets;
uint32_t ul;

	for( ul = 0; ul < ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS; ul++ )
	{
		pxNetworkBuffers[ ul ].pucEthernetBuffer = ucRAMBuffer + ipBUFFER_PADDING;
		*( ( unsigned * ) ucRAMBuffer ) = ( unsigned ) ( &( pxNetworkBuffers[ ul ] ) );
		ucRAMBuffer += NETWORK_BUFFER_SIZE;
	}
}
/*-----------------------------------------------------------*/

BaseType_t xGetPhyLinkStatus( void )
{
BaseType_t xReturn;

	if( ( ulPHYLinkStatus & niBMSR_LINK_STATUS ) == 0 )
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

static BaseType_t xMayAcceptPacket( uint8_t *pcBuffer )
{
const ProtocolPacket_t *pxProtPacket = ( const ProtocolPacket_t * )pcBuffer;

	switch( pxProtPacket->xTCPPacket.xEthernetHeader.usFrameType )
	{
	case ipARP_FRAME_TYPE:
		/* Check it later. */
		return pdTRUE;
	case ipIPv4_FRAME_TYPE:
		/* Check it here. */
		break;
	default:
		/* Refuse the packet. */
		return pdFALSE;
	}

	#if( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 1 )
	{
		const IPHeader_t *pxIPHeader = &(pxProtPacket->xTCPPacket.xIPHeader);
		uint32_t ulDestinationIPAddress;

		/* Ensure that the incoming packet is not fragmented (only outgoing packets
		 * can be fragmented) as these are the only handled IP frames currently. */
		if( ( pxIPHeader->usFragmentOffset & FreeRTOS_ntohs( ipFRAGMENT_OFFSET_BIT_MASK ) ) != 0U )
		{
			return pdFALSE;
		}
		/* HT: Might want to make the following configurable because
		 * most IP messages have a standard length of 20 bytes */

		/* 0x45 means: IPv4 with an IP header of 5 x 4 = 20 bytes
		 * 0x47 means: IPv4 with an IP header of 7 x 4 = 28 bytes */
		if( pxIPHeader->ucVersionHeaderLength < 0x45 || pxIPHeader->ucVersionHeaderLength > 0x4F )
		{
			return pdFALSE;
		}

		ulDestinationIPAddress = pxIPHeader->ulDestinationIPAddress;
		/* Is the packet for this node? */
		if( ( ulDestinationIPAddress != *ipLOCAL_IP_ADDRESS_POINTER ) &&
			/* Is it a broadcast address x.x.x.255 ? */
			( ( FreeRTOS_ntohl( ulDestinationIPAddress ) & 0xff ) != 0xff ) &&
		#if( ipconfigUSE_LLMNR == 1 )
			( ulDestinationIPAddress != ipLLMNR_IP_ADDR ) &&
		#endif
			( *ipLOCAL_IP_ADDRESS_POINTER != 0 ) ) {
			FreeRTOS_printf( ( "Drop IP %lxip\n", FreeRTOS_ntohl( ulDestinationIPAddress ) ) );
			return pdFALSE;
		}

		if( pxIPHeader->ucProtocol == ipPROTOCOL_UDP )
		{
			uint16_t port = pxProtPacket->xUDPPacket.xUDPHeader.usDestinationPort;

			if( ( xPortHasUDPSocket( port ) == pdFALSE )
			#if ipconfigUSE_LLMNR == 1
				&& ( port != FreeRTOS_ntohs( ipLLMNR_PORT ) )
			#endif
			#if ipconfigUSE_NBNS == 1
				&& ( port != FreeRTOS_ntohs( ipNBNS_PORT ) )
			#endif
			#if ipconfigUSE_DNS == 1
				&& ( pxProtPacket->xUDPPacket.xUDPHeader.usSourcePort != FreeRTOS_ntohs( ipDNS_PORT ) )
			#endif
				) {
				/* Drop this packet, not for this device. */
				return pdFALSE;
			}
		}
	}
	#endif	/* ipconfigETHERNET_DRIVER_FILTERS_PACKETS */
	return pdTRUE;
}
/*-----------------------------------------------------------*/

struct xFrameInfo
{
	gmac_rx_descriptor_t *FSRxDesc;          /*!< First Segment Rx Desc */
	gmac_rx_descriptor_t *LSRxDesc;          /*!< Last Segment Rx Desc */
	uint32_t  SegCount;                    /*!< Segment count */
	uint32_t length;                       /*!< Frame length */
	uint32_t buffer;                       /*!< Frame buffer */
};
typedef struct xFrameInfo FrameInfo_t;

static BaseType_t xGetReceivedFrame( FrameInfo_t *pxFrame );

static BaseType_t xGetReceivedFrame( FrameInfo_t *pxFrame )
{
uint32_t ulCounter = 0;
gmac_rx_descriptor_t *pxDescriptor = pxNextRxDesc;
BaseType_t xResult = -1;

	/* Scan descriptors owned by CPU */
	while( ( pxDescriptor->own == 0u ) && ( ulCounter < GMAC_RX_BUFFERS ) )
	{
		/* Just for security. */
		ulCounter++;

		if( pxDescriptor->last_descriptor == 0u )
		{
			if( pxDescriptor->first_descriptor != 0u )
			{
				/* First segment in frame, but not the last. */
				pxFrame->FSRxDesc = pxDescriptor;
				pxFrame->LSRxDesc = ( gmac_rx_descriptor_t *)NULL;
				pxFrame->SegCount = 1;
			}
			else
			{
				/* This is an intermediate segment, not first, not last. */
				/* Increment segment count. */
				pxFrame->SegCount++;
			}
			/* Point to next descriptor. */
			pxDescriptor = (gmac_rx_descriptor_t*) (pxDescriptor->next_descriptor);
			pxNextRxDesc = pxDescriptor;
		}
		/* Must be a last segment */
		else
		{
			if( pxDescriptor->first_descriptor != 0u )
			{
				pxFrame->SegCount = 0;
				/* Remember the first segment. */
				pxFrame->FSRxDesc = pxDescriptor;
			}

			/* Increment segment count */
			pxFrame->SegCount++;

			/* Remember the last segment. */
			pxFrame->LSRxDesc = pxDescriptor;

			/* Get the Frame Length of the received packet: substruct 4 bytes of the CRC */
			pxFrame->length = pxDescriptor->buf1_byte_count;

			/* Get the address of the buffer start address */
			pxFrame->buffer = pxDescriptor->buf1_address;

			/* Point to next descriptor */
			pxNextRxDesc = ( gmac_rx_descriptor_t * ) pxDescriptor->next_descriptor;

			/* Return OK status: a packet was received. */
			xResult = pxFrame->length;
			break;
		}
	}

	/* Return function status */
	return xResult;
}

int gmac_check_rx()
{
FrameInfo_t frameInfo;
BaseType_t xReceivedLength, xAccepted;
xIPStackEvent_t xRxEvent = { eNetworkRxEvent, NULL };
const TickType_t xDescriptorWaitTime = pdMS_TO_TICKS( 250 );
uint8_t *pucBuffer;
gmac_rx_descriptor_t *pxRxDescriptor;
NetworkBufferDescriptor_t *pxCurNetworkBuffer;
NetworkBufferDescriptor_t *pxNewNetworkBuffer = NULL;

	xReceivedLength = xGetReceivedFrame( &frameInfo );

	if( xReceivedLength > 0 )
	{
		pucBuffer = ( uint8_t * )frameInfo.buffer;

		/* Update the ETHERNET DMA global Rx descriptor with next Rx descriptor */
		/* Chained Mode */    
		/* Selects the next DMA Rx descriptor list for next buffer to read */ 
		pxRxDescriptor = ( gmac_rx_descriptor_t* )frameInfo.FSRxDesc;

		/* In order to make the code easier and faster, only packets in a single buffer
		will be accepted.  This can be done by making the buffers large enough to
		hold a complete Ethernet packet (1536 bytes).
		Therefore, two sanity checks: */
		configASSERT( xReceivedLength <= ETH_RX_BUF_SIZE );

		if( pxRxDescriptor->error_summary != 0u )
		{
			/* Not an Ethernet frame-type or a checmsum error. */
			xAccepted = pdFALSE;
		}
		else
		{
			/* See if this packet must be handled. */
			xAccepted = xMayAcceptPacket( pucBuffer );
		}

		if( xAccepted != pdFALSE )
		{
			/* The packet wil be accepted, but check first if a new Network Buffer can
			be obtained. If not, the packet will still be dropped. */
			pxNewNetworkBuffer = pxGetNetworkBufferWithDescriptor( ETH_RX_BUF_SIZE, xDescriptorWaitTime );

			if( pxNewNetworkBuffer == NULL )
			{
				/* A new descriptor can not be allocated now. This packet will be dropped. */
				xAccepted = pdFALSE;
			}
		}
		#if( ipconfigZERO_COPY_RX_DRIVER != 0 )
		{
			/* Find out which Network Buffer was originally passed to the descriptor. */
			pxCurNetworkBuffer = pxPacketBuffer_to_NetworkBuffer( pucBuffer );
			configASSERT( pxCurNetworkBuffer != NULL );
		}
		#else
		{
			/* In this mode, the two descriptors are the same. */
			pxCurNetworkBuffer = pxNewNetworkBuffer;
			if( pxNewNetworkBuffer != NULL )
			{
				/* The packet is acepted and a new Network Buffer was created,
				copy data to the Network Bufffer. */
				memcpy( pxNewNetworkBuffer->pucEthernetBuffer, pucBuffer, xReceivedLength );
			}
		}
		#endif

		if( xAccepted != pdFALSE )
		{
			pxCurNetworkBuffer->xDataLength = xReceivedLength;
			xRxEvent.pvData = ( void * ) pxCurNetworkBuffer;

			/* Pass the data to the TCP/IP task for processing. */
			if( xSendEventStructToIPTask( &xRxEvent, xDescriptorWaitTime ) == pdFALSE )
			{
				/* Could not send the descriptor into the TCP/IP stack, it
				must be released. */
				vReleaseNetworkBufferAndDescriptor( pxCurNetworkBuffer );
				iptraceETHERNET_RX_EVENT_LOST();
			}
			else
			{
				iptraceNETWORK_INTERFACE_RECEIVE();
			}
		}

		/* Set Buffer1 address pointer */
		if( pxNewNetworkBuffer != NULL )
		{
			pxRxDescriptor->buf1_address = (uint32_t)pxNewNetworkBuffer->pucEthernetBuffer;
		}
		else
		{
			/* The packet was dropped and the same Network
			Buffer will be used to receive a new packet. */
		}

		pxRxDescriptor->desc0 = 0u;
		/* Set Buffer1 size and Second Address Chained bit */
		pxRxDescriptor->buf1_byte_count = ETH_RX_BUF_SIZE;
		pxRxDescriptor->second_address_chained = pdTRUE;
		pxRxDescriptor->own = pdTRUE;

		/* Ensure completion of memory access */
		__DSB();

		/* Resume DMA reception. */
		gmac_dma_start_rx( iMacID, 0 );

//		/* When Rx Buffer unavailable flag is set clear it and resume
//		reception. */
//		if( ( xETH.Instance->DMASR & ETH_DMASR_RBUS ) != 0 )
//		{
//			/* Clear RBUS ETHERNET DMA flag. */
//			xETH.Instance->DMASR = ETH_DMASR_RBUS;
//
//			/* Resume DMA reception. */
//			xETH.Instance->DMARPDR = 0;
//		}
	}

	return ( xReceivedLength > 0 );
}
/*-----------------------------------------------------------*/

static void prvEMACHandlerTask( void *pvParameters )
{
UBaseType_t uxCurrentCount;
const TickType_t ulMaxBlockTime = pdMS_TO_TICKS( 500UL );
UBaseType_t uxLastMinBufferCount = 0;
UBaseType_t uxCurrentBufferCount = 0;
UBaseType_t uxLowestSemCount = ( UBaseType_t ) GMAC_TX_BUFFERS - 1;
UBaseType_t uxCurrentSemCount = 0;
uint32_t ulStatus;

	/* Remove compiler warnings about unused parameters. */
	( void ) pvParameters;

	/* A possibility to set some additional task properties like calling
	portTASK_USES_FLOATING_POINT() */
	iptraceEMAC_TASK_STARTING();

	for( ;; )
	{
		uxCurrentBufferCount = uxGetMinimumFreeNetworkBuffers();
		if( uxLastMinBufferCount != uxCurrentBufferCount )
		{
			/* The logging produced below may be helpful
			while tuning +TCP: see how many buffers are in use. */
			uxLastMinBufferCount = uxCurrentBufferCount;
			FreeRTOS_printf( ( "Network buffers: %lu lowest %lu\n",
				uxGetNumberOfFreeNetworkBuffers(), uxCurrentBufferCount ) );
		}

		if( xTXDescriptorSemaphore != NULL )
		{
			uxCurrentSemCount = uxSemaphoreGetCount( xTXDescriptorSemaphore );
			if( uxLowestSemCount > uxCurrentSemCount )
			{
				uxLowestSemCount = uxCurrentSemCount;
				FreeRTOS_printf( ( "TX DMA buffers: lowest %lu\n", uxLowestSemCount ) );
			}

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

		if( ( ulISREvents & EMAC_IF_ALL_EVENT ) == 0 )
		{
			/* No events to process now, wait for the next. */
			ulTaskNotifyTake( pdFALSE, ulMaxBlockTime );
		}

		if( ( ulISREvents & EMAC_IF_RX_EVENT ) != 0 )
		{
			ulISREvents &= ~EMAC_IF_RX_EVENT;
			while ( gmac_check_rx() > 0 ) {}
		}

		if( ( ulISREvents & EMAC_IF_TX_EVENT ) != 0 )
		{
			/* Code to release TX buffers if zero-copy is used. */
			ulISREvents &= ~EMAC_IF_TX_EVENT;
			/* Check if DMA packets have been delivered. */
			vClearTXBuffers();
		}

		if( ( ulISREvents & EMAC_IF_ERR_EVENT ) != 0 )
		{
			ulISREvents &= ~EMAC_IF_ERR_EVENT;
			gmac_check_errors( &xEMACif );
		}

		ulStatus = ulReadMDIO( PHY_REG_01_BMSR );

		if( ulPHYLinkStatus != ulStatus )
		{
		volatile BaseType_t xStartNego = 0;

			/* Test if the Link Status has become high. */
			if( ( ( ulPHYLinkStatus ^ ulStatus ) & niBMSR_LINK_STATUS ) && ( ( ulStatus & niBMSR_LINK_STATUS ) != 0 ) )
			{
				xStartNego = pdTRUE;
			}

			if( ulPHYLinkStatus != ulStatus )
			{
				ulPHYLinkStatus = ulStatus;
				FreeRTOS_printf( ( "prvEMACHandlerTask: PHY LS now %d Start nego %d\n", ( ulPHYLinkStatus & niBMSR_LINK_STATUS ) != 0, xStartNego ) );
				if( xStartNego )
				{
				uint32_t ulLinkSpeed;
					ulLinkSpeed = Phy_Setup( &xEMACif );
					if( ulLinkSpeed )
					{
						//
					}

					/* Setting the operating speed of the MAC needs a delay. */
					vTaskDelay( pdMS_TO_TICKS( 25UL ) );
				}
			}
		}
	}
}
/*-----------------------------------------------------------*/
