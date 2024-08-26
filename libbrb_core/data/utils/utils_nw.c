/*
 * utils_nw.c
 *
 *  Created on: 2019-08-26
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2011 BrByte Software (Oliveira Alves & Amorim LTDA)
 * Todos os direitos reservados. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "../include/libbrb_core.h"

/**************************************************************************************************************************/
int BrbNw_IpMask(struct sockaddr_storage *addr, struct sockaddr_storage *mask)
{
	if (addr->ss_family == AF_INET6)
		return BrbNw_Ipv6Mask(&satosin6(addr)->sin6_addr, &satosin6(mask)->sin6_addr);

	return BrbNw_Ipv4Mask(&satosin(addr)->sin_addr, &satosin(mask)->sin_addr);
}
/**************************************************************************************************************************/
int BrbNw_IpBroadCast(struct sockaddr_storage *addr, struct sockaddr_storage *mask)
{
	if (addr->ss_family == AF_INET6)
		return BrbNw_Ipv6BroadCast(&satosin6(addr)->sin6_addr, &satosin6(mask)->sin6_addr);

	return BrbNw_Ipv4BroadCast(&satosin(addr)->sin_addr, &satosin(mask)->sin_addr);
}
/**************************************************************************************************************************/
int BrbNw_IpCompareMasked(struct sockaddr_storage *addr1, struct sockaddr_storage *addr2, struct sockaddr_storage *mask)
{
	if (addr1->ss_family == AF_INET6)
		return BrbNw_Ipv6CompareMasked(&satosin6(addr1)->sin6_addr, &satosin6(addr2)->sin6_addr, &satosin6(mask)->sin6_addr);

	return BrbNw_Ipv4CompareMasked(&satosin(addr1)->sin_addr, &satosin(addr2)->sin_addr, &satosin(mask)->sin_addr);
}
/**************************************************************************************************************************/
int BrbNw_IpCompare(struct sockaddr_storage *addr1, struct sockaddr_storage *addr2)
{
	if (addr1->ss_family == AF_INET6)
		return memcmp(&satosin6(addr1)->sin6_addr, &satosin6(addr2)->sin6_addr, sizeof(struct in6_addr));

	return memcmp(&satosin(addr1)->sin_addr, &satosin(addr2)->sin_addr, sizeof(struct in_addr));
}
/**************************************************************************************************************************/
int BrbNw_Ipv4Mask(struct in_addr *addr, struct in_addr *mask)
{
	addr->s_addr 	&= mask->s_addr;

	return 0;
}
/**************************************************************************************************************************/
int BrbNw_Ipv4BroadCast(struct in_addr *addr, struct in_addr *mask)
{
	addr->s_addr 	= (addr->s_addr & mask->s_addr) | ~mask->s_addr;

	return 0;
}
/**************************************************************************************************************************/
int BrbNw_Ipv6Mask(struct in6_addr *addr, struct in6_addr *mask)
{
#ifdef IS_LINUX
	addr->s6_addr32[0] &= mask->s6_addr32[0];
	addr->s6_addr32[1] &= mask->s6_addr32[1];
	addr->s6_addr32[2] &= mask->s6_addr32[2];
	addr->s6_addr32[3] &= mask->s6_addr32[3];
#else
	addr->__u6_addr.__u6_addr32[0] &= mask->__u6_addr.__u6_addr32[0];
	addr->__u6_addr.__u6_addr32[1] &= mask->__u6_addr.__u6_addr32[1];
	addr->__u6_addr.__u6_addr32[2] &= mask->__u6_addr.__u6_addr32[2];
	addr->__u6_addr.__u6_addr32[3] &= mask->__u6_addr.__u6_addr32[3];
#endif
	return 0;
}
/**************************************************************************************************************************/
int BrbNw_Ipv6BroadCast(struct in6_addr *addr, struct in6_addr *mask)
{
#ifdef IS_LINUX
	addr->s6_addr32[0] = addr->s6_addr32[0] | ~mask->s6_addr32[0];
	addr->s6_addr32[1] = addr->s6_addr32[1] | ~mask->s6_addr32[1];
	addr->s6_addr32[2] = addr->s6_addr32[2] | ~mask->s6_addr32[2];
	addr->s6_addr32[3] = addr->s6_addr32[3] | ~mask->s6_addr32[3];
#else
	addr->__u6_addr.__u6_addr32[0] = addr->__u6_addr.__u6_addr32[0] | ~mask->__u6_addr.__u6_addr32[0];
	addr->__u6_addr.__u6_addr32[1] = addr->__u6_addr.__u6_addr32[1] | ~mask->__u6_addr.__u6_addr32[1];
	addr->__u6_addr.__u6_addr32[2] = addr->__u6_addr.__u6_addr32[2] | ~mask->__u6_addr.__u6_addr32[2];
	addr->__u6_addr.__u6_addr32[3] = addr->__u6_addr.__u6_addr32[3] | ~mask->__u6_addr.__u6_addr32[3];
#endif
	return 0;
}
/**************************************************************************************************************************/
int BrbNw_Ipv4CompareMasked(const struct in_addr *addr1, const struct in_addr *addr2, const struct in_addr *mask)
{
	struct in_addr masked = *addr2;

	masked.s_addr 		&= mask->s_addr;

	return memcmp(addr1, &masked, sizeof(struct in_addr));
}
/**************************************************************************************************************************/
int BrbNw_Ipv6CompareMasked(const struct in6_addr *addr1, const struct in6_addr *addr2, const struct in6_addr *mask)
{
	struct in6_addr masked = *addr2;
#ifdef IS_LINUX
	masked.s6_addr32[0] &= mask->s6_addr32[0];
	masked.s6_addr32[1] &= mask->s6_addr32[1];
	masked.s6_addr32[2] &= mask->s6_addr32[2];
	masked.s6_addr32[3] &= mask->s6_addr32[3];
#else
	masked.__u6_addr.__u6_addr32[0] &= mask->__u6_addr.__u6_addr32[0];
	masked.__u6_addr.__u6_addr32[1] &= mask->__u6_addr.__u6_addr32[1];
	masked.__u6_addr.__u6_addr32[2] &= mask->__u6_addr.__u6_addr32[2];
	masked.__u6_addr.__u6_addr32[3] &= mask->__u6_addr.__u6_addr32[3];
#endif
	return memcmp(addr1, &masked, sizeof(struct in6_addr));
}
/**************************************************************************************************************************/
void BrbNw_Ipv4Netmask(struct in_addr *netmask, int mask)
{
	memset(netmask, 0, sizeof(struct in_addr));

	if (mask)
		netmask->s_addr = htonl(0xFFFFFFFFu << (32 - mask));
//		netmask->s_addr = htonl(~((1 << (32 - mask)) - 1));

	return;
}
/**************************************************************************************************************************/
void BrbNw_Ipv6Netmask(struct in6_addr *netmask, int mask)
{
	int bits_incomplete = mask;
	uint32_t *p_netmask;
	memset(netmask, 0, sizeof(struct in6_addr));

	if (mask < 0)
		mask = 0;
	else if (mask > 128)
		mask = 128;

#ifdef IS_LINUX
	p_netmask 	= &netmask->s6_addr32[0];
#else
	p_netmask 	= &netmask->__u6_addr.__u6_addr32[0];
#endif

	while (32 < mask)
	{
		*p_netmask = 0xffffffff;
		p_netmask++;
		mask -= 32;
	}

	if (mask != 0) {
		*p_netmask = htonl(0xFFFFFFFF << (32 - mask));
	}

	return;
}
/**************************************************************************************************************************/
int BrbNw_IpMaskExplode(char *network_ptr, struct sockaddr_storage *prefix_addr, struct sockaddr_storage *prefix_mask)
{
	int af_type;

	/* sanitize */
	if (!network_ptr)
		return -1;

	/* sanitize */
	if (!prefix_addr || !prefix_mask)
		return -2;

	char only_addr_str[128];
	char *ip_mask_ptr 				= NULL;
	int ip_mask;
	int i;

	/* zero fill info */
	memset(prefix_addr, 0, sizeof(struct sockaddr_storage));
	memset(prefix_mask, 0, sizeof(struct sockaddr_storage));

	/* Split IP from NETMASK */
	for (i = 0; (network_ptr[i] != '\0' && i < sizeof(only_addr_str) - 1); i++)
	{
		if (network_ptr[i] == '/')
		{
			ip_mask_ptr				= network_ptr + i + 1;
			break;
		}

		only_addr_str[i] 			= network_ptr[i];
		continue;
	}

	only_addr_str[i] 				= '\0';
	ip_mask 						= ip_mask_ptr ? atoi(ip_mask_ptr) : -1;

	/* Check address */
	af_type 						= BrbIsValidIpToSockAddr((char *)&only_addr_str, prefix_addr);

	if (af_type == AF_UNSPEC)
		return -3;

	if (af_type == AF_INET)
	{
//		if (satosin(prefix_addr)->sin_addr.s_addr == ntohl(0))
//		{
//			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "INVALID ADDR [%s]\n", network_ptr);
//
//			return -4;
//		}

		prefix_addr->ss_family 		= AF_INET;
		prefix_mask->ss_family 		= AF_INET;
#if !defined(__linux__)
		prefix_addr->ss_len 		= sizeof(struct sockaddr_in);
		prefix_mask->ss_len 		= sizeof(struct sockaddr_in);
#endif

		/* Get mask width */
		if ((ip_mask < 0) || (ip_mask > 32))
			ip_mask 				= 32;

		BrbNw_Ipv4Netmask(&satosin(prefix_mask)->sin_addr, ip_mask);
		BrbNw_Ipv4Mask(&satosin(prefix_addr)->sin_addr, &satosin(prefix_mask)->sin_addr);
	}
	else if (af_type == AF_INET6)
	{
		prefix_addr->ss_family 		= AF_INET6;
		prefix_mask->ss_family 		= AF_INET6;
#if !defined(__linux__)
		prefix_addr->ss_len 		= sizeof(struct sockaddr_in6);
		prefix_mask->ss_len 		= sizeof(struct sockaddr_in6);
#endif

		/* Get mask width */
		if ((ip_mask < 0) || (ip_mask > 128))
			ip_mask 				= 128;

		BrbNw_Ipv6Netmask(&satosin6(prefix_mask)->sin6_addr, ip_mask);
		BrbNw_Ipv6Mask(&satosin6(prefix_addr)->sin6_addr, &satosin6(prefix_mask)->sin6_addr);
	}

	return af_type;
}
/**************************************************************************************************************************/
int BrbNw_IpMaskImplode(char *network_ptr, int network_sz, struct sockaddr_storage *prefix_addr, struct sockaddr_storage *prefix_mask)
{
	char addr_str[128] = {0};
	char mask_str[128] = {0};

//	BrbNetworkSockNtop((char*)&addr_str, sizeof(addr_str) - 1, (const struct sockaddr *)prefix_addr, prefix_addr->ss_len);
	// TODO linuxes, not used ss_len inside
	BrbNetworkSockNtop((char*)&addr_str, sizeof(addr_str) - 1, (const struct sockaddr *)prefix_addr, 0);
	BrbNetworkSockMask((char*)&mask_str, sizeof(mask_str) - 1, (const struct sockaddr *)prefix_mask);

	return snprintf(network_ptr, network_sz, "%s%s", (char *)&addr_str, (char*)&mask_str);
}
///**************************************************************************************************************************/
//int BrbNw_Ipv4_match(struct in_addr *addr, struct in_addr *net, uint8_t bits)
//{
//  if (bits == 0)
//  {
//    // C99 6.5.7 (3): u32 << 32 is undefined behaviour
//    return 1;
//  }
//
//  return ((addr->s_addr ^ net->s_addr) & htonl(0xFFFFFFFFu << (32 - bits))) ? 1 : 0;
//}
/**************************************************************************************************************************/
/* Old ones to convert naming */
/**************************************************************************************************************************/

/**************************************************************************************************************************/
int BrbIsValidIpCidr(char *ip_cidr_str)
{
	char *mask_ptr;
	int	mask_value;
	int af_type;

	/* sanitize */
	if (!ip_cidr_str)
		return 0;

	mask_ptr 			= strchr(ip_cidr_str, '/');

	if (!mask_ptr)
		return -3;

	/* convert / to finish address string  */
	mask_ptr[0] 		= '\0';
	mask_value 			= atoi(&mask_ptr[1]);

	/* Check address */
	af_type 			= BrbIsValidIp(ip_cidr_str);

	/* convert again to / */
	mask_ptr[0] 		= '/';

	if (af_type == AF_UNSPEC)
		return -1;

	/* Get mask width */
	if ((mask_value < 0) || (af_type == AF_INET && mask_value > 32) || (af_type == AF_INET6 && mask_value > 128))
	{
		return -2;
	}

	return 1;
}
/**************************************************************************************************************************/
int BrbIsValidIp(char *ip_addr_str)
{
	struct sockaddr_storage target_sockaddr;

	return BrbIsValidIpToSockAddr(ip_addr_str, &target_sockaddr);
}
/**************************************************************************************************************************/
int BrbIsValidIpToSockAddr(char *ip_addr_str, struct sockaddr_storage *target_sockaddr)
{
	/* sanitize */
	if (!ip_addr_str || !target_sockaddr)
	{
		target_sockaddr->ss_family 	= AF_UNSPEC;
//		target_sockaddr->ss_len 	= 0;
	}
	else if (inet_pton(AF_INET, ip_addr_str, &satosin(target_sockaddr)->sin_addr) == 1)
	{
		target_sockaddr->ss_family 	= AF_INET;
//		target_sockaddr->ss_len 	= sizeof(struct sockaddr_in);
	}
	else if (inet_pton(AF_INET6, ip_addr_str, &satosin6(target_sockaddr)->sin6_addr) == 1)
	{
		target_sockaddr->ss_family 	= AF_INET6;
//		target_sockaddr->ss_len 	= sizeof(struct sockaddr_in6);
	}
	/* Unknown case */
	else
	{
		target_sockaddr->ss_family 	= AF_UNSPEC;
//		target_sockaddr->ss_len 	= 0;
	}

	return target_sockaddr->ss_family;
}
/**************************************************************************************************************************/
void BrbSockAddrSetPort(struct sockaddr_storage *sa_st, int port)
{
	if (sa_st->ss_family == AF_INET6)
		satosin6(sa_st)->sin6_port 	= htons(port);
	else
		satosin(sa_st)->sin_port 		= htons(port);

}
/**************************************************************************************************************************/
int BrbIpFamilyParse(const char *ip_str, BrbIpFamily *ip_family, unsigned char allow)
{
	/* Can't parse CIDR */
	if (strchr(ip_str, '/'))
		return 0;

	/* Check Ipv4 valid */
	if ((allow & IP_FAMILY_ALLOW_IPV4) && inet_pton(AF_INET, ip_str, &ip_family->u.ip4))
	{
		ip_family->family 	= AF_INET;
	}
	/* Check Ipv6 valid */
	else if ((allow & IP_FAMILY_ALLOW_IPV6) && inet_pton(AF_INET6, ip_str, &ip_family->u.ip6))
	{
		ip_family->family 	= AF_INET6;
	}
	else
	{
		return 0;
	}

	return 1;
}
/**************************************************************************************************************************/
int BrbNetworkSockNtop(char *ret_data_ptr, int ret_maxlen, const struct sockaddr *sa, size_t salen)
{
	char	portstr[7];
//	static char buf_ptr[INET6_ADDRSTRLEN + 1];
	static char buf_ptr[128];

	int cur_sz = 0;

	// TODO linuxes, not used salen inside

	switch (sa->sa_family)
	{
#if defined(SOCK_MAXADDRLEN)
	case SOCK_MAXADDRLEN:
	{
		int	i = 0;
		u_long	mask;
		u_int	index = 1 << 31;
		unsigned short	new_mask = 0;

		mask = ntohl(satosin(sa)->sin_addr.s_addr);

		while (mask & index)
		{
			new_mask++;
			index >>= 1;
		}

		cur_sz += snprintf(ret_data_ptr, ret_maxlen, "/%hu", new_mask);

		return cur_sz;
	}
#endif
	case AF_UNSPEC:
	case AF_INET:
	{
		const char *inet_ptr;

		inet_ptr 			= inet_ntop(AF_INET, &satosin(sa)->sin_addr, (char *)&buf_ptr, sizeof(buf_ptr));

		if (!inet_ptr)
			return 0;

		cur_sz 				+= snprintf(ret_data_ptr, ret_maxlen, "%s", (char *)&buf_ptr);

		/* If have port */
		if (ntohs(satosin(sa)->sin_port) != 0)
			cur_sz 			+= snprintf(ret_data_ptr + cur_sz, ret_maxlen, ".%d", ntohs(satosin(sa)->sin_port));

//		/* If default route, write just default on it */
//		if (!strcmp(ret_data_ptr, "0.0.0.0"))
//			cur_sz = snprintf(ret_data_ptr, ret_maxlen,  "default");

		return cur_sz;
	}
	case AF_INET6:
	{
		static char buf_ptr[INET6_ADDRSTRLEN + 1];
		const char *inet_ptr;

		inet_ptr 		= inet_ntop(AF_INET6, &satosin6(sa)->sin6_addr, buf_ptr, sizeof(buf_ptr));

		if (!inet_ptr)
			return 0;

		cur_sz 			+= snprintf(ret_data_ptr, ret_maxlen, "%s", buf_ptr);

		/* If have port */
		if (ntohs(satosin6(sa)->sin6_port) != 0)
			cur_sz 		+= snprintf(ret_data_ptr + cur_sz, ret_maxlen, ".%d", ntohs(satosin6(sa)->sin6_port));

		return cur_sz;
	}

#if defined(AF_LINK)
	case AF_LINK:
	{
		struct	sockaddr_dl *sdl = (struct sockaddr_dl *)sa;
		char *cp;

		if (sdl->sdl_nlen > 0)
		{
//			switch (sdl->sdl_type) {
//
//			case IFT_ETHER:
//			case IFT_L2VLAN:
//			case IFT_BRIDGE:
//				if (sdl->sdl_alen == ETHER_ADDR_LEN) {
//					cp = ether_ntoa((struct ether_addr *)
//						(sdl->sdl_data + sdl->sdl_nlen));
//					break;
//				}
//				/* FALLTHROUGH */
//			default:
//				cp = link_ntoa(sdl);
//				break;
//			}
//
//			strlcpy(ret_data_ptr, cp, (sdl->sdl_nlen + 1));

			cur_sz = strlcpy(ret_data_ptr, sdl->sdl_data, (sdl->sdl_nlen + 1));
			//printf("SysControl_NetworkSockNtop - AFF_LINK -> [%d]-[%s]-[%s]\n", sdl->sdl_nlen, sdl->sdl_data, ret_data_ptr);

		}
		else
			cur_sz = snprintf(ret_data_ptr, ret_maxlen, "link#%d", sdl->sdl_index);

		return cur_sz;
	}
#endif
	default:
		cur_sz = snprintf(ret_data_ptr, ret_maxlen, "unknown %d", sa->sa_family);
		return cur_sz;
	}

	return 0;
}
/**************************************************************************************************************************/
int BrbNetworkSockMask(char *ret_data_ptr, int ret_maxlen, const struct sockaddr *sa)
{
	int cur_sz = 0;

	switch (sa->sa_family)
	{
#if defined(SOCK_MAXADDRLEN)
	case SOCK_MAXADDRLEN:
#endif
	case AF_UNSPEC:
	case AF_INET:
	{
		int	i = 0;
		unsigned long	mask;
		unsigned int	index = 1 << 31;
		unsigned short	new_mask = 0;

		mask = ntohl(satosin(sa)->sin_addr.s_addr);

		while (mask & index)
		{
			new_mask++;
			index >>= 1;
		}

		cur_sz 	+= snprintf(ret_data_ptr, ret_maxlen, "/%hu", new_mask);

		return 1;
	}
	case AF_INET6:
	{
		struct in6_addr *mask = &((struct sockaddr_in6*)sa)->sin6_addr;
		unsigned char *p = (unsigned char *)mask;
		unsigned char *lim;
		int masklen, illegal = 0, flag = 0;

		for (masklen = 0, lim = p + 16; p < lim; p++) {
			switch (*p) {
			 case 0xff:
				 masklen += 8;
				 break;
			 case 0xfe:
				 masklen += 7;
				 break;
			 case 0xfc:
				 masklen += 6;
				 break;
			 case 0xf8:
				 masklen += 5;
				 break;
			 case 0xf0:
				 masklen += 4;
				 break;
			 case 0xe0:
				 masklen += 3;
				 break;
			 case 0xc0:
				 masklen += 2;
				 break;
			 case 0x80:
				 masklen += 1;
				 break;
			 case 0x00:
				 break;
			 default:
				 illegal ++;
				 break;
			}

			if (illegal)
				break;
		}

//		if (illegal)
//		{
//			cur_sz 	+= snprintf(ret_data_ptr, ret_maxlen, "/%d -%d", masklen, illegal);
//
//			return 0;
//		}

		cur_sz 	+= snprintf(ret_data_ptr, ret_maxlen, "/%d", masklen);

		return 1;
	}

	default:

		strlcpy(ret_data_ptr, "", 1);

		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
int BrbIsValidIpV4(char *ip_addr_str, struct in_addr *ip4)
{
	/* sanitize */
	if (!ip_addr_str)
		return 0;

	if (inet_pton(AF_INET, ip_addr_str, ip4))
	{
		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
unsigned char *BrbMacStrToOctedDup(char *mac_str)
{
	struct ether_addr *ether_addr;
	unsigned char mac[6];
	int i;

	ether_addr 				= ether_aton(mac_str);

	if (!ether_addr)
		return NULL;
#if defined(linux)
	for(i=0; i < 6; i++)
		mac[i] = (unsigned char)ether_addr->ether_addr_octet[i];
#else
	for(i=0; i < 6; i++)
		mac[i] = (unsigned char)ether_addr->octet[i];
#endif

	return (unsigned char *)memcpy(calloc(1, sizeof(unsigned int) * 6), mac, 6);
}

/**************************************************************************************************************************/
/** Read a packet from a file descriptor, retrieving additional header information
 *
 * Abstracts away the complexity of using the complexity of using recvmsg().
 *
 * In addition to reading data from the file descriptor, the src and dst addresses
 * and the receiving interface index are retrieved.  This enables us to send
 * replies using the correct IP interface, in the case where the server is multihomed.
 * This is not normally possible on unconnected datagram sockets.
 *
 * @param[in] fd	The file descriptor to read from.
 * @param[out] buf	Where to write the received datagram data.
 * @param[in] len	of buf.
 * @param[in] flags	passed unmolested to recvmsg.
 * @param[out] from	Where to write the source address.
 * @param[in] from_len	Length of the structure pointed to by from.
 * @param[out] to	Where to write the destination address.  If NULL recvmsg()
 *			will be used instead.
 * @param[in] to_len	Length of the structure pointed to by to.
 * @param[out] if_index	The interface which received the datagram (may be NULL).
 *			Will only be populated if to is not NULL.
 * @param[out] when	the packet was received (may be NULL).  If SO_TIMESTAMP is
 *			not available or SO_TIMESTAMP Was not set on the socket,
 *			then another method will be used instead to get the time.
 * @return
 *	- 0 on success.
 *	- -1 on failure.
 */
int brb_recvfromto(int fd, void *buf, size_t len, int flags, struct sockaddr_storage *from, socklen_t *from_len, struct sockaddr_storage *to, socklen_t *to_len, int *if_index)
{
	struct msghdr msgh;
	struct cmsghdr *cmsg;
	struct iovec iov;
	char cbuf[256];
	int ret;
	struct sockaddr_storage	si;
	socklen_t		si_len = sizeof(si);

#if !defined(IP_PKTINFO) && !defined(IP_RECVDSTADDR) && !defined(IPV6_PKTINFO)
	/* If the recvmsg() flags aren't defined, fall back to using recvfrom()  */
	to 	= NULL:
#endif

	/*
	 *	Catch the case where the caller passes invalid arguments.
	 */
	if (!to || !to_len)
		return recvfrom(fd, buf, len, flags, (struct sockaddr *)from, from_len);

	/* zero fill info */
	memset(&si, 0, sizeof(si));

	/*
	 *	recvmsg doesn't provide sin_port so we have to
	 *	retrieve it using getsockname().
	 */
	if (getsockname(fd, (struct sockaddr *)&si, &si_len) < 0)
	{
		errno 	= EIO;
		return -1;
	}

	/*
	 *	Initialize the 'to' address.  It may be INADDR_ANY here,
	 *	with a more specific address given by recvmsg(), below.
	 */
	if (si.ss_family == AF_INET)
	{
#if !defined(IP_PKTINFO) && !defined(IP_RECVDSTADDR)
		return recvfrom(fd, buf, len, flags, (struct sockaddr *)from, from_len);
#else
		if (*to_len < sizeof(struct sockaddr_in))
		{
			errno 	= EINVAL;
			return -1;
		}

		*to_len 	= sizeof(struct sockaddr_in);
		memcpy(to, &si, sizeof(struct sockaddr_in));
#endif
	}

#ifdef AF_INET6
	else if (si.ss_family == AF_INET6)
	{
#if !defined(IPV6_PKTINFO)
		return recvfrom(fd, buf, len, flags, from, from_len);
#else
		if (*to_len < sizeof(struct sockaddr_in6))
		{
			errno 	= EINVAL;
			return -1;
		}

		*to_len 	= sizeof(struct sockaddr_in6);
		memcpy(to, &si, sizeof(struct sockaddr_in6));
#endif
	}
#endif
	/*
	 *	Unknown address family.
	 */
	else
	{
		errno = EINVAL;
		return -1;
	}

	/* Set up iov and msgh structures. */
	memset(&cbuf, 0, sizeof(cbuf));
	memset(&msgh, 0, sizeof(struct msghdr));
	iov.iov_base 		= buf;
	iov.iov_len  		= len;
	msgh.msg_control 	= cbuf;
	msgh.msg_controllen = sizeof(cbuf);
	msgh.msg_name 		= from;
	msgh.msg_namelen 	= from_len ? *from_len : 0;
	msgh.msg_iov 		= &iov;
	msgh.msg_iovlen 	= 1;
	msgh.msg_flags 		= 0;

	/* Receive one packet. */
	ret 				= recvmsg(fd, &msgh, flags);

	if (ret < 0)
		return ret;

	if (from_len)
		*from_len 		= msgh.msg_namelen;

	if (if_index)
		*if_index 		= 0;

	/* Process auxiliary received data in msgh */
	for (cmsg = CMSG_FIRSTHDR(&msgh); cmsg != NULL; cmsg = CMSG_NXTHDR(&msgh, cmsg))
	{
#ifdef IP_PKTINFO
		if ((cmsg->cmsg_level == SOL_IP) && (cmsg->cmsg_type == IP_PKTINFO))
		{
			struct in_pktinfo *i 	= (struct in_pktinfo *) CMSG_DATA(cmsg);
			satosin(to)->sin_addr 	= i->ipi_addr;

#if !defined(__linux__)
			to->sa_family 			= AF_INET;
#endif
			*to_len 				= sizeof(struct sockaddr_in);

			if (if_index)
				*if_index 			= i->ipi_ifindex;

			break;
		}
#endif

#ifdef IP_RECVDSTADDR
		if ((cmsg->cmsg_level == IPPROTO_IP) && (cmsg->cmsg_type == IP_RECVDSTADDR))
		{
			struct in_addr *i 		= (struct in_addr *) CMSG_DATA(cmsg);
			satosin(to)->sin_addr 	= *i;
			to->ss_family 			= AF_INET;
			*to_len 				= sizeof(struct sockaddr_in);

			break;
		}
#endif

#ifdef IPV6_PKTINFO
		if ((cmsg->cmsg_level == IPPROTO_IPV6) && (cmsg->cmsg_type == IPV6_PKTINFO))
		{
			struct in6_pktinfo *i 	= (struct in6_pktinfo *) CMSG_DATA(cmsg);
			satosin6(to)->sin6_addr = i->ipi6_addr;
			to->ss_family 			= AF_INET6;
			*to_len 				= sizeof(struct sockaddr_in6);

			if (if_index)
				*if_index 			= i->ipi6_ifindex;

			break;
		}
#endif


//#ifdef SO_TIMESTAMP
//		if (when && (cmsg->cmsg_level == SOL_IP) && (cmsg->cmsg_type == SO_TIMESTAMP)) {
//			*when = fr_time_from_timeval((struct timeval *)CMSG_DATA(cmsg));
//		}
//#endif
	}

	return ret;
}
/**************************************************************************************************************************/
/** Send packet via a file descriptor, setting the src address and outbound interface
 *
 * Abstracts away the complexity of using the complexity of using sendmsg().
 *
 * @param[in] fd	The file descriptor to write to.
 * @param[in] buf	Where to read datagram data from.
 * @param[in] len	of datagram data.
 * @param[in] flags	passed unmolested to sendmsg.
 * @param[in] from	The source address.
 * @param[in] from_len	Length of the structure pointed to by from.
 * @param[in] to	The destination address.
 * @param[in] to_len	Length of the structure pointed to by to.
 * @param[in] if_index	The interface on which to send the datagram.
 *			If automatic interface selection is desired, value should be 0.
 * @return
 *	- 0 on success.
 *	- -1 on failure.
 */
int brb_sendfromto(int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t from_len, struct sockaddr *to, socklen_t to_len, int if_index)
{
	struct msghdr msgh 	= {0};
	struct iovec iov 	= {0};
	char cbuf[256] 		= {0};
	int op_status 		= 0;

	/* Unknown address family */
	if (from && (from->sa_family != AF_INET) && (from->sa_family != AF_INET6))
	{
//		errno 	= ENOENT;
//		return -1;

		/* just use regular sendto */
		op_status 	= sendto(fd, buf, len, flags, to, to_len);

		if (op_status < 0)
			return -2;

		return op_status;
	}

#ifdef __FreeBSD__
	/*
	 *	FreeBSD is extra pedantic about the use of IP_SENDSRCADDR,
	 *	and sendmsg will fail with EINVAL if IP_SENDSRCADDR is used
	 *	with a socket which is bound to something other than
	 *	INADDR_ANY
	 */
	struct sockaddr bound 	= {0};
	socklen_t bound_len 	= sizeof(bound);

	if (getsockname(fd, &bound, &bound_len) < 0)
	{
		errno 		= EBADF;
		return -1;
	}

	switch (bound.sa_family)
	{
	case AF_INET:
		if (satosin(&bound)->sin_addr.s_addr != INADDR_ANY)
			from 	= NULL;
		break;
	case AF_INET6:
		if (!IN6_IS_ADDR_UNSPECIFIED(&satosin6(&bound)->sin6_addr))
			from 	= NULL;
		break;
	}
#endif	/* !__FreeBSD__ */

	/*
	 *	If the sendmsg() flags aren't defined, fall back to
	 *	using sendto().  These flags are defined on FreeBSD,
	 *	but laying it out this way simplifies the look of the
	 *	code.
	 */
#  if !defined(IP_PKTINFO) && !defined(IP_SENDSRCADDR)
	if (from && from->sa_family == AF_INET)
		from 	= NULL;
#  endif

#  if !defined(IPV6_PKTINFO)
	if (from && from->sa_family == AF_INET6)
		from 	= NULL;
#  endif

	/* No "from", just use regular sendto */
	if (!from || (from_len == 0))
	{
		op_status 	= sendto(fd, buf, len, flags, to, to_len);

		if (op_status < 0)
			return -2;

		return op_status;
	}

	/* Set up control buffer iov and msgh structures. */
	memset(&cbuf, 0, sizeof(cbuf));
	memset(&msgh, 0, sizeof(msgh));
	memset(&iov, 0, sizeof(iov));
	iov.iov_base 		= buf;
	iov.iov_len 		= len;

	msgh.msg_iov 		= &iov;
	msgh.msg_iovlen 	= 1;
	msgh.msg_name 		= to;
	msgh.msg_namelen 	= to_len;

# if defined(IP_PKTINFO) || defined(IP_SENDSRCADDR)

	if (from->sa_family == AF_INET)
	{
		struct sockaddr_in *s4 = (struct sockaddr_in *)from;

#  ifdef IP_PKTINFO
		struct cmsghdr *cmsg;
		struct in_pktinfo *pkt;

		msgh.msg_control 	= cbuf;
		msgh.msg_controllen = CMSG_SPACE(sizeof(*pkt));

		cmsg 				= CMSG_FIRSTHDR(&msgh);
		cmsg->cmsg_level 	= SOL_IP;
		cmsg->cmsg_type 	= IP_PKTINFO;
		cmsg->cmsg_len 		= CMSG_LEN(sizeof(*pkt));

		pkt 				= (struct in_pktinfo *)CMSG_DATA(cmsg);
		memset(pkt, 0, sizeof(*pkt));
		pkt->ipi_spec_dst 	= s4->sin_addr;
		pkt->ipi_ifindex 	= if_index;

#  elif defined(IP_SENDSRCADDR)
		struct cmsghdr *cmsg;
		struct in_addr *in;

		msgh.msg_control 	= cbuf;
		msgh.msg_controllen = CMSG_SPACE(sizeof(*in));
//		msgh.msg_controllen = CMSG_SPACE(sizeof(struct in_addr));

		cmsg = CMSG_FIRSTHDR(&msgh);
		cmsg->cmsg_level 	= IPPROTO_IP;
		cmsg->cmsg_type 	= IP_SENDSRCADDR;
		cmsg->cmsg_len 		= CMSG_LEN(sizeof(*in));
//		cmsg->cmsg_len 		= CMSG_LEN(sizeof(struct in_addr));

		in 					= (struct in_addr *)CMSG_DATA(cmsg);
		*in 				= s4->sin_addr;

//		msgh.msg_controllen = cmsg->cmsg_len;
#  endif
	}

#endif

#  if defined(IPV6_PKTINFO)

	if (from->sa_family == AF_INET6)
	{
		struct sockaddr_in6 *s6 = (struct sockaddr_in6 *)from;

		struct cmsghdr *cmsg;
		struct in6_pktinfo *pkt;

		msgh.msg_control 	= cbuf;
		msgh.msg_controllen = CMSG_SPACE(sizeof(struct in6_pktinfo));

		cmsg = CMSG_FIRSTHDR(&msgh);
		cmsg->cmsg_level 	= IPPROTO_IPV6;
		cmsg->cmsg_type 	= IPV6_PKTINFO;
		cmsg->cmsg_len 		= CMSG_LEN(sizeof(struct in6_pktinfo));

		pkt 				= (struct in6_pktinfo *) CMSG_DATA(cmsg);
		memset(pkt, 0, sizeof(struct in6_pktinfo));
		pkt->ipi6_addr 		= s6->sin6_addr;
		pkt->ipi6_ifindex 	= if_index;
	}

#  endif	/* IPV6_PKTINFO */

	/* Send the message */
	op_status 	= sendmsg(fd, &msgh, flags);

	if (op_status < 0)
		return -3;

	return op_status;
}
/**************************************************************************************************************************/

/**************************************************************************************************************************/

