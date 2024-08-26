/*
 * libbrb_comm_utils.h
 *
 *  Created on: 2016-10-05
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2016 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#ifndef LIBBRB_COMM_UTILS_H_
#define LIBBRB_COMM_UTILS_H_

/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
#define RESOLVER_DNS_MAX_SEQID 65535
#define RESOLVER_DNS_MAX_REPLY_COUNT 32
#define RESOLVER_DNS_QUERY_SZ 512

/* rfc1035 - DNS */
#define RFC1035_MAXHOSTNAMESZ 256
#define RFC1035_MAXLABELSZ 63
#define rfc1035_unpack_error 15
#define RFC1035_UNPACK_DEBUG  (void)0

#define RFC1035_TYPE_A      1    // IPv4 Address
#define RFC1035_TYPE_NS     2    // Authoritative Name Server
#define RFC1035_TYPE_MD     3    // Mail Destination (Obsolete)
#define RFC1035_TYPE_MF     4    // Mail Forwarder (Obsolete)
#define RFC1035_TYPE_CNAME  5    // Canonical Name
#define RFC1035_TYPE_SOA    6    // Start of Authority
#define RFC1035_TYPE_MB     7    // Mailbox (Experimental)
#define RFC1035_TYPE_MG     8    // Mail Group (Experimental)
#define RFC1035_TYPE_MR     9    // Mail Rename (Experimental)
#define RFC1035_TYPE_NULL   10   // Null (Obsolete)
#define RFC1035_TYPE_WKS    11   // Well-Known Services (Obsolete)
#define RFC1035_TYPE_PTR    12   // Pointer
#define RFC1035_TYPE_HINFO  13   // Host Information
#define RFC1035_TYPE_MINFO  14   // Mailbox Information (Experimental)
#define RFC1035_TYPE_MX     15   // Mail Exchanger
#define RFC1035_TYPE_TXT    16   // Text
#define RFC1035_TYPE_AAAA   28   // IPv6 Address
#define RFC1035_TYPE_SRV    33   // Service Locator

#define RFC1035_CLASS_IN 1
/*******************************************************/
typedef void CommEvDNSResolverCBH(void *, void *, void *, int);
/*******************************************************/
typedef enum
{
	DNS_REPLY_TIMEDOUT,
	DNS_REPLY_SUCCESS,
	DNS_REPLY_FAILURE,
	DNS_REPLY_LASTITEM
} DNSReplyCodes;
/*******************************************************/
typedef struct _DNSPendingQuery
{
	DLinkedListNode node;
	int req_count;
	int fail_count;
	int slot_id;

	struct timeval transmit_tv;
	struct timeval retransmit_tv;

	char rfc1035_request[RESOLVER_DNS_QUERY_SZ];
	unsigned long rfc1035_request_sz;

	struct
	{
		CommEvDNSResolverCBH *cb_handler;
		void *cb_data;
	} events;

	struct
	{
		unsigned int in_use:1;
		unsigned int re_xmit:1;
		unsigned int waiting_reply:1;
		unsigned int canceled_req:1;
	} flags;
} DNSPendingQuery;
/*******************************************************/
typedef struct _DNSEntry
{
	int ttl;
	unsigned short type;
	int ctt;

	union
	{
		struct in_addr addr;
		struct in6_addr addr6;
	};

} DNSEntry;
/*******************************************************/
typedef struct _DNSAReply
{
	int ip_count;
	DNSEntry ip_arr[RESOLVER_DNS_MAX_REPLY_COUNT];

} DNSAReply;
/*******************************************************/
typedef struct _EvDNSResolverConf
{
	char *dns_ip_str;
	unsigned short dns_port;
	int lookup_timeout_ms;
	int retry_timeout_ms;
	int retry_count;
} EvDNSResolverConf;
/*******************************************************/
typedef struct _EvDNSResolverBase
{
	struct _EvKQBaseLogBase *log_base;
	struct _EvKQBase *ev_base;
	struct sockaddr_in dns_serv_addr;
	int socket_fd;
	int retry_count;
	int timer_id;

	struct
	{
		int retry;
		int lookup;
	} timeout_ms;

	struct
	{
		MemArena *arena;
		SlotQueue slots;
		DLinkedList req_list;
		DLinkedList reply_list;
	} pending_req;

	struct
	{
		long request_sent_sched;
		long request_sent_write;
		long request_failed;
		long request_timeout;

		long reply_received_total;
		long reply_received_valid;

		long total_bytes_sent;
		long total_bytes_received;
	} stats;

} EvDNSResolverBase;
/*******************************************************/
typedef struct _EvDNSReplyHandler
{
	EvDNSResolverBase *resolv_base;
	DNSAReply a_reply;
	long expire_ts;
	int cur_idx;
	int req_id;

	int ipv6_query;

} EvDNSReplyHandler;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
#define ICMP_QUERY_MAX_SEQID		65535
#define ICMP_BASE_TIMEOUT_TIMER		1000
#define ICMP_BASE_MAX_TX_RETRYCOUNT	5

#define MAX_PKT_SZ					(COMM_ICMP_MAX_PAYLOAD + sizeof(struct timeval) + sizeof (char) + sizeof(CommEvICMPHeader) + 1)
/*******************************************************/
typedef void CommEvICMPBaseCBH(void*, void*, void*);
/*******************************************************/
typedef enum
{
	ICMP_CODE_TX_FAILED = -2,			/* -2 */
	ICMP_CODE_TIMEOUT 	= -1,			/* -1 */
	ICMP_CODE_ECHO_REPLY,				/* 00 */
	ICMP_CODE_UNASSIGNED_00,			/* 01 */
	ICMP_CODE_UNASSIGNED_01,			/* 02 */
	ICMP_CODE_DESTINATION_UNREACHABLE,	/* 03 */
	ICMP_CODE_SOURCE_QUENCH,			/* 04 */
	ICMP_CODE_REDIRECT,					/* 05 */
	ICMP_CODE_ALTERNATE_HOST,			/* 06 */
	ICMP_CODE_UNASSIGNED_02,			/* 07 */
	ICMP_CODE_ECHO_REQUEST,				/* 08 */
	ICMP_CODE_ROUTER_ADVERTISE,			/* 09 */
	ICMP_CODE_ROUTER_SELECTION,			/* 10 */
	ICMP_CODE_TTL_EXCEEDED,				/* 11 */
	ICMP_CODE_PARAMETER_PROBLEM,		/* 12 */
	ICMP_CODE_TIMESTAMP,				/* 13 */
	ICMP_CODE_TIMESTAMP_REPLY,			/* 14 */
	ICMP_CODE_INFORMATION_REQUEST,		/* 15 */
	ICMP_CODE_INFORMATION_REPLY,		/* 16 */
	ICMP_CODE_ADDRESS_MASK_REQUEST,		/* 17 */
	ICMP_CODE_ADDRESS_MASK_REPLY,		/* 18 */
	ICMP_CODE_UNASSIGNED_03,			/* 19 */
	ICMP_CODE_UNASSIGNED_04,			/* 20 */
	ICMP_CODE_UNASSIGNED_05,			/* 21 */
	ICMP_CODE_UNASSIGNED_06,			/* 22 */
	ICMP_CODE_UNASSIGNED_07,			/* 23 */
	ICMP_CODE_UNASSIGNED_08,			/* 24 */
	ICMP_CODE_UNASSIGNED_09,			/* 25 */
	ICMP_CODE_UNASSIGNED_10,			/* 26 */
	ICMP_CODE_UNASSIGNED_11,			/* 27 */
	ICMP_CODE_UNASSIGNED_12,			/* 28 */
	ICMP_CODE_UNASSIGNED_13,			/* 29 */
	ICMP_CODE_TRACEROUTE,				/* 30 */
	ICMP_CODE_DGRAM_CONVERSION_ERROR,	/* 31 */
	ICMP_CODE_MOBILE_HOST_REDIRECT,		/* 32 */
	ICMP_CODE_IPV6_WHERE_ARE_YOU,		/* 33 */
	ICMP_CODE_IPV6_I_AM_HERE,			/* 34 */
	ICMP_CODE_MOBILE_REGISTRATION_REQ,	/* 35 */
	ICMP_CODE_MOBILE_REGISTRATION_REPLY,/* 36 */
	ICMP_CODE_DOMAIN_NAME_REQ,			/* 37 */
	ICMP_CODE_DOMAIN_NAME_REPLY,		/* 38 */
} ICMPCodes;


typedef enum
{
	ICMP_PINGER_STATE_DISCONNECTED,
	ICMP_PINGER_STATE_RESOLVING_DNS,
	ICMP_PINGER_STATE_FAILED_DNS,
	ICMP_PINGER_STATE_LASTITEM
} ICMPPingerState;

typedef enum
{
	ICMP_EVENT_TIMEOUT,
	ICMP_EVENT_REPLY,
	ICMP_EVENT_DNS_RESOLV,
	ICMP_EVENT_DNS_FAILED,
	ICMP_EVENT_STOP,
	ICMP_EVENT_LASTITEM
} EvICMPBaseEventCodes;
/*******************************************************/
typedef struct _ICMPQueryInfo
{
	DLinkedListNode node;
	CommEvICMPBaseCBH *cb_handler;
	CommEvICMPHeader icmp_packet;
	struct timeval transmit_time;
	struct sockaddr_storage target_sockaddr;
	void *cb_data;

	int slot_id;
	int owner_id;

	int timeout_ms;
	int icmp_packet_sz;
	int tx_retry_count;

	struct
	{
		unsigned int in_use:1;
		unsigned int waiting_reply:1;
	} flags;

} ICMPQueryInfo;

typedef struct _ICMPReply
{
	CommEvIPHeader *ip_header;
	CommEvICMPHeader *icmp_packet;
	struct sockaddr_storage *sockaddr;
	int ip_header_sz;
	int ip_packet_sz;
	int icmp_header_sz;
	int icmp_payload_sz;
	int icmp_hopcount;

	unsigned int icmp_type :8;
	unsigned int icmp_code :8;
	unsigned int icmp_id :16;
	unsigned int icmp_seq :16;

} ICMPReply;

typedef struct _EvICMPPending
{
	MemArena *arena;
	DLinkedList req_list;
	DLinkedList reply_list;
	SlotQueue slots;
} EvICMPPending;

typedef struct _EvICMPBase
{
	struct _EvKQBase *ev_base;
	struct _EvKQBaseLogBase *log_base;

	int socket_fdv4;
	int socket_fdv6;

	int identy_id;
	int timer_id;
	int min_seen_timeout;

	EvICMPPending pending_v4;
	EvICMPPending pending_v6;

} EvICMPBase;

typedef struct _EvICMPPeriodicPingerConf
{
	EvDNSResolverBase *resolv_base;
	struct _EvKQBaseLogBase *log_base;
	struct sockaddr_storage *sockaddr;
	char *hostname_str;
	char *target_ip_str;
	long reset_count;
	long stop_count;
	int interval_ms;
	int timeout_ms;
	int payload_sz;
	int unique_id;
	int dns_resolv_on_fail;
	int timer_resolv_dns_ms;

} EvICMPPeriodicPingerConf;

typedef struct _EvICMPPeriodicPinger
{
	EvICMPBase *icmp_base;
	struct _EvDNSResolverBase *resolv_base;
	struct _EvKQBaseLogBase *log_base;
	int timer_id;
	int dnsreq_id;
	int state;

	struct
	{
		int resolv_dns_id;
	} timers;

	struct
	{
		int seq_reply_id;
		int seq_req_id;
		int icmp_slot_id;
	} last;

	struct
	{
		CommEvICMPBaseCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct
		{
			unsigned int enabled :1;
			unsigned int mutex_init :1;
		} flags;
	} events[ICMP_EVENT_LASTITEM];

	struct
	{
		struct sockaddr_storage sockaddr;
		char ip_addr_str[128];
		char hostname_str[1024];
		unsigned int unique_id;
		long reset_count;
		long stop_count;
		int interval;
		int timeout;
		int payload_sz;

		int timer_resolv_dns_ms;

	} cfg;

	struct
	{
		struct timeval lastreq_tv;
		struct timeval lastreply_tv;
		float packet_loss_pct;
		float latency_ms;
		long request_sent;
		long reply_recv;
		int hop_count;
	} stats;

	struct
	{
		DNSAReply a_reply;
		long expire_ts;
		int cur_idx;
	} dns;

	struct
	{
		unsigned int random_unique_id:1;
		unsigned int dns_need_lookup:1;
		unsigned int dns_balance_ips:1;
		unsigned int dns_resolv_on_fail:1;
		unsigned int destroy_after_dns_fail:1;

		unsigned int waiting_reply:1;
	} flags;

} EvICMPPeriodicPinger;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
/* comm/core/comm_dns_resolver.c */
/******************************************************************************************************/
EvDNSResolverBase *CommEvDNSResolverBaseNew(struct _EvKQBase *ev_base, EvDNSResolverConf *conf);
void CommEvDNSResolverBaseDestroy(EvDNSResolverBase *resolv_base);
int CommEvDNSGetHostByName(EvDNSResolverBase *resolv_base, char *host, CommEvDNSResolverCBH *cb_handler, void *cb_data);
int CommEvDNSGetHostByNameX(EvDNSResolverBase *resolv_base, char *host, CommEvDNSResolverCBH *cb_handler, void *cb_data, int query_x);
int CommEvDNSCheckIfNeedDNSLookup(char *host_ptr);
int CommEvDNSCancelPendingRequest(EvDNSResolverBase *resolv_base, int req_id);
int CommEvDNSDataCancelAndClean(EvDNSReplyHandler *dnsdata);

/******************************************************************************************************/
/* comm/core/icmp/comm_icmp_base.c */
/******************************************************************************************************/
EvICMPBase *CommEvICMPBaseNew(struct _EvKQBase *kq_base);
void CommEvICMPBaseDestroy(EvICMPBase *icmp_base);
int CommEvICMPRequestCancelByReqID(EvICMPBase *icmp_base, EvICMPPending *icmp_pending, int req_id);
int CommEvICMPRequestCancelByOwnerID(EvICMPBase *icmp_base, EvICMPPending *icmp_pending, int owner_id);

int CommEvICMPRequestSend(EvICMPBase *icmp_base, struct sockaddr_storage *sockaddr, int type, int code, int seq, int timeout_ms, char *payload, int payload_len,
		CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id);
int CommEvICMPEchoSendRequestByInAddr(EvICMPBase *icmp_base, struct sockaddr_storage *sockaddr, int code, int seq, int size, int timeout_ms,
		CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id);
int CommEvICMPEchoSendRequestBySockAddr(EvICMPBase *icmp_base, struct sockaddr_storage *sockaddr, int code, int seq, int size, int timeout_ms, CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id);
int CommEvICMPEchoSendRequest(EvICMPBase *icmp_base, char *addr_str, int code, int seq, int size, int timeout_ms, CommEvICMPBaseCBH *cb_handler, void *cb_data, int owner_id);

float CommEvICMPtvSubMsec(struct timeval *when, struct timeval *now);

/* comm/utils/comm_icmp_pinger.c */
EvICMPPeriodicPinger *CommEvICMPPeriodicPingerNew(EvICMPBase *icmp_base, EvICMPPeriodicPingerConf *pinger_conf);
void CommEvICMPPeriodicPingerDestroy(EvICMPPeriodicPinger *icmp_pinger);
int CommEvICMPPeriodicPingerRun(EvICMPPeriodicPinger *icmp_pinger, int only_sched);
int CommEvICMPPeriodicPingerStop(EvICMPPeriodicPinger *icmp_pinger);
void CommEvICMPPeriodicPingerStatsReset(EvICMPPeriodicPinger *icmp_pinger);
int CommEvICMPPeriodicPingerEventIsSet(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type);
void CommEvICMPPeriodicPingerEventSet(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type, CommEvICMPBaseCBH *cb_handler, void *cb_data);
void CommEvICMPPeriodicPingerEventCancel(EvICMPPeriodicPinger *icmp_pinger, EvICMPBaseEventCodes ev_type);
void CommEvICMPPeriodicPingerEventCancelAll(EvICMPPeriodicPinger *icmp_pinger);
int CommEvICMPPeriodicPingerJSONDump(EvICMPPeriodicPinger *icmp_pinger, MemBuffer *json_reply_mb);
/******************************************************************************************************/
#endif /* LIBBRB_COMM_UTILS_H_ */
