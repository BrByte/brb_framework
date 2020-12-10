/*
 * test_thandler.c
 *
 *  Created on: 2011-10-11
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

#include <libbrb_core.h>

//typedef std::function<void(void)> THandlerFunction;
//typedef void THandlerFunction(void);
//
////typedef void THandlerCallback;
//
//static THandlerFunction test_hdr;
//
//static void test_hdr(char *test_str, int number);

typedef struct _BrbFlowRecord
{
	uint16_t group_id;
	uint8_t proto_id;

	struct
	{
		uint32_t ingress;
		uint32_t egress;
	} interface;

	struct
	{
		unsigned char nat_src[6];
		unsigned char src[6];
		unsigned char dst[6];
	} mac;

	struct
	{
		struct
		{
			uint8_t family;
			struct in_addr  v4;
			struct in6_addr v6;
		} src;

		struct
		{
			uint8_t family;
			struct in_addr  v4;
			struct in6_addr v6;
		} dst;
	} addr;

	struct
	{
		uint64_t total_len;
		uint32_t as_number;

		uint16_t port_src;
		uint16_t port_dst;

		uint8_t igmp_type;
		uint8_t hdr_len;

		uint8_t ver;
		uint8_t ttl;

		uint8_t is_mcast;
		uint8_t cos;

		struct
		{
			struct in_addr nexthop;
			uint16_t srcaddr_pref_len;
			uint16_t dstaddr_pref_len;
		} v4;

	} ip;

	struct
	{
		uint8_t type;
		uint8_t code;
	} icmp;

	struct
	{
		uint16_t msg_len;
	} udp;

	struct
	{
		// 8 bits
		uint32_t window;
		uint32_t seq;
		uint32_t ack;
		uint16_t ctl_bits;
	} tcp;

	struct
	{
		struct in_addr  addr_src;
		struct in_addr  addr_dst;
		uint16_t port_src;
		uint16_t port_dst;
	} nat44;

	struct
	{
		uint64_t octets_delta;
		uint64_t packets_delta;

		struct
		{
			struct timeval start;	/* As sent by exporter */
			struct timeval end;
			struct timeval first;	/* When this starts from my view */
		} tv;
	} stats;

} BrbFlowRecord;


typedef struct _BrbFlowRecord2
{
	uint16_t group_id;
	uint32_t as_number;
	uint32_t ingress;
	uint32_t egress;

	unsigned char nat_src[6];
	unsigned char src[6];
	unsigned char dst[6];
	uint8_t proto_id;
	uint8_t sfamily;
	struct in_addr  sv4;
	struct in6_addr sv6;
	uint8_t dfamily;
	struct in_addr  dv4;
	struct in6_addr dv6;
	uint64_t total_len;
	uint16_t port_src;
	uint16_t port_dst;
	uint8_t igmp_type;
	uint8_t hdr_len;
	uint8_t ver;
	uint8_t ttl;
	uint8_t is_mcast;
	uint8_t cos;
	struct in_addr nexthop;
	uint16_t srcaddr_pref_len;
	uint16_t dstaddr_pref_len;
	uint16_t msg_len;
	uint32_t window;
	uint32_t seq;
	uint32_t ack;
	uint16_t ctl_bits;
	uint8_t type;
	uint8_t code;
	struct in_addr  addr_src;
	struct in_addr  addr_dst;
	uint16_t nport_src;
	uint16_t nport_dst;
	uint64_t octets_delta;
	uint64_t packets_delta;

	struct timeval start;	/* As sent by exporter */
	struct timeval end;
	struct timeval first;	/* When this starts from my view */

} BrbFlowRecord2;
/************************************************************/
typedef struct _ConnDBNAT44Record
{
	uint8_t prot;		/* 1 */
	uint8_t start_d:4;
	uint8_t stop_d:4;
	uint8_t start_h;
	uint8_t start_i;
	uint8_t start_s;
	uint8_t stop_h;
	uint8_t stop_i;
	uint8_t stop_s;

	struct in_addr  sa;	/* 4 */
	struct in_addr  da;	/* 4 */
	uint16_t sp;		/* 2 */
	uint16_t dp;		/* 2 */

	struct {
		struct in_addr  sa;
		struct in_addr  da;
		uint16_t sp;
		uint16_t dp;
	} nat;

} ConnDBNAT44Record;
/************************************************************/
typedef struct _ConnDBNAT44Record2
{
	uint8_t prot;		/* 1 */
	uint8_t start_d:4;
	uint8_t stop_d:4;
	uint8_t start_h;
	uint8_t start_i;
	uint8_t start_s;
	uint8_t stop_h;
	uint8_t stop_i;
	uint8_t stop_s;

	uint32_t s_asn;
	uint32_t d_asn;

	struct in_addr  sa;	/* 4 */
	struct in_addr  da;	/* 4 */
	uint16_t sp;		/* 2 */
	uint16_t dp;		/* 2 */

	struct {
		struct in_addr  sa;
		struct in_addr  da;
		uint16_t sp;
		uint16_t dp;
	} nat;

} ConnDBNAT44Record2;
/**************************************************************************************************************************/
//void TestCallback(char *test_str, THandlerFunction cb_fund)
//{
//	printf("TESTE [%s] - [%p]\n", test_str, cb_fund);
//
//} // A function taking a function pointer as argument.
// Call the function `f` `nb` times.

int main()
{
	int a = 128;

	printf("BrbFlowRecord 224 %lu\n", sizeof(BrbFlowRecord));
	printf("BrbFlowRecord2 %lu\n", sizeof(BrbFlowRecord2));
	printf("ConnDBNAT44Record %lu\n", sizeof(ConnDBNAT44Record));
	printf("ConnDBNAT44Record2 %lu\n", sizeof(ConnDBNAT44Record2));

	struct sockaddr_storage ip_addr 	= {0};

	BrbIsValidIpToSockAddr("0.0.0.0", &ip_addr);

	printf("equals %u\n", ((struct sockaddr_in *)&ip_addr)->sin_addr.s_addr == ntohl(0));

	BrbIsValidIpToSockAddr("255.255.255.255", &ip_addr);

	printf("equals %u\n", ((struct sockaddr_in *)&ip_addr)->sin_addr.s_addr == ntohl(0xFFFFFFFF));


}
/**************************************************************************************************************************/
