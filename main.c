/*
	FreeRTOS V9.0.0 - Copyright (C) 2016 Real Time Engineers Ltd.
	All rights reserved

	VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

	This file is part of the FreeRTOS distribution.

	FreeRTOS is free software; you can redistribute it and/or modify it under
	the terms of the GNU General Public License (version 2) as published by the
	Free Software Foundation >>>> AND MODIFIED BY <<<< the FreeRTOS exception.

	***************************************************************************
	>>!   NOTE: The modification to the GPL is included to allow you to     !<<
	>>!   distribute a combined work that includes FreeRTOS without being   !<<
	>>!   obliged to provide the source code for proprietary components     !<<
	>>!   outside of the FreeRTOS kernel.                                   !<<
	***************************************************************************

	FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE.  Full license text is available on the following
	link: http://www.freertos.org/a00114.html

	***************************************************************************
	 *                                                                       *
	 *    FreeRTOS provides completely free yet professionally developed,    *
	 *    robust, strictly quality controlled, supported, and cross          *
	 *    platform software that is more than just the market leader, it     *
	 *    is the industry's de facto standard.                               *
	 *                                                                       *
	 *    Help yourself get started quickly while simultaneously helping     *
	 *    to support the FreeRTOS project by purchasing a FreeRTOS           *
	 *    tutorial book, reference manual, or both:                          *
	 *    http://www.FreeRTOS.org/Documentation                              *
	 *                                                                       *
	***************************************************************************

	http://www.FreeRTOS.org/FAQHelp.html - Having a problem?  Start by reading
	the FAQ page "My application does not run, what could be wrong?".  Have you
	defined configASSERT()?

	http://www.FreeRTOS.org/support - In return for receiving this top quality
	embedded software for free we request you assist our global community by
	participating in the support forum.

	http://www.FreeRTOS.org/training - Investing in training allows your team to
	be as productive as possible as early as possible.  Now you can receive
	FreeRTOS training directly from Richard Barry, CEO of Real Time Engineers
	Ltd, and the world's leading authority on the world's leading RTOS.

	http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
	including FreeRTOS+Trace - an indispensable productivity tool, a DOS
	compatible FAT file system, and our tiny thread aware UDP/IP stack.

	http://www.FreeRTOS.org/labs - Where new FreeRTOS products go to incubate.
	Come and try FreeRTOS+TCP, our new open source TCP/IP stack for FreeRTOS.

	http://www.OpenRTOS.com - Real Time Engineers ltd. license FreeRTOS to High
	Integrity Systems ltd. to sell under the OpenRTOS brand.  Low cost OpenRTOS
	licenses offer ticketed support, indemnification and commercial middleware.

	http://www.SafeRTOS.com - High Integrity Systems also provide a safety
	engineered and independently SIL3 certified version for use in safety and
	mission critical applications that require provable dependability.

	1 tab == 4 spaces!
*/

/* Standard includes. */
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>

/* Scheduler include files. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Standard demo includes. */
#include "partest.h"
#include "TimerDemo.h"
#include "QueueOverwrite.h"
#include "EventGroupsDemo.h"
#include "IntSemTest.h"

/* Altera library includes. */
#include "alt_timers.h"
#include "alt_clock_manager.h"
#include "alt_interrupt.h"
#include "alt_globaltmr.h"
#include "alt_address_space.h"
#include "mmu_support.h"
#include "cache_support.h"

#include "serial.h"
#include "alt_16550_uart.h"

#include "cyclone_dma.h"
#include "cyclone_emac.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_DHCP.h"
#include "FreeRTOS_tcp_server.h"
#include "NetworkInterface.h"
#if( ipconfigMULTI_INTERFACE != 0 )
	#include "FreeRTOS_Routing.h"
#endif

/* FreeRTOS+FAT includes. */
#include "ff_headers.h"
#include "ff_stdio.h"
#include "ff_ramdisk.h"
#include "ff_sddisk.h"

/* Demo application includes. */
#include "hr_gettime.h"

#include "UDPLoggingPrintf.h"

#include "eventLogging.h"

#if( USE_TELNET != 0 )
	#include "telnet.h"
#endif

#if( USE_IPERF != 0 )
	#include "iperf_task.h"
#endif

/* mainCREATE_SIMPLE_BLINKY_DEMO_ONLY is used to select between two demo
 * applications, as described at the top of this file.
 *
 * When mainCREATE_SIMPLE_BLINKY_DEMO_ONLY is set to 1 the simple blinky example
 * will be run.
 *
 * When mainCREATE_SIMPLE_BLINKY_DEMO_ONLY is set to 0 the comprehensive test
 * and demo application will be run.
 */
#define mainCREATE_SIMPLE_BLINKY_DEMO_ONLY	1

#define mainCOMMAND_TASK_PRIORITY		( tskIDLE_PRIORITY + 1 )
#define mainCOMMAND_TASK_STACK_SIZE		( 1024 )

/* FTP and HTTP servers execute in the TCP server work task. */
#define mainTCP_SERVER_TASK_PRIORITY	( tskIDLE_PRIORITY + 2 )
#define	mainTCP_SERVER_STACK_SIZE		2048

#define mainHAS_RAMDISK					1
#define mainHAS_SDCARD					0

/* Set the following constants to 1 to include the relevant server, or 0 to
exclude the relevant server. */
#define mainCREATE_FTP_SERVER			1
#define mainCREATE_HTTP_SERVER 			0

#define mainRAM_DISK_SECTOR_SIZE		512
#define mainRAM_DISK_SECTORS			( ( 200 * 1024 * 1024 )  / mainRAM_DISK_SECTOR_SIZE )
#define mainRAM_DISK_NAME				"/"
#define mainIO_MANAGER_CACHE_SIZE		( 15UL * mainRAM_DISK_SECTOR_SIZE )

/* Define names that will be used for DNS, LLMNR and NBNS searches. */
#define mainHOST_NAME					"camera"
#define mainDEVICE_NICK_NAME			"camera004f6"

#ifndef ARRAY_SIZE
	#define ARRAY_SIZE(x)	( BaseType_t )( sizeof( x ) / sizeof( x )[ 0 ] )
#endif

/*-----------------------------------------------------------*/

/*
 * Configure the hardware as necessary to run this demo.
 */
static void prvSetupHardware( void );

/*
 * See the comments at the top of this file and above the
 * mainSELECTED_APPLICATION definition.
 */
#if ( mainCREATE_SIMPLE_BLINKY_DEMO_ONLY == 1 )
	extern void main_blinky( void );
#else
	extern void main_full( void );
#endif /* #if mainCREATE_SIMPLE_BLINKY_DEMO_ONLY == 1 */

/* Prototypes for the standard FreeRTOS callback/hook functions implemented
within this file. */
void vApplicationMallocFailedHook( void );
void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName );
void vApplicationTickHook( void );

/*-----------------------------------------------------------*/

static void prvCommandTask( void *pvParameters );
static BaseType_t xTasksAlreadyCreated = pdFALSE;

static void prvTCPServerTask( void *pvParameters );

#warning Take away
void david_test(void);

/* configAPPLICATION_ALLOCATED_HEAP is set to 1 in FreeRTOSConfig.h so the
application can define the array used as the FreeRTOS heap.  This is done so the
heap can be forced into fast internal RAM - useful because the stacks used by
the tasks come from this space. */
//uint8_t ucHeap[ configTOTAL_HEAP_SIZE ] __attribute__ ( ( section( ".oc_ram" ) ) );
/* Allocate the memory for the heap. */
#if( configAPPLICATION_ALLOCATED_HEAP == 1 )
	/* The application writer has already defined the array used for the RTOS
	heap - probably so it can be placed in a special segment or address. */
	uint8_t __attribute__ ( ( aligned( 32 ) ) ) __attribute__ ((section (".ddr_sdram"))) ucHeap[ configTOTAL_HEAP_SIZE ];
#endif

/* FreeRTOS uses its own interrupt handler code.  This code cannot use the array
of handlers defined by the Altera drivers because the array is declared static,
and so not accessible outside of the dirver's source file.  Instead declare an
array for use by the FreeRTOS handler.  See:
http://www.freertos.org/Using-FreeRTOS-on-Cortex-A-Embedded-Processors.html. */
static INT_DISPATCH_t xISRHandlers[ ALT_INT_PROVISION_INT_COUNT ];

/* The default IP and MAC address used by the demo.  The address configuration
defined here will be used if ipconfigUSE_DHCP is 0, or if ipconfigUSE_DHCP is
1 but a DHCP server could not be contacted.  See the online documentation for
more information. */
#define	_A4_	__attribute__ ( ( aligned( 4 ) ) )
/* When not aligned, code might crash because of 32-bit access to const data. */
static _A4_ const uint8_t ucIPAddress[ 4 ] = { configIP_ADDR0, configIP_ADDR1, configIP_ADDR2, configIP_ADDR3 };
static _A4_ const uint8_t ucIPAddress_2[ 4 ] = { configIP_ADDR0, configIP_ADDR1, configIP_ADDR2, configIP_ADDR3 + 1 };
static _A4_ const uint8_t ucNetMask[ 4 ] = { configNET_MASK0, configNET_MASK1, configNET_MASK2, configNET_MASK3 };
static _A4_ const uint8_t ucGatewayAddress[ 4 ] = { configGATEWAY_ADDR0, configGATEWAY_ADDR1, configGATEWAY_ADDR2, configGATEWAY_ADDR3 };
static _A4_ const uint8_t ucDNSServerAddress[ 4 ] = { configDNS_SERVER_ADDR0, configDNS_SERVER_ADDR1, configDNS_SERVER_ADDR2, configDNS_SERVER_ADDR3 };

/* Default MAC address configuration.  The demo creates a virtual network
connection that uses this MAC address by accessing the raw Ethernet data
to and from a real network connection on the host PC.  See the
configNETWORK_INTERFACE_TO_USE definition for information on how to configure
the real network connection to use. */

/* 00:11:22:33:44:49 */
const _A4_ uint8_t ucMACAddress[ 6 ] = { configMAC_ADDR0, configMAC_ADDR1, configMAC_ADDR2, configMAC_ADDR3, configMAC_ADDR4, configMAC_ADDR5 };
const _A4_ uint8_t ucMACAddress_2[ 6 ] = { configMAC_ADDR0, configMAC_ADDR1, configMAC_ADDR2, configMAC_ADDR3, configMAC_ADDR4, configMAC_ADDR5 + 1 };

/* Use by the pseudo random number generator. */
static UBaseType_t ulNextRand;

/* Handle of the task that runs the FTP and HTTP servers. */
static TaskHandle_t xServerWorkTaskHandle = NULL;

static SemaphoreHandle_t xServerSemaphore;

/* Use the time it takes to configure EMAC and Ethernet PHY as a seed for
the randomiser. */
uint64_t ullStartConfigTime;

int verboseLevel;

/* FreeRTOS+FAT disks for the SD card and RAM disk. */
#if( mainHAS_SDCARD != 0 )
	FF_Disk_t *pxSDDisk;
#endif

#if( mainHAS_RAMDISK != 0 )
	FF_Disk_t *pxRAMDisk;
#endif

/* Handle of the task that runs the FTP and HTTP servers. */
static TaskHandle_t xCommandTaskHandle = NULL;

static alt_freq_t ulMPUFrequency;

#if( USE_TELNET != 0 )
	static Telnet_t xTelnet;
	static BaseType_t xTelnetCreated;
#endif

#define RECEIVE_TASK_STACK_SIZE		640
#define RECEIVE_TASK_PRIORITY		2

#warning just debugging
volatile uint32_t int_count[ 5 ];

static void show_emac( void );
void vShowTaskTable( BaseType_t aDoClear );

void memcpy_test(void);

#if( ipconfigMULTI_INTERFACE != 0 )
	NetworkInterface_t *pxCyclone_FillInterfaceDescriptor( BaseType_t xEMACIndex, NetworkInterface_t *pxInterface );
#endif /* ipconfigMULTI_INTERFACE */

/*-----------------------------------------------------------*/

int main( void )
{
#if( ipconfigMULTI_INTERFACE != 0 )
	static NetworkInterface_t xInterfaces[ 1 ];
	static NetworkEndPoint_t xEndPoints[ 2 ];
#endif /* ipconfigMULTI_INTERFACE */
	/* Configure the hardware ready to run the demo. */
	prvSetupHardware();

	xSerialPortInitMinimal( 115200, 128 );

	alt_globaltmr_init();
	alt_globaltmr_start();

	/* Initialise the network interface.

	***NOTE*** Tasks that use the network are created in the network event hook
	when the network is connected and ready for use (see the definition of
	vApplicationIPNetworkEventHook() below).  The address values passed in here
	are used if ipconfigUSE_DHCP is set to 0, or if ipconfigUSE_DHCP is set to 1
	but a DHCP server cannot be	contacted. */

//#warning Re-enable

	ullStartConfigTime = ullGetHighResolutionTime();
#if( ipconfigMULTI_INTERFACE != 0 )
	/* As EMAC1 is used, give index 1 as a parameter. */
	pxCyclone_FillInterfaceDescriptor( 1, &( xInterfaces[ 0 ] ) );

	FreeRTOS_FillEndPoint( &( xEndPoints[ 0 ] ), ucIPAddress, ucNetMask, ucGatewayAddress, ucDNSServerAddress, ucMACAddress );
	FreeRTOS_AddEndPoint( &( xInterfaces[ 0 ] ), &( xEndPoints[ 0 ] ) );

	/* You can modify fields: */
	xEndPoints[ 0 ].bits.bIsDefault = pdTRUE_UNSIGNED;
	xEndPoints[ 0 ].bits.bWantDHCP = pdFALSE_UNSIGNED;

	FreeRTOS_FillEndPoint( &( xEndPoints[ 1 ] ), ucIPAddress_2, ucNetMask, ucGatewayAddress, ucDNSServerAddress, ucMACAddress_2 );
	FreeRTOS_AddEndPoint( &( xInterfaces[ 0 ] ), &( xEndPoints[ 1 ] ) );

	/* You can modify fields: */
	xEndPoints[ 1 ].bits.bIsDefault = pdFALSE_UNSIGNED;
	xEndPoints[ 1 ].bits.bWantDHCP = pdFALSE_UNSIGNED;

	FreeRTOS_IPStart();
#else
	FreeRTOS_IPInit( ucIPAddress, ucNetMask, ucGatewayAddress, ucDNSServerAddress, ucMACAddress );
#endif

	xTaskCreate( prvCommandTask, "Command", mainCOMMAND_TASK_STACK_SIZE, NULL, mainCOMMAND_TASK_PRIORITY, &xCommandTaskHandle );
	/* Custom task for HTTP and FTp servers. */
	xTaskCreate( prvTCPServerTask, "TCPWork", mainTCP_SERVER_STACK_SIZE, NULL, mainTCP_SERVER_TASK_PRIORITY, &xServerWorkTaskHandle );

	#if( USE_LOG_EVENT != 0 )
	{
		iEventLogInit();
	}
	#endif
	/* Start the tasks and timer running. */
	vTaskStartScheduler();

	/* If all is well, the scheduler will now be running, and the following
	line will never be reached.  If the following line does execute, then
	there was either insufficient FreeRTOS heap memory available for the idle
	and/or timer tasks to be created, or vTaskStartScheduler() was called from
	User mode.  See the memory management section on the FreeRTOS web site for
	more details on the FreeRTOS heap http://www.freertos.org/a00111.html.  The
	mode from which main() is called is set in the C start up code and must be
	a privileged mode (not user mode). */
	for( ;; );

	/* Don't expect to reach here. */
	return 0;
}
/*-----------------------------------------------------------*/

	extern ALT_16550_HANDLE_t g_uart0_handle;	// See uart0_support.c

extern BaseType_t xPlusTCPStarted;
	void UartHandler( uint32_t ulICCIAR, void * pvContext )
	{
	}
	extern void emac_show_buffers( void );

	static void prvTCPServerTask( void *pvParameters )
	{
	const TickType_t xBlockTime = pdMS_TO_TICKS( 200UL );
	TCPServer_t *pxTCPServer = NULL;
	/* A structure that defines the servers to be created.  Which servers are
	included in the structure depends on the mainCREATE_HTTP_SERVER and
	mainCREATE_FTP_SERVER settings at the top of this file. */
	static const struct xSERVER_CONFIG xServerConfiguration[] =
	{
		#if( mainCREATE_HTTP_SERVER == 1 )
				/* Server type,		port number,	backlog, 	root dir. */
				{ eSERVER_HTTP, 	80, 			12, 		configHTTP_ROOT },
		#endif

		#if( mainCREATE_FTP_SERVER == 1 )
				/* Server type,		port number,	backlog, 	root dir. */
				{ eSERVER_FTP,  	21, 			12, 		"" }
		#endif
	};
#if( mainHAS_RAMDISK != 0 )
	/* Declare the disk space as a static character array. */
	static uint8_t ucRAMDisk[ mainRAM_DISK_SECTORS * mainRAM_DISK_SECTOR_SIZE ]
		__attribute__ ( ( aligned( 32 ) ) );
#endif

		/* Wait for the network to come up. */
		ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

		#if( mainHAS_RAMDISK != 0 )
		{
			FreeRTOS_printf( ( "Create RAM-disk\n" ) );
			/* Create the RAM disk. */
			pxRAMDisk = FF_RAMDiskInit( mainRAM_DISK_NAME, ucRAMDisk, mainRAM_DISK_SECTORS, mainIO_MANAGER_CACHE_SIZE );
			configASSERT( pxRAMDisk );

			/* Print out information on the RAM disk. */
			FF_RAMDiskShowPartition( pxRAMDisk );
		}
		#endif	/* mainHAS_RAMDISK */
		#if( mainHAS_SDCARD != 0 )
		{
			FreeRTOS_printf( ( "Mount SD-card\n" ) );
			/* Create the SD card disk. */
			pxSDDisk = FF_SDDiskInit( mainSD_CARD_DISK_NAME );
			if( pxSDDisk != NULL )
			{
				FF_IOManager_t *pxIOManager = sddisk_ioman( pxSDDisk );
				uint64_t ullFreeBytes =
					( uint64_t ) pxIOManager->xPartition.ulFreeClusterCount * pxIOManager->xPartition.ulSectorsPerCluster * 512ull;
				FreeRTOS_printf( ( "Volume %-12.12s\n",
					pxIOManager->xPartition.pcVolumeLabel ) );
				FreeRTOS_printf( ( "Free clusters %lu total clusters %lu Free %lu KB\n",
					pxIOManager->xPartition.ulFreeClusterCount,
					pxIOManager->xPartition.ulNumClusters,
					( uint32_t ) ( ullFreeBytes / 1024ull ) ) );
			}
			FreeRTOS_printf( ( "Mount SD-card done\n" ) );
		}
		#endif	/* mainHAS_SDCARD */

		/* Create the servers defined by the xServerConfiguration array above. */
		pxTCPServer = FreeRTOS_CreateTCPServer( xServerConfiguration, sizeof( xServerConfiguration ) / sizeof( xServerConfiguration[ 0 ] ) );
		configASSERT( pxTCPServer );

		for( ;; )
		{
			if( pxTCPServer )
			{
				FreeRTOS_TCPServerWork( pxTCPServer, xBlockTime );
			}
			else
			{
				/* Creation of handler failed, configASSERT() is not
				defined, let it sleep for a minute. */
				vTaskDelay( pdMS_TO_TICKS( 60000ul ) );
			}
		}
	}

	static void prvCommandTask( void *pvParameters )
	{
		xSocket_t xSocket;
		BaseType_t xHadSocket = pdFALSE;
		BaseType_t xLastCount = 0;
		char cLastBuffer[ 16 ];
		char cBuffer[ 128 ];
		BaseType_t xLedValue = 0;
		/* Echo function. */
		Socket_t xClientSocket = NULL;
		BaseType_t wasConnected = pdFALSE;
		struct freertos_sockaddr xAddress;
		struct freertos_sockaddr xLocalAddress;
		struct freertos_sockaddr xEchoServerAddress;
		Socket_t xUDPSocket = NULL;

		xServerSemaphore = xSemaphoreCreateBinary();
		configASSERT( xServerSemaphore != NULL );

		/* Wait until the network is up before creating the servers.  The
		notification is given from the network event hook. */

		ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

		/* The priority of this task can be raised now the disk has been
		initialised. */
		vTaskPrioritySet( NULL, mainCOMMAND_TASK_PRIORITY );

		alt_int_dist_enable( ALT_INT_INTERRUPT_UART0 );
		vRegisterIRQHandler( ALT_INT_INTERRUPT_UART0, UartHandler, NULL );
		alt_int_dist_priority_set( ALT_INT_INTERRUPT_UART0, portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );

		alt_16550_fifo_enable(&g_uart0_handle);
		alt_16550_int_enable_rx(&g_uart0_handle);
		alt_16550_int_enable_tx(&g_uart0_handle);

		for( ;; )
		{
		TickType_t xReceiveTimeOut = pdMS_TO_TICKS( 200 );
		BaseType_t xCount;
		struct freertos_sockaddr xSourceAddress;
		socklen_t xSourceAddressLength = sizeof( xSourceAddress );

			xSemaphoreTake( xServerSemaphore, xReceiveTimeOut );

			if( xClientSocket != NULL )
			{
			int rc;
			uint8_t buffer[ 128 ];
				rc = FreeRTOS_recv( xClientSocket, buffer, sizeof buffer, 0 );
				if( rc != 0 )
				{
					FreeRTOS_printf( ( "FreeRTOS_recv: %d%s\n",
						rc,
						rc == -pdFREERTOS_ERRNO_ENOTCONN ? " ENOTCONN" : "" ) );
				}
				if( !wasConnected && FreeRTOS_issocketconnected( xClientSocket ) )
				{
				const char hello[] = "Hello world\r\n";
				const TickType_t xReceiveTimeOut = pdMS_TO_TICKS( 100 );
				int count;
					FreeRTOS_printf( ( "Socket now connected\n" ) );
					wasConnected = pdTRUE;
					rc = FreeRTOS_send( xClientSocket, hello, sizeof hello - 1, 0 );

					/* Finished using the connected socket, initiate a graceful close:
					FIN, FIN+ACK, ACK. */
					FreeRTOS_shutdown( xClientSocket, FREERTOS_SHUT_RDWR );

					FreeRTOS_setsockopt( xClientSocket, 0, FREERTOS_SO_RCVTIMEO, &xReceiveTimeOut, sizeof( xReceiveTimeOut ) );

					/* Expect FreeRTOS_recv() to return an error once the shutdown is
					complete. */
					for( count = 0; count < 10; count++ )
					{
					int rc;
						rc = FreeRTOS_recv( xClientSocket, buffer, sizeof buffer, 0 );
						if( ( rc < 0 ) && ( rc != -pdFREERTOS_ERRNO_EWOULDBLOCK ) )
						{
							break;
						}

					}
				}
				if( ( rc < 0 ) && ( rc != -pdFREERTOS_ERRNO_EWOULDBLOCK ) )
				{
					FreeRTOS_closesocket( xClientSocket );
					xClientSocket = NULL;
				}
			}
			vSetLED( xLedValue < 3 );
			if( ++xLedValue >= 6 )
			{
				xLedValue = 0;
			}
			xSocket = xLoggingGetSocket();
			{
				if( ( xSocket != NULL ) && ( xHadSocket == pdFALSE ) && ( xTasksAlreadyCreated ) )
				{
					xHadSocket = pdTRUE;
					FreeRTOS_printf( ( "prvCommandTask started\n" ) );
					/* xServerSemaphore will be given to when there is data for xSocket
					and also as soon as there is USB/CDC data. */
					FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_SET_SEMAPHORE, ( void * ) &xServerSemaphore, sizeof( xServerSemaphore ) );
					//FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_RCVTIMEO, &xReceiveTimeOut, sizeof( xReceiveTimeOut ) );
				}
				xCount = 0;

				if( xCount == 0 )
				{
				int rc;
					rc = pcSerialReadLine( cBuffer, sizeof( cBuffer )-1 );
					if( rc > 0 )
					{
						eventLogAdd("Serial INPUT %d", rc);
						xCount = rc;
					}
				}
				#if( USE_TELNET != 0 )
				if( xTelnetCreated == pdFALSE )
				{
					xTelnetCreated = pdTRUE;
					xTelnetCreate( &xTelnet, 23 );
				}

				if( ( xCount == 0 ) && ( xPlusTCPStarted != 0 ) )
				{
				int rc;
					rc = xTelnetRecv( &xTelnet, &xSourceAddress, cBuffer, sizeof( cBuffer ) - 1 );
					if( rc > 0 )
					{
						if( cBuffer[ rc - 2 ] == '\r' )
						{
							cBuffer[ rc - 2 ] = '\n';
							rc--;
						}
						eventLogAdd("Telnet INPUT %d", rc);
						xCount = rc;
					}
				}
				#endif
				if( ( xUDPSocket != NULL ) && ( xUDPSocket != FREERTOS_INVALID_SOCKET ) && ( xCount == 0 ) )
				{
					xCount = FreeRTOS_recvfrom( xUDPSocket, ( void * )cBuffer, sizeof( cBuffer )-1, FREERTOS_MSG_DONTWAIT,
						&xSourceAddress, &xSourceAddressLength );
					if( xCount > 0 )
					{
						eventLogAdd("UDP INPUT %ld", xCount);
						FreeRTOS_sendto( xUDPSocket, (const void *)cBuffer, xCount, 0, &xSourceAddress, sizeof( xSourceAddress ) );
					}
				}

				if( ( xSocket != NULL ) && ( xCount == 0 ) )
				{
					xCount = FreeRTOS_recvfrom( xSocket, ( void * )cBuffer, sizeof( cBuffer )-1, FREERTOS_MSG_DONTWAIT,
						&xSourceAddress, &xSourceAddressLength );
					if( xCount > 0 )
					{
						eventLogAdd("Ether INPUT %ld", xCount);
					}
				}

				if( ( xCount >= 3 ) && ( xCount <= 5 ) && ( cBuffer[ 0 ] == 0x1b ) && ( cBuffer[ 1 ] == 0x5b ) && ( cBuffer[ 2 ] == 0x41 ) )
				{
					xCount = xLastCount;
					memcpy( cBuffer, cLastBuffer, sizeof cLastBuffer );
				}
				else if( xCount > 0 )
				{
					xLastCount = xCount;
					memcpy( cLastBuffer, cBuffer, sizeof cLastBuffer );
				}

				if( ( xCount > 0 ) && ( cBuffer[ 0 ] >= 32 ) && ( cBuffer[ 0 ] < 0x7F ) )
				{
					cBuffer[ xCount ] = '\0';
					if( cBuffer[ xCount - 1 ]  == '\n' )
					{
						cBuffer[ --xCount ]  = '\0';
					}
					FreeRTOS_printf( ( ">> %s\n", cBuffer ) );
					if( strncmp( cBuffer, "ver", 3 ) == 0 )
					{
					int level;

						if( sscanf( cBuffer + 3, "%d", &level ) == 1 )
						{
							verboseLevel = level;
						}
						lUDPLoggingPrintf( "Verbose level %d\n", verboseLevel );
					}
					if( strncmp( cBuffer, "clock", 5 ) == 0 )
					{
					static uint64_t ullLast;
					static uint32_t ulLast;
					uint64_t ullNow;
					uint32_t ulNow;
						/* The high-resolution timer seems to be reliable.
						The FreeRTOS clock tick seems to tick quite irregularly. */
						ullNow = ullGetHighResolutionTime();
						ulNow = xTaskGetTickCount();;
						lUDPLoggingPrintf( "Cyclone / Tick %6u %6u\n",
							( uint32_t ) ( ( ullNow - ullLast ) / 1000ull ),
							ulNow - ulLast );
						ullLast = ullNow;
						ulLast = ulNow;
					}
					if( strncmp( cBuffer, "led", 3 ) == 0 )
					{
					UBaseType_t ledNr = 0;
						if( ( sscanf( cBuffer+3, "%lu", &ledNr ) > 0 ) && ( ledNr < 4 ) )
						{
							vParTestToggleLED( ledNr );
						}
						lUDPLoggingPrintf( "LED toggle %lu\n", ledNr );
					}
					if( strncmp( cBuffer, "tcplen", 6 ) == 0 )
					{
					extern int tcp_min_rx_buflen;
						int len = tcp_min_rx_buflen;
						if( sscanf( cBuffer + 6, "%d", &len ) > 0 )
						{
							tcp_min_rx_buflen = len;
						}
						lUDPLoggingPrintf( "tcp_min_rx_buflen %d\n", tcp_min_rx_buflen );
					}

					if( strncmp( cBuffer, "udp", 3 ) == 0 )
					{
						char *ptr = cBuffer + 3;
						unsigned u[ 4 ];
						unsigned uPortNr;
						uint8_t ch[ 4 ];
						int msgLength;
						while( isspace( *ptr )) { ptr++; }
						if( sscanf( ptr, "%u%c%u%c%u%c%u%c%u",
							u + 0, ch + 0,
							u + 1, ch + 1,
							u + 2, ch + 2,
							u + 3, ch + 3,
							&uPortNr) >= 9 )
						{
							const TickType_t xReceiveTimeOut = pdMS_TO_TICKS( 0 );
							const TickType_t xSendTimeOut = pdMS_TO_TICKS( 0 );
							uint32_t ipAddress =
								( u[ 0 ] << 24 ) |
								( u[ 1 ] << 16 ) |
								( u[ 2 ] <<  8 ) |
								( u[ 3 ] <<  0 );
							int spaceCount = 2;
							char wasSpace = 0;
							/* udp 192.168.2.5 2403 Hello world */
							while( *ptr && spaceCount > 0 )
							{
								if( isspace( *ptr ) )
								{
									if( !wasSpace )
									{
										wasSpace = 1;
										spaceCount--;
									}
								}
								else
								{
									wasSpace = 0;
								}
								ptr++;
							}
							msgLength = strlen( ptr );
							lUDPLoggingPrintf( "UDP %lxip port %u msg '%s'\n", ipAddress, uPortNr, ptr );
							if( msgLength > 0 )
							{
								if( xUDPSocket == NULL )
								{
									xUDPSocket= FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
									if( xUDPSocket == FREERTOS_INVALID_SOCKET)
									{
										lUDPLoggingPrintf( "Creation UDP socket failed.\n" );
									}
									else
									{
									BaseType_t xSendTimeOut = 200;
									struct freertos_sockaddr xServer;
										/* Set to non-blocking sends with a timeout of zero as the socket might
										also be used for debug prints which should not block. */
										FreeRTOS_setsockopt( xUDPSocket, 0, FREERTOS_SO_SNDTIMEO, &xSendTimeOut, sizeof( xSendTimeOut ) );
										FreeRTOS_setsockopt( xUDPSocket, 0, FREERTOS_SO_SET_SEMAPHORE, ( void * ) &xServerSemaphore, sizeof( xServerSemaphore ) );
										/* Zero out the server structure. */
										memset( ( void * ) &xServer, 0x00, sizeof( xServer ) );

										/* Set family and port. */
										xServer.sin_len = sizeof( xServer );		/* length of this structure. */
										xServer.sin_family = FREERTOS_AF_INET;
										xServer.sin_port = FreeRTOS_htons( uPortNr );

										/* Bind the address to the socket. */
										if( FreeRTOS_bind( xUDPSocket, &xServer, sizeof( xServer ) ) == -1 )
										{
											FreeRTOS_closesocket( xUDPSocket );
											xUDPSocket = FREERTOS_INVALID_SOCKET;
											lUDPLoggingPrintf( "Bind UDP socket failed.\n" );
										}
									}
								}
								if( ( xUDPSocket != NULL ) && ( xUDPSocket != FREERTOS_INVALID_SOCKET ) )
								{
								struct freertos_sockaddr xAddress;
									memset( &xAddress, '\0', sizeof xAddress );

									xAddress.sin_len = sizeof( xAddress );		/* length of this structure. */
									xAddress.sin_family = FREERTOS_AF_INET;
									xAddress.sin_port = FreeRTOS_htons( uPortNr );
									xAddress.sin_addr = FreeRTOS_htonl( ipAddress );
									FreeRTOS_sendto( xUDPSocket, (const void *)ptr, msgLength, 0, &xAddress, sizeof( xAddress ) );
								}
							}
						}
					}

					if( strncmp( cBuffer, "echo", 4 ) == 0 )
					{
						char *ptr = cBuffer + 4;
						while( isspace( *ptr ) ) ptr++;
						unsigned u[ 4 ];
						unsigned portNr;
						char ch[ 4 ];
						int rc;
						if( sscanf( ptr, "%u%c%u%c%u%c%u%c%u",
							u + 0, ch + 0,
							u + 1, ch + 1,
							u + 2, ch + 2,
							u + 3, ch + 3,
							&portNr) >= 9 )
						{
							const TickType_t xReceiveTimeOut = pdMS_TO_TICKS( 0 );
							const TickType_t xSendTimeOut = pdMS_TO_TICKS( 0 );
							uint32_t ipAddress =
								( u[ 0 ] << 24 ) |
								( u[ 1 ] << 16 ) |
								( u[ 2 ] <<  8 ) |
								( u[ 3 ] <<  0 );
							lUDPLoggingPrintf( "Connect to %lxip port %u\n",
								ipAddress, portNr);

							/* Create a TCP socket. */
							xClientSocket = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_STREAM, FREERTOS_IPPROTO_TCP );
							configASSERT( xClientSocket != FREERTOS_INVALID_SOCKET );
							wasConnected = pdFALSE;

							memset( &xAddress, '\0', sizeof xAddress );
							memset( &xEchoServerAddress, '\0', sizeof xEchoServerAddress );

							xEchoServerAddress.sin_addr = FreeRTOS_htonl( ipAddress );
							xEchoServerAddress.sin_port = FreeRTOS_htons( portNr );

							FreeRTOS_bind( xClientSocket, &xAddress, sizeof xAddress );

							/* Set a time out so a missing reply does not cause the task to block
							indefinitely. */
							FreeRTOS_setsockopt( xClientSocket, 0, FREERTOS_SO_RCVTIMEO, &xReceiveTimeOut, sizeof( xReceiveTimeOut ) );
							FreeRTOS_setsockopt( xClientSocket, 0, FREERTOS_SO_SNDTIMEO, &xSendTimeOut, sizeof( xSendTimeOut ) );
							FreeRTOS_setsockopt( xClientSocket, 0, FREERTOS_SO_SET_SEMAPHORE, ( void * ) &xServerSemaphore, sizeof( xServerSemaphore ) );

							#if( ipconfigUSE_TCP_WIN == 1 )
							{
							WinProperties_t xWinProps;

								/* Fill in the buffer and window sizes that will be used by the socket. */
								xWinProps.lTxBufSize = 4 * ipconfigTCP_MSS;
								xWinProps.lTxWinSize = 2;
								xWinProps.lRxBufSize = 4 * ipconfigTCP_MSS;
								xWinProps.lRxWinSize = 2;
								/* Set the window and buffer sizes. */
								FreeRTOS_setsockopt( xClientSocket, 0, FREERTOS_SO_WIN_PROPERTIES, ( void * ) &xWinProps,	sizeof( xWinProps ) );
							}
							#endif /* ipconfigUSE_TCP_WIN */

							FreeRTOS_GetLocalAddress( xClientSocket, &xLocalAddress );
							/* Connect to the echo server. */
							rc = FreeRTOS_connect( xClientSocket, &xEchoServerAddress, sizeof( xEchoServerAddress ) );

							FreeRTOS_printf( ( "FreeRTOS_connect %lxip:%u to %lxip:%u: rc %d%s\n",
								FreeRTOS_ntohl( xLocalAddress.sin_addr ),
								FreeRTOS_ntohs( xLocalAddress.sin_port ),
								FreeRTOS_ntohl( xEchoServerAddress.sin_addr ),
								FreeRTOS_ntohs( xEchoServerAddress.sin_port ),
								rc,
								rc == -pdFREERTOS_ERRNO_EWOULDBLOCK ? " EWOULDBLOCK" : "" ) );
						}
						else
						{
							lUDPLoggingPrintf( "Usage: echo <ip-address> <port nr>\n" );
						}
					}
					if( strncmp( cBuffer, "freq", 4 ) == 0 )
					{
					alt_freq_t ulCurFreq;
						alt_clk_freq_get( ALT_CLK_MPU_PERIPH, &ulCurFreq );
						lUDPLoggingPrintf( "MPU freq %lu Mhz ( now %lu MHz )\n", ulMPUFrequency / 1000000ul, ulCurFreq / 1000000ul );
					}
					if( strncmp( cBuffer, "memtest", 7 ) == 0 )
					{
						memcpy_test ();
					}
					if( strncmp( cBuffer, "ints", 4 ) == 0 )
					{
						lUDPLoggingPrintf( "int_count[] = %5u %5u %5u %5u %5u\n",
								int_count[0],
								int_count[1],
								int_count[2],
								int_count[3],
								int_count[4]);
					}
					if( strncmp( cBuffer, "emacreg", 7 ) == 0 )
					{
						show_emac();
					}
					if( strncmp( cBuffer, "emacbuf", 7 ) == 0 )
					{
						emac_show_buffers();
					}
					#if( USE_IPERF != 0 )
					{
						if( strncmp( cBuffer, "iperf", 5 ) == 0 )
						{
							vIPerfInstall();
						}
					}
					#endif
					if( strncmp( cBuffer, "david", 5 ) == 0 )
					{
						david_test();
					}
					#if( USE_IPERF != 0 )
					{
						if( strncmp( cBuffer, "iperf", 5 ) == 0 )
						{
							vIPerfInstall();
						}
					}
					#endif
					if( strncmp( cBuffer, "list", 4 ) == 0 )
					{
						vShowTaskTable( cBuffer[ 4 ] == 'c' );
					}

					if( strncmp( cBuffer, "netstat", 7 ) == 0 )
					{
						FreeRTOS_netstat();
					}
					#if( USE_LOG_EVENT != 0 )
					{
						if( strncmp( cBuffer, "event", 5 ) == 0 )
						{
							if( cBuffer[ 5 ] == 'c' )
							{
								int rc = iEventLogClear();
								lUDPLoggingPrintf( "cleared %d events\n", rc );
							}
							else
							{
								eventLogDump();
							}
						}
					}
					#endif /* USE_LOG_EVENT */
				}	/* if( xCount > 0 ) */
			}	/* if( xSocket != NULL) */
		}
	}

static void prvSetupHardware( void )
{
extern uint8_t __cs3_interrupt_vector;
uint32_t ulSCTLR, ulVectorTable = ( uint32_t ) &__cs3_interrupt_vector;
const uint32_t ulVBit = 13U;

	alt_int_global_init();
	alt_int_cpu_binary_point_set( 0 );

	/* Clear SCTLR.V for low vectors and map the vector table to the beginning
	of the code. */
	__asm( "MRC p15, 0, %0, c1, c0, 0" : "=r" ( ulSCTLR ) );
	ulSCTLR &= ~( 1 << ulVBit );
	__asm( "MCR p15, 0, %0, c1, c0, 0" : : "r" ( ulSCTLR ) );
	__asm( "MCR p15, 0, %0, c12, c0, 0" : : "r" ( ulVectorTable ) );

	// Try this
	alt_int_global_enable_all();

	cache_init();
	//mmu_init();

	/* GPIO for LEDs.  ParTest is a historic name which used to stand for
	parallel port test. */
	vParTestInitialise();
}

/*-----------------------------------------------------------*/

void vConfigureTickInterrupt( void )
{
const alt_freq_t ulMicroSecondsPerSecond = 1000000UL;
void FreeRTOS_Tick_Handler( void );

	/* Interrupts are disabled when this function is called. */

	/* Initialise the general purpose timer modules. */
	alt_gpt_all_tmr_init();

	/* ALT_CLK_MPU_PERIPH = mpu_periph_clk */
	alt_clk_freq_get( ALT_CLK_MPU_PERIPH, &ulMPUFrequency );

	/* Use the local private timer. */
	alt_gpt_counter_set( ALT_GPT_CPU_PRIVATE_TMR, ulMPUFrequency / configTICK_RATE_HZ );

	/* Sanity check. */
	configASSERT( alt_gpt_time_microsecs_get( ALT_GPT_CPU_PRIVATE_TMR ) == ( ulMicroSecondsPerSecond / configTICK_RATE_HZ ) );

	/* Set to periodic mode. */
	alt_gpt_mode_set( ALT_GPT_CPU_PRIVATE_TMR, ALT_GPT_RESTART_MODE_PERIODIC );

	/* The timer can be started here as interrupts are disabled. */
	alt_gpt_tmr_start( ALT_GPT_CPU_PRIVATE_TMR );

	/* Register the standard FreeRTOS Cortex-A tick handler as the timer's
	interrupt handler.  The handler clears the interrupt using the
	configCLEAR_TICK_INTERRUPT() macro, which is defined in FreeRTOSConfig.h. */
	vRegisterIRQHandler( ALT_INT_INTERRUPT_PPI_TIMER_PRIVATE, ( alt_int_callback_t ) FreeRTOS_Tick_Handler, NULL );

	/* This tick interrupt must run at the lowest priority. */
	alt_int_dist_priority_set( ALT_INT_INTERRUPT_PPI_TIMER_PRIVATE, portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT );

	/* Ensure the interrupt is forwarded to the CPU. */
	alt_int_dist_enable( ALT_INT_INTERRUPT_PPI_TIMER_PRIVATE );

	/* Finally, enable the interrupt. */
	alt_gpt_int_clear_pending( ALT_GPT_CPU_PRIVATE_TMR );
	alt_gpt_int_enable( ALT_GPT_CPU_PRIVATE_TMR );

}
/*-----------------------------------------------------------*/

void vRegisterIRQHandler( uint32_t ulID, alt_int_callback_t pxHandlerFunction, void *pvContext )
{
	if( ulID < ALT_INT_PROVISION_INT_COUNT )
	{
		xISRHandlers[ ulID ].pxISR = pxHandlerFunction;
		xISRHandlers[ ulID ].pvContext = pvContext;
	}
}
/*-----------------------------------------------------------*/

void vApplicationIRQHandler( uint32_t ulICCIAR )
{
uint32_t ulInterruptID;
void *pvContext;
alt_int_callback_t pxISR;
int IRQ_Ok = pdFALSE;
	/* Re-enable interrupts. */
	__asm ( "cpsie i" );

	/* The ID of the interrupt is obtained by bitwise anding the ICCIAR value
	with 0x3FF. */
	ulInterruptID = ulICCIAR & 0x3FFUL;

	switch( ulInterruptID )
	{
	case ALT_INT_INTERRUPT_EMAC1_IRQ:
		int_count[0]++;
		IRQ_Ok = pdTRUE;
		/* Do not call the handler, because it does not yet use the fromISR() API's */
		break;
	case ALT_INT_INTERRUPT_PPI_TIMER_PRIVATE:
		int_count[1]++;
		IRQ_Ok = pdTRUE;
		break;
	case 0x3FFUL:
		int_count[2]++;
		break;
	case ALT_INT_INTERRUPT_RAM_ECC_CORRECTED_IRQ:
		int_count[3]++;
		break;
	default:
		int_count[4]++;
		break;
	}

	if( IRQ_Ok != pdFALSE )
	{
		/* Call the function installed in the array of installed handler
		functions. */
		pxISR = xISRHandlers[ ulInterruptID ].pxISR;
		pvContext = xISRHandlers[ ulInterruptID ].pvContext;
		pxISR( ulICCIAR, pvContext );
	}
}
/*-----------------------------------------------------------*/

/* Called by FreeRTOS+TCP when the network connects or disconnects.  Disconnect
events are only received if implemented in the MAC driver. */

/* Called by FreeRTOS+TCP when the network connects or disconnects.  Disconnect
events are only received if implemented in the MAC driver. */
extern void vNetworkSocketsInit( void );
#if( ipconfigMULTI_INTERFACE != 0 )
	void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent, NetworkEndPoint_t *pxEndPoint )
#else
	void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent )
#endif
{

	/* If the network has just come up...*/
	if( eNetworkEvent == eNetworkUp )
	{
		{
		uint64_t ullDiff = ullGetHighResolutionTime() - ullStartConfigTime;
		uint32_t ulRandomNumber = ( uint32_t ) ( ( ullDiff ^ ( ullDiff << 8 ) ) & 0xffffffffull );
			FreeRTOS_printf( ( "Network initialisation took %lu mS srand( 0x%08lX )\n",
				( uint32_t )( ( ullDiff + 500ull ) / 1000ull ), ulRandomNumber ) );
			vSRand( ulRandomNumber );
			vNetworkSocketsInit();
		}
		/* Create the tasks that use the IP stack if they have not already been
		created. */
		if( xTasksAlreadyCreated == pdFALSE )
		{
			/* Tasks that use the TCP/IP stack can be created here. */

			#if( mainCREATE_TCP_ECHO_TASKS_SINGLE == 1 )
			{
				/* See http://www.freertos.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/TCP_Echo_Clients.html */
				vStartTCPEchoClientTasks_SingleTasks( configMINIMAL_STACK_SIZE * 4, tskIDLE_PRIORITY + 1 );
			}
			#endif

			#if( mainCREATE_SIMPLE_TCP_ECHO_SERVER == 1 )
			{
				/* See http://www.freertos.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/TCP_Echo_Server.html */
				vStartSimpleTCPServerTasks( configMINIMAL_STACK_SIZE * 4, tskIDLE_PRIORITY + 1 );
			}
			#endif


			#if( ( mainCREATE_FTP_SERVER == 1 ) || ( mainCREATE_HTTP_SERVER == 1 ) )
			{
				/* See TBD.
				Let the server work task now it can now create the servers. */
				xTaskNotifyGive( xServerWorkTaskHandle );
			}
			#endif
			xTaskNotifyGive( xCommandTaskHandle );

			/* Start a new task to fetch logging lines and send them out. */
			vUDPLoggingTaskCreate();

			xTasksAlreadyCreated = pdTRUE;
			xPlusTCPStarted = pdTRUE;
		}

		{
		uint32_t ulIPAddress, ulNetMask, ulGatewayAddress, ulDNSServerAddress;
		char cBuffer[ 16 ];
			/* Print out the network configuration, which may have come from a DHCP
			server. */
			#if( ipconfigMULTI_INTERFACE != 0 )
				FreeRTOS_GetAddressConfiguration( pxEndPoint, &ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress );
			#else
				FreeRTOS_GetAddressConfiguration( &ulIPAddress, &ulNetMask, &ulGatewayAddress, &ulDNSServerAddress );
			#endif
				

			FreeRTOS_inet_ntoa( ulIPAddress, cBuffer );
			FreeRTOS_printf( ( "IP Address: %s\n", cBuffer ) );

			FreeRTOS_inet_ntoa( ulNetMask, cBuffer );
			FreeRTOS_printf( ( "Subnet Mask: %s\n", cBuffer ) );

			FreeRTOS_inet_ntoa( ulGatewayAddress, cBuffer );
			FreeRTOS_printf( ( "Gateway Address: %s\n", cBuffer ) );

			FreeRTOS_inet_ntoa( ulDNSServerAddress, cBuffer );
			FreeRTOS_printf( ( "DNS Server Address: %s\n", cBuffer ) );
		}
	}
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_DHCP_HOOK != 0 )
	eDHCPCallbackAnswer_t xApplicationDHCPHook( eDHCPCallbackPhase_t eDHCPPhase, uint32_t ulIPAddress )
	{
	eDHCPCallbackAnswer_t xResult = eDHCPContinue;
	static BaseType_t xUseDHCP = pdTRUE;

		switch( eDHCPPhase )
		{
		case eDHCPPhasePreDiscover:		/* Driver is about to ask for a DHCP offer. */
			/* Returning eDHCPContinue. */
			break;
		case eDHCPPhasePreRequest:		/* Driver is about to request DHCP an IP address. */
			if( xUseDHCP == pdFALSE )
			{
				xResult = eDHCPStopNoChanges;
			}
			else
			{
				xResult = eDHCPContinue;
			}
			break;
		}
		lUDPLoggingPrintf( "DHCP %s: use = %d: %s\n",
			( eDHCPPhase == eDHCPPhasePreDiscover ) ? "pre-discover" : "pre-request",
			xUseDHCP,
			xResult == eDHCPContinue ? "Continue" : "Stop" );
		return xResult;
	}
#endif	/* ipconfigUSE_DHCP_HOOK */
/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook( void )
{
	/* Called if a call to pvPortMalloc() fails because there is insufficient
	free memory available in the FreeRTOS heap.  pvPortMalloc() is called
	internally by FreeRTOS API functions that create tasks, queues, software
	timers, and semaphores.  The size of the FreeRTOS heap is set by the
	configTOTAL_HEAP_SIZE configuration constant in FreeRTOSConfig.h. */
	vAssertCalled( __FILE__, __LINE__ );
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char *pcTaskName )
{
	( void ) pcTaskName;
	( void ) pxTask;

	/* Run time stack overflow checking is performed if
	configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
	function is called if a stack overflow is detected. */
	taskDISABLE_INTERRUPTS();
	for( ;; );
}
/*-----------------------------------------------------------*/

UBaseType_t uxRand( void )
{
const uint32_t ulMultiplier = 0x015a4e35UL, ulIncrement = 1UL;

	/* Utility function to generate a pseudo random number. */

	ulNextRand = ( ulMultiplier * ulNextRand ) + ulIncrement;
	return( ( int ) ( ulNextRand >> 16UL ) & 0x7fffUL );
}
/*-----------------------------------------------------------*/

void vSRand( UBaseType_t ulSeed )
{
	/* Utility function to seed the pseudo random number generator. */
	ulNextRand = ulSeed;
}
/*-----------------------------------------------------------*/

void vApplicationPingReplyHook( ePingReplyStatus_t eStatus, uint16_t usIdentifier )
{
}
/*-----------------------------------------------------------*/

const char *pcApplicationHostnameHook( void )
{
	/* Assign the name "FreeRTOS" to this network node.  This function will be
	called during the DHCP: the machine will be registered with an IP address
	plus this name. */
	return mainHOST_NAME;
}
/*-----------------------------------------------------------*/

#if( ipconfigMULTI_INTERFACE != 0 )
	BaseType_t xApplicationDNSQueryHook( NetworkEndPoint_t *pxEndPoint, const char *pcName )
#else
	BaseType_t xApplicationDNSQueryHook( const char *pcName )
#endif
{
BaseType_t xReturn;

	/* Determine if a name lookup is for this node.  Two names are given
	to this node: that returned by pcApplicationHostnameHook() and that set
	by mainDEVICE_NICK_NAME. */
	if( strcasecmp( pcName, pcApplicationHostnameHook() ) == 0 )
	{
		xReturn = pdPASS;
	}
	else if( strcasecmp( pcName, mainDEVICE_NICK_NAME ) == 0 )
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

void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
volatile unsigned long ul = 0;

	( void ) pcFile;
	( void ) ulLine;

	taskENTER_CRITICAL();
	{
		/* Set ul to a non-zero value using the debugger to step out of this
		function. */
		while( ul == 0 )
		{
			portNOP();
		}
	}
	taskEXIT_CRITICAL();
}
/*-----------------------------------------------------------*/

void vApplicationTickHook( void )
{
	#if( mainCREATE_SIMPLE_BLINKY_DEMO_ONLY == 0 )
	{
		/* The full demo includes a software timer demo/test that requires
		prodding periodically from the tick interrupt. */
		vTimerPeriodicISRTests();

		/* Call the periodic queue overwrite from ISR demo. */
		vQueueOverwritePeriodicISRDemo();

		/* Call the periodic event group from ISR demo. */
		vPeriodicEventGroupsProcessing();

		/* Call the periodic test that uses mutexes form an interrupt. */
		vInterruptSemaphorePeriodicTest();
	}
	#endif
}
/*-----------------------------------------------------------*/

extern void vOutputChar( const char cChar, const TickType_t xTicksToWait  );
void vOutputChar( const char cChar, const TickType_t xTicksToWait  )
{
	/* printf-stdarg.c needs this function. */
}

#define KILO_BYTE						( 1024u )
#define MEGA_BYTE						( KILO_BYTE * KILO_BYTE )

#define BOOT_ROM_ORIGIN					( 0xfffd0000u )
#define BOOT_ROM_LENGTH					( 64u * KILO_BYTE )

#define ON_CHIP_RAM_ORIGIN				( 0xffff0000u )
#define ON_CHIP_RAM_LENGTH				( 64u * KILO_BYTE )

#define SD_RAM_ORIGIN					( 0x00100000u )
#define SD_RAM_LENGTH					( 1023u * MEGA_BYTE )

#define IN_RANGE( ADDR, START, LEN )	( ( ADDR >= START ) && ( ADDR <= START + ( LEN - 1 ) ) )

//  boot_rom (rx) : ORIGIN = 0xfffd0000, LENGTH = 64K
//  oc_ram (rwx) : ORIGIN = 0xffff0000, LENGTH = 64K
//  ram (rwx) : ORIGIN = 0x100000, LENGTH = 1023M

extern BaseType_t xApplicationMemoryPermissions( uint32_t aAddress );
BaseType_t xApplicationMemoryPermissions( uint32_t aAddress )
{
BaseType_t xReturn;

	if( IN_RANGE( aAddress, BOOT_ROM_ORIGIN, BOOT_ROM_LENGTH ) )
	{
		xReturn = 1;
	}
	else if( IN_RANGE( aAddress, SD_RAM_ORIGIN, SD_RAM_LENGTH ) )
	{
		xReturn = 3;
	}
	else if( IN_RANGE( aAddress, ON_CHIP_RAM_ORIGIN, ON_CHIP_RAM_LENGTH ) )
	{
		xReturn = 3;
	}
	else
	{
		xReturn = 0;
	}

	return xReturn;
}

int _read(int handle, void *buf, unsigned len)
{
	return 0;
}

int _write(int handle, void *buf, unsigned len)
{
	return len;
}

int _close(int file)
{
	return -1;
}

extern int _end;

caddr_t _sbrk(int incr)
{
	static unsigned char *heap = NULL;
	unsigned char *prev_heap;

	if (heap == NULL) {
		heap = (unsigned char *)&_end;
	}
	prev_heap = heap;

	heap += incr;

	return (caddr_t) prev_heap;
}

int _lseek(int file, int ptr, int dir)
{
	return 0;
}

//	void __attribute__ ( ( interrupt ) ) __cs3_isr_dabort( void )
//	{
//		while(1);
//	}

void __cs3_isr_dabort( void )
{
	vAssertCalled( __FILE__, __LINE__ );
}

void vUDPLoggingHook( const char *pcMessage, BaseType_t xLength )
{
	void *pxPort = NULL;
	vSerialPutString( pxPort, ( const char * )pcMessage, ( unsigned short ) xLength );
	#if( USE_TELNET != 0 )
	{
		if( xTelnetCreated != pdFALSE )
		{
			xTelnetSend( &xTelnet, ( struct freertos_sockaddr * )NULL, pcMessage, xLength );
		}
	}
	#endif
}

uint32_t *pulMAC_Config;

#define EMAC_OFFSET		0x0000u
#define DMA_OFFSET		0x1000u

static void show_emac()
{
const int iMacID = 1;
int index;
uint32_t offset;

	// ======================================================================

	offset = EMAC_OFFSET;

	pulMAC_Config = ( uint32_t * )( ucFirstIOAddres( iMacID ) + offset );

	FreeRTOS_printf( ( "ST GMAC Registers\n" ) );
	FreeRTOS_printf( ( "GMAC Registers at 0x%08X\n", ( uint32_t )pulMAC_Config ) );

	for( index = 0; index < 16/*55*/; index++ )
	{
	uint32_t value;

		if( index >= 9 && index <= 11 ) {
			value = 0;
		} else {
			value = pulMAC_Config[ index ];
		}
		FreeRTOS_printf( ( "Reg%02d  0x%08X\n", index, value ) );
		vTaskDelay( 60 );
	}

	// ======================================================================

	offset = DMA_OFFSET;

	pulMAC_Config = ( uint32_t * )( ucFirstIOAddres( iMacID ) + offset );

	FreeRTOS_printf( ( "\n" ) );
	FreeRTOS_printf( ( "DMA Registers at 0x%08X\n", ( uint32_t )pulMAC_Config ) );

	for( index = 0; index < 22; index++ )
	{
	uint32_t value = pulMAC_Config[ index ];

		FreeRTOS_printf( ( "Reg%02d  0x%08X\n", index, value ) );
		vTaskDelay( 60 );
	}
	// ======================================================================

}

static uint64_t ullHiresTime;

extern uint32_t ulGetRunTimeCounterValue( void );

extern uint32_t ulGetRunTimeCounterValue(void);
extern void vStartRunTimeCounter(void)
{
}

uint32_t ulGetRunTimeCounterValue( void )
{
	return ( uint32_t ) ( ullGetHighResolutionTime() - ullHiresTime );
}

extern BaseType_t xTaskClearCounters;
void vShowTaskTable( BaseType_t aDoClear )
{
TaskStatus_t *pxTaskStatusArray;
volatile UBaseType_t uxArraySize, x;
uint64_t ullTotalRunTime;
uint32_t ulStatsAsPermille;

	// Take a snapshot of the number of tasks in case it changes while this
	// function is executing.
	uxArraySize = uxTaskGetNumberOfTasks();

	// Allocate a TaskStatus_t structure for each task.  An array could be
	// allocated statically at compile time.
	pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) );

	FreeRTOS_printf( ( "Task name    Prio    Stack  Switches   Time(mS)    Avg(uS) Perc \n" ) );

	if( pxTaskStatusArray != NULL )
	{
		// Generate raw status information about each task.
		uint32_t ulDummy;
		xTaskClearCounters = aDoClear;
		uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, &ulDummy );

		ullTotalRunTime = ullGetHighResolutionTime() - ullHiresTime;

		// For percentage calculations.
		ullTotalRunTime /= 1000UL;

		// Avoid divide by zero errors.
		if( ullTotalRunTime > 0ull )
		{
			// For each populated position in the pxTaskStatusArray array,
			// format the raw data as human readable ASCII data
			for( x = 0; x < uxArraySize; x++ )
			{
			uint32_t avg_time = 0;
			if( pxTaskStatusArray[ x ].ulSwitchCounter != 0u )
			{
				avg_time = pxTaskStatusArray[ x ].ulRunTimeCounter / pxTaskStatusArray[ x ].ulSwitchCounter;
			}
				// What percentage of the total run time has the task used?
				// This will always be rounded down to the nearest integer.
				// ulTotalRunTimeDiv100 has already been divided by 100.
				ulStatsAsPermille = pxTaskStatusArray[ x ].ulRunTimeCounter / ullTotalRunTime;
/*
Task name    Prio    Stack    Switches Time(mS)    Perc 
Command         1      862          32 3007.650   17.3 %
IDLE            0      116          53 9620.438   55.4 %
IP-task         5      825          29 2007.052   11.5 %
LogTask         2      349          18  942.456    5.4 %
EMAC            6      505          23 1787.474   10.2 %
*/
				FreeRTOS_printf( ( "%-14.14s %2lu %8u %9lu %6lu.%03lu %8lu %3lu.%lu %%\n",
					pxTaskStatusArray[ x ].pcTaskName,
					pxTaskStatusArray[ x ].uxCurrentPriority,
					pxTaskStatusArray[ x ].usStackHighWaterMark,
					pxTaskStatusArray[ x ].ulSwitchCounter,
					pxTaskStatusArray[ x ].ulRunTimeCounter / 1000u,
					pxTaskStatusArray[ x ].ulRunTimeCounter % 1000u,
					avg_time,
					ulStatsAsPermille / 10,
					ulStatsAsPermille % 10) );
			}
		}

		// The array is no longer needed, free the memory it consumes.
		vPortFree( pxTaskStatusArray );
	}

	FreeRTOS_printf( ( "Heap: min/cur/max: %lu %lu %lu\n",
			( uint32_t )xPortGetMinimumEverFreeHeapSize(),
			( uint32_t )xPortGetFreeHeapSize(),
			configTOTAL_HEAP_SIZE) );
	if( aDoClear != pdFALSE )
	{
//		ulListTime = xTaskGetTickCount();
		ullHiresTime = ullGetHighResolutionTime();
	}
}
/*-----------------------------------------------------------*/

#define ipMAC_ADDR_LENGTH_BYTES   6

struct MAC_PACKED
{
	uint8_t ucBytes[ ipMAC_ADDR_LENGTH_BYTES ];
} __attribute__( (packed) );

struct MAC_PACKED_ARRAY
{
	struct MAC_PACKED mac1, mac2, mac3;
	uint32_t start;
};

struct MAC_PACKED_ARRAY_PACKED
{
	struct MAC_PACKED mac1, mac2, mac3;
	uint32_t start;
} __attribute__( (packed) );

struct MAC_NORMAL
{
	uint8_t ucBytes[ ipMAC_ADDR_LENGTH_BYTES ];
};

struct MAC_PACKET_NORMAL
{
	struct MAC_NORMAL mac1, mac2, mac3;
	uint32_t start;
};

struct MAC_PACKET_NORMAL_PACKED
{
	struct MAC_NORMAL mac1, mac2, mac3;
	uint32_t start;
} __attribute__( (packed) );


struct MAC_PACKED				s1;
struct MAC_PACKED_ARRAY			s2;
struct MAC_PACKED_ARRAY_PACKED	s3;
struct MAC_NORMAL				s4;
struct MAC_PACKET_NORMAL		s5;
struct MAC_PACKET_NORMAL_PACKED	s6;

static const struct MAC_PACKED broadcastAddr = { { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } };
#define logPrintf	lUDPLoggingPrintf
static void showMac(const uint8_t *pucBytes)
{
	logPrintf("MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
		pucBytes[0],
		pucBytes[1],
		pucBytes[2],
		pucBytes[3],
		pucBytes[4],
		pucBytes[5]);
}
void david_test(void)
{
	logPrintf("MAC packed struct sizes: %lu %lu %lu\n",	
		sizeof s1,
		sizeof s2,
		sizeof s3);
	logPrintf("MAC normal struct sizes: %lu %lu %lu\n",	
		sizeof s4,
		sizeof s5,
		sizeof s6);
	memcpy (s2.mac1.ucBytes, broadcastAddr.ucBytes, ipMAC_ADDR_LENGTH_BYTES);
	memcpy (s2.mac2.ucBytes, broadcastAddr.ucBytes, ipMAC_ADDR_LENGTH_BYTES);
	memcpy (s2.mac3.ucBytes, broadcastAddr.ucBytes, ipMAC_ADDR_LENGTH_BYTES);
	showMac(s2.mac1.ucBytes);
	showMac(s2.mac2.ucBytes);
	showMac(s2.mac3.ucBytes);

	memcpy (s5.mac1.ucBytes, broadcastAddr.ucBytes, ipMAC_ADDR_LENGTH_BYTES);
	memcpy (s5.mac2.ucBytes, broadcastAddr.ucBytes, ipMAC_ADDR_LENGTH_BYTES);
	memcpy (s5.mac3.ucBytes, broadcastAddr.ucBytes, ipMAC_ADDR_LENGTH_BYTES);
	showMac(s5.mac1.ucBytes);
	showMac(s5.mac2.ucBytes);
	showMac(s5.mac3.ucBytes);
}

/*
static void socfpga_cyclone5_restart(enum reboot_mode mode, const char *cmd)
{
	u32 temp;

	temp = readl(rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);

	if (mode == REBOOT_HARD)
		temp |= RSTMGR_CTRL_SWCOLDRSTREQ;
	else
		temp |= RSTMGR_CTRL_SWWARMRSTREQ;
	writel(temp, rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);
}
*/

/*
S:0x00100020 : DCD      0x00100040	// __cs3_reset
S:0x00100024 : DCD      0x001002B4	// __cs3_isr_undef
S:0x00100028 : DCD      0x00102F60	// __cs3_isr_swi ( FreeRTOS_SWI_Handler )
S:0x0010002C : DCD      0x001002B8	// __cs3_isr_pabort
S:0x00100030 : DCD      0x001002BC	// __cs3_isr_dabort
S:0x00100034 : DCD      0x001002B0	// __cs3_isr_interrupt
S:0x00100038 : DCD      0x00103070	// __cs3_isr_irq ( FreeRTOS_IRQ_Handler )
S:0x0010003C : DCD      0x001002C0	// __cs3_isr_fiq
*/

//void __cs3_reset()
//{
//	vAssertCalled( __FILE__, __LINE__ );
//}

void __cs3_isr_undef()
{
	vAssertCalled( __FILE__, __LINE__ );
}

extern int unknown_function(void);
void __cs3_isr_pabort()
{
	vAssertCalled( __FILE__, __LINE__ );
}

//void __cs3_isr_dabort()
//{
//	vAssertCalled( __FILE__, __LINE__ );
//}

void __cs3_isr_interrupt()
{
	vAssertCalled( __FILE__, __LINE__ );
}

void __cs3_isr_fiq()
{
	vAssertCalled( __FILE__, __LINE__ );
}

char *getTaskName(void);
char *getTaskName()
{
	return pcTaskGetName( ( TaskHandle_t ) NULL );
}

#define MEMCPY_BLOCK_SIZE		2048
#define MEMCPY_EXTA_SIZE		128

struct SMEmcpyData {
	char target_pre [ MEMCPY_EXTA_SIZE  ];
	char target_data[ MEMCPY_BLOCK_SIZE + 16 ];
	char target_post[ MEMCPY_EXTA_SIZE  ];

	char source_pre [ MEMCPY_EXTA_SIZE  ];
	char source_data[ MEMCPY_BLOCK_SIZE + 16 ];
	char source_post[ MEMCPY_EXTA_SIZE  ];
};

typedef void * ( * FMemcpy ) ( void *pvDest, const void *pvSource, size_t ulBytes );
typedef void * ( * FMemset ) ( void *pvDest, int iChar, size_t ulBytes );

void *x_memcpy( void *pvDest, const void *pvSource, size_t ulBytes )
{
	return pvDest;
}

void *x_memset(void *pvDest, int iValue, size_t ulBytes)
{
	return pvDest;
}


#define 	LOOP_COUNT		100

static char __attribute__ ( ( aligned( 32 ) ) ) __attribute__ ((section (".oc_ram"))) oc_memory[ MEMCPY_BLOCK_SIZE + 16 ] ;

void memcpy_test()
{
int target_offset;
int source_offset;
int algorithm;
struct SMEmcpyData *pxBlock;
char *target;
char *source;
uint64_t ullStartTime;
uint32_t ulDelta;
uint32_t ulTimes[ 2 ][ 4 ][ 4 ];
uint32_t ulOcTimes[ 2 ][ 4 ][ 4 ];
uint32_t ulSetTimes[ 2 ][ 4 ];
int time_index = 0;
int index;
uint64_t copy_size = LOOP_COUNT * MEMCPY_BLOCK_SIZE;

	memset( ulTimes, '\0', sizeof ulTimes );
	
	pxBlock = ( struct SMEmcpyData * ) pvPortMalloc( sizeof *pxBlock );
	if( pxBlock == NULL )
	{
		logPrintf( "Failed to allocate %lu bytes\n", sizeof *pxBlock );
		return;
	}
	FMemcpy memcpy_func;
	for( algorithm = 0; algorithm < 2; algorithm++ )
	{
		memcpy_func = algorithm == 0 ? memcpy : x_memcpy;
		for( target_offset = 0; target_offset < 4; target_offset++ ) {
			for( source_offset = 0; source_offset < 4; source_offset++ ) {
				target = pxBlock->target_data + target_offset;
				source = pxBlock->source_data + source_offset;
				ullStartTime = ullGetHighResolutionTime();
				for( index = 0; index < LOOP_COUNT; index++ )
				{
					memcpy_func( target, source, MEMCPY_BLOCK_SIZE );
				}
				ulDelta = ( uint32_t ) ( ullGetHighResolutionTime() - ullStartTime );
				ulTimes[ algorithm ][ target_offset ][ source_offset ] = ulDelta;
			}
		}
	}
	for( algorithm = 0; algorithm < 2; algorithm++ )
	{
		memcpy_func = memcpy;
		for( target_offset = 0; target_offset < 4; target_offset++ ) {
			for( source_offset = 0; source_offset < 4; source_offset++ ) {
				if (algorithm == 0) {
					target = pxBlock->target_data + target_offset;
				} else {
					target = oc_memory + target_offset;
				}
				source = pxBlock->source_data + source_offset;
				ullStartTime = ullGetHighResolutionTime();
				for( index = 0; index < LOOP_COUNT; index++ )
				{
					memcpy_func( target, source, MEMCPY_BLOCK_SIZE );
				}
				ulDelta = ( uint32_t ) ( ullGetHighResolutionTime() - ullStartTime );
				ulOcTimes[ algorithm ][ target_offset ][ source_offset ] = ulDelta;
			}
		}
	}
	FMemset memset_func;
	for( algorithm = 0; algorithm < 2; algorithm++ )
	{
		memset_func = algorithm == 0 ? memset : x_memset;
		for( target_offset = 0; target_offset < 4; target_offset++ ) {
			target = pxBlock->target_data + target_offset;
			ullStartTime = ullGetHighResolutionTime();
			for( index = 0; index < LOOP_COUNT; index++ )
			{
				memset_func( target, '\xff', MEMCPY_BLOCK_SIZE );
			}
			ulDelta = ( uint32_t ) ( ullGetHighResolutionTime() - ullStartTime );
			ulSetTimes[ algorithm ][ target_offset ] = ulDelta;
		}
	}
	logPrintf( "copy_size - %lu ( %lu bits)\n",
		( uint32_t )copy_size, ( uint32_t )(8ull * copy_size) );
	for( target_offset = 0; target_offset < 4; target_offset++ )
	{
		for( source_offset = 0; source_offset < 4; source_offset++ )
		{
			uint32_t ulTime1 = ulTimes[ 0 ][ target_offset ][ source_offset ];
			uint32_t ulTime2 = ulTimes[ 1 ][ target_offset ][ source_offset ];

			uint64_t avg1 = ( copy_size * 1000000ull ) / ulTime1;
			uint64_t avg2 = ( copy_size * 1000000ull ) / ulTime2;

			uint32_t mb1 = ( uint32_t ) ( ( avg1 + 500000ull ) / 1000000ull );
			uint32_t mb2 = ( uint32_t ) ( ( avg2 + 500000ull ) / 1000000ull );

			logPrintf( "Offset[%d,%d] = memcpy %3lu.%03lu ms (%5lu MB/s) x_memcpy %3lu.%03lu ms  (%5lu MB/s)\n",
				target_offset,
				source_offset,
				ulTime1 / 1000ul, ulTime1 % 1000ul,
				mb1,
				ulTime2 / 1000ul, ulTime2 % 1000ul,
				mb2);
		}
	}
	for( target_offset = 0; target_offset < 4; target_offset++ )
	{
		for( source_offset = 0; source_offset < 4; source_offset++ )
		{
			uint32_t ulTime1 = ulOcTimes[ 0 ][ target_offset ][ source_offset ];
			uint32_t ulTime2 = ulOcTimes[ 1 ][ target_offset ][ source_offset ];

			uint64_t avg1 = ( copy_size * 1000000ull ) / ulTime1;
			uint64_t avg2 = ( copy_size * 1000000ull ) / ulTime2;

			uint32_t mb1 = ( uint32_t ) ( ( avg1 + 500000ull ) / 1000000ull );
			uint32_t mb2 = ( uint32_t ) ( ( avg2 + 500000ull ) / 1000000ull );

			logPrintf( "Offset[%d,%d] = SDRAM  %3lu.%03lu ms (%5lu MB/s) OC mem   %3lu.%03lu ms  (%5lu MB/s)\n",
				target_offset,
				source_offset,
				ulTime1 / 1000ul, ulTime1 % 1000ul,
				mb1,
				ulTime2 / 1000ul, ulTime2 % 1000ul,
				mb2);
		}
	}
	for( target_offset = 0; target_offset < 4; target_offset++ )
	{
		uint32_t ulTime1 = ulSetTimes[ 0 ][ target_offset ];
		uint32_t ulTime2 = ulSetTimes[ 1 ][ target_offset ];

		uint64_t avg1 = ( copy_size * 1000000ull ) / ulTime1;
		uint64_t avg2 = ( copy_size * 1000000ull ) / ulTime2;

		uint32_t mb1 = ( uint32_t ) ( ( avg1 + 500000ull ) / 1000000ull );
		uint32_t mb2 = ( uint32_t ) ( ( avg2 + 500000ull ) / 1000000ull );

		logPrintf( "Offset[%d] = memset %3lu.%03lu ms (%5lu MB/s) x_memset %3lu.%03lu ms  (%5lu MB/s)\n",
			target_offset,
			ulTime1 / 1000ul, ulTime1 % 1000ul,
			mb1,
			ulTime2 / 1000ul, ulTime2 % 1000ul,
			mb2);
	}
	vPortFree( ( void * ) pxBlock );
}

