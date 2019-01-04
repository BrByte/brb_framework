/*
 * comm_icmp_pinger.c
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

#include "../../include/libbrb_core.h"

#include <netinet/icmp6.h>

static int EvICMPPeriodicPingerSendRequest(EvICMPPeriodicPinger *icmp_pinger);
static void EvICMPPeriodicPingerEventDispatchInternal(EvICMPPeriodicPinger *icmp_pinger, void *icmp_data, int ev_type);
static int EvICMPPeriodicPingerCheckIfNeedDNSLookup(EvICMPPeriodicPinger *icmp_pinger);

static EvBaseKQCBH EvICMPPeriodicPingerResolverTimer;
static CommEvDNSResolverCBH EvICMPPeriodicPingerResolverCB;
static CommEvICMPBaseCBH EvICMPPeriodicPingerCB;
static EvBaseKQCBH EvICMPPeriodicPingerTimer;

/**************************************************************************************************************************/
EvICMPPeriodicPinger *CommEvICMPPeriodicPingerNew(EvICMPBase *icmp_base, EvICMPPeriodicPingerConf *pinger_conf)
{
	EvICMPPeriodicPinger *icmp_pinger;
	struct timeval timeval_now;

	/* Initialize local time and target_addr info */
	gettimeofday((struct timeval*)&timeval_now, NULL);

	icmp_pinger									= calloc(1, sizeof(EvICMPPeriodicPinger));
	icmp_pinger->icmp_base						= icmp_base;
	icmp_pinger->log_base						= pinger_conf->log_base;
	icmp_pinger->resolv_base					= pinger_conf->resolv_base;

	/* Copy interval and PAYLOAD data */
	icmp_pinger->cfg.reset_count				= ((pinger_conf->reset_count > 0) ? pinger_conf->reset_count : 8092);
	icmp_pinger->cfg.interval					= ((pinger_conf->interval_ms > 0) ? pinger_conf->interval_ms : 1000);
	icmp_pinger->cfg.timeout					= ((pinger_conf->timeout_ms > 0) ? pinger_conf->timeout_ms : 1000);
	icmp_pinger->cfg.payload_sz					= ((pinger_conf->payload_sz > 0) ? pinger_conf->payload_sz : 128);

	/* Initialize values */
	icmp_pinger->last.seq_reply_id				= -1;
	icmp_pinger->last.icmp_slot_id				= -1;
	icmp_pinger->timer_id						= -1;
	icmp_pinger->dnsreq_id						= -1;
	icmp_pinger->stats.latency_ms				= -1;
	icmp_pinger->last.seq_req_id				= 0;
	icmp_pinger->stats.packet_loss_pct 			= 100;

	/* Set flags */
	icmp_pinger->flags.dns_resolv_on_fail 		= (pinger_conf->dns_resolv_on_fail > 0) ? 1 : 0;

	/* Use an external provided UNIQUE ID, or do our best to generate one randomly */
	if (pinger_conf->unique_id > 0)
		icmp_pinger->cfg.unique_id 				= pinger_conf->unique_id;
	else
	{
		icmp_pinger->cfg.unique_id				= arc4random();
		icmp_pinger->flags.random_unique_id 	= 1;
	}

	/* Save a copy of HOSTNAME */
	if (pinger_conf->hostname_str)
		strncpy((char*)&icmp_pinger->cfg.hostname_str, pinger_conf->hostname_str, sizeof(icmp_pinger->cfg.hostname_str));
	/* Save target ADDR */
	else if (pinger_conf->sockaddr)
		memcpy(&icmp_pinger->cfg.sockaddr, pinger_conf->sockaddr, sizeof(icmp_pinger->cfg.sockaddr));
	else if (pinger_conf->target_ip_str)
	{
		BrbIsValidIpToSockAddr(pinger_conf->target_ip_str, &icmp_pinger->cfg.sockaddr);

		if (icmp_pinger->cfg.sockaddr.ss_family == AF_UNSPEC)
		{
			KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "UID [%u] - Failed on IP identify [%s]\n",
					icmp_pinger->cfg.unique_id, pinger_conf->target_ip_str);

			CommEvICMPPeriodicPingerDestroy(icmp_pinger);

			return NULL;
		}

		/* Convert back IP Address */
		BrbNetworkSockNtop((char *)&icmp_pinger->cfg.ip_addr_str, sizeof(icmp_pinger->cfg.ip_addr_str), (struct sockaddr *)&icmp_pinger->cfg.sockaddr, 0);
	}

	/* Check now if user sent us a HOSTNAME and we have no resolver base defined - Will set flags.dns_need_lookup */
	if (pinger_conf->hostname_str && EvICMPPeriodicPingerCheckIfNeedDNSLookup(icmp_pinger) && (!pinger_conf->resolv_base))
	{
		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "UID [%u] - Failed on DNS lookup\n", icmp_pinger->cfg.unique_id);

		CommEvICMPPeriodicPingerDestroy(icmp_pinger);

		return NULL;
	}

	/* Check if host needs to be resolved - If RESOLV fails, invoke COMM_CLIENT_STATE_CONNECT_FAILED_DNS */
	if (icmp_pinger->flags.dns_need_lookup)
	{
		/* Start ASYNC DNS lookup */
		icmp_pinger->dnsreq_id 			= CommEvDNSGetHostByName(icmp_pinger->resolv_base, pinger_conf->hostname_str, EvICMPPeriodicPingerResolverCB, icmp_pinger);

		if (icmp_pinger->dnsreq_id	> -1)
		{
			icmp_pinger->state			= ICMP_PINGER_STATE_RESOLVING_DNS;

			return icmp_pinger;
		}
		else
		{
			KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "UID [%u] - Failed on DNS lookup\n", icmp_pinger->cfg.unique_id);

			CommEvICMPPeriodicPingerDestroy(icmp_pinger);

			return NULL;
		}
	}
	/* By sequence order, HostName no need to resolve, plain old IP address, or IP address in string */
	else
	{
		/* Schedule timer to begin dispatching ICMP ECHO requests */
		icmp_pinger->timer_id 			= EvKQBaseTimerAdd(icmp_pinger->icmp_base->ev_base, COMM_ACTION_ADD_PERSIST, icmp_pinger->cfg.interval, EvICMPPeriodicPingerTimer, icmp_pinger);
	}

	/* Send ICMP request */
	EvICMPPeriodicPingerSendRequest(icmp_pinger);

	return icmp_pinger;
}
/**************************************************************************************************************************/
void CommEvICMPPeriodicPingerDestroy(EvICMPPeriodicPinger *icmp_pinger)
{
	EvICMPPending *icmp_pending;

	/* Sanity check */
	if (!icmp_pinger)
		return;

	KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "PERIODIC_PINGER ID [%d] - TIMER_ID [%d] - ICMP_SLOT [%d] - Destroy\n",
			icmp_pinger->cfg.unique_id, icmp_pinger->timer_id, icmp_pinger->last.icmp_slot_id);

	if (icmp_pinger->cfg.sockaddr.ss_family == AF_INET6)
		icmp_pending			= &icmp_pinger->icmp_base->pending_v6;
	else
		icmp_pending			= &icmp_pinger->icmp_base->pending_v4;

	/* Cancel last pending request */
	if (icmp_pinger->last.icmp_slot_id > -1)
		CommEvICMPRequestCancelByReqID(icmp_pinger->icmp_base, icmp_pending, icmp_pinger->last.icmp_slot_id);

	/* Delete control timer */
	if (icmp_pinger->timer_id > -1)
		EvKQBaseTimerCtl(icmp_pinger->icmp_base->ev_base, icmp_pinger->timer_id, COMM_ACTION_DELETE);

	if (icmp_pinger->timers.resolv_dns_id > -1)
		EvKQBaseTimerCtl(icmp_pinger->icmp_base->ev_base, icmp_pinger->timers.resolv_dns_id, COMM_ACTION_DELETE);

	/* Cancel pending DNS request */
	if (icmp_pinger->dnsreq_id	> -1)
	{
		/* Cancel pending DNS request and set it to -1 */
		CommEvDNSCancelPendingRequest(icmp_pinger->resolv_base, icmp_pinger->dnsreq_id);

		icmp_pinger->dnsreq_id = -1;
	}

	/* Cancel all ICMP requests related to this ICMP PINGER */
	CommEvICMPRequestCancelByOwnerID(icmp_pinger->icmp_base, icmp_pending, icmp_pinger->cfg.unique_id);

	/* Reset IDs */
	icmp_pinger->last.icmp_slot_id	= -1;
	icmp_pinger->timer_id			= -1;

	/* Free willy */
	free(icmp_pinger);
	return;
}
/**************************************************************************************************************************/
void CommEvICMPPeriodicPingerStatsReset(EvICMPPeriodicPinger *icmp_pinger)
{
	/* Sanity check */
	if (!icmp_pinger)
		return;

	/* Clean up statistics */
	memset(&icmp_pinger->stats, 0, sizeof(icmp_pinger->stats));

	return;
}
/**************************************************************************************************************************/
int CommEvICMPPeriodicPingerEventIsSet(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type)
{
	/* Sanity check */
	if (!icmp_pinger)
		return 0;

	/* Sanity check */
	if (ev_type >= ICMP_EVENT_LASTITEM)
		return 0;

	if (icmp_pinger->events[ev_type].cb_handler_ptr && icmp_pinger->events[ev_type].flags.enabled)
		return 1;

	return 0;
}
/**************************************************************************************************************************/
void CommEvICMPPeriodicPingerEventSet(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type, CommEvICMPBaseCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (!icmp_pinger)
		return;

	/* Sanity check */
	if (ev_type >= ICMP_EVENT_LASTITEM)
		return;

	/* Set event */
	icmp_pinger->events[ev_type].cb_handler_ptr	= cb_handler;
	icmp_pinger->events[ev_type].cb_data_ptr	= cb_data;

	/* Mark enabled */
	icmp_pinger->events[ev_type].flags.enabled	= 1;

	return;
}
/**************************************************************************************************************************/
void CommEvICMPPeriodicPingerEventCancel(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type)
{
	/* Sanity check */
	if (!icmp_pinger)
		return;

	/* Sanity check */
	if (ev_type >= ICMP_EVENT_LASTITEM)
		return;

	/* Set event */
	icmp_pinger->events[ev_type].cb_handler_ptr	= NULL;
	icmp_pinger->events[ev_type].cb_data_ptr	= NULL;

	/* Mark disabled */
	icmp_pinger->events[ev_type].flags.enabled	= 0;


	return;
}
/**************************************************************************************************************************/
void CommEvICMPPeriodicPingerEventCancelAll(EvICMPPeriodicPinger *icmp_pinger)
{
	int i;

	/* Sanity check */
	if (!icmp_pinger)
		return;

	/* Cancel all possible events */
	for (i = 0; i < ICMP_EVENT_LASTITEM; i++)
		CommEvICMPPeriodicPingerEventCancel(icmp_pinger, i);

	return;
}
/**************************************************************************************************************************/
int CommEvICMPPeriodicPingerJSONDump(EvICMPPeriodicPinger *icmp_pinger, MemBuffer *json_reply_mb)
{
	if (icmp_pinger)
	{
		MEMBUFFER_JSON_ADD_INT(json_reply_mb, "state", icmp_pinger->state);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_FLOAT(json_reply_mb, "latency_ms", icmp_pinger->stats.latency_ms);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_FLOAT(json_reply_mb, "packet_loss_pct", icmp_pinger->stats.packet_loss_pct);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "request_sent", icmp_pinger->stats.request_sent);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "reply_recv", icmp_pinger->stats.reply_recv);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_INT(json_reply_mb, "hop_count", icmp_pinger->stats.hop_count);
	}
	else
	{
		MEMBUFFER_JSON_ADD_INT(json_reply_mb, "state", 0);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_INT(json_reply_mb, "latency_ms", 0);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_INT(json_reply_mb, "packet_loss_pct", 0);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "request_sent", 0);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_LONG(json_reply_mb, "reply_recv", 0);
		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
		MEMBUFFER_JSON_ADD_INT(json_reply_mb, "hop_count", 0);
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvICMPPeriodicPingerSendRequest(EvICMPPeriodicPinger *icmp_pinger)
{
	struct timeval timeval_now;
	EvICMPBase *icmp_base = icmp_pinger->icmp_base;

	/* If there is a reset count, reset statistics */
	if (icmp_pinger->cfg.reset_count > 0 && (icmp_pinger->stats.request_sent >= icmp_pinger->cfg.reset_count))
		CommEvICMPPeriodicPingerStatsReset(icmp_pinger);

	/* Reset sequence ID */
	if (icmp_pinger->last.seq_req_id > 8092)
		icmp_pinger->last.seq_req_id = 0;

	/* Send ICMP request */
	icmp_pinger->last.icmp_slot_id 		= CommEvICMPEchoSendRequestBySockAddr(icmp_pinger->icmp_base, &icmp_pinger->cfg.sockaddr, 0, icmp_pinger->last.seq_req_id,
			icmp_pinger->cfg.payload_sz, icmp_pinger->cfg.timeout, EvICMPPeriodicPingerCB, icmp_pinger, icmp_pinger->cfg.unique_id);

	/* Failed writing ICMP request, schedule TIMER */
	if (icmp_pinger->last.icmp_slot_id < 0)
	{
		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed writing ICMP_ECHO to IP [%s] with STATUS [%d] - ERRNO [%d]\n",
				icmp_pinger->cfg.ip_addr_str, icmp_pinger->last.icmp_slot_id, errno);
	}
	/* Ping wrote OK */
	else
	{
		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Wrote ICMP_ECHO to IP [%s] with ICMP_SLOT [%d] - SEQ_ID [%d] - TIMER_ID [%d]\n",
				icmp_pinger->cfg.ip_addr_str, icmp_pinger->last.icmp_slot_id, icmp_pinger->last.seq_req_id, icmp_pinger->timer_id);

		/* Initialize local time */
		gettimeofday((struct timeval*)&timeval_now, NULL);

		/* Save last sent TIMEVAL and increment REQUEST_SENT count */
		memcpy(&icmp_pinger->stats.lastreq_tv, &timeval_now, sizeof(struct timeval));
		icmp_pinger->stats.request_sent++;
		icmp_pinger->last.seq_req_id++;
	}

	return 1;
}
/**************************************************************************************************************************/
static void EvICMPPeriodicPingerEventDispatchInternal(EvICMPPeriodicPinger *icmp_pinger, void *icmp_data, int ev_type)
{
	CommEvICMPBaseCBH *cb_handler		= NULL;
	void *cb_handler_data				= NULL;

	/* Dispatch CALLBACK function */
	if (icmp_pinger->events[ev_type].cb_handler_ptr)
		icmp_pinger->events[ev_type].cb_handler_ptr(icmp_pinger, icmp_pinger->events[ev_type].cb_data_ptr, icmp_data);

	return;
}
/**************************************************************************************************************************/
static void EvICMPPeriodicPingerCB(void *icmp_base_ptr, void *req_cb_data_ptr, void *icmp_reply_ptr)
{
	struct timeval timeval_now;

	EvICMPBase *icmp_base				= icmp_base_ptr;
	ICMPReply *icmp_reply				= icmp_reply_ptr;
	EvICMPPeriodicPinger *icmp_pinger	= req_cb_data_ptr;

	/* Reset last request ID */
	icmp_pinger->last.icmp_slot_id = -1;

	/* Initialize local time and target_addr info */
	gettimeofday((struct timeval*)&timeval_now, NULL);

	/* No reply packet, this is a timeout */
	if (!icmp_reply->icmp_packet)
	{
		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "ICMP Timed out - Seq ID [%d]\n", icmp_reply->icmp_seq);
		goto timed_out;
	}
	/* Unexpected sequence ID */
	else if (icmp_reply->icmp_packet->icmp_seq != (icmp_pinger->last.seq_req_id - 1))
	{
//		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "ICMP Unexpected ID [%u] - Seq ID [%u] - TYPE [%d]\n",
//				icmp_reply->icmp_packet->icmp_seq, icmp_pinger->last.seq_req_id, icmp_reply->icmp_type);
		goto unexpected_packet;
	}
	/* Unexpected IpAddress */
	else if (!COMM_SOCK_ADDR_EQ_ADDR(icmp_reply->sockaddr, &icmp_pinger->cfg.sockaddr))
	{
//		char bud_ptr[128];
//		struct sockaddr_storage *sockaddr;
//		int op_status;
//		/* Convert back IP Address */
//		sockaddr		= ((struct sockaddr *)icmp_reply->sockaddr);
//		EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_WARNING, sockaddr, COMM_SOCK_ADDR_LEN(sockaddr), 8, 4);
//		op_status 		= BrbNetworkSockNtop((char *)&bud_ptr, sizeof(bud_ptr), sockaddr, sockaddr->ss_len);
//		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "OP [%d] - ICMP Unexpected IP [%s] - LEN [%d] - FAMILY [%d] - SIN [%ul]\n",
//				op_status, (char *)&bud_ptr, sockaddr->ss_len, sockaddr->ss_family, ((struct sockaddr_in6 *)sockaddr)->sin6_addr.s6_addr);
//
//		/* Convert back IP Address */
//		sockaddr		= (struct sockaddr *)&icmp_pinger->cfg.sockaddr;
//		EvKQBaseLoggerHexDump(icmp_base->log_base, LOGTYPE_WARNING, sockaddr, COMM_SOCK_ADDR_LEN(sockaddr), 8, 4);
//		op_status 		= BrbNetworkSockNtop((char *)&bud_ptr, sizeof(bud_ptr), sockaddr, sockaddr->ss_len);
//		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "OP [%d] - ICMP Unexpected IP [%s] - LEN [%d] - FAMILY [%d] - SIN [%ul]\n",
//				op_status, (char *)&bud_ptr, sockaddr->ss_len, sockaddr->ss_family, ((struct sockaddr_in6 *)sockaddr)->sin6_addr.s6_addr);

		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "ICMP Unexpected SRC_IP [%s]\n", icmp_pinger->cfg.ip_addr_str);

		goto unexpected_packet;
	}
	/* This packet is not an ECHO reply, ignore it */
	else if ((icmp_reply->sockaddr->ss_family == AF_INET) && (icmp_reply->icmp_type != ICMP_CODE_ECHO_REPLY))
	{
		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "ICMP NON_ECHO reply - Seq ID [%d] - ICMP_TYPE [%d]\n",
				icmp_reply->icmp_seq, icmp_reply->icmp_type);

		goto unexpected_packet;
	}
	/* This packet is not an ECHO reply, ignore it */
	else if ((icmp_reply->sockaddr->ss_family == AF_INET6) && (icmp_reply->icmp_type != ICMP6_ECHO_REPLY))
	{
		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "ICMP NON_ECHO reply - Seq ID [%d] - ICMP_TYPE [%d]\n",
				icmp_reply->icmp_seq, icmp_reply->icmp_type);

		goto unexpected_packet;
	}

	/* Save last reply TIMEVAL */
	memcpy(&icmp_pinger->stats.lastreply_tv, &timeval_now, sizeof(struct timeval));

	/* Increment reply count and calculate statistics */
	icmp_pinger->stats.reply_recv++;
	icmp_pinger->stats.latency_ms		= CommEvICMPtvSubMsec((struct timeval*)&icmp_reply->icmp_packet->payload, &timeval_now);
	icmp_pinger->stats.hop_count		= icmp_reply->icmp_hopcount;
	icmp_pinger->stats.packet_loss_pct	= (100.00 - ((icmp_pinger->stats.reply_recv * 100.00) / (icmp_pinger->stats.request_sent ? icmp_pinger->stats.request_sent : 1.00)));
	icmp_pinger->last.seq_reply_id		= icmp_reply->icmp_packet->icmp_seq;

	/* Dispatch internal REPLY event */
	EvICMPPeriodicPingerEventDispatchInternal(icmp_pinger, icmp_reply, ICMP_EVENT_REPLY);

	return;

	/* Tag to ICMP timeout, invalid sequence ID or not ECHO REPLY */
	timed_out:

	/* Dispatch internal TIMEOUT event with NULL ICMP_REPLY */
	EvICMPPeriodicPingerEventDispatchInternal(icmp_pinger, NULL, ICMP_EVENT_TIMEOUT);

	/* Tag to unexpected SEQ_ID (probably delayed) replies, avoiding internal TIMEOUT event above */
	unexpected_packet:

	/* Calculate packet loss and reset latency */
	icmp_pinger->stats.packet_loss_pct	= (100.00 - ((icmp_pinger->stats.reply_recv * 100.00) /  (icmp_pinger->stats.request_sent ? icmp_pinger->stats.request_sent : 1.00)));
	icmp_pinger->stats.latency_ms		= -1;

	return;
}
/**************************************************************************************************************************/
static int EvICMPPeriodicPingerTimer(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	EvICMPPeriodicPinger *icmp_pinger	= cb_data;
	EvICMPBase *icmp_base				= icmp_pinger->icmp_base;

	KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Periodic TIMER_ID [%d] event\n", timer_id);

	/* Send ICMP request */
	EvICMPPeriodicPingerSendRequest(icmp_pinger);

	return 0;
}
/**************************************************************************************************************************/
static int EvICMPPeriodicPingerCheckIfNeedDNSLookup(EvICMPPeriodicPinger *icmp_pinger)
{
	int af_family;

	af_family 									= BrbIsValidIp(icmp_pinger->cfg.hostname_str);

	/* Set flags */
	icmp_pinger->flags.dns_need_lookup 			= (af_family != AF_UNSPEC) ? 0 : 1;

	/* if have HostName, we set this in priority order, and its already a IP in string format */
	if (af_family != AF_UNSPEC)
	{
		icmp_pinger->cfg.sockaddr.ss_family		= af_family;

		if (icmp_pinger->cfg.sockaddr.ss_family == AF_INET6)
			inet_pton(icmp_pinger->cfg.sockaddr.ss_family, (char*)&icmp_pinger->cfg.hostname_str, &((struct sockaddr_in6 *)&icmp_pinger->cfg.sockaddr)->sin6_addr);
		else if (icmp_pinger->cfg.sockaddr.ss_family == AF_INET)
			inet_pton(icmp_pinger->cfg.sockaddr.ss_family, (char*)&icmp_pinger->cfg.hostname_str, &((struct sockaddr_in *)&icmp_pinger->cfg.sockaddr)->sin_addr);
	}

	return icmp_pinger->flags.dns_need_lookup;
}
/**************************************************************************************************************************/
static void EvICMPPeriodicPingerResolverCB(void *ev_dns_ptr, void *cb_data, void *a_reply_ptr, int code)
{
	EvICMPPeriodicPinger *icmp_pinger 	= cb_data;
	DNSAReply *a_reply					= a_reply_ptr;
	struct sockaddr_in *sockaddr_cur  	= (struct sockaddr_in *)&icmp_pinger->cfg.sockaddr;
	int lower_seen_ttl					= 0;
	int i;

	/* TODO resolver only reply v4 for now */
	/* Resolve succeeded OK, begin ASYNC connection procedure */
	if (a_reply->ip_count > 0)
	{
		for (i = 0; i < a_reply->ip_count; i++)
		{
			/* First iteration, initialize values */
			if (0 == i)
				lower_seen_ttl = a_reply->ip_arr[i].ttl;
			/* There is a smaller TTL, keep it */
			else if (a_reply->ip_arr[i].ttl < lower_seen_ttl)
				lower_seen_ttl = a_reply->ip_arr[i].ttl;
		}

		/* Save DNS reply */
		memcpy(&icmp_pinger->dns.a_reply, a_reply, sizeof(DNSAReply));

		/* Set expire based on lower TTL seen on array */
		icmp_pinger->dns.expire_ts 	= icmp_pinger->resolv_base->ev_base->stats.cur_invoke_ts_sec + lower_seen_ttl;
		icmp_pinger->dns.cur_idx	= 0;

		/* Clean dst_addr structure for later use */
		memset(&icmp_pinger->cfg.sockaddr, 0, sizeof(icmp_pinger->cfg.sockaddr));

		/* Balance on IPs */
		if (icmp_pinger->flags.dns_balance_ips)
		{
			/* Copy currently pointed address */
			memcpy(&sockaddr_cur->sin_addr, &icmp_pinger->dns.a_reply.ip_arr[icmp_pinger->dns.cur_idx].addr, sizeof(struct in_addr));

			/* Adjust INDEX to rotate ROUND ROBIN */
			if (icmp_pinger->dns.cur_idx == (icmp_pinger->dns.a_reply.ip_count - 1) )
				icmp_pinger->dns.cur_idx = 0;
			else
				icmp_pinger->dns.cur_idx++;
		}
		else
		{
			memcpy(&sockaddr_cur->sin_addr, &icmp_pinger->dns.a_reply.ip_arr[0].addr, sizeof(struct in_addr));
		}

		/* TODO Copy IP Address using return as parameters */
		sockaddr_cur->sin_family	= AF_INET;
//		sockaddr_cur->sin_len		= sizeof(struct sockaddr_in);
		inet_ntop(sockaddr_cur->sin_family, &sockaddr_cur->sin_addr, (char *)&icmp_pinger->cfg.ip_addr_str, sizeof(icmp_pinger->cfg.ip_addr_str));

		/* Dispatch internal REPLY event */
		EvICMPPeriodicPingerEventDispatchInternal(icmp_pinger, a_reply, ICMP_EVENT_DNS_RESOLV);

		/* Schedule timer to begin dispatching ICMP ECHO requests */
		if (icmp_pinger->timer_id < 0)
		{
			icmp_pinger->timer_id 	= EvKQBaseTimerAdd(icmp_pinger->icmp_base->ev_base, COMM_ACTION_ADD_PERSIST, icmp_pinger->cfg.interval, EvICMPPeriodicPingerTimer, icmp_pinger);
		}

	}
	/* Resolve failed, notify upper layers */
	else
	{
		KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FAILED [%d]\n", a_reply->ip_count);

		/* Set flags */
		icmp_pinger->state			= ICMP_PINGER_STATE_FAILED_DNS;
		icmp_pinger->dns.expire_ts 	= -1;

		/* Dispatch internal event */
		EvICMPPeriodicPingerEventDispatchInternal(icmp_pinger, a_reply, ICMP_EVENT_DNS_FAILED);

		/* Upper layers want a full DESTROY if CONNECTION FAILS */
		if (icmp_pinger->flags.destroy_after_dns_fail)
		{
			CommEvICMPPeriodicPingerDestroy(icmp_pinger);

			return;
		}
		/* Upper layers want a reconnect retry if CONNECTION FAILS */
		else if (icmp_pinger->flags.dns_resolv_on_fail)
		{
			/* Set state */
			icmp_pinger->state					= ICMP_PINGER_STATE_RESOLVING_DNS;

			/* Schedule RECONNECT timer */
			icmp_pinger->timers.resolv_dns_id 	=	EvKQBaseTimerAdd(icmp_pinger->icmp_base->ev_base, COMM_ACTION_ADD_VOLATILE,
					((icmp_pinger->cfg.timer_resolv_dns_ms > 0) ? icmp_pinger->cfg.timer_resolv_dns_ms : ICMP_BASE_TIMEOUT_TIMER),
					EvICMPPeriodicPingerResolverTimer, icmp_pinger);

			KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "UID [%u] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
					icmp_pinger->cfg.unique_id, icmp_pinger->timers.resolv_dns_id);

		}
	}

	/* Cancel DNS_REQ_ID */
	icmp_pinger->dnsreq_id 			= -1;

	return;
}
/**************************************************************************************************************************/
static int EvICMPPeriodicPingerResolverTimer(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	EvICMPPeriodicPinger *icmp_pinger 	= cb_data;

	KQBASE_LOG_PRINTF(icmp_pinger->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "UID [%u] - Reconnect timer at ID [%d]\n", icmp_pinger->cfg.unique_id, timer_id);

	/* Start ASYNC DNS lookup */
	icmp_pinger->timers.resolv_dns_id 	= -1;
	icmp_pinger->dnsreq_id 				= CommEvDNSGetHostByName(icmp_pinger->resolv_base, (char *)&icmp_pinger->cfg.hostname_str, EvICMPPeriodicPingerResolverCB, icmp_pinger);

	if (icmp_pinger->dnsreq_id	> -1)
	{
		icmp_pinger->state				= ICMP_PINGER_STATE_RESOLVING_DNS;
	}
	else
	{
		/* Dispatch internal event */
		EvICMPPeriodicPingerEventDispatchInternal(icmp_pinger, NULL, ICMP_EVENT_DNS_FAILED);

		/* Upper layers want a full DESTROY if CONNECTION FAILS */
		if (icmp_pinger->flags.destroy_after_dns_fail)
			CommEvICMPPeriodicPingerDestroy(icmp_pinger);
	}

	return 1;
}
/**************************************************************************************************************************/


