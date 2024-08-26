/*
 * libbrb_aiopq.c
 *
 *  Created on: 2013-09-08
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

#include "libbrb_aiopq.h"
/**********************************************************************************************************************/
/* Timer events */
/************************************************************/
static int EvAIOPQClientCheckConnHealthCheck(EvAIOPQClient *aiopq_client);
static EvBaseKQCBH EvAIOPQClientCheckConnHealthTimer;
static EvBaseKQCBH EvAIOPQBufferedRequestTimerTick;
static EvBaseKQCBH EvAIOPQCheckDecreasePQCliTimer;
/************************************************************/
/* AIO events */
static EvBaseKQCBH EvAIOPQClientEventClose;
static EvBaseKQCBH EvAIOPQClientEventRead;
static EvBaseKQCBH EvAIOPQClientEventWrite;
/************************************************************/
/* Core internal procedures */
static void EvAIOPQBaseClientPoolInitN(EvAIOPQBase *aiopq_base, int client_count);
static void EvAIOPQBaseClientPoolInitBegin(EvAIOPQBase *aiopq_base);
static void EvAIOPQClientConnect(EvAIOPQBase *aiopq_base, int client_id);
static int EvAIOPQClientCheckPollingStatus(EvAIOPQClient *aiopq_client);
static int EvAIOPQClientCheckConnReady(EvAIOPQClient *aiopq_client);
static int EvAIOPQClientCheckSocketConnState(EvAIOPQClient *aiopq_client);
static EvAIOPQClient *EvAIOPQClientGrabFree(EvAIOPQBase *aiopq_base, int outstanding_flag);
static void EvAIOPQSocketClose(EvKQBase *kq_base, int fd);
/************************************************************/
/* SQL query buffer control procedures */
static int EvAIOPQClientFlushBegin(EvAIOPQBase *aiopq_base, EvAIOPQClient *aiopq_client);
static int EvAIOPQClientFlushDispatch(EvAIOPQBase *aiopq_base, EvAIOPQClient *aiopq_client);
static int EvAIOPQBufferDispatch(EvAIOPQBase *aiopq_base);
static EvAIOPQRequest *EvAIOPQRequestEnqueue(EvAIOPQBase *aiopq_base, char *sql_query, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id);
static EvAIOPQRequest *EvAIOPQRequestNew(EvAIOPQBase *aiopq_base, char *sql_query, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id);
static void EvAIOPQRequestDestroy(EvAIOPQBase *aiopq_base, EvAIOPQRequest *aiopq_req);
/**********************************************************************************************************************/
EvAIOPQBase *EvAIOPQBaseNew(EvKQBase *ev_base, EvAIOPQBaseConf *aiopq_conf)
{
	EvAIOPQBase *aiopq_base;

	/* Create base outer shell */
	aiopq_base								= calloc(1, sizeof(EvAIOPQBase));

	/* Save reference event base */
	aiopq_base->ev_base 					= ev_base;
	aiopq_base->log.base					= aiopq_conf->log.base;
	aiopq_base->log.min_level				= aiopq_conf->log.min_level;

	/* Copy FLAGs */
	aiopq_base->flags.canceled_notify		= aiopq_conf->flags.canceled_notify;

	/* Fill in buffer info and client connection limit info */
	aiopq_base->pqclient.count_begin		= aiopq_conf->pqclient.count_begin;
	aiopq_base->pqclient.count_max			= aiopq_conf->pqclient.count_max;
	aiopq_base->pqclient.count_grow			= aiopq_conf->pqclient.count_grow;
	aiopq_base->pending.count_max			= aiopq_conf->pending.count_max;
	aiopq_base->pending.timer_id			= -1;

	if (!aiopq_conf->db_data.host)
		aiopq_conf->db_data.host			= AIOPQCLIENT_CONN_HOST;

	if (!aiopq_conf->db_data.dbname)
		aiopq_conf->db_data.dbname			= AIOPQCLIENT_CONN_DBNAME;

	if (!aiopq_conf->db_data.username)
		aiopq_conf->db_data.username		= AIOPQCLIENT_CONN_USERNAME;

	if (!aiopq_conf->db_data.password)
		aiopq_conf->db_data.password		= AIOPQCLIENT_CONN_PASSWORD;

	/* Fill in database info */
	strncpy((char*)&aiopq_base->db_data.host, (aiopq_conf->db_data.host ? aiopq_conf->db_data.host : AIOPQCLIENT_CONN_HOST), AIOPQ_MAX_HOSTNAME);
	strncpy((char*)&aiopq_base->db_data.dbname, (aiopq_conf->db_data.dbname ? aiopq_conf->db_data.dbname : AIOPQCLIENT_CONN_DBNAME), AIOPQ_MAX_DBNAME);
	strncpy((char*)&aiopq_base->db_data.username, (aiopq_conf->db_data.username ? aiopq_conf->db_data.username : AIOPQCLIENT_CONN_USERNAME), AIOPQ_MAX_USERNAME);
	strncpy((char*)&aiopq_base->db_data.password, (aiopq_conf->db_data.password ? aiopq_conf->db_data.password : AIOPQCLIENT_CONN_PASSWORD), AIOPQ_MAX_PASSWORD);

	/* Initialize HEALTH_CHECKER TIMER */
	aiopq_base->health_timer_id				= EvKQBaseTimerAdd(aiopq_base->ev_base, COMM_ACTION_ADD_PERSIST, 1000, EvAIOPQClientCheckConnHealthTimer, aiopq_base);

	/* Initialize HEALTH_CHECKER TIMER */
	aiopq_base->pqclient.decrease_timer_id	= -1;

	/* Initialize client arena and pending request ARENA */
	aiopq_base->pqcli_arena 				= MemArenaNew((aiopq_base->pqclient.count_begin + 1), (sizeof(EvAIOPQClient) + 1), (aiopq_base->pqclient.count_begin + 1), MEMARENA_MT_UNSAFE);
	MemSlotBaseInit(&aiopq_base->pending.reqslot, (sizeof(EvAIOPQRequest) + 1), (aiopq_base->pending.count_max + 2), BRBDATA_THREAD_UNSAFE);

	/* Fire up pq_client arena initialization */
	EvAIOPQBaseClientPoolInitBegin(aiopq_base);

	return aiopq_base;
}
/**********************************************************************************************************************/
void EvAIOPQBaseDestroy(EvAIOPQBase *aiopq_base)
{
	EvAIOPQClient *aiopq_client;
	int pq_socket;
	int i;

	/* Sanity check */
	if (!aiopq_base)
		return;

	/* Stop timers */
	if (aiopq_base->pending.timer_id > -1)
		EvKQBaseTimerCtl(aiopq_base->ev_base, aiopq_base->pending.timer_id, COMM_ACTION_DELETE);

	if (aiopq_base->health_timer_id > -1)
		EvKQBaseTimerCtl(aiopq_base->ev_base, aiopq_base->health_timer_id, COMM_ACTION_DELETE);

	/* Reset TIMER_IDs */
	aiopq_base->pending.timer_id	= -1;
	aiopq_base->health_timer_id		= -1;

	/* Gracefully disconnect all PQClients */
	for (i = 0; i < aiopq_base->pqclient.count_cur; i++)
	{
		/* Grab AIOPQ client */
		aiopq_client				= MemArenaGrabByID(aiopq_base->pqcli_arena, i);
		aiopq_client->flags.online	= 0;

		/* Grab socket from underlying database connection */
		pq_socket 					= PQsocket(aiopq_client->pq_conn);

		/* Reset poll and finish connection */
		PQresetPoll(aiopq_client->pq_conn);
		PQfinish(aiopq_client->pq_conn);

//		EvKQBaseSocketClose(aiopq_base->ev_base, pq_socket);
		EvAIOPQSocketClose(aiopq_base->ev_base, pq_socket);
		continue;
	}

	/* Destroy PQClient arena and request slot_mem */
	MemSlotBaseClean(&aiopq_base->pending.reqslot);
	MemArenaDestroy(aiopq_base->pqcli_arena);
	aiopq_base->pqcli_arena = NULL;

	/* Free outer shell */
	free(aiopq_base);

	return;
}
/**********************************************************************************************************************/
int EvAIOPQBaseToJsonMemBuffer(EvAIOPQBase *aiopq_base, MemBuffer *json_reply_mb)
{
	int op_status;

	if (!aiopq_base || !json_reply_mb)
		return -1;

	/* Pending */
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "req_cur", aiopq_base->pending.count_cur);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "req_fail", aiopq_base->pending.count_fail);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "req_cancel", aiopq_base->pending.count_cancel);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "req_cancel_replyed", aiopq_base->pending.count_cancel_notified);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "req_success", aiopq_base->pending.count_success);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "req_max", aiopq_base->pending.count_max);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_ULONGLONG(json_reply_mb, "req_total", aiopq_base->pending.count_total);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);

//	/* Arena */
//	op_status		= MemArenaToJsonMemBuffer(aiopq_base->pqcli_arena, json_reply_mb);
//
//	if (op_status == 0)
//		MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);

	/* Client */
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "cli_begin", aiopq_base->pqclient.count_begin);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "cli_max", aiopq_base->pqclient.count_max);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "cli_grow", aiopq_base->pqclient.count_grow);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "cli_cur", aiopq_base->pqclient.count_cur);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "cli_ready", aiopq_base->pqclient.count_ready);
	MEMBUFFER_JSON_ADD_COMMA(json_reply_mb);
	MEMBUFFER_JSON_ADD_UINT(json_reply_mb, "cli_busy", aiopq_base->pqclient.count_busy);

	return 0;
}
/**********************************************************************************************************************/
int EvAIOPQSendQueryFmt(EvAIOPQBase *aiopq_base, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id, char *sql_query, ...)
{
	char sql_buf[AIOPQ_MAX_FMTQUERY] 	= {0};
	char *buf_ptr 						= (char*)&sql_buf;
	va_list args;
	int op_status;
	int msg_sz;

	/* Check needed size */
	va_start(args, sql_query);
	msg_sz 			= vsnprintf(NULL, 0, sql_query, args) + 1; // size + null terminator

	/* Decide buffer we will use */
	if (msg_sz > (AIOPQ_MAX_FMTQUERY - 1))
	{
		buf_ptr 	= malloc(msg_sz);

		/* Sanitize */
		if (!buf_ptr)
			return -1;
	}

	/* Restart args */
	va_end(args);
	va_start(args, sql_query);

	/* Initialize query */
	msg_sz 			= vsnprintf(buf_ptr, msg_sz, sql_query, args);
	buf_ptr[msg_sz] = '\0';
	va_end(args);

	/* Send SQL query */
	op_status = EvAIOPQSendQuery(aiopq_base, buf_ptr, cb_func, cb_data, user_data, owner_id);

	/* Free willy */
	if (buf_ptr != (char*)&sql_buf)
		free(buf_ptr);

	return op_status;
}
/**********************************************************************************************************************/
EvAIOPQRequest *EvAIOPQSendReqFmt(EvAIOPQBase *aiopq_base, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id, char *sql_query, ...)
{
	EvAIOPQRequest *aiopq_req 			= NULL;
	char sql_buf[AIOPQ_MAX_FMTQUERY] 	= {0};
	char *buf_ptr 						= (char*)&sql_buf;
	va_list args;
	int msg_sz;

	/* Check needed size */
	va_start(args, sql_query);
	msg_sz 			= vsnprintf(NULL, 0, sql_query, args) + 1; // size + null terminator

	/* Decide buffer we will use */
	if (msg_sz > (AIOPQ_MAX_FMTQUERY - 1))
	{
		buf_ptr 	= malloc(msg_sz);

		/* Sanitize */
		if (!buf_ptr)
			return NULL;
	}

	/* Restart args */
	va_end(args);
	va_start(args, sql_query);

	/* Initialize query */
	msg_sz 			= vsnprintf(buf_ptr, msg_sz, sql_query, args);
	buf_ptr[msg_sz] = '\0';
	va_end(args);

	/* Send SQL query */
	aiopq_req 		= EvAIOPQSendReq(aiopq_base, buf_ptr, cb_func, cb_data, user_data, owner_id);

	/* Free willy */
	if (buf_ptr != (char*)&sql_buf)
		free(buf_ptr);

	return aiopq_req;
}
/**********************************************************************************************************************/
int EvAIOPQSendQuery(EvAIOPQBase *aiopq_base, char *sql_query, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id)
{
	EvAIOPQRequest *aiopq_req;

	aiopq_req 			= EvAIOPQSendReq(aiopq_base, sql_query, cb_func, cb_data, user_data, owner_id);

	return aiopq_req ? aiopq_req->req_id : -1;
}
/**********************************************************************************************************************/
EvAIOPQRequest *EvAIOPQSendReq(EvAIOPQBase *aiopq_base, char *sql_query, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id)
{
	EvAIOPQClient *aiopq_client;
	EvAIOPQRequest *aiopq_req;
	int flush_status;
	int pq_socket;
	int op_status;

	/* Sanity check */
	if (!sql_query)
		return NULL;

	/* Try to enqueue new AIOPQ_REQ */
	aiopq_req = EvAIOPQRequestEnqueue(aiopq_base, sql_query, cb_func, cb_data, user_data, owner_id);

	/* Failed to enqueue new request, STOP */
	if (!aiopq_req)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_WARNING, LOGCOLOR_RED, "FAILED - Unable to enqueue more requests CUR/MAX [%lu / %lu] - WILL DISCARD!\n",
				aiopq_base->pending.count_max, MemSlotBaseSlotListSizeAll(&aiopq_base->pending.reqslot));

		return NULL;
	}

	/* Command Queued */
	aiopq_base->pending.count_cur++;
	aiopq_base->pending.count_total++;

	/* Grab a free client from base */
	aiopq_client = EvAIOPQClientGrabFree(aiopq_base, 0);

	/* There is no free client - Dispatch timer will send this query once possible */
	if (!aiopq_client)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_YELLOW,
				"No free client to immediate send query - CUR [%d] - MAX [%d] - BUSY [%d] - READY [%d]\n",
				aiopq_base->pqclient.count_cur, aiopq_base->pqclient.count_max, aiopq_base->pqclient.count_busy, aiopq_base->pqclient.count_ready);

		/* Set timer to the next polling - Set flags we have pending requests */
		if (aiopq_base->pending.timer_id < 0)
			aiopq_base->pending.timer_id		= EvKQBaseTimerAdd(aiopq_base->ev_base, COMM_ACTION_ADD_VOLATILE, 10, EvAIOPQBufferedRequestTimerTick, aiopq_base);

		/* Mark we are holding OUTSTANSING requests to maintain sequence */
		aiopq_base->flags.pqbuffer_pending	= 1;

		return aiopq_req;
	}

	/* Make sure we are ONLINE, FREE and there is no SQL_QUERY attached to this client */
	assert(aiopq_client->flags.online);
	assert(!aiopq_client->flags.busy);
	assert(!aiopq_client->aiopq_req);

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Will send SQL_QUERY with CLI_ID [%d] - CB_FUNC [%p] - CB_DATA [%p] - U_DATA [%p]\n",
			aiopq_client->cli_id, cb_func, cb_data, user_data);

	/* Grab socket from underlying database connection and send query */
	pq_socket		= PQsocket(aiopq_client->pq_conn);
	op_status		= PQsendQuery(aiopq_client->pq_conn, sql_query);
	flush_status	= PQflush(aiopq_client->pq_conn);

	/* Something went wrong while sending this query */
	if (!op_status)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed sending QUERY, will try again later\n", pq_socket);

		/* Mark we are holding OUTSTANSING requests to maintain sequence */
		aiopq_base->flags.pqbuffer_pending	= 1;
		return aiopq_req;
	}

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - IMMEDIATE - SQL query sent with [%d] bytes - OP_STATUS [%d] - FL_STATUS [%d]\n",
			pq_socket, strlen(sql_query), op_status, flush_status);

	/* Switch AIOPQ_REQ to REPLY_LIST */
	MemSlotBaseSlotListIDSwitch(&aiopq_base->pending.reqslot, aiopq_req->req_id, AIOPQ_QUERYLIST_REPLY);
	aiopq_req->flags.sent 		= 1;

	/* Attach current AIOREQ with AIOPQ_CLIENT */
	aiopq_client->aiopq_req		= aiopq_req;
	aiopq_client->flags.busy	= 1;

	/* Increment busy count */
	aiopq_base->pqclient.count_busy++;

	/* Failed flushing query */
	if (0 != flush_status)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Failed flushing QUERY with STATUS [%d], will try again later\n",
				pq_socket, flush_status);

		/* Mark we are holding UNFLUSHED requests to maintain sequence */
		EvAIOPQClientFlushBegin(aiopq_base, aiopq_client);
		return aiopq_req;
	}

	return aiopq_req;
}
/**********************************************************************************************************************/
int EvAIOPQQueryCancelByOwnerID(EvAIOPQBase *aiopq_base, long owner_id)
{
	EvAIOPQRequest *aiopq_req;
	DLinkedListNode *node;

	MemSlotBase *req_memsl	= &aiopq_base->pending.reqslot;
	int cancel_count		= 0;

	/* Cancel all pending REQUESTs of this OWNER */
	for (node = req_memsl->list[AIOPQ_QUERYLIST_REQUEST].head; node; node = node->next)
	{
		/* Grab AIOPQ_REQ */
		aiopq_req	= MemSlotBaseSlotData(node->data);

		if (aiopq_req->flags.cb_called)
			continue;

		/* MATCH, cancel */
		if (aiopq_req->owner_id == owner_id)
		{
			aiopq_req->flags.cancelled = 1;
			cancel_count++;
		}

		continue;
	}

	/* Cancel all pending REPLIEs of this OWNER */
	for (node = req_memsl->list[AIOPQ_QUERYLIST_REPLY].head; node; node = node->next)
	{
		/* Grab AIOPQ_REQ */
		aiopq_req	= MemSlotBaseSlotData(node->data);

		if (aiopq_req->flags.cb_called)
			continue;

		/* MATCH, cancel */
		if (aiopq_req->owner_id == owner_id)
		{
			aiopq_req->flags.cancelled = 1;
			cancel_count++;
		}

		continue;
	}

	return cancel_count;
}
/**********************************************************************************************************************/
int EvAIOPQQueryCancelByReqID(EvAIOPQBase *aiopq_base, int query_id)
{
	EvAIOPQRequest *aiopq_req;

	/* Sanity check */
	if (!aiopq_base)
		return 0;

	/* Invalid QUERY_ID */
	if (query_id < 0)
		return 0;

	/* Grab AIOPQ_REQ from MEM_SLOT */
	aiopq_req = MemSlotBaseSlotGrabByID(&aiopq_base->pending.reqslot, query_id);

	assert(aiopq_req->flags.in_use);
	assert(!aiopq_req->flags.cancelled);

	/* Mark as canceled */
	aiopq_req->flags.cancelled = 1;
	return 1;
}
/**********************************************************************************************************************/
/**/
/**/
/**********************************************************************************************************************/
static void EvAIOPQBaseClientPoolInitN(EvAIOPQBase *aiopq_base, int client_count)
{
	int cli_id_start;
	int cli_id_end;
	int i;

	cli_id_start	= aiopq_base->pqclient.count_cur;
	cli_id_end		= aiopq_base->pqclient.count_cur + client_count;

	/* Check Max */
	if (cli_id_end > aiopq_base->pqclient.count_max)
		cli_id_end	= aiopq_base->pqclient.count_max;

	/* Need more clients, remove when not needed */
	if (aiopq_base->pqclient.decrease_timer_id == -1)
		aiopq_base->pqclient.decrease_timer_id	= EvKQBaseTimerAdd(aiopq_base->ev_base, COMM_ACTION_ADD_VOLATILE, AIOPQ_DECREASE_CLI_TIMER, EvAIOPQCheckDecreasePQCliTimer, aiopq_base);

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Will start PQ_Clients from ID [%d] to [%d]\n", cli_id_start, cli_id_end);

	/* Begin connection procedure for initial PQclient count */
	for (i = cli_id_start; (i < cli_id_end); (i++, aiopq_base->pqclient.count_cur++))
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Initializing new client ID [%d]\n", i);

		/* Connect client */
		EvAIOPQClientConnect(aiopq_base, i);
		continue;
	}

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FINISH - Current client count is [%d]\n", aiopq_base->pqclient.count_cur);

	return;
}
/**********************************************************************************************************************/
static void EvAIOPQBaseClientPoolInitBegin(EvAIOPQBase *aiopq_base)
{
	int i;

	/* Begin connection procedure for initial PQclient count */
	for (i = 0; i < aiopq_base->pqclient.count_begin; i++)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Initializing client ID [%d / %d]\n", aiopq_base->pqclient.count_begin, i);

		/* Connect client */
		EvAIOPQClientConnect(aiopq_base, i);
		continue;
	}

	/* Update current initialized connections */
	aiopq_base->pqclient.count_cur = aiopq_base->pqclient.count_begin;
	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Current client count is [%d]\n", aiopq_base->pqclient.count_cur);

	return;

}
/**********************************************************************************************************************/
static void EvAIOPQClientConnect(EvAIOPQBase *aiopq_base, int client_id)
{
	EvAIOPQClient *aiopq_client;
	int conn_state;
	int pq_socket;

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "MemArena at [%p] - Client ID [%d]\n", aiopq_base->pqcli_arena, client_id);

	/* Grab a new PQClient from arena */
	aiopq_client				= MemArenaGrabByID(aiopq_base->pqcli_arena, client_id);

	/* Set current state */
	aiopq_client->pq_state		= AIOPQCLIENT_STATE_CONNECTING;

	/* Save parent reference pointer and client ID on MEM_ARENA */
	aiopq_client->aiopq_base	= aiopq_base;
	aiopq_client->cli_id		= client_id;

	/* Initialize PQ_backend connection - FIXME: This is a blocking call, and should be made async */
	aiopq_client->pq_conn		= PQsetdbLogin(aiopq_base->db_data.host, NULL, NULL, NULL, aiopq_base->db_data.dbname, aiopq_base->db_data.username, aiopq_base->db_data.password);

	/* Set non blocking */
	PQsetnonblocking(aiopq_client->pq_conn, 1);

	/* Grab lower layer socket FD and initialize it */
	pq_socket					= PQsocket(aiopq_client->pq_conn);

	/* Set socket description */
	EvKQBaseFDGenericInit(aiopq_base->ev_base, pq_socket, FD_TYPE_TCP_SOCKET);

	EvKQBaseFDDescriptionSetByFD(aiopq_base->ev_base, pq_socket, "BRB_AIOPQ - Client socket [%d] - [%s] - [%s]",
		pq_socket, aiopq_base->db_data.host, aiopq_base->db_data.dbname);

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - PQInit at [%p]\n", pq_socket, aiopq_client);

	/* Do the first polling */
	conn_state					= EvAIOPQClientCheckPollingStatus(aiopq_client);

	/* Connection is ready on first try! */
	if (conn_state)
	{
		/* We now set event to receive read data (blocks for biggest data) */
		EvKQBaseSetEvent(aiopq_base->ev_base, pq_socket, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvAIOPQClientEventRead, aiopq_client);
		EvKQBaseSetEvent(aiopq_base->ev_base, pq_socket, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, EvAIOPQClientEventClose, aiopq_client);

		/* Set flags */
		AIOPQCLIENT_MARK_ONLINE(aiopq_client);
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Client ID [%d] is connected on FD [%d] and ONLINE\n", client_id, pq_socket);
	}

	return;
}
/**********************************************************************************************************************/
static int EvAIOPQClientCheckPollingStatus(EvAIOPQClient *aiopq_client)
{
	PostgresPollingStatusType pqpoll_status;

	EvAIOPQBase *aiopq_base = aiopq_client->aiopq_base;
	int pq_socket			= PQsocket(aiopq_client->pq_conn);

	/* Something went wrong with this connection, retry connect and return */
	if (!EvAIOPQClientCheckConnReady(aiopq_client))
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_WARNING, LOGCOLOR_CYAN, "FD [%d] - Lost connection with PQSQL.. begin reconnect\n", pq_socket);

		/* Retry connect because EOF is never called, i don know why */
		EvAIOPQClientEventClose(pq_socket, 0, 0, aiopq_client, NULL);

		/* Set flags */
		AIOPQCLIENT_MARK_OFFLINE(aiopq_client);

		return 0;
	}

	/* This connection is GOOD, POLL its status to see if its FREE */
	pqpoll_status = PQconnectPoll(aiopq_client->pq_conn);

	/* Check polling status */
	switch (pqpoll_status)
	{
	case PGRES_POLLING_READING:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Busy reading\n", pq_socket);
		return 0;

	case PGRES_POLLING_WRITING:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Busy writing\n", pq_socket);
		return 0;

	case PGRES_POLLING_ACTIVE:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Can call poll function immediately (called in the next try)\n", pq_socket);
		return 0;

	case PGRES_POLLING_FAILED:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Connection is failed, will try reconnect\n", pq_socket);
		return 0;

	case PGRES_POLLING_OK:
	{
		/* OK, check if connection is busy doing IO */
		if (!PQisBusy(aiopq_client->pq_conn))
		{
			/* Polling is OK and connection is not busy */
			KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Connection is EMPTY and FREE, use it\n", pq_socket);
			return 1;
		}
		/* Yes, I am busy */
		else
		{
			KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Connection is busy waiting for INPUT\n", pq_socket);
			return 0;
		}
	}
	}

	return 0;
}
/**********************************************************************************************************************/
static int EvAIOPQClientCheckConnReady(EvAIOPQClient *aiopq_client)
{
	EvAIOPQBase *aiopq_base = aiopq_client->aiopq_base;
	int pq_socket = PQsocket(aiopq_client->pq_conn);
	int pq_status = PQstatus(aiopq_client->pq_conn);

	/* Check connection status */
	switch (pq_status)
	{
	case CONNECTION_STARTED:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Connecting..\n", pq_socket);
		return 0;
	case CONNECTION_MADE:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Connected to server\n", pq_socket);
		return 0;
	case CONNECTION_AWAITING_RESPONSE:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Waiting for a response from server\n", pq_socket);
		return 0;
	case CONNECTION_AUTH_OK:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Received authentication; waiting for backend start-up to finish\n", pq_socket);
		return 0;
	case CONNECTION_SSL_STARTUP:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Negotiating SSL encryption\n", pq_socket);
		return 0;
	case CONNECTION_SETENV:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Negotiating environment-driven parameter settings\n", pq_socket);
		return 0;
	case CONNECTION_BAD:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - WAS_ONLINE [%d] - Error trying to connect\n",
				pq_socket, aiopq_client->flags.online);
		return 0;
	case CONNECTION_OK:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Connection to DB is OK\n", pq_socket);
		return 1;
	default:
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Unknown status\n", pq_socket);
		return 0;
	}

	return 0;
}
/**********************************************************************************************************************/
static int EvAIOPQClientCheckSocketConnState(EvAIOPQClient *aiopq_client)
{
	EvAIOPQBase *aiopq_base = aiopq_client->aiopq_base;
	int op_status		= -1;
	int err				= 0;
	socklen_t errlen	= sizeof(err);
	int pq_socket		= PQsocket(aiopq_client->pq_conn);

	/* Ask the kernel for this socket current state */
	op_status = getsockopt(pq_socket, SOL_SOCKET, SO_ERROR, &err, &errlen);

	if (op_status == 0)
	{
		switch (err)
		{
		case 0:
			return AIOPQCLIENT_SOCKSTATE_CONNECTED;

		case EINPROGRESS:
			/* FALL TROUGHT */
		case EWOULDBLOCK:
			/* FALL TROUGHT */
		case EALREADY:
			/* FALL TROUGHT */
		case EINTR:
			return AIOPQCLIENT_SOCKSTATE_CONNECTING;

		case ECONNREFUSED:
			return AIOPQCLIENT_SOCKSTATE_CONN_REFUSED;

		}
	}

	return AIOPQCLIENT_SOCKSTATE_CONN_UNKNOWN_ERROR;
}
/**********************************************************************************************************************/
static EvAIOPQClient *EvAIOPQClientGrabFree(EvAIOPQBase *aiopq_base, int outstanding_flag)
{
	int pq_socket;
	int i;

	EvAIOPQClient *aiopq_client	= NULL;
	int retry_count				= 0;

	/* If we have pending outstanding requests on this base, we must wait for them to go */
	if ((!outstanding_flag) && (aiopq_base->flags.pqbuffer_pending))
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN,
				"Refusing to send. There are still outstanding requests to be sent - CUR [%d] - MAX [%d] - BUSY [%d] - READY [%d]\n",
				aiopq_base->pqclient.count_cur, aiopq_base->pqclient.count_max, aiopq_base->pqclient.count_busy, aiopq_base->pqclient.count_ready);
		return NULL;
	}

	/* TAG to search a new client */
	search_again:

	/* Too many retries, give up */
	if (retry_count > 1)
		return NULL;

	/* Search for an active and free database connection */
	for (i = 0; i < aiopq_base->pqclient.count_cur; i++)
	{
		/* Grab PQClient from arena */
		aiopq_client = MemArenaGrabByID(aiopq_base->pqcli_arena, i);

		if (aiopq_client->flags.busy)
			continue;

		/* Test connecting with poll connection */
		if (!EvAIOPQClientCheckPollingStatus(aiopq_client))
			continue;

		/* Grab socket FD */
		pq_socket = PQsocket(aiopq_client->pq_conn);

		/* Not valid, move your ass */
		if (pq_socket == 0)
			continue;

		/* Update timestamp */
		aiopq_client->last_grab_ts	= aiopq_base->ev_base->stats.cur_invoke_tv.tv_sec;

		/* Return this client */
		return aiopq_client;
	}

	/* Are we allowed to span more connections? */
	if (aiopq_base->pqclient.count_cur < aiopq_base->pqclient.count_max)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "No free client to send query - Will start [%d] NEW clients\n",
				aiopq_base->pqclient.count_grow);

		/* Initialize N more clients, update retry count and then search again */
		EvAIOPQBaseClientPoolInitN(aiopq_base, aiopq_base->pqclient.count_grow);
		retry_count++;

		goto search_again;
	}

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "No free PQ_SQL connection open - CUR [%d] - MAX [%d] - BUSY [%d] - READY [%d]\n",
			aiopq_base->pqclient.count_cur, aiopq_base->pqclient.count_max, aiopq_base->pqclient.count_busy, aiopq_base->pqclient.count_ready);

	assert(aiopq_base->pqclient.count_ready >= 0);

	/* No free connection, return NULL */
	return NULL;
}
/**********************************************************************************************************************/
static int EvAIOPQClientCheckConnHealthCheck(EvAIOPQClient *aiopq_client)
{
	EvAIOPQBase *aiopq_base		= aiopq_client->aiopq_base;
	int pq_socket				= PQsocket(aiopq_client->pq_conn);
	int sock_state				= EvAIOPQClientCheckSocketConnState(aiopq_client);

	//KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Checking state for PQ_CLI ID [%d] with SOCK_STATE [%d]\n",
	//		pq_socket, aiopq_client->cli_id, sock_state);

	/* This socket is not CONNECTED, neither CONNECTING */
	if ((AIOPQCLIENT_SOCKSTATE_CONNECTED != sock_state) && (AIOPQCLIENT_SOCKSTATE_CONNECTING != sock_state))
		goto lost_conn;

	/* Something went wrong with this connection, retry connect and return */
	if (!EvAIOPQClientCheckConnReady(aiopq_client))
		goto lost_conn;
	/* Set flags */
	else
	{
		//KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - CLI_ID [%d] is ONLINE with STATE [%d]\n",
		//		pq_socket, aiopq_client->cli_id, sock_state);

		/* We now set event to receive read data (blocks for biggest data) */
		EvKQBaseSetEvent(aiopq_base->ev_base, pq_socket, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvAIOPQClientEventRead, aiopq_client);
		EvKQBaseSetEvent(aiopq_base->ev_base, pq_socket, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, EvAIOPQClientEventClose, aiopq_client);

		AIOPQCLIENT_MARK_ONLINE(aiopq_client);
	}

	return 0;

	/* Tag to lost connection */
	lost_conn:

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, ""
			"FD [%d] - Lost connection with PQSQL - WAS_ONLINE [%d] - READY [%d] - Begin reconnect\n",
			pq_socket, aiopq_client->flags.online, aiopq_base->pqclient.count_ready);

	/* Retry connect because EOF is never called, i don know why - Set flags */
	EvAIOPQClientEventClose(pq_socket, 0, 0, aiopq_client, NULL);

	return 1;
}
/**********************************************************************************************************************/
static int EvAIOPQClientCheckConnHealthTimer(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOPQClient *aiopq_client;
	int pq_socket;
	int sock_state;
	int i;

	EvAIOPQBase *aiopq_base		= cb_data;

	//KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "TIMER_ID [%d] - Checking state for [%d] PQ_CLIENTS\n",
	//		timer_id, aiopq_base->pqclient.count_cur);

	/* Walk all SQL_CLIENTs */
	for (i = 0; i < aiopq_base->pqclient.count_cur; i++)
	{
		/* Grab AIOPQ_CLIENT from arena */
		aiopq_client	= MemArenaGrabByID(aiopq_base->pqcli_arena, i);
		pq_socket		= PQsocket(aiopq_client->pq_conn);
		sock_state		= EvAIOPQClientCheckSocketConnState(aiopq_client);

		/* Check SQL_CLIENT health */
		EvAIOPQClientCheckConnHealthCheck(aiopq_client);
		continue;
	}

	return i;
}
/**********************************************************************************************************************/
static int EvAIOPQClientEventClose(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOPQClient *aiopq_client = cb_data;
	EvAIOPQBase *aiopq_base		= aiopq_client->aiopq_base;
	EvKQBase *ev_base			= aiopq_base->ev_base;
	EvAIOPQRequest *aiopq_req	= aiopq_client->aiopq_req;
	int pq_socket				= PQsocket(aiopq_client->pq_conn);

	BRB_ASSERT_FMT(ev_base, (pq_socket == fd), "FD [%d] - PQ_SOCKET [%d]\n", fd, pq_socket);

//	assert(pq_socket == fd);

	/* Set flags */
	AIOPQCLIENT_MARK_OFFLINE(aiopq_client);
//	EvKQBaseSocketClose(ev_base, fd);
	EvAIOPQSocketClose(ev_base, fd);

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_WARNING, LOGCOLOR_CYAN, "FD [%d] - Disconnected from [%s] - REQ_ID [%d]\n",
			fd, aiopq_base->db_data.host, aiopq_req ? aiopq_req->req_id : -1);

	/* Try reconnect */
	if (PQresetStart(aiopq_client->pq_conn))
	{
		/* Reset poll and finish connection */
		PQresetPoll(aiopq_client->pq_conn);
		PQfinish(aiopq_client->pq_conn);

		/* Reconnect */
		aiopq_client->pq_conn = PQsetdbLogin(aiopq_base->db_data.host, NULL, NULL, NULL, aiopq_base->db_data.dbname,
				aiopq_base->db_data.username, aiopq_base->db_data.password);

		/* Set non blocking */
		PQsetnonblocking(aiopq_client->pq_conn, 1);

		/* Grab lower layer socket FD and initialize it */
		pq_socket	= PQsocket(aiopq_client->pq_conn);

		/* Set socket description */
		EvKQBaseFDGenericInit(aiopq_base->ev_base, pq_socket, FD_TYPE_TCP_SOCKET);
		EvKQBaseFDDescriptionSetByFD(aiopq_base->ev_base, pq_socket, "BRB_AIOPQ - Client socket - [%s] - [%s]", aiopq_base->db_data.host, aiopq_base->db_data.dbname);
	}

	/* Set disconnect event -> This event never called, why? */
	EvKQBaseSetEvent(aiopq_base->ev_base, PQsocket(aiopq_client->pq_conn), COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, EvAIOPQClientEventClose, aiopq_client);
	return 0;

}
/**********************************************************************************************************************/
static int EvAIOPQClientEventRead(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvBaseKQFileDesc *kq_fd;
	PGresult *result;
	int op_status;
	char *data_ptr;
	int data_sz;

	EvAIOPQClient *aiopq_client = cb_data;
	EvAIOPQBase *aiopq_base		= aiopq_client->aiopq_base;
	EvAIOPQRequest *aiopq_req	= aiopq_client->aiopq_req;
	int pq_socket				= PQsocket(aiopq_client->pq_conn);

	/* We now set event to receive read data (blocks for biggest data) */
	EvKQBaseSetEvent(aiopq_base->ev_base, pq_socket, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvAIOPQClientEventRead, aiopq_client);

	/* If we receive a reply without a request, PGSQL is shutting down or something bad happened, recycle this connection */
	if (!aiopq_req)
	{
		data_ptr	= calloc(1, (to_read_sz + 32));
		data_sz		= read(fd, data_ptr, to_read_sz);

		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Unexpected reply from SQL backend with [%d] bytes - Read [%d] bytes\n",
				fd, to_read_sz, data_sz);

		/* Dump data for diagnostics */
		if (data_sz > 0)
			EvKQBaseLoggerHexDump(aiopq_base->log.base, LOGTYPE_CRITICAL, data_ptr, data_sz, 8, 4);

		free(data_ptr);
		return to_read_sz;
	}

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN,
			"FD [%d] - CANCELED [%d] - Read event for CLIENT_ID [%d] - OWNER_ID [%d] - with [%d] bytes\n",
			pq_socket, aiopq_req->flags.cancelled, aiopq_client->cli_id, aiopq_req->owner_id, to_read_sz);

	/* Consume input */
	if (PQconsumeInput(aiopq_client->pq_conn))
	{
		/* Check if all data is received */
		if (PQisBusy(aiopq_client->pq_conn))
		{
			KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - CLIENT_ID [%d] - Busy receiving data, reschedule - [%s]\n",
					pq_socket, aiopq_client->cli_id, MemBufferDeref(aiopq_req->sql_query_mb));

			return to_read_sz;
		}
		/* OK, data is ready to consume */
		else
		{
			KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLIENT_ID [%d] - OWNER_ID [%d] - Finished receiving reply\n",
					pq_socket, aiopq_client->cli_id, aiopq_req->owner_id);

			goto invoke_and_leave;
		}
	}
	/* Failed, notify caller */
	else
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - ERROR: %s\n",
				pq_socket, PQerrorMessage(aiopq_client->pq_conn));
		goto invoke_and_leave;
	}

	return to_read_sz;

	/* TAG to invoke CALLBACK and leave */
	invoke_and_leave:

	/* Notify upper layers and handle client back to them */
	if (aiopq_req->cb_func)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN,
				"FD [%d] - CANCELED [%d] - REQ [%d] - CLIENT_ID [%d] - OWNER_ID [%d] - PTR [%p] with CB_DATA/U_DATA [%p / %p] - QUERY [%p]\n",
				pq_socket, aiopq_req->flags.cancelled, aiopq_req->req_id, aiopq_client->cli_id, aiopq_req->owner_id, aiopq_req->cb_func,
				aiopq_req->cb_data, aiopq_req->user_data, aiopq_req->sql_query_mb);


		/* Invoke CB if its not CANCELLED */
		if ((aiopq_base->flags.canceled_notify) || (!aiopq_req->flags.cancelled))
		{
			aiopq_req->flags.cb_called		= 1;
			aiopq_req->cb_func(aiopq_client, aiopq_req->cb_data, aiopq_req->user_data);
		}
	}
	else
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - CLIENT_ID [%d] - Empty CB_FUNC\n",
				pq_socket, aiopq_client->cli_id);
	}

	/* Clear all results for this client connection */
	while ((result = PQgetResult(aiopq_client->pq_conn)))
		PQclear(result);

	/* Make sure we are BUSY */
	assert(aiopq_client->flags.busy);

	/* Clean AIOPQ_REQ context and clean busy flags */
	aiopq_client->aiopq_req				= NULL;
	aiopq_client->flags.busy			= 0;
	aiopq_client->flags.flush_need		= 0;
	aiopq_client->flags.flush_failed	= 0;

	/* Decrement general base busy count */
	aiopq_base->pqclient.count_busy--;
	aiopq_base->pending.count_cur--;

	if (aiopq_req->flags.cancelled)
		aiopq_base->pending.count_cancel_notified++;
	else
		aiopq_base->pending.count_success++;

	/* Destroy AIOPQ_REQ request */
	EvAIOPQRequestDestroy(aiopq_base, aiopq_req);

	return to_read_sz;
}
/**********************************************************************************************************************/
static int EvAIOPQClientEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOPQClient *aiopq_client = cb_data;
	EvAIOPQBase *aiopq_base		= aiopq_client->aiopq_base;
	int pq_socket				= PQsocket(aiopq_client->pq_conn);
	int flush_status			= 0;

	/* Error writing, FD is probably going down */
	if (can_write_sz < 0)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d / %d] - Write error\n", fd, pq_socket);
		return 0;

	}

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d / %d] - Write event with [%d] bytes\n", fd, pq_socket, can_write_sz);
	assert(pq_socket = fd);

	/* Begin flushing request */
	flush_status = EvAIOPQClientFlushDispatch(aiopq_base, aiopq_client);

	/* Set write event for this SQL socket */
	if (!flush_status)
		EvKQBaseSetEvent(aiopq_base->ev_base, pq_socket, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvAIOPQClientEventWrite, aiopq_client);

	return 1;
}
/**********************************************************************************************************************/
/**/
/**/
/**********************************************************************************************************************/
static int EvAIOPQBufferedRequestTimerTick(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	int pq_socket;
	int sock_state;
	int op_status;

	EvAIOPQBase *aiopq_base 		= cb_data;
	int dispatch_wants				= MemSlotBaseSlotListIDSize(&aiopq_base->pending.reqslot, AIOPQ_QUERYLIST_REQUEST);
	int dispatch_count				= 0;
	int dispatch_delay				= 0;

	/* Reset TIMER_ID */
	aiopq_base->pending.timer_id	= -1;

	/* Empty list, disable timer and bail out */
	if ((MemSlotBaseIsEmptyList(&aiopq_base->pending.reqslot, AIOPQ_QUERYLIST_REQUEST)))
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "No more events to dispatch, shutting down timer ID [%d]\n", timer_id);

		/* No more pending buffered requests */
		aiopq_base->flags.pqbuffer_pending	= 0;

		return 0;
	}

	/* Dispatch as many requests as we can */
	while (1)
	{
		/* Dispatch request */
		op_status 						= EvAIOPQBufferDispatch(aiopq_base);

		/* Failed dispatching, STOP */
		if (!op_status)
			break;

		dispatch_count++;
		continue;
	}

	/* Failed dispatching ALL requests - Increment COUNT */
	if (0 == dispatch_count)
		aiopq_base->pending.count_fail++;
	else
		aiopq_base->pending.count_fail	= 0;

	/* Calculate DISPATCH DELAY */
	dispatch_delay						= ((aiopq_base->pending.count_fail >= AIOPQ_MAX_FAIL_BEFORE_DELAY) ? 1000 : 10);

	/* Set timer to the next polling */
	if (aiopq_base->pending.timer_id < 0)
		aiopq_base->pending.timer_id	= EvKQBaseTimerAdd(aiopq_base->ev_base, COMM_ACTION_ADD_VOLATILE, dispatch_delay, EvAIOPQBufferedRequestTimerTick, aiopq_base);

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Dispatched [%d of %d] SQL_QUERIES, new TIMER_ID at [%d]\n",
			dispatch_count, dispatch_wants, aiopq_base->pending.timer_id);

	return dispatch_count;
}
/**********************************************************************************************************************/
static int EvAIOPQCheckDecreasePQCliTimer(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOPQBase *aiopq_base 				= cb_data;
	EvAIOPQClient *aiopq_client;
	EvBaseKQFileDesc *kq_fd;

	int pq_socket;
	int dispatch_delay						= AIOPQ_DECREASE_CLI_TIMER;
	long delta_ts;

	/* Reset TIMER_ID */
	aiopq_base->pqclient.decrease_timer_id	= -1;

	/* No more client to clear */
	if (aiopq_base->pqclient.count_cur <= aiopq_base->pqclient.count_begin)
		return 0;

	aiopq_client							= MemArenaGrabByID(aiopq_base->pqcli_arena, (aiopq_base->pqclient.count_cur - 1));

	if (aiopq_client->flags.busy)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "CLI is busy, try again later\n");
		goto CONTINUE;
	}

	delta_ts					= (aiopq_base->ev_base->stats.cur_invoke_tv.tv_sec - aiopq_client->last_grab_ts);

	if (delta_ts < AIOPQ_DECREASE_CLI_DELTA_TIME)
	{
		/* try again after expected time to 5 seconds */
		dispatch_delay		= ((AIOPQ_DECREASE_CLI_DELTA_TIME - delta_ts) * 1000) + 1000;

		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, aiopq_base->pqclient.count_begin, "CLI is still using, try again in [%d] ms - [%ul][%ul]\n",
			  dispatch_delay, aiopq_base->ev_base->stats.cur_invoke_tv.tv_sec, aiopq_client->last_grab_ts);

		goto CONTINUE;
	}


	/* Stop this client */
	AIOPQCLIENT_MARK_OFFLINE(aiopq_client);

	/* Grab socket from underlying database connection */
	pq_socket					= PQsocket(aiopq_client->pq_conn);

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_VERBOSE, aiopq_base->pqclient.count_begin, "Stopping client [%d] - FD [%d]\n", aiopq_client->cli_id, pq_socket);

	/* Reset poll and finish connection */
	PQresetPoll(aiopq_client->pq_conn);
	PQfinish(aiopq_client->pq_conn);

	EvAIOPQSocketClose(aiopq_base->ev_base, pq_socket);

	aiopq_base->pqclient.count_cur--;
	aiopq_client->pq_conn 		= NULL;
	aiopq_client->pq_state 		= AIOPQCLIENT_SOCKSTATE_DISCONNECTED;

	CONTINUE:

	/* Set timer to the next tick */
	if (aiopq_base->pqclient.decrease_timer_id < 0)
		aiopq_base->pqclient.decrease_timer_id	= EvKQBaseTimerAdd(aiopq_base->ev_base, COMM_ACTION_ADD_VOLATILE, dispatch_delay, EvAIOPQCheckDecreasePQCliTimer, aiopq_base);

	return 0;
}
/**********************************************************************************************************************/
static int EvAIOPQClientFlushBegin(EvAIOPQBase *aiopq_base, EvAIOPQClient *aiopq_client)
{
	int pq_socket = PQsocket(aiopq_client->pq_conn);

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Begin flushing queued SQL request\n", pq_socket);

	/* Set write event for this SQL socket */
	EvKQBaseSetEvent(aiopq_base->ev_base, pq_socket, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvAIOPQClientEventWrite, aiopq_client);

	/* Set flag to mark for FLUSH */
	aiopq_client->flags.flush_need	= 1;
	return 0;
}
/**********************************************************************************************************************/
static int EvAIOPQClientFlushDispatch(EvAIOPQBase *aiopq_base, EvAIOPQClient *aiopq_client)
{
	int flush_status;
	int pq_socket = PQsocket(aiopq_client->pq_conn);

	/* Try to flush client */
	flush_status = PQflush(aiopq_client->pq_conn);

	/* Check results */
	switch (flush_status)
	{
	/* Flush failed */
	case -1:
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed flushing QUERY with STATUS [%d], will try again later\n",
				pq_socket, flush_status);

		/* Set fail flag */
		aiopq_client->flags.flush_failed	= 1;
		return 0;
	}
	/* Flush OK */
	case 0:
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Flush query OK\n", pq_socket);

		/* Reset need flag */
		aiopq_client->flags.flush_need	= 0;
		return 1;
	}
	/* Flush need */
	case 1:
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Flush incomplete\n", pq_socket);
		return 0;
	}
	default:
		assert(0);
	}

	return 0;
}
/**********************************************************************************************************************/
static int EvAIOPQBufferDispatch(EvAIOPQBase *aiopq_base)
{
	EvAIOPQClient *aiopq_client;
	EvAIOPQRequest *aiopq_req;
	int flush_status;
	int pq_socket;
	int op_status;

	/* Point to HEAD on REQUEST LIST */
	aiopq_req = MemSlotBaseSlotPointToHead(&aiopq_base->pending.reqslot, AIOPQ_QUERYLIST_REQUEST);

	/* No more requests to dispatch, STOP */
	if (!aiopq_req)
		return 0;

	/* Try to grab a free client from base */
	aiopq_client = EvAIOPQClientGrabFree(aiopq_base, 1);

	/* No client found, we have no option other than enqueue */
	if (!aiopq_client)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "No free available client to dispatch [%p], ITEMS [%d]-[%d] return\n",
				aiopq_client, aiopq_base->pending.reqslot.list[AIOPQ_QUERYLIST_REPLY].size, aiopq_base->pending.reqslot.list[AIOPQ_QUERYLIST_REQUEST].size);

		return 0;
	}

	/* Hit canceled request, do not DISPATCH */
	if (aiopq_req->flags.cancelled)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Ignoring canceled REQ_ID [%d] - OWNER_ID [%ld]\n",
				aiopq_req->req_id, aiopq_req->owner_id);

		/* Count canceled, didn't request */
		aiopq_base->pending.count_cancel++;
		aiopq_base->pending.count_cur--;

		/* Invoke CB if its not CANCELLED */
		if (aiopq_base->flags.canceled_notify)
		{
			aiopq_client->aiopq_req		= aiopq_req;
			aiopq_req->flags.cb_called	= 1;
			aiopq_req->cb_func(aiopq_client, aiopq_req->cb_data, aiopq_req->user_data);
			aiopq_client->aiopq_req		= NULL;
		}

		/* Destroy OUT_STANDING request */
		EvAIOPQRequestDestroy(aiopq_base, aiopq_req);

		/* Returning TRUE we will be recursively called again to dispatch more requests */
		return 1;
	}

	/* Make sure we are ONLINE, FREE and there is no SQL_QUERY attached to this client */
	assert(aiopq_client->flags.online);
	assert(!aiopq_client->flags.busy);
	assert(!aiopq_client->aiopq_req);

	/* Save AIOPQ_CLIENT on AIOPQ_REQ */
	aiopq_req->parent_cli = aiopq_client;

	/* Try to dispatch request */
	pq_socket		= PQsocket(aiopq_client->pq_conn);
	op_status 		= PQsendQuery(aiopq_client->pq_conn, MemBufferDeref(aiopq_req->sql_query_mb));
	flush_status	= PQflush(aiopq_client->pq_conn);

	/* Something went wrong while sending this query */
	if (!op_status)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed writing SQL_QUERY, RE_ENQUEUE\n", pq_socket);
		return 0;
	}

	KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - SCHEDULE - SQL query sent with [%d] bytes - OP_STATUS [%d] - FL_STATUS [%d]\n",
			pq_socket, MemBufferGetSize(aiopq_req->sql_query_mb), op_status, flush_status);

	/* Query sent OK - Switch AIOPQ_REQ to REPLY_LIST */
	MemSlotBaseSlotListIDSwitch(&aiopq_base->pending.reqslot, aiopq_req->req_id, AIOPQ_QUERYLIST_REPLY);
	aiopq_req->flags.sent 		= 1;

	/* Attach current AIOREQ with AIOPQ_CLIENT */
	aiopq_client->aiopq_req		= aiopq_req;
	aiopq_client->flags.busy	= 1;

	/* Increment busy count */
	aiopq_base->pqclient.count_busy++;

	/* Failed flushing query */
	if (0 != flush_status)
	{
		KQBASE_LOG_PRINTF(aiopq_base->log.base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed flushing QUERY with STATUS [%d], will try again later\n",
				pq_socket, flush_status);

		/* Begin flushing */
		EvAIOPQClientFlushBegin(aiopq_base, aiopq_client);
		return 1;
	}

	/* Successfully sent query */
	return 1;
}
/**********************************************************************************************************************/
static EvAIOPQRequest *EvAIOPQRequestEnqueue(EvAIOPQBase *aiopq_base, char *sql_query, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id)
{
	EvAIOPQRequest *aiopq_req;

	/* Cannot enqueue anymore */
	if (MemSlotBaseSlotListSizeAll(&aiopq_base->pending.reqslot) >= aiopq_base->pending.count_max)
		return NULL;

	/* Create a new outstanding request */
	aiopq_req = EvAIOPQRequestNew(aiopq_base, sql_query, cb_func, cb_data, user_data, owner_id);

	/* Failed to enqueue new request, bail out */
	if (!aiopq_req)
		return NULL;

	/* Enqueued! */
	return aiopq_req;
}
/**********************************************************************************************************************/
static EvAIOPQRequest *EvAIOPQRequestNew(EvAIOPQBase *aiopq_base, char *sql_query, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id)
{
	EvAIOPQRequest *aiopq_req;

	/* Grab a new AIOPQ_REQ from our MEM_SLOT */
	aiopq_req			= MemSlotBaseSlotGrab(&aiopq_base->pending.reqslot);

	/* Failed creating a new AIO_REQ, bail out */
	if (!aiopq_req)
		return NULL;

	assert(!aiopq_req->flags.in_use);
	memset(aiopq_req, 0, sizeof(EvAIOPQRequest));

	/* Save this REQ_ID and create a new buffer to hold this outstanding request */
	aiopq_req->sql_query_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 128);
	aiopq_req->req_id		= MemSlotBaseSlotGetID((char*)aiopq_req);
	MemBufferAdd(aiopq_req->sql_query_mb, sql_query, strlen(sql_query));

	/* Fill in context data */
	aiopq_req->cb_func			= cb_func;
	aiopq_req->cb_data			= cb_data;
	aiopq_req->user_data		= user_data;
	aiopq_req->owner_id			= owner_id;
	aiopq_req->flags.in_use		= 1;
	aiopq_req->flags.sent		= 0;
	aiopq_req->flags.cancelled	= 0;

	return aiopq_req;
}
/**********************************************************************************************************************/
static void EvAIOPQRequestDestroy(EvAIOPQBase *aiopq_base, EvAIOPQRequest *aiopq_req)
{
	/* Sanity check */
	if (!aiopq_req)
		return;

	/* Destroy sql_query and detach it from AIOPQ_REQ */
	MemBufferDestroy(aiopq_req->sql_query_mb);
	aiopq_req->sql_query_mb 	= NULL;
	aiopq_req->flags.in_use		= 0;

	/* Free slot */
	MemSlotBaseSlotFree(&aiopq_base->pending.reqslot, (char*)aiopq_req);
	return;
}
/**********************************************************************************************************************/
static void EvAIOPQSocketClose(EvKQBase *kq_base, int fd)
{
//	return EvKQBaseSocketClose(kq_base, fd);

	EvBaseKQFileDesc *kq_fd;
	int op_status;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return;

	/* Grab FD from reference table - Set flags as CLOSING */
	kq_fd					= EvKQBaseFDGrabFromArena(kq_base, fd);
	kq_fd->flags.closing	= 1;

	/* Actually close it */
	EvKQBaseFDCleanupByKQFD(kq_base, kq_fd);

	return;
}
/**********************************************************************************************************************/
