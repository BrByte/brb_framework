/*
 * comm_icmp_base.c
 *
 *  Created on: 2013-01-26
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2013 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#include <netinet/icmp6.h>

static int CommEvICMPCheckSum(unsigned short *ptr, int size);
static int CommEvICMPGuessHopsFromTTL(int ttl);

static EvBaseKQCBH CommEvICMPBaseEventRead;
static EvBaseKQCBH CommEvICMPBaseEventWrite;
static EvBaseKQCBH CommEvICMPBaseTimeoutTimer;
static int CommEvICMPBaseTimeoutCheckPending(EvICMPBase *icmp_base, EvICMPPending *icmp_pending);

/**************************************************************************************************************************/
EvICMPBase *CommEvICMPBaseNew(EvKQBase *kq_base)
{
	EvICMPBase *icmp_base;

	/* Create new ICMP base and save reference with parent KQ_BASE */
	icmp_base						= calloc(1, sizeof(EvICMPBase));
	icmp_base->ev_base				= kq_base;

	/* Initialize pending request list, slots and arena */
	icmp_base->pending_v4.arena 	= MemArenaNew(1024, (sizeof(ICMPQueryInfo) + 1), 128, MEMARENA_MT_UNSAFE);
	icmp_base->pending_v6.arena 	= MemArenaNew(1024, (sizeof(ICMPQueryInfo) + 1), 128, MEMARENA_MT_UNSAFE);

	DLinkedListInit(&icmp_base->pending_v4.req_list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&icmp_base->pending_v4.reply_list, BRBDATA_THREAD_UNSAFE);

	DLinkedListInit(&icmp_base->pending_v6.req_list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&icmp_base->pending_v6.reply_list, BRBDATA_THREAD_UNSAFE);

	SlotQueueInit(&icmp_base->pending_v4.slots, ICMP_QUERY_MAX_SEQID, (kq_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));
	SlotQueueInit(&icmp_base->pending_v6.slots, ICMP_QUERY_MAX_SEQID, (kq_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));

	/* Create a new RAW socket and set it to non_blocking */
	icmp_base->socket_fdv4 	= EvKQBaseSocketRAWNew(icmp_base->ev_base, AF_INET, IPPROTO_ICMP);
	icmp_base->socket_fdv6 	= EvKQBaseSocketRAWNew(icmp_base->ev_base, AF_INET6, IPPROTO_ICMPV6);

	EvKQBaseSocketSetNonBlock(icmp_base->ev_base, icmp_base->socket_fdv4);
	EvKQBaseSocketSetNonBlock(icmp_base->ev_base, icmp_base->socket_fdv6);

	/* Set socket description */
	EvKQBaseFDDescriptionSetByFD(icmp_base->ev_base, icmp_base->socket_fdv4 , "BRB_EV_COMM - ICMP BASE V4");
	EvKQBaseFDDescriptionSetByFD(icmp_base->ev_base, icmp_base->socket_fdv6 , "BRB_EV_COMM - ICMP BASE V6");

	/* Initialize timeout timer */
	icmp_base->timer_id			= -1;
	icmp_base->min_seen_timeout = ICMP_BASE_TIMEOUT_TIMER;

	return icmp_base;
}
/**************************************************************************************************************************/
void CommEvICMPBaseDestroy(EvICMPBase *icmp_base)
{
	/* Sanity check */
	if (!icmp_base)
		return;

	MemArenaDestroy(icmp_base->pending_v4.arena);
	MemArenaDestroy(icmp_base->pending_v6.arena);
	EvKQBaseTimerCtl(icmp_base->ev_base, icmp_base->timer_id, COMM_ACTION_DELETE);
	EvKQBaseSocketClose(icmp_base->ev_base, icmp_base->socket_fdv4);
	EvKQBaseSocketClose(icmp_base->ev_base, icmp_base->socket_fdv6);

	free(icmp_base);

	return;
}
/**************************************************************************************************************************/
int CommEvICMPRequestCancelByReqID(EvICMPBase *icmp_base, EvICMPPending *icmp_pending, int req_id)
{
	ICMPQueryInfo *icmp_queryinfo;
	DLinkedList *target_list;
	int slot_id;

	/* Sanity check */
	if (!icmp_base)
		return 0;

	/* Grab ICMP_QUERY info from arena, and be sure we are trying to cancel an active request */
	icmp_queryinfo 	= MemArenaGrabByID(icmp_pending->arena, req_id);
	assert(icmp_queryinfo->flags.in_use);

	/* Grab Pending or Reply List */
	target_list 	= (!icmp_queryinfo->flags.waiting_reply ? &icmp_pending->req_list: &icmp_pending->reply_list);
	slot_id			= icmp_queryinfo->slot_id;

	/* Free slot and delete from pending list and memory area */
	SlotQueueFree(&icmp_pending->slots, slot_id);
	DLinkedListDelete(target_list, &icmp_queryinfo->node);
	memset(icmp_queryinfo, 0, sizeof(ICMPQueryInfo));

	/* Release SLOT */
	MemArenaReleaseByID(icmp_pending->arena, req_id);

	return 1;
}
/**************************************************************************************************************************/
int CommEvICMPRequestCancelByOwnerID(EvICMPBase *icmp_base, EvICMPPending *icmp_pending, int owner_id)
{
	ICMPQueryInfo *icmp_queryinfo;
	DLinkedListNode *node;

	int cancel_count = 0;

	/* Walk all pending REQUEST LIST */
	node = icmp_pending->req_list.head;
	while (node)
	{
		/* Sanity check */
		if (!node)
			break;

		/* Grab ICMP INFO and calculate DELTA TX */
		icmp_queryinfo	= node->data;
		node			= node->next;

		/* Found owner, drop */
		if (icmp_queryinfo->owner_id == owner_id)
		{
			CommEvICMPRequestCancelByReqID(icmp_base, icmp_pending, icmp_queryinfo->slot_id);
			cancel_count++;
		}

		continue;
	}

	/* Walk all pending REPLY LIST */
	node = icmp_pending->reply_list.head;
	while (node)
	{
		/* Sanity check */
		if (!node)
			break;

		/* Grab ICMP INFO and calculate DELTA TX */
		icmp_queryinfo	= node->data;
		node			= node->next;

		/* Found owner, drop */
		if (icmp_queryinfo->owner_id == owner_id)
		{
			CommEvICMPRequestCancelByReqID(icmp_base, icmp_pending, icmp_queryinfo->slot_id);
			cancel_count++;
		}

		continue;
	}

	return cancel_count;
}
/**************************************************************************************************************************/
int CommEvICMPRequestSend(EvICMPBase *icmp_base, struct sockaddr_storage *sockaddr, int type, int code, int seq, int timeout_ms, char *payload, int payload_len,
		CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id)
{
	EvICMPPending *icmp_pending;
	ICMPQueryInfo *icmp_queryinfo;
	struct timeval *current_time_ptr;
	struct timeval current_time;
	int socked_fd;
	unsigned short i;
	int op_status;
	int slot_id;

	/* Sanity check */
	if (timeout_ms <= 0)
	{
		KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "Invalid timeout value [%d]\n", timeout_ms);
		return -1;
	}

	if (sockaddr->ss_family == AF_INET6)
	{
		icmp_pending				= &icmp_base->pending_v6;
		socked_fd 					= icmp_base->socket_fdv6;
	}
	else
	{
		icmp_pending				= &icmp_base->pending_v4;
		socked_fd 					= icmp_base->socket_fdv4;
	}

	/* Beware signed/unsigned issues in untrusted data from the network!!  */
	if (payload_len < 0)
		payload_len = 0;

	slot_id 						= SlotQueueGrab(&icmp_pending->slots);

	/* No more slots */
	if (slot_id < 0)
	{
		KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - FAMILY [%d] - No more available slots to send request!\n",
				socked_fd, sockaddr->ss_family);
		return -1;
	}

	/* Adjust minimum TIMEOUT */
	if (icmp_base->min_seen_timeout > timeout_ms)
	{
		icmp_base->min_seen_timeout = timeout_ms;
		KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Minimum SEEN_TIMEOUT set to [%d]\n", icmp_base->min_seen_timeout);
	}
	else
		KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "TIMEOUT_ASKED [%d] - MIN_SEEN [%d] - KEPT\n", timeout_ms, icmp_base->min_seen_timeout);

	icmp_queryinfo 					= MemArenaGrabByID(icmp_pending->arena, slot_id);

	/* FATAL: Already in use, bail out */
	assert(!icmp_queryinfo->flags.in_use);

	/* Clean query info */
	memset(icmp_queryinfo, 0, sizeof(ICMPQueryInfo));

	icmp_queryinfo->flags.in_use	= 1;
	icmp_queryinfo->slot_id			= slot_id;
	icmp_queryinfo->owner_id		= owner_id;

	/* Fill cb_data structure */
	icmp_queryinfo->cb_handler		= cb_handler;
	icmp_queryinfo->cb_data			= cb_data;

	/* Initialize packet size with just headers, there is no payload yet */
	icmp_queryinfo->icmp_packet_sz = sizeof(CommEvICMPHeader) - COMM_ICMP_MAX_PAYLOAD;

	/* Save for timeout purposes */
	icmp_queryinfo->timeout_ms					= timeout_ms;
	icmp_queryinfo->icmp_packet.icmp_type		= type;
	icmp_queryinfo->icmp_packet.icmp_code 		= code;
	icmp_queryinfo->icmp_packet.icmp_id			= slot_id;
	icmp_queryinfo->icmp_packet.icmp_seq		= seq;
	icmp_queryinfo->icmp_packet.icmp_cksum		= 0;
//	struct icmp_hdr icmp_hdr;
//	struct icmp6_hdr icmp_hdr6;
//	struct icmp;
//	struct packet;
	/* Get current TIMESTAMP only if its not inside ev_base */
	if (0 == icmp_base->ev_base->stats.cur_invoke_tv.tv_sec)
	{
		gettimeofday(&current_time, NULL);
		current_time_ptr = &current_time;
	}
	else
		current_time_ptr = &icmp_base->ev_base->stats.cur_invoke_tv;

	/* Get current time_stamps */
	memcpy(&icmp_queryinfo->transmit_time, current_time_ptr, sizeof(struct timeval));

	/* There is a pay_load, copy it into packet buffer */
	if (payload)
	{
		if (payload_len > COMM_ICMP_MAX_PAYLOAD)
			payload_len = COMM_ICMP_MAX_PAYLOAD;

		memcpy(&icmp_queryinfo->icmp_packet.payload, payload, payload_len);
		icmp_queryinfo->icmp_packet_sz += payload_len;
	}

	/* Calculate the checksum for the whole packet */
	icmp_queryinfo->icmp_packet.icmp_cksum	= CommEvICMPCheckSum((unsigned short *)&icmp_queryinfo->icmp_packet, icmp_queryinfo->icmp_packet_sz);

	/* Fill in the stub sockaddr_storage structure */
	memcpy(&icmp_queryinfo->target_sockaddr, sockaddr, sizeof(struct sockaddr_storage));

	/* Add to pending request list */
	DLinkedListAddTail(&icmp_pending->req_list, &icmp_queryinfo->node, icmp_queryinfo);

	/* Schedule write event for this packet */
	EvKQBaseSetEvent(icmp_base->ev_base, socked_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvICMPBaseEventWrite, icmp_base);

	/* Schedule timeout timer if NONE if RUNNING */
	if (icmp_base->timer_id < 0)
	{
		icmp_base->timer_id = EvKQBaseTimerAdd(icmp_base->ev_base, COMM_ACTION_ADD_VOLATILE, icmp_base->min_seen_timeout, CommEvICMPBaseTimeoutTimer, icmp_base);
		KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Schedule TIMEOUT TIMER_ID at [%d]\n", icmp_base->timer_id);
	}
	else
		KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Will not schedule TIMEOUT TIMER - Already at [%d]\n", icmp_base->timer_id);

	return slot_id;
}
/**************************************************************************************************************************/
int CommEvICMPEchoSendRequestBySockAddr(EvICMPBase *icmp_base, struct sockaddr_storage *sockaddr, int code, int seq, int size, int timeout_ms, CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id)
{
	char buf_addr[128];
	int op_status;
	int i;

	int timeval_sz = sizeof(struct timeval);
	int payload_sz = timeval_sz + size;

	char payload[payload_sz];
	char *payload_dstptr = &payload[timeval_sz];

	/* Sanity check */
	if (!icmp_base)
		return -1;

	/* Initialize local time and target_addr info */
	gettimeofday((struct timeval*)&payload, NULL);

	/* Generate randomic_payload */
	for (i = 0; i < size; i++)
		payload_dstptr[i] = arc4random();

	/* Make the ICMP_ECHO request sending current_time as pay_load */
	if (sockaddr->ss_family == AF_INET6)
		op_status 	= CommEvICMPRequestSend(icmp_base, sockaddr, 128, code, seq, timeout_ms, (char*)&payload, payload_sz, cb_handler, cb_data, owner_id);
	else
		op_status 	= CommEvICMPRequestSend(icmp_base, sockaddr, 8, code, seq, timeout_ms, (char*)&payload, payload_sz, cb_handler, cb_data, owner_id);

	payload_dstptr 	= (sockaddr->ss_family == AF_INET6) ? (char *)&((struct sockaddr_in6 *)sockaddr)->sin6_addr : (char *)&((struct sockaddr_in *)sockaddr)->sin_addr;

	/* Failed writing ICMP echo request */
	if (op_status < 0)
	{
		inet_ntop(sockaddr->ss_family, payload_dstptr, (char *)&buf_addr, sizeof(buf_addr));

		KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "Failed trying to send ICMP_ECHO to ADDR [%s]\n", (char *)&buf_addr);

		return -1;
	}

//	KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Send echo with ID [%d] to ADDR [%s]\n", op_status, inet_ntop(sockaddr->ss_family, payload_dstptr, &buf_addr, sizeof(buf_addr)));

	return op_status;
}
/**************************************************************************************************************************/
int CommEvICMPEchoSendRequest(EvICMPBase *icmp_base, char *addr_str, int code, int seq, int size, int timeout_ms, CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id)
{
	struct sockaddr_storage target_sockaddr;
	int op_status;

	/* Sanity check */
	if (!icmp_base || !addr_str)
		return 0;

	BrbIsValidIpToSockAddr(addr_str, &target_sockaddr);

	/* Check */
	if (target_sockaddr.ss_family == AF_UNSPEC)
		return 0;

	/* Fill TARGET */
	if (target_sockaddr.ss_family == AF_INET6)
		inet_pton(target_sockaddr.ss_family, addr_str, &((struct sockaddr_in6 *)&addr_str)->sin6_addr);
	else if (target_sockaddr.ss_family == AF_INET)
		inet_pton(target_sockaddr.ss_family, addr_str, &((struct sockaddr_in *)&addr_str)->sin_addr);

	op_status			= CommEvICMPEchoSendRequestBySockAddr(icmp_base, &target_sockaddr, code, seq, size, timeout_ms, cb_handler, cb_data, owner_id);

	return op_status;
}
/**************************************************************************************************************************/
float CommEvICMPtvSubMsec(struct timeval *when, struct timeval *now)
{
	return (float)((float)(now->tv_sec - when->tv_sec) * 1000) + ((float)((float)(now->tv_usec - when->tv_usec) / 1000));
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvICMPCheckSum(unsigned short *ptr, int size)
{
	long sum;
	unsigned short oddbyte;
	unsigned short answer;
	sum = 0;

	while (size > 1)
	{
		sum		+= *ptr++;
		size	-= 2;
	}

	if (size == 1)
	{
		oddbyte							= 0;
		*((unsigned char *) &oddbyte)	= *(unsigned char *) ptr;
		sum								+= oddbyte;
	}

	sum		= (sum >> 16) + (sum & 0xffff);
	sum		+= (sum >> 16);
	answer	= (unsigned short) ~sum;

	return answer;
}
/**************************************************************************************************************************/
static int CommEvICMPGuessHopsFromTTL(int ttl)
{
	if (ttl < 33)
		return 33 - ttl;
	if (ttl < 63)
		return 63 - ttl;
	if (ttl < 65)
		return 65 - ttl;
	if (ttl < 129)
		return 129 - ttl;
	if (ttl < 193)
		return 193 - ttl;
	return 256 - ttl;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvICMPBaseEventRead(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvICMPBase *icmp_base = cb_data;
	EvICMPPending *icmp_pending;
	ICMPReply icmp_reply;
	ICMPQueryInfo *icmp_queryinfo;
	CommEvICMPBaseCBH *cb_handler;
	CommEvICMPHeader *icmp_header;
	void *req_cb_data;
	void *ip_hdr;
	char packet_buf[to_read_sz + 16];
	struct sockaddr_storage from_sockaddr;
	socklen_t from_sockaddr_sz;
	int need_sz;
	int data_read;
	int ip_header_sz;
	int icmp_payload_sz;
	int icmp_header_sz;
	int icmp_hopcount;

	if (fd == icmp_base->socket_fdv6)
	{
		icmp_pending			= &icmp_base->pending_v6;
		from_sockaddr_sz 		= sizeof(struct sockaddr_in6);
		need_sz					= sizeof(struct ip6_hdr) + sizeof(CommEvICMPHeader) - COMM_ICMP_MAX_PAYLOAD;
	}
	else
	{
		icmp_pending			= &icmp_base->pending_v4;
		from_sockaddr_sz 		= sizeof(struct sockaddr_in);
		need_sz					= sizeof(CommEvIPHeader) + sizeof(CommEvICMPHeader) - COMM_ICMP_MAX_PAYLOAD;
	}

	/* Reschedule read event */
	EvKQBaseSetEvent(icmp_base->ev_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvICMPBaseEventRead, icmp_base);

	/* Read data from socket */
	data_read 					= recvfrom(fd, &packet_buf, to_read_sz, 0, (struct sockaddr *)&from_sockaddr, &from_sockaddr_sz);

//	KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - READ [%d] of [%d] - NEED [%d] - HDR [%d][%d] - FAMILY [%d]\n",
//			fd, data_read, to_read_sz, need_sz, sizeof(CommEvIPHeader), sizeof(struct ip6_hdr), from_sockaddr.ss_family);
//	EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_DEBUG, &packet_buf, data_read, 8, 4);
//	KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - SIZE [%d]\n", fd, from_sockaddr_sz);
//	EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_DEBUG, (struct sockaddr *)&from_sockaddr, from_sockaddr_sz, 8, 4);

	/* Error reading from ICMP socket, bail out */
	if (data_read < need_sz)
		return 0;

	if (fd == icmp_base->socket_fdv6)
	{
		struct ip6_hdr *ip_header;

		/* Cast recv_icmphdr as pointer to ICMP header within received IpV6 header */
		icmp_header 				= (CommEvICMPHeader *)&packet_buf;
		icmp_header_sz				= sizeof(CommEvICMPHeader) - sizeof(icmp_header->payload);

		/* Cast recv_iphdr as pointer to IPv6 header within received ethernet frame.*/
		ip_header 					= (struct ip6_hdr *)&icmp_header->payload;
		ip_hdr						= NULL;
		ip_header_sz				= sizeof(struct ip6_hdr);
		icmp_payload_sz 			= ip_header->ip6_plen >> 8;
		icmp_hopcount				= CommEvICMPGuessHopsFromTTL(ip_header->ip6_hops);

		/* This actions may happen without echo request, threat as well to manage the route or other things */
		switch (icmp_header->icmp_type)
		{
		case ICMP6_DST_UNREACH:
		{
			char buf_src_ptr[128];
			char buf_dst_ptr[128];
			int op_status;

//			inet_ntop(AF_INET6, &ip_header->ip6_src, buf_src_ptr, sizeof(buf_src_ptr));
//			inet_ntop(AF_INET6, &ip_header->ip6_dst, buf_dst_ptr, sizeof(buf_dst_ptr));
//
//			KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_ORANGE, "FD [%d] - READ [%d] IP SRC [%s] -> DST [%s] - HDR [%d][%d] NEXT [%d]\n",
//					fd, data_read, (char *)&buf_src_ptr, (char *)&buf_dst_ptr, ip_header->ip6_plen, icmp_payload_sz, ip_header->ip6_nxt);
//
//			KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_ORANGE, "FD [%d] - ICMP6_DST_UNREACH [%d] - CODE [%d] - IP [%s] - [%d]\n",
//					fd, icmp_header->icmp_type, icmp_header->icmp_code, (char *)&buf_dst_ptr, from_sockaddr_sz);

//			EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_DEBUG, (struct sockaddr *)&from_sockaddr, from_sockaddr_sz, 8, 4);
//			EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_DEBUG, &packet_buf, data_read, 8, 4);

		  /* keep it simple, assume the ICMPv6 header is the first one */
//			if (ip_header->ip6_nxt != IPPROTO_ICMPV6)
//			{
//				KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_ORANGE, "FD [%d] - LENGHT [%d] - NEXT [%d]\n",
//						fd, ip_header->ip6_plen, ip_header->ip6_nxt);
//			}

			/* this is from icmp_header->icmp_code */
			//#define ICMP6_DST_UNREACH_NOROUTE	0	/* no route to destination */
			//#define ICMP6_DST_UNREACH_ADMIN	 	1	/* administratively prohibited */
			//#define ICMP6_DST_UNREACH_NOTNEIGHBOR	2	/* not a neighbor(obsolete) */
			//#define ICMP6_DST_UNREACH_BEYONDSCOPE	2	/* beyond scope of source address */
			//#define ICMP6_DST_UNREACH_ADDR		3	/* address unreachable */
			//#define ICMP6_DST_UNREACH_NOPORT	4	/* port unreachable */

			return data_read;
		}
		case ICMP6_ECHO_REPLY:
		{
			/* just continue */
			break;
		}
		case ND_NEIGHBOR_SOLICIT:
		{
//			char bud_ptr[128];
//			int op_status;
//			op_status 		= BrbNetworkSockNtop((char *)&bud_ptr, sizeof(bud_ptr), &from_sockaddr, from_sockaddr.ss_len);
//			KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "FD [%d] - ND_NEIGHBOR_SOLICIT [%d] - IP [%s]\n", fd, icmp_header->icmp_type, (char *)&bud_ptr);
//			EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_DEBUG, &packet_buf, data_read, 8, 4);
//			KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "FD [%d] - SIZE [%d]\n", fd, from_sockaddr_sz);
//			EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_DEBUG, (struct sockaddr *)&from_sockaddr, from_sockaddr_sz, 8, 4);

			return data_read;
		}
		case ND_NEIGHBOR_ADVERT:
		{
//			char bud_ptr[128];
//			int op_status;
//			op_status 		= BrbNetworkSockNtop((char *)&bud_ptr, sizeof(bud_ptr), &from_sockaddr, from_sockaddr.ss_len);
//			KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "FD [%d] - ND_NEIGHBOR_ADVERT [%d] - IP [%s]\n", fd, icmp_header->icmp_type, (char *)&bud_ptr);
//			EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_DEBUG, &packet_buf, data_read, 8, 4);
//			KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "FD [%d] - SIZE [%d]\n", fd, from_sockaddr_sz);
//			EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_DEBUG, (struct sockaddr *)&from_sockaddr, from_sockaddr_sz, 8, 4);

			return data_read;
		}
		/* anything else */
		default:
		{
			char bud_ptr[128];
			int op_status;

//			op_status 		= BrbNetworkSockNtop((char *)&bud_ptr, sizeof(bud_ptr), (const struct sockaddr *)&from_sockaddr, sizeof(struct sockaddr_in6));
//			KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - UNKNOWN TYPE [%d] - IP [%s]\n", fd, icmp_header->icmp_type, (char *)&bud_ptr);

			return data_read;
		}
		}
	}
	else
	{
		CommEvIPHeader *ip_header;

		/* Cast IP header to begin of buffer and grab size from ip_vhl */
		ip_header					= (CommEvIPHeader*)&packet_buf;
		ip_hdr						= ip_header;
		ip_header_sz				= (ip_header->ip_vhl & 0xF) << 2;
		icmp_hopcount				= CommEvICMPGuessHopsFromTTL(ip_header->ip_ttl);

		/* ICMP header begins just after IP header */
		icmp_header					= (CommEvICMPHeader*)&packet_buf[ip_header_sz];
		icmp_header_sz				= sizeof(CommEvICMPHeader) - sizeof(icmp_header->payload) - 4;
		icmp_payload_sz 			= ip_header->total_len - (ip_header_sz + icmp_header_sz);

		/* This actions may happen without echo request, threat as well to manage the route or other things */
		switch (icmp_header->icmp_type)
		{
		/* 0 is echo reply */
		case ICMP_CODE_ECHO_REPLY:
		{
			/* just continue */
			break;
		}
		/* anything else */
		default:
		{
//			char bud_ptr[128];
//			int op_status;
//			op_status 		= BrbNetworkSockNtop((char *)&bud_ptr, sizeof(bud_ptr), (const struct sockaddr *)&from_sockaddr, sizeof(struct sockaddr_in));
//			KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - UNKNOWN TYPE [%d] - IP [%s]\n", fd, icmp_header->icmp_type, (char *)&bud_ptr);

			return data_read;
		}
		}
	}

	/* Safety cap */
	if (icmp_header->icmp_id >= ICMP_QUERY_MAX_SEQID)
		return data_read;

	/* Grab query info from query_info array */
	icmp_queryinfo 					= MemArenaGrabByID(icmp_pending->arena, icmp_header->icmp_id);

	/* Unexpected reply, bail out */
	if (!icmp_queryinfo->flags.in_use)
	{
		MemArenaReleaseByID(icmp_pending->arena, icmp_header->icmp_id);
		return data_read;
	}

//	KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d]-[%d] - ID [%u][%d] - TYPE [%u] - CODE [%u] - SEQ [%u] - ITEMS [%d][%d]\n",
//			fd, (fd == icmp_base->socket_fdv6) ? 1 : 0, icmp_header->icmp_id, icmp_queryinfo->slot_id, icmp_header->icmp_type,
//			icmp_header->icmp_code, icmp_header->icmp_seq,
//			icmp_pending->req_list.size, icmp_pending->reply_list.size);

	/* Grab cb_handler and cb_data back */
	cb_handler					= icmp_queryinfo->cb_handler;
	req_cb_data					= icmp_queryinfo->cb_data;

	/* Clean stack space */
	memset(&icmp_reply, 0, sizeof(ICMPReply));

	/* Assembly reply structure to be exposed to upper layer */
	icmp_reply.sockaddr			= &from_sockaddr;
	icmp_reply.ip_header		= ip_hdr;
	icmp_reply.ip_header_sz		= ip_header_sz;
	icmp_reply.icmp_packet		= icmp_header;
	icmp_reply.icmp_header_sz	= icmp_header_sz;
	icmp_reply.icmp_payload_sz	= icmp_payload_sz;
	icmp_reply.icmp_hopcount	= icmp_hopcount;

	icmp_reply.icmp_type		= icmp_header->icmp_type;
	icmp_reply.icmp_code		= icmp_header->icmp_code;
	icmp_reply.icmp_id			= icmp_header->icmp_id;
	icmp_reply.icmp_seq			= icmp_header->icmp_seq;

	/* Invoke upper layer notification handler */
	if (cb_handler)
		cb_handler(icmp_base, req_cb_data, &icmp_reply);

	/* Free slot and delete from pending list - Make sure we do it just AFTER we invoke CBHandler,
	 * so this slot wont get reused if CBH reschedule another ICMP query immediately */
	CommEvICMPRequestCancelByReqID(icmp_base, icmp_pending, icmp_queryinfo->slot_id);

	return data_read;
}
/**************************************************************************************************************************/
static int CommEvICMPBaseEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvICMPBase *icmp_base			= cb_data;
	EvICMPPending *icmp_pending;
	ICMPQueryInfo *icmp_queryinfo;
	ICMPReply icmp_reply;
	CommEvICMPBaseCBH *cb_handler;
	socklen_t socketaddr_sz			= 0;
	void *req_cb_data				= NULL;
	int wrote_bytes 				= 0;
	int total_wrote_bytes			= 0;

	if (fd == icmp_base->socket_fdv6)
	{
		icmp_pending			= &icmp_base->pending_v6;
		socketaddr_sz 			= sizeof(struct sockaddr_in6);
	}
	else
	{
		icmp_pending			= &icmp_base->pending_v4;
		socketaddr_sz 			= sizeof(struct sockaddr_in);
	}

	while (1)
	{
		icmp_queryinfo 			= (ICMPQueryInfo *)DLinkedListPopHead(&icmp_pending->req_list);

		if (!icmp_queryinfo)
		{
			/* Set read event for socket */
			EvKQBaseSetEvent(icmp_base->ev_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvICMPBaseEventRead, icmp_base);
			return total_wrote_bytes;
		}

		assert(!icmp_queryinfo->flags.waiting_reply);

		/* Unable to write next request, leave */
		if ( (icmp_queryinfo->icmp_packet_sz + wrote_bytes + 1) > can_write_sz)
		{
			/* Enqueue request back for next IO loop */
			DLinkedListAdd(&icmp_pending->req_list, &icmp_queryinfo->node, icmp_queryinfo);

			goto leave;
		}

		/* Write to socket */
		wrote_bytes 				= sendto(fd, (char *)&icmp_queryinfo->icmp_packet, icmp_queryinfo->icmp_packet_sz, 0,
				(struct sockaddr *)&icmp_queryinfo->target_sockaddr, socketaddr_sz);

		/* Failed writing, enqueue it back and bail out */
		if (wrote_bytes <= 0)
		{
			/* Too many TX fails for this request, GIVE UP */
			if (icmp_queryinfo->tx_retry_count > ICMP_BASE_MAX_TX_RETRYCOUNT)
			{
				KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Too many TX_FAILs, drop\n",
						fd, icmp_queryinfo->slot_id);

				/* Grab cb_handler and cb_data back */
				cb_handler	= icmp_queryinfo->cb_handler;
				req_cb_data	= icmp_queryinfo->cb_data;

				/* Clean stack */
				memset(&icmp_reply, 0, sizeof(ICMPReply));

				/* Assembly reply structure to be exposed to upper layer */
				icmp_reply.icmp_type	= ICMP_CODE_TX_FAILED;
				icmp_reply.icmp_code	= icmp_queryinfo->icmp_packet.icmp_code;
				icmp_reply.icmp_id		= icmp_queryinfo->icmp_packet.icmp_id;
				icmp_reply.icmp_seq		= icmp_queryinfo->icmp_packet.icmp_seq;

				if (cb_handler)
					cb_handler(icmp_base, req_cb_data, &icmp_reply);

				/* Cancel this pending, timed out request - Will remove from active list and free slot */
				CommEvICMPRequestCancelByReqID(icmp_base, icmp_pending, icmp_queryinfo->slot_id);
			}
			else
			{
				/* Enqueue request back for next IO loop */
				DLinkedListAdd(&icmp_pending->req_list, &icmp_queryinfo->node, icmp_queryinfo);
				icmp_queryinfo->tx_retry_count++;

//				char buf_ip[128];
//				inet_ntop(icmp_queryinfo->target_sockaddr.ss_family, (fd == icmp_base->socket_fdv6) ? &((struct sockaddr_in6 *)&icmp_queryinfo->target_sockaddr)->sin6_addr :
//						&((struct sockaddr_in *)&icmp_queryinfo->target_sockaddr)->sin_addr, &buf_ip, sizeof(buf_ip));
//				KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - WRITE EVENT - IP [%s]\n", fd, &buf_ip);
//				KQBASE_LOG_PRINTF(icmp_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Write failed with errno [%d] - TX_TRY [%d]\n",
//						fd, icmp_queryinfo->slot_id, errno, icmp_queryinfo->tx_retry_count);

			}

			goto leave;
		}

		total_wrote_bytes += wrote_bytes;

		/* Move it to pending reply list and increment sent request count */
		DLinkedListDelete(&icmp_pending->req_list, &icmp_queryinfo->node);
		DLinkedListAddTail(&icmp_pending->reply_list, &icmp_queryinfo->node, icmp_queryinfo);

		/* Set as waiting for reply - On reply_list */
		icmp_queryinfo->flags.waiting_reply = 1;

		continue;

	}

	leave:

	/* Set read and write event for socket */
	EvKQBaseSetEvent(icmp_base->ev_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvICMPBaseEventRead, icmp_base);
	EvKQBaseSetEvent(icmp_base->ev_base, fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvICMPBaseEventWrite, icmp_base);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvICMPBaseTimeoutTimer(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvICMPBase *icmp_base 		= cb_data;
	DLinkedListNode *node;
	ICMPQueryInfo *icmp_queryinfo;
	CommEvICMPBaseCBH *cb_handler;
	ICMPReply icmp_reply;
	void *req_cb_data;
	int op_status_v4;
	int op_status_v6;

	op_status_v4 				= CommEvICMPBaseTimeoutCheckPending(icmp_base, &icmp_base->pending_v4);
	op_status_v6 				= CommEvICMPBaseTimeoutCheckPending(icmp_base, &icmp_base->pending_v6);
	icmp_base->timer_id			= -1;

	/* Nothing else to check, STOP TIMER */
	if (!op_status_v4 && !op_status_v6)
		icmp_base->min_seen_timeout = ICMP_BASE_TIMEOUT_TIMER;
	/* Reschedule timer event */
	else
		icmp_base->timer_id 	= EvKQBaseTimerAdd(icmp_base->ev_base, COMM_ACTION_ADD_VOLATILE, icmp_base->min_seen_timeout, CommEvICMPBaseTimeoutTimer, icmp_base);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvICMPBaseTimeoutCheckPending(EvICMPBase *icmp_base, EvICMPPending *icmp_pending)
{
	DLinkedListNode *node;
	ICMPQueryInfo *icmp_queryinfo;
	CommEvICMPBaseCBH *cb_handler;
	ICMPReply icmp_reply;
	void *req_cb_data;

	int delta_tx;
	int i;

	/* Nothing else to check, STOP TIMER */
	if (DLINKED_LIST_ISEMPTY(icmp_pending->req_list) && DLINKED_LIST_ISEMPTY(icmp_pending->reply_list))
		return 0;

	/* Walk all pending REQUEST LIST */
	node = icmp_pending->req_list.head;
	while (node)
	{
		/* Grab ICMP INFO and calculate DELTA TX */
		icmp_queryinfo	= node->data;
		node			= node->next;
		delta_tx		= CommEvICMPtvSubMsec(&icmp_queryinfo->transmit_time, &icmp_base->ev_base->stats.cur_invoke_tv);

		/* Request has timed_out */
		if (delta_tx > icmp_queryinfo->timeout_ms)
		{
			/* Grab cb_handler and cb_data back */
			cb_handler	= icmp_queryinfo->cb_handler;
			req_cb_data	= icmp_queryinfo->cb_data;

			/* Clean stack */
			memset(&icmp_reply, 0, sizeof(ICMPReply));

			/* Assembly reply structure to be exposed to upper layer */
			icmp_reply.icmp_type	= ICMP_CODE_TIMEOUT;
			icmp_reply.icmp_code	= icmp_queryinfo->icmp_packet.icmp_code;
			icmp_reply.icmp_id		= icmp_queryinfo->icmp_packet.icmp_id;
			icmp_reply.icmp_seq		= icmp_queryinfo->icmp_packet.icmp_seq;

			/* Invoke TIMEOUT HANDLER */
			if (cb_handler)
				cb_handler(icmp_base, req_cb_data, &icmp_reply);

			/* Cancel this pending, timed out request - Will remove from active list and free slot */
			CommEvICMPRequestCancelByReqID(icmp_base, icmp_pending, icmp_queryinfo->slot_id);
		}

		continue;
	}

	/* Walk all pending REPLY LIST */
	node = icmp_pending->reply_list.head;
	while (node)
	{
		/* Grab ICMP INFO and calculate DELTA TX */
		icmp_queryinfo	= node->data;
		node			= node->next;
		delta_tx		= CommEvICMPtvSubMsec(&icmp_queryinfo->transmit_time, &icmp_base->ev_base->stats.cur_invoke_tv);

		/* Request has timed_out */
		if (delta_tx > icmp_queryinfo->timeout_ms)
		{
			/* Grab cb_handler and cb_data back */
			cb_handler	= icmp_queryinfo->cb_handler;
			req_cb_data	= icmp_queryinfo->cb_data;

			/* Clean stack */
			memset(&icmp_reply, 0, sizeof(ICMPReply));

			/* Assembly reply structure to be exposed to upper layer */
			icmp_reply.icmp_type	= ICMP_CODE_TIMEOUT;
			icmp_reply.icmp_code	= icmp_queryinfo->icmp_packet.icmp_code;
			icmp_reply.icmp_id		= icmp_queryinfo->icmp_packet.icmp_id;
			icmp_reply.icmp_seq		= icmp_queryinfo->icmp_packet.icmp_seq;

			/* Invoke TIMEOUT HANDLER */
			if (cb_handler)
				cb_handler(icmp_base, req_cb_data, &icmp_reply);

			/* Cancel this pending, timed out request - Will remove from active list and free slot */
			CommEvICMPRequestCancelByReqID(icmp_base, icmp_pending, icmp_queryinfo->slot_id);
		}

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
