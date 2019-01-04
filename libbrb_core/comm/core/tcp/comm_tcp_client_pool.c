/*
 * comm_tcp_client_pool.c
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

static EvBaseKQObjDestroyCBH CommEvTCPClientPoolObjectDestroyCBH;

/**************************************************************************************************************************/
CommEvTCPClientPool *CommEvTCPClientPoolNew(struct _EvKQBase *kq_base, CommEvTCPClientPoolConf *tcp_clientpool_conf)
{
	CommEvTCPClientPool *tcp_clientpool;
	CommEvTCPClient *ev_tcpclient;
	int op_status;
	int i;

	tcp_clientpool = calloc(1, sizeof(CommEvTCPClientPool));

	/* Populate KQ_BASE object structure */
	tcp_clientpool->kq_obj.code					= EV_OBJ_TCP_CLIENT_POOL;
	tcp_clientpool->kq_obj.obj.ptr				= tcp_clientpool;
	tcp_clientpool->kq_obj.obj.destroy_cbh		= CommEvTCPClientPoolObjectDestroyCBH;
	tcp_clientpool->kq_obj.obj.destroy_cbdata	= NULL;

	/* Register KQ_OBJECT */
	EvKQBaseObjectRegister(kq_base, &tcp_clientpool->kq_obj);

	/* Adjust POOL MAX SIZE */
	if (tcp_clientpool_conf->cli_count_max > COMM_TCP_CLIENT_POOL_MAX)
		tcp_clientpool_conf->cli_count_max = COMM_TCP_CLIENT_POOL_MAX;

	/* Copy TCP client pool configuration to control structure */
	memcpy(&tcp_clientpool->pool_conf, tcp_clientpool_conf, sizeof(CommEvTCPClientPoolConf));

	/* Initialize MEM_SLOT for client pool */
	MemSlotBaseInit(&tcp_clientpool->client.memslot, sizeof(CommEvTCPClient), COMM_TCP_CLIENT_POOL_MAX, BRBDATA_THREAD_UNSAFE);

	for (i = 0; i < tcp_clientpool->pool_conf.cli_count_init; i++)
	{
		/* Grab a NEW TCP_CLIENT memory area from SLOT and save PARENT_POOL */
		ev_tcpclient							= MemSlotBaseSlotGrab(&tcp_clientpool->client.memslot);
		ev_tcpclient->parent_pool				= tcp_clientpool;

		/* Common client initialization (i will be client_id on pool) */
		op_status 					= CommEvTCPClientInit(kq_base, ev_tcpclient, i);

		/* Check if created socket is OK */
		if (COMM_CLIENT_INIT_OK != op_status)
		{
			tcp_clientpool->flags.has_error = 1;
			return tcp_clientpool;
		}

		/* Increment client count */
		tcp_clientpool->client.count_init++;

		KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "TCP_CLI [%d] with socket FD [%d]\n", i, ev_tcpclient->socket_fd);
		continue;
	}

	return tcp_clientpool;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolDestroy(CommEvTCPClientPool *tcp_clientpool)
{
	CommEvTCPClient *ev_tcpclient;
	int i;

	/* Sanity check */
	if (!tcp_clientpool)
		return 0;

	/* Cleanup individual TCP_CLIENTs from pool */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		ev_tcpclient = MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);
		CommEvTCPClientClean(ev_tcpclient);
		continue;
	}

	/* Clean MEM_SLOT_BASE of TCP_CLIENT pool */
	MemSlotBaseClean(&tcp_clientpool->client.memslot);
	tcp_clientpool = NULL;

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolConnect(CommEvTCPClientPool *tcp_clientpool, CommEvTCPClientConf *ev_tcpclient_conf)
{
	CommEvTCPClient *ev_tcpclient;
	int i;

	/* Copy TCP client pool configuration to control structure */
	memcpy(&tcp_clientpool->cli_conf, ev_tcpclient_conf, sizeof(CommEvTCPClientConf));

	/* Cleanup individual TCP_CLIENTs from pool */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab and CONNECT the TCP client */
		ev_tcpclient			= MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);
		ev_tcpclient->log_base = ev_tcpclient_conf->log_base;

		CommEvTCPClientConnect(ev_tcpclient, &tcp_clientpool->cli_conf);

		continue;
	}

	return i;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolDisconnect(CommEvTCPClientPool *tcp_clientpool)
{
	CommEvTCPClient *ev_tcpclient;
	int i;


	/* Cleanup individual TCP_CLIENTs from pool */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab and CONNECT the TCP client */
		ev_tcpclient			= MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);

		CommEvTCPClientDisconnect(ev_tcpclient);

		continue;
	}

	return i;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolReconnect(CommEvTCPClientPool *tcp_clientpool)
{
	CommEvTCPClient *ev_tcpclient;
	int i;

	/* Cleanup individual TCP_CLIENTs from pool */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab and CONNECT the TCP client */
		ev_tcpclient			= MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);

		CommEvTCPClientReconnect(ev_tcpclient);

		continue;
	}

	return i;
}
/**************************************************************************************************************************/
CommEvTCPClient *CommEvTCPClientPoolClientSelect(CommEvTCPClientPool *tcp_clientpool, int select_method, int select_connected)
{
	CommEvTCPClient *ev_tcpclient = NULL;

	switch(select_method)
	{
	case COMM_POOL_SELECT_ROUND_ROBIN:	ev_tcpclient = CommEvTCPClientPoolClientSelectRoundRobin(tcp_clientpool, select_connected);	break;
	case COMM_POOL_SELECT_LEAST_LOAD:	ev_tcpclient = CommEvTCPClientPoolClientSelectLeastLoad(tcp_clientpool, select_connected);	break;
	}

	return ev_tcpclient;
}
/**************************************************************************************************************************/
CommEvTCPClient *CommEvTCPClientPoolClientSelectLeastLoad(CommEvTCPClientPool *tcp_clientpool, int select_connected)
{
	CommEvTCPClient *ev_tcpclient_cur 		= NULL;
	CommEvTCPClient *ev_tcpclient_selected 	= NULL;
	int i;

//	DLinkedListNode *node;
//
//	/* Walk all initialized clients */
//	for (node = tcp_clientpool->client.memslot.list[0].head; node; node = node->next)
//	{
//		/* Grab the TCP client */
//		ev_tcpclient_cur 			= MemSlotBaseSlotData(&node->data);
//
//		KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - TCP [%d] of [%d] - QUEUE [%d] - STATE [%d]\n",
//				ev_tcpclient_cur->socket_fd, ev_tcpclient_cur->cli_id_onpool, tcp_clientpool->client.count_init, ev_tcpclient_cur->iodata.write_queue.stats.queue_sz, ev_tcpclient_cur->socket_state);
//
//		/* This client is disconnected, and we just want connected clients, ignore it */
//		if ((select_connected == COMM_CLIENT_STATE_CONNECTED) && (ev_tcpclient_cur->socket_state != COMM_CLIENT_STATE_CONNECTED))
//			continue;
//
//		/* Use enqueued bytes for writing as a load reference */
//		/* Uninitialized MIN SEEN or smaller write queue, select */
//		if (!ev_tcpclient_selected)
//			ev_tcpclient_selected	= ev_tcpclient_cur;
//		else if (ev_tcpclient_cur->iodata.write_queue.stats.queue_sz < ev_tcpclient_selected->iodata.write_queue.stats.queue_sz)
//			ev_tcpclient_selected	= ev_tcpclient_cur;
//		else if (ev_tcpclient_cur->statistics.last_write_ts < ev_tcpclient_selected->statistics.last_write_ts)
//			ev_tcpclient_selected	= ev_tcpclient_cur;
//
//		KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - TCP [%d] of [%d] - QUEUE [%d] - STATE [%d]\n",
//				ev_tcpclient_selected->socket_fd, ev_tcpclient_selected->cli_id_onpool, tcp_clientpool->client.count_init, ev_tcpclient_selected->iodata.write_queue.stats.queue_sz, ev_tcpclient_selected->socket_state);
//
//		continue;
//
//	}

	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab the TCP client */
		ev_tcpclient_cur 			= MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);

//		KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - TCP [%d] of [%d] - QUEUE [%d] - TS [%ld] - STATE [%d]\n",
//				ev_tcpclient_cur->socket_fd, ev_tcpclient_cur->cli_id_onpool, tcp_clientpool->client.count_init,
//				ev_tcpclient_cur->iodata.write_queue.stats.queue_sz, ev_tcpclient_cur->statistics.last_write_ts, ev_tcpclient_cur->socket_state);

		/* This client is disconnected, and we just want connected clients, ignore it */
		if ((select_connected == COMM_CLIENT_STATE_CONNECTED) && (ev_tcpclient_cur->socket_state != COMM_CLIENT_STATE_CONNECTED))
			continue;

		/* Use enqueued bytes for writing as a load reference */
		/* Uninitialized MIN SEEN or smaller write queue, select */
		if (!ev_tcpclient_selected)
			ev_tcpclient_selected	= ev_tcpclient_cur;
		else if (ev_tcpclient_cur->iodata.write_queue.stats.queue_sz < ev_tcpclient_selected->iodata.write_queue.stats.queue_sz)
			ev_tcpclient_selected	= ev_tcpclient_cur;
		else if (ev_tcpclient_cur->iodata.write_queue.stats.total_sz < ev_tcpclient_selected->iodata.write_queue.stats.total_sz)
			ev_tcpclient_selected	= ev_tcpclient_cur;

//		KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - TCP [%d] of [%d] - QUEUE [%d] - TS [%ld] - STATE [%d]\n",
//				ev_tcpclient_selected->socket_fd, ev_tcpclient_selected->cli_id_onpool, tcp_clientpool->client.count_init,
//				ev_tcpclient_selected->iodata.write_queue.stats.queue_sz, ev_tcpclient_cur->statistics.last_write_ts, ev_tcpclient_selected->socket_state);

		continue;
	}

	/* Found client, return */
	if (ev_tcpclient_selected)
	{
		KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Selected TCP_CLIENT ID [%d] with LOAD [%d]\n",
				ev_tcpclient_selected->cli_id_onpool, ev_tcpclient_selected->iodata.write_queue.stats.queue_sz);

		return ev_tcpclient_selected;
	}

	KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "No TCP client found - CLI_COUNT [%d]\n", tcp_clientpool->client.count_init);

	return NULL;
}
/**************************************************************************************************************************/
CommEvTCPClient *CommEvTCPClientPoolClientSelectRoundRobin(CommEvTCPClientPool *tcp_clientpool, int select_connected)
{
	CommEvTCPClient *ev_tcpclient;
	int i;

	/* Walk clients RR */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab TCP_CLIENT and point to NEXT client in RR */
		ev_tcpclient = MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, tcp_clientpool->client.rr_current);
		tcp_clientpool->client.rr_current = ((tcp_clientpool->client.rr_current + 1) % tcp_clientpool->pool_conf.cli_count_max);

		/* This client is disconnected, and we just want connected clients, ignore it */
		if ((select_connected == COMM_CLIENT_STATE_CONNECTED) && (ev_tcpclient->socket_state != COMM_CLIENT_STATE_CONNECTED))
			continue;

		KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Selected TCP_CLIENT ID [%d]\n", ev_tcpclient->cli_id_onpool);

		return ev_tcpclient;
	}

	KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "No TCP client found - CLI_COUNT [%d]\n", tcp_clientpool->client.count_init);
	return NULL;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolHasConnected(CommEvTCPClientPool *tcp_clientpool)
{
	CommEvTCPClient *ev_tcpclient;
	int i;

	/* Walk clients RR */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab TCP_CLIENT and point to NEXT client in RR */
		ev_tcpclient = MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);

		/* This client is disconnected, and we just want connected clients, ignore it */
		if (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
			return 1;

		continue;
	}

	return 0;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolEventSet(CommEvTCPClientPool *tcp_clientpool, CommEvTCPClientEventCodes ev_type, CommEvTCPClientCBH *cb_handler, void *cb_data)
{
	CommEvTCPClient *ev_tcpclient;
	int i;

	/* Walk all initialized clients */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab the TCP client */
		ev_tcpclient = MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);

		/* Set event pointing EV_TCPCLIENT as OWN CBDATA, otherwise set user supplied CBDATA */
		CommEvTCPClientEventSet(ev_tcpclient, ev_type, cb_handler, (NULL == cb_data ? ev_tcpclient : cb_data));

		continue;
	}

	KQBASE_LOG_PRINTF(tcp_clientpool->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Event [%d] set for [%d] clients on pool\n", ev_type, i);
	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolEventCancel(CommEvTCPClientPool *tcp_clientpool, CommEvTCPClientEventCodes ev_type)
{
	CommEvTCPClient *ev_tcpclient;
	int i;

	/* Walk all initialized clients */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab the TCP client */
		ev_tcpclient = MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);
		CommEvTCPClientEventCancel(ev_tcpclient, ev_type);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolEventCancelAll(CommEvTCPClientPool *tcp_clientpool)
{
	CommEvTCPClient *ev_tcpclient;
	int i;

	/* Walk all initialized clients */
	for (i = 0; i < tcp_clientpool->client.count_init; i++)
	{
		/* Grab the TCP client */
		ev_tcpclient = MemSlotBaseSlotGrabByID(&tcp_clientpool->client.memslot, i);
		CommEvTCPClientEventCancelAll(ev_tcpclient);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientPoolAIOWrite(CommEvTCPClientPool *tcp_clientpool, int select_method, char *data, long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	CommEvTCPClient *ev_tcpclient;
	int write_id;

	/* First try to select only connected clients */
	ev_tcpclient = CommEvTCPClientPoolClientSelect(tcp_clientpool, select_method, COMM_CLIENT_STATE_CONNECTED);

	if (!ev_tcpclient)
	{
		/* Select any client */
		ev_tcpclient = CommEvTCPClientPoolClientSelect(tcp_clientpool, select_method, COMM_POOL_SELECT_ANY);

		/* No client anyway, give up */
		if (!ev_tcpclient)
			return -1;
	}

	/* Schedule write */
	write_id = CommEvTCPClientAIOWrite(ev_tcpclient, data, data_sz, finish_cb, finish_cbdata);

	return write_id;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTCPClientPoolObjectDestroyCBH(void *kq_obj_ptr, void *cb_data)
{
	EvBaseKQObject *kq_obj						= kq_obj_ptr;
	CommEvTCPClientPool *ev_tcpclient_pool	= kq_obj->obj.ptr;

	KQBASE_LOG_PRINTF(ev_tcpclient_pool->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "Invoked to destroy COMM_EV_TCP_CLIENT_POOL at [%p]\n", kq_obj->obj.ptr);

	/* Destroy and clean structure */
	CommEvTCPClientPoolDestroy(ev_tcpclient_pool);

	return 1;
}
/**************************************************************************************************************************/
