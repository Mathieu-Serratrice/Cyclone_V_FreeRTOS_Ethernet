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

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_ARP.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_DHCP.h"
#if( ipconfigUSE_LLMNR == 1 )
	#include "FreeRTOS_DNS.h"
#endif /* ipconfigUSE_LLMNR */
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"
#include "FreeRTOS_Routing.h"


/* When the age of an entry in the ARP table reaches this value (it counts down
to zero, so this is an old entry) an ARP request will be sent to see if the
entry is still valid and can therefore be refreshed. */
#define arpMAX_ARP_AGE_BEFORE_NEW_ARP_REQUEST		( 3 )

/* The time between gratuitous ARPs. */
#ifndef arpGRATUITOUS_ARP_PERIOD
	#define arpGRATUITOUS_ARP_PERIOD					( pdMS_TO_TICKS( 20000 ) )
#endif

/*-----------------------------------------------------------*/

/*
 * Lookup an MAC address in the ARP cache from the IP address.
 */
static eARPLookupResult_t prvCacheLookup( uint32_t ulAddressToLookup, MACAddress_t * const pxMACAddress, NetworkInterface_t **ppxInterface );

/*-----------------------------------------------------------*/

/* The ARP cache. */
static ARPCacheRow_t xARPCache[ ipconfigARP_CACHE_ENTRIES ];

/* The time at which the last gratuitous ARP was sent.  Gratuitous ARPs are used
to ensure ARP tables are up to date and to detect IP address conflicts. */
static TickType_t xLastGratuitousARPTime = ( TickType_t ) 0;

/*
 * IP-clash detection is currently only used internally. When DHCP doesn't respond, the
 * driver can try out a random LinkLayer IP address (169.254.x.x).  It will send out a
 * gratuitos ARP message and, after a period of time, check the variables here below:
 */
#if( ipconfigARP_USE_CLASH_DETECTION != 0 )
	/* Becomes non-zero if another device responded to a gratuitos ARP message. */
	BaseType_t xARPHadIPClash;
	/* MAC-address of the other device containing the same IP-address. */
	MACAddress_t xARPClashMacAddress;
#endif /* ipconfigARP_USE_CLASH_DETECTION */

/* Part of the Ethernet and ARP headers are always constant when sending an IPv4
ARP packet.  This array defines the constant parts, allowing this part of the
packet to be filled in using a simple memcpy() instead of individual writes. */
static const uint8_t xDefaultPartARPPacketHeader[] =
{
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 	/* Ethernet destination address. */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 	/* Ethernet source address. */
	0x08, 0x06, 							/* Ethernet frame type (ipARP_FRAME_TYPE). */
	0x00, 0x01, 							/* usHardwareType (ipARP_HARDWARE_TYPE_ETHERNET). */
	0x08, 0x00,								/* usProtocolType. */
	ipMAC_ADDRESS_LENGTH_BYTES, 			/* ucHardwareAddressLength. */
	ipIP_ADDRESS_LENGTH_BYTES, 				/* ucProtocolAddressLength. */
	0x00, 0x01, 							/* usOperation (ipARP_REQUEST). */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 	/* xSenderHardwareAddress. */
	0x00, 0x00, 0x00, 0x00, 				/* ulSenderProtocolAddress. */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00 		/* xTargetHardwareAddress. */
};

/*-----------------------------------------------------------*/

eFrameProcessingResult_t eARPProcessPacket( NetworkBufferDescriptor_t * const pxNetworkBuffer )
{
ARPPacket_t * const pxARPFrame = ( ARPPacket_t * const ) pxNetworkBuffer->pucEthernetBuffer;
eFrameProcessingResult_t eReturn = eReleaseBuffer;
ARPHeader_t *pxARPHeader = &( pxARPFrame->xARPHeader );
uint32_t ulTargetProtocolAddress, ulSenderProtocolAddress;
/*_RB_ I think this was determined before eARPProcessPacket was called so perhaps could just be passed in as a parameter. */
NetworkEndPoint_t *pxTargetEndPoint = FreeRTOS_FindEndPointOnIP( pxARPHeader->ulTargetProtocolAddress, 1 );
#if( ipconfigARP_USE_CLASH_DETECTION != 0 )
	NetworkEndPoint_t *pxSourceEndPoint;
#endif

	/* The field ulSenderProtocolAddress is badly aligned, copy byte-by-byte. */
	memcpy( ( void *)&( ulSenderProtocolAddress ), ( void * )pxARPHeader->ucSenderProtocolAddress, sizeof( ulSenderProtocolAddress ) );
	/* The field ulTargetProtocolAddress is well-aligned, a 32-bits copy. */
	ulTargetProtocolAddress = pxARPHeader->ulTargetProtocolAddress;

	#if( ipconfigARP_USE_CLASH_DETECTION != 0 )
	{
		pxSourceEndPoint = FreeRTOS_FindEndPointOnIP( ulSenderProtocolAddress, 2 );
	}
	#endif

	traceARP_PACKET_RECEIVED();

	/* Don't do anything if the local IP address is zero because
	that means a DHCP request has not completed. */
	/*_RB_ Is this check done elsewhere now, on an interface by interface basis? */
//	if( *ipLOCAL_IP_ADDRESS_POINTER != 0UL )
	{
		switch( pxARPHeader->usOperation )
		{
			case ipARP_REQUEST	:
				/* The packet contained an ARP request.  Was it for the IP
				address of one of the end-points? */
				if( pxTargetEndPoint != NULL )
				{
					iptraceSENDING_ARP_REPLY( ulSenderProtocolAddress );

					/* The request is for the address of this node.  Add the
					entry into the ARP cache, or refresh the entry if it
					already exists. */
					vARPRefreshCacheEntry( &( pxARPHeader->xSenderHardwareAddress ), ulSenderProtocolAddress, pxNetworkBuffer->pxInterface );

					/* Generate a reply payload in the same buffer. */
					pxARPHeader->usOperation = ( uint16_t ) ipARP_REPLY;
					if( pxARPHeader->ulTargetProtocolAddress == ulSenderProtocolAddress )
					{
						/* The same IP address!  Give the sources MAC address 
						the value of the broadcast address, will be swapped 
						later. *//*_RB_ What is this path? */
						memcpy( pxARPFrame->xEthernetHeader.xSourceAddress.ucBytes, xBroadcastMACAddress.ucBytes, sizeof( xBroadcastMACAddress ) );
						memset( pxARPHeader->xTargetHardwareAddress.ucBytes, '\0', sizeof( MACAddress_t ) );
						pxARPHeader->ulTargetProtocolAddress = 0UL;
					}
					else
					{
						memcpy( pxARPHeader->xTargetHardwareAddress.ucBytes, pxARPHeader->xSenderHardwareAddress.ucBytes, sizeof( MACAddress_t ) );
						pxARPHeader->ulTargetProtocolAddress = ulSenderProtocolAddress;
					}
					memcpy( pxARPHeader->xSenderHardwareAddress.ucBytes, pxTargetEndPoint->xMACAddress.ucBytes, sizeof( MACAddress_t ) );
					memcpy( ( void* )pxARPHeader->ucSenderProtocolAddress, ( void* ) & pxTargetEndPoint->ulIPAddress, sizeof( pxARPHeader->ucSenderProtocolAddress ) );
					pxNetworkBuffer->pxEndPoint = pxTargetEndPoint;

					eReturn = eReturnEthernetFrame;
				}
				break;

			case ipARP_REPLY :
				iptracePROCESSING_RECEIVED_ARP_REPLY( ulTargetProtocolAddress );
				vARPRefreshCacheEntry( &( pxARPHeader->xSenderHardwareAddress ), ulSenderProtocolAddress, pxNetworkBuffer->pxInterface );
				/* Process received ARP frame to see if there is a clash. */
				#if( ipconfigARP_USE_CLASH_DETECTION != 0 )
				{
					if( pxSourceEndPoint != NULL )
					{
						xARPHadIPClash = pdTRUE;
						/* Remember the MAC-address of the other device which has the same IP-address. */
						memcpy( xARPClashMacAddress.ucBytes, pxARPHeader->xSenderHardwareAddress.ucBytes, sizeof xARPClashMacAddress.ucBytes );
					}
				}
				#endif /* ipconfigARP_USE_CLASH_DETECTION */
				break;

			default :
				/* Invalid. */
				break;
		}
	}

	return eReturn;
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_ARP_REMOVE_ENTRY != 0 )

	uint32_t ulARPRemoveCacheEntryByMac( const MACAddress_t * pxMACAddress )
	{
	BaseType_t x;
	uint32_t lResult = 0;

		/* For each entry in the ARP cache table. */
		for( x = 0; x < ipconfigARP_CACHE_ENTRIES; x++ )
		{
			if( ( memcmp( xARPCache[ x ].xMACAddress.ucBytes, pxMACAddress->ucBytes, sizeof( pxMACAddress->ucBytes ) ) == 0 ) )
			{
				lResult = xARPCache[ x ].ulIPAddress;
				memset( &xARPCache[ x ], '\0', sizeof( xARPCache[ x ] ) );
				break;
			}
		}

		return lResult;
	}

#endif	/* ipconfigUSE_ARP_REMOVE_ENTRY != 0 */
/*-----------------------------------------------------------*/

void vARPRefreshCacheEntry( const MACAddress_t * pxMACAddress, const uint32_t ulIPAddress, NetworkInterface_t *pxInterface )
{
BaseType_t x, xIpEntry = -1, xMacEntry = -1, xUseEntry = 0;
uint8_t ucMinAgeFound = 0U;
#if( ipconfigARP_STORES_REMOTE_ADDRESSES != 0 )
	BaseType_t xAddressIsLocal = ( BaseType_t ) -1;
#endif

	#if( ipconfigARP_STORES_REMOTE_ADDRESSES == 0 )
		/* Only process the IP address if it matches with one of the end-points,
		or as long as not all end-points are up. */
		if( ( FreeRTOS_FindEndPointOnNetMask( ulIPAddress, 1 ) != NULL ) ||
			( FreeRTOS_AllEndPointsUp( NULL ) == pdFALSE ) )
	#else
		/* If ipconfigARP_STORES_REMOTE_ADDRESSES is non-zero, IP addresses with
		a different netmask will also be stored.  After when replying to a UDP
		message from a different netmask, the IP address can be looped up and a
		reply sent.  This option is useful for systems with multiple gateways,
		the reply will surely arrive.  If ipconfigARP_STORES_REMOTE_ADDRESSES is
		zero the the gateway address is the only option. */
		if( pdTRUE )
	#endif
	{
		/* Start with the maximum possible number. */
		ucMinAgeFound--;

		/* For each entry in the ARP cache table. */
		for( x = 0; x < ipconfigARP_CACHE_ENTRIES; x++ )
		{
			/* Does this line in the cache table hold an entry for the IP
			address	being queried? */
			if( xARPCache[ x ].ulIPAddress == ulIPAddress )
			{
				if( pxMACAddress == NULL )
				{
					/* In case the parameter pxMACAddress is NULL, an entry will be reserved to
					indicate that there is an outstanding ARP request, This entry will have
					"ucValid == pdFALSE". */
					xIpEntry = x;
					break;
				}

				/* See if the MAC-address also matches. */
				if( memcmp( xARPCache[ x ].xMACAddress.ucBytes, pxMACAddress->ucBytes, sizeof( pxMACAddress->ucBytes ) ) == 0 )
				{
					/* This function will be called for each received packet
					As this is by far the most common path the coding standard
					is relaxed in this case and a return is permitted as an
					optimisation. */
					xARPCache[ x ].ucAge = ( uint8_t ) ipconfigMAX_ARP_AGE;
					xARPCache[ x ].ucValid = ( uint8_t ) pdTRUE;
					xARPCache[ x ].pxInterface = pxInterface;
					return;
				}

				/* Found an entry containing ulIPAddress, but the MAC address
				doesn't match.  Might be an entry with ucValid=pdFALSE, waiting
				for an ARP reply.  Still want to see if there is match with the
				given MAC address.ucBytes.  If found, either of the two entries
				must be cleared. */
				xIpEntry = x;
			}
			else if( ( pxMACAddress != NULL ) && ( memcmp( xARPCache[ x ].xMACAddress.ucBytes, pxMACAddress->ucBytes, sizeof( pxMACAddress->ucBytes ) ) == 0 ) )
			{
				/* Found an entry with the given MAC-address, but the IP-address
				is different.  Continue looping to find a possible match with
				ulIPAddress. */
				#if( ipconfigARP_STORES_REMOTE_ADDRESSES != 0 )
				{
				BaseType_t xOtherIsLocal = FreeRTOS_FindEndPointOnNetMask( xARPCache[ x ].ulIPAddress, 3 ) != NULL;

					if( xAddressIsLocal < ( BaseType_t ) 0 )
					{
						/* Only look-up the address when needed. */
						xAddressIsLocal = FreeRTOS_FindEndPointOnNetMask( ulIPAddress, 2 ) != NULL;
					}
					if( xAddressIsLocal == xOtherIsLocal )
					{
						xMacEntry = x;
					}
				}
				#else
				{
					xMacEntry = x;
				}
				#endif
			}
			/* _HT_
			Shouldn't we test for xARPCache[ x ].ucValid == pdFALSE here ? */
			else if( xARPCache[ x ].ucAge < ucMinAgeFound )
			{
				/* As the table is traversed, remember the table row that
				contains the oldest entry (the lowest age count, as ages are
				decremented to zero) so the row can be re-used if this function
				needs to add an entry that does not already exist. */
				ucMinAgeFound = xARPCache[ x ].ucAge;
				xUseEntry = x;
			}
		}

		if( xMacEntry >= 0 )
		{
			xUseEntry = xMacEntry;

			if( xIpEntry >= 0 )
			{
				/* Both the MAC address as well as the IP address were found in
				different locations: clear the entry which matches the
				IP-address */
				memset( &xARPCache[ xIpEntry ], '\0', sizeof( xARPCache[ xIpEntry ] ) );
			}
		}
		else if( xIpEntry >= 0 )
		{
			/* An entry containing the IP-address was found, but it had a different MAC address */
			xUseEntry = xIpEntry;
		}

		/* If the entry was not found, we use the oldest entry and set the IPaddress */
		xARPCache[ xUseEntry ].ulIPAddress = ulIPAddress;

		if( pxMACAddress != NULL )
		{
			memcpy( xARPCache[ xUseEntry ].xMACAddress.ucBytes, pxMACAddress->ucBytes, sizeof( pxMACAddress->ucBytes ) );

			iptraceARP_TABLE_ENTRY_CREATED( ulIPAddress, (*pxMACAddress) );
			/* And this entry does not need immediate attention */
			xARPCache[ xUseEntry ].ucAge = ( uint8_t ) ipconfigMAX_ARP_AGE;
			xARPCache[ xUseEntry ].ucValid = ( uint8_t ) pdTRUE;
			xARPCache[ xUseEntry ].pxInterface = pxInterface;
		}
		else if( xIpEntry < 0 )
		{
			xARPCache[ xUseEntry ].ucAge = ( uint8_t ) ipconfigMAX_ARP_RETRANSMISSIONS;
			xARPCache[ xUseEntry ].ucValid = ( uint8_t ) pdFALSE;
		}
	}
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_ARP_REVERSED_LOOKUP == 1 )
	eARPLookupResult_t eARPGetCacheEntryByMac( MACAddress_t * const pxMACAddress, uint32_t *pulIPAddress )
	{
	BaseType_t x;
	eARPLookupResult_t eReturn = eARPCacheMiss;

		/* Loop through each entry in the ARP cache. */
		for( x = 0; x < ipconfigARP_CACHE_ENTRIES; x++ )
		{
			/* Does this row in the ARP cache table hold an entry for the MAC
			address being searched? */
			if( memcmp( pxMACAddress->ucBytes, xARPCache[ x ].xMACAddress.ucBytes, sizeof( MACAddress_t ) ) == 0 )
			{
				*pulIPAddress = xARPCache[ x ].ulIPAddress;
				eReturn = eARPCacheHit;
				break;
			}
		}

		return eReturn;
	}
#endif /* ipconfigUSE_ARP_REVERSED_LOOKUP */

/*-----------------------------------------------------------*/

eARPLookupResult_t eARPGetCacheEntry( uint32_t *pulIPAddress, MACAddress_t * const pxMACAddress, NetworkInterface_t **ppxInterface )
{
eARPLookupResult_t eReturn;
uint32_t ulAddressToLookup;

	if( ppxInterface != NULL )
	{
		*( ppxInterface ) = NULL;
	}
#if( ipconfigUSE_LLMNR == 1 )
	if( *pulIPAddress == ipLLMNR_IP_ADDR )	/* Is in network byte order. */
	{
		/* The LLMNR IP-address has a fixed virtual MAC address. */
		memcpy( pxMACAddress->ucBytes, xLLMNR_MacAdress.ucBytes, sizeof( MACAddress_t ) );
		eReturn = eARPCacheHit;
	}
	else
#endif
	if( ( FreeRTOS_ntohl( *pulIPAddress ) & 0xff ) == 0xff )
	{
		/* This is a broadcast (x.x.x.255) so uses the broadcast MAC address. */
		memcpy( pxMACAddress->ucBytes, xBroadcastMACAddress.ucBytes, sizeof( MACAddress_t ) );
		eReturn = eARPCacheHit;
	}
	else
	{
		NetworkEndPoint_t *pxEndPoint = FreeRTOS_FindEndPointOnNetMask( *pulIPAddress, 4 );

		if( ( pxEndPoint != NULL ) && ( pxEndPoint->ulIPAddress == 0UL ) )
		{
			/* The IP address has not yet been assigned, so there is nothing that
			can be done. */
			eReturn = eCantSendPacket;
		}
		else
		{
			eReturn = eARPCacheMiss;

			if( pxEndPoint == NULL )
			{
				/* No matching end-point is found, look for a gateway. */
#if( ipconfigARP_STORES_REMOTE_ADDRESSES == 1 )
				eReturn = prvCacheLookup( *pulIPAddress, pxMACAddress, ppxInterface );

				if( eReturn == eARPCacheHit )
				{
					/* The stack is configured to store 'remote IP addresses', 
					i.e. addresses belonging to a different the netmask.  
					prvCacheLookup() returned a hit, so the MAC address is 
					known. */
				}
				else
#endif
				{
					/* The IP address is off the local network, so look up the
					hardware address of the router, if any. */
					if( xNetworkAddressing.ulGatewayAddress != ( uint32_t )0u )
					{
						ulAddressToLookup = xNetworkAddressing.ulGatewayAddress;
					}
					else
					{
						ulAddressToLookup = *pulIPAddress;
					}
				}
			}
			else
			{
			/* The IP address is on the local network, so lookup the requested
			IP address directly. */
				ulAddressToLookup = *pulIPAddress;
			}

			if( eReturn == eARPCacheMiss )
			{
				if( ulAddressToLookup == 0UL )
				{
				/* The address is not on the local network, and there is not a
				router. */
					eReturn = eCantSendPacket;
				}
				else
				{
					eReturn = prvCacheLookup( ulAddressToLookup, pxMACAddress, ppxInterface );

					if( eReturn == eARPCacheMiss )
					{
						/* It might be that the ARP has to go to the gateway. */
						*pulIPAddress = ulAddressToLookup;
					}
				}
			}
		}
	}

	return eReturn;
}

/*-----------------------------------------------------------*/

static eARPLookupResult_t prvCacheLookup( uint32_t ulAddressToLookup, MACAddress_t * const pxMACAddress, NetworkInterface_t **ppxInterface )
{
BaseType_t x;
eARPLookupResult_t eReturn = eARPCacheMiss;

	/* Loop through each entry in the ARP cache. */
	for( x = 0; x < ipconfigARP_CACHE_ENTRIES; x++ )
	{
		/* Does this row in the ARP cache table hold an entry for the IP address
		being queried? */
		if( xARPCache[ x ].ulIPAddress == ulAddressToLookup )
		{
			/* A matching valid entry was found. */
			if( xARPCache[ x ].ucValid == ( uint8_t ) pdFALSE )
			{
				/* This entry is waiting an ARP reply, so is not valid. */
				eReturn = eCantSendPacket;
			}
			else
			{
				/* A valid entry was found. */
				memcpy( pxMACAddress->ucBytes, xARPCache[ x ].xMACAddress.ucBytes, sizeof( MACAddress_t ) );
				if( ppxInterface != NULL )
				{
					*( ppxInterface ) = xARPCache[ x ].pxInterface;
				}
				eReturn = eARPCacheHit;
			}
			break;
		}
	}

	return eReturn;
}
/*-----------------------------------------------------------*/

void vARPAgeCache( void )
{
BaseType_t x;
TickType_t xTimeNow;

	/* Loop through each entry in the ARP cache. */
	for( x = 0; x < ipconfigARP_CACHE_ENTRIES; x++ )
	{
		/* If the entry is valid (its age is greater than zero). */
		if( xARPCache[ x ].ucAge > 0U )
		{
			/* Decrement the age value of the entry in this ARP cache table row.
			When the age reaches zero it is no longer considered valid. */
			( xARPCache[ x ].ucAge )--;

			/* If the entry is not yet valid, then it is waiting an ARP
			reply, and the ARP request should be retransmitted. */
			if( xARPCache[ x ].ucValid == ( uint8_t ) pdFALSE )
			{
				FreeRTOS_OutputARPRequest( xARPCache[ x ].ulIPAddress );
			}
			else if( xARPCache[ x ].ucAge <= ( uint8_t ) arpMAX_ARP_AGE_BEFORE_NEW_ARP_REQUEST )
			{
				/* This entry will get removed soon.  See if the MAC address is
				still valid to prevent this happening. */
				iptraceARP_TABLE_ENTRY_WILL_EXPIRE( xARPCache[ x ].ulIPAddress );
				FreeRTOS_OutputARPRequest( xARPCache[ x ].ulIPAddress );
			}
			else
			{
				/* The age has just ticked down, with nothing to do. */
			}

			if( xARPCache[ x ].ucAge == 0u )
			{
				/* The entry is no longer valid.  Wipe it out. */
				iptraceARP_TABLE_ENTRY_EXPIRED( xARPCache[ x ].ulIPAddress );
				xARPCache[ x ].ulIPAddress = 0UL;
			}
		}
	}

	xTimeNow = xTaskGetTickCount ();

	if( ( xLastGratuitousARPTime == ( TickType_t ) 0 ) || ( ( xTimeNow - xLastGratuitousARPTime ) > ( TickType_t ) arpGRATUITOUS_ARP_PERIOD ) )
	{
	NetworkEndPoint_t *pxEndPoint = pxNetworkEndPoints;

		while( pxEndPoint != NULL )
		{
			if( ( pxEndPoint->bits.bEndPointUp != pdFALSE_UNSIGNED ) && ( pxEndPoint->ulIPAddress != 0ul ) )
			{
			#if( ipconfigUSE_IPv6 != 0 )
				if( pxEndPoint->bits.bIPv6 != pdFALSE_UNSIGNED )
				{
					FreeRTOS_OutputAdvertiseIPv6( pxEndPoint );
				}
			#endif
				if( pxEndPoint->ulIPAddress != 0 )
				{
					FreeRTOS_OutputARPRequest( pxEndPoint->ulIPAddress );
				}
			}
			pxEndPoint = pxEndPoint->pxNext;
		}
		xLastGratuitousARPTime = xTimeNow;
	}
}
/*-----------------------------------------------------------*/

void vARPSendGratuitous( void )
{
	/* Setting xLastGratuitousARPTime to 0 will force a gratuitous ARP the next
	time vARPAgeCache() is called. */
	xLastGratuitousARPTime = ( TickType_t ) 0;

	/* Let the IP-task call vARPAgeCache(). */
	xSendEventToIPTask( eARPTimerEvent );
}

/*-----------------------------------------------------------*/
void FreeRTOS_OutputARPRequest( uint32_t ulIPAddress )
{
NetworkBufferDescriptor_t *pxNetworkBuffer;
NetworkEndPoint_t *pxEndPoint;
NetworkInterface_t *pxInterface;

	for( pxInterface = FreeRTOS_FirstNetworkInterface();
		 pxInterface != NULL;
		 pxInterface = FreeRTOS_NextNetworkInterface( pxInterface ) )
	{
		pxEndPoint = FreeRTOS_FindEndPointOnIP( ulIPAddress, 25 );
		if( pxEndPoint == NULL )
		{
			pxEndPoint = FreeRTOS_InterfaceEndPointOnNetMask( pxInterface, ulIPAddress, 26 );
		}
		FreeRTOS_printf( ( "OutputARPRequest: remote IP = %lxip end-point = %lxip\n",
			FreeRTOS_ntohl( ulIPAddress ),
			FreeRTOS_ntohl( pxEndPoint != 0 ? pxEndPoint->ulIPAddress : 0x0ul ) ) );

		if( pxEndPoint != NULL )
		{
		NetworkInterface_t *pxInterface;

			/* This is called from the context of the IP event task, so a block time
			must not be used. */
			pxNetworkBuffer = pxGetNetworkBufferWithDescriptor( sizeof( ARPPacket_t ), 0 );

			if( pxNetworkBuffer != NULL )
			{
				pxNetworkBuffer->ulIPAddress = ulIPAddress;
				pxNetworkBuffer->pxEndPoint = pxEndPoint;
		
				pxInterface = pxNetworkBuffer->pxEndPoint->pxNetworkInterface;
				configASSERT( pxInterface != NULL );
				vARPGenerateRequestPacket( pxNetworkBuffer );
				#if defined( ipconfigETHERNET_MINIMUM_PACKET_BYTES )
				{
					if( pxNetworkBuffer->xDataLength < ( size_t ) ipconfigETHERNET_MINIMUM_PACKET_BYTES )
					{
					BaseType_t xIndex;
		
	//					FreeRTOS_printf( ( "OutputARPRequest: length %lu -> %lu\n",
	//						pxNetworkBuffer->xDataLength, ipconfigETHERNET_MINIMUM_PACKET_BYTES ) );
						for( xIndex = ( BaseType_t ) pxNetworkBuffer->xDataLength; xIndex < ( BaseType_t ) ipconfigETHERNET_MINIMUM_PACKET_BYTES; xIndex++ )
						{
							pxNetworkBuffer->pucEthernetBuffer[ xIndex ] = 0u;
						}
						pxNetworkBuffer->xDataLength = ( size_t ) ipconfigETHERNET_MINIMUM_PACKET_BYTES;
					}
				}
				#endif
				pxInterface->pfOutput( pxInterface, pxNetworkBuffer, pdTRUE );
			}
		}
	}
}

void vARPGenerateRequestPacket( NetworkBufferDescriptor_t * const pxNetworkBuffer )
{
ARPPacket_t *pxARPPacket;

	pxARPPacket = ( ARPPacket_t * ) pxNetworkBuffer->pucEthernetBuffer;

	/* memcpy the const part of the header information into the correct
	location in the packet.  This copies:
		xEthernetHeader.ulDestinationAddress
		xEthernetHeader.usFrameType;
		xARPHeader.usHardwareType;
		xARPHeader.usProtocolType;
		xARPHeader.ucHardwareAddressLength;
		xARPHeader.ucProtocolAddressLength;
		xARPHeader.usOperation;
		xARPHeader.xTargetHardwareAddress;
	*/
	configASSERT( pxNetworkBuffer->pxEndPoint != NULL );

	memcpy( ( void * ) &( pxARPPacket->xEthernetHeader ), ( void * ) xDefaultPartARPPacketHeader, sizeof( xDefaultPartARPPacketHeader ) );
	memcpy( ( void * ) pxARPPacket->xEthernetHeader.xSourceAddress.ucBytes , ( void * ) pxNetworkBuffer->pxEndPoint->xMACAddress.ucBytes, ( size_t ) ipMAC_ADDRESS_LENGTH_BYTES );
	memcpy( ( void * ) pxARPPacket->xARPHeader.xSenderHardwareAddress.ucBytes, ( void * ) pxNetworkBuffer->pxEndPoint->xMACAddress.ucBytes, ( size_t ) ipMAC_ADDRESS_LENGTH_BYTES );
	/* HT:endian: network to network */
	memcpy( ( void* )pxARPPacket->xARPHeader.ucSenderProtocolAddress, ( void* )&( pxNetworkBuffer->pxEndPoint->ulIPAddress ), sizeof( pxARPPacket->xARPHeader.ucSenderProtocolAddress ) );

	pxARPPacket->xARPHeader.ulTargetProtocolAddress = pxNetworkBuffer->ulIPAddress;

	pxNetworkBuffer->xDataLength = sizeof( ARPPacket_t );

	iptraceCREATING_ARP_REQUEST( pxNetworkBuffer->ulIPAddress );
}
/*-----------------------------------------------------------*/

void FreeRTOS_ClearARP( void )
{
	memset( xARPCache, '\0', sizeof( xARPCache ) );
}
/*-----------------------------------------------------------*/

#if( ipconfigHAS_PRINTF != 0 ) || ( ipconfigHAS_DEBUG_PRINTF != 0 )

	void FreeRTOS_PrintARPCache( void )
	{
	BaseType_t x, xCount = 0;

		/* Loop through each entry in the ARP cache. */
		for( x = 0; x < ipconfigARP_CACHE_ENTRIES; x++ )
		{
			if( ( xARPCache[ x ].ulIPAddress != 0ul ) && ( xARPCache[ x ].ucAge > 0U ) )
			{
				/* See if the MAC-address also matches, and we're all happy */
				FreeRTOS_printf( ( "Arp %2ld: %3u - %16lxip : %02x:%02x:%02x : %02x:%02x:%02x\n",
					x,
					xARPCache[ x ].ucAge,
					xARPCache[ x ].ulIPAddress,
					xARPCache[ x ].xMACAddress.ucBytes[0],
					xARPCache[ x ].xMACAddress.ucBytes[1],
					xARPCache[ x ].xMACAddress.ucBytes[2],
					xARPCache[ x ].xMACAddress.ucBytes[3],
					xARPCache[ x ].xMACAddress.ucBytes[4],
					xARPCache[ x ].xMACAddress.ucBytes[5] ) );
				xCount++;
			}
		}

		FreeRTOS_printf( ( "Arp has %ld entries\n", xCount ) );
	}

#endif /* ( ipconfigHAS_PRINTF != 0 ) || ( ipconfigHAS_DEBUG_PRINTF != 0 ) */
