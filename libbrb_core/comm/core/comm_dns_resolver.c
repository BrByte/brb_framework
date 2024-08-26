/*
 * comm_dns_resolver.c
 *
 *  Created on: 2013-01-19
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

/**************************************************************************************************************************/
typedef struct _rfc1035_rr
{
	char name[RFC1035_MAXHOSTNAMESZ];
	unsigned short type;
	unsigned short class;
	unsigned int ttl;
	unsigned short rdlength;
	char *rdata;
} rfc1035_rr;
/**************************************************************************************************************************/
typedef struct _rfc1035_query
{
	char name[RFC1035_MAXHOSTNAMESZ];
	unsigned short qtype;
	unsigned short qclass;
} rfc1035_query;
/**************************************************************************************************************************/
typedef struct _rfc1035_message
{
    unsigned short id;          // 16-bit identifier for the DNS message

    unsigned int qr:1;          // Query/Response flag (0 for query, 1 for response)
    unsigned int opcode:4;      // Operation code (4 bits) indicating the type of query
    							// Tells receiving machine the intent of the message.
								// Generally 0 meaning normal query, however there are other valid options such as 1 for reverse query and 2 for server status.

    unsigned int aa:1;          // Authoritative Answer flag (1 if response is from authoritative server)
    unsigned int tc:1;          // Truncated flag (1 if message was truncated)
    unsigned int rd:1;          // Recursion Desired flag (1 if client wants recursive resolution)
    unsigned int ra:1;          // Recursion Available flag (1 if server supports recursive queries)
    unsigned int rcode:4;       // Response code (4 bits) indicating the result of the query

    unsigned short qdcount;     // Number of questions in the message
    unsigned short ancount;     // Number of answer resource records
    unsigned short nscount;     // Number of authority resource records
    unsigned short arcount;     // Number of additional resource records

    rfc1035_query *query;       // Pointer to the question section of the message
    rfc1035_rr *answer;         // Pointer to the answer section of the message
} rfc1035_message;
/**************************************************************************************************************************/
typedef struct _rfc1035_errors
{
	int error_n;
	char *string;
} rfc1035_errors;
/**************************************************************************************************************************/
static const rfc1035_errors rfc1035_str_error_arr[] =
{
		{0, "No error condition"},
		{1, "Format Error: The name server was unable to interpret the query."},
		{2, "Server Failure: The name server was unable to process this query."},
		{3, "Name Error: The domain name does not exist."},
		{4, "Not Implemented: The name server does not support the requested kind of query."},
		{5, "Refused: The name server refuses to perform the specified operation."},
		{15, "The DNS reply message is corrupt or could not be safely parsed."}

};
/**************************************************************************************************************************/
int rfc1035_errno;
const char *rfc1035_error_message;

static EvBaseKQCBH CommEvDNSDataEvent;
static EvBaseKQCBH CommEvDNSWriteEvent;
static EvBaseKQCBH CommEvDNSPeriodicTimerEvent;
static int CommEvDNStvSubMsec(struct timeval *t1, struct timeval *t2);

static int rfc1035AnswersUnpack(const char *buf, size_t sz, rfc1035_rr **records, unsigned short *id);
static unsigned short rfc1035BuildAllQuery(const char *hostname, char *buf, size_t *szp, unsigned short qid);
static unsigned short rfc1035BuildAQuery(const char *hostname, char *buf, size_t *szp, unsigned short qid);
static unsigned short rfc1035BuildAAAAQuery(const char *hostname, char *buf, size_t *szp, unsigned short qid);
static unsigned short rfc1035BuildPTRQuery(const struct in_addr addr, char *buf, size_t *szp, unsigned short qid);
static void rfc1035SetQueryID(char *buf, unsigned short qid);
static int rfc1035HeaderPack(char *buf, size_t sz, rfc1035_message *hdr);
static int rfc1035LabelPack(char *buf, size_t sz, const char *label);
static int rfc1035NamePack(char *buf, size_t sz, const char *name);
static int rfc1035QuestionPack(char *buf, size_t sz, const char *name, unsigned short type, unsigned short class);
static int rfc1035HeaderUnpack(const char *buf, size_t sz, int *off, rfc1035_message * h);
static int rfc1035NameUnpack(const char *buf, size_t sz, int *off, unsigned short *rdlength, char *name, size_t ns, int rdepth);
static int rfc1035RRUnpack(const char *buf, size_t sz, int *off, rfc1035_rr * RR);
static char *rfc1035ErronoStr(int n);
static void rfc1035SetErrno(int n);
static void rfc1035RRDestroy(rfc1035_rr *rr, int n);
static int rfc1035QueryUnpack(const char *buf, size_t sz, int *off, rfc1035_query * query);
static void rfc1035MessageDestroy(rfc1035_message * msg);
static int rfc1035MessageUnpack(const char *buf, size_t sz, rfc1035_message ** answer);
/**************************************************************************************************************************/
EvDNSResolverBase *CommEvDNSResolverBaseNew(EvKQBase *ev_base, EvDNSResolverConf *conf)
{
	EvDNSResolverBase *resolv_base = calloc(1, sizeof(EvDNSResolverBase));

	/* Create a new UDP socket and set it to non_blocking */
	resolv_base->socket_fd = EvKQBaseSocketUDPNew(ev_base);
	EvKQBaseSocketSetNonBlock(ev_base, resolv_base->socket_fd);

	/* Set socket description */
	EvKQBaseFDDescriptionSetByFD(ev_base, resolv_base->socket_fd , "BRB_EV_COMM - DNS resolver UDP client - Port [%u]", htons(conf->dns_port));

	resolv_base->ev_base						= ev_base;
	resolv_base->dns_serv_addr.sin_family		= AF_INET;
	resolv_base->dns_serv_addr.sin_port			= htons(conf->dns_port);
	resolv_base->dns_serv_addr.sin_addr.s_addr	= inet_addr(conf->dns_ip_str);

	resolv_base->retry_count					= conf->retry_count;
	resolv_base->timeout_ms.lookup				= conf->lookup_timeout_ms;
	resolv_base->timeout_ms.retry				= conf->retry_timeout_ms;
	resolv_base->pending_req.arena				= MemArenaNew(4096, (sizeof(DNSPendingQuery) + 1), 4096, MEMARENA_MT_UNSAFE);

	/* Initialize resolver SLOTs */
	SlotQueueInit(&resolv_base->pending_req.slots, RESOLVER_DNS_MAX_SEQID, (ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));

	/* Initialize request and reply list */
	DLinkedListInit(&resolv_base->pending_req.req_list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&resolv_base->pending_req.reply_list, BRBDATA_THREAD_UNSAFE);

	/* Initialize periodic timer */
	resolv_base->timer_id 	= EvKQBaseTimerAdd(resolv_base->ev_base, COMM_ACTION_ADD_VOLATILE, (resolv_base->timeout_ms.retry / 2), CommEvDNSPeriodicTimerEvent, resolv_base);

	return resolv_base;
}
/**************************************************************************************************************************/
void CommEvDNSResolverBaseDestroy(EvDNSResolverBase *resolv_base)
{
	/* Sanity check */
	if (!resolv_base)
		return;

	if (resolv_base->socket_fd > -1)
	{
		EvKQBaseSocketClose(resolv_base->ev_base, resolv_base->socket_fd);
		resolv_base->socket_fd = -1;
	}

	SlotQueueDestroy(&resolv_base->pending_req.slots);
	MemArenaDestroy(resolv_base->pending_req.arena);

	free(resolv_base);

	return;
}
/**************************************************************************************************************************/
int CommEvDNSGetHostByName(EvDNSResolverBase *resolv_base, char *host, CommEvDNSResolverCBH *cb_handler, void *cb_data)
{
	return CommEvDNSGetHostByNameX(resolv_base, host, cb_handler, cb_data, 0);
}
/**************************************************************************************************************************/
int CommEvDNSGetHostByNameX(EvDNSResolverBase *resolv_base, char *host, CommEvDNSResolverCBH *cb_handler, void *cb_data, int query_x)
{
	struct timeval *current_time_ptr;
	struct timeval current_time;
	int slot_id;

	DNSPendingQuery *pending_query	= NULL;

	/* Must have a notification handler */
	if (!cb_handler)
	{
		/* Touch base statistics */
		resolv_base->stats.request_failed++;
		return -1;
	}

	slot_id = SlotQueueGrab(&resolv_base->pending_req.slots);

	/* No more slots left, bail out */
	if (slot_id < 0)
	{
		/* Touch base statistics */
		resolv_base->stats.request_failed++;
		return -1;
	}

	pending_query 						= MemArenaGrabByID(resolv_base->pending_req.arena, slot_id);

	/* FATAL: Already in use, bail out */
	assert(!pending_query->flags.in_use);

	/* Clean query info */
	memset(pending_query, 0, sizeof(DNSPendingQuery));

	/* Mark as in use */
	pending_query->flags.in_use			= 1;

	/* Finish event cb_hanler and data */
	pending_query->events.cb_handler	= cb_handler;
	pending_query->events.cb_data		= cb_data;
	pending_query->slot_id				= slot_id;

	/* Get current TIMESTAMP only if its not inside ev_base */
	if (0 == resolv_base->ev_base->stats.cur_invoke_tv.tv_sec)
	{
		gettimeofday(&current_time, NULL);
		current_time_ptr 				= &current_time;
	}
	else
		current_time_ptr 				= &resolv_base->ev_base->stats.cur_invoke_tv;

	KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "SLOT_ID [%d] - Will resolve [%s]\n", slot_id, host);

	/* Save sent time */
	memcpy(&pending_query->transmit_tv, current_time_ptr, sizeof(struct timeval));
	memcpy(&pending_query->retransmit_tv, current_time_ptr, sizeof(struct timeval));

	/* Build 1035 packet */
	if (query_x > 0)
		rfc1035BuildAAAAQuery(host, (char*)&pending_query->rfc1035_request, (size_t*)&pending_query->rfc1035_request_sz, pending_query->slot_id);
	else
		rfc1035BuildAQuery(host, (char*)&pending_query->rfc1035_request, (size_t*)&pending_query->rfc1035_request_sz, pending_query->slot_id);

	/* Enqueue for writing */
	DLinkedListAddTail(&resolv_base->pending_req.req_list, &pending_query->node, pending_query);

	/* Set write event for socket */
	EvKQBaseSetEvent(resolv_base->ev_base, resolv_base->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvDNSWriteEvent, resolv_base);

	/* If there is ENQUEUED data, schedule WRITE event and LEAVE, as we need to PRESERVE WRITE ORDER */
	if (!DLINKED_LIST_PTR_ISEMPTY(&resolv_base->pending_req.reply_list))
	{
		/* Set write event for socket */
		EvKQBaseSetEvent(resolv_base->ev_base, resolv_base->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvDNSWriteEvent, resolv_base);
	}
	/* Try to write on this very same IO LOOP */
	else
	{
		CommEvDNSWriteEvent(resolv_base->socket_fd, 8092, -1, resolv_base, resolv_base->ev_base);
	}

	/* Touch base statistics */
	resolv_base->stats.request_sent_sched++;

	/* Initialize periodic timer */
	if (resolv_base->timer_id < 0)
		resolv_base->timer_id 			= EvKQBaseTimerAdd(resolv_base->ev_base, COMM_ACTION_ADD_VOLATILE, (resolv_base->timeout_ms.retry / 2), CommEvDNSPeriodicTimerEvent, resolv_base);

	return slot_id;
}
/**************************************************************************************************************************/
int CommEvDNSCheckIfNeedDNSLookup(char *host_ptr)
{
    int dots_found;
    int colons_found;
    int state;
    int i;

    enum
    {
        HOSTNAME_IS_IP,
        HOSTNAME_IS_FQDN,
        HOSTNAME_IS_IPV6,
    };

    /* Check if request URL is FQDN, IPv4 or IPv6 */
    for (i = 0, dots_found = 0, colons_found = 0, state = HOSTNAME_IS_IP; host_ptr[i] != '\0'; i++)
    {
        /* Seen a dot on domain (for IPv4 or FQDN) */
        if ('.' == host_ptr[i])
            dots_found++;

        /* Seen a colon (for IPv6) */
        if (':' == host_ptr[i])
        {
            colons_found++;
            state = HOSTNAME_IS_IPV6;
        }

        /* If both dots and colons are found, it's neither IPv4 nor IPv6, set as FQDN */
        if (dots_found > 0 && colons_found > 0)
        {
            state = HOSTNAME_IS_FQDN;
            break;
        }

        /* An IPv4 can not have more than three dot chars, set as FQDN and stop */
        if (dots_found > 3)
        {
            state = HOSTNAME_IS_FQDN;
            break;
        }

        /* An IPv6 can have at most seven colons */
        if (colons_found > 7)
        {
            state = HOSTNAME_IS_FQDN;
            break;
        }

        /* Not a valid character for IP addresses, set to FQDN and bail out */
        if ((!isdigit(host_ptr[i])) && (host_ptr[i] != '.') && (host_ptr[i] != ':') &&
            (!isxdigit(host_ptr[i]))) /* isxdigit checks for valid hex characters for IPv6 */
        {
            state = HOSTNAME_IS_FQDN;
            break;
        }
    }

    /* Set flags and return */
    if (HOSTNAME_IS_FQDN == state)
        return 1;

    /* If it's an IPv6 address, handle accordingly */
    if (HOSTNAME_IS_IPV6 == state)
        return 0;

    /* It's an IPv4 address */
    return 0;
}
/**************************************************************************************************************************/
int CommEvDNSDataCancelAndClean(EvDNSReplyHandler *dnsdata)
{
	/* Cancel pending DNS request */
	if (dnsdata->req_id	> -1)
	{
		/* Cancel pending DNS request and set it to -1 */
		CommEvDNSCancelPendingRequest(dnsdata->resolv_base, dnsdata->req_id);
		dnsdata->req_id 	= -1;
	}

	return 0;
}
/**************************************************************************************************************************/
int CommEvDNSCancelPendingRequest(EvDNSResolverBase *resolv_base, int req_id)
{
	DNSPendingQuery *pending_query;

	/* sanitize */
	if (!resolv_base)
		return 0;

	pending_query 		= MemArenaGrabByID(resolv_base->pending_req.arena, req_id);

	/* Not in use, bail out */
	if (!pending_query->flags.in_use)
	{
		KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Trying to cancel not in use request [%d]\n", pending_query->slot_id);
		return 0;
	}

	KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "SLOT_ID [%d] - WAITING_REPLY [%d]\n", pending_query->slot_id, pending_query->flags.waiting_reply);

	/* There are two conditions to be addressed here: if we are not waiting for reply, just wipe the pending request out. Otherwise, mark it as canceled, and release
	 * the slot once we see a reply, or if we try to RXMIT */
	if (pending_query->flags.waiting_reply)
	{
		/* Mark it as canceled */
		pending_query->flags.canceled_req = 1;
	}
	else
	{
		/* Free slot and delete from pending list */
		SlotQueueFree(&resolv_base->pending_req.slots, pending_query->slot_id);
		DLinkedListDelete(&resolv_base->pending_req.req_list, &pending_query->node);

		/* Clean array slot */
		memset(pending_query, 0, sizeof(DNSPendingQuery));
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvDNSPeriodicTimerEvent(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DLinkedListNode *node;
	DNSPendingQuery *pending_query;
	DNSAReply a_reply;
	int total_delta_ms;
	int rxmit_delta_ms;

	EvDNSResolverBase *resolv_base	= cb_data;
	int rxmit_flag					= 0;

	/* We will reschedule timer after done */
	resolv_base->timer_id 			= -1;

	/* Nothing to check, just return - timer will be disabled */
	if (DLINKED_LIST_ISEMPTY(resolv_base->pending_req.reply_list))
		return 1;

	/* Walk all pending reply list */
	for (node = resolv_base->pending_req.reply_list.head; node; node = node->next)
	{
		loop_without_move:

		/* Sanity check */
		if ((!node) || (!node->data))
			break;

		pending_query			= node->data;

		/* If we are here, we must be waiting for a reply */
		assert(pending_query->flags.waiting_reply);

		/* Calculate time deltas */
		total_delta_ms	= CommEvDNStvSubMsec(&pending_query->transmit_tv, &resolv_base->ev_base->stats.cur_invoke_tv);
		rxmit_delta_ms	= CommEvDNStvSubMsec(&pending_query->retransmit_tv, &resolv_base->ev_base->stats.cur_invoke_tv);

		KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Checking REQ_ID [%d] - TOTAL_MS [%d] - RXMIT_MS [%d]\n", pending_query->slot_id, total_delta_ms, rxmit_delta_ms);

		/* Request timed out, invoke CB_H with reply_count zero */
		if ((total_delta_ms > resolv_base->timeout_ms.lookup))
		{
			goto timed_out;
		}
		/* Request partial timed out, retry lookup */
		else if (rxmit_delta_ms > resolv_base->timeout_ms.retry)
		{
			/* We can still retry - Reschedule to write again */
			if (pending_query->req_count <= resolv_base->retry_count)
			{
				KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "RXMIT REQ_ID [%d] - TOTAL [%d/%d]\n", pending_query->slot_id, pending_query->req_count, resolv_base->retry_count);

				/* Touch rxmit_timestamp */
				memcpy(&pending_query->retransmit_tv,  &resolv_base->ev_base->stats.cur_invoke_tv, sizeof(struct timeval));

				/* Move it to pending reply list and increment sent request count */
				DLinkedListDelete(&resolv_base->pending_req.reply_list, &pending_query->node);
				DLinkedListAddTail(&resolv_base->pending_req.req_list, &pending_query->node, pending_query);

				/* Schedule for retransmit, not waiting for reply */
				pending_query->flags.waiting_reply	= 0;
				pending_query->flags.re_xmit		= 1;

				rxmit_flag++;

				continue;

			}
			/* Too many retries, bail out */
			else if (!pending_query->flags.canceled_req)
				goto timed_out;
		}

		continue;

		timed_out:

		resolv_base->stats.request_timeout++;

		/* Clean buffers */
		memset(&a_reply, 0, sizeof(DNSAReply));

		/* Invoke CB_HANDLER with zero reply */
		if ((pending_query->events.cb_handler) && (!pending_query->flags.canceled_req))
			pending_query->events.cb_handler(resolv_base, pending_query->events.cb_data, (void*)&a_reply, DNS_REPLY_TIMEDOUT);

		/* Point to next node */
		node = node->next;

		KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "TIMED_OUT REQ_ID [%d] - TOTAL [%d/%d]\n", pending_query->slot_id, pending_query->req_count, resolv_base->retry_count);

		/* Free slot and remove node from list */
		SlotQueueFree(&resolv_base->pending_req.slots, pending_query->slot_id);
		DLinkedListDelete(&resolv_base->pending_req.reply_list, &pending_query->node);

		/* Clean pending request */
		memset(pending_query, 0, sizeof(DNSPendingQuery));

		goto loop_without_move;
	}

	/* Reschedule WRITE event */
	if (rxmit_flag > 0)
		EvKQBaseSetEvent(resolv_base->ev_base, resolv_base->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvDNSWriteEvent, resolv_base);

	/* Initialize periodic timer */
	resolv_base->timer_id = EvKQBaseTimerAdd(resolv_base->ev_base, COMM_ACTION_ADD_VOLATILE, (resolv_base->timeout_ms.retry / 2), CommEvDNSPeriodicTimerEvent, resolv_base);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvDNSWriteEvent(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DNSPendingQuery *pending_query;
	DNSPendingQuery *next_pending_query;

	EvDNSResolverBase *resolv_base	= cb_data;
	int wrote_bytes 				= 0;
	int total_wrote_bytes			= 0;
	int total_req_sent				= 0;

	/* Write all packets we can */
	while (1)
	{
		pending_query 				= DLinkedListPopHead(&resolv_base->pending_req.req_list);

		/* No more queries to write, bail out */
		if (!pending_query)
		{
			KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FINISH LIST - Wrote [%d] requests\n", total_req_sent);

			/* Set read event for socket */
			EvKQBaseSetEvent(resolv_base->ev_base, resolv_base->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvDNSDataEvent, resolv_base);
			return total_wrote_bytes;
		}

		assert(!pending_query->flags.waiting_reply);

		/* Unable to write next request, leave */
		if ( (pending_query->rfc1035_request_sz + wrote_bytes + 1) > can_write_sz)
		{
			/* Enqueue request back for next IO loop */
			DLinkedListAdd(&resolv_base->pending_req.req_list, &pending_query->node, pending_query);

			goto leave;
		}

		/* Write DNS query */
		wrote_bytes = sendto(resolv_base->socket_fd, (char*)&pending_query->rfc1035_request, pending_query->rfc1035_request_sz, 0,
				(struct sockaddr *)&resolv_base->dns_serv_addr, sizeof(struct sockaddr_in));

		KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "WROTE [%d] bytes\n", wrote_bytes);

		/* Failed writing, enqueue it back and bail out */
		if (wrote_bytes < pending_query->rfc1035_request_sz)
		{
			KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed writing\n");

			/* Enqueue request back for next IO loop */
			DLinkedListAdd(&resolv_base->pending_req.req_list, &pending_query->node, pending_query);

			/* Increment fail count */
			pending_query->fail_count++;

			goto leave;
		}

		total_req_sent++;
		total_wrote_bytes += wrote_bytes;

		/* Touch base statistics */
		resolv_base->stats.request_sent_write++;
		resolv_base->stats.total_bytes_sent += wrote_bytes;

		/* Move it to pending reply list and increment sent request count */
		DLinkedListDelete(&resolv_base->pending_req.req_list, &pending_query->node);
		DLinkedListAddTail(&resolv_base->pending_req.reply_list, &pending_query->node, pending_query);
		pending_query->req_count++;

		/* Set as waiting for reply - On reply_list */
		pending_query->flags.waiting_reply = 1;

		continue;
	}

	leave:

	/* Set read event for socket */
	EvKQBaseSetEvent(resolv_base->ev_base, resolv_base->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvDNSDataEvent, resolv_base);

	/* Reschedule WRITE event */
	EvKQBaseSetEvent(resolv_base->ev_base, resolv_base->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvDNSWriteEvent, resolv_base);

	KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "LEAVE - Wrote [%d] requests\n", total_req_sent);

	return total_wrote_bytes;
}
/**************************************************************************************************************************/
static int CommEvDNSDataEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DNSPendingQuery *pending_query;
	rfc1035_rr *answers = NULL;
	DNSAReply a_reply;

	char rfc1053_reply[to_read_sz + 16];
	int data_read;
	int i;

	EvDNSResolverBase *resolv_base	= cb_data;
	int reply_count					= -1;
	int packet_count				= 0;
	int valid_reply_count			= 0;
	unsigned short reply_seq_id		= 0;

	packet_count = 1;

	/* Clean buffers */
	memset(&a_reply, 0, sizeof(DNSAReply));
	memset(&rfc1053_reply, 0, sizeof(rfc1053_reply));

	KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - TO read [%d] bytes\n", fd, to_read_sz);

//	if (to_read_sz < 12)
//		goto next_packet;

	/* Read UDP data from socket */
	data_read 						= recv(fd, (void*)&rfc1053_reply, to_read_sz, 0);

	KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] READ [%d]/[%d] bytes\n", fd, data_read, to_read_sz);

	/* Failed reading */
	if (data_read < 0)
		goto next_packet;

	/* Touch base statistics */
	resolv_base->stats.reply_received_total++;
	resolv_base->stats.total_bytes_received += data_read;

	/* Parse DNS reply */
	reply_count 			= rfc1035AnswersUnpack((const char*)&rfc1053_reply, data_read, &answers, &reply_seq_id);

//	EvKQBaseLoggerHexDump(resolv_base->log_base, LOGTYPE_DEBUG, &rfc1053_reply, data_read, 8, 4);

	/* Grab pending query back */
	pending_query			= MemArenaGrabByID(resolv_base->pending_req.arena, reply_seq_id);

	KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] ID [%d] - READ [%d]/[%d] bytes - reply [%d]\n", fd, reply_seq_id, data_read, to_read_sz, reply_count);

//	assert(pending_query->flags.in_use);

	/* Unexpected reply */
	if (!pending_query->flags.in_use)
	{
		KQBASE_LOG_PRINTF(resolv_base->log_base, LOGTYPE_WARNING, LOGCOLOR_GREEN, "Unexpected reply for REQ_ID [%d]\n", reply_seq_id);
		goto next_packet;
	}

	/* Negative replies means error */
	if (reply_count < 0)
	{
		a_reply.ip_count 	= reply_count;
		goto failed_invoke;
	}

	/* Navigate thru all replies and grab data */
	for (valid_reply_count = 0, i = 0; (i < reply_count) && (i < RESOLVER_DNS_MAX_REPLY_COUNT - 1); i++)
	{
		/* TYPE_A is what we want, copy IP and TTL */
		if (answers[i].type == RFC1035_TYPE_A)
		{
			memcpy(&a_reply.ip_arr[valid_reply_count].addr, answers[i].rdata, sizeof(struct in_addr));
		}
		else if (answers[i].type == RFC1035_TYPE_AAAA)
		{
			memcpy(&a_reply.ip_arr[valid_reply_count].addr6, answers[i].rdata, sizeof(struct in6_addr));
		}
		else
		{
			continue;
		}

		a_reply.ip_arr[valid_reply_count].type 	= answers[i].type;
		a_reply.ip_arr[valid_reply_count].ttl 	= answers[i].ttl;
		valid_reply_count++;
	}

	/* Touch base statistics */
	resolv_base->stats.reply_received_valid++;

	/* Save reply count */
	a_reply.ip_count = valid_reply_count;

	failed_invoke:

	/* Invoke CB_HANDLER */
	if ((pending_query->events.cb_handler) && (!pending_query->flags.canceled_req))
		pending_query->events.cb_handler(resolv_base, pending_query->events.cb_data, (void*)&a_reply, DNS_REPLY_SUCCESS);

	/* Release pending query */
	SlotQueueFree(&resolv_base->pending_req.slots, pending_query->slot_id);
	DLinkedListDelete(&resolv_base->pending_req.reply_list, &pending_query->node);
	memset(pending_query, 0, sizeof(DNSPendingQuery));

	next_packet:

	/* Destroy RR answers */
	rfc1035RRDestroy(answers, reply_count);

	/* Set read event for socket */
	EvKQBaseSetEvent(resolv_base->ev_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvDNSDataEvent, resolv_base);

	return data_read;

}
/**************************************************************************************************************************/
static int CommEvDNStvSubMsec(struct timeval *t1, struct timeval *t2)
{
	return (t2->tv_sec - t1->tv_sec) * 1000 +	(t2->tv_usec - t1->tv_usec) / 1000;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* RFC 1035 private functions
/**************************************************************************************************************************/
static int rfc1035AnswersUnpack(const char *buf, size_t sz, rfc1035_rr **records, unsigned short *id)
{
	int off = 0;
	int l;
	int i;
	int nr = 0;

	rfc1035_message hdr;
	rfc1035_rr *recs;

	memset(&hdr, '\0', sizeof(hdr));

	if (rfc1035HeaderUnpack(buf + off, sz - off, &off, &hdr))
	{
		rfc1035SetErrno(rfc1035_unpack_error);
		return -rfc1035_unpack_error;
	}

	*id = hdr.id;

	rfc1035_errno = 0;
	rfc1035_error_message = NULL;

	if (hdr.rcode)
	{
		rfc1035SetErrno((int) hdr.rcode);
		return -rfc1035_errno;
	}

	i = (int) hdr.qdcount;

	/* skip question */
	while (i--)
	{

		do
		{
			l = (int) (unsigned char) *(buf + off);
			off++;

			/* compression */
			if (l > 191)
			{
				off++;
				break;
			}
			/* illegal combination of compression bits */
			else if (l > RFC1035_MAXLABELSZ)
			{
				rfc1035SetErrno(rfc1035_unpack_error);
				return -rfc1035_unpack_error;
			}
			else
			{
				off += l;
			}
		} while (l > 0);	/* a zero-length label terminates */

		/* qtype, qclass */
		off += 4;

		if (off > sz)
		{
			rfc1035SetErrno(rfc1035_unpack_error);
			return -rfc1035_unpack_error;
		}
	}

	i = (int) hdr.ancount;

	if (i == 0)
		return 0;

	recs = calloc(i, sizeof(rfc1035_rr));

	while (i--)
	{
		/* corrupt packet */
		if (off >= sz)
		{
			break;
		}
		/* corrupt RR */
		if (rfc1035RRUnpack(buf, sz, &off, &recs[i]))
		{
			break;
		}

		nr++;
	}

	if (nr == 0)
	{
		/* we expected to unpack some answers (ancount != 0), but didn't actually get any. */
		free(recs);
		rfc1035SetErrno(rfc1035_unpack_error);
		return -rfc1035_unpack_error;
	}

	*records = recs;
	return nr;
}
/**************************************************************************************************************************/
/* Builds a message buffer with a QUESTION to lookup A records for a hostname.  Caller must allocate 'buf' which should
 * probably be at least 512 octets.  The 'szp' initially specifies the size of the buffer, on return it contains
 * the size of the message (i.e. how much to write).
 * Returns the size of the query
/**************************************************************************************************************************/
static unsigned short rfc1035BuildAQuery(const char *hostname, char *buf, size_t *szp, unsigned short qid)
{
	static rfc1035_message header;
	off_t offset = 0;
	size_t sz = *szp;

	memset(&header, 0, sizeof(rfc1035_message));

	header.id		= qid;
	header.qr		= 0;
	header.rd		= 1;
	header.opcode	= 0;		/* QUERY */
	header.qdcount	= (unsigned int) 1;

	offset += rfc1035HeaderPack(buf + offset, sz - offset, &header);
	offset += rfc1035QuestionPack(buf + offset, sz - offset, hostname, RFC1035_TYPE_A, RFC1035_CLASS_IN);

	*szp = (size_t) offset;

	return header.id;
}
/**************************************************************************************************************************/
static unsigned short rfc1035BuildAAAAQuery(const char *hostname, char *buf, size_t *szp, unsigned short qid)
{
	static rfc1035_message header;
	off_t offset = 0;
	size_t sz = *szp;

	memset(&header, 0, sizeof(rfc1035_message));

	header.id		= qid;
	header.qr		= 0;
	header.rd		= 1;
	header.opcode	= 0;		/* QUERY */
	header.qdcount	= (unsigned int) 1;

	offset += rfc1035HeaderPack(buf + offset, sz - offset, &header);
	offset += rfc1035QuestionPack(buf + offset, sz - offset, hostname, RFC1035_TYPE_AAAA, RFC1035_CLASS_IN);

	*szp = (size_t) offset;

	return header.id;
}
/**************************************************************************************************************************/
static unsigned short rfc1035BuildAllQuery(const char *hostname, char *buf, size_t *szp, unsigned short qid)
{
	static rfc1035_message header;
	off_t offset = 0;
	size_t sz = *szp;

	memset(&header, 0, sizeof(rfc1035_message));

	header.id		= qid;
	header.qr		= 0;
	header.rd		= 1;
	header.opcode	= 0;		/* QUERY */
	header.qdcount	= (unsigned int) 2;

	offset += rfc1035HeaderPack(buf + offset, sz - offset, &header);
	offset += rfc1035QuestionPack(buf + offset, sz - offset, hostname, RFC1035_TYPE_A, RFC1035_CLASS_IN);
	offset += rfc1035QuestionPack(buf + offset, sz - offset, hostname, RFC1035_TYPE_AAAA, RFC1035_CLASS_IN);

	*szp = (size_t) offset;

	return header.id;
}
/**************************************************************************************************************************/
/* Builds a message buffer with a QUESTION to lookup PTR records for an address.  Caller must allocate 'buf' which should
 * probably be at least 512 octets.  The 'szp' initially specifies the size of the buffer, on return it contains
 * the size of the message (i.e. how much to write).
 * Returns the size of the query
/**************************************************************************************************************************/
static unsigned short rfc1035BuildPTRQuery(const struct in_addr addr, char *buf, size_t *szp, unsigned short qid)
{
	rfc1035_message header;
	static char rev[32];

	unsigned long offset	= 0;
	unsigned long sz		= *szp;
	unsigned int i			= ntohl(addr.s_addr);

	memset(&header, 0, sizeof(rfc1035_message));

	snprintf(rev, 32, "%u.%u.%u.%u.in-addr.arpa.", (i & 255), ((i >> 8) & 255), ((i >> 16) & 255), (i >> 24) & 255);

	header.id		= qid;
	header.qr		= 0;
	header.rd		= 1;
	header.opcode	= 0;		/* QUERY */
	header.qdcount	= (unsigned int) 1;

	/* Pack header */
	offset += rfc1035HeaderPack((buf + offset), (sz - offset), &header);
	/* Pack question */
	offset += rfc1035QuestionPack((buf + offset), (sz - offset), rev, RFC1035_TYPE_PTR, RFC1035_CLASS_IN);
	/* Update size */
	*szp = (size_t) offset;

	/* Return ID */
	return header.id;
}
/**************************************************************************************************************************/
/* We're going to retry a former query, but we just need a new ID for it.  Lucky for us ID is the first field in the message buffer.
/**************************************************************************************************************************/
static void rfc1035SetQueryID(char *buf, unsigned short qid)
{
	unsigned short s = htons(qid);
	memcpy(buf, &s, sizeof(s));
}
/**************************************************************************************************************************/
/* Packs a rfc1035_header structure into a buffer. Returns number of octets packed (should always be 12)
/**************************************************************************************************************************/
static int rfc1035HeaderPack(char *buf, size_t sz, rfc1035_message *hdr)
{
	int off = 0;
	unsigned short s;
	unsigned short t;

	s = htons(hdr->id);
	memcpy(buf + off, &s, sizeof(s));
	off += sizeof(s);

	t = 0;
	t |= hdr->qr << 15;
	t |= (hdr->opcode << 11);
	t |= (hdr->aa << 10);
	t |= (hdr->tc << 9);
	t |= (hdr->rd << 8);
	t |= (hdr->ra << 7);
	t |= hdr->rcode;

	s = htons(t);
	memcpy(buf + off, &s, sizeof(s));
	off += sizeof(s);

	s = htons(hdr->qdcount);
	memcpy(buf + off, &s, sizeof(s));
	off += sizeof(s);

	s = htons(hdr->ancount);
	memcpy(buf + off, &s, sizeof(s));
	off += sizeof(s);

	s = htons(hdr->nscount);
	memcpy(buf + off, &s, sizeof(s));
	off += sizeof(s);

	s = htons(hdr->arcount);
	memcpy(buf + off, &s, sizeof(s));
	off += sizeof(s);

	return off;
}
/**************************************************************************************************************************/
/* Packs a label into a buffer.  The format of a label is one octet specifying the number of character bytes to follow.
 * Labels must be smaller than 64 octets. Returns number of octets packed.
/**************************************************************************************************************************/
static int rfc1035LabelPack(char *buf, size_t sz, const char *label)
{
	int off = 0;
	size_t len = label ? strlen(label) : 0;

	if (len > RFC1035_MAXLABELSZ)
		len = RFC1035_MAXLABELSZ;

	*(buf + off) = (char) len;
	off++;
	memcpy(buf + off, label, len);
	off += len;
	return off;
}
/**************************************************************************************************************************/
/* Packs a name into a buffer.  Names are packed as a sequence of labels, terminated with NULL label.
 * Note message compression is not supported here. Returns number of octets packed.
/**************************************************************************************************************************/
static int rfc1035NamePack(char *buf, size_t sz, const char *name)
{
	int off = 0;
	char *copy = strdup(name);
	char *t;

	/* NOTE: use of strtok here makes names like foo....com valid. */
	for (t = strtok(copy, "."); t; t = strtok(NULL, "."))
		off += rfc1035LabelPack(buf + off, sz - off, t);

	free(copy);
	off += rfc1035LabelPack(buf + off, sz - off, NULL);
	return off;
}
/**************************************************************************************************************************/
/* Packs a QUESTION section of a message. Returns number of octets packed.
/**************************************************************************************************************************/
static int rfc1035QuestionPack(char *buf, size_t sz, const char *name, unsigned short type, unsigned short class)
{
	int off = 0;
	unsigned short s;
	off += rfc1035NamePack(buf + off, sz - off, name);
	s = htons(type);
	memcpy(buf + off, &s, sizeof(s));
	off += sizeof(s);
	s = htons(class);
	memcpy(buf + off, &s, sizeof(s));
	off += sizeof(s);
	return off;
}
/**************************************************************************************************************************/
/* Unpacks a RFC1035 message header buffer into the header fields of the rfc1035_message structure.
 * Updates the buffer offset, which is the same as number of octects unpacked since the header starts at offset 0.
 * Returns 0 (success) or 1 (error)
/**************************************************************************************************************************/
static int rfc1035HeaderUnpack(const char *buf, size_t sz, int *off, rfc1035_message * h)
{
	unsigned short s;
	unsigned short t;

	/* The header is 12 octets.  This is a bogus message if the size is less than that. */
	if (sz < 12)
		return 1;

	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);
	h->id = ntohs(s);

	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);
	t = ntohs(s);

	h->qr = (t >> 15) & 0x01;
	h->opcode = (t >> 11) & 0x0F;
	h->aa = (t >> 10) & 0x01;
	h->tc = (t >> 9) & 0x01;
	h->rd = (t >> 8) & 0x01;
	h->ra = (t >> 7) & 0x01;

	/* We might want to check that the reserved 'Z' bits (6-4) are all zero as per RFC 1035.  If not the message should be rejected. */
	h->rcode = t & 0x0F;
	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);

	h->qdcount = ntohs(s);
	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);

	h->ancount = ntohs(s);
	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);

	h->nscount = ntohs(s);
	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);

	h->arcount = ntohs(s);
	return 0;
}
/**************************************************************************************************************************/
/* Unpacks a Name in a message buffer into a char*. Note 'buf' points to the beginning of the whole message, 'off' points to the spot where
 * the Name begins, and 'sz' is the size of the whole message.  'name' must be allocated by the caller.
 *
 * Supports the RFC1035 message compression through recursion. Updates the new buffer offset.
 *
 * Returns 0 (success) or 1 (error)
/**************************************************************************************************************************/
static int rfc1035NameUnpack(const char *buf, size_t sz, int *off, unsigned short *rdlength, char *name, size_t ns, int rdepth)
{
	int no = 0;
	unsigned char c;
	size_t len;

	do {

		/* sanity check */
		if ((*off) >= sz)
			return 1;

		c = *(buf + (*off));

		if (c > 191)
		{
			/* blasted compression */
			unsigned short s;
			int ptr;

			/* infinite pointer loop */
			if (rdepth > 64)
				return 1;

			memcpy(&s, buf + (*off), sizeof(s));
			s = ntohs(s);
			(*off) += sizeof(s);

			/* Sanity check */
			if ((*off) > sz)
				return 1;

			ptr = s & 0x3FFF;

			/* Make sure the pointer is inside this message */
			if (ptr >= sz)
				return 1;

			return rfc1035NameUnpack(buf, sz, &ptr, rdlength, name + no, ns - no, rdepth + 1);

		}
		else if (c > RFC1035_MAXLABELSZ)
		{
			/* (The 10 and 01 combinations are reserved for future use.)" */
			return 1;
		}
		else
		{
			(*off)++;
			len = (size_t) c;

			if (len == 0)
				break;

			/* label won't fit */
			if (len > (ns - no - 1))
				return 1;

			/* message is too short */
			if ((*off) + len >= sz)
				return 1;

			memcpy(name + no, buf + (*off), len);
			(*off) += len;
			no += len;
			*(name + (no++)) = '.';

			if (rdlength)
				*rdlength += len + 1;
		}

	} while (c > 0 && no < ns);

	if (no)
		*(name + no - 1) = '\0';
	else
		*name = '\0';
	return 0;
}
/**************************************************************************************************************************/
/* Unpacks a RFC1035 Resource Record into 'RR' from a message buffer. The caller must free RR->rdata!
 * Updates the new message buffer offset.
 *
 * Returns 0 (success) or 1 (error)
/**************************************************************************************************************************/
static int rfc1035RRUnpack(const char *buf, size_t sz, int *off, rfc1035_rr * RR)
{
	unsigned short s;
	unsigned int i;
	unsigned short rdlength;
	int rdata_off;

	if (rfc1035NameUnpack(buf, sz, off, NULL, RR->name, RFC1035_MAXHOSTNAMESZ, 0))
	{
		memset(RR, '\0', sizeof(*RR));
		return 1;
	}

	/* Make sure the remaining message has enough octets for the rest of the RR fields. */
	if ((*off) + 10 > sz)
	{
		memset(RR, '\0', sizeof(*RR));
		return 1;
	}

	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);

	RR->type = ntohs(s);
	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);

	RR->class = ntohs(s);
	memcpy(&i, buf + (*off), sizeof(i));
	(*off) += sizeof(i);

	RR->ttl = ntohl(i);
	memcpy(&s, buf + (*off), sizeof(s));
	(*off) += sizeof(s);
	rdlength = ntohs(s);

	if ((*off) + rdlength > sz)
	{
		/* We got a truncated packet. 'dnscache' truncates UDPreplies at 512 octets, as per RFC 1035. */
		memset(RR, '\0', sizeof(*RR));
		return 1;
	}

	RR->rdlength = rdlength;

	switch (RR->type)
	{
	case RFC1035_TYPE_PTR:
	{
		RR->rdata = malloc(RFC1035_MAXHOSTNAMESZ);
		rdata_off = *off;
		RR->rdlength = 0;	/* Filled in by rfc1035NameUnpack */

		if (rfc1035NameUnpack(buf, sz, &rdata_off, &RR->rdlength, RR->rdata, RFC1035_MAXHOSTNAMESZ, 0))
			return 1;

		if (rdata_off > ((*off) + rdlength))
		{
			/* This probably doesn't happen for valid packets, but I want to make sure that NameUnpack doesn't go beyond the RDATA area. */
			RFC1035_UNPACK_DEBUG;
			free(RR->rdata);
			memset(RR, '\0', sizeof(*RR));
			return 1;
		}
		break;
	}
	case RFC1035_TYPE_A:
	default:
		RR->rdata = malloc(rdlength);
		memcpy(RR->rdata, buf + (*off), rdlength);
		break;
	}

	(*off) += rdlength;
	return 0;
}
/**************************************************************************************************************************/
static char *rfc1035ErronoStr(int n)
{
	if (n <= 5)
		return rfc1035_str_error_arr[n].string;
	else
		return "Unknown error";
}
/**************************************************************************************************************************/
static void rfc1035SetErrno(int n)
{
	switch (rfc1035_errno = n)
	{
	case 0:
		rfc1035_error_message = "No error condition";
		break;
	case 1:
		rfc1035_error_message = "Format Error: The name server was unable to interpret the query.";
		break;
	case 2:
		rfc1035_error_message = "Server Failure: The name server was unable to process this query.";
		break;
	case 3:
		rfc1035_error_message = "Name Error: The domain name does not exist.";
		break;
	case 4:
		rfc1035_error_message = "Not Implemented: The name server does not support the requested kind of query.";
		break;
	case 5:
		rfc1035_error_message = "Refused: The name server refuses to perform the specified operation.";
		break;
	case rfc1035_unpack_error:
		rfc1035_error_message = "The DNS reply message is corrupt or could not be safely parsed.";
		break;
	default:
		rfc1035_error_message = "Unknown Error";
		break;
	}
}
/**************************************************************************************************************************/
static void rfc1035RRDestroy(rfc1035_rr *rr, int n)
{

	if (rr == NULL || n < 0)
		return;

	while (n--)
	{
		if (rr[n].rdata)
			free(rr[n].rdata);
	}

	free(rr);
}
/**************************************************************************************************************************/
/* Unpacks a RFC1035 Query Record into 'query' from a message buffer. Updates the new message buffer offset.
 * Returns 0 (success) or 1 (error)
/**************************************************************************************************************************/
static int rfc1035QueryUnpack(const char *buf, size_t sz, int *off, rfc1035_query * query)
{
	unsigned short s;

	if (rfc1035NameUnpack(buf, sz, off, NULL, query->name, RFC1035_MAXHOSTNAMESZ, 0))
	{
		memset(query, '\0', sizeof(*query));
		return 1;
	}

	if (*off + 4 > sz)
	{
		memset(query, '\0', sizeof(*query));
		return 1;
	}

	memcpy(&s, buf + *off, 2);
	*off += 2;
	query->qtype = ntohs(s);
	memcpy(&s, buf + *off, 2);
	*off += 2;
	query->qclass = ntohs(s);

	return 0;
}
/**************************************************************************************************************************/
static void rfc1035MessageDestroy(rfc1035_message * msg)
{
	if (!msg)
		return;
	if (msg->query)
		free(msg->query);
	if (msg->answer)
		rfc1035RRDestroy(msg->answer, msg->ancount);
	free(msg);
}
/**************************************************************************************************************************/
/* Takes the contents of a DNS reply and fills in an array of resource record structures.  The records array is allocated
 * here, and should be freed by calling rfc1035RRDestroy().
 * Returns number of records unpacked, zero if DNS reply indicates zero answers, or an error number < 0.
/**************************************************************************************************************************/
static int rfc1035MessageUnpack(const char *buf, size_t sz, rfc1035_message ** answer)
{
	int off = 0;
	int i;
	int nr = 0;
	rfc1035_message *msg;
	rfc1035_rr *recs;
	rfc1035_query *querys;

	msg = calloc(1, sizeof(rfc1035_message));

	if (rfc1035HeaderUnpack(buf + off, sz - off, &off, msg))
	{
		RFC1035_UNPACK_DEBUG;
		rfc1035SetErrno(rfc1035_unpack_error);
		free(msg);
		return -rfc1035_unpack_error;
	}

	rfc1035_errno = 0;
	rfc1035_error_message = NULL;
	i = (int) msg->qdcount;

	if (i != 1)
	{
		/* This can not be an answer to our queries.. */
		RFC1035_UNPACK_DEBUG;
		rfc1035SetErrno(rfc1035_unpack_error);
		free(msg);
		return -rfc1035_unpack_error;
	}

	querys = msg->query = calloc((int) msg->qdcount, sizeof(*querys));

	for (i = 0; i < (int) msg->qdcount; i++)
	{
		if (rfc1035QueryUnpack(buf, sz, &off, &querys[i]))
		{
			RFC1035_UNPACK_DEBUG;
			rfc1035SetErrno(rfc1035_unpack_error);
			rfc1035MessageDestroy(msg);
			return -rfc1035_unpack_error;
		}
	}

	*answer = msg;

	if (msg->rcode)
	{
		RFC1035_UNPACK_DEBUG;
		rfc1035SetErrno((int) msg->rcode);
		return -rfc1035_errno;
	}

	if (msg->ancount == 0)
		return 0;

	recs = msg->answer = calloc((int) msg->ancount, sizeof(rfc1035_rr));

	for (i = 0; i < (int) msg->ancount; i++)
	{
		if (off >= sz)
		{	/* corrupt packet */
			RFC1035_UNPACK_DEBUG;
			break;
		}
		if (rfc1035RRUnpack(buf, sz, &off, &recs[i]))
		{		/* corrupt RR */
			RFC1035_UNPACK_DEBUG;
			break;
		}
		nr++;
	}

	if (nr == 0)
	{
		/* we expected to unpack some answers (ancount != 0), but didn't actually get any. */
		rfc1035MessageDestroy(msg);
		*answer = NULL;
		rfc1035SetErrno(rfc1035_unpack_error);
		return -rfc1035_unpack_error;
	}
	return nr;
}
/**************************************************************************************************************************/



