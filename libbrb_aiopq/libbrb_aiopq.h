/*
 * libbrb_aiopq.h
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

#ifndef LIBBRB_AIOPQ_H_
#define LIBBRB_AIOPQ_H_
/**********************************************************************************************************************/
/* INCLUDES */
/************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>
#include <regex.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdarg.h>
#include <sys/time.h>
/************************************************************/
#if !defined(__linux__)
#include <libpq-fe.h>
#else
#include <postgresql/libpq-fe.h>
#endif
/************************************************************/
/* BrByte Framework */
#include <libbrb_core.h>
/**********************************************************************************************************************/
/* DEFINES */
/************************************************************/
#define AIOPQ_MAX_FAIL_BEFORE_DELAY 	16
#define AIOPQ_MAX_HOSTNAME				512
#define AIOPQ_MAX_DBNAME				512
#define AIOPQ_MAX_USERNAME				512
#define AIOPQ_MAX_PASSWORD				512
#define AIOPQ_MAX_FMTQUERY				65535

#define AIOPQ_DECREASE_CLI_TIMER		60000
#define AIOPQ_DECREASE_CLI_DELTA_TIME	5

#define AIOPQCLIENT_MARK_ONLINE(aiopq_client) if (!aiopq_client->flags.online) { aiopq_client->flags.online = 1; aiopq_client->aiopq_base->pqclient.count_ready++; }
#define AIOPQCLIENT_MARK_OFFLINE(aiopq_client) if (aiopq_client->flags.online) { aiopq_client->aiopq_base->pqclient.count_ready--; aiopq_client->flags.online = 0; aiopq_client->flags.busy = 0; }

#define AIOPQCLIENT_CONN_HOST			"127.0.0.1"
#define AIOPQCLIENT_CONN_DBNAME			"sistema"
#define AIOPQCLIENT_CONN_USERNAME		"sistema"
#define AIOPQCLIENT_CONN_PASSWORD		""
/************************************************************/
/* Generic cast for EvAIOPQClient *, CB_DATA, USER_DATA */
typedef void AIOPqBaseResultCB(void *, void *, void *);
/**********************************************************************************************************************/
/* ENUMS */
/************************************************************/
typedef enum
{
	AIOPQCLIENT_STATE_UNINITIALIZED,
	AIOPQCLIENT_STATE_CONNECTING,
} EvAIOPQClientStates;
/************************************************************/
typedef enum
{
	AIOPQCLIENT_SOCKSTATE_DISCONNECTED,
	AIOPQCLIENT_SOCKSTATE_CONNECTING,
	AIOPQCLIENT_SOCKSTATE_CONNECTED,
	AIOPQCLIENT_SOCKSTATE_CONN_REFUSED,
	AIOPQCLIENT_SOCKSTATE_CONN_UNKNOWN_ERROR,
	AIOPQCLIENT_SOCKSTATE_LASTITEM
} EvAIOPQClientSocketStates;
/************************************************************/
typedef enum
{
	AIOPQ_QUERYLIST_REQUEST,
	AIOPQ_QUERYLIST_REPLY,
	AIOPQ_QUERYLIST_LASTITEM
} EvAIOPQQueryListStatus;
/**********************************************************************************************************************/
/* STRUCTS */
/************************************************************/
typedef struct _EvAIOPQClient
{
	PGconn *pq_conn;
	struct _EvAIOPQBase *aiopq_base;
	struct _EvAIOPQRequest *aiopq_req;
	int pq_state;
	int cli_id;
	time_t last_grab_ts;

	struct
	{
		unsigned int online:1;
		unsigned int busy:1;
		unsigned int flush_need:1;
		unsigned int flush_failed:1;
	} flags;

} EvAIOPQClient;
/************************************************************/
typedef struct _EvAIOPQBaseConf
{
	struct
	{
		EvKQBaseLogBase *base;
		int min_level;
	} log;

	struct
	{
		int count_max;
	} pending;

	struct
	{
		int count_begin;
		int count_max;
		int count_grow;
	} pqclient;

	struct
	{
		char *host;
		char *dbname;
		char *username;
		char *password;
	} db_data;

	struct
	{
		unsigned int canceled_notify:1;
	} flags;

} EvAIOPQBaseConf;
/************************************************************/
typedef struct _EvAIOPQRequest
{
	MemBuffer *sql_query_mb;
	AIOPqBaseResultCB *cb_func;
	EvAIOPQClient *parent_cli;
	long owner_id;
	long req_id;
	void *cb_data;
	void *user_data;

	struct
	{
		unsigned int in_use:1;
		unsigned int sent:1;
		unsigned int cancelled:1;
		unsigned int cb_called:1;
	} flags;
} EvAIOPQRequest;
/************************************************************/
typedef struct _EvAIOPQBase
{
	EvKQBase *ev_base;
	MemArena *pqcli_arena;
	int health_timer_id;

	struct
	{
		EvKQBaseLogBase *base;
		int min_level;
	} log;

	struct
	{
		MemSlotBase reqslot;
		unsigned int count_max;
		unsigned int count_cur;
		unsigned int count_fail;
		unsigned int count_cancel;
		unsigned int count_cancel_notified;
		unsigned int count_success;
		unsigned long long count_total;
		int timer_id;
	} pending;

	struct
	{
		unsigned int count_begin;
		unsigned int count_max;
		unsigned int count_grow;
		unsigned int count_cur;
		unsigned int count_ready;
		unsigned int count_busy;
		int decrease_timer_id;
	} pqclient;

	struct
	{
		char host[AIOPQ_MAX_HOSTNAME];
		char dbname[AIOPQ_MAX_DBNAME];
		char username[AIOPQ_MAX_USERNAME];
		char password[AIOPQ_MAX_PASSWORD];
	} db_data;

	struct
	{
		unsigned int pqbuffer_pending:1;
		unsigned int canceled_notify:1;
	} flags;

} EvAIOPQBase;
/**********************************************************************************************************************/
/* PUBLIC */
/************************************************************/
EvAIOPQBase *EvAIOPQBaseNew(EvKQBase *ev_base, EvAIOPQBaseConf *aiopq_conf);
void EvAIOPQBaseDestroy(EvAIOPQBase *aiopq_base);
int EvAIOPQBaseToJsonMemBuffer(EvAIOPQBase *aiopq_base, MemBuffer *json_reply_mb);
int EvAIOPQSendQueryFmt(EvAIOPQBase *aiopq_base, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id, char *sql_query, ...);
EvAIOPQRequest *EvAIOPQSendReqFmt(EvAIOPQBase *aiopq_base, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id, char *sql_query, ...);
int EvAIOPQSendQuery(EvAIOPQBase *aiopq_base, char *sql_query, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id);
EvAIOPQRequest *EvAIOPQSendReq(EvAIOPQBase *aiopq_base, char *sql_query, AIOPqBaseResultCB *cb_func, void *cb_data, void *user_data, long owner_id);
int EvAIOPQQueryCancelByOwnerID(EvAIOPQBase *aiopq_base, long owner_id);
int EvAIOPQQueryCancelByReqID(EvAIOPQBase *aiopq_base, int query_id);
/**********************************************************************************************************************/
#endif /* LIBBRB_AIOPQ_H_ */
