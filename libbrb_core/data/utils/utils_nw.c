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

		return 1;
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

		return 1;
	}
	case AF_INET6:
	{
		static char buf_ptr[INET6_ADDRSTRLEN + 1];
		char *inet_ptr;

		inet_ptr 		= inet_ntop(AF_INET6, &satosin6(sa)->sin6_addr, buf_ptr, sizeof(buf_ptr));

		if (!inet_ptr)
			return 0;

		cur_sz 			+= snprintf(ret_data_ptr, ret_maxlen, "%s", buf_ptr);

		/* If have port */
		if (ntohs(satosin6(sa)->sin6_port) != 0)
			cur_sz 		+= snprintf(ret_data_ptr + cur_sz, ret_maxlen, ".%d", ntohs(satosin6(sa)->sin6_port));

		return 1;
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

			strlcpy(ret_data_ptr, sdl->sdl_data, (sdl->sdl_nlen + 1));
			//printf("SysControl_NetworkSockNtop - AFF_LINK -> [%d]-[%s]-[%s]\n", sdl->sdl_nlen, sdl->sdl_data, ret_data_ptr);

		}
		else
			snprintf(ret_data_ptr, ret_maxlen, "link#%d", sdl->sdl_index);

		return 1;
	}
#endif
	default:
		snprintf(ret_data_ptr, ret_maxlen, "unknown %d", sa->sa_family);
		return 0;
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

