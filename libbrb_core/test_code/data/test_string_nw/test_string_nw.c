/*
 * test_string_nw.c
 *
 *  Created on: 2019-11-14
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *
 *
 * Copyright (c) 2018 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#include "libbrb_core.h"

/**************************************************************************************************************************/
static int ParseAddress(struct sockaddr_storage *target_sockaddr, struct sockaddr_storage *mask_sockaddr, char *ip_cidr_str, char *addr_str, int addr_sz, char *mask_str, int mask_sz)
{
	char *mask_ptr;
	int mask_num;
	int af_type;

	if (!ip_cidr_str)
		return -1;

	/* Grab the CIDR prefix */
	mask_ptr 						= strchr(ip_cidr_str, '/');

	if (!mask_ptr)
		return -1;

	/* convert / to finish address string  */
	mask_ptr[0] 					= '\0';
	mask_num						= atoi(&mask_ptr[1]);

	/* Check address */
	af_type 						= BrbIsValidIpToSockAddr(ip_cidr_str, target_sockaddr);

	/* convert again to / */
	mask_ptr[0] 					= '/';

	if (af_type != AF_UNSPEC)
	{
		/* Get mask width */
		if (mask_num < 0)
			mask_num 		= 0;

		if (af_type == AF_INET)
		{
			target_sockaddr->ss_family 	= AF_INET;
			target_sockaddr->ss_len 		= sizeof(struct sockaddr_in);

			mask_sockaddr->ss_family 	= AF_INET;
			mask_sockaddr->ss_len 		= sizeof(struct sockaddr_in);

			BrbNw_Ipv4Netmask(&((struct sockaddr_in *)mask_sockaddr)->sin_addr, mask_num);
			BrbNw_Ipv4Mask(&((struct sockaddr_in *)target_sockaddr)->sin_addr, &((struct sockaddr_in *)mask_sockaddr)->sin_addr);
		}
		else if (af_type == AF_INET6)
		{
			target_sockaddr->ss_family 	= AF_INET6;
			target_sockaddr->ss_len 		= sizeof(struct sockaddr_in6);

			mask_sockaddr->ss_family 	= AF_INET6;
			mask_sockaddr->ss_len 		= sizeof(struct sockaddr_in6);

			BrbNw_Ipv6Netmask(&((struct sockaddr_in6 *)mask_sockaddr)->sin6_addr, mask_num);
			BrbNw_Ipv6Mask(&((struct sockaddr_in6 *)target_sockaddr)->sin6_addr, &((struct sockaddr_in6 *)mask_sockaddr)->sin6_addr);
		}

		BrbNetworkSockNtop(addr_str, addr_sz, (const struct sockaddr *)target_sockaddr, target_sockaddr->ss_len);
		BrbNetworkSockMask(mask_str, mask_sz, (const struct sockaddr *)mask_sockaddr);
	}
	else
	{
		strlcpy(addr_str, "0.0.0.0", 64);
		strlcpy(mask_str, "/0", 32);
	}

	return 0;
}
/**************************************************************************************************************************/
static int MatchAddress(struct sockaddr_storage *prefix_addr, struct sockaddr_storage *mask_addr, void *addr_ptr, int addr_family, int addr_not)
{
	if ((addr_family == 0) && (BrbNw_Ipv4CompareMasked(&((struct sockaddr_in *)prefix_addr)->sin_addr, addr_ptr, &((struct sockaddr_in *)mask_addr)->sin_addr) == 0))
		return (addr_not ? 0 : 1);
	else if ((addr_family == 1) && BrbNw_Ipv6CompareMasked(&((struct sockaddr_in6 *)prefix_addr)->sin6_addr, addr_ptr, &((struct sockaddr_in6 *)mask_addr)->sin6_addr) == 0)
		return (addr_not ? 0 : 1);

	return (addr_not ? 1 : 0);
}
/************************************************************************************************************************/
int main(int argc, char **argv)
{
	struct sockaddr_storage target_sockaddr = {0};
	struct sockaddr_storage mask_sockaddr 	= {0};
	char addr_str[64] 						= {0};
	char mask_str[64] 						= {0};

	char ip_cidr_str[64] 					= "192.168.142.18/32";
	char ip_addr_str[64] 					= "192.168.142.18";

	char *ip_cidr_ptr			= (char *)&ip_cidr_str;
	char *ip_addr_ptr			= (char *)&ip_addr_str;
	struct in_addr addr_data;
	int op_status;

	printf("STARTING\n");

	addr_data.s_addr 			= inet_addr(ip_addr_ptr);

	printf("SEARCHING %s in %s\n", ip_addr_ptr, ip_cidr_ptr);

	ParseAddress(&target_sockaddr, &mask_sockaddr, ip_cidr_str, addr_str, sizeof(addr_str), mask_str, sizeof(mask_str));

	op_status 					= MatchAddress(&target_sockaddr, &mask_sockaddr, &addr_data, 0, 0);

	printf("%s%s - %s\n", (char *)&addr_str, (char *)&mask_str,  op_status ? "FOUND" : "NOTHING");


	return 0;
}
/************************************************************************************************************************/
