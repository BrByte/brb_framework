/*
 * comm_unix_server.c
 *
 *  Created on: 2014-06-13
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

static EvBaseKQCBH CommEvUNIXServerEventCommonAccept;
static EvBaseKQCBH CommEvUNIXServerEventRawRead;
static EvBaseKQCBH CommEvUNIXServerEventRawWrite;
static EvBaseKQCBH CommEvUNIXServerEventBrbProtoRead;
static EvBaseKQCBH CommEvUNIXServerEventBrbProtoWriteACK;
static EvBaseKQCBH CommEvUNIXServerEventBrbProtoWrite;
static EvBaseKQCBH CommEvUNIXServerEventCommonClose;

static void CommEvUNIXServerEventDispatchInternal(CommEvUNIXServer *srv_ptr, CommEvUNIXServerConn *conn_hnd, int data_sz, int thrd_id, int ev_type);
static void CommEvUNIXServerConnEventDispatchInternal(CommEvUNIXServerConn *conn_hnd, int data_sz, int thrd_id, int ev_type);

/**************************************************************************************************************************/
CommEvUNIXServer *CommEvUNIXServerNew(EvKQBase *kq_base)
{
	CommEvUNIXServer *srv_ptr;

	srv_ptr = calloc(1, sizeof(CommEvUNIXServer));
	srv_ptr->kq_base		= kq_base;

	/* Initialize CONN arena and list */
	srv_ptr->conn.arena = MemArenaNew(1024, (sizeof(CommEvUNIXServerConn) + 1), 32, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&srv_ptr->conn.listener_list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&srv_ptr->conn.global_list, BRBDATA_THREAD_UNSAFE);

	/* Initialize LISTENER SLOTs */
	SlotQueueInit(&srv_ptr->listener.slot, COMM_UNIX_SERVER_MAX_LISTERNERS, (kq_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));

	return srv_ptr;
}
/**************************************************************************************************************************/
void CommEvUNIXServerDestroy(CommEvUNIXServer *srv_ptr)
{
	/* Sanity check */
	if (!srv_ptr)
		return;

	/* Destroy client arena */
	MemArenaDestroy(srv_ptr->conn.arena);
	srv_ptr->conn.arena = NULL;

	free(srv_ptr);

	return;
}
/**************************************************************************************************************************/
int CommEvUNIXServerListenerAdd(CommEvUNIXServer *srv_ptr, CommEvUNIXServerConf *server_conf)
{
	CommEvUNIXServerListener *listener;
	EvBaseKQFileDesc *kq_fd;

	struct sockaddr_un  servaddr_un;
	struct sockaddr    *servaddr;

	int servaddr_len;
	int status;
	int slot_id;
	int i;

	int reuseaddr_on	= 1;
	int op_status		= 0;

	/* Sanity check */
	if ((!srv_ptr) || (!server_conf))
		return (- COMM_SERVER_FAILURE_UNKNOWN);

	/* Need path */
	if (!server_conf->path_str)
		return (- COMM_SERVER_FAILURE_UNKNOWN);

	/* Grab a free listener SLOT ID */
	slot_id = SlotQueueGrab(&srv_ptr->listener.slot);

	/* No more slots allowed, bail out */
	if (slot_id < 0)
		return (- COMM_SERVER_FAILURE_NO_MORE_SLOTS);

	/* Clean stack space */
	memset (&servaddr_un, 0, sizeof (struct sockaddr_un));

	/* Point to selected LISTENER and point LISTENER back to server */
	listener							= &srv_ptr->listener.arr[slot_id];
	listener->parent_srv				= srv_ptr;
	listener->slot_id					= slot_id;
	listener->listen_queue_sz			= ((server_conf->listen_queue_sz > 0) ? server_conf->listen_queue_sz : 256);

	/* Copy FLAGs */
	listener->flags.autoclose_fd_on_ack = server_conf->flags.autoclose_fd_on_ack;
	listener->flags.reuse_addr			= server_conf->flags.reuse_addr;
	listener->flags.reuse_port			= server_conf->flags.reuse_port;
	listener->flags.no_brb_proto		= server_conf->flags.no_brb_proto;

	/* Load path we will listen to */
	strncpy((char*)&listener->path_str, server_conf->path_str, sizeof(listener->path_str));

	/* Unlink it if already exists */
	unlink((char*)&listener->path_str);

	/* Copy common configuration information */
	srv_ptr->cfg[slot_id].cli_queue_max	= server_conf->limits.cli_queue_max;

	/* Create socket */
	listener->socket_fd = EvKQBaseSocketUNIXNew(srv_ptr->kq_base);

	/* Check if created socket is ok */
	if (listener->socket_fd < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed initializing socket for UNIX listener [%d] - ERRNO [%d]\n", slot_id, errno);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- COMM_SERVER_FAILURE_SOCKET);
	}

	/* Set SOCKOPT SO_REUSEADDR */
	if ((listener->flags.reuse_addr) && (EvKQBaseSocketSetReuseAddr(srv_ptr->kq_base, listener->socket_fd) == -1))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_ADDR on socket for UNIX listener [%d] - ERRNO [%d]\n", slot_id, errno);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- COMM_SERVER_FAILURE_REUSEADDR);
	}

	/* Set SOCKOPT SO_REUSEPORT */
	if ((listener->flags.reuse_port) && (EvKQBaseSocketSetReusePort(srv_ptr->kq_base, listener->socket_fd) == -1))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed setting REUSE_PORT on socket for UNIX listener [%d] - ERRNO [%d]\n", slot_id, errno);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- COMM_SERVER_FAILURE_REUSEPORT);
	}

	/* Populate structure */
	servaddr_un.sun_family  = AF_UNIX;
	strcpy (servaddr_un.sun_path, (char*)&listener->path_str);

	/* Cast and calculate size */
	servaddr     = (struct sockaddr *) &servaddr_un;
	servaddr_len = sizeof (struct sockaddr_un) - sizeof (servaddr_un.sun_path) + strlen ((char*)&listener->path_str);

	/* Bind it */
	op_status = bind (listener->socket_fd, servaddr, servaddr_len);

	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed BIND on socket [%d] for UNIX listener [%d] - ERRNO [%d]\n", listener->socket_fd, slot_id, errno);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- COMM_SERVER_FAILURE_BIND);
	}

	/* Start listening */
	op_status = listen(listener->socket_fd, listener->listen_queue_sz);

	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed LISTEN on socket [%d] for UNIX listener [%d] - ERRNO [%d]\n", listener->socket_fd, slot_id, errno);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- COMM_SERVER_FAILURE_LISTEN);
	}

	/* Grab FD from reference table and mark listening socket flags */
	kq_fd = EvKQBaseFDGrabFromArena(srv_ptr->kq_base, listener->socket_fd);
	kq_fd->flags.so_listen = 1;

	/* Set humanized description of this socket and make it non blocking */
	EvKQBaseFDDescriptionSet(kq_fd, "EV_UNIXSERVER [%s] - LID/FD [%d/%d]", listener->path_str , slot_id, listener->socket_fd);
	EvKQBaseSocketSetNonBlock(srv_ptr->kq_base, listener->socket_fd);

	/* Set default IO events */
	for (i = 0; i < COMM_UNIX_SERVER_EVENT_LASTITEM; i++)
		if (server_conf->events[i].cb_handler_ptr)
			CommEvUNIXServerEventSet(srv_ptr, slot_id, i, server_conf->events[i].cb_handler_ptr, server_conf->events[i].cb_data_ptr);

	/* Add into active linked list and set flags to active */
	DLinkedListAdd(&srv_ptr->listener.list, &listener->node, listener);
	listener->flags.active = 1;

	/* Set volatile read event for accepting new connections - Will be rescheduled by internal accept event */
	EvKQBaseSetEvent(srv_ptr->kq_base, listener->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventCommonAccept, listener);

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Listener ID [%d] added\n",
			listener->socket_fd, listener->slot_id);

	return slot_id;
}
/**************************************************************************************************************************/
int CommEvUNIXServerListenerDel(CommEvUNIXServer *srv_ptr, int listener_id)
{
	/* NOT IMPLEMENTED */
	return 1;
}
/**************************************************************************************************************************/
void CommEvUNIXServerEventSet(CommEvUNIXServer *srv_ptr, int listener_id, CommEvUNIXServerEventCodes ev_type, CommEvUNIXGenericCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= COMM_UNIX_SERVER_EVENT_LASTITEM)
		return;

	/* Set event */
	srv_ptr->events[listener_id][ev_type].cb_handler_ptr	= cb_handler;
	srv_ptr->events[listener_id][ev_type].cb_data_ptr		= cb_data;

	//printf("CommEvUNIXServerEventSet - LISTENER_ID [%d] - EV_TYPE [%d] CB_H [%p]\n", listener_id, ev_type, cb_handler);

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXServerEventCancel(CommEvUNIXServer *srv_ptr, int listener_id, CommEvUNIXServerEventCodes ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_UNIX_SERVER_EVENT_LASTITEM)
		return;

	/* Set event */
	srv_ptr->events[listener_id][ev_type].cb_handler_ptr		= NULL;
	srv_ptr->events[listener_id][ev_type].cb_data_ptr			= NULL;

	/* Mark disabled */
	srv_ptr->events[listener_id][ev_type].flags.enabled			= 0;

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXServerEventCancelAll(CommEvUNIXServer *srv_ptr)
{
	CommEvUNIXServerListener *listener;
	DLinkedListNode *node;
	int i;

	/* Cancel all possible events */
	for (node = srv_ptr->listener.list.head; node; node = node->next)
	{
		listener = node->data;

		/* Cancel all events of this listener */
		for (i = 0; i < COMM_UNIX_SERVER_EVENT_LASTITEM; i++)
			CommEvUNIXServerEventCancel(srv_ptr, listener->slot_id, i);

		continue;
	}

	return;
}
/**************************************************************************************************************************/
int CommEvUNIXServerConnAIOBrbProtoACK(CommEvUNIXServerConn *conn_hnd, int req_id)
{
	CommEvUNIXACKReply *ack_reply;
	int op_status;
	int ret_reqid;

	CommEvUNIXServer *srv_ptr = conn_hnd->parent_srv;

	/* INIT IODATA and mark WRITE_QUEUE as initialized */
	CommEvUNIXIODataInit(&conn_hnd->iodata, conn_hnd->parent_srv, conn_hnd->socket_fd, COMM_UNIX_MAX_WRITE_REQ, conn_hnd->parent_srv->kq_base->flags.mt_engine);

	/* Grab a write request slot */
	ack_reply	= (CommEvUNIXACKReply*)MemSlotBaseSlotGrab(&conn_hnd->iodata.write.ack_mem_slot);

	/* Cannot enqueue anymore */
	if (!ack_reply)
		return -1;

	assert(!ack_reply->flags.in_use);
	memset(ack_reply, 0, sizeof(CommEvUNIXACKReply));

	/* Populate ACK reply */
	ack_reply->req_id	= req_id;
	ret_reqid			= MemSlotBaseSlotGetID(ack_reply);

	/* Already pending, enqueue */
	if (conn_hnd->flags.pending_write_request)
		goto enqueue_write;

	/* Try to write ASAP */
	op_status = CommEvUNIXIOReplyACK(ack_reply, conn_hnd->socket_fd);

	/* Success writing */
	if (op_status > 0)
	{
		assert(op_status >= sizeof(CommEvUNIXControlData));
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - Immediate write - [%d] bytes \n", conn_hnd->socket_fd, ack_reply->req_id, op_status);

		/* Release write slot */
		MemSlotBaseSlotFree(&conn_hnd->iodata.write.ack_mem_slot, ack_reply);
	}
	/* Failed to write on this IO loop, mark as in use */
	else
	{
		/* Tag to schedule request */
		enqueue_write:

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Scheduled write\n", conn_hnd->socket_fd, ack_reply->req_id);

		EvKQBaseSetEvent(conn_hnd->parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoWrite, conn_hnd);
		ack_reply->flags.in_use		= 1;
		ack_reply->tx_retry_count++;

		/* Mark as pending write ACK */
		conn_hnd->flags.pending_write_ack = 1;
	}

	return ret_reqid;
}
/**************************************************************************************************************************/
int CommEvUNIXServerConnAIORawWriteStr(CommEvUNIXServerConn *conn_hnd, char *data, CommEvUNIXGenericCBH *finish_cb, void *finish_cbdata)
{
	long str_sz = strlen(data);
	return (CommEvUNIXServerConnAIORawWrite(conn_hnd, data, str_sz, finish_cb, finish_cbdata));
}
/**************************************************************************************************************************/
int CommEvUNIXServerConnAIORawWrite(CommEvUNIXServerConn *conn_hnd, char *data, long data_sz, CommEvUNIXGenericCBH *finish_cb, void *finish_cbdata)
{
	CommEvUNIXWriteRequest *write_req;
	int op_status;

	CommEvUNIXServer *srv_ptr			= conn_hnd->parent_srv;
	CommEvUNIXServerListener *listener	= conn_hnd->listener;

	/* Close request */
	if (conn_hnd->flags.close_request)
		return -1;

	/* INIT IODATA and mark WRITE_QUEUE as initialized */
	CommEvUNIXIODataInit(&conn_hnd->iodata, srv_ptr, conn_hnd->socket_fd, COMM_UNIX_MAX_WRITE_REQ, conn_hnd->parent_srv->kq_base->flags.mt_engine);

	/* Grab a write request slot */
	write_req	= MemSlotBaseSlotGrab(&conn_hnd->iodata.write.req_mem_slot);

	/* Cannot enqueue anymore */
	if (!write_req)
		return -2;

	assert(!write_req->flags.in_use);
	memset(write_req, 0, sizeof(CommEvUNIXWriteRequest));

	/* Fill write request */
	write_req->kq_base					= srv_ptr->kq_base;
	write_req->data.ptr					= data;
	write_req->data.size				= data_sz;
	write_req->data.remain				= data_sz;
	write_req->data.offset 				= 0;
	write_req->finish_cb				= finish_cb;
	write_req->finish_cbdata			= finish_cbdata;
	write_req->parent_ptr				= conn_hnd;
	write_req->parent_type				= COMM_UNIX_PARENT_UNIX_CONN_HND;
	write_req->req_id					= MemSlotBaseSlotGetID(write_req);
	write_req->flags.in_use				= 1;

	/* If there is ENQUEUED data, schedule WRITE event and LEAVE, as we need to PRESERVE WRITE ORDER */
	if (conn_hnd->flags.pending_write_request)
		goto enqueue_write;

	/* Try to write on this very same IO LOOP */
	op_status = CommEvUNIXServerEventRawWrite(conn_hnd->socket_fd, 8092, -1, conn_hnd, srv_ptr->kq_base);

	/* Incomplete write, reschedule */
	if (write_req->data.remain > 0)
		goto enqueue_write;
	else
	{
		write_req->flags.in_use		= 0;
		write_req->flags.wrote		= 1;
		return op_status;
	}

	/* Tag to schedule request */
	enqueue_write:

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Scheduled write - [%d] bytes - REMAIN [%d]\n",
			conn_hnd->socket_fd, write_req->req_id, write_req->data.size, write_req->data.remain);

	EvKQBaseSetEvent(conn_hnd->parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventRawWrite, conn_hnd);

	/* Touch counters */
	conn_hnd->counters.req_scheduled++;
	conn_hnd->flags.pending_write_request = 1;

	/* Set FLAGs */
	write_req->flags.wrote		= 0;
	write_req->tx_retry_count++;

	return write_req->req_id;
}
/**************************************************************************************************************************/
int CommEvUNIXServerConnAIOBrbProtoWrite(CommEvUNIXServerConn *conn_hnd, char *data, long data_sz, int *fd_arr, int fd_sz, CommEvUNIXGenericCBH *finish_cb,
		CommEvUNIXACKCBH *ack_cb, void *finish_cbdata)
{
	CommEvUNIXWriteRequest *write_req;
	int write_delta;
	int op_status;
	int ret_reqid;
	int i;

	CommEvUNIXServer *srv_ptr			= conn_hnd->parent_srv;
	CommEvUNIXServerListener *listener	= conn_hnd->listener;
	int control_data_sz					= sizeof(CommEvUNIXControlData);

	/* Too many FDs */
	if (fd_sz > COMM_UNIX_MAX_FDARR_SZ)
		return -1;

	/* Close request */
	if (conn_hnd->flags.close_request)
		return -1;

	/* XXX TODO: finish this - Cannot enqueue anymore */
	//if (!COMM_UNIX_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
	//	return 0;

	/* INIT IODATA and mark WRITE_QUEUE as initialized */
	CommEvUNIXIODataInit(&conn_hnd->iodata, srv_ptr, conn_hnd->socket_fd, COMM_UNIX_MAX_WRITE_REQ, conn_hnd->parent_srv->kq_base->flags.mt_engine);

	/* Grab a write request slot */
	write_req	= MemSlotBaseSlotGrab(&conn_hnd->iodata.write.req_mem_slot);

	/* Cannot enqueue anymore */
	if (!write_req)
		return -2;

	assert(!write_req->flags.in_use);
	memset(write_req, 0, sizeof(CommEvUNIXWriteRequest));

	/* Fill write request */
	write_req->kq_base					= srv_ptr->kq_base;
	write_req->data.ptr					= data;
	write_req->data.size				= data_sz;
	write_req->data.remain				= data_sz;
	write_req->data.offset 				= 0;
	write_req->ack_cb					= ack_cb;
	write_req->finish_cb				= finish_cb;
	write_req->finish_cbdata			= finish_cbdata;
	write_req->parent_ptr				= conn_hnd;

	write_req->parent_type				= COMM_UNIX_PARENT_UNIX_CONN_HND;
	write_req->req_id					= MemSlotBaseSlotGetID(write_req);
	write_req->flags.autoclose_on_ack	= listener->flags.autoclose_fd_on_ack;
	ret_reqid							= write_req->req_id;

	/* Set ctrl_flag as REQUEST, and set WANT_ACK if we have a CALLBACK defined */
	EBIT_SET(write_req->ctrl_flags, COMM_UNIX_CONTROL_FLAGS_REQUEST);

	/* Either if we have ACK_CB or FLAGs set to AUTODESTROY on ACK, we will want ACK from CLIENT_SIDE */
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

	/* Already pending, enqueue */
	if ((conn_hnd->flags.pending_write_request) || (conn_hnd->flags.pending_write_ack))
		goto enqueue_write;

	/* Write buffer too small, enqueue */
	//if (EvKQBaseSocketBufferWriteSizeGet(srv_ptr->kq_base, conn_hnd->socket_fd) < ( sizeof(CommEvUNIXControlData) + (COMM_UNIX_MAX_FDARR_SZ * sizeof(int))) + write_req->data.size, 64)
	//	goto enqueue_write;

	/* Try to write ASAP */
	op_status = CommEvUNIXIOWrite(srv_ptr->kq_base, write_req, conn_hnd->socket_fd);

	/* Success writing */
	if (op_status > 0)
	{
		write_delta = (op_status - write_req->data.remain);

		/* Incomplete write, reschedule */
		if (write_req->data.remain > 0)
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Incomplete - written [%d] - data_sz [%d] - delta [%d] - remain [%d]\n",
					conn_hnd->socket_fd, write_req->req_id, op_status, write_req->data.size, write_delta, write_req->data.remain);
			goto enqueue_write;
		}

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] - REQ_ID [%d] - Immediate write - [%d] bytes - DELTA [%d] - REMAIN [%d]\n",
				conn_hnd->socket_fd, write_req->req_id, op_status, write_delta, write_req->data.remain);

		assert(0 == write_req->data.remain);

		/* Invoke finish CB */
		if (finish_cb)
			finish_cb(conn_hnd->socket_fd, op_status, -1, write_req, finish_cbdata);

		/* If we are not waiting for an ACK reply, release this slot right now. Otherwise, keep it until peer-side ACKs */
		if ((!write_req->ack_cb) && (!write_req->flags.autoclose_on_ack))
		{
			/* Release SLOT and touch counters */
			MemSlotBaseSlotFree(&conn_hnd->iodata.write.req_mem_slot, write_req);
			conn_hnd->counters.req_sent_no_ack++;
			goto leave;
		}
		/* We will keep this request pending for ACK. Mark it as WROTE and move it from PENDING_WRITE list so it wont get RE-SENT */
		else
		{
			//KQBASE_LOG_PRINTF(ev_unixclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - REQ_ID [%d] - WANT ACK\n",	ev_unixclient->socket_fd, MemSlotBaseSlotGetID((char*)write_req));

			/* Touch counters */
			conn_hnd->counters.req_sent_with_ack++;

			/* Set FLAGs */
			write_req->flags.in_use		= 1;
			write_req->flags.wrote		= 1;

			/* Switch to PENDING_ACK list */
			MemSlotBaseSlotListIDSwitch(&conn_hnd->iodata.write.req_mem_slot, write_req->req_id, COMM_UNIX_LIST_PENDING_ACK);
		}
	}
	/* Failed to write on this IO loop, mark as in use */
	else
	{
		/* Tag to schedule request */
		enqueue_write:

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Scheduled write - [%d] bytes - REMAIN [%d]\n",
				conn_hnd->socket_fd, write_req->req_id, write_req->data.size, write_req->data.remain);

		EvKQBaseSetEvent(conn_hnd->parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoWrite, conn_hnd);

		/* Touch counters */
		conn_hnd->counters.req_scheduled++;
		conn_hnd->flags.pending_write_request = 1;

		/* Set FLAGs */
		write_req->flags.in_use		= 1;
		write_req->flags.wrote		= 0;
		write_req->tx_retry_count++;
	}

	/* Reschedule READ event */
	leave:
	EvKQBaseSetEvent(conn_hnd->parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoRead, conn_hnd);
	return ret_reqid;
}
/**************************************************************************************************************************/
void CommEvUNIXServerConnCloseRequest(CommEvUNIXServerConn *conn_hnd)
{
	/* Mark flags to close as soon as we finish writing all */
	conn_hnd->flags.close_request = 1;

	if (MemSlotBaseIsEmpty(&conn_hnd->iodata.write.req_mem_slot))
		CommEvUNIXServerConnClose(conn_hnd);

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXServerConnClose(CommEvUNIXServerConn *conn_hnd)
{
	CommEvUNIXServer *srv_ptr = conn_hnd->parent_srv;
	int fd = conn_hnd->socket_fd;

	/* Destroy IO data buffers and cancel any pending event */
	CommEvUNIXIODataDestroy(&conn_hnd->iodata);
	CommEvUNIXServerConnEventCancelAll(conn_hnd);

	/* Close socket and set to -1 */
	EvKQBaseSocketClose(srv_ptr->kq_base, fd);
	conn_hnd->socket_fd = -1;

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXServerConnEventSetDefault(CommEvUNIXServer *srv_ptr, CommEvUNIXServerConn *conn_hnd)
{
	int listener_id = conn_hnd->listener->slot_id;

	/* Default READ event */
	conn_hnd->events[COMM_UNIX_SERVER_CONN_EVENT_READ].cb_handler_ptr	= srv_ptr->events[listener_id][COMM_UNIX_SERVER_EVENT_READ].cb_handler_ptr;
	conn_hnd->events[COMM_UNIX_SERVER_CONN_EVENT_READ].cb_data_ptr		= srv_ptr->events[listener_id][COMM_UNIX_SERVER_EVENT_READ].cb_data_ptr;

	/* Default CLOSE event */
	conn_hnd->events[COMM_UNIX_SERVER_CONN_EVENT_CLOSE].cb_handler_ptr	= srv_ptr->events[listener_id][COMM_UNIX_SERVER_EVENT_CLOSE].cb_handler_ptr;
	conn_hnd->events[COMM_UNIX_SERVER_CONN_EVENT_CLOSE].cb_data_ptr		= srv_ptr->events[listener_id][COMM_UNIX_SERVER_EVENT_CLOSE].cb_data_ptr;

	return;

}
/**************************************************************************************************************************/
void CommEvUNIXServerConnEventSet(CommEvUNIXServerConn *conn_hnd, CommEvUNIXServerConnEventCodes ev_type, CommEvUNIXGenericCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= COMM_UNIX_SERVER_CONN_EVENT_LASTITEM)
		return;

	/* Set event */
	conn_hnd->events[ev_type].cb_handler_ptr	= cb_handler;
	conn_hnd->events[ev_type].cb_data_ptr		= cb_data;
	conn_hnd->events[ev_type].flags.enabled		= 1;
	return;
}
/**************************************************************************************************************************/
void CommEvUNIXServerConnEventCancel(CommEvUNIXServerConn *conn_hnd, CommEvUNIXServerConnEventCodes ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_UNIX_SERVER_CONN_EVENT_LASTITEM)
		return;

	/* Set event */
	conn_hnd->events[ev_type].cb_handler_ptr	= NULL;
	conn_hnd->events[ev_type].cb_data_ptr		= NULL;
	conn_hnd->events[ev_type].flags.enabled		= 0;

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXServerConnEventCancelAll(CommEvUNIXServerConn *conn_hnd)
{
	int i;

	/* Cancel all possible events */
	for (i = 0; i < COMM_UNIX_SERVER_CONN_EVENT_LASTITEM; i++)
		CommEvUNIXServerConnEventCancel(conn_hnd, i);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvUNIXServerEventCommonAccept(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXServerConn *conn_hnd;
	EvBaseKQFileDesc *kq_fd;
	struct sockaddr_in clientaddr;
	int conn_fd;

	EvKQBase *ev_base					= base_ptr;
	CommEvUNIXServerListener *listener	= cb_data;
	CommEvUNIXServer *srv_ptr			= listener->parent_srv;
	unsigned int sockaddr_sz			= sizeof(struct sockaddr_in);
	int listener_id						= listener->slot_id;

	CommEvUNIXGenericCBH *cb_handler	= NULL;
	void *cb_handler_data				= NULL;

	/* Accept the connection */
	conn_fd = accept(fd, (struct sockaddr *)&clientaddr, &sockaddr_sz);

	/* Reschedule for new accept */
	EvKQBaseSetEvent(ev_base, listener->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventCommonAccept, listener);

	/* Check if succeeded accepting connection */
	if (conn_fd > 0)
	{
		/* Grab a connection handler from server internal arena */
		conn_hnd	= MemArenaGrabByID(srv_ptr->conn.arena, conn_fd);
		kq_fd		= EvKQBaseFDGrabFromArena(ev_base, conn_fd);

		/* Touch flags */
		kq_fd->flags.active		= 1;
		kq_fd->flags.closed		= 0;
		kq_fd->flags.closing	= 0;

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Accept NEW client on FD [%d]-[%p] - Thread [%d] with listener ID [%d]\n",
				fd, conn_fd, conn_hnd, thrd_id, listener_id);

		EvKQBaseFDDescriptionSet(kq_fd, "EV_UNIX_CONN - SERVER_LID/FD [%d / %d]", listener_id, listener->socket_fd);

		/* Clean all flags, statistics and iodata */
		memset(&conn_hnd->flags, 0, sizeof(conn_hnd->flags));
		memset(&conn_hnd->statistics, 0, sizeof(conn_hnd->statistics));
		memset(&conn_hnd->iodata, 0, sizeof(CommEvUNIXIOData));

		/* Save remote client address into context */
		memcpy(&conn_hnd->conn_addr, &clientaddr, sizeof(struct sockaddr_in));

		/* Make it non-blocking */
		EvKQBaseSocketSetNonBlock(ev_base, conn_fd);

		/* Grab a reference of server information */
		conn_hnd->socket_fd						= conn_fd;
		conn_hnd->parent_srv					= srv_ptr;
		conn_hnd->listener						= listener;
		conn_hnd->flags.no_brb_proto			= listener->flags.no_brb_proto;

		/* Set all TIMEOUT stuff to UNINITIALIZED */
		EvKQBaseTimeoutInitAllByFD(ev_base, conn_fd);

		/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
		CommEvUNIXServerEventDispatchInternal(srv_ptr, conn_hnd, read_sz, thrd_id, COMM_UNIX_SERVER_EVENT_ACCEPT);

		/* Set default READ and CLOSE events to this conn_fd, if any is defined in server */
		CommEvUNIXServerConnEventSetDefault(srv_ptr, conn_hnd);

		/* Set disconnect and read internal events for newly connected socket */
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventCommonClose, conn_hnd);
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE,
				(conn_hnd->flags.no_brb_proto ? CommEvUNIXServerEventRawRead : CommEvUNIXServerEventBrbProtoRead), conn_hnd);

	}
	/* Failed accepting */
	else
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "FD [%d] - FAILED accepting client on FD [%d]-[%p] - Thread [%d] with listener ID [%d]\n",
				fd, conn_fd, conn_hnd, thrd_id, listener_id);
		return 0;
	}

	return 1;
}
/**************************************************************************************************************************/
static int CommEvUNIXServerEventRawRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base					= base_ptr;
	CommEvUNIXServerConn *conn_hnd		= cb_data;
	CommEvUNIXServerListener *listener	= conn_hnd->listener;
	CommEvUNIXServer *srv_ptr			= conn_hnd->parent_srv;
	CommEvUNIXIOData *iodata			= &conn_hnd->iodata;
	int listener_id						= listener->slot_id;
	int still_can_read					= can_read_sz;
	int read_bytes						= 0;
	int total_read						= 0;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - [%d] bytes pending to READ - PARTIAL [%d]\n",
			conn_hnd->socket_fd, can_read_sz, iodata->flags.read_partial);

	/* Reschedule READ event */
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventRawRead, conn_hnd);

	/* Empty read */
	if (can_read_sz <= 0)
		return 0;

	/* Label to keep reading */
	read_again:
	read_bytes = CommEvUNIXIOReadRaw(ev_base, iodata, fd, still_can_read);

	/* Failed reading */
	if (read_bytes < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Failed trying to read [%d] bytes - ERRNO [%d]\n", conn_hnd->socket_fd, read_bytes, errno);
		return 0;
	}

	/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
	CommEvUNIXServerEventDispatchInternal(srv_ptr, conn_hnd, read_bytes, thrd_id, COMM_UNIX_SERVER_EVENT_READ);

	/* Account read bytes */
	total_read 		+= read_bytes;
	still_can_read	-= read_bytes;

	if (still_can_read > 0)
		goto read_again;

	return total_read;
}
/**************************************************************************************************************************/
static int CommEvUNIXServerEventRawWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DLinkedList *slot_list;

	EvKQBase *ev_base					= base_ptr;
	CommEvUNIXServerConn *conn_hnd		= cb_data;
	CommEvUNIXServerListener *listener	= conn_hnd->listener;
	CommEvUNIXServer *srv_ptr			= conn_hnd->parent_srv;
	CommEvUNIXIOData *iodata			= &conn_hnd->iodata;
	CommEvUNIXWriteRequest *write_req	= NULL;
	int listener_id						= listener->slot_id;
	int wrote_bytes						= 0;
	int wrote_total						= 0;
	int wrote_count						= 0;
	int write_delta						= 0;
	int sill_can_write_sz				= can_write_sz;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - [%d] bytes can be written\n",	conn_hnd->socket_fd, can_write_sz);

	write_again:

	/* Nothing left to write */
	if (MemSlotBaseIsEmptyList(&conn_hnd->iodata.write.req_mem_slot, COMM_UNIX_LIST_PENDING_WRITE))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_BLUE, "FD [%d] - Finish writing!\n", conn_hnd->socket_fd);

		/* No more pending write */
		if (wrote_total == 0)
			conn_hnd->flags.pending_write_request = 0;
		return 0;
	}

	/* Point to MEM_SLOT list */
	slot_list	= &conn_hnd->iodata.write.req_mem_slot.list[COMM_UNIX_LIST_PENDING_WRITE];

	/* Point to next HEAD element and calculate how many bytes we can still write on this IO loop */
	write_req			= MemSlotBaseSlotData(slot_list->head->data);
	sill_can_write_sz	= (can_write_sz - wrote_total);

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] - REQ_ID [%d / %d] - Will write [%d] pending items - [%d] Already written\n",
			conn_hnd->socket_fd, write_req->req_id, write_req->data.seq_id, (slot_list->size - wrote_count), wrote_count);

	/* Invoke UNIX RAW WRITE */
	wrote_bytes = CommEvUNIXIOWriteRaw(ev_base, write_req, fd);

	/* Touch sequence ID and offset */
	if (wrote_bytes > 0)
	{
		write_req->data.offset += wrote_bytes;
		write_req->data.remain -= wrote_bytes;
		write_req->data.seq_id++;
	}
	else
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - FAILED - written [%d] - data_sz [%d] - delta [%d] - remain [%d]\n",
				conn_hnd->socket_fd, write_req->req_id, wrote_bytes, write_req->data.size, write_delta, write_req->data.remain);
	}

	/* Incomplete write, reschedule */
	if (write_req->data.remain > 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Incomplete - written [%d] - data_sz [%d] - delta [%d] - remain [%d]\n",
				conn_hnd->socket_fd, write_req->req_id, wrote_bytes, write_req->data.size, write_delta, write_req->data.remain);
		goto reschedule;
	}

	assert(0 == write_req->data.remain);
	wrote_total			+= wrote_bytes;
	sill_can_write_sz	-= wrote_bytes;

	/* Invoke finish CB */
	if (write_req->finish_cb)
		write_req->finish_cb(conn_hnd->socket_fd, wrote_bytes, -1, write_req, write_req->finish_cbdata);

	/* Not in use anymore */
	write_req->flags.in_use	= 0;
	MemSlotBaseSlotFree(&conn_hnd->iodata.write.req_mem_slot, write_req);

	/* Write more bytes if we can */
	if (sill_can_write_sz > 0)
		goto write_again;

	return wrote_total;

	/* Reschedule WRITE event */
	reschedule:
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventRawWrite, conn_hnd);
	return wrote_total;
}
/**************************************************************************************************************************/
static int CommEvUNIXServerEventBrbProtoRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base					= base_ptr;
	CommEvUNIXServerConn *conn_hnd		= cb_data;
	CommEvUNIXServerListener *listener	= conn_hnd->listener;
	CommEvUNIXServer *srv_ptr			= conn_hnd->parent_srv;
	CommEvUNIXIOData *iodata			= &conn_hnd->iodata;
	int listener_id						= listener->slot_id;
	int total_read						= 0;
	int read_bytes						= 0;
	int reply_count						= 0;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - [%d] bytes pending to READ - PARTIAL [%d]\n",
			conn_hnd->socket_fd, can_read_sz, iodata->flags.read_partial);

	/* Reschedule READ event */
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoRead, conn_hnd);

	/* Empty read */
	if (can_read_sz <= 0)
		return 0;

	/* Label to keep reading */
	read_again:

	/* Don't have all data to read partial */
	if (!iodata->flags.read_partial && (can_read_sz < sizeof(CommEvUNIXControlData)))
		return 0;

	/* Read message */
	read_bytes 							= CommEvUNIXIORead(ev_base, iodata, conn_hnd->socket_fd, (can_read_sz - total_read));

	/* Error reading, bail out */
	if (read_bytes < 0)
		return total_read;

	/* Increment total count */
	total_read							+= read_bytes;

	/* Partial READ, leave */
	if (iodata->flags.read_partial)
		return total_read;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - PKT_ID [%d] - PKT_FLAGS [%d] - PKT_SZ [%d] - Data event - [%d] FDs - LID [%d] - [%d] bytes in KERNEL - "
			"[%d] bytes READ\n", conn_hnd->socket_fd, iodata->read.control_data.req_id, iodata->read.control_data.flags, MemBufferGetSize(conn_hnd->iodata.read.data_mb),
			conn_hnd->iodata.read.fd_arr.sz, listener_id, can_read_sz, read_bytes);

	/* Check if received CONTROL_PACKET wants ACK */
	if (EBIT_TEST(iodata->read.control_data.flags, COMM_UNIX_CONTROL_FLAGS_REQUEST))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Flag REQUEST set for ID [%d]\n", fd, iodata->read.control_data.req_id);

		/* Write back ACK to client */
		if (EBIT_TEST(iodata->read.control_data.flags, COMM_UNIX_CONTROL_FLAGS_WANT_ACK))
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Sending ACK for REQ_ID [%d]\n", fd, iodata->read.control_data.req_id);
			CommEvUNIXServerConnAIOBrbProtoACK(conn_hnd, iodata->read.control_data.req_id);
		}

		/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
		CommEvUNIXServerEventDispatchInternal(srv_ptr, conn_hnd, read_bytes, thrd_id, COMM_UNIX_SERVER_EVENT_READ);

		/* REMOVE ME */
		MemBufferClean(iodata->read.data_mb);
	}
	/* This is an ACK reply - There may be ADJACENT replies, process them all */
	else if (EBIT_TEST(iodata->read.control_data.flags, COMM_UNIX_CONTROL_FLAGS_REPLY))
	{
		/* Process control reply - Invoke ACK CALLBACKs is any is active */
		reply_count = CommEvUNIXIOControlDataProcess(ev_base, &conn_hnd->iodata, &iodata->read.control_data, read_bytes);
		conn_hnd->counters.reply_ack += reply_count;

		/* Calculate reply count */
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - Flag REPLY set for ID [%d] - [%d] Processed replies\n", fd,
				iodata->read.control_data.req_id, reply_count);

	}
	else
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - NO FLAG [%d] set for ID [%d]\n", fd,
				iodata->read.control_data.flags, iodata->read.control_data.req_id);


	/* Drained kernel buffer, reschedule and leave */
	if ( (can_read_sz - total_read) < (sizeof(CommEvUNIXControlData)))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - FINISH on REQ_ID [%d]! Drained [%d] of [%d] bytes of kernel buffer, reschedule\n",
				fd, iodata->read.control_data.req_id, total_read, can_read_sz);
	}
	/* Keep reading */
	else
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - CAN_READ [%d] - TOT_READ [%d] - READ_AGAIN!!\n", fd, (can_read_sz - total_read), total_read);
		goto read_again;
	}

	return total_read;
}
/**************************************************************************************************************************/
static int CommEvUNIXServerEventBrbProtoWriteACK(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DLinkedList *ack_slot_list;
	CommEvUNIXACKReply *ack_reply;
	int op_status;

	EvKQBase *ev_base							= base_ptr;
	CommEvUNIXServerConn *conn_hnd				= cb_data;
	CommEvUNIXServerListener *listener			= conn_hnd->listener;
	CommEvUNIXServer *srv_ptr					= conn_hnd->parent_srv;
	int total_wrote								= 0;
	int sill_can_write_sz						= 0;

	write_again:

	ack_slot_list	= &conn_hnd->iodata.write.ack_mem_slot.list[COMM_UNIX_LIST_PENDING_WRITE];

	/* Nothing left to write */
	if (MemSlotBaseIsEmptyList(&conn_hnd->iodata.write.ack_mem_slot, COMM_UNIX_LIST_PENDING_WRITE))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - ACK - Finish writing!\n", conn_hnd->socket_fd);

		/* No more pending write */
		if (total_wrote == 0)
			conn_hnd->flags.pending_write_ack = 0;
		return 0;
	}

	/* Calculate how many bytes we can still write on this IO loop */
	sill_can_write_sz = (can_write_sz - total_wrote);

	/* Too small window to write, bail out */
	if ((sizeof(CommEvUNIXControlData) + 16) > sill_can_write_sz)
	{
		/* Reschedule READ and WRITE event */
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoRead, conn_hnd);
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoWrite, conn_hnd);

		return total_wrote;
	}

	/* Point to next enqueued ACK reply */
	ack_reply = MemSlotBaseSlotData(ack_slot_list->head->data);

	/* Try to write ASAP */
	op_status = CommEvUNIXIOReplyACK(ack_reply, conn_hnd->socket_fd);

	/* Success writing */
	if (op_status > 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - Immediate write - [%d] bytes \n", conn_hnd->socket_fd, ack_reply->req_id, op_status);

		/* Touch total bytes written */
		total_wrote += op_status;

		/* Release write slot */
		ack_reply->flags.in_use		= 0;
		MemSlotBaseSlotFree(&conn_hnd->iodata.write.ack_mem_slot, ack_reply);
		goto write_again;
	}
	/* Failed to write on this IO loop, mark as in use */
	else
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Failed ACK REPLY\n", conn_hnd->socket_fd, ack_reply->req_id);
		ack_reply->flags.in_use		= 1;
		ack_reply->tx_retry_count++;

		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoWrite, conn_hnd);
	}

	/* Reschedule READ event */
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoRead, conn_hnd);
	return total_wrote;
}
/**************************************************************************************************************************/
static int CommEvUNIXServerEventBrbProtoWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	DLinkedList *slot_list;
	DLinkedList *ack_list;
	DLinkedListNode *node;
	CommEvUNIXWriteRequest *write_req;
	int op_status;
	int wrote_count;
	int write_delta;

	EvKQBase *ev_base							= base_ptr;
	CommEvUNIXServerConn *conn_hnd				= cb_data;
	CommEvUNIXServerListener *listener			= conn_hnd->listener;
	CommEvUNIXServer *srv_ptr					= conn_hnd->parent_srv;
	int control_data_sz							= sizeof(CommEvUNIXControlData);
	int total_wrote								= 0;
	int sill_can_write_sz						= 0;

	/* Label to keep writing */
	write_again:

	/* Point to MEM_SLOT list */
	slot_list	= &conn_hnd->iodata.write.req_mem_slot.list[COMM_UNIX_LIST_PENDING_WRITE];
	ack_list	= &conn_hnd->iodata.write.ack_mem_slot.list[COMM_UNIX_LIST_PENDING_WRITE];
	wrote_count	= 0;

	/* Before we can write further, first of all dispatch ALL pending ACKs */
	if (!MemSlotBaseIsEmptyList(&conn_hnd->iodata.write.ack_mem_slot, COMM_UNIX_LIST_PENDING_WRITE))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] - Will dispatch [%d] ACKs\n", conn_hnd->socket_fd, ack_list->size);

		/* Jump into ACK dispatch */
		CommEvUNIXServerEventBrbProtoWriteACK(fd, can_write_sz, thrd_id, cb_data, base_ptr);
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoWrite, conn_hnd);
		return 0;
	}

	/* Nothing left to write */
	if (MemSlotBaseIsEmptyList(&conn_hnd->iodata.write.req_mem_slot, COMM_UNIX_LIST_PENDING_WRITE))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_BLUE, "FD [%d] - Finish writing!\n", conn_hnd->socket_fd);

		/* Jump into ACK dispatch */
		CommEvUNIXServerEventBrbProtoWriteACK(fd, can_write_sz, thrd_id, cb_data, base_ptr);

		/* No more pending write */
		if (total_wrote == 0)
			conn_hnd->flags.pending_write_request = 0;
		return 0;
	}

	/* Point to next HEAD element and calculate how many bytes we can still write on this IO loop */
	write_req			= MemSlotBaseSlotData(slot_list->head->data);
	sill_can_write_sz	= (can_write_sz - total_wrote);

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] - REQ_ID [%d / %d] - Will write [%d] pending items - [%d] Already written\n",
			conn_hnd->socket_fd, write_req->req_id, write_req->data.seq_id, (slot_list->size - wrote_count), wrote_count);

	assert(write_req->flags.in_use);

	/* WRITE DATA TO UNIX SOCKET */
	op_status = CommEvUNIXIOWrite(ev_base, write_req, conn_hnd->socket_fd);

	/* Success writing */
	if (op_status > 0)
	{
		/* Calculate write delta */
		write_delta = (op_status - write_req->data.remain);

		/* Incomplete write, reschedule */
		if (write_req->data.remain > 0)
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - REQ_ID [%d] - Incomplete - written [%d] - data_sz [%d] - delta [%d] - remain [%d]\n",
					conn_hnd->socket_fd, write_req->req_id, op_status, write_req->data.size, write_delta, write_req->data.remain);

			goto tx_reschedule;
		}

		assert(0 == write_req->data.remain);
		total_wrote += op_status;

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - WROTE - [%d] bytes - TOTAL_WROTE [%d]\n", fd, write_req->req_id, op_status, total_wrote);

		conn_hnd->counters.req_scheduled--;
		assert(conn_hnd->counters.req_scheduled >= 0);

		/* Invoke finish CB */
		if (write_req->finish_cb)
			write_req->finish_cb(conn_hnd->socket_fd, op_status, -1, write_req, write_req->finish_cbdata);

		/* If we are not waiting for an ACK reply, release this slot right now. Otherwise, keep it until peer-side ACKs */
		if ((!write_req->ack_cb) && (!write_req->flags.autoclose_on_ack))
		{
			/* Not in use anymore */
			write_req->flags.in_use	= 0;

			MemSlotBaseSlotFree(&conn_hnd->iodata.write.req_mem_slot, write_req);
			conn_hnd->counters.req_sent_no_ack++;
		}
		/* We will keep this request pending for ACK. Mark it as WROTE and move it from PENDING_WRITE list so it wont get RE-SENT */
		else
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - REQ_ID [%d] - WANT ACK\n", conn_hnd->socket_fd, write_req->req_id);
			conn_hnd->counters.req_sent_with_ack++;
			write_req->flags.in_use		= 1;
			write_req->flags.wrote		= 1;

			/* Switch to PENDING_ACK list */
			MemSlotBaseSlotListIDSwitch(&conn_hnd->iodata.write.req_mem_slot, write_req->req_id, COMM_UNIX_LIST_PENDING_ACK);
		}

		/* Try to write the next request */
		goto write_again;
	}
	/* Too many retries, fail */
	else if (write_req->tx_retry_count > COMM_UNIX_MAX_TX_RETRY)
	{
		/* Invoke finish CB */
		if (write_req->finish_cb)
			write_req->finish_cb(conn_hnd->socket_fd, op_status, -1, write_req, write_req->finish_cbdata);

		/* Invoke ACK CB */
		if (write_req->ack_cb)
			write_req->ack_cb(COMM_UNIX_ACK_FAILED, write_req, write_req->ack_cbdata);

		/* Upper layers want we close local FDs after ACK */
		if (write_req->flags.autoclose_on_ack)
			CommEvUNIXAutoCloseLocalDescriptors(write_req);

		/* Not in use anymore */
		write_req->flags.in_use	= 0;

		/* Release write slot */
		MemSlotBaseSlotFree(&conn_hnd->iodata.write.req_mem_slot, write_req);
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoWrite, conn_hnd);
	}
	/* Failed to write on this IO loop, reschedule */
	else
	{
		tx_reschedule:

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - REQ_ID [%d] - ITER FAIL - [%d] bytes - TOTAL_WROTE [%d]\n",
				fd,  MemSlotBaseSlotGetID((char*)write_req), write_req->data.size, total_wrote);

		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoWrite, conn_hnd);
		write_req->tx_retry_count++;
	}

	/* Reschedule READ event */
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventBrbProtoRead, conn_hnd);

	return total_wrote;
}
/**************************************************************************************************************************/
static int CommEvUNIXServerEventCommonClose(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base					= base_ptr;
	CommEvUNIXServerConn *conn_hnd		= cb_data;
	CommEvUNIXServerListener *listener	= conn_hnd->listener;
	CommEvUNIXServer *srv_ptr			= conn_hnd->parent_srv;
	int listener_id						= listener->slot_id;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Client disconnected - Thread [%d] with listener ID [%d] - [%d] bytes in KERNEL\n",
			fd, thrd_id, listener_id, read_sz);

	/* Mark socket as EOF */
	conn_hnd->flags.socket_eof = 1;

	/* We have data pending for read, reschedule close event */
	if (read_sz > 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Not closing, there is data to read\n", fd);
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvUNIXServerEventCommonClose, conn_hnd);
		return 0;
	}

	/* Dispatch event before destroying IO structures to give a chance for the operator to use it */
	CommEvUNIXServerConnEventDispatchInternal(conn_hnd, read_sz, thrd_id, COMM_UNIX_SERVER_CONN_EVENT_CLOSE);
	CommEvUNIXServerConnClose(conn_hnd);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvUNIXServerEventDispatchInternal(CommEvUNIXServer *srv_ptr, CommEvUNIXServerConn *conn_hnd, int data_sz, int thrd_id, int ev_type)
{
	CommEvUNIXServerListener *listener;
	CommEvUNIXGenericCBH *cb_handler	= NULL;
	void *cb_handler_data				= NULL;

	/* Point to selected LISTENER */
	listener = conn_hnd ? conn_hnd->listener : 0;

	/* Grab callback_ptr */
	cb_handler = srv_ptr->events[listener->slot_id][ev_type].cb_handler_ptr;

	//printf("CommEvUNIXServerDispatchEvent - FD [%d] - LISTENER ID [%d] at FD [%d] - EV_TYPE [%d] - CB_H [%p]\n", conn_hnd->socket_fd, listener->slot_id, listener->socket_fd, ev_type, cb_handler);

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Grab data for this CBH */
		cb_handler_data = srv_ptr->events[listener->slot_id][ev_type].cb_data_ptr;

		/* Jump into CBH. Use conn_hnd fd if it exists, otherwise, send accept fd. Base for this event is CommEvUNIXServer* */
		cb_handler(conn_hnd ? conn_hnd->socket_fd : listener->socket_fd, data_sz, thrd_id, cb_handler_data, srv_ptr);
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvUNIXServerConnEventDispatchInternal(CommEvUNIXServerConn *conn_hnd, int data_sz, int thrd_id, int ev_type)
{
	CommEvUNIXGenericCBH *cb_handler	= NULL;
	void *cb_handler_data				= NULL;

	/* Grab callback_ptr */
	cb_handler = conn_hnd->events[ev_type].cb_handler_ptr;

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Grab data for this CBH */
		cb_handler_data = conn_hnd->events[ev_type].cb_data_ptr;

		/* Jump into CBH. Base for this event is CommEvUNIXServer* */
		cb_handler(conn_hnd->socket_fd, data_sz, thrd_id, cb_handler_data, conn_hnd->parent_srv);
	}

	return;
}
/**************************************************************************************************************************/


