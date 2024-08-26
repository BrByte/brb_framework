/*
 * comm_unix_client_pool.c
 *
 *  Created on: 2014-09-24
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2014 BrByte Software (Oliveira Alves & Amorim LTDA)
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

static EvBaseKQObjDestroyCBH CommEvUNIXClientPoolObjectDestroyCBH;

/**************************************************************************************************************************/
CommEvUNIXClientPool *CommEvUNIXClientPoolNew(EvKQBase *kq_base, CommEvUNIXClientPoolConf *unix_clientpool_conf)
{
	CommEvUNIXClientPool *unix_clientpool;
	CommEvUNIXClient *ev_unixclient;
	int socket_fd;
	int i;

	unix_clientpool = calloc(1, sizeof(CommEvUNIXClientPool));

	/* Populate KQ_BASE object structure */
	unix_clientpool->kq_obj.code				= EV_OBJ_UNIX_CLIENT_POOL;
	unix_clientpool->kq_obj.obj.ptr				= unix_clientpool;
	unix_clientpool->kq_obj.obj.destroy_cbh		= CommEvUNIXClientPoolObjectDestroyCBH;
	unix_clientpool->kq_obj.obj.destroy_cbdata	= NULL;

	/* Register KQ_OBJECT */
	EvKQBaseObjectRegister(kq_base, &unix_clientpool->kq_obj);

	/* Adjust POOL MAX SIZE */
	if (unix_clientpool_conf->cli_count_max > COMM_UNIX_CLIENT_POOL_MAX)
		unix_clientpool_conf->cli_count_max = COMM_UNIX_CLIENT_POOL_MAX;

	/* Copy UNIX client pool configuration to control structure */
	memcpy(&unix_clientpool->pool_conf, unix_clientpool_conf, sizeof(CommEvUNIXClientPoolConf));

	/* Initialize MEM_SLOT for client pool */
	MemSlotBaseInit(&unix_clientpool->client.memslot, sizeof(CommEvUNIXClient), COMM_UNIX_CLIENT_POOL_MAX, BRBDATA_THREAD_UNSAFE);

	for (i = 0; i < unix_clientpool->pool_conf.cli_count_init; i++)
	{
		/* Grab a NEW UNIX_CLIENT memory area from SLOT and save PARENT_POOL */
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrab(&unix_clientpool->client.memslot);
		ev_unixclient->parent_pool = unix_clientpool;

		/* Create socket and set it to non_blocking */
		socket_fd = EvKQBaseSocketUNIXNew(kq_base);

		/* Check if created socket is OK */
		if (socket_fd < 0)
		{
			unix_clientpool->flags.has_error = 1;
			return unix_clientpool;
		}

		/* Common client initialization (i will be client_id on pool) */
		CommEvUNIXClientInit(kq_base, ev_unixclient, socket_fd, i);
		unix_clientpool->client.count_init++;

		KQBASE_LOG_PRINTF(unix_clientpool->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "UNIX_CLI [%d] with socket FD [%d]\n", i, ev_unixclient->socket_fd);
		continue;
	}

	return unix_clientpool;
}
/**************************************************************************************************************************/
int CommEvUNIXClientPoolDestroy(CommEvUNIXClientPool *unix_clientpool)
{
	CommEvUNIXClient *ev_unixclient;
	int i;

	/* Sanity check */
	if (!unix_clientpool)
		return 0;

	/* Cleanup individual UNIX_CLIENTs from pool */
	for (i = 0; i < unix_clientpool->client.count_init; i++)
	{
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, i);
		CommEvUNIXClientClean(ev_unixclient);
		continue;
	}

	/* Clean MEM_SLOT_BASE of UNIX_CLIENT pool */
	MemSlotBaseClean(&unix_clientpool->client.memslot);

	return 1;
}
/**************************************************************************************************************************/
int CommEvUNIXClientPoolConnect(CommEvUNIXClientPool *unix_clientpool, CommEvUNIXClientConf *ev_unixclient_conf)
{
	CommEvUNIXClient *ev_unixclient;
	int i;

	/* Copy UNIX client pool configuration to control structure */
	memcpy(&unix_clientpool->cli_conf, ev_unixclient_conf, sizeof(CommEvUNIXClientConf));

	/* Cleanup individual UNIX_CLIENTs from pool */
	for (i = 0; i < unix_clientpool->client.count_init; i++)
	{
		/* Grab and CONNECT the UNIX client */
		ev_unixclient			= (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, i);
		ev_unixclient->log_base = ev_unixclient_conf->log_base;

		CommEvUNIXClientConnect(ev_unixclient, &unix_clientpool->cli_conf);

		continue;
	}

	return i;
}
/**************************************************************************************************************************/
CommEvUNIXClient *CommEvUNIXClientPoolClientSelect(CommEvUNIXClientPool *unix_clientpool, int select_method, int select_connected)
{
	CommEvUNIXClient *ev_unixclient = NULL;

	switch(select_method)
	{
	case COMM_UNIX_SELECT_ROUND_ROBIN:	ev_unixclient = CommEvUNIXClientPoolClientSelectRoundRobin(unix_clientpool, select_connected);	break;
	case COMM_UNIX_SELECT_LEAST_LOAD:	ev_unixclient = CommEvUNIXClientPoolClientSelectLeastLoad(unix_clientpool, select_connected);	break;
	}

	return ev_unixclient;
}
/**************************************************************************************************************************/
CommEvUNIXClient *CommEvUNIXClientPoolClientSelectLeastLoad(CommEvUNIXClientPool *unix_clientpool, int select_connected)
{
	CommEvUNIXClient *ev_unixclient;

	int write_queue_pend;
	int write_queue_ack;
	int ack_queue;
	int i;

	int load_cli_cur		= 0;
	int load_cli_id_least	= -1;
	int load_least_seen		= -1;

	/* Walk all initialized clients */
	for (i = 0; i < unix_clientpool->client.count_init; i++)
	{
		/* Grab the UNIX client */
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, i);

		/* This client is disconnected, and we just want connected clients, ignore it */
		if ((select_connected == COMM_UNIX_SELECT_CONNECTED) && (ev_unixclient->socket_state != COMM_CLIENT_STATE_CONNECTED))
			continue;

		/* Grab QUEUED request size */
		write_queue_pend	= MemSlotBaseSlotListIDSize(&ev_unixclient->iodata.write.req_mem_slot, COMM_UNIX_LIST_PENDING_WRITE);
		write_queue_ack		= MemSlotBaseSlotListIDSize(&ev_unixclient->iodata.write.req_mem_slot, COMM_UNIX_LIST_PENDING_ACK);
		ack_queue			= MemSlotBaseSlotListIDSize(&ev_unixclient->iodata.write.ack_mem_slot, COMM_UNIX_LIST_PENDING_WRITE);

		load_cli_cur		= (write_queue_pend + write_queue_ack + ack_queue);

		/* Uninitialized MIN SEEN or smaller write queue, select */
		if ((-1 == load_least_seen) || (load_cli_cur < load_least_seen))
		{
			load_least_seen		= load_cli_cur;
			load_cli_id_least	= ev_unixclient->cli_id_onpool;

			/* This client must be part of the pool */
			assert(load_cli_id_least > -1);

			continue;
		}

		continue;
	}

	/* Found client, return */
	if (load_cli_id_least > -1)
	{
		/* Grab the UNIX client with the LOWER SEEN LOAD by ID */
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, load_cli_id_least);

		KQBASE_LOG_PRINTF(unix_clientpool->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Selected UNIX_CLIENT ID [%d] with LOAD [%d]\n", ev_unixclient->cli_id_onpool, load_least_seen);
		return ev_unixclient;
	}

	KQBASE_LOG_PRINTF(unix_clientpool->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "No UNIX client found - CLI_COUNT [%d]\n", unix_clientpool->client.count_init);
	return NULL;
}
/**************************************************************************************************************************/
CommEvUNIXClient *CommEvUNIXClientPoolClientSelectRoundRobin(CommEvUNIXClientPool *unix_clientpool, int select_connected)
{
	CommEvUNIXClient *ev_unixclient;
	int i;

	/* Walk clients RR */
	for (i = 0; i < unix_clientpool->client.count_init; i++)
	{
		/* Grab UNIX_CLIENT and point to NEXT client in RR */
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, unix_clientpool->client.rr_current);
		unix_clientpool->client.rr_current = ((unix_clientpool->client.rr_current + 1) % unix_clientpool->pool_conf.cli_count_max);

		/* This client is disconnected, and we just want connected clients, ignore it */
		if ((select_connected == COMM_UNIX_SELECT_CONNECTED) && (ev_unixclient->socket_state != COMM_CLIENT_STATE_CONNECTED))
			continue;

		KQBASE_LOG_PRINTF(unix_clientpool->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Selected UNIX_CLIENT ID [%d]\n", ev_unixclient->cli_id_onpool);

		return ev_unixclient;
	}

	KQBASE_LOG_PRINTF(unix_clientpool->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "No UNIX client found - CLI_COUNT [%d]\n", unix_clientpool->client.count_init);
	return NULL;
}
/**************************************************************************************************************************/
int CommEvUNIXClientPoolHasConnected(CommEvUNIXClientPool *unix_clientpool)
{
	CommEvUNIXClient *ev_unixclient;
	int i;

	/* Walk clients RR */
	for (i = 0; i < unix_clientpool->client.count_init; i++)
	{
		/* Grab UNIX_CLIENT and point to NEXT client in RR */
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, i);

		/* This client is disconnected, and we just want connected clients, ignore it */
		if (ev_unixclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
			return 1;

		continue;
	}

	return 0;
}
/**************************************************************************************************************************/
int CommEvUNIXClientPoolEventSet(CommEvUNIXClientPool *unix_clientpool, CommEvUNIXClientEventCodes ev_type, CommEvUNIXGenericCBH *cb_handler, void *cb_data)
{
	CommEvUNIXClient *ev_unixclient;
	int i;

	/* Walk all initialized clients */
	for (i = 0; i < unix_clientpool->client.count_init; i++)
	{
		/* Grab the UNIX client */
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, i);

		/* Set event pointing EV_UNIXCLIENT as OWN CBDATA, otherwise set user supplied CBDATA */
		CommEvUNIXClientEventSet(ev_unixclient, ev_type, cb_handler, (NULL == cb_data ? ev_unixclient : cb_data));

		continue;
	}

	//KQBASE_LOG_PRINTF(unix_clientpool->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Event [%d] set for [%d] clients on pool\n", ev_type, i);
	return 1;
}
/**************************************************************************************************************************/
int CommEvUNIXClientPoolEventCancel(CommEvUNIXClientPool *unix_clientpool, CommEvUNIXClientEventCodes ev_type)
{
	CommEvUNIXClient *ev_unixclient;
	int i;

	/* Walk all initialized clients */
	for (i = 0; i < unix_clientpool->client.count_init; i++)
	{
		/* Grab the UNIX client */
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, i);
		CommEvUNIXClientEventCancel(ev_unixclient, ev_type);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int CommEvUNIXClientPoolEventCancelAll(CommEvUNIXClientPool *unix_clientpool)
{
	CommEvUNIXClient *ev_unixclient;
	int i;

	/* Walk all initialized clients */
	for (i = 0; i < unix_clientpool->client.count_init; i++)
	{
		/* Grab the UNIX client */
		ev_unixclient = (CommEvUNIXClient*)MemSlotBaseSlotGrabByID(&unix_clientpool->client.memslot, i);
		CommEvUNIXClientEventCancelAll(ev_unixclient);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int CommEvUNIXClientPoolAIOWrite(CommEvUNIXClientPool *unix_clientpool, int select_method, char *data, long data_sz, int *fd_arr, int fd_sz,
		CommEvUNIXGenericCBH *finish_cb, CommEvUNIXACKCBH *ack_cb, void *finish_cbdata, void *ack_cbdata)
{
	CommEvUNIXClient *ev_unixclient;
	int write_id;

	/* First try to select only connected clients */
	ev_unixclient 		= CommEvUNIXClientPoolClientSelect(unix_clientpool, select_method, COMM_UNIX_SELECT_CONNECTED);

	if (!ev_unixclient)
	{
		/* Select any client */
		ev_unixclient 	= CommEvUNIXClientPoolClientSelect(unix_clientpool, select_method, COMM_UNIX_SELECT_ANY);

		/* No client anyway, give up */
		if (!ev_unixclient)
			return -1;
	}

	/* Schedule write */
	write_id 			= CommEvUNIXClientAIOBrbProtoWrite(ev_unixclient, data, data_sz, fd_arr, fd_sz, finish_cb, ack_cb, finish_cbdata, ack_cbdata);

	return write_id;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvUNIXClientPoolObjectDestroyCBH(void *kq_obj_ptr, void *cb_data)
{
	EvBaseKQObject *kq_obj						= kq_obj_ptr;
	CommEvUNIXClientPool *ev_unixclient_pool	= kq_obj->obj.ptr;

	KQBASE_LOG_PRINTF(ev_unixclient_pool->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "Invoked to destroy COMM_EV_UNIX_CLIENT_POOL at [%p]\n", kq_obj->obj.ptr);

	/* Destroy and clean structure */
	CommEvUNIXClientPoolDestroy(ev_unixclient_pool);

	return 1;
}
/**************************************************************************************************************************/
