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

#ifndef FREERTOS_ROUTING_H
#define FREERTOS_ROUTING_H

#ifdef __cplusplus
extern "C" {
#endif

#include "FreeRTOS_DHCP.h"

/* Every NetworkInterface needs a set of access functions: */

/* Forward declaration of 'struct xNetworkInterface'. */
struct xNetworkInterface;

/* Initialise the interface. */
typedef BaseType_t ( * NetworkInterfaceInitialiseFunction_t ) (
	struct xNetworkInterface * /* pxDescriptor */ );

/* Send out an Ethernet packet. */
typedef BaseType_t ( * NetworkInterfaceOutputFunction_t ) (
	struct xNetworkInterface * /* pxDescriptor */,
	NetworkBufferDescriptor_t * const /* pxNetworkBuffer */,
	BaseType_t /* xReleaseAfterSend */ );

/* Return true as long as the LinkStatus on the PHY is present. */
typedef BaseType_t ( * GetPhyLinkStatusFunction_t ) (
	struct xNetworkInterface * /* pxDescriptor */ );

/* These NetworkInterface access functions are collected in a struct: */

typedef struct xNetworkInterface
{
	const char *pcName;	/* Just for logging, debugging. */
	void *pvArgument;	/* Will be passed to the access functions. */
	NetworkInterfaceInitialiseFunction_t pfInitialise;
	NetworkInterfaceOutputFunction_t pfOutput;
	GetPhyLinkStatusFunction_t pfGetPhyLinkStatus;
	struct
	{
		uint32_t
			bInterfaceUp : 1;
	} bits;

	struct xNetworkEndPoint *pxEndPoint;
	struct xNetworkInterface *pxNext;
} NetworkInterface_t;

/*
	// As an example:
	NetworkInterface_t xZynqDescriptor = {
		.pcName					= "Zynq-GEM",
		.pvArgument				= ( void * )1,
		.pfInitialise           = xZynqGEMInitialise,
		.pfOutput               = xZynqGEMOutput,
		.pfGetPhyLinkStatus     = xZynqGEMGetPhyLinkStatus,
	};
*/

typedef struct xNetworkEndPoint
{
	uint32_t ulDefaultIPAddress;	/* Use this address in case DHCP has failed. */
#if( ipconfigUSE_IPv6 != 0 )
	IPv6_Address_t ulIPAddress_IPv6;
#endif
	uint32_t ulIPAddress;			/* The actual IPv4 address. Will be 0 as long as end-point is still down. */
	uint32_t ulNetMask;
	uint32_t ulGatewayAddress;
	uint32_t ulDNSServerAddresses[ ipconfigENDPOINT_DNS_ADDRESS_COUNT ];
	uint32_t ulBroadcastAddress;
	MACAddress_t xMACAddress;
	struct
	{
		uint32_t
			bIsDefault : 1,
			bWantDHCP : 1,
			#if( ipconfigUSE_IPv6 != 0 )
				bIPv6 : 1,
			#endif /* ipconfigUSE_IPv6 */
			#if( ipconfigUSE_NETWORK_EVENT_HOOK != 0 )
				bCallDownHook : 1,
			#endif /* ipconfigUSE_NETWORK_EVENT_HOOK */
			bEndPointUp : 1;
	} bits;
#if( ipconfigUSE_DHCP != 0 )
	IPTimer_t xDHCPTimer;
	DHCPData_t xDHCPData;
#endif
	NetworkInterface_t *pxNetworkInterface;
	struct xNetworkEndPoint *pxNext;
} NetworkEndPoint_t;

/*
 * Add a new physical Network Interface.  The object pointed to by 'pxInterface'
 * must continue to exist.
 */
NetworkInterface_t *FreeRTOS_AddNetworkInterface( NetworkInterface_t *pxInterface );

/*
 * Add a new IP-address to a Network Interface.  The object pointed to by
 * 'pxEndPoint' must continue to exist.
 */
NetworkEndPoint_t *FreeRTOS_AddEndPoint( NetworkInterface_t *pxInterface, NetworkEndPoint_t *pxEndPoint );

/*
 * Get the first Network Interface.
 */
NetworkInterface_t *FreeRTOS_FirstNetworkInterface( void );

/*
 * Get the next Network Interface.
 */
NetworkInterface_t *FreeRTOS_NextNetworkInterface( NetworkInterface_t *pxInterface );

/*
 * Get the first end-point belonging to a given interface.  When pxInterface is
 * NULL, the very first end-point will be returned.
 */
NetworkEndPoint_t *FreeRTOS_FirstEndPoint( NetworkInterface_t *pxInterface );

/*
 * Get the next end-point.  When pxInterface is null, all end-points can be
 * iterated.
 */
NetworkEndPoint_t *FreeRTOS_NextEndPoint( NetworkInterface_t *pxInterface, NetworkEndPoint_t *pxEndPoint );

/*
 * Find the end-point with given IP-address.
 * The 'ulWhere' parameter is only for debugging puposes.
 */
NetworkEndPoint_t *FreeRTOS_FindEndPointOnIP( uint32_t ulIPAddress, uint32_t ulWhere );

/*
 * Find the end-point with given IP-address.
 */
NetworkEndPoint_t *FreeRTOS_FindEndPointOnIP( uint32_t ulIPAddress, uint32_t ulWhere );

#if( ipconfigUSE_IPv6 != 0 )
	/* Find the end-point with given IP-address. */
	NetworkEndPoint_t *FreeRTOS_FindEndPointOnIP_IPv6( const IPv6_Address_t *pxIPAddress );
#endif /* ipconfigUSE_IPv6 */

/*
 * Find the end-point with given MAC-address.
 */
NetworkEndPoint_t *FreeRTOS_FindEndPointOnMAC( const MACAddress_t *pxMACAddress, NetworkInterface_t *pxInterface );

/*
 * Returns the addresses stored in an end point structure.
 */
void FreeRTOS_GetAddressConfiguration( NetworkEndPoint_t *pxEndPoint, uint32_t *pulIPAddress, uint32_t *pulNetMask, uint32_t *pulGatewayAddress, uint32_t *pulDNSServerAddress );

/*
 * Find the best fitting end-point to reach a given IP-address.
 */
NetworkEndPoint_t *FreeRTOS_FindEndPointOnNetMask( uint32_t ulIPAddress, uint32_t ulWhere );

/*
 * Find the best fitting end-point to reach a given IP-address on a given interface
 */
NetworkEndPoint_t *FreeRTOS_InterfaceEndPointOnNetMask( NetworkInterface_t *pxInterface, uint32_t ulIPAddress, uint32_t ulWhere );

#if( ipconfigUSE_IPv6 != 0 )
	NetworkEndPoint_t *FreeRTOS_FindEndPointOnNetMask_IPv6( IPv6_Address_t *pxIPv6Address );
#endif /* ipconfigUSE_IPv6 */

#if( ipconfigUSE_IPv6 != 0 )
	/* Get the first end-point belonging to a given interface.
	When pxInterface is NULL, the very first end-point will be returned. */
	NetworkEndPoint_t *FreeRTOS_FirstEndPoint_IPv6( NetworkInterface_t *pxInterface );
#endif /* ipconfigUSE_IPv6 */

#if( ipconfigUSE_IPv6 != 0 )
	void FreeRTOS_OutputAdvertiseIPv6( NetworkEndPoint_t *pxEndPoint );
#endif

/* A ethernet packet has come in on 'pxMyInterface'. Find the best matching end-point. */
NetworkEndPoint_t *FreeRTOS_MatchingEndpoint( NetworkInterface_t *pxMyInterface, uint8_t *pucEthernetBuffer );

/* Return the default end-point. */
NetworkEndPoint_t *FreeRTOS_FindDefaultEndPoint( void );

/* Fill-in the end-point structure. */
void FreeRTOS_FillEndPoint(	NetworkEndPoint_t *pxNetworkEndPoint,
	const uint8_t ucIPAddress[ ipIP_ADDRESS_LENGTH_BYTES ],
	const uint8_t ucNetMask[ ipIP_ADDRESS_LENGTH_BYTES ],
	const uint8_t ucGatewayAddress[ ipIP_ADDRESS_LENGTH_BYTES ],
	const uint8_t ucDNSServerAddress[ ipIP_ADDRESS_LENGTH_BYTES ],
	const uint8_t ucMACAddress[ ipMAC_ADDRESS_LENGTH_BYTES ] );

/* Return pdTRUE if all end-points are up.
When pxInterface is null, all end-points can be iterated. */
BaseType_t FreeRTOS_AllEndPointsUp( NetworkInterface_t *pxInterface );

typedef struct xRoutingStats
{
	UBaseType_t ulOnIp;
	UBaseType_t ulOnMAC;
	UBaseType_t ulOnNetMask;
	UBaseType_t ulDefault;
	UBaseType_t ulMatching;
	UBaseType_t ulLocations[ 14 ];
	UBaseType_t ulLocationsIP[ 8 ];
} RoutingStats_t;

extern RoutingStats_t xRoutingStats;

NetworkEndPoint_t *pxGetSocketEndpoint( Socket_t xSocket );
void vSetSocketEndpoint( Socket_t xSocket, NetworkEndPoint_t *pxEndPoint );

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* FREERTOS_ROUTING_H */













