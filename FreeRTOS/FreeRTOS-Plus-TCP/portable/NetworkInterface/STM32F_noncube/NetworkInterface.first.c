/*
 * Some constants, hardware definitions and comments taken from ST's Standard
 * Peripheral Driver, COPYRIGHT(c) 2015 STMicroelectronics.
 */

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

/* ST includes. */
#include "stm32f4x7_eth.h"

#ifndef	BMSR_LINK_STATUS
	#define BMSR_LINK_STATUS            0x0004UL
#endif

#ifndef	PHY_LS_HIGH_CHECK_TIME_MS
	/* Check if the LinkSStatus in the PHY is still high after 15 seconds of not
	receiving packets. */
	#define PHY_LS_HIGH_CHECK_TIME_MS	15000
#endif

#ifndef	PHY_LS_LOW_CHECK_TIME_MS
	/* Check if the LinkSStatus in the PHY is still low every second. */
	#define PHY_LS_LOW_CHECK_TIME_MS	1000
#endif

/* Interrupt events to process.  Currently only the Rx event is processed
although code for other events is included to allow for possible future
expansion. */
#define EMAC_IF_RX_EVENT        1UL
#define EMAC_IF_TX_EVENT        2UL
#define EMAC_IF_ERR_EVENT       4UL
#define EMAC_IF_ALL_EVENT       ( EMAC_IF_RX_EVENT | EMAC_IF_TX_EVENT | EMAC_IF_ERR_EVENT )

#define ETH_DMA_ALL_INTS \
	( ETH_DMA_IT_TST | ETH_DMA_IT_PMT | ETH_DMA_IT_MMC | ETH_DMA_IT_NIS | ETH_DMA_IT_AIS | ETH_DMA_IT_ER | \
	  ETH_DMA_IT_FBE | ETH_DMA_IT_ET | ETH_DMA_IT_RWT | ETH_DMA_IT_RPS | ETH_DMA_IT_RBU | ETH_DMA_IT_R | \
	  ETH_DMA_IT_TU | ETH_DMA_IT_RO | ETH_DMA_IT_TJT | ETH_DMA_IT_TPS | ETH_DMA_IT_T )

/* Naming and numbering of PHY registers. */
#define PHY_REG_00_BMCR			0x00	/* Basic mode control register */
#define PHY_REG_01_BMSR			0x01	/* Basic mode status register */
#define PHY_REG_02_PHYSID1		0x02	/* PHYS ID 1 */
#define PHY_REG_03_PHYSID2		0x03	/* PHYS ID 2 */
#define PHY_REG_04_ADVERTISE	0x04	/* Advertisement control reg */
#define PHY_REG_05_PART_ABILITY	0x05	/* Auto-Negotiation Link Partner Ability Register (Base Page) */
#define PHY_REG_10_PHY_SR		0x10	/* PHY status register Offset */
#define	PHY_REG_19_PHYCR		0x19	/* 25 RW PHY Control Register */

#define	PHY_SR_LINK_STATUS		0x01

/* Some defines used internally here to indicate preferences about speed, MDIX
(wired direct or crossed), and duplex (half or full). */
#define	PHY_SPEED_10       1
#define	PHY_SPEED_100      2
#define	PHY_SPEED_AUTO     (PHY_SPEED_10|PHY_SPEED_100)

#define	PHY_MDIX_DIRECT    1
#define	PHY_MDIX_CROSSED   2
#define	PHY_MDIX_AUTO      (PHY_MDIX_CROSSED|PHY_MDIX_DIRECT)

#define	PHY_DUPLEX_HALF    1
#define	PHY_DUPLEX_FULL    2
#define	PHY_DUPLEX_AUTO    (PHY_DUPLEX_FULL|PHY_DUPLEX_HALF)

#define PHY_AUTONEGO_COMPLETE    ((uint16_t)0x0020)  /*!< Auto-Negotiation process completed   */

/*
 * Description of all capabilities that can be advertised to
 * the peer (usually a switch or router).
 */
#define ADVERTISE_CSMA			0x0001		// Only selector supported
#define ADVERTISE_10HALF		0x0020		// Try for 10mbps half-duplex
#define ADVERTISE_10FULL		0x0040		// Try for 10mbps full-duplex
#define ADVERTISE_100HALF		0x0080		// Try for 100mbps half-duplex
#define ADVERTISE_100FULL		0x0100		// Try for 100mbps full-duplex

#define ADVERTISE_ALL			( ADVERTISE_10HALF | ADVERTISE_10FULL | \
								  ADVERTISE_100HALF | ADVERTISE_100FULL)

/*
 * Value for the 'PHY_REG_00_BMCR', the PHY's Basic mode control register
 */
#define BMCR_FULLDPLX			0x0100		// Full duplex
#define BMCR_ANRESTART			0x0200		// Auto negotiation restart
#define BMCR_ANENABLE			0x1000		// Enable auto negotiation
#define BMCR_SPEED100			0x2000		// Select 100Mbps
#define BMCR_RESET				0x8000		// Reset the PHY

#define PHYCR_MDIX_EN			0x8000		// Enable Auto MDIX
#define PHYCR_MDIX_FORCE		0x4000		// Force MDIX crossed

/*
 * Most users will want a PHY that negotiates about
 * the connection properties: speed, dmix and duplex.
 * On some rare cases, you want to select what is being
 * advertised, properties like MDIX and duplex.
 */

#if !defined( ipconfigETHERNET_AN_ENABLE )
	/* Enable auto-negotiation */
	#define ipconfigETHERNET_AN_ENABLE				1
#endif

#if !defined( ipconfigETHERNET_AUTO_CROSS_ENABLE )
	#define ipconfigETHERNET_AUTO_CROSS_ENABLE		1
#endif

#if( ipconfigETHERNET_AN_ENABLE == 0 )
	/*
	 * The following three defines are only used in case there
	 * is no auto-negotiation.
	 */
	#if !defined( ipconfigETHERNET_CROSSED_LINK )
		#define	ipconfigETHERNET_CROSSED_LINK			1
	#endif

	#if !defined( ipconfigETHERNET_USE_100MB )
		#define ipconfigETHERNET_USE_100MB				1
	#endif

	#if !defined( ipconfigETHERNET_USE_FULL_DUPLEX )
		#define ipconfigETHERNET_USE_FULL_DUPLEX		1
	#endif
#endif /* ipconfigETHERNET_AN_ENABLE == 0 */

/* Default the size of the stack used by the EMAC deferred handler task to twice
the size of the stack used by the idle task - but allow this to be overridden in
FreeRTOSConfig.h as configMINIMAL_STACK_SIZE is a user definable constant. */
#ifndef configEMAC_TASK_STACK_SIZE
	#define configEMAC_TASK_STACK_SIZE ( 2 * configMINIMAL_STACK_SIZE )
#endif

#if ETH_TX_BUF_SIZE != ETH_MAX_PACKET_SIZE || ETH_MAX_PACKET_SIZE < 1536
	#error Not bigger ?
#endif

/*-----------------------------------------------------------*/

/*
 * A deferred interrupt handler task that processes
 */
static void prvEMACHandlerTask( void *pvParameters );

/*
 * Force a negotiation with the Switch or Router and wait for LS.
 */
static void prvEthernetUpdateConfig( BaseType_t xForce );

/*
 * See if there is a new packet and forward it to the IP-task.
 */
static BaseType_t prvNetworkInterfaceInput( void );

#if( ipconfigZERO_COPY_RX_DRIVER != 0 )
	static void vSetRXBuffers( void );
#endif /* ipconfigZERO_COPY_RX_DRIVER */

#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
	static void vClearTXBuffers( void );
#endif /* ipconfigZERO_COPY_TX_DRIVER */
	

/*-----------------------------------------------------------*/

typedef struct _PhyProperties_t
{
	uint8_t speed;
	uint8_t mdix;
	uint8_t duplex;
	uint8_t spare;
} PhyProperties_t;

/* Bit map of outstanding ETH interrupt events for processing.  Currently only
the Rx interrupt is handled, although code is included for other events to
enable future expansion. */
static volatile uint32_t ulISREvents;

/* A copy of PHY register 1: 'PHY_REG_01_BMSR' */
static uint32_t ulPHYLinkStatus = 0;

#if( ipconfigUSE_LLMNR == 1 )
	static const uint8_t xLLMNR_MACAddress[] = { 0x01, 0x00, 0x5E, 0x00, 0x00, 0xFC };
#endif

/* Ethernet handle. */
ETH_InitTypeDef ETH_InitStructure;
__IO uint32_t  EthStatus = 0;
#define DP83848_PHY_ADDRESS       0x01 

static BaseType_t xFirstTransmit;

/* Ethernet Rx & Tx DMA Descriptors */
extern ETH_DMADESCTypeDef  DMARxDscrTab[ETH_RXBUFNB], DMATxDscrTab[ETH_TXBUFNB];

#if( ipconfigZERO_COPY_RX_DRIVER == 0 )
	/* Ethernet Driver Receive buffers  */
	extern uint8_t Rx_Buff[ETH_RXBUFNB][ETH_RX_BUF_SIZE]; 
#endif

#if( ipconfigZERO_COPY_TX_DRIVER == 0 )
	/* Ethernet Driver Transmit buffers */
	extern uint8_t Tx_Buff[ETH_TXBUFNB][ETH_TX_BUF_SIZE]; 
#endif

/* Global pointers to track current transmit and receive descriptors */
extern __IO ETH_DMADESCTypeDef  *DMATxDescToSet;
extern __IO ETH_DMADESCTypeDef  *DMARxDescToGet;

#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
	static __IO ETH_DMADESCTypeDef  *DMATxDescToClear;
#endif

/* Global pointer for last received frame infos */
extern ETH_DMA_Rx_Frame_infos *DMA_RX_FRAME_infos;

/* Value to be written into the 'Basic mode Control Register'. */
static uint32_t ulBCRvalue;

/* Value to be written into the 'Advertisement Control Register'. */
static uint32_t ulACRValue;

/* ucMACAddress as it appears in main.c */
extern uint8_t ucMACAddress[ 6 ];

/* Holds the handle of the task used as a deferred interrupt processor.  The
handle is used so direct notifications can be sent to the task for all EMAC/DMA
related interrupts. */
static TaskHandle_t xEMACTaskHandle = NULL;

/* For local use only: describe the PHY's properties: */
const PhyProperties_t xPHYProperties =
{
	#if( ipconfigETHERNET_AN_ENABLE != 0 )
		.speed = PHY_SPEED_AUTO,
		.duplex = PHY_DUPLEX_AUTO,
	#else
		#if( ipconfigETHERNET_USE_100MB != 0 )
			.speed = PHY_SPEED_100,
		#else
			.speed = PHY_SPEED_10,
		#endif

		#if( ipconfigETHERNET_USE_FULL_DUPLEX != 0 )
			.duplex = PHY_DUPLEX_FULL,
		#else
			.duplex = PHY_DUPLEX_HALF,
		#endif
	#endif

	#if( ipconfigETHERNET_AN_ENABLE != 0 ) && ( ipconfigETHERNET_AUTO_CROSS_ENABLE != 0 )
		.mdix = PHY_MDIX_AUTO,
	#elif( ipconfigETHERNET_CROSSED_LINK != 0 )
		.mdix = PHY_MDIX_CROSSED,
	#else
		.mdix = PHY_MDIX_DIRECT,
	#endif
};

/*-----------------------------------------------------------*/

/**
  * @brief  This function handles ethernet DMA interrupt request.
  * @param  None
  * @retval None
  */
void ETH_IRQHandler(void)
{
portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
uint32_t ulDMASR = ETH->DMASR & ETH_DMA_ALL_INTS;

	/* Frame received */
	if( ( ulDMASR & ETH_DMA_IT_R ) != 0 )
	{
		ulISREvents |= EMAC_IF_RX_EVENT;
		/* Give the semaphore to wakeup prvEMACHandlerTask. */
		if( xEMACTaskHandle != NULL )
		{
			vTaskNotifyGiveFromISR( xEMACTaskHandle, &xHigherPriorityTaskWoken );
		}
	}

	/* Frame sent */
	if( ( ulDMASR & ETH_DMA_IT_T ) != 0 )
	{
		ulISREvents |= EMAC_IF_TX_EVENT;
		/* Give the semaphore to wakeup prvEMACHandlerTask. */
		if( xEMACTaskHandle != NULL )
		{
			vTaskNotifyGiveFromISR( xEMACTaskHandle, &xHigherPriorityTaskWoken );
		}
	}

	/* Clear all interrupt flags. */
	ETH->DMASR = ulDMASR;

	/* Switch tasks if necessary. */	
	if( xHigherPriorityTaskWoken != pdFALSE )
	{
		portEND_SWITCHING_ISR( xHigherPriorityTaskWoken );
	}
}
/*-----------------------------------------------------------*/

#if( ipconfigZERO_COPY_RX_DRIVER != 0 )
	static void vSetRXBuffers()
	{
	BaseType_t xIndex;
	ETH_DMADESCTypeDef *xDescriptor = DMARxDscrTab;
	ETH_DMADESCTypeDef *xLastDescriptor = DMARxDscrTab + ETH_RXBUFNB;

		/* Fill each xDescriptor descriptor with the right values */
		for( xIndex = 0; xDescriptor < xLastDescriptor; xIndex++, xDescriptor++ )
		{
			/* Set Buffer1 size and Second Address Chained bit */
			xDescriptor->ControlBufferSize = ETH_DMARxDesc_RCH | (uint32_t)ETH_RX_BUF_SIZE;  

			/* Set Buffer1 address pointer */
			NetworkBufferDescriptor_t *pxBuffer = pxGetNetworkBufferWithDescriptor( ETH_RX_BUF_SIZE, 100ul );
			if( pxBuffer == NULL )
			{
				break;
			}
			else
			{
				xDescriptor->Buffer1Addr = (uint32_t)pxBuffer->pucEthernetBuffer;
				xDescriptor->Status |= ETH_DMARxDesc_OWN;
			}
		}
	}
#endif /* ipconfigZERO_COPY_RX_DRIVER */
/*-----------------------------------------------------------*/

#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
	static void vClearTXBuffers()
	{
	__IO ETH_DMADESCTypeDef  *txLastDescriptor = DMATxDescToSet;
	xNetworkBufferDescriptor_t *pxNetworkBuffer;

		while( ( DMATxDescToClear->Status & ETH_DMATxDesc_OWN ) == 0 )
		{
			if( DMATxDescToClear == txLastDescriptor )
			{
				break;
			}

			if( DMATxDescToClear->Buffer1Addr )
			{
			uint8_t *ucPayLoad;

				ucPayLoad = ( uint8_t * )DMATxDescToClear->Buffer1Addr;
				ucPayLoad += sizeof( UDPPacket_t );
				pxNetworkBuffer = pxUDPPayloadBuffer_to_NetworkBuffer( ucPayLoad );
				if( pxNetworkBuffer != NULL )
				{
					vReleaseNetworkBufferAndDescriptor( pxNetworkBuffer ) ;
				}
				DMATxDescToClear->Buffer1Addr = ( uint32_t )0u;
			}
			DMATxDescToClear = ( ETH_DMADESCTypeDef * )( DMATxDescToClear->Buffer2NextDescAddr );
		}
	}
#endif /* ipconfigZERO_COPY_TX_DRIVER */
	

BaseType_t xNetworkInterfaceInitialise( void )
{
BaseType_t xResult;
	if( xEMACTaskHandle == NULL )
	{
		/* configure ethernet (GPIOs, clocks, MAC, DMA) */ 
		ETH_BSP_Config();

		#if( ipconfigZERO_COPY_TX_DRIVER == 0 )
		{
			/* Initialize Tx Descriptors list: Chain Mode */
			ETH_DMATxDescChainInit(DMATxDscrTab, Tx_Buff[0], ETH_TXBUFNB);
		}
		#else
		{
			/* Initialize Tx Descriptors list: Chain Mode */
			ETH_DMATxDescChainInit(DMATxDscrTab, NULL, ETH_TXBUFNB);
			DMATxDescToClear = DMATxDscrTab;
		}
		#endif /* ipconfigZERO_COPY_TX_DRIVER */

		#if( ipconfigZERO_COPY_RX_DRIVER == 0 )
		{
			/* Initialize Rx Descriptors list: Chain Mode  */
			ETH_DMARxDescChainInit(DMARxDscrTab, Rx_Buff[0], ETH_RXBUFNB);
		}
		#else
		{
			/* Initialize Rx Descriptors list: Chain Mode  */
			ETH_DMARxDescChainInit(DMARxDscrTab, NULL, ETH_RXBUFNB);
			/* Created enough Network Buffers. */
			vSetRXBuffers();
		}
		#endif /* ipconfigZERO_COPY_RX_DRIVER */
		/* Enable Ethernet Rx interrrupt */
		{
		BaseType_t xIndex;

			for( xIndex=0; xIndex<ETH_RXBUFNB; xIndex++ )
			{
				ETH_DMARxDescReceiveITConfig(&DMARxDscrTab[xIndex], ENABLE);
			}
		}

		#ifdef CHECKSUM_BY_HARDWARE
		/* Enable the checksum insertion for the Tx frames */
		{
		BaseType_t xIndex;

			for( xIndex=0; xIndex<ETH_TXBUFNB; xIndex++ )
			{
				ETH_DMATxDescChecksumInsertionConfig(&DMATxDscrTab[xIndex], ETH_DMATxDesc_ChecksumTCPUDPICMPFull);
			}
		} 
		#endif

		/* Force a negotiation with the Switch or Router and wait for LS. */
		prvEthernetUpdateConfig( pdTRUE );

		/* The deferred interrupt handler task is created at the highest
		possible priority to ensure the interrupt handler can return directly
		to it.  The task's handle is stored in xEMACTaskHandle so interrupts can
		notify the task when there is something to process. */
		xTaskCreate( prvEMACHandlerTask, "EMAC", configEMAC_TASK_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, &xEMACTaskHandle );
	}
	if( ( ulPHYLinkStatus & BMSR_LINK_STATUS ) != 0 )
	{
		//ETH_DMAITConfig( ETH_DMA_ALL_INTS, ENABLE );
	    ETH->DMAIER |= ETH_DMA_ALL_INTS;
		xResult = pdPASS;
		xFirstTransmit = pdTRUE;
	}
	else
	{
		xResult = pdFAIL;
	}
	/* When returning non-zero, the stack will become active and
    start DHCP (in configured) */
	return xResult;
}
/*-----------------------------------------------------------*/

BaseType_t xNetworkInterfaceOutput( xNetworkBufferDescriptor_t * const pxDescriptor, BaseType_t bReleaseAfterSend )
{
BaseType_t xReturn;
uint32_t ulTransmitSize = 0;
__IO ETH_DMADESCTypeDef *pxDmaTxDesc;

	#if( ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM != 0 )
	{
	ProtocolPacket_t *pxPacket;

		/* If the peripheral must calculate the checksum, it wants
		the protocol checksum to have a value of zero. */
		pxPacket = ( ProtocolPacket_t * ) ( pxDescriptor->pucEthernetBuffer );

		if( pxPacket->xICMPPacket.xIPHeader.ucProtocol == ipPROTOCOL_ICMP )
		{
			pxPacket->xICMPPacket.xICMPHeader.usChecksum = ( uint16_t )0u;
		}
	}
	#endif

	if( ( ulPHYLinkStatus & BMSR_LINK_STATUS ) != 0 )
	{
		/* This function does the actual transmission of the packet. The packet is
		contained in 'pxDescriptor' that is passed to the function. */
		pxDmaTxDesc = DMATxDescToSet;

		/* Is this buffer available? */
		if( ( pxDmaTxDesc->Status & ETH_DMATxDesc_OWN ) != 0 )
		{
			xReturn = pdFAIL;
		}
		else
		{
			/* Get bytes in current buffer. */
			ulTransmitSize = pxDescriptor->xDataLength;

			if( ulTransmitSize > ETH_TX_BUF_SIZE )
			{
				ulTransmitSize = ETH_TX_BUF_SIZE;
			}

			#if( ipconfigZERO_COPY_TX_DRIVER == 0 )
			{
				/* Copy the bytes. */
				memcpy( ( void * ) pxDmaTxDesc->Buffer1Addr, pxDescriptor->pucEthernetBuffer, ulTransmitSize );
				pxDmaTxDesc->Status |= ETH_DMATxDesc_CIC_TCPUDPICMP_Full;
			}
			#else
			{
				/* Move the buffer. */
				pxDmaTxDesc->Buffer1Addr = ( uint32_t )pxDescriptor->pucEthernetBuffer;
				/* Ask to set the IPv4 checksum.
				Also need an Interrupt on Completion so that 'vClearTXBuffers()' will be called.. */
				pxDmaTxDesc->Status |= ETH_DMATxDesc_CIC_TCPUDPICMP_Full | ETH_DMATxDesc_IC;
				/* The Network Buffer has been passed to DMA, no need to release it. */
				bReleaseAfterSend = pdFALSE_UNSIGNED;
			}
			#endif /* ipconfigZERO_COPY_TX_DRIVER */


			/* Prepare transmit descriptors to give to DMA. */
			ETH_Prepare_Transmit_Descriptors( ulTransmitSize );
			if( xFirstTransmit != 0 )
			{
				xFirstTransmit = 0;
				/* Resume DMA transmission*/
				ETH->DMATPDR = 0;
			}
			iptraceNETWORK_INTERFACE_TRANSMIT();
			xReturn = pdPASS;
		}
	}
	else
	{
		/* The PHY has no Link Status, packet shall be dropped. */
		xReturn = pdFAIL;
	}

	/* The buffer has been sent so can be released. */
	if( bReleaseAfterSend != pdFALSE )
	{
		vReleaseNetworkBufferAndDescriptor( pxDescriptor );
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

static BaseType_t prvNetworkInterfaceInput( void )
{
xNetworkBufferDescriptor_t *pxDescriptor;
BaseType_t xReceivedLength, xIndex, xBytesLeft;
__IO ETH_DMADESCTypeDef *pxDMARxDescriptor;
uint32_t ulSegCount;
xIPStackEvent_t xRxEvent = { eNetworkRxEvent, NULL };
const TickType_t xDescriptorWaitTime = pdMS_TO_TICKS( 250 );
FrameTypeDef frame;
uint8_t *buffer, *pucTarget;


	/* get received frame */
	frame = ETH_Get_Received_Frame_interrupt();

	/* Obtain the size of the packet and put it into the "usReceivedLength" variable. */
	xReceivedLength = frame.length;

	/* get received frame */
	if( xReceivedLength > 0ul )
	{
		ulSegCount = DMA_RX_FRAME_infos->Seg_Count;
		buffer = (u8 *)frame.buffer;
		#if( ipconfigZERO_COPY_RX_DRIVER == 0 )
		{
			/* Create a buffer of the required length. */
			pxDescriptor = pxGetNetworkBufferWithDescriptor( xReceivedLength, xDescriptorWaitTime );
		}
		#else
		{
		uint8_t *ucPayLoad = buffer;
			ucPayLoad += sizeof( UDPPacket_t );
			pxDescriptor = pxUDPPayloadBuffer_to_NetworkBuffer( ucPayLoad );
		}
		#endif
		if( pxDescriptor != NULL )
		{
		xBytesLeft = xReceivedLength;
		pxDMARxDescriptor = frame.descriptor;
		pucTarget = pxDescriptor->pucEthernetBuffer;

			/* Check if the length of bytes to copy in current pbuf is bigger than Rx buffer size*/
			for( xIndex = 0; xIndex < ulSegCount; xIndex++ )
			{
				int xCopyLength = ETH_RX_BUF_SIZE;
				if( xCopyLength > xBytesLeft )
				{
					xCopyLength = xBytesLeft;
				}
				xBytesLeft -= xCopyLength;
 
				#if( ipconfigZERO_COPY_RX_DRIVER == 0 )
				{
					/* Copy data to pbuf*/
					memcpy( pucTarget, buffer, xCopyLength );
				}
				#endif

				if( xBytesLeft == 0 )
				{
					break;
				}

				/* Point to next descriptor */
				pucTarget += xCopyLength;
				pxDMARxDescriptor = ( ETH_DMADESCTypeDef * )( pxDMARxDescriptor->Buffer2NextDescAddr );
				buffer = (unsigned char *)( pxDMARxDescriptor->Buffer1Addr );
			}
			xRxEvent.pvData = ( void * ) pxDescriptor;

			/* Pass the data to the TCP/IP task for processing. */
			if( xSendEventStructToIPTask( &xRxEvent, xDescriptorWaitTime ) == pdFALSE )
			{
				/* Could not send the descriptor into the TCP/IP stack, it
				must be released. */
				vReleaseNetworkBufferAndDescriptor( pxDescriptor );
				iptraceETHERNET_RX_EVENT_LOST();
			}
			else
			{
				iptraceNETWORK_INTERFACE_RECEIVE();
			}
		}
		else
		{
			FreeRTOS_printf( ( "prvNetworkInterfaceInput: pxGetNetworkBuffer failed length %u\n", xReceivedLength ) );
		}
		/* Release descriptors to DMA */
		pxDMARxDescriptor = frame.descriptor;

		/* Set Own bit in Rx descriptors: gives the buffers back to DMA */
		for (xIndex=0; xIndex < ulSegCount; xIndex++)
		{  
			#if( ipconfigZERO_COPY_RX_DRIVER == 0 )
			{
				pxDMARxDescriptor->Status = ETH_DMARxDesc_OWN;
			}
			#else
			{
				/* Set Buffer1 size and Second Address Chained bit */
				pxDMARxDescriptor->ControlBufferSize = ETH_DMARxDesc_RCH | (uint32_t)ETH_RX_BUF_SIZE;  

				/* Set Buffer1 address pointer */
				NetworkBufferDescriptor_t *pxBuffer = pxGetNetworkBufferWithDescriptor( ETH_RX_BUF_SIZE, 100ul );
				if( pxBuffer != NULL )
				{
					pxDMARxDescriptor->Buffer1Addr = (uint32_t)pxBuffer->pucEthernetBuffer;
					pxDMARxDescriptor->Status = ETH_DMARxDesc_OWN;
				}
			}
			#endif /* ipconfigZERO_COPY_RX_DRIVER */
			pxDMARxDescriptor = (ETH_DMADESCTypeDef *)(pxDMARxDescriptor->Buffer2NextDescAddr);
		}

		/* Clear Segment_Count */
		DMA_RX_FRAME_infos->Seg_Count = 0;

		/* When Rx Buffer unavailable flag is set clear it and resume
		reception. */
		if( ( ETH->DMASR & ETH_DMASR_RBUS ) != 0 )
		{
			/* Clear RBUS ETHERNET DMA flag. */
			ETH->DMASR = ETH_DMASR_RBUS;

			/* Resume DMA reception. */
			ETH->DMARPDR = 0;
		}
	}

	return ( xReceivedLength > 0 );
}
/*-----------------------------------------------------------*/

void vMACBProbePhy( void )
{
uint32_t ulPhyControl, ulConfig, ulAdvertise, ulLower, ulUpper, ulMACPhyID, ulValue;
TimeOut_t xPhyTime;
TickType_t xRemTime = 0;

	ulLower = ( uint32_t ) ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_03_PHYSID2 );
	ulUpper = ( uint32_t ) ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_02_PHYSID1 );

	ulMACPhyID = ( ( ulUpper << 16 ) & 0xFFFF0000 ) | ( ulLower & 0xFFF0 );

	/* The expected ID for the 'DP83848I' is 0x20005C90. */
	FreeRTOS_printf( ( "PHY ID %X\n", ulMACPhyID ) );

	/* Remove compiler warning if FreeRTOS_printf() is not defined. */
	( void ) ulMACPhyID;

    /* Set advertise register. */
	if( ( xPHYProperties.speed == PHY_SPEED_AUTO ) && ( xPHYProperties.duplex == PHY_DUPLEX_AUTO ) )
	{
		ulAdvertise = ADVERTISE_CSMA | ADVERTISE_ALL;
		/* Reset auto-negotiation capability. */
	}
	else
	{
		ulAdvertise = ADVERTISE_CSMA;

		if( xPHYProperties.speed == PHY_SPEED_AUTO )
		{
			if( xPHYProperties.duplex == PHY_DUPLEX_FULL )
			{
				ulAdvertise |= ADVERTISE_10FULL | ADVERTISE_100FULL;
			}
			else
			{
				ulAdvertise |= ADVERTISE_10HALF | ADVERTISE_100HALF;
			}
		}
		else if( xPHYProperties.duplex == PHY_DUPLEX_AUTO )
		{
			if( xPHYProperties.speed == PHY_SPEED_10 )
			{
				ulAdvertise |= ADVERTISE_10FULL | ADVERTISE_10HALF;
			}
			else
			{
				ulAdvertise |= ADVERTISE_100FULL | ADVERTISE_100HALF;
			}
		}
		else if( xPHYProperties.speed == PHY_SPEED_100 )
		{
			if( xPHYProperties.duplex == PHY_DUPLEX_FULL )
			{
				ulAdvertise |= ADVERTISE_100FULL;
			}
			else
			{
				ulAdvertise |= ADVERTISE_100HALF;
			}
		}
		else
		{
			if( xPHYProperties.duplex == PHY_DUPLEX_FULL )
			{
				ulAdvertise |= ADVERTISE_10FULL;
			}
			else
			{
				ulAdvertise |= ADVERTISE_10HALF;
			}
		}
	}

	/* Read Control register. */
	ulConfig = ( uint32_t )ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_00_BMCR );

	ETH_WritePHYRegister( DP83848_PHY_ADDRESS, PHY_REG_00_BMCR, ulConfig | BMCR_RESET );
	xRemTime = ( TickType_t ) pdMS_TO_TICKS( 1000UL );
	vTaskSetTimeOutState( &xPhyTime );

	for( ; ; )
	{
		ulValue = ( uint32_t )ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_00_BMCR );
		if( ( ulValue & BMCR_RESET ) == 0 )
		{
			FreeRTOS_printf( ( "BMCR_RESET ready\n" ) );
			break;
		}
		if( xTaskCheckForTimeOut( &xPhyTime, &xRemTime ) != pdFALSE )
		{
			FreeRTOS_printf( ( "BMCR_RESET timed out\n" ) );
			break;
		}
	}
	ETH_WritePHYRegister( DP83848_PHY_ADDRESS, PHY_REG_00_BMCR, ulConfig & ~BMCR_RESET );

	vTaskDelay( pdMS_TO_TICKS( 50ul ) );

    /* Write advertise register. */
	ETH_WritePHYRegister( DP83848_PHY_ADDRESS, PHY_REG_04_ADVERTISE, ulAdvertise );

	/*
			AN_EN        AN1         AN0       Forced Mode
			  0           0           0        10BASE-T, Half-Duplex
			  0           0           1        10BASE-T, Full-Duplex
			  0           1           0        100BASE-TX, Half-Duplex
			  0           1           1        100BASE-TX, Full-Duplex
			AN_EN        AN1         AN0       Advertised Mode
			  1           0           0        10BASE-T, Half/Full-Duplex
			  1           0           1        100BASE-TX, Half/Full-Duplex
			  1           1           0        10BASE-T Half-Duplex
											   100BASE-TX, Half-Duplex
			  1           1           1        10BASE-T, Half/Full-Duplex
											   100BASE-TX, Half/Full-Duplex
	*/

    /* Read Control register. */
	ulConfig = ( uint32_t )ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_00_BMCR );

	ulConfig &= ~( BMCR_ANRESTART | BMCR_ANENABLE | BMCR_SPEED100 | BMCR_FULLDPLX );

	/* HT 12/9/14: always set AN-restart and AN-enable, even though the choices
	are limited. */
	ulConfig |= (BMCR_ANRESTART | BMCR_ANENABLE);

	if( xPHYProperties.speed == PHY_SPEED_100 )
	{
		ulConfig |= BMCR_SPEED100;
	}
	else if( xPHYProperties.speed == PHY_SPEED_10 )
	{
		ulConfig &= ~BMCR_SPEED100;
	}

	if( xPHYProperties.duplex == PHY_DUPLEX_FULL )
	{
		ulConfig |= BMCR_FULLDPLX;
	}
	else if( xPHYProperties.duplex == PHY_DUPLEX_HALF )
	{
		ulConfig &= ~BMCR_FULLDPLX;
	}

	/* Read PHY Control register. */
	ulPhyControl = ( uint32_t )ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_19_PHYCR );

	/* Clear bits which might get set: */
	ulPhyControl &= ~( PHYCR_MDIX_EN|PHYCR_MDIX_FORCE );

	if( xPHYProperties.mdix == PHY_MDIX_AUTO )
	{
		ulPhyControl |= PHYCR_MDIX_EN;
	}
	else if( xPHYProperties.mdix == PHY_MDIX_CROSSED )
	{
		/* Force direct link = Use crossed RJ45 cable. */
		ulPhyControl &= ~PHYCR_MDIX_FORCE;
	}
	else
	{
		/* Force crossed link = Use direct RJ45 cable. */
		ulPhyControl |= PHYCR_MDIX_FORCE;
	}
	/* update PHY Control Register. */
	ETH_WritePHYRegister( DP83848_PHY_ADDRESS, PHY_REG_19_PHYCR, ulPhyControl );

	FreeRTOS_printf( ( "+TCP: advertise: %lX config %lX\n", ulAdvertise, ulConfig ) );

	/* Now the two values to global values for later use. */
	ulBCRvalue = ulConfig;
	ulACRValue = ulAdvertise;
}
/*-----------------------------------------------------------*/

static void prvEthernetUpdateConfig( BaseType_t xForce )
{
__IO uint32_t ulTimeout = 0;
uint32_t ulRegValue = 0;
uint32_t ulTempReg;

	FreeRTOS_printf( ( "prvEthernetUpdateConfig: LS %d Force %d\n",
		( ulPHYLinkStatus & BMSR_LINK_STATUS ) != 0 ,
		xForce ) );

	if( ( xForce != pdFALSE ) || ( ( ulPHYLinkStatus & BMSR_LINK_STATUS ) != 0 ) )
	{
		/* Restart the auto-negotiation. */
		if( ETH_InitStructure.ETH_AutoNegotiation != ETH_AutoNegotiation_Disable )
		{
			/* Enable Auto-Negotiation. */
			ETH_WritePHYRegister( DP83848_PHY_ADDRESS, PHY_REG_04_ADVERTISE, ulACRValue );
			ETH_WritePHYRegister( DP83848_PHY_ADDRESS, PHY_REG_00_BMCR, ulBCRvalue | BMCR_ANRESTART );

			/* Wait until the auto-negotiation will be completed */
			do
			{
				ulTimeout++;
				ulRegValue = ( uint32_t )ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_01_BMSR );
			} while( ( ( ulRegValue & PHY_AUTONEGO_COMPLETE) == 0 ) && ( ulTimeout < PHY_READ_TO ) );

			ETH_WritePHYRegister( DP83848_PHY_ADDRESS, PHY_REG_00_BMCR, ulBCRvalue & ~BMCR_ANRESTART );

			if( ulTimeout < PHY_READ_TO )
			{
				/* Reset Timeout counter. */
				ulTimeout = 0;

				/* Read the result of the auto-negotiation. */
				ulRegValue = ( uint32_t )ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_10_PHY_SR );
				if( ( ulRegValue & PHY_SR_LINK_STATUS ) != 0 )
				{
					ulPHYLinkStatus |= BMSR_LINK_STATUS;
				}
				else
				{
					ulPHYLinkStatus &= ~( BMSR_LINK_STATUS );
				}

				FreeRTOS_printf( ( ">> Autonego ready: %08x: %s duplex %u mbit %s status\n",
					ulRegValue,
					(ulRegValue & PHY_DUPLEX_STATUS) ? "full" : "half",
					(ulRegValue & PHY_SPEED_STATUS) ? 10 : 100,
					(ulRegValue & PHY_SR_LINK_STATUS) ? "high" : "low" ) );

				/* Configure the MAC with the Duplex Mode fixed by the
				auto-negotiation process. */
				if( ( ulRegValue & PHY_DUPLEX_STATUS ) != ( uint32_t ) RESET )
				{
					/* Set Ethernet duplex mode to Full-duplex following the
					auto-negotiation. */
					ETH_InitStructure.ETH_Mode = ETH_Mode_FullDuplex;
				}
				else
				{
					/* Set Ethernet duplex mode to Half-duplex following the
					auto-negotiation. */
					ETH_InitStructure.ETH_Mode = ETH_Mode_HalfDuplex;
				}

				/* Configure the MAC with the speed fixed by the
				auto-negotiation process. */
				if( ( ulRegValue & PHY_SPEED_STATUS) != 0 )
				{
					/* Set Ethernet speed to 10M following the
					auto-negotiation. */
					ETH_InitStructure.ETH_Speed = ETH_Speed_10M; 
				}
				else
				{
					/* Set Ethernet speed to 100M following the
					auto-negotiation. */
					ETH_InitStructure.ETH_Speed = ETH_Speed_100M; 
				}
				
				ulTempReg = ETH->MACCR & ~( ETH_Speed_100M |  ETH_Mode_FullDuplex );
				ulTempReg |= ( ETH_InitStructure.ETH_Speed | ETH_InitStructure.ETH_Mode );
				ETH->MACCR = ulTempReg;
				if( ETH->MACCR ) {}
				ETH->MACCR = ulTempReg;
			}	/* if( ulTimeout < PHY_READ_TO ) */
		}
		else /* AutoNegotiation Disable */
		{
		uint16_t usValue = 0u;

			/* Send MAC Speed and Duplex Mode to PHY. */
			if( ( ETH_InitStructure.ETH_Mode & ETH_Mode_FullDuplex ) != 0 )
			{
				usValue |= BMCR_FULLDPLX;
			}
			if( ( ETH_InitStructure.ETH_Speed & ETH_Speed_100M ) != 0 )
			{
				usValue |= BMCR_SPEED100;
			}

			usValue = ( uint16_t ) ( ETH_InitStructure.ETH_Mode >> 3 ) | ( uint16_t ) ( ETH_InitStructure.ETH_Speed >> 1 );
			ETH_WritePHYRegister( DP83848_PHY_ADDRESS, PHY_REG_00_BMCR, usValue );
		}

		/* ETHERNET MAC Re-Configuration */
		/* initialize MAC address in ethernet MAC */ 
		ETH_MACAddressConfig( ETH_MAC_Address0, ( uint8_t * )ucMACAddress ); 
		#if( ipconfigUSE_LLMNR == 1 )
		{
			ETH_MACAddressConfig( ETH_MAC_Address1, ( uint8_t * )xLLMNR_MACAddress ); 
			ETH_MACAddressPerfectFilterCmd( ETH_MAC_Address1, ENABLE ); 
		}
		#endif
		xFirstTransmit = pdTRUE;

		/* Restart MAC interface */
		ETH_Start();
	}
	else
	{
		/* Stop MAC interface */
		ETH_Stop();
	}
}
/*-----------------------------------------------------------*/

BaseType_t xGetPhyLinkStatus( void )
{
BaseType_t xReturn;

	if( ( ulPHYLinkStatus & BMSR_LINK_STATUS ) != 0 )
	{
		xReturn = pdPASS;
	}
	else
	{
		xReturn = pdFAIL;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

static void prvEMACHandlerTask( void *pvParameters )
{
TimeOut_t xPhyTime;
TickType_t xPhyRemTime;
UBaseType_t uxLastMinBufferCount = 0;
#if( ipconfigCHECK_IP_QUEUE_SPACE != 0 )
UBaseType_t uxLastMinQueueSpace = 0;
#endif
UBaseType_t uxCurrentCount;
BaseType_t xResult = 0;
uint32_t xStatus;
const TickType_t ulMaxBlockTime = pdMS_TO_TICKS( 100UL );

	/* Remove compiler warnings about unused parameters. */
	( void ) pvParameters;

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

			xResult = prvNetworkInterfaceInput();
			if( xResult > 0 )
			{
			  	while( prvNetworkInterfaceInput() > 0 )
				{
				}
			}
		}

		if( ( ulISREvents & EMAC_IF_TX_EVENT ) != 0 )
		{
			/* Future extension: code to release TX buffers if zero-copy is used. */
			ulISREvents &= ~EMAC_IF_TX_EVENT;
			#if( ipconfigZERO_COPY_TX_DRIVER != 0 )
			{
				/* Check if DMA packets have been delivered. */
				vClearTXBuffers();
			}
			#endif
		}

		if( ( ulISREvents & EMAC_IF_ERR_EVENT ) != 0 )
		{
			/* Future extension: logging about errors that occurred. */
			ulISREvents &= ~EMAC_IF_ERR_EVENT;
		}

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
			xStatus = ( uint32_t )ETH_ReadPHYRegister( DP83848_PHY_ADDRESS, PHY_REG_01_BMSR );
			if( ( ulPHYLinkStatus & BMSR_LINK_STATUS ) != ( xStatus & BMSR_LINK_STATUS ) )
			{
				ulPHYLinkStatus = xStatus;
				FreeRTOS_printf( ( "prvEMACHandlerTask: PHY LS now %d\n", ( ulPHYLinkStatus & BMSR_LINK_STATUS ) != 0 ) );
				prvEthernetUpdateConfig( pdFALSE );
			}

			vTaskSetTimeOutState( &xPhyTime );
			if( ( ulPHYLinkStatus & BMSR_LINK_STATUS ) != 0 )
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

