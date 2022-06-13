/** ***************************************************************************************************
 * @file NetworkInterface.c
 * @author Lovas Szilard <lovas.szilard@gmail.com>
 * @date 2016.02.22
 * @version 0.2
 * @copyright Lovas Szilard
 * GNU GENERAL PUBLIC LICENSE Version 2, June 1991
 *
 * This code is made as a part of a research, that has been supported by the „Highly industrialized region on the west part of
 * Hungary with limited R&D capacity: Research and development programs related to
 * strengthening the strategic futureoriented industries manufacturing technologies and
 * products of regional competences carried out in comprehensive collaboration” program of
 * the National Research, Development and Innovation Fund (NKFI), Hungary, Grant. No. VKSZ_12-1-2013-0038.
 *
 * A kód elkészítését a Nemzeti Kutatási, Fejlesztési és Innovációs Alap támogatásával megvalósuló
 * VKSZ_12-1-2013-0038:"Stratégiai ipari ágazatok jövõbemutató gyártási technológiáihoz és
 * termékeihez kapcsolódó térségi kutatási kompetenciáik megerõsítése széleskörû
 * együttmûködésben megvalósított kutatás-fejlesztési programmal" projekt támogatta.
 *
 * Homepage: http://jkk.sze.hu/fooldal
 *
 * @brief FreeRTOS-Plus-TCP NetworkInterface.c port for Texas Instruments TMS570LC4357 microcontroller.
 *
 * @details
 * Dependencies:
 * -------------
 * - FreeRTOS Labs 160112
 * - Modified HALCoGen 04.04.00
 *
 * Changelog:
 * ----------
 * 2016.02.22
 * Initial release
 * 2016.04.15
 * Using vTaskNotifyGiveFromISR/ulTaskNotifyTake for signaling  EMAC RX interrupt
 * 2016.04.25
 * Doxygen style documentation added.
 */

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "os_task.h"
#include "os_semphr.h"

/* HALCoGen generated header files */
#include "HL_emac.h"
#include "HL_mdio.h"
#include "HL_phy_dp83640.h"
#include "HL_sys_vim.h"
#include "HL_gio.h"
#include "HL_reg_het.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_IP_Private.h"
#include "NetworkBufferManagement.h"

/* HALCoGen generated c files */
#include "HL_emac.c"

#define EMAC_INT_CORE0_RX_THRESH		(0x0U)		// Acknowledge C0RXTHRESH Interrupt
#define EMAC_INT_CORE0_MISC				(0x3U)		// Acknowledge C0MISC Interrupt (STATPEND, HOSTPEND, MDIO LINKINT0, MDIO USERINT0)
#define EMAC_INT_CORE1_RX_THRESH		(0x4U)		// Acknowledge C1RXTHRESH Interrupt
#define EMAC_INT_CORE1_MISC				(0x7U)		// Acknowledge C1MISC Interrupt (STATPEND, HOSTPEND, MDIO LINKINT0, MDIO USERINT0)
#define EMAC_INT_CORE2_RX_THRESH		(0x8U)		// Acknowledge C2RXTHRESH Interrupt
#define EMAC_INT_CORE2_MISC				(0xbU)		// Acknowledge C2MISC Interrupt (STATPEND, HOSTPEND, MDIO LINKINT0, MDIO USERINT0)

/* Missing defines in HL_hw_emac_ctrl.h.. */
/* A HL_hw_emac_ctrl.h-ból hiányzó #define-ok.. */
#define EMAC_CTRL_C0MISEN_USERINT0EN 	(0x00000001U)
#define EMAC_CTRL_C0MISEN_USERINT0EN_SHIFT (0x00000000U)
#define EMAC_CTRL_C0MISEN_LINKINT0EN 	(0x00000002U)
#define EMAC_CTRL_C0MISEN_LINKINT0EN_SHIFT (0x000000001)
#define EMAC_CTRL_C0MISEN_HOSTPENDEN 	(0x00000004U)
#define EMAC_CTRL_C0MISEN_HOSTPENDEN_SHIFT (0x000000002)
#define EMAC_CTRL_C0MISEN_STATPENDEN 	(0x00000008U)
#define EMAC_CTRL_C0MISEN_STATPENDEN_SHIFT (0x000000003)

/* RXINTSTATRAW regiszter threshold pending bitek */
#define RXnTHRESHPEND_BIT(n)		    (1 << (8 + n))

/* EMAC related VIM channels */
/* EMAC-hoz kapcsolódó VIM csatornák */
#define C0_MISC_PULSE					76U
#define C0_TX_PULSE						77U
#define C0_THRSH_PULSE					78U
#define C0_RX_PULSE						79U

/* Maximum number of retries for reading PHY ID */
/* Legfeljebb ennyiszer próbálkozik a PHY ID kiolvasásával */
#define PHY_INIT_ID_READ_MAX_RETRIES	0xffff

/* Number of TX and RX DMA buffers */
/* Tx és RX EMAC DMA pufferek száma */
#define EMAC_TXDMA_PBUF_START_ADDRESS	(EMAC_CTRL_RAM_BASE)
#define EMAC_TXDMA_PBUF_ALLOC 			((SIZE_EMAC_CTRL_RAM / 2) / sizeof(emac_tx_bd_t))
#define EMAC_RXDMA_PBUF_START_ADDRESS	(EMAC_CTRL_RAM_BASE + (SIZE_EMAC_CTRL_RAM / 2))
#define EMAC_RXDMA_PBUF_ALLOC			(MAX_RX_PBUF_ALLOC)				/* HALCoGen GUI érték */

/* EMAC descriptor flags TX+RX */
/* EMAC descriptor flag-ek TX+RX */
#define EMAC_DSC_FLAG_SOP 				0x80000000u
#define EMAC_DSC_FLAG_EOP 				0x40000000u
#define EMAC_DSC_FLAG_OWNER 			0x20000000u
#define EMAC_DSC_FLAG_EOQ 				0x10000000u
#define EMAC_DSC_FLAG_TDOWNCMPLT 		0x08000000u
#define EMAC_DSC_FLAG_PASSCRC 			0x04000000u
/* EMAC descriptor flags only RX */
/* EMAC Descriptor flag-ek csak RX */
#define EMAC_DSC_FLAG_JABBER 			0x02000000u
#define EMAC_DSC_FLAG_OVERSIZE 			0x01000000u
#define EMAC_DSC_FLAG_FRAGMENT 			0x00800000u
#define EMAC_DSC_FLAG_UNDERSIZED 		0x00400000u
#define EMAC_DSC_FLAG_CONTROL 			0x00200000u
#define EMAC_DSC_FLAG_OVERRUN 			0x00100000u
#define EMAC_DSC_FLAG_CODEERROR 		0x00080000u
#define EMAC_DSC_FLAG_ALIGNERROR 		0x00040000u
#define EMAC_DSC_FLAG_CRCERROR			0x00020000u
#define EMAC_DSC_FLAG_NOMATCH 			0x00010000u

/* Interrupt macros */
/* Interrupt makrók */
#ifndef traceEMAC_INT_CORE0_RX_THRESH
	#define traceEMAC_INT_CORE0_RX_THRESH()
#endif

#ifndef traceEMAC_INT_CORE0_MISC_LINK_STATUS
	#define traceEMAC_INT_CORE0_MISC_LINK_STATUS()
#endif

#ifndef traceEMAC_INT_CORE0_MISC
	#define traceEMAC_INT_CORE0_MISC()
#endif

#ifndef traceEMAC_INT_CORE0_MISC_USER_COMMAND
	#define traceEMAC_INT_CORE0_MISC_USER_COMMAND()
#endif

#ifndef traceEMAC_INT_CORE0_MISC_HOST
	#define traceEMAC_INT_CORE0_MISC_HOST()		gioSetBit(gioPORTB, 7, 1)
#endif

#ifndef traceEMAC_INT_CORE0_MISC_STAT
	#define traceEMAC_INT_CORE0_MISC_STAT()
#endif

#ifndef traceEMAC_INT_CORE0_TX
	#define traceEMAC_INT_CORE0_TX()
#endif

/* Minimum ethernet frame size without Frame Check Sequence */
/* Minimális ethernet csomag méret a Frame Check Sequence nélkül */
#define MIN_ETHERNET_PACKET_SIZE		(60)

#define _CPU_TMS570LS4357_

void vFreeRTOSEMACMiscInterrupt(void);
void vFreeRTOSEMACTxInterrupt(void);
void vFreeRTOSEMACRxThrshInterrupt(void);
void vFreeRTOSEMACRxInterrupt(void);
uint32 xFreeRTOSEMACHWInit(uint8_t macaddr[6U]);
void prvDisableEMACInterrupts(void);
static void prvEmacRxTask(void *pvParameters);
static void prvEmacDMAInit(hdkif_t *hdkif);
static void prvDisableEMACInterrupts(void);
static void prvEnableEMACInterrupts(void);

static BaseType_t xEMACDriverLoggingLevel = 0;

SemaphoreHandle_t xEMACMiscEventSemaphore = NULL;			/* Link, User, Stat, Host események szemafor */
volatile unsigned int xEMACMiscEventBit = pdFALSE;			/* Link, User, Stat, Host események jelzõbit */

static xTaskHandle prvEmacRxTaskHandle = NULL;

extern BaseType_t xEMACRxEventSemaphoreFulls;
extern _dcacheCleanRange_(unsigned int startAddress, unsigned int endAddress);
extern _dcacheInvalidateRange_(unsigned int startAddress, unsigned int endAddress);

/* The MAC address defined in HL_sys_main.c */
extern uint8 emacAddress[6U];
extern hdkif_t hdkif_data[];

/* If ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES is set to 1, then the Ethernet
driver will filter incoming packets and only pass the stack those packets it
considers need processing. */
#if(ipconfigETHERNET_DRIVER_FILTERS_FRAME_TYPES == 0)
	#define ipCONSIDER_FRAME_FOR_PROCESSING(pucEthernetBuffer) eProcessBuffer
#else
	#define ipCONSIDER_FRAME_FOR_PROCESSING(pucEthernetBuffer) eConsiderFrameForProcessing((pucEthernetBuffer))
#endif


/** ***************************************************************************************************
 * @fn		static void prvDisableEMACInterrupts(void)
 * @brief	Disable EMAC module interrupts in VIM.
 */
static void prvDisableEMACInterrupts(void)
{
	vimREG->REQMASKCLR2 = (uint32)1U << (C0_MISC_PULSE-64U) | (uint32)1U << (C0_TX_PULSE-64U) | (uint32)1U << (C0_THRSH_PULSE-64U) | (uint32)1U << (C0_RX_PULSE-64U);
}

/** ***************************************************************************************************
 * @fn		static void prvEnableEMACInterrupts(void)
 * @brief	Enable EMAC module interrupts in VIM.
 */
static void prvEnableEMACInterrupts(void)
{
	vimREG->REQMASKSET2 = (uint32)1U << (C0_MISC_PULSE-64U) | (uint32)1U << (C0_TX_PULSE-64U) | (uint32)1U << (C0_THRSH_PULSE-64U) | (uint32)1U << (C0_RX_PULSE-64U);
}

/** ***************************************************************************************************
 * @fn		BaseType_t xNetworkInterfaceInitialise(void)
 * @brief	High level function for initializing EMAC module for sending and receiving ethernet frames.
 * 			- Redirects ISR vectors
 * 			- Calls the low level EMAC HW init function.
 * 			- Creates the necessary task/semaphore(s)
 * @return	pdFAIL Error
 * 			pdPASS Success
 */
BaseType_t xNetworkInterfaceInitialise(void)
{
	BaseType_t xReturn = pdFAIL;
	hdkif_t *hdkif = &hdkif_data[0U];

	/* Disable all EMAC interrupts in VIM. */
	/* Az összes EMAC interrupt letiltása  a VIM-ben. */
	prvDisableEMACInterrupts();

    /* EMAC RX és TX tiltása */
    HWREG(EMAC_0_BASE + EMAC_RXCONTROL) = EMAC_RXCONTROL_RXDIS;
    HWREG(EMAC_0_BASE + EMAC_TXCONTROL) = EMAC_TXCONTROL_TXDIS;

	/* Redirect interrupt vectors to the */
    /* Interrupt vektorok átirányítása a saját ISR függvényekre */
	vimChannelMap(C0_MISC_PULSE, C0_MISC_PULSE, &vFreeRTOSEMACMiscInterrupt);
	vimChannelMap(C0_TX_PULSE, C0_TX_PULSE, &vFreeRTOSEMACTxInterrupt);
	vimChannelMap(C0_THRSH_PULSE, C0_THRSH_PULSE, &vFreeRTOSEMACRxThrshInterrupt);
	vimChannelMap(C0_RX_PULSE, C0_RX_PULSE, &vFreeRTOSEMACRxInterrupt);

	/* Meghívjuk az EMAC init függvényét  */
	if(xFreeRTOSEMACHWInit(emacAddress) != EMAC_ERR_OK)xReturn = pdFAIL;
	else
	{
		/* Elsõ inicializáláskor Létrehozzuk a szükséges RX taszkot és a kapcsolódó szemaforokat */
		if(xEMACMiscEventSemaphore == NULL)
		{
			xEMACMiscEventSemaphore = xSemaphoreCreateBinary();
			configASSERT(xEMACMiscEventSemaphore);
		}
		if(prvEmacRxTaskHandle == NULL)
		{
			/* Az _dCacheInvalidateRange_() miatt kell privilegizált módban futtatni */
			xTaskCreate(prvEmacRxTask, "EmacRx", ipconfigETHERNET_DRIVER_RX_TASK_STACK_SIZE_WORDS, NULL, ipconfigETHERNET_DRIVER_RX_TASK_PRIORITY, &prvEmacRxTaskHandle);
			configASSERT(prvEmacRxTaskHandle);
		}

		/* Minden szükséges taszk és szemafor rendben létrejött */
		if(xEMACMiscEventSemaphore != NULL && prvEmacRxTaskHandle != NULL)
		{
			/* IRQ engedélyezések */
			/* A MISC interrupt-ok (Link, HOST, STAT) engedélyezése */
			HWREG(EMAC_CTRL_0_BASE + EMAC_CTRL_CnMISCEN(0)) = EMAC_CTRL_C0MISEN_LINKINT0EN | EMAC_CTRL_C0MISEN_HOSTPENDEN | EMAC_CTRL_C0MISEN_STATPENDEN;
			/* Az 1-es phyaddr címû eszkösz link változás monitorozása */
			HWREG(MDIO_BASE + MDIO_USERPHYSEL0) |= MDIO_USERPHYSEL0_LINKINTENB | 0x1U;

			/* Host és Stat megszakítások engedélyezése */
			HWREG(EMAC_BASE + EMAC_MACINTMASKSET) = EMAC_MACINTMASKSET_HOSTMASK | EMAC_MACINTMASKSET_STATMASK;

			/* TX és RX megszakítások engedélyezése */
			HWREG(hdkif->emac_base + EMAC_TXINTMASKSET) |= ((uint32)1U << EMAC_CHANNELNUMBER);
			HWREG(hdkif->emac_ctrl_base + EMAC_CTRL_CnTXEN(EMAC_CHANNELNUMBER)) |= ((uint32)1U << EMAC_CHANNELNUMBER);
			HWREG(hdkif->emac_base + EMAC_RXINTMASKSET) |= ((uint32)1U << EMAC_CHANNELNUMBER);
			HWREG(hdkif->emac_ctrl_base + EMAC_CTRL_CnRXEN(EMAC_CHANNELNUMBER)) |= ((uint32)1U << EMAC_CHANNELNUMBER);

			/* EMAC RX és TX engedélyezése */
			HWREG(hdkif->emac_base + EMAC_RXCONTROL) = EMAC_RXCONTROL_RXEN;
			HWREG(hdkif->emac_base + EMAC_TXCONTROL) = EMAC_TXCONTROL_TXEN;

			/* Az összes EMAC interrupt engedélyezése a VIM-ben. */
			prvEnableEMACInterrupts();

			/* Csomagok fogadásának indítása a HP beállításával */
			HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER)) = EMAC_RXDMA_PBUF_START_ADDRESS;
			xReturn = pdPASS;
		}
	}

	return xReturn;
}

/** ***************************************************************************************************
 * @fn		BaseType_t xNetworkInterfaceOutput(NetworkBufferDescriptor_t * const pxDescriptor, BaseType_t xReleaseAfterSend)
 * @brief	Send data over ethernet (EMAC) interface.
 * @param	pxDescriptor pointer to the buffer descriptor
 * @param	xReleaseAfterSend Release the descriptor after send (pdPASS)
 * @return	pdFAIL Error
 * 			pdPASS Success
 */
BaseType_t xNetworkInterfaceOutput(NetworkBufferDescriptor_t * const pxDescriptor, BaseType_t xReleaseAfterSend)
{
    hdkif_t *hdkif = &hdkif_data[0U];
    uint32 xFlagsPktlen;
    uint16 xTotalLength;
    emac_tx_bd_t * pxTransmitBufferDescriptor;
    static uint8_t cPaddingBuffer[MIN_ETHERNET_PACKET_SIZE];

    _dcacheCleanRange_((uint32)(pxDescriptor->pucEthernetBuffer),(uint32)(pxDescriptor->pucEthernetBuffer) + MAX_TRANSFER_UNIT);
	pxTransmitBufferDescriptor = (emac_tx_bd_t *)EMAC_TXDMA_PBUF_START_ADDRESS;

	if((EMACSwizzleData(pxTransmitBufferDescriptor->flags_pktlen) & EMAC_DSC_FLAG_OWNER) == EMAC_DSC_FLAG_OWNER)
	{
		return pdFALSE;
	}
	else
	{
		/* Csak a nem 0 hosszú csomagokat küldjük el a nem null címrõl. */
		if(pxDescriptor->xDataLength != 0 && pxDescriptor->pucEthernetBuffer != NULL)
		{
			/* Ha a csomag méret nem éri el a minimális méretet, akkor ki kell egészíteni */
			if(pxDescriptor->xDataLength < MIN_ETHERNET_PACKET_SIZE)
			{
				if(xEMACDriverLoggingLevel > 2)FreeRTOS_debug_printf(("xNetworkInterfaceOutput() packet size: %d padding to %d bytes\n\r", pxDescriptor->xDataLength, MIN_ETHERNET_PACKET_SIZE));
				/* Buffer pointer az eredeti csomag számára*/
				pxTransmitBufferDescriptor->bufptr = EMACSwizzleData((uint32_t)pxDescriptor->pucEthernetBuffer);
				/* Buffer Offset and Buffer length */
				pxTransmitBufferDescriptor->bufoff_len = EMACSwizzleData(pxDescriptor->xDataLength);
				/* Flags and Packet length */
				xTotalLength = MIN_ETHERNET_PACKET_SIZE;
				/* Nem állítjuk be a EOP bitet, az ethernet packet több descriptor-on keresztûl húzódik */
				xFlagsPktlen = ((uint32)(xTotalLength) | (EMAC_DSC_FLAG_SOP | EMAC_DSC_FLAG_OWNER));
				pxTransmitBufferDescriptor->flags_pktlen = EMACSwizzleData(xFlagsPktlen);
				/* Next descriptor pointer */
				pxTransmitBufferDescriptor->next = (emac_tx_bd_t *)EMACSwizzleData((uint32_t)(pxTransmitBufferDescriptor + 1));
				/* Továbblépünk a következõ descriptor-ra ami a csomag padding-ot fogja végezni */
				pxTransmitBufferDescriptor++;
				/* Buffer pointer a padding számára */
				pxTransmitBufferDescriptor->bufptr = EMACSwizzleData((uint32_t)cPaddingBuffer);
				/* Buffer Offset and Buffer length */
				pxTransmitBufferDescriptor->bufoff_len = EMACSwizzleData((MIN_ETHERNET_PACKET_SIZE - pxDescriptor->xDataLength));
				/* Flags and Packet length */
				xTotalLength = MIN_ETHERNET_PACKET_SIZE;
				xFlagsPktlen = ((uint32)(xTotalLength) | (EMAC_DSC_FLAG_EOP));
				pxTransmitBufferDescriptor->flags_pktlen = EMACSwizzleData(xFlagsPktlen);
				/* Next descriptor pointer */
				pxTransmitBufferDescriptor->next = NULL;
			}
			else
			{
				/* Buffer pointer */
				pxTransmitBufferDescriptor->bufptr = EMACSwizzleData((uint32_t)pxDescriptor->pucEthernetBuffer);
				/* Buffer Offset and Buffer length */
				pxTransmitBufferDescriptor->bufoff_len = EMACSwizzleData(pxDescriptor->xDataLength);
				/* Flags and Packet length */
				xTotalLength = pxDescriptor->xDataLength;
				xFlagsPktlen = ((uint32)(xTotalLength) | (EMAC_DSC_FLAG_SOP | EMAC_DSC_FLAG_EOP | EMAC_DSC_FLAG_OWNER));
				pxTransmitBufferDescriptor->flags_pktlen = EMACSwizzleData(xFlagsPktlen);
				/* Next descriptor pointer */
				pxTransmitBufferDescriptor->next = NULL;
			}

			pxTransmitBufferDescriptor = (emac_tx_bd_t *)EMAC_TXDMA_PBUF_START_ADDRESS;
			EMACTxHdrDescPtrWrite(hdkif->emac_base, (uint32)(pxTransmitBufferDescriptor), (uint32)EMAC_CHANNELNUMBER);

			/* Megvárjuk, amíg átmegy a csomag */
			while(EMAC_DSC_FLAG_OWNER == (EMACSwizzleData(pxTransmitBufferDescriptor->flags_pktlen) & EMAC_DSC_FLAG_OWNER));

			/* Call the standard trace macro to log the send event. */
			iptraceNETWORK_INTERFACE_TRANSMIT();
		}
		else
		{
			/* Megpróbáljuk felszabadítani a hibás csomagleírót */
			xReleaseAfterSend = pdTRUE;
		}
	}
#if(ipconfigZERO_COPY_TX_DRIVER == 0)
	if(xReleaseAfterSend != pdFALSE)
    {
        /* It is assumed SendData() copies the data out of the FreeRTOS+TCP Ethernet
        buffer.  The Ethernet buffer is therefore no longer needed, and must be
        freed for re-use. */
        vReleaseNetworkBufferAndDescriptor(pxDescriptor);
    }
#else
#error ipconfigZERO_COPY_TX_DRIVER not available yet
#endif

    return pdTRUE;
}

/** ***************************************************************************************************
* @fn void vFreeRTOSEMACMiscInterrupt(void)
* @brief Misc Interrupt handler for EMAC in FreeRTOS-Plus-TCP compatibility mode
*/
#pragma INTERRUPT(vFreeRTOSEMACMiscInterrupt, IRQ)
void vFreeRTOSEMACMiscInterrupt(void)
{
    hdkif_t *hdkif = &hdkif_data[0U];

    /* Link Status Change interrupt */
    if((HWREG(MDIO_BASE + MDIO_LINKINTRAW) & MDIO_LINKINTRAW_USERPHY0) == MDIO_LINKINTRAW_USERPHY0)
    {
    	/* Nyugtázzuk a megszakítást */
    	HWREG(MDIO_BASE + MDIO_LINKINTRAW) = MDIO_LINKINTRAW_USERPHY0;
    	traceEMAC_INT_CORE0_MISC_LINK_STATUS();		/* trace macro */
    }

    /* User Command Complete Interrupt */
    if((HWREG(MDIO_BASE + MDIO_USERINTMASKED) & MDIO_LINKINTMASKED_USERPHY0) == MDIO_LINKINTMASKED_USERPHY0)
    {
    	/* Tiltjuk a megszakítást. Nyugtázni és újra engedélyezni majd az applikációnak kell. */
    	HWREG(MDIO_BASE + MDIO_USERINTMASKCLEAR) = MDIO_USERINTMASKCLEAR_USERACCESS0;
        traceEMAC_INT_CORE0_MISC_USER_COMMAND();	/* trace macro */
    }

    /* HOST interrupt */
	if((HWREG(hdkif->emac_base + EMAC_MACINTSTATMASKED) & EMAC_MACINTSTATMASKED_HOSTPEND) == EMAC_MACINTSTATMASKED_HOSTPEND)
	{
		/* Tiltjuk a megszakítást. Nyugtázni és újra engedélyezni majd az applikációnak kell. */
		HWREG(hdkif->emac_base + EMAC_MACINTMASKCLEAR) =  EMAC_MACINTSTATMASKED_HOSTPEND;
        traceEMAC_INT_CORE0_MISC_HOST();			/* trace macro */
	}

    /* STAT(istic) interrupt */
	if((HWREG(hdkif->emac_base + EMAC_MACINTSTATMASKED) & EMAC_MACINTSTATMASKED_STATPEND) == EMAC_MACINTSTATMASKED_STATPEND)
	{
		/* Tiltjuk a megszakítást. Nyugtázni és újra engedélyezni majd az applikációnak kell. */
		HWREG(hdkif->emac_base + EMAC_MACINTMASKCLEAR) =  EMAC_MACINTSTATMASKED_STATPEND;
		traceEMAC_INT_CORE0_MISC_STAT();			/* trace macro */
	}

    traceEMAC_INT_CORE0_MISC();						/* trace macro */

	xEMACMiscEventBit = pdTRUE;						/* Ha a szemafor nem jött volna még létre egy biten is jelezzük, hogy volt interrupt */

	/* Küldünk egy xEMACMiscEventSemaphore-t */
   	if(xEMACMiscEventSemaphore != NULL)
   	{
   		xSemaphoreGiveFromISR(xEMACMiscEventSemaphore, NULL);
   	}

   	EMACCoreIntAck(hdkif->emac_base, (uint32)EMAC_INT_CORE0_MISC);
}

/** ***************************************************************************************************
* @fn void vFreeRTOSEMACTxInterrupt(void)
* @brief TX Interrupt for EMAC in FreeRTOS-Plus-TCP compatibility mode
*/
#pragma INTERRUPT(vFreeRTOSEMACTxInterrupt, IRQ)
void vFreeRTOSEMACTxInterrupt(void)
{
    static hdkif_t *hdkif = &hdkif_data[0U];
    emac_tx_bd_t *pxCurrentBufferDescriptor;
    traceEMAC_INT_CORE0_TX();			/* trace macro */

//TODO: ennek a megszakításnak kell felszabadítani a elküldött BD-khez tartozó network puffereket
#if(ipconfigZERO_COPY_TX_DRIVER == 1)
#error ipconfigZERO_COPY_TX_DRIVER not available yet
#else
#endif

    /* Nyugtázzuk az EMAC-nak BD feldolgozását. */
    pxCurrentBufferDescriptor = (emac_tx_bd_t *)HWREG(hdkif->emac_base + EMAC_TXCP(EMAC_CHANNELNUMBER));
    HWREG(hdkif->emac_base + EMAC_TXCP(EMAC_CHANNELNUMBER)) = (uint32_t)pxCurrentBufferDescriptor;
	/* Nyugtázzuk az EMAC control modul RX megszakítását. */
    EMACCoreIntAck(hdkif->emac_base, EMAC_INT_CORE0_TX);
}

/** ***************************************************************************************************
* @fn void vFreeRTOSEMACRxThrshInterrupt(void)
* @brief RX Threshold Interrupt for EMAC in FreeRTOS-Plus-TCP compatibility mode
*/
#pragma INTERRUPT(vFreeRTOSEMACRxThrshInterrupt, IRQ)
void vFreeRTOSEMACRxThrshInterrupt(void)
{
    static hdkif_t *hdkif = &hdkif_data[0U];

	if((HWREG(hdkif->emac_base + EMAC_RXINTSTATRAW) & RXnTHRESHPEND_BIT(EMAC_CHANNELNUMBER)) == RXnTHRESHPEND_BIT(EMAC_CHANNELNUMBER))
	{
		/* Letíltjuk a további threshold interrupt generálást. Újra engedélyezni majd az RX tasknak kell. */
		HWREG(hdkif->emac_base + EMAC_RXINTMASKCLEAR) = RXnTHRESHPEND_BIT(EMAC_CHANNELNUMBER);
	}

	traceEMAC_INT_CORE0_RX_THRESH();	/* trace macro */
    EMACCoreIntAck(hdkif->emac_base, (uint32)EMAC_INT_CORE0_RX_THRESH);
}

/** ***************************************************************************************************
* @fn void vFreeRTOSEMACRxInterrupt(void)
* @brief RX Interrupt for EMAC in FreeRTOS-Plus-TCP compatibility mode
*/
#pragma INTERRUPT(vFreeRTOSEMACRxInterrupt, IRQ)
void vFreeRTOSEMACRxInterrupt(void)
{
	static BaseType_t xHigherPriorityTaskWoken;
    static hdkif_t *hdkif = &hdkif_data[0U];
    emac_rx_bd_t *pxCurrentBufferDescriptor;

    if(prvEmacRxTaskHandle != NULL)
    {
    	vTaskNotifyGiveFromISR(prvEmacRxTaskHandle, &xHigherPriorityTaskWoken);
    }
    pxCurrentBufferDescriptor = (emac_rx_bd_t *)HWREG(hdkif->emac_base + EMAC_RXCP(EMAC_CHANNELNUMBER));
	HWREG(hdkif->emac_base + EMAC_RXCP(EMAC_CHANNELNUMBER)) = (uint32_t)pxCurrentBufferDescriptor; 	// Nyugtázzuk az EMAC-nak BD feldolgozását.
	EMACCoreIntAck(hdkif->emac_base, EMAC_INT_CORE0_RX);											// Nyugtázzuk az EMAC control modul RX megszakítását.

	portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/** ***************************************************************************************************
* @fn void prvEmacRxTask(void *pvParameters)
* @brief defered EMAC RX interrupt handler task.
*/
void prvEmacRxTask(void *pvParameters)
{
	hdkif_t *hdkif = &hdkif_data[0U];						/* EMAC pointerek */
	volatile rxch_t *pxRxChannelDMA = &(hdkif->rxchptr);	/* RX DMA pointerek */
	volatile emac_rx_bd_t *pxCurrentBufferDescriptor; 		/* Az aktuális host feldolgozás alatt álló BD címe */
	volatile emac_rx_bd_t *pxCurrentBufferDescTemp; 		/* Az aktuális host feldolgozás alatt álló BD címének mentésére szolgálló változó */
    volatile emac_rx_bd_t *pxTailBufferDescriptor;			/* A láncolt lista utolsó eleme (NEXT = NULL) */
    unsigned int xPacketSize;								/* Az érkezett csomag mérete byte-okban */
    NetworkBufferDescriptor_t *pxBufferDescriptor;			/* A FreeRTOS-Plus-TCP csomagleírója, ezen keresztûl kerül átadásra az érkezett csomag */
    xIPStackEvent_t xRxEvent;								/* A FreeRTOS-Plus-TCP esemény leírója */

    pxCurrentBufferDescriptor = pxRxChannelDMA->active_head;
    pxTailBufferDescriptor = (emac_rx_bd_t *)(0xfc521130);

    while(1)
    {
    	if(ulTaskNotifyTake(pdTRUE, ipconfigETHERNET_DRIVER_RX_TASK_BLOCK_TIME) > 0)
		{
			while(1)
			{
				/* Megnézzük mekkora csomag érkezett */
				xPacketSize = EMACSwizzleData(pxCurrentBufferDescriptor->flags_pktlen) & 0xffff;

				/* Invalidáljuk a cache-t, hogy az memóriában lévõ új csomagba ne zavarjon bele */
				_dcacheInvalidateRange_(EMACSwizzleData((uint32_t)pxCurrentBufferDescriptor->bufptr), EMACSwizzleData((uint32_t)pxCurrentBufferDescriptor->bufptr) + xPacketSize);

				/* Csak akkor állunk neki feldolgozni a csomagot, ha az EMAC átadta az adott BD-t és a SOP (Start Of Packet bit is be van állítva */
				if((EMACSwizzleData(pxCurrentBufferDescriptor->flags_pktlen) & EMAC_BUF_DESC_OWNER) != EMAC_BUF_DESC_OWNER && (EMACSwizzleData(pxCurrentBufferDescriptor->flags_pktlen) & EMAC_BUF_DESC_SOP) == EMAC_BUF_DESC_SOP)
				{
    				if(xEMACDriverLoggingLevel > 1)FreeRTOS_debug_printf(("EMACRX: Packet arrived, BD: %p, RXHP: %p\r\n", pxCurrentBufferDescriptor, HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER))));

					if((EMACSwizzleData(pxCurrentBufferDescriptor->flags_pktlen) & EMAC_BUF_DESC_EOP) != EMAC_BUF_DESC_EOP)
					{
						FreeRTOS_debug_printf(("EMACRX: NO EOP: %p, RXHP: %p\r\n", pxCurrentBufferDescriptor, HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER))));
						// Itt mit kéne csinálni ??
					}

					/* Csomagkezelés */
					if(eConsiderFrameForProcessing((const uint8_t *)EMACSwizzleData((uint32_t)pxCurrentBufferDescriptor->bufptr)) == eProcessBuffer)
					{
						if(xEMACDriverLoggingLevel > 1)FreeRTOS_debug_printf(("EMACRX: BD processing: %p, RXHP: %p\r\n", pxCurrentBufferDescriptor, HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER))));

						/* Kérünk a FreeRTOS+TCP stack-tõl egy megfelelõ méretû leirót és puffert */
						pxBufferDescriptor = pxGetNetworkBufferWithDescriptor(xPacketSize, (TickType_t)ipconfigTCP_MAX_RECV_BLOCK_TIME_TICKS);

						if(pxBufferDescriptor != NULL)
						{

		    				if(xEMACDriverLoggingLevel > 1)FreeRTOS_debug_printf(("EMACRX: Network buffer allocated. NP: %p, BD: %p, RXHP: %p\r\n", pxBufferDescriptor, pxCurrentBufferDescriptor, HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER))));

		    				memcpy((void *)pxBufferDescriptor->pucEthernetBuffer,(void *)(EMACSwizzleData(pxCurrentBufferDescriptor->bufptr)),xPacketSize);
							pxBufferDescriptor->xDataLength = xPacketSize;

							/* The event about to be sent to the TCP/IP is an Rx event. */
							xRxEvent.eEventType = eNetworkRxEvent;

							/* pvData is used to point to the network buffer descriptor that references the received data. */
							xRxEvent.pvData = (void *) pxBufferDescriptor;

							/* Átadjuk feldolgozásra a kapcsott csomagot */
							if(xSendEventStructToIPTask(&xRxEvent, 0) == pdFALSE)
							{
								/* Nem sikerült átadni a puffert, ezért felszabadítjuk*/
								vReleaseNetworkBufferAndDescriptor(pxBufferDescriptor);

								/* És logoljuk az eseményt.. */
								iptraceETHERNET_RX_EVENT_LOST();
							}
							else
							{
								/* The message was successfully sent to the TCP/IP stack.
								Call the standard trace macro to log the occurrence. */
								iptraceNETWORK_INTERFACE_RECEIVE();
							}
						}
						else
						{
							FreeRTOS_debug_printf(("EMACRX: No more free pxBufferDescriptor.\n\r"));
						}
					}
					else
					{
						if(xEMACDriverLoggingLevel > 0)FreeRTOS_debug_printf(("EMACRX: BD %p dropped, RXHP: %p\r\n", pxCurrentBufferDescriptor, HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER))));
					}

					/* Aktuális BD felszabadítása */
					pxCurrentBufferDescriptor->bufoff_len = EMACSwizzleData(ipTOTAL_ETHERNET_FRAME_SIZE);
					pxCurrentBufferDescriptor->flags_pktlen = EMACSwizzleData(EMAC_BUF_DESC_OWNER);
					pxCurrentBufferDescTemp = (emac_rx_bd_t *)EMACSwizzleData((uint32_t)pxCurrentBufferDescriptor->next);
					pxCurrentBufferDescriptor->next = NULL;

					/* Jelezzük a Threshold mehanizmus számára, hogy felszabadult egy puffer. */
					if(HWREG(hdkif->emac_base + EMAC_RXFREEBUFFER(EMAC_CHANNELNUMBER)) < ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS)HWREG(hdkif->emac_base + EMAC_RXFREEBUFFER(EMAC_CHANNELNUMBER)) = 1;

					/* A láncolt lista végét frissítjük az épp felszabadított BD címével */
					pxTailBufferDescriptor->next = (emac_rx_bd_t *)EMACSwizzleData((uint32_t)pxCurrentBufferDescriptor);

					/* Ellenõrízzük, hogy idõközben nem használta e fel az EMAC a lánc végét is, ebben az esetben az EOQ bit be van állítva */
					if((EMACSwizzleData(pxTailBufferDescriptor->flags_pktlen) & EMAC_BUF_DESC_EOQ) == EMAC_BUF_DESC_EOQ)
					{
						while(HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER)) != 0);
						HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER)) = (uint32_t)pxCurrentBufferDescriptor;
						if(xEMACDriverLoggingLevel > 0)FreeRTOS_debug_printf(("EMACRX: RX restarted at BD: %p\r\n", pxCurrentBufferDescriptor));
					}
					pxTailBufferDescriptor = pxCurrentBufferDescriptor;
					pxCurrentBufferDescriptor = pxCurrentBufferDescTemp;
				}
				else
				{
					break;
				}
			}
		}
		else
		{
			/* Nem kaptunk szemafor-t az adott blocking time-on belül, de head pointer tovább lépett az utolsó ellenõrzés óta -> IRQ elveszett */
			if(HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER)) == 0)
			{
				/* Ebben az esetben újraindítjuk az EMAC vételt a head pointer írásával */
				HWREG(hdkif->emac_base + EMAC_RXHDP(EMAC_CHANNELNUMBER)) = (uint32_t)pxCurrentBufferDescriptor;
				if(xEMACDriverLoggingLevel > 0)FreeRTOS_debug_printf(("EMACRX: RX restarted at BD: %p\r\n", pxCurrentBufferDescriptor));
			}
		}
    }
}

/** ***************************************************************************************************
 * @fn		uint32 xFreeRTOSEMACHWInit(uint8_t macaddr[6U])
 * @brief   Low level function for Initializes the EMAC module for transmission and reception.
 * @param   macaddr MAC Address of the Module.
 * @return  EMAC_ERR_OK if everything gets initialized
 *          EMAC_ERR_CONN in case of an error in connecting.
 */
uint32 xFreeRTOSEMACHWInit(uint8_t macaddr[6U])
{
	uint32_t i;
	uint32 xPhyIdReadCount = 0U;
	volatile uint32 xPhyId = 0U;
	volatile uint32 phyLinkRetries = 0xFFFFU;
	uint32 xReturn = EMAC_ERR_OK;

	hdkif_t *hdkif;
	hdkif = &hdkif_data[0U];

	/* A hdkif struktúra inicializálása */
	EMACInstConfig(hdkif);

	/* MAC address átmásolása a hdkif struktúrába */
	for(i=0;i<EMAC_HWADDR_LEN;i++)hdkif->mac_addr[i] = macaddr[(EMAC_HWADDR_LEN - 1U) - i];

	/* Az EMAC és EMAC control modul inicializálása. */

	/* Soft reset EMAC control modul. (Törli az irq státusz, control regiszterek, és a CPPI ram tartalmát) */
    HWREG(hdkif->emac_ctrl_base + EMAC_CTRL_SOFTRESET) = EMAC_CONTROL_RESET;
    while((HWREG(hdkif->emac_ctrl_base + EMAC_CTRL_SOFTRESET) & EMAC_CONTROL_RESET) == EMAC_CONTROL_RESET);
    HWREG(hdkif->emac_base + EMAC_SOFTRESET) = EMAC_SOFT_RESET;
    while((HWREG(hdkif->emac_base + EMAC_SOFTRESET) & EMAC_SOFT_RESET) == EMAC_SOFT_RESET);

    HWREG(hdkif->emac_base + EMAC_MACCONTROL)= 0U;
    HWREG(hdkif->emac_base + EMAC_RXCONTROL)= 0U;
    HWREG(hdkif->emac_base + EMAC_TXCONTROL)= 0U;

    /* Head, Completion pointerek nullázása az összes csatornánál. */
    for(i=0;i<EMAC_MAX_HEADER_DESC;i++)
    {
        HWREG(hdkif->emac_base + EMAC_RXHDP(i)) = 0U;
        HWREG(hdkif->emac_base + EMAC_TXHDP(i)) = 0U;
        HWREG(hdkif->emac_base + EMAC_RXCP(i)) = 0U;
        HWREG(hdkif->emac_base + EMAC_TXCP(i)) = 0U;
    }

    /* Flow control használatához szükséges regiszterek beállítása csak a használt csatornán. */
    HWREG(hdkif->emac_base + EMAC_RXFREEBUFFER(EMAC_CHANNELNUMBER)) = ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS;
    HWREG(hdkif->emac_base + EMAC_RXFLOWTHRESH(EMAC_CHANNELNUMBER)) &= (0x0U);
    HWREG(hdkif->emac_base + EMAC_RXFLOWTHRESH(EMAC_CHANNELNUMBER)) |= ipconfigRX_FLOWCONTROL_START_LEVEL;
	#if(ipconfigETHERNET_DRIVER_RX_FLOW_CONTROLL == 1)
    HWREG(hdkif->emac_base + EMAC_MACCONTROL) |= EMAC_MACCONTROL_RXBUFFERFLOWEN;			/* Flow control engedélyezése */
	#endif

    /* Valamennyi csatornán tiltjuk a TX/RX megszakításokat */
    HWREG(hdkif->emac_base + EMAC_TXINTMASKCLEAR) = 0xFFU;
    HWREG(hdkif->emac_base + EMAC_RXINTMASKCLEAR) = 0xFFU;

    /* Multicast csomagok vételéhez kellene.. */
    HWREG(hdkif->emac_base + EMAC_MACHASH1) = 0U;
    HWREG(hdkif->emac_base + EMAC_MACHASH2) = 0U;

    /* AZ RX descriptorok SOP mezõjének offset értéke. */
    HWREG(hdkif->emac_base + EMAC_RXBUFFEROFFSET) = 0U;

    /* Az MDIO modul inicializálása, State Machine engedélyezése, clock beállítása. */
	MDIOInit(hdkif->mdio_base, MDIO_FREQ_INPUT, MDIO_FREQ_OUTPUT);

	/* az MDIO init közben van idõ beállítani az EMAC MAC címeket. */
	EMACMACSrcAddrSet(hdkif->emac_base, hdkif->mac_addr);
	for(i=0;i<8U;i++){EMACMACAddrSet(hdkif->emac_base, i, hdkif->mac_addr, EMAC_MACADDR_MATCH);}

	/* PHY ID kiolvasása */
	do
	{
		if((xPhyId = Dp83640IDGet(hdkif->mdio_base,hdkif->phy_addr)) != 0)break;
	}while(xPhyIdReadCount++ < PHY_INIT_ID_READ_MAX_RETRIES);

	if(0U == xPhyId)xReturn = EMAC_ERR_CONNECT; 	/* Hibajelzés, ha 0-t olvastunk ID-nak */


	if((uint32)0U == ((MDIOPhyAliveStatusGet(hdkif->mdio_base) >> hdkif->phy_addr) & (uint32)0x01U))
	{
		xReturn = EMAC_ERR_CONNECT;
	}

	if(!Dp83640LinkStatusGet(hdkif->mdio_base, (uint32)EMAC_PHYADDRESS, (uint32)phyLinkRetries))
	{
		xReturn = EMAC_ERR_CONNECT;
	}

	/* EMAC link UP */
	if(EMACLinkSetup(hdkif) != EMAC_ERR_OK)
	{
		xReturn = EMAC_ERR_CONNECT;
	}

	/* RX és TX Buffer Descriptorok kialakítása */
	prvEmacDMAInit(hdkif);

	EMACMIIEnable(hdkif->emac_base);
	EMACRxBroadCastEnable(hdkif->emac_base, (uint32)EMAC_CHANNELNUMBER);
	EMACRxUnicastSet(hdkif->emac_base, (uint32)EMAC_CHANNELNUMBER);
	EMACDisableLoopback(hdkif->emac_base);

	return xReturn;
}


/** ***************************************************************************************************
 * @fn		static void prvEmacDMAInit(hdkif_t *hdkif)
 * @brief   Creates linked buffer descriptor lists for sending and receiving ethernet frames
 * @param   hdkif   network interface structure
 * @return  None
 */
static void prvEmacDMAInit(hdkif_t *hdkif)
{
      txch_t *pxTxChannelDMA;
      rxch_t *pxRxChannelDMA;
	  volatile emac_rx_bd_t *pxCurrentBD;			/* BD linkelt lista buffer kialakításához az aktuális elem címe */
	  unsigned int i;

	  pxTxChannelDMA = &(hdkif->txchptr);
	  pxRxChannelDMA = &(hdkif->rxchptr);

	  pxCurrentBD = (void *)EMAC_TXDMA_PBUF_START_ADDRESS;
	  pxTxChannelDMA->free_head = (void *)pxCurrentBD;
	  pxTxChannelDMA->next_bd_to_process = (void *)pxCurrentBD;
	  pxTxChannelDMA->active_tail = NULL;

	  /* TX Buffer descriptor láncolt lista kialakítása */
      for(i = 0; i < EMAC_TXDMA_PBUF_ALLOC; i++)
      {
    	  if (i < (EMAC_TXDMA_PBUF_ALLOC - 1))pxCurrentBD->next = (emac_rx_bd_t *)EMACSwizzleData((uint32)(pxCurrentBD + 1));
          else
        	  {
        	  pxCurrentBD->next = (emac_rx_bd_t *)NULL;	/* láncolt lista vége */
        	  }
    	  pxCurrentBD->bufptr = NULL;
    	  pxCurrentBD->bufoff_len = 0;
    	  pxCurrentBD->flags_pktlen = 0;
    	  pxCurrentBD++;
      }

      pxCurrentBD = (void *)EMAC_RXDMA_PBUF_START_ADDRESS;

      /* RX Buffer descriptor láncolt lista kialakítása */
      pxRxChannelDMA->active_head = pxRxChannelDMA->active_tail = pxRxChannelDMA->free_head = pxCurrentBD;
      for(i = 0; i < EMAC_RXDMA_PBUF_ALLOC; i++)
      {
    	  if (i < (EMAC_RXDMA_PBUF_ALLOC - 1))pxCurrentBD->next = (emac_rx_bd_t *)EMACSwizzleData((uint32)(pxCurrentBD + 1));
    	  else pxCurrentBD->next = NULL;	/* Láncolt lista vége */

    	  //TODO: Hibakereséshez malloc() használata
    	  pxCurrentBD->bufptr = EMACSwizzleData((uint32)pvPortMalloc(ipTOTAL_ETHERNET_FRAME_SIZE));

    	  //pxCurrentBD->bufoff_len = EMACSwizzleData(MAX_TRANSFER_UNIT);
    	  pxCurrentBD->bufoff_len = EMACSwizzleData(ipTOTAL_ETHERNET_FRAME_SIZE);
    	  pxCurrentBD->flags_pktlen = EMACSwizzleData(EMAC_BUF_DESC_OWNER);
    	  pxCurrentBD++;
      }

      /* DMA BD maradék terület nullázása */
      for(; i<(SIZE_EMAC_CTRL_RAM) / sizeof(emac_tx_bd_t); i++)
      {
    	  pxCurrentBD->next = NULL;
    	  pxCurrentBD->bufptr = NULL;
    	  pxCurrentBD->bufoff_len = NULL;
    	  pxCurrentBD->flags_pktlen = NULL;
    	  pxCurrentBD++;
      }
}


