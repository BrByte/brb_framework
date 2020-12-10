/*
 * comm_unix_server.c
 *
 *  Created on: 2014-07-20
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

static EvBaseKQCBH CommEvUNIXClientEventConnect;
static EvBaseKQCBH CommEvUNIXClientEventRawRead;
static EvBaseKQCBH CommEvUNIXClientEventBrbProtoRead;
static EvBaseKQCBH CommEvUNIXClientEventRawWrite;
static EvBaseKQCBH CommEvUNIXClientEventBrbProtoWriteACK;
static EvBaseKQCBH CommEvUNIXClientEventBrbProtoWrite;
static EvBaseKQCBH CommEvUNIXClientEventEOF;

/* Timer events */
static EvBaseKQCBH CommEvUNIXClientRatesCalculateTimer;
static EvBaseKQCBH CommEvUNIXClientReconnectTimer;
static EvBaseKQCBH CommEvUNIXClientTimerConnectTimeout;

static int CommEvUNIXClientCheckState(int socket_fd);
static void CommEvUNIXClientInternalDisconnect(CommEvUNIXClient *ev_unixclient);
static int CommEvUNIXClientAsyncConnect(CommEvUNIXClient *ev_unixclient);
static void CommEvUNIXClientEventDispatchInternal(CommEvUNIXClient *ev_unixclient, int data_sz, int thrd_id, int ev_type);
static int CommEvUNIXClientTimersCancelAll(CommEvUNIXClient *ev_unixclient);
static int CommEvUNIXClientObjectDestroyCBH(void *kq_obj_ptr, void *cb_data);

/**************************************************************************************************************************/
CommEvUNIXClient *CommEvUNIXClientNew(EvKQBase *kq_base)
{
	CommEvUNIXClient *ev_unixclient;
	int socket_fd;

	/* Create socket and set it to non_blocking */
	socket_fd = EvKQBaseSocketUNIXNew(kq_base);

	/* Check if created socket is ok */
	if (socket_fd < 0)
		return COMM_UNIX_CLIENT_FAILURE_SOCKET;

	ev_unixclient = calloc(1, sizeof(CommEvUNIXClient));

	/* Common client initialization - (pool_id = -1 means we are not part of a pool) */
	CommEvUNIXClientInit(kq_base, ev_unixclient, socket_fd, -1);

	return ev_unixclient;
}
/**************************************************************************************************************************/
int CommEvUNIXClientInit(EvKQBase *kq_base, CommEvUNIXClient *ev_unixclient, int socket_fd, int cli_id_onpool)
{
	ev_unixclient->kq_base						= kq_base;

	/* Populate KQ_BASE object structure */
	if (cli_id_onpool == -1)
	{
		ev_unixclient->kq_obj.code					= EV_OBJ_UNIX_CLIENT;
		ev_unixclient->kq_obj.obj.ptr				= ev_unixclient;
		ev_unixclient->kq_obj.obj.destroy_cbh		= CommEvUNIXClientObjectDestroyCBH;
		ev_unixclient->kq_obj.obj.destroy_cbdata	= NULL;
	}

	/* Register KQ_OBJECT */
	EvKQBaseObjectRegister(kq_base, &ev_unixclient->kq_obj);

	/* Set default READ METHOD and READ PROTO, can be override by CONNECT function */
	ev_unixclient->socket_state					= COMM_UNIX_CLIENT_STATE_DISCONNECTED;
	ev_unixclient->socket_fd					= socket_fd;
	ev_unixclient->cli_id_onpool				= cli_id_onpool;

	/* Initialize all timers */
	ev_unixclient->timers.reconnect_after_close_id		= -1;
	ev_unixclient->timers.reconnect_after_timeout_id	= -1;
	ev_unixclient->timers.reconnect_on_fail_id			= -1;
	ev_unixclient->timers.calculate_datarate_id			= -1;

	/* Set it to non_blocking and save it into newly allocated client */
	EvKQBaseSocketSetNonBlock(kq_base, ev_unixclient->socket_fd);

	/* Set description */
	EvKQBaseFDDescriptionSetByFD(kq_base, socket_fd, "BRB_EV_COMM - UNIX client");

	return 1;
}
/**************************************************************************************************************************/
void CommEvUNIXClientClean(CommEvUNIXClient *ev_unixclient)
{
	/* Sanity check */
	if (!ev_unixclient)
		return;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Destroying at [%p]\n", ev_unixclient->socket_fd, ev_unixclient);

	/* Cancel any possible timer or events */
	CommEvUNIXClientTimersCancelAll(ev_unixclient);
	CommEvUNIXClientEventCancelAll(ev_unixclient);

	/* Destroy read and write buffers */
	CommEvUNIXIODataDestroy(&ev_unixclient->iodata);

	/* Close the socket and cancel any pending events */
	EvKQBaseSocketClose(ev_unixclient->kq_base, ev_unixclient->socket_fd);
	ev_unixclient->socket_fd = -1;

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXClientDestroy(CommEvUNIXClient *ev_unixclient)
{
	/* Sanity check */
	if (!ev_unixclient)
		return;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Destroying at [%p]\n", ev_unixclient->socket_fd, ev_unixclient);

	/* Unregister object */
	EvKQBaseObjectUnregister(&ev_unixclient->kq_obj);

	/* Cancel any possible timer or events */
	CommEvUNIXClientTimersCancelAll(ev_unixclient);
	CommEvUNIXClientEventCancelAll(ev_unixclient);

	/* Destroy read and write buffers */
	CommEvUNIXIODataDestroy(&ev_unixclient->iodata);

	/* Close the socket and cancel any pending events */
	EvKQBaseSocketClose(ev_unixclient->kq_base, ev_unixclient->socket_fd);
	ev_unixclient->socket_fd = -1;

	/* Free WILLY */
	free(ev_unixclient);

	return;
}
/**************************************************************************************************************************/
int CommEvUNIXClientConnect(CommEvUNIXClient *ev_unixclient, CommEvUNIXClientConf *ev_unixclient_conf)
{
	int op_status = 0;

	/* Sanity check */
	if (!ev_unixclient)
		return 0;

	/* No path, fail */
	if (!ev_unixclient_conf->server_path)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Failed to connect - Empty PATH\n", ev_unixclient->socket_fd);
		return 0;
	}

	/* Load LOG base */
	if (ev_unixclient_conf->log_base)
		ev_unixclient->log_base								= ev_unixclient_conf->log_base;

	/* Load flags */
	ev_unixclient->flags.reconnect_on_timeout				= ev_unixclient_conf->flags.reconnect_on_timeout;
	ev_unixclient->flags.reconnect_on_close					= ev_unixclient_conf->flags.reconnect_on_close;
	ev_unixclient->flags.reconnect_on_fail					= ev_unixclient_conf->flags.reconnect_on_fail;
	ev_unixclient->flags.destroy_after_connect_fail			= ev_unixclient_conf->flags.destroy_after_connect_fail;
	ev_unixclient->flags.destroy_after_close				= ev_unixclient_conf->flags.destroy_after_close;
	ev_unixclient->flags.calculate_datarate					= ev_unixclient_conf->flags.calculate_datarate;
	ev_unixclient->flags.autoclose_fd_on_ack				= ev_unixclient_conf->flags.autoclose_fd_on_ack;
	ev_unixclient->flags.no_brb_proto						= ev_unixclient_conf->flags.no_brb_proto;

	/* Load timeout information */
	ev_unixclient->timeout.connect_ms						= ev_unixclient_conf->timeout.connect_ms;
	ev_unixclient->timeout.read_ms							= ev_unixclient_conf->timeout.read_ms;
	ev_unixclient->timeout.write_ms							= ev_unixclient_conf->timeout.write_ms;

	/* Load retry timers information */
	ev_unixclient->retry_times.reconnect_after_timeout_ms	= ev_unixclient_conf->retry_times.reconnect_after_timeout_ms;
	ev_unixclient->retry_times.reconnect_after_close_ms		= ev_unixclient_conf->retry_times.reconnect_after_close_ms;
	ev_unixclient->retry_times.reconnect_on_fail_ms			= ev_unixclient_conf->retry_times.reconnect_on_fail_ms;

	/* Copy server path to connect */
	strncpy ((char*)&ev_unixclient->server_path, ev_unixclient_conf->server_path, sizeof(ev_unixclient->server_path));

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Begin ASYNC to UNIX path [%s]\n",
			ev_unixclient->socket_fd, ev_unixclient->server_path);

	/* Begin ASYNC connect */
	CommEvUNIXClientAsyncConnect(ev_unixclient);

	return 1;
}
/**************************************************************************************************************************/
void CommEvUNIXClientResetFD(CommEvUNIXClient *ev_unixclient)
{
	int old_socketfd;

	/* Sanity check */
	if (!ev_unixclient)
		return;

	if (ev_unixclient->socket_state == COMM_UNIX_CLIENT_STATE_CONNECTED)
		return;

	/* Save a copy of old socket_fd */
	old_socketfd = ev_unixclient->socket_fd;

	/* Create a new socket */
	ev_unixclient->socket_fd = EvKQBaseSocketUNIXNew(ev_unixclient->kq_base);

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Reset to NEW_FD [%d]\n", old_socketfd, ev_unixclient->socket_fd);

	/* Set it to non_blocking and save it into newly allocated client */
	EvKQBaseSocketSetNonBlock(ev_unixclient->kq_base, ev_unixclient->socket_fd);

	/* Will close socket and cancel any pending events of old_socketfd */
	if (old_socketfd > 0)
		EvKQBaseSocketClose(ev_unixclient->kq_base, old_socketfd);

	return;
}
/**************************************************************************************************************************/
int CommEvUNIXClientReconnect(CommEvUNIXClient *ev_unixclient)
{
	int op_status = 0;

	/* Sanity check */
	if (!ev_unixclient)
		return 0;

	if (ev_unixclient->socket_state == COMM_UNIX_CLIENT_STATE_CONNECTED)
		return 0;

	/* Reset this client SOCKET_FD */
	CommEvUNIXClientResetFD(ev_unixclient);

	if (0 == ev_unixclient->socket_fd)
	{
		/* Schedule RECONNECT timer */
		ev_unixclient->timers.reconnect_on_fail_id =	EvKQBaseTimerAdd(ev_unixclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_unixclient->retry_times.reconnect_on_fail_ms > 0) ? ev_unixclient->retry_times.reconnect_on_fail_ms : COMM_UNIX_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
				CommEvUNIXClientReconnectTimer, ev_unixclient);

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - ZERO_FD - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
				ev_unixclient->socket_fd, ev_unixclient->timers.reconnect_on_fail_id);

		return 0;
	}

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Begin ASYNC connect\n", ev_unixclient->socket_fd);

	/* Begin ASYNC connect */
	op_status = CommEvUNIXClientAsyncConnect(ev_unixclient);

	return op_status;
}
/**************************************************************************************************************************/
void CommEvUNIXClientDisconnectRequest(CommEvUNIXClient *ev_unixclient)
{
	/* Mark flags to close as soon as we finish writing all */
	ev_unixclient->flags.close_request = 1;

	if (MemSlotBaseIsEmpty(&ev_unixclient->iodata.write.req_mem_slot))
		CommEvUNIXClientDisconnect(ev_unixclient);

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXClientDisconnect(CommEvUNIXClient *ev_unixclient)
{
	EvKQBase *kq_base	= ev_unixclient->kq_base;
	int fd				= ev_unixclient->socket_fd;

	/* Destroy IO data buffers and cancel any pending event */
	CommEvUNIXIODataDestroy(&ev_unixclient->iodata);
	CommEvUNIXClientEventCancelAll(ev_unixclient);
	CommEvUNIXClientTimersCancelAll(ev_unixclient);

	/* Close socket and set to -1 */
	EvKQBaseSocketClose(ev_unixclient->kq_base, fd);

	/* Set new state */
	ev_unixclient->socket_state	= COMM_UNIX_CLIENT_STATE_DISCONNECTED;
	ev_unixclient->socket_fd	= -1;

	return;
}
/**************************************************************************************************************************/
int CommEvUNIXClientEventIsSet(CommEvUNIXClient *ev_unixclient, CommEvUNIXClientEventCodes ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_UNIX_CLIENT_EVENT_LASTITEM)
		return 0;

	if (ev_unixclient->events[ev_type].cb_handler_ptr && ev_unixclient->events[ev_type].flags.enabled)
		return 1;

	return 0;
}
/**************************************************************************************************************************/
void CommEvUNIXClientEventSet(CommEvUNIXClient *ev_unixclient, CommEvUNIXClientEventCodes ev_type, CommEvUNIXGenericCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= COMM_UNIX_CLIENT_EVENT_LASTITEM)
		return;

	/* Set event */
	ev_unixclient->events[ev_type].cb_handler_ptr	= cb_handler;
	ev_unixclient->events[ev_type].cb_data_ptr		= cb_data;

	/* Mark enabled */
	ev_unixclient->events[ev_type].flags.enabled		= 1;

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXClientEventCancel(CommEvUNIXClient *ev_unixclient, CommEvUNIXClientEventCodes ev_type)
{
	/* Set event */
	ev_unixclient->events[ev_type].cb_handler_ptr		= NULL;
	ev_unixclient->events[ev_type].cb_data_ptr			= NULL;

	/* Mark disabled */
	ev_unixclient->events[ev_type].flags.enabled			= 0;

	/* Update kqueue_event */
	switch (ev_type)
	{
	case COMM_UNIX_CLIENT_EVENT_READ:
		EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);
		break;
	}

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXClientEventCancelAll(CommEvUNIXClient *ev_unixclient)
{
	int i;

	/* Cancel all possible events */
	for (i = 0; i < COMM_UNIX_CLIENT_EVENT_LASTITEM; i++)
	{
		CommEvUNIXClientEventCancel(ev_unixclient, i);
	}

	return;
}
/**************************************************************************************************************************/
int CommEvUNIXClientAIOBrbProtoACK(CommEvUNIXClient *ev_unixclient, int req_id)
{
	CommEvUNIXACKReply *ack_reply;
	int op_status;
	int ret_req_id;

	/* INIT IODATA and mark WRITE_QUEUE as initialized */
	CommEvUNIXIODataInit(&ev_unixclient->iodata, ev_unixclient, ev_unixclient->socket_fd, COMM_UNIX_MAX_WRITE_REQ, ev_unixclient->kq_base->flags.mt_engine);

	/* Grab a write request slot */
	ack_reply	= MemSlotBaseSlotGrab(&ev_unixclient->iodata.write.ack_mem_slot);

	/* Cannot enqueue anymore */
	if (!ack_reply)
		return -1;

	assert(!ack_reply->flags.in_use);
	memset(ack_reply, 0, sizeof(CommEvUNIXACKReply));

	/* Populate ACK reply */
	ack_reply->req_id = req_id;
	ret_req_id			= MemSlotBaseSlotGetID(ack_reply);

	/* Already pending, enqueue */
	if (ev_unixclient->flags.pending_write_request)
		goto enqueue_write;

	/* Try to write ASAP */
	op_status = CommEvUNIXIOReplyACK(ack_reply, ev_unixclient->socket_fd);

	/* Success writing */
	if (op_status > 0)
	{
		assert(op_status >= sizeof(CommEvUNIXControlData));
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - Immediate write - [%d] bytes \n", ev_unixclient->socket_fd, ack_reply->req_id, op_status);

		/* Release write slot */
		MemSlotBaseSlotFree(&ev_unixclient->iodata.write.ack_mem_slot, ack_reply);
	}
	/* Failed to write on this IO loop, mark as in use */
	else
	{
		/* Tag to schedule request */
		enqueue_write:

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Scheduled write\n", ev_unixclient->socket_fd, ack_reply->req_id);

		EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);
		ack_reply->flags.in_use		= 1;
		ack_reply->tx_retry_count++;

		/* Mark we have pending writes */
		ev_unixclient->flags.pending_write_ack = 1;

	}

	return ret_req_id;
}
/**************************************************************************************************************************/
int CommEvUNIXClientAIORawWriteStr(CommEvUNIXClient *ev_unixclient, char *data, CommEvUNIXGenericCBH *finish_cb, void *finish_cbdata)
{
	long data_sz = strlen(data);
	return (CommEvUNIXClientAIORawWrite(ev_unixclient, data, data_sz, finish_cb, finish_cbdata));
}
/**************************************************************************************************************************/
int CommEvUNIXClientAIORawWrite(CommEvUNIXClient *ev_unixclient, char *data, long data_sz, CommEvUNIXGenericCBH *finish_cb, void *finish_cbdata)
{
	CommEvUNIXWriteRequest *write_req;
	int write_buffer_sz;
	int op_status;
	int write_delta;
	int ret_reqid;
	int i;

	/* Close request */
	if (ev_unixclient->flags.close_request)
		return -1;

	/* INIT IODATA if not already initialized */
	CommEvUNIXIODataInit(&ev_unixclient->iodata, ev_unixclient, ev_unixclient->socket_fd, COMM_UNIX_MAX_WRITE_REQ, ev_unixclient->kq_base->flags.mt_engine);

	/* Grab a write request slot and adjust FD_SZ to INTEGER size boundary */
	write_req	= MemSlotBaseSlotGrab(&ev_unixclient->iodata.write.req_mem_slot);

	/* Cannot enqueue anymore */
	if (!write_req)
		return -1;

	assert(!write_req->flags.in_use);
	memset(write_req, 0, sizeof(CommEvUNIXWriteRequest));

	/* Fill write request */
	write_req->kq_base					= ev_unixclient->kq_base;
	write_req->data.ptr					= data;
	write_req->data.size				= data_sz;
	write_req->data.remain				= data_sz;
	write_req->data.offset 				= 0;
	write_req->finish_cb				= finish_cb;
	write_req->finish_cbdata			= finish_cbdata;
	write_req->parent_ptr				= ev_unixclient;
	write_req->parent_type				= COMM_UNIX_PARENT_UNIX_CLIENT;
	write_req->req_id					= MemSlotBaseSlotGetID((char*)write_req);
	write_req->flags.in_use				= 1;

	/* If there is ENQUEUED data, schedule WRITE event and LEAVE, as we need to PRESERVE WRITE ORDER */
	if (ev_unixclient->flags.pending_write_request)
		goto enqueue_write;

	/* Try to write on this very same IO LOOP */
	op_status = CommEvUNIXClientEventRawWrite(ev_unixclient->socket_fd, 8092, -1, ev_unixclient, ev_unixclient->kq_base);

	/* Return amount of write, up layer can reschedule */
	return op_status;

	/* Tag to schedule request */
	enqueue_write:

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Scheduled write - [%d] bytes - REMAIN [%d]\n",
			ev_unixclient->socket_fd, write_req->req_id, write_req->data.size, write_req->data.remain);

	EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventRawWrite, ev_unixclient);

	/* Touch counters */
	ev_unixclient->counters.req_scheduled++;
	ev_unixclient->flags.pending_write_request = 1;

	/* Set FLAGs */
	write_req->flags.wrote				= 0;
	write_req->tx_retry_count++;

	return write_req->req_id;
}
/**************************************************************************************************************************/
int CommEvUNIXClientAIOBrbProtoWrite(CommEvUNIXClient *ev_unixclient, char *data, long data_sz, int *fd_arr, int fd_sz, CommEvUNIXGenericCBH *finish_cb,
		CommEvUNIXACKCBH *ack_cb, void *finish_cbdata, void *ack_cbdata)
{
	CommEvUNIXWriteRequest *write_req;
	int write_buffer_sz;
	int op_status;
	int write_delta;
	int ret_reqid;
	int i;

	/* PAYLOAD too big, bail out */
	if (data_sz > COMM_UNIX_MAX_WRITE_SIZE)
		return -1;

	/* Too many FDs */
	if (fd_sz > COMM_UNIX_MAX_FDARR_SZ)
		return -1;

	/* Close request */
	if (ev_unixclient->flags.close_request)
		return -1;

	/* XXX TODO: finish this - Cannot enqueue anymore */
	//if (!COMM_UNIX_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
	//	return 0;

	/* INIT IODATA if not already initialized */
	CommEvUNIXIODataInit(&ev_unixclient->iodata, ev_unixclient, ev_unixclient->socket_fd, COMM_UNIX_MAX_WRITE_REQ, ev_unixclient->kq_base->flags.mt_engine);

	/* Grab a write request slot and adjust FD_SZ to INTEGER size boundary */
	write_req	= MemSlotBaseSlotGrab(&ev_unixclient->iodata.write.req_mem_slot);

	/* Cannot enqueue anymore */
	if (!write_req)
		return -1;

	assert(!write_req->flags.in_use);
	memset(write_req, 0, sizeof(CommEvUNIXWriteRequest));

	/* Fill write request */
	write_req->kq_base					= ev_unixclient->kq_base;
	write_req->data.ptr					= data;
	write_req->data.size				= data_sz;
	write_req->data.remain				= data_sz;
	write_req->data.offset 				= 0;
	write_req->finish_cb				= finish_cb;
	write_req->ack_cb					= ack_cb;
	write_req->finish_cbdata			= finish_cbdata;
	write_req->ack_cbdata				= ack_cbdata;
	write_req->parent_ptr				= ev_unixclient;
	write_req->parent_type				= COMM_UNIX_PARENT_UNIX_CLIENT;
	write_req->req_id					= MemSlotBaseSlotGetID((char*)write_req);
	write_req->flags.autoclose_on_ack	= ev_unixclient->flags.autoclose_fd_on_ack;
	ret_reqid							= write_req->req_id;

	/* Set ctrl_flag as REQUEST, and set WANT_ACK if we have a CALLBACK defined */
	EBIT_SET(write_req->ctrl_flags, COMM_UNIX_CONTROL_FLAGS_REQUEST);

	/* Either if we have ACK_CB or FLAGs set to AUTODESTROY on ACK, we will want ACK from PEER_SIDE */
	if (write_req->ack_cb || write_req->flags.autoclose_on_ack)
		EBIT_SET(write_req->ctrl_flags, COMM_UNIX_CONTROL_FLAGS_WANT_ACK);

	/* Load up FDs */
	for (i = 0; i < fd_sz; i++)
	{
		assert(fd_arr[i] > -1);
		write_req->fd_arr.data[i] = fd_arr[i];
		continue;
	}

	/* Set array size */
	write_req->fd_arr.sz = fd_sz;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - REQ_ID [%d] - Will send [%d] FDs - [%d] bytes\n",
			ev_unixclient->socket_fd, write_req->req_id, fd_sz, write_req->data.size);

	/* Already pending, enqueue */
	if ((ev_unixclient->flags.pending_write_request) || (ev_unixclient->flags.pending_write_ack))
		goto enqueue_write;

	/* Write buffer too small, enqueue */
	if (EvKQBaseSocketBufferWriteSizeGet(ev_unixclient->kq_base, ev_unixclient->socket_fd) <
			(sizeof(CommEvUNIXControlData) + (COMM_UNIX_MAX_FDARR_SZ * sizeof(int))) + write_req->data.size + 64)
		goto enqueue_write;

	/* Try to write ASAP */
	op_status = CommEvUNIXIOWrite(ev_unixclient->kq_base, write_req, ev_unixclient->socket_fd);

	/* Success writing */
	if (op_status > 0)
	{
		write_delta = (op_status - write_req->data.remain);

		/* Incomplete write, reschedule */
		if (write_req->data.remain > 0)
		{
			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Incomplete - written [%d] - data_sz [%d] - delta [%d] - remain [%d]\n",
					ev_unixclient->socket_fd, write_req->req_id, op_status, write_req->data.size, write_delta, write_req->data.remain);
			goto enqueue_write;
		}

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - Immediate write - [%d] bytes \n",
				ev_unixclient->socket_fd, write_req->req_id, op_status);

		assert(0 == write_req->data.remain);

		/* Invoke finish CB */
		if (finish_cb)
			finish_cb(ev_unixclient->socket_fd, op_status, -1, write_req, finish_cbdata);

		/* If we are not waiting for an ACK reply, release this slot right now. Otherwise, keep it until peer-side ACKs */
		if ((!ack_cb) && (!write_req->flags.autoclose_on_ack))
		{
			/* Release SLOT and touch counters */
			MemSlotBaseSlotFree(&ev_unixclient->iodata.write.req_mem_slot, write_req);
			ev_unixclient->counters.req_sent_no_ack++;
			goto leave;
		}
		/* We will keep this request pending for ACK. Mark it as WROTE and move it from PENDING_WRITE list so it wont get RE-SENT */
		else
		{
			//KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - REQ_ID [%d] - WANT ACK\n",	ev_unixclient->socket_fd, MemSlotBaseSlotGetID((char*)write_req));

			/* Touch counters */
			ev_unixclient->counters.req_sent_with_ack++;

			/* Set FLAGs */
			write_req->flags.in_use		= 1;
			write_req->flags.wrote		= 1;

			/* Switch to PENDING_ACK list */
			MemSlotBaseSlotListIDSwitch(&ev_unixclient->iodata.write.req_mem_slot, write_req->req_id, COMM_UNIX_LIST_PENDING_ACK);
		}
	}
	/* Failed to write on this IO loop, mark as in use */
	else
	{
		/* Tag to schedule request */
		enqueue_write:

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Scheduled write - [%d] bytes \n",
				ev_unixclient->socket_fd, write_req->req_id, write_req->data.size);

		EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);

		/* Touch counters */
		ev_unixclient->counters.req_scheduled++;
		ev_unixclient->flags.pending_write_request = 1;

		/* Set FLAGs */
		write_req->flags.in_use		= 1;
		write_req->flags.wrote		= 0;
		write_req->tx_retry_count++;

	}

	/* Schedule READ_EV and leave */
	leave:
	EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoRead, ev_unixclient);
	return ret_reqid;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvUNIXClientEventConnect(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *ev_unixclient = cb_data;
	int pending_conn				= 0;

	/* Query the kernel for the current socket state */
	ev_unixclient->socket_state = CommEvUNIXClientCheckState(fd);

	/* Connection not yet completed, reschedule and return */
	if (COMM_UNIX_CLIENT_STATE_CONNECTING == ev_unixclient->socket_state)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CONNECTING...!\n", ev_unixclient->socket_fd);
		EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventConnect, ev_unixclient);
		return 0;
	}

	/* If connected OK, set READ and EOF events */
	if (COMM_UNIX_CLIENT_STATE_CONNECTED == ev_unixclient->socket_state)
	{

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Connected!\n", ev_unixclient->socket_fd);

		EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventEOF, ev_unixclient);
		EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE,
				(ev_unixclient->flags.no_brb_proto ? CommEvUNIXClientEventRawRead : CommEvUNIXClientEventBrbProtoRead), ev_unixclient);


		/* Schedule DATARATE CALCULATE TIMER timer */
		if (ev_unixclient->flags.calculate_datarate)
		{
			ev_unixclient->timers.calculate_datarate_id = EvKQBaseTimerAdd(ev_unixclient->kq_base, COMM_ACTION_ADD_VOLATILE,
					((ev_unixclient->retry_times.calculate_datarate_ms > 0) ? ev_unixclient->retry_times.calculate_datarate_ms : COMM_UNIX_CLIENT_CALCULATE_DATARATE_DEFAULT_MS),
					CommEvUNIXClientRatesCalculateTimer, ev_unixclient);

			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule DATARATE at TIMER_ID [%d]\n", ev_unixclient->socket_fd, ev_unixclient->timers.calculate_datarate_id);
		}
		else
			ev_unixclient->timers.calculate_datarate_id = -1;
	}

	/* Dispatch the internal event */
	CommEvUNIXClientEventDispatchInternal(ev_unixclient, data_sz, thrd_id, COMM_UNIX_CLIENT_EVENT_CONNECT);

	/* Connection has failed */
	if (ev_unixclient->socket_state > COMM_UNIX_CLIENT_STATE_CONNECTED)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Connection FAILED!\n", ev_unixclient->socket_fd);

		/* Upper layers want a full DESTROY if CONNECTION FAILS */
		if (ev_unixclient->flags.destroy_after_connect_fail)
		{
			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Destroy as set by flag - DESTROY_AFTER_FAIL\n", ev_unixclient->socket_fd);
			CommEvUNIXClientDestroy(ev_unixclient);
		}
		/* Upper layers want a reconnect retry if CONNECTION FAILS */
		else if ((ev_unixclient->flags.reconnect_on_fail) && (ev_unixclient->socket_state > COMM_UNIX_CLIENT_STATE_CONNECTED))
		{
			/* Will close socket and cancel any pending events of ev_unixclient->socket_fd, including the close event */
			if ( ev_unixclient->socket_fd > 0)
				EvKQBaseSocketClose(ev_unixclient->kq_base, ev_unixclient->socket_fd);

			/* Destroy read and write buffers */
			CommEvUNIXIODataDestroy(&ev_unixclient->iodata);

			/* Schedule RECONNECT timer */
			ev_unixclient->timers.reconnect_on_fail_id =	EvKQBaseTimerAdd(ev_unixclient->kq_base, COMM_ACTION_ADD_VOLATILE,
					((ev_unixclient->retry_times.reconnect_on_fail_ms > 0) ? ev_unixclient->retry_times.reconnect_on_fail_ms : COMM_UNIX_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
					CommEvUNIXClientReconnectTimer, ev_unixclient);

			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
					ev_unixclient->socket_fd, ev_unixclient->timers.reconnect_on_fail_id);

			/* Set flags and STATE */
			ev_unixclient->socket_state	= COMM_UNIX_CLIENT_STATE_DISCONNECTED;
			ev_unixclient->socket_fd	= -1;
		}
		else
		{
			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - NO ACTION TAKEN!\n", ev_unixclient->socket_fd);
		}
	}

	return 1;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientEventRawRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base					= base_ptr;
	CommEvUNIXClient *ev_unixclient		= cb_data;
	CommEvUNIXIOData *iodata			= &ev_unixclient->iodata;
	int still_can_read					= can_read_sz;
	int total_read						= 0;
	int read_bytes						= 0;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - [%d] bytes pending to READ - PARTIAL [%d]\n",
			ev_unixclient->socket_fd, can_read_sz, iodata->flags.read_partial);

	/* Reschedule READ event */
	EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventRawRead, ev_unixclient);

	/* Empty read */
	if (can_read_sz <= 0)
		return 0;

	/* Label to keep reading */
	read_again:
	read_bytes = CommEvUNIXIOReadRaw(ev_base, iodata, fd, still_can_read);

	/* Failed reading */
	if (read_bytes < 0)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Failed trying to read [%d] bytes - ERRNO [%d]\n", ev_unixclient->socket_fd, read_bytes, errno);
		return 0;
	}

	/* Dispatch event - This upper layer event can make ev_unixclient->socket_fd get disconnected, and destroy IO buffers beneath our feet */
	CommEvUNIXClientEventDispatchInternal(ev_unixclient, read_bytes, thrd_id, COMM_UNIX_CLIENT_EVENT_READ);

	/* Account read bytes */
	total_read 		+= read_bytes;
	still_can_read	-= read_bytes;

	if (still_can_read > 0)
		goto read_again;

	return total_read;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientEventBrbProtoRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base					= base_ptr;
	CommEvUNIXClient *ev_unixclient		= cb_data;
	CommEvUNIXIOData *iodata			= &ev_unixclient->iodata;
	CommEvUNIXControlData *control_data = &iodata->read.control_data;
	int total_read						= 0;
	int read_bytes						= 0;
	int reply_count						= 0;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - [%d] bytes pending to READ - PARTIAL [%d]\n",
			ev_unixclient->socket_fd, can_read_sz, iodata->flags.read_partial);

	/* Reschedule READ event */
	EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoRead, ev_unixclient);

	/* Empty read */
	if (can_read_sz <= 0)
		return 0;

	/* Label to keep reading */
	read_again:

	/* Don't have all data to read partial */
	if (!iodata->flags.read_partial && (can_read_sz < sizeof(CommEvUNIXControlData)))
		return 0;

	/* Read message */
	read_bytes 							= CommEvUNIXIORead(ev_base, iodata, ev_unixclient->socket_fd, (can_read_sz - total_read));

	/* Error reading, bail out */
	if (read_bytes < 0)
		return total_read;

	total_read 							+= read_bytes;

	/* Partial READ, leave */
	if (iodata->flags.read_partial)
		return total_read;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - PKT_SZ [%d] - Data event - [%d] FDs - [%d] bytes in KERNEL - [%d] bytes READ\n",
			ev_unixclient->socket_fd, MemBufferGetSize(iodata->read.data_mb), iodata->read.fd_arr.sz, can_read_sz, read_bytes);

	/* Check if received CONTROL_PACKET wants ACK */
	if (EBIT_TEST(control_data->flags, COMM_UNIX_CONTROL_FLAGS_REQUEST))
	{
		if (MemBufferGetSize(iodata->read.data_mb) < control_data->data_sz)
		{
			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - Incomplete READ\n", ev_unixclient->socket_fd);
			goto can_read;

		}

		/* REMOVE ME */
		assert(MemBufferGetSize(iodata->read.data_mb) == control_data->data_sz);

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Flag REQUEST set for ID [%d]\n", fd, control_data->req_id);

		/* Write back ACK to client */
		if (EBIT_TEST(control_data->flags, COMM_UNIX_CONTROL_FLAGS_WANT_ACK))
		{
			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Sending ACK for REQ_ID [%d]\n", fd, control_data->req_id);
			CommEvUNIXClientAIOBrbProtoACK(ev_unixclient, control_data->req_id);
		}

		/* Dispatch event - This upper layer event can make ev_unixclient->socket_fd get disconnected, and destroy IO buffers beneath our feet */
		CommEvUNIXClientEventDispatchInternal(ev_unixclient, read_bytes, thrd_id, COMM_UNIX_CLIENT_EVENT_READ);

		/* REMOVE ME */
		MemBufferClean(iodata->read.data_mb);
	}
	/* This is an ACK reply - There may be ADJACENT replies, process them all */
	else if (EBIT_TEST(iodata->read.control_data.flags, COMM_UNIX_CONTROL_FLAGS_REPLY))
	{
		/* Process control reply - Invoke ACK CALLBACKs is any is active */
		reply_count = CommEvUNIXIOControlDataProcess(ev_base, iodata, control_data, read_bytes);
		ev_unixclient->counters.reply_ack += reply_count;

		/* Calculate reply count */
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Flag REPLY set for ID [%d] - [%d] Processed replies - [%d] Missing ACKs\n",
				fd, control_data->req_id, reply_count, ev_unixclient->counters.req_sent_with_ack - ev_unixclient->counters.reply_ack);


	}
	else
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - NO FLAG [%d] set for ID [%d]\n", fd, control_data->flags, control_data->req_id);

	can_read:

	/* Drained kernel buffer, reschedule and leave */
	if ( (can_read_sz - total_read) <= COMM_UNIX_MIN_DATA_SZ)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Finished - Drained [%d] bytes of [%d], reschedule\n", fd, total_read, can_read_sz);
		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoRead, ev_unixclient);
	}
	/* Keep reading */
	else
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - CAN_READ [%d] - TOT_READ [%d] - DELTA [%d/%d] - READ_AGAIN!!\n",
				fd, can_read_sz, total_read, (can_read_sz - total_read), COMM_UNIX_MIN_DATA_SZ);
		goto read_again;
	}

	return total_read;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientEventBrbProtoWriteACK(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DLinkedList *ack_slot_list;
	CommEvUNIXACKReply *ack_reply;
	int op_status;

	EvKQBase *ev_base							= base_ptr;
	CommEvUNIXClient *ev_unixclient				= cb_data;
	int total_wrote								= 0;
	int sill_can_write_sz						= 0;

	write_again:

	ack_slot_list	= &ev_unixclient->iodata.write.ack_mem_slot.list[COMM_UNIX_LIST_PENDING_WRITE];

	/* Nothing left to write */
	if (MemSlotBaseIsEmptyList(&ev_unixclient->iodata.write.ack_mem_slot, COMM_UNIX_LIST_PENDING_WRITE))
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - ACK - Finish writing!\n", ev_unixclient->socket_fd);

		/* No more pending write */
		if (total_wrote == 0)
			ev_unixclient->flags.pending_write_ack = 0;

		return 0;
	}

	/* Calculate how many bytes we can still write on this IO loop */
	sill_can_write_sz = (can_write_sz - total_wrote);

	/* Too small window to write, bail out */
	if ((sizeof(CommEvUNIXControlData) + 16) > sill_can_write_sz)
	{
		/* Reschedule READ and WRITE event */
		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoRead, ev_unixclient);
		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);

		return total_wrote;
	}

	/* Point to next enqueued ACK reply */
	ack_reply = (CommEvUNIXACKReply*)MemSlotBaseSlotData(ack_slot_list->head->data);

	/* Try to write ASAP */
	op_status = CommEvUNIXIOReplyACK(ack_reply, ev_unixclient->socket_fd);

	/* Success writing */
	if (op_status > 0)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - Immediate write - [%d] bytes \n", ev_unixclient->socket_fd, ack_reply->req_id, op_status);

		/* Touch total bytes written */
		total_wrote += op_status;
		ack_reply->flags.in_use		= 0;

		/* Release write slot */
		MemSlotBaseSlotFree(&ev_unixclient->iodata.write.ack_mem_slot, ack_reply);
		goto write_again;
	}
	/* Failed to write on this IO loop, mark as in use */
	else
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Failed ACK REPLY\n", ev_unixclient->socket_fd, ack_reply->req_id);
		ack_reply->flags.in_use		= 1;
		ack_reply->tx_retry_count++;

		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);
	}

	/* Reschedule READ event */
	EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoRead, ev_unixclient);
	return total_wrote;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientEventRawWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DLinkedList *slot_list;
	DLinkedListNode *node;
	CommEvUNIXWriteRequest *write_req;
	int wrote_bytes;
	int wrote_count;

	EvKQBase *ev_base				= base_ptr;
	CommEvUNIXClient *ev_unixclient	= cb_data;
	int wrote_total					= 0;
	int sill_can_write_sz			= 0;
	int write_delta					= 0;

	/* Label to keep writing */
	write_again:

	/* Point to MEM_SLOT list */
	slot_list	= &ev_unixclient->iodata.write.req_mem_slot.list[COMM_UNIX_LIST_PENDING_WRITE];
	wrote_count	= 0;

	/* Nothing left to write */
	if (MemSlotBaseIsEmptyList(&ev_unixclient->iodata.write.req_mem_slot, COMM_UNIX_LIST_PENDING_WRITE))
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Empty list - Finish writing!\n", ev_unixclient->socket_fd);

		/* No more pending write */
		if (ev_unixclient == 0)
			ev_unixclient->flags.pending_write_request = 0;
		return 0;
	}

	/* Point to next HEAD element and calculate how many bytes we can still write on this IO loop */
	write_req			= MemSlotBaseSlotData(slot_list->head->data);
	sill_can_write_sz	= (can_write_sz - wrote_total);

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Will write [%d] pending items - [%d] Already written\n",
			ev_unixclient->socket_fd, (slot_list->size - wrote_count), wrote_count);
	assert(write_req->flags.in_use);

	/* Write data * */
	wrote_bytes = CommEvUNIXIOWriteRaw(ev_unixclient->kq_base, write_req, ev_unixclient->socket_fd);

	/* Touch sequence ID and offset */
	if (wrote_bytes > 0)
	{
		write_req->data.offset += wrote_bytes;
		write_req->data.remain -= wrote_bytes;
		write_req->data.seq_id++;
	}
	else
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - FAILED - written [%d] - data_sz [%d] - delta [%d] - remain [%d]\n",
				ev_unixclient->socket_fd, write_req->req_id, wrote_bytes, write_req->data.size, write_delta, write_req->data.remain);
	}

	/* Incomplete write, reschedule */
	if (write_req->data.remain > 0)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Incomplete - written [%d] - data_sz [%d] - delta [%d] - remain [%d]\n",
				ev_unixclient->socket_fd, write_req->req_id, wrote_bytes, write_req->data.size, write_delta, write_req->data.remain);
		goto reschedule;
	}

	assert(0 == write_req->data.remain);
	wrote_total			+= wrote_bytes;
	sill_can_write_sz	-= wrote_bytes;

	/* Invoke finish CB */
	if (write_req->finish_cb)
		write_req->finish_cb(ev_unixclient->socket_fd, wrote_bytes, -1, write_req, write_req->finish_cbdata);

	/* Not in use anymore */
	write_req->flags.in_use	= 0;
	MemSlotBaseSlotFree(&ev_unixclient->iodata.write.req_mem_slot, write_req);

	/* Write more bytes if we can */
	if (sill_can_write_sz > 0)
		goto write_again;

	return wrote_total;

	/* Reschedule WRITE event */
	reschedule:

	EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventRawWrite, ev_unixclient);

	/* Touch counters */
	ev_unixclient->counters.req_scheduled++;
	ev_unixclient->flags.pending_write_request = 1;

	/* Set FLAGs */
	write_req->flags.wrote				= 0;
	write_req->tx_retry_count++;

	return wrote_total;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientEventBrbProtoWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DLinkedList *slot_list;
	DLinkedList *ack_list;
	DLinkedListNode *node;
	CommEvUNIXWriteRequest *write_req;
	int op_status;
	int wrote_count;

	EvKQBase *ev_base				= base_ptr;
	CommEvUNIXClient *ev_unixclient	= cb_data;
	int total_wrote					= 0;
	int sill_can_write_sz			= 0;
	int write_delta					= 0;

	/* Label to keep writing */
	write_again:

	/* Point to MEM_SLOT list */
	slot_list	= &ev_unixclient->iodata.write.req_mem_slot.list[COMM_UNIX_LIST_PENDING_WRITE];
	ack_list	= &ev_unixclient->iodata.write.ack_mem_slot.list[COMM_UNIX_LIST_PENDING_WRITE];
	wrote_count	= 0;

	/* Before we can write further, first of all dispatch ALL pending ACKs */
	if (!MemSlotBaseIsEmptyList(&ev_unixclient->iodata.write.ack_mem_slot, COMM_UNIX_LIST_PENDING_WRITE))
	{
		/* Jump into ACK dispatch */
		CommEvUNIXClientEventBrbProtoWriteACK(fd, can_write_sz, thrd_id, cb_data, base_ptr);
		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);
		return 0;
	}

	/* Nothing left to write */
	if (MemSlotBaseIsEmptyList(&ev_unixclient->iodata.write.req_mem_slot, COMM_UNIX_LIST_PENDING_WRITE))
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Empty list - Finish writing!\n", ev_unixclient->socket_fd);

		/* Jump into ACK dispatch */
		CommEvUNIXClientEventBrbProtoWriteACK(fd, can_write_sz, thrd_id, cb_data, base_ptr);

		/* No more pending write */
		if (total_wrote == 0)
			ev_unixclient->flags.pending_write_request = 0;
		return 0;
	}

	/* Point to next HEAD element and calculate how many bytes we can still write on this IO loop */
	write_req			= MemSlotBaseSlotData(slot_list->head->data);
	sill_can_write_sz	= (can_write_sz - total_wrote);

	/* Too small window to write, bail out */
	if ((write_req->data.size + sizeof(CommEvUNIXControlData) + (COMM_UNIX_MAX_FDARR_SZ * sizeof(int)) + 16) > sill_can_write_sz)
	{
		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);
		return total_wrote;
	}

	/* Write buffer too small, enqueue */
	if (EvKQBaseSocketBufferWriteSizeGet(ev_unixclient->kq_base, ev_unixclient->socket_fd) <
			(sizeof(CommEvUNIXControlData) + (COMM_UNIX_MAX_FDARR_SZ * sizeof(int))) + write_req->data.size + 64)
	{
		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);
		return total_wrote;
	}

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Will write [%d] pending items - [%d] Already written\n",
			ev_unixclient->socket_fd, (slot_list->size - wrote_count), wrote_count);
	assert(write_req->flags.in_use);

	op_status = CommEvUNIXIOWrite(ev_unixclient->kq_base, write_req, ev_unixclient->socket_fd);

	/* Success writing */
	if (op_status > 0)
	{
		/* Calculate write delta */
		write_delta = (op_status - write_req->data.remain);

		/* Incomplete write, reschedule */
		if (write_req->data.remain > 0)
		{
			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW,
					"FD [%d] - REQ_ID [%d] - Incomplete - written [%d] - data_sz [%d] - delta [%d] - remain [%d]\n",
					ev_unixclient->socket_fd, write_req->req_id, op_status, write_req->data.size, write_delta, write_req->data.remain);

			goto tx_retry;
		}

		assert(0 == write_req->data.remain);
		total_wrote += op_status;

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - WROTE - [%d] bytes - TOTAL_WROTE [%d]\n", fd, write_req->req_id, op_status, total_wrote);

		ev_unixclient->counters.req_scheduled--;
		assert(ev_unixclient->counters.req_scheduled >= 0);

		/* Invoke finish CB */
		if (write_req->finish_cb)
			write_req->finish_cb(ev_unixclient->socket_fd, op_status, -1, write_req, write_req->finish_cbdata);

		/* If we are not waiting for an ACK reply, release this slot right now. Otherwise, keep it until peer-side ACKs */
		if ((!write_req->ack_cb) && (!write_req->flags.autoclose_on_ack))
		{
			/* Not in use anymore */
			write_req->flags.in_use	= 0;
			MemSlotBaseSlotFree(&ev_unixclient->iodata.write.req_mem_slot, write_req);

			ev_unixclient->counters.req_sent_no_ack++;
		}
		/* We will keep this request pending for ACK. Mark it as WROTE and move it from PENDING_WRITE list so it wont get RE-SENT */
		else
		{
			KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - REQ_ID [%d] - WANT ACK\n",	ev_unixclient->socket_fd, MemSlotBaseSlotGetID((char*)write_req));
			ev_unixclient->counters.req_sent_with_ack++;
			write_req->flags.in_use		= 1;
			write_req->flags.wrote		= 1;

			/* Switch to PENDING_ACK list */
			MemSlotBaseSlotListIDSwitch(&ev_unixclient->iodata.write.req_mem_slot, write_req->req_id, COMM_UNIX_LIST_PENDING_ACK);
		}

		/* Try to write the next request */
		goto write_again;
	}
	/* Too many retries, fail */
	else if (write_req->tx_retry_count > COMM_UNIX_MAX_TX_RETRY)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - REQ_ID [%d] - DROPPING - [%d] bytes - TOTAL_WROTE [%d]\n",
				fd,  write_req->req_id, write_req->data.size, total_wrote);

		/* Invoke finish CB */
		if (write_req->finish_cb)
			write_req->finish_cb(ev_unixclient->socket_fd, op_status, -1, write_req, write_req->finish_cbdata);

		/* Invoke ACK CB */
		if (write_req->ack_cb)
			write_req->ack_cb(COMM_UNIX_ACK_FAILED, write_req, write_req->ack_cbdata);

		/* Upper layers want we close local FDs after ACK */
		if (write_req->flags.autoclose_on_ack)
			CommEvUNIXAutoCloseLocalDescriptors(write_req);

		/* Not in use anymore */
		write_req->flags.in_use	= 0;

		/* Release write slot */
		MemSlotBaseSlotFree((MemSlotBase*)&ev_unixclient->iodata.write.req_mem_slot, write_req);
		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);
	}
	/* Failed to write on this IO loop, reschedule */
	else
	{
		tx_retry:

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - ITER FAIL - [%d] bytes - TOTAL_WROTE [%d]\n",
				fd,  MemSlotBaseSlotGetID((char*)write_req), write_req->data.size, total_wrote);

		EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoWrite, ev_unixclient);
		write_req->tx_retry_count++;
	}

	/* Reschedule READ event */
	EvKQBaseSetEvent(ev_base, ev_unixclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventBrbProtoRead, ev_unixclient);

	return total_wrote;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientEventEOF(int fd, int buf_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *ev_unixclient = cb_data;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF - [%d] bytes left in kernel buffer\n", fd, buf_read_sz);

	ev_unixclient->flags.socket_eof = 1;

	/* Do not close for now, there is data pending read */
	if (buf_read_sz > 0)
	{
		EvKQBaseSetEvent(ev_unixclient->kq_base, fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventEOF, ev_unixclient);
		return 0;
	}

	/* Dispatch internal event */
	CommEvUNIXClientEventDispatchInternal(ev_unixclient, buf_read_sz, thrd_id, COMM_UNIX_CLIENT_EVENT_CLOSE);

	if (ev_unixclient->flags.destroy_after_close)
	{
		CommEvUNIXClientDestroy(ev_unixclient);
	}
	else
	{
		/* Do the internal disconnect and destroy IO buffers */
		CommEvUNIXClientInternalDisconnect(ev_unixclient);
	}

	return 1;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientRatesCalculateTimer(int timer_id, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *ev_unixclient = cb_data;

	//KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - TIMER_ID [%d] - Will calculate rates at [%p]\n", ev_unixclient->socket_fd, timer_id, ev_unixclient);

	if(ev_unixclient->socket_fd < 0)
	{
		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - TIMER_ID [%d] - CLIENT IS GONE!! [%p]\n", ev_unixclient->socket_fd, timer_id, ev_unixclient);

		ev_unixclient->timers.calculate_datarate_id = -1;
		return 1;

		abort();
	}

	/* Calculate read rates */
	if (ev_unixclient->flags.calculate_datarate)
	{
		CommEvStatisticsRateCalculate(ev_unixclient->kq_base, &ev_unixclient->statistics, ev_unixclient->socket_fd, COMM_RATES_READ);
		CommEvStatisticsRateCalculate(ev_unixclient->kq_base, &ev_unixclient->statistics, ev_unixclient->socket_fd, COMM_RATES_WRITE);
		CommEvStatisticsRateCalculate(ev_unixclient->kq_base, &ev_unixclient->statistics, ev_unixclient->socket_fd, COMM_RATES_USER);

		/* Reschedule DATARATE CALCULATE TIMER timer */
		ev_unixclient->timers.calculate_datarate_id = EvKQBaseTimerAdd(ev_unixclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_unixclient->retry_times.calculate_datarate_ms > 0) ? ev_unixclient->retry_times.calculate_datarate_ms : COMM_UNIX_CLIENT_CALCULATE_DATARATE_DEFAULT_MS),
				CommEvUNIXClientRatesCalculateTimer, ev_unixclient);
	}
	else
		ev_unixclient->timers.calculate_datarate_id = -1;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientReconnectTimer(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *ev_unixclient = cb_data;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - TIMER_ID [%d]\n", ev_unixclient->socket_fd, timer_id);

	/* Disable timer ID on CLIENT */
	if (timer_id == ev_unixclient->timers.reconnect_after_close_id)
		ev_unixclient->timers.reconnect_after_close_id = -1;
	else if (timer_id == ev_unixclient->timers.reconnect_after_timeout_id)
		ev_unixclient->timers.reconnect_after_timeout_id = -1;
	else if (timer_id == ev_unixclient->timers.reconnect_on_fail_id)
		ev_unixclient->timers.reconnect_on_fail_id = -1;

	/* Try to reconnect */
	CommEvUNIXClientReconnect(ev_unixclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientTimerConnectTimeout(int fd, int timeout_type, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *ev_unixclient = cb_data;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Connection TIMEDOUT\n", ev_unixclient->socket_fd);

	/* Set client state */
	ev_unixclient->socket_state = COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_TIMEOUT;

	/* Cancel and close any pending event associated with UNIX client FD, to avoid double notification */
	EvKQBaseClearEvents(ev_unixclient->kq_base, ev_unixclient->socket_fd);
	EvKQBaseTimeoutClearAll(ev_unixclient->kq_base, ev_unixclient->socket_fd);

	/* Dispatch internal event */
	CommEvUNIXClientEventDispatchInternal(ev_unixclient, timeout_type, thrd_id, COMM_UNIX_CLIENT_EVENT_CONNECT);

	/* Disconnect any pending stuff - Will schedule reconnect if upper layers asked for it */
	CommEvUNIXClientInternalDisconnect(ev_unixclient);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvUNIXClientCheckState(int socket_fd)
{
	int op_status = -1;
	int err = 0;
	socklen_t errlen = sizeof(err);

	op_status = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &errlen);

	if (op_status == 0)
	{
		switch (err)
		{

		case 0:
		case EISCONN:
			return COMM_UNIX_CLIENT_STATE_CONNECTED;

		case EINPROGRESS:
			/* FALL TROUGHT */
		case EWOULDBLOCK:
			/* FALL TROUGHT */
		case EALREADY:
			/* FALL TROUGHT */
		case EINTR:
			return COMM_UNIX_CLIENT_STATE_CONNECTING;

		case ECONNREFUSED:
			return COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_REFUSED;

		default:
			break;

		}
	}

	return COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_UNKNWON;
}
/**************************************************************************************************************************/
static void CommEvUNIXClientInternalDisconnect(CommEvUNIXClient *ev_unixclient)
{
	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Clean up\n", ev_unixclient->socket_fd);

	/* Will close socket and cancel any pending events of ev_unixclient->socket_fd, including the close event */
	if ( ev_unixclient->socket_fd > 0)
	{
		EvKQBaseSocketClose(ev_unixclient->kq_base, ev_unixclient->socket_fd);

		ev_unixclient->socket_state	= COMM_UNIX_CLIENT_STATE_DISCONNECTED;
		ev_unixclient->socket_fd		= -1;
	}

	/* Cancel any possible timer */
	CommEvUNIXClientTimersCancelAll(ev_unixclient);

	/* Destroy read and write buffers */
	CommEvUNIXIODataDestroy(&ev_unixclient->iodata);

	/* If client want to be reconnected automatically after a CONN_TIMEOUT, honor it */
	if ( (COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_TIMEOUT == ev_unixclient->socket_state) && ev_unixclient->flags.reconnect_on_timeout)
	{
		ev_unixclient->timers.reconnect_after_timeout_id = EvKQBaseTimerAdd(ev_unixclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_unixclient->retry_times.reconnect_after_timeout_ms > 0) ? ev_unixclient->retry_times.reconnect_after_timeout_ms : COMM_UNIX_CLIENT_RECONNECT_TIMEOUT_DEFAULT_MS),
				CommEvUNIXClientReconnectTimer, ev_unixclient);

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Timed out! Auto reconnect in [%d] ms at timer_id [%d]\n",
				ev_unixclient->socket_fd, ev_unixclient->retry_times.reconnect_after_timeout_ms, ev_unixclient->timers.reconnect_after_timeout_id);
	}
	/* If client want to be reconnected automatically after an EOF, honor it */
	else if (ev_unixclient->flags.reconnect_on_close)
	{
		ev_unixclient->timers.reconnect_after_close_id =	EvKQBaseTimerAdd(ev_unixclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_unixclient->retry_times.reconnect_after_close_ms > 0) ? ev_unixclient->retry_times.reconnect_after_close_ms : COMM_UNIX_CLIENT_RECONNECT_CLOSE_DEFAULT_MS),
				CommEvUNIXClientReconnectTimer, ev_unixclient);

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Unexpectedly closed! Auto reconnect in [%d] ms at timer_id [%d]\n",
				ev_unixclient->socket_fd, ev_unixclient->retry_times.reconnect_after_close_ms, ev_unixclient->timers.reconnect_after_close_id);
	}

	return;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientAsyncConnect(CommEvUNIXClient *ev_unixclient)
{
	int op_status;

	struct sockaddr *servaddr		= (struct sockaddr *) &ev_unixclient->servaddr_un;
	int              servaddr_len	= (sizeof (struct sockaddr_un) - sizeof (ev_unixclient->servaddr_un.sun_path) + strlen (ev_unixclient->server_path));

	/* Populate structure */
	ev_unixclient->servaddr_un.sun_family  = AF_UNIX;
	strncpy (ev_unixclient->servaddr_un.sun_path, ev_unixclient->server_path, sizeof(ev_unixclient->servaddr_un.sun_path));

	/* Begin connect */
	op_status = connect(ev_unixclient->socket_fd, servaddr, servaddr_len);

	/* Success connecting */
	if (0 == op_status)
		goto conn_success;

	if (op_status < 0)
	{
		switch(errno)
		{
		/* Already connected, bail out */
		case EALREADY:
		case EINTR:
			return 1;

		case EINPROGRESS:
			goto conn_success;

		default:
			goto conn_failed;
		}
	}

	/* Success beginning connection */
	conn_success:

	/* Schedule WRITE event to catch ASYNC connect */
	EvKQBaseSetEvent(ev_unixclient->kq_base, ev_unixclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXClientEventConnect, ev_unixclient);

	/* Set client state */
	ev_unixclient->socket_state = COMM_UNIX_CLIENT_STATE_CONNECTING;

	/* If upper layers want CONNECT_TIMEOUT, set timeout on WRITE event on low level layer */
	if (ev_unixclient->timeout.connect_ms > 0)
		EvKQBaseTimeoutSet(ev_unixclient->kq_base, ev_unixclient->socket_fd, KQ_CB_TIMEOUT_WRITE, ev_unixclient->timeout.connect_ms, CommEvUNIXClientTimerConnectTimeout, ev_unixclient);


	return 1;

	/* Failed to begin connection */
	conn_failed:

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Connection failed\n", ev_unixclient->socket_fd);

	/* Failed connecting */
	ev_unixclient->socket_state = COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_CONNECT_SYSCALL;

	/* Dispatch the internal event */
	CommEvUNIXClientEventDispatchInternal(ev_unixclient, 0, -1, COMM_UNIX_CLIENT_EVENT_CONNECT);

	/* Upper layers want a full DESTROY if CONNECTION FAILS */
	if (ev_unixclient->flags.destroy_after_connect_fail)
	{
		CommEvUNIXClientDestroy(ev_unixclient);
	}
	/* Upper layers want a reconnect retry if CONNECTION FAILS */
	else if (ev_unixclient->flags.reconnect_on_fail)
	{
		/* Will close socket and cancel any pending events of ev_unixclient->socket_fd, including the close event */
		if ( ev_unixclient->socket_fd > 0)
		{
			EvKQBaseSocketClose(ev_unixclient->kq_base, ev_unixclient->socket_fd);
			ev_unixclient->socket_fd = -1;
		}

		/* Destroy read and write buffers */
		CommEvUNIXIODataDestroy(&ev_unixclient->iodata);

		ev_unixclient->socket_state	= COMM_UNIX_CLIENT_STATE_DISCONNECTED;
		ev_unixclient->socket_fd		= -1;

		/* Schedule RECONNECT timer */
		ev_unixclient->timers.reconnect_on_fail_id =	EvKQBaseTimerAdd(ev_unixclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_unixclient->retry_times.reconnect_on_fail_ms > 0) ? ev_unixclient->retry_times.reconnect_on_fail_ms : COMM_UNIX_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
				CommEvUNIXClientReconnectTimer, ev_unixclient);

		KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
				ev_unixclient->socket_fd, ev_unixclient->timers.reconnect_on_fail_id);
	}

	return 0;
}
/**************************************************************************************************************************/
static void CommEvUNIXClientEventDispatchInternal(CommEvUNIXClient *ev_unixclient, int data_sz, int thrd_id, int ev_type)
{
	CommEvUNIXGenericCBH *cb_handler	= NULL;
	void *cb_handler_data				= NULL;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EV_ID [%d] - with [%d] bytes\n", ev_unixclient->socket_fd, ev_type, data_sz);

	/* Grab callback_ptr */
	cb_handler	= ev_unixclient->events[ev_type].cb_handler_ptr;

	/* Touch time stamps */
	ev_unixclient->events[ev_type].last_ts = ev_unixclient->kq_base->stats.cur_invoke_ts_sec;
	memcpy(&ev_unixclient->events[ev_type].last_tv, &ev_unixclient->kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Grab data for this CBH */
		cb_handler_data = ev_unixclient->events[ev_type].cb_data_ptr;

		/* Jump into CBH. Base for this event is CommEvUNIXServer* */
		cb_handler(ev_unixclient->socket_fd, data_sz, thrd_id, cb_handler_data, ev_unixclient);
	}

	return;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientTimersCancelAll(CommEvUNIXClient *ev_unixclient)
{
	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Will cancel timers - RE_AFTER_CLOSE [%d] -  RE_AFTER_TIMEOUT [%d] -  "
			"RE_AFTER_FAIL [%d] - CALCULATE [%d]\n", ev_unixclient->socket_fd, ev_unixclient->timers.reconnect_after_close_id, ev_unixclient->timers.reconnect_after_timeout_id,
			ev_unixclient->timers.reconnect_on_fail_id, ev_unixclient->timers.calculate_datarate_id);

	/* DELETE all pending timers */
	if (ev_unixclient->timers.reconnect_after_close_id > -1)
		EvKQBaseTimerCtl(ev_unixclient->kq_base, ev_unixclient->timers.reconnect_after_close_id, COMM_ACTION_DELETE);

	if (ev_unixclient->timers.reconnect_after_timeout_id > -1)
		EvKQBaseTimerCtl(ev_unixclient->kq_base, ev_unixclient->timers.reconnect_after_timeout_id, COMM_ACTION_DELETE);

	if (ev_unixclient->timers.reconnect_on_fail_id > -1)
		EvKQBaseTimerCtl(ev_unixclient->kq_base, ev_unixclient->timers.reconnect_on_fail_id, COMM_ACTION_DELETE);

	if (ev_unixclient->timers.calculate_datarate_id > -1)
		EvKQBaseTimerCtl(ev_unixclient->kq_base, ev_unixclient->timers.calculate_datarate_id, COMM_ACTION_DELETE);

	ev_unixclient->timers.reconnect_after_close_id		= -1;
	ev_unixclient->timers.reconnect_after_timeout_id	= -1;
	ev_unixclient->timers.reconnect_on_fail_id			= -1;
	ev_unixclient->timers.calculate_datarate_id			= -1;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvUNIXClientObjectDestroyCBH(void *kq_obj_ptr, void *cb_data)
{
	EvBaseKQObject *kq_obj			= kq_obj_ptr;
	CommEvUNIXClient *ev_unixclient	= kq_obj->obj.ptr;

	KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "Invoked to destroy COMM_EV_UNIX_CLIENT at [%p]\n", kq_obj->obj.ptr);

	/* Destroy and clean structure */
	CommEvUNIXClientDestroy(ev_unixclient);

	return 1;
}
/**************************************************************************************************************************/

