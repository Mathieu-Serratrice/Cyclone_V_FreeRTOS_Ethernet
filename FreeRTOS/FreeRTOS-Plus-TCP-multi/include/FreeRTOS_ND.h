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

#ifndef FREERTOS_ND_H
#define FREERTOS_ND_H

#ifdef __cplusplus
extern "C" {
#endif

/* Application level configuration options. */
#include "FreeRTOSIPConfig.h"
#include "FreeRTOSIPConfigDefaults.h"
#include "IPTraceMacroDefaults.h"

#if( ipconfigUSE_IPv6 != 0 )
/*-----------------------------------------------------------*/
/* Miscellaneous structure and definitions. */
/*-----------------------------------------------------------*/

typedef struct xND_CACHE_TABLE_ROW
{
	IPv6_Address_t xIPAddress;	/* The IP address of an ND cache entry. */
	MACAddress_t xMACAddress;	/* The MAC address of an ND cache entry. */
	uint8_t ucAge;				/* A value that is periodically decremented but can also be refreshed by active communication.  The ND cache entry is removed if the value reaches zero. */
    uint8_t ucValid;			/* pdTRUE: xMACAddress is valid, pdFALSE: waiting for ND reply */
} NDCacheRow_t;

/*
 * If ulIPAddress is already in the ND cache table then reset the age of the
 * entry back to its maximum value.  If ulIPAddress is not already in the ND
 * cache table then add it - replacing the oldest current entry if there is not
 * a free space available.
 */
void vNDRefreshCacheEntry( const MACAddress_t * pxMACAddress, const IPv6_Address_t *pxIPAddress );

#if( ipconfigUSE_ARP_REMOVE_ENTRY != 0 )

	/*
	 * In some rare cases, it might be useful to remove a ND cache entry of a
	 * known MAC address to make sure it gets refreshed.
	 */
	uint32_t ulNDRemoveCacheEntryByMac( const MACAddress_t * pxMACAddress );

#endif /* ipconfigUSE_ARP_REMOVE_ENTRY != 0 */

/*
 * Look for ulIPAddress in the ND cache.  If the IP address exists, copy the
 * associated MAC address into pxMACAddress, refresh the ND cache entry's
 * age, and return eARPCacheHit.  If the IP address does not exist in the ND
 * cache return eARPCacheMiss.  If the packet cannot be sent for any reason
 * (maybe DHCP is still in process, or the addressing needs a gateway but there
 * isn't a gateway defined) then return eCantSendPacket.
 */
eARPLookupResult_t eNDGetCacheEntry( IPv6_Address_t *pxIPAddress, MACAddress_t * const pxMACAddress );

#if( ipconfigUSE_ARP_REVERSED_LOOKUP != 0 )

	/* Lookup an IP-address if only the MAC-address is known */
	eARPLookupResult_t eNDGetCacheEntryByMac( MACAddress_t * const pxMACAddress, IPv6_Address_t *pxIPAddress );

#endif
/*
 * Reduce the age count in each entry within the ND cache.  An entry is no
 * longer considered valid and is deleted if its age reaches zero.
 */
void vNDAgeCache( void );

/*
 * Send out an ND request for the IP address contained in pxNetworkBuffer, and
 * add an entry into the ND table that indicates that an ND reply is
 * outstanding so re-transmissions can be generated.
 */
void vNDGenerateRequestPacket( NetworkBufferDescriptor_t * const pxNetworkBuffer );

/*
 * After DHCP is ready and when changing IP address, force a quick send of our new IP
 * address
 */
void vNDSendGratuitous( void );

void FreeRTOS_PrintNDCache( void );

#endif /* ipconfigUSE_IPv6 != 0 */

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* FREERTOS_ND_H */













