/*
 * comm_tcp_server_conn.c
 *
 *  Created on: 2012-11-02
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2012 BrByte Software (Oliveira Alves & Amorim LTDA)
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

static EvBaseKQJobCBH CommEvTCPServerConnSSLShutdownJob;
static void CommEvTCPServerConnEnqueueAndKickQueue(CommEvTCPServerConn *conn_hnd, EvAIOReq *aio_req);
static void CommEvTCPServerConnIODoDataDestroy(CommEvTCPServerConn *conn_hnd);

static EvBaseKQCBH CommEvTCPServerConnSSLShutdown;
static CommEvUNIXGenericCBH CommEvTCPServerConnTransferFinishEvent;
static CommEvUNIXACKCBH CommEvTCPServerConnTransferACKEvent;
static EvBaseKQCBH CommEvTCPServerConnTransferGCTimer;

/**************************************************************************************************************************/
int CommEvTCPServerConnTransferViaUnixClientPool(CommEvTCPServerConn *conn_hnd, CommEvUNIXClientPool *unix_client_pool)
{
	int write_slot_id;

	/* Write and return */
	write_slot_id = CommEvTCPServerConnTransferViaUnixClientPoolWithMB(conn_hnd, unix_client_pool, conn_hnd->iodata.read_buffer);
	return write_slot_id;
}
/**************************************************************************************************************************/
int CommEvTCPServerConnTransferViaUnixClientPoolWithMB(CommEvTCPServerConn *conn_hnd, CommEvUNIXClientPool *unix_client_pool, MemBuffer *payload_mb)
{
	MemBuffer *conn_transfer_mb;
	int write_slot_id;

	CommEvTCPServer *tcp_srv	= conn_hnd->parent_srv;
	EvKQBase *ev_base			= tcp_srv->kq_base;
	EvBaseKQFileDesc *kq_fd		= EvKQBaseFDGrabFromArena(ev_base, conn_hnd->socket_fd);
	int listener_id				= conn_hnd->listener->slot_id;
	int transfer_ms				= tcp_srv->cfg[listener_id].timeout.transfer_ms;

	/* We CANT transfer more than 2k */
	conn_transfer_mb 			= MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

	if (!unix_client_pool->pool_conf.flags.no_brb_proto)
	{
		CommEvTCPServerConnTransferData conn_transfer_data;

		/* Clean up stack */
		memset(&conn_transfer_data, 0, sizeof(CommEvTCPServerConnTransferData));

		/* Populate transfer size data */
		conn_transfer_data.read_buffer_bytes	= MemBufferGetSize(payload_mb);
		conn_transfer_data.write_buffer_bytes	= EvAIOReqQueueGetQueueSize(&conn_hnd->iodata.write_queue);
		conn_transfer_data.read_pending_bytes	= kq_fd->defer.read.pending_bytes;
		conn_transfer_data.write_pending_bytes	= kq_fd->defer.write.pending_bytes;

		/* Populate transfer FLAGs */
		conn_transfer_data.flags.defering_read	= kq_fd->flags.defer_read;
		conn_transfer_data.flags.defering_write	= kq_fd->flags.defer_write;
		conn_transfer_data.flags.peek_on_read	= conn_hnd->flags.peek_on_read;
		conn_transfer_data.flags.ssl_enabled	= conn_hnd->flags.ssl_enabled;
		conn_transfer_data.flags.conn_read_mb	= (conn_transfer_data.read_buffer_bytes > 0 ? 1 : 0);

		/* Create a new TRANSFER_MB and attach READ_MB of CONN_HND into DATA */
		MemBufferAdd(conn_transfer_mb, &conn_transfer_data, sizeof(CommEvTCPServerConnTransferData));
	}

	MemBufferAdd(conn_transfer_mb, MemBufferDeref(payload_mb), MemBufferGetSize(payload_mb));

	/* Begin the actual FD transfer */
	write_slot_id 		= CommEvUNIXClientPoolAIOWrite(unix_client_pool, COMM_UNIX_SELECT_LEAST_LOAD, MemBufferDeref(conn_transfer_mb), MemBufferGetSize(conn_transfer_mb),
			(int*)&conn_hnd->socket_fd, 1, CommEvTCPServerConnTransferFinishEvent, CommEvTCPServerConnTransferACKEvent, conn_transfer_mb, conn_hnd);

	/* Failed writing */
	if (write_slot_id < 0)
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Unable to transfer - No more slots\n", conn_hnd->socket_fd);
		MemBufferDestroy(conn_transfer_mb);
		return -1;
	}

	KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - UNIX_TRANSFER begin on WRITE_SLOT [%d]\n", conn_hnd->socket_fd, write_slot_id);

	/* Clear all lower layer timeout and events */
	EvKQBaseClearEvents(ev_base, conn_hnd->socket_fd);
	EvKQBaseTimeoutClearAll(ev_base, conn_hnd->socket_fd);

	/* Cancel all upper layer timers and events */
	CommEvTCPServerConnTimersCancelAll(conn_hnd);
	CommEvTCPServerConnCancelEvents(conn_hnd);

	/* Add to transfer LIST, save TV, IO_N and set flags we are TRANSFERING this CONN_HND to another PROCESS */
	DLinkedListAdd(&tcp_srv->transfer.list, &conn_hnd->transfer.node, conn_hnd);
	memcpy(&conn_hnd->transfer.tv, &ev_base->stats.cur_invoke_tv, sizeof(struct timeval));
	conn_hnd->transfer.io_loop				= ev_base->stats.kq_invoke_count;
	conn_hnd->flags.conn_hnd_in_transfer	= 1;

	/* Fire up the garbage collector TIMER, if there is none */
	if ((transfer_ms > 0) && (tcp_srv->transfer.gc_timerid < 0))
		tcp_srv->transfer.gc_timerid = EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_VOLATILE, 1000, CommEvTCPServerConnTransferGCTimer, tcp_srv);

	return write_slot_id;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnCloseRequest(CommEvTCPServerConn *conn_hnd)
{
	EvKQBase *ev_base							= conn_hnd->parent_srv->kq_base;
	EvBaseKQFileDesc *kq_fd						= EvKQBaseFDGrabFromArena(ev_base, conn_hnd->socket_fd);
	EvBaseKQGenericEventPrototype *write_proto	= kq_fd ? &kq_fd->cb_handler[KQ_CB_HANDLER_WRITE] : NULL;

	/* Mark flags to close as soon as we finish writing all */
	conn_hnd->flags.close_request = 1;

	/* No write event set, close immediately */
	if ((!write_proto) || (!write_proto->flags.enabled))
		goto close;

	/* Write QUEUE is empty, close immediately */
	if (EvAIOReqQueueIsEmpty(&conn_hnd->iodata.write_queue))
		goto close;

	/* EOF flag already SET, close immediately */
	if (conn_hnd->flags.socket_eof)
		goto close;

	return;

	/* TAG for CLOSE */
	close:

	CommEvTCPServerConnClose(conn_hnd);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnClose(CommEvTCPServerConn *conn_hnd)
{
	CommEvTCPServer *srv_ptr = conn_hnd->parent_srv;
	int fd = conn_hnd->socket_fd;

	/* Already closed, bail out */
	if (conn_hnd->socket_fd < 0)
		return;

	/* SSL shutdown */
	if ((conn_hnd->flags.ssl_enabled) && (!conn_hnd->flags.socket_eof))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Begin SSL/TLS shutdown\n", conn_hnd->socket_fd);
		CommEvTCPServerConnSSLShutdownBegin(conn_hnd);
	}
	/* Destroy IO data, cancel any pending comm_events, destroy SSL session data */
	else
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF [%d] - CLOSE_REQ [%d] - Immediate shutdown\n",
				conn_hnd->socket_fd, conn_hnd->flags.socket_eof, conn_hnd->flags.close_request);

		/* We are NOT disconnecting with an EOF (we ASKED it ), so we do not want to receive write events with WRITE_SZ == -1 to tell us what we already know */
		//if ((!conn_hnd->flags.socket_eof) || (conn_hnd->flags.close_request))
		//{
		//	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Will cancel whole WRITE_QUEUE\n", conn_hnd->socket_fd);
		//	CommEvTCPServerConnCancelWriteQueue(conn_hnd);
		//}

		/* Invoke internal shutdown to clean up CONN_HND */
		CommEvTCPServerConnInternalShutdown(conn_hnd);
	}

	return;
}
/**************************************************************************************************************************/
int CommEvTCPServerConnTimersCancelAll(CommEvTCPServerConn *conn_hnd)
{
	/* DELETE all pending timers */
	if (conn_hnd->timers.calculate_datarate_id > -1)
		EvKQBaseTimerCtl(conn_hnd->parent_srv->kq_base, conn_hnd->timers.calculate_datarate_id, COMM_ACTION_DELETE);

	conn_hnd->timers.calculate_datarate_id		= -1;

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPServerConnAIOWriteVectored(CommEvTCPServerConn *conn_hnd, EvAIOReqIOVectorData *vector_table, int vector_table_sz, CommEvTCPServerCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReqIOVectorData *cur_iovec;
	EvAIOReq *aio_req;
	int i;

	/* Cannot write anymore, either CLOSE_REQUEST or IN_TRANSFER */
	if (!COMM_SERVER_CONN_CAN_WRITE(conn_hnd))
		return 0;

	/* Cannot enqueue anymore and CONN_UNLIMITED is disabled */
	if (!COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
		return 0;

	/* Write all vectors on table */
	for (i = 0; i < vector_table_sz; i++)
	{
		/* Grab current offset data */
		cur_iovec = &vector_table[i];

		/* Last WRITE, create with FINISH CB INFO */
		if ((i - 1) == vector_table_sz)
			aio_req = EvAIOReqNew(&conn_hnd->iodata.write_queue, conn_hnd->socket_fd, conn_hnd->parent_srv, (cur_iovec->data_ptr + cur_iovec->offset),
					cur_iovec->size, 0, NULL, finish_cb, finish_cbdata);
		/* Create NO_FINISH AIO_REQ for initial WRITEs*/
		else
			aio_req = EvAIOReqNew(&conn_hnd->iodata.write_queue, conn_hnd->socket_fd, conn_hnd->parent_srv, (cur_iovec->data_ptr + cur_iovec->offset),
					cur_iovec->size, 0, NULL, NULL, NULL);

		/* No more AIO slots, STOP */
		if (!aio_req)
			return 0;

		/* Set flags we are WRITING to a SOCKET */
		aio_req->flags.aio_write	= 1;
		aio_req->flags.aio_socket	= 1;

		/* Enqueue it and begin writing ASAP */
		CommEvTCPServerConnEnqueueAndKickQueue(conn_hnd, aio_req);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPServerConnAIOWriteAndDestroyMemBufferOffset(CommEvTCPServerConn *conn_hnd, MemBuffer *mem_buf, int offset, CommEvTCPServerCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;
	int mem_buf_sz;

	/* Cannot write anymore, either CLOSE_REQUEST or IN_TRANSFER */
	if (!COMM_SERVER_CONN_CAN_WRITE(conn_hnd))
		return 0;

	/* Cannot enqueue anymore and CONN_UNLIMITED is disabled */
	if (!COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
		return 0;

	mem_buf_sz = MemBufferGetSize(mem_buf);

	/* Do not try to write past the end of buffer */
	if (mem_buf_sz < offset)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&conn_hnd->iodata.write_queue, conn_hnd->socket_fd, conn_hnd->parent_srv, MemBufferOffsetDeref(mem_buf, offset), (MemBufferGetSize(mem_buf) - offset), 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Populate destroy data */
	aio_req->destroy_func		= (EvAIOReqDestroyFunc*)MemBufferDestroy;
	aio_req->destroy_cbdata		= mem_buf;

	/* Enqueue it and begin writing ASAP */
	CommEvTCPServerConnEnqueueAndKickQueue(conn_hnd, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPServerConnAIOWriteAndDestroyMemBuffer(CommEvTCPServerConn *conn_hnd, MemBuffer *mem_buf, CommEvTCPServerCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Cannot write anymore, either CLOSE_REQUEST or IN_TRANSFER */
	if (!COMM_SERVER_CONN_CAN_WRITE(conn_hnd))
		return 0;

	/* Cannot enqueue anymore and CONN_UNLIMITED is disabled */
	if (!COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&conn_hnd->iodata.write_queue, conn_hnd->socket_fd, conn_hnd->parent_srv, MemBufferDeref(mem_buf), MemBufferGetSize(mem_buf), 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Populate destroy data */
	aio_req->destroy_func		= (EvAIOReqDestroyFunc*)MemBufferDestroy;
	aio_req->destroy_cbdata		= mem_buf;

	/* Enqueue it and begin writing ASAP */
	CommEvTCPServerConnEnqueueAndKickQueue(conn_hnd, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPServerConnAIOWriteMemBuffer(CommEvTCPServerConn *conn_hnd, MemBuffer *mem_buf, CommEvTCPServerCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Cannot write anymore, either CLOSE_REQUEST or IN_TRANSFER */
	if (!COMM_SERVER_CONN_CAN_WRITE(conn_hnd))
		return 0;

	/* Cannot enqueue anymore and CONN_UNLIMITED is disabled */
	if (!COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&conn_hnd->iodata.write_queue, conn_hnd->socket_fd, conn_hnd->parent_srv, MemBufferDeref(mem_buf), MemBufferGetSize(mem_buf), 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue it and begin writing ASAP */
	CommEvTCPServerConnEnqueueAndKickQueue(conn_hnd, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPServerConnAIOWriteStringFmt(CommEvTCPServerConn *conn_hnd, CommEvTCPServerCBH *finish_cb, void *finish_cbdata, char *string, ...)
{
	EvAIOReq *aio_req;
	va_list args;
	char *buf_ptr;
	int buf_sz;
	int msg_len;

	/* Cannot write anymore, either CLOSE_REQUEST or IN_TRANSFER */
	if (!COMM_SERVER_CONN_CAN_WRITE(conn_hnd))
		return 0;

	/* Cannot enqueue anymore and CONN_UNLIMITED is disabled */
	if (!COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
		return 0;

	/* Probe message size */
	va_start(args, string);
	msg_len = vsnprintf(NULL, 0, string, args);
	va_end(args);

	/* Create a new buffer to hold it */
	buf_ptr = malloc(msg_len + 16);

	/* Write it into buffer and NULL terminate it */
	va_start( args, string );
	buf_sz = vsnprintf(buf_ptr, (msg_len + 1), string, args);
	buf_ptr[buf_sz] = '\0';
	va_end(args);

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&conn_hnd->iodata.write_queue, conn_hnd->socket_fd, conn_hnd->parent_srv, buf_ptr, buf_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
	{
		free(buf_ptr);
		return 0;
	}

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue it and begin writing ASAP */
	CommEvTCPServerConnEnqueueAndKickQueue(conn_hnd, aio_req);
	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPServerConnAIOWriteString(CommEvTCPServerConn *conn_hnd, char *string, CommEvTCPServerCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;
	int string_sz = strlen(string);

	/* Cannot write anymore, either CLOSE_REQUEST or IN_TRANSFER */
	if (!COMM_SERVER_CONN_CAN_WRITE(conn_hnd))
		return 0;

	/* Cannot enqueue anymore and CONN_UNLIMITED is disabled */
	if (!COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&conn_hnd->iodata.write_queue, conn_hnd->socket_fd, conn_hnd->parent_srv, strdup(string), string_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue it and begin writing ASAP */
	CommEvTCPServerConnEnqueueAndKickQueue(conn_hnd, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPServerConnAIOWrite(CommEvTCPServerConn *conn_hnd, char *data, unsigned long data_sz, CommEvTCPServerCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Cannot write anymore, either CLOSE_REQUEST or IN_TRANSFER */
	if (!COMM_SERVER_CONN_CAN_WRITE(conn_hnd))
		return 0;

	/* Cannot enqueue anymore and CONN_UNLIMITED is disabled */
	if (!COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&conn_hnd->iodata.write_queue, conn_hnd->socket_fd, conn_hnd->parent_srv, data, data_sz, 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue it and begin writing ASAP */
	CommEvTCPServerConnEnqueueAndKickQueue(conn_hnd, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPServerConnSSLShutdownBegin(CommEvTCPServerConn *conn_hnd)
{
	CommEvTCPServer *parent_srv = conn_hnd->parent_srv;

	/* Already shutting down, bail out */
	if (conn_hnd->flags.ssl_shuting_down)
		return 0;

	/* Set flags as shutting down */
	conn_hnd->flags.ssl_shuting_down = 1;

	/* Schedule SSL shutdown JOB for NEXT IO LOOP */
	conn_hnd->ssldata.shutdown_jobid = EvKQJobsAdd(parent_srv->kq_base, JOB_ACTION_ADD_VOLATILE, 0, CommEvTCPServerConnSSLShutdownJob, conn_hnd);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPServerConnInternalShutdown(CommEvTCPServerConn *conn_hnd)
{
	CommEvTCPServer *srv_ptr	= conn_hnd->parent_srv;
	int socket_fd				= conn_hnd->socket_fd;

	/* Already closed, bail out */
	if (conn_hnd->socket_fd < 0)
		return 0;

	/* Shit will happen if we disappear with an IN_TRANSFER CONN_HND */
	if (conn_hnd->flags.conn_hnd_in_transfer)
		return 0;

	/* Cancel any pending SSL SHUTDOWN JOB */
	EvKQJobsCtl(srv_ptr->kq_base, JOB_ACTION_DELETE, conn_hnd->ssldata.shutdown_jobid);
	conn_hnd->ssldata.shutdown_jobid = -1;

	/* Delete client from ACTIVE list */
	DLinkedListDelete(&srv_ptr->conn.list, &conn_hnd->conn_node);

	/* Clean UP */
	CommEvTCPServerConnTimersCancelAll(conn_hnd);
	CommEvTCPServerConnIODataDestroy(conn_hnd);
	CommEvTCPServerConnCancelEvents(conn_hnd);
	CommEvTCPServerConnSSLSessionDestroy(conn_hnd);

	/* Close socket and set to -1 */
	EvKQBaseSocketClose(srv_ptr->kq_base, conn_hnd->socket_fd);
	conn_hnd->socket_fd = -1;

	/*  Reset shutdown retry count and flags */
	conn_hnd->ssldata.ssl_shutdown_trycount = 0;
	conn_hnd->flags.ssl_shuting_down 		= 0;
	conn_hnd->flags.conn_hnd_inuse			= 0;

	/* Release CONN_HND from ARENA */
	MemArenaReleaseByID(srv_ptr->conn.arena, socket_fd);
	return 1;
}
/**************************************************************************************************************************/
/* CONN event interface
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
void CommEvTCPServerConnCancelWriteQueue(CommEvTCPServerConn *conn_hnd)
{
	EvAIOReqQueue *write_queue = &conn_hnd->iodata.write_queue;

	/* Set the whole queue as canceled */
	write_queue->flags.cancelled = 1;
	return;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnEventCancel(CommEvTCPServerConn *conn_hnd, CommEvTCPServerConnEventCodes ev_type)
{
	/* Sanity check */
	if (ev_type >= CONN_EVENT_LASTITEM)
		return;

	/* Clear event data */
	conn_hnd->events[ev_type].cb_handler_ptr	= NULL;
	conn_hnd->events[ev_type].cb_data_ptr		= NULL;
	conn_hnd->events[ev_type].flags.enabled		= 0;

	/* Update underlying event */
	if (CONN_EVENT_READ == ev_type)
		EvKQBaseSetEvent(conn_hnd->parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnCancelEvents(CommEvTCPServerConn *conn_hnd)
{
	int i;

	/* Cancel all events */
	for (i = 0; i < CONN_EVENT_LASTITEM; i++)
		CommEvTCPServerConnEventCancel(conn_hnd, i);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnSetDefaultEvents(CommEvTCPServer *srv_ptr, CommEvTCPServerConn *conn_hnd)
{
	int listener_id = conn_hnd->listener->slot_id;

	/* Default READ event */
	conn_hnd->events[CONN_EVENT_READ].cb_handler_ptr				= srv_ptr->events[listener_id][COMM_SERVER_EVENT_DEFAULT_READ].cb_handler_ptr;
	conn_hnd->events[CONN_EVENT_READ].cb_data_ptr					= srv_ptr->events[listener_id][COMM_SERVER_EVENT_DEFAULT_READ].cb_data_ptr;

	/* Default CLOSE event */
	conn_hnd->events[CONN_EVENT_CLOSE].cb_handler_ptr				= srv_ptr->events[listener_id][COMM_SERVER_EVENT_DEFAULT_CLOSE].cb_handler_ptr;
	conn_hnd->events[CONN_EVENT_CLOSE].cb_data_ptr					= srv_ptr->events[listener_id][COMM_SERVER_EVENT_DEFAULT_CLOSE].cb_data_ptr;

	/* Default SSL_HANDSHAKE_FAIL event */
	conn_hnd->events[CONN_EVENT_SSL_HANDSHAKE_FAIL].cb_handler_ptr	= srv_ptr->events[listener_id][COMM_SERVER_EVENT_ACCEPT_SSL_HANDSHAKE_FAIL].cb_handler_ptr;
	conn_hnd->events[CONN_EVENT_SSL_HANDSHAKE_FAIL].cb_data_ptr		= srv_ptr->events[listener_id][COMM_SERVER_EVENT_ACCEPT_SSL_HANDSHAKE_FAIL].cb_data_ptr;

	return;

}
/**************************************************************************************************************************/
void CommEvTCPServerConnSetEvent(CommEvTCPServerConn *conn_hnd, CommEvTCPServerConnEventCodes ev_type, CommEvTCPServerCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= CONN_EVENT_LASTITEM)
		return;

	/* Set event */
	conn_hnd->events[ev_type].cb_handler_ptr	= cb_handler;
	conn_hnd->events[ev_type].cb_data_ptr		= cb_data;
	conn_hnd->events[ev_type].flags.enabled		= 1;

	/* Read event may have not been set, so reschedule when UPPER LAYER read event is set */
	if ((CONN_EVENT_READ == ev_type) && cb_handler)
		CommEvTCPServerConnReadReschedule(conn_hnd);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnDispatchEventByFD(CommEvTCPServer *srv_ptr, int fd, int data_sz, int thrd_id, int ev_type)
{
	CommEvTCPServerConn *conn_hnd	= NULL;
	EvBaseKQCBH *cb_handler			= NULL;
	void *cb_handler_data			= NULL;

	/* Grab a connection handler from server internal arena */
	conn_hnd = CommEvTCPServerConnArenaGrab(srv_ptr, fd);

	/* Dispatch */
	CommEvTCPServerConnDispatchEvent(conn_hnd, data_sz, thrd_id, ev_type);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnDispatchEvent(CommEvTCPServerConn *conn_hnd, int data_sz, int thrd_id, int ev_type)
{
	CommEvTCPServer *srv_ptr			= conn_hnd->parent_srv;
	CommEvTCPServerCBH *cb_handler		= NULL;
	void *cb_handler_data				= NULL;

	/* Grab callback_ptr */
	cb_handler = conn_hnd->events[ev_type].cb_handler_ptr;
	memcpy(&conn_hnd->events[ev_type].last_tv, &conn_hnd->parent_srv->kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - EV_TYPE [%d] - Will jump to CBH at [%p]\n", conn_hnd->socket_fd, ev_type, cb_handler);

		/* Grab data for this CBH and jump into CBH. Base for this event is CommEvTCPServer */
		cb_handler_data = conn_hnd->events[ev_type].cb_data_ptr;
		cb_handler(conn_hnd->socket_fd, data_sz, thrd_id, cb_handler_data, conn_hnd->parent_srv);
	}
	else
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - EV_TYPE [%d] - Not dispatched - No CB_H\n", conn_hnd->socket_fd, ev_type);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* Context arena control functions
/**************************************************************************************************************************/
CommEvTCPServerConn *CommEvTCPServerConnArenaGrab(CommEvTCPServer *srv_ptr, int fd)
{
	CommEvTCPServerConn *conn_hnd;

	if (fd <= 0)
		return NULL;

	/* Grab, initialize and return */
	conn_hnd				= MemArenaGrabByID(srv_ptr->conn.arena, fd);
	conn_hnd->parent_srv	= srv_ptr;
	return conn_hnd;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnArenaNew(CommEvTCPServer *srv_ptr)
{
	EvKQBase *ev_base = srv_ptr->kq_base;

	BRB_ASSERT (ev_base, (!srv_ptr->conn.arena), "Trying to REINIT CONN_HND arena!\n");

	/* Initialize CONN_HND arena and list */
	srv_ptr->conn.arena = MemArenaNew(1024, (sizeof(CommEvTCPServerConn) + 1), 128, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&srv_ptr->conn.list, BRBDATA_THREAD_UNSAFE);
	return;
}
/**************************************************************************************************************************/
void CommEvTCPServerConnArenaDestroy(CommEvTCPServer *srv_ptr)
{
	/* Sanity check */
	if (!srv_ptr || !srv_ptr->conn.arena)
		return;

	/* Destroy and set to NULL */
	MemArenaDestroy(srv_ptr->conn.arena);
	srv_ptr->conn.arena = NULL;
	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* SSL support functions
/**************************************************************************************************************************/
void CommEvTCPServerConnSSLSessionInit(CommEvTCPServerConn *ret_conn)
{
	CommEvTCPServer *srv_ptr			= ret_conn->parent_srv;
	CommEvTCPServerListener *listener	= ret_conn->listener;
	int listener_id						= listener->slot_id;
	int sign_status;

	if (!ret_conn)
		return;

	/* SSL already INITIALIZED, bail out */
	if (ret_conn->flags.ssl_init)
		return;

	/* Mark INIT */
	ret_conn->flags.ssl_init 					= 1;

	/* Upper layers generated a custom certificate for this connection, use it */
	if (ret_conn->ssldata.x509_cert)
	{
		ret_conn->ssldata.ssl_context	= SSL_CTX_new(SSLv23_server_method());

		/* Error */
		if (!ret_conn->ssldata.ssl_context)
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed creating new context\n", ret_conn->socket_fd);

			/* Close this connection */
			CommEvTCPServerConnClose(ret_conn);
			return;
		}

		/* Use provided certificate */
		if (!SSL_CTX_use_certificate(ret_conn->ssldata.ssl_context, ret_conn->ssldata.x509_cert))
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed binding X.509 to context\n", ret_conn->socket_fd);

			/* Close this connection */
			CommEvTCPServerConnClose(ret_conn);
			return;
		}

		/* Set private key to be used with this fake certificate */
		SSL_CTX_use_PrivateKey(ret_conn->ssldata.ssl_context, srv_ptr->ssldata.main_key);

		/* Create context and set flags */
		ret_conn->ssldata.ssl_handle	= SSL_new(ret_conn->ssldata.ssl_context);
		ret_conn->flags.ssl_cert_custom = 1;

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Using custom certificate for TLS VHOST [%s]\n", CommEvTCPServerConnSSLDataGetSNIStr(ret_conn));
	}

	/* Create SSL context base on default server certificate */
	else
	{
		ret_conn->ssldata.ssl_handle = (listener->ssldata.ssl_context ? SSL_new (listener->ssldata.ssl_context) : NULL);
	}

	ret_conn->ssldata.ssl_negotiatie_trycount	= 0;

	/* Bind FD to SSL context */
	if (ret_conn->ssldata.ssl_handle)
		SSL_set_fd(ret_conn->ssldata.ssl_handle, ret_conn->socket_fd);

	return;

}
/**************************************************************************************************************************/
void CommEvTCPServerConnSSLSessionDestroy(CommEvTCPServerConn *ret_conn)
{
	CommEvTCPServer *srv_ptr;
	int listener_id = ret_conn->listener->slot_id;

	if (!ret_conn)
		return;

	srv_ptr = ret_conn->parent_srv;

	/* Clean up X509 certificate data */
	if (ret_conn->ssldata.x509_cert)
	{
		/* Destroy certificate is asked by upper layers */
		if (ret_conn->flags.ssl_cert_destroy_onclose)
			X509_free(ret_conn->ssldata.x509_cert);

		ret_conn->ssldata.x509_cert = NULL;

		/* Destroy private context */
		SSL_CTX_free(ret_conn->ssldata.ssl_context);
		ret_conn->ssldata.ssl_context = NULL;
	}

	/* Release SSL related objects */
	if (ret_conn->ssldata.ssl_handle)
	{
		SSL_free (ret_conn->ssldata.ssl_handle);
		ret_conn->ssldata.ssl_handle = NULL;
	}

	/* Reset SNI data */
	if (ret_conn->ssldata.sni_host_ptr)
	{
		free(ret_conn->ssldata.sni_host_ptr);
		ret_conn->ssldata.sni_host_ptr = NULL;
	}

	/* Mark NOT INIT */
	ret_conn->flags.ssl_init = 0;

	return;
}
/**************************************************************************************************************************/
char *CommEvTCPServerConnSSLDataGetSNIStr(CommEvTCPServerConn *conn_hnd)
{
	/* Sanity check */
	if (!conn_hnd->ssldata.sni_host_ptr)
		return "";

	return conn_hnd->ssldata.sni_host_ptr;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int CommEvTCPServerConnIODataLock(CommEvTCPServerConn *conn_hnd)
{
	/* Sanity check */
	if (!conn_hnd)
		return 0;

	COMM_SERVER_CONN_AIO_MUTEX_LOCK(conn_hnd)

	/* Increment ref_count */
	conn_hnd->iodata.ref_count++;

	COMM_SERVER_CONN_AIO_MUTEX_UNLOCK(conn_hnd)

	return conn_hnd->iodata.ref_count;
}
/**************************************************************************************************************************/
int CommEvTCPServerConnIODataUnlock(CommEvTCPServerConn *conn_hnd)
{
	int destroyed;

	/* Sanity check */
	if (!conn_hnd)
		return 0;

	destroyed = CommEvTCPServerConnIODataDestroy(conn_hnd);

	return (destroyed ? 0 : 1);
}
/**************************************************************************************************************************/
int CommEvTCPServerConnIODataDestroy(CommEvTCPServerConn *conn_hnd)
{
	COMM_SERVER_CONN_AIO_MUTEX_LOCK(conn_hnd);

	/* If zero or below, destroy */
	if (conn_hnd->iodata.ref_count <= 0)
	{
		CommEvTCPServerConnIODoDataDestroy(conn_hnd);
		return 1;
	}
	/* Decrement ref_count and leave */
	else
	{
		conn_hnd->iodata.ref_count--;
	}

	COMM_SERVER_CONN_AIO_MUTEX_UNLOCK(conn_hnd);

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTCPServerConnSSLShutdownJob(void *job, void *cbdata_ptr)
{
	CommEvTCPServerConn *conn_hnd	= cbdata_ptr;
	CommEvTCPServer *parent_srv		= conn_hnd->parent_srv;

	/* Reset JOB_ID */
	conn_hnd->ssldata.shutdown_jobid = -1;

	/* Clean any pending TIMEOUT */
	EvKQBaseTimeoutClearAll(parent_srv->kq_base, conn_hnd->socket_fd);

	/* Now remove any READ/WRITE events, then remove DEFER and only then restore READ/WRITE events. This will avoid wandering events from being dispatched to SSL_SHUTDOWN */
	EvKQBaseSetEvent(parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);
	EvKQBaseSetEvent(parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_DELETE, NULL, NULL);

	/* Cancel LOWER LEVEL DEFER_READ and DEFER_WRITE events */
	EvKQBaseSetEvent(parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_DEFER_CHECK_READ, COMM_ACTION_DELETE, NULL, NULL);
	EvKQBaseSetEvent(parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_DEFER_CHECK_WRITE, COMM_ACTION_DELETE, NULL, NULL);

	/* Redirect READ/WRITE events to SSL_SHUTDOWN */
	EvKQBaseSetEvent(parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerConnSSLShutdown, conn_hnd);
	EvKQBaseSetEvent(parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerConnSSLShutdown, conn_hnd);

	/* Fire up SSL_shutdown sequence */
	conn_hnd->ssldata.ssl_shutdown_trycount = 0;
	CommEvTCPServerConnSSLShutdown(conn_hnd->socket_fd, 0, -1, conn_hnd, parent_srv->kq_base);

	return 1;
}
/**************************************************************************************************************************/
static void CommEvTCPServerConnEnqueueAndKickQueue(CommEvTCPServerConn *conn_hnd, EvAIOReq *aio_req)
{
	CommEvTCPServer *tcp_srv	= conn_hnd->parent_srv;
	EvAIOReqQueue *write_queue	= &conn_hnd->iodata.write_queue;
	int listener_id				= conn_hnd->listener->slot_id;

	/* Cannot write anymore, either CLOSE_REQUEST or IN_TRANSFER */
	if (!COMM_SERVER_CONN_CAN_WRITE(conn_hnd))
	{
		EvAIOReqInvokeCallBacks(aio_req, 1, aio_req->fd, -1, -1, aio_req->parent_ptr);
		EvAIOReqDestroy(aio_req);
		return;
	}

	/* Cannot enqueue anymore */
	if (!COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd))
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Cannot enqueue - [%d] already queued\n",
				conn_hnd->socket_fd, write_queue->stats.queue_sz);

		EvAIOReqInvokeCallBacks(aio_req, 1, aio_req->fd, -1, -1, aio_req->parent_ptr);
		EvAIOReqDestroy(aio_req);
		return;
	}

	/* Allow upper layers to transform data */
	EvAIOReqTransform_WriteData(&conn_hnd->transform, write_queue, aio_req);

	/* Enqueue it in conn_queue */
	EvAIOReqQueueEnqueue(write_queue, aio_req);

	/* Ask the event base to write */
	CommEvTCPServerKickConnWriteQueue(conn_hnd);

	return;
}
/**************************************************************************************************************************/
static void CommEvTCPServerConnIODoDataDestroy(CommEvTCPServerConn *conn_hnd)
{
	/* Clean any pending write event, keep just write queue and MUTEXEs */
	EvAIOReqQueueClean(&conn_hnd->iodata.write_queue);

	/* Destroy any data buffered on read_stream or mem_buffer. This will be re-created later, once the new slot users receives a read_event */
	if (conn_hnd->iodata.read_stream)
		MemStreamDestroy(conn_hnd->iodata.read_stream);

	if (conn_hnd->iodata.partial_read_stream)
		MemStreamDestroy(conn_hnd->iodata.partial_read_stream);

	if (conn_hnd->iodata.read_buffer)
		MemBufferDestroy(conn_hnd->iodata.read_buffer);

	if (conn_hnd->iodata.partial_read_buffer)
		MemBufferDestroy(conn_hnd->iodata.partial_read_buffer);

	conn_hnd->iodata.read_stream			= NULL;
	conn_hnd->iodata.read_buffer			= NULL;
	conn_hnd->iodata.partial_read_buffer	= NULL;
	conn_hnd->iodata.partial_read_stream	= NULL;

	return;
}
/**************************************************************************************************************************/
static int CommEvTCPServerConnSSLShutdown(int fd, int can_data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	char junk_buf[1024];
	int shutdown_state;
	int ssl_error;

	EvKQBase *ev_base				= base_ptr;
	CommEvTCPServerConn *conn_hnd	= cb_data;
	CommEvTCPServer *srv_ptr		= conn_hnd->parent_srv;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Trying to shut down\n", conn_hnd->socket_fd);

	/* EOF set from PEER_SIDE, bail out */
	if (kq_fd->flags.so_read_eof || kq_fd->flags.so_write_eof)
	{
		conn_hnd->flags.socket_eof = 1;
		goto do_shutdown;
	}

	/* SSL handle disappeared, bail out */
	if (!conn_hnd->ssldata.ssl_handle)
		goto do_shutdown;

	/* Too many shutdown retries, bail out */
	if (conn_hnd->ssldata.ssl_shutdown_trycount++ > 10)
		goto do_shutdown;

	/* Not yet initialized, bail out */
	if (!SSL_is_init_finished(conn_hnd->ssldata.ssl_handle))
		goto do_shutdown;

	/* Clear SSL error queue and invoke shutdown */
	ERR_clear_error();
	shutdown_state = SSL_shutdown(conn_hnd->ssldata.ssl_handle);

	/* Check shutdown STATE - https://www.openssl.org/docs/ssl/SSL_shutdown.html */
	switch(shutdown_state)
	{
	/* The shutdown was not successful because a fatal error occurred either at the protocol level or a connection failure occurred. It can also occur if action is need to continue
	 * the operation for non-blocking BIOs. Call SSL_get_error(3) with the return value ret to find out the reason. */
	case -1:
	{
		goto check_error;
	}

	/* The shutdown is not yet finished. Call SSL_shutdown() for a second time, if a bidirectional shutdown shall be performed. The output of SSL_get_error(3) may
	 * be misleading, as an erroneous SSL_ERROR_SYSCALL may be flagged even though no error occurred. */
	case 0:
	{
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerConnSSLShutdown, conn_hnd);
		return 0;
	}
	/*	The shutdown was successfully completed. The ``close notify'' alert was sent and the peer's ``close notify'' alert was received. */
	case 1:
	default:
		goto do_shutdown;
	}

	/* TAG to examine SSL error and decide what to do */
	check_error:

	/* Grab error from SSL */
	ssl_error = SSL_get_error(conn_hnd->ssldata.ssl_handle, shutdown_state);

	switch (ssl_error)
	{
	case SSL_ERROR_WANT_READ:
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_WANT_READ\n", conn_hnd->socket_fd);

		EvKQBaseSetEvent(srv_ptr->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerConnSSLShutdown, conn_hnd);
		return 0;
	}
	case SSL_ERROR_WANT_WRITE:
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_WANT_WRITE\n", conn_hnd->socket_fd);
		EvKQBaseSetEvent(srv_ptr->kq_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerConnSSLShutdown, conn_hnd);
		return 0;
	}
	case SSL_ERROR_ZERO_RETURN:
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_ZERO_RETURN\n", conn_hnd->socket_fd);
		EvKQBaseSetEvent(srv_ptr->kq_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerConnSSLShutdown, conn_hnd);
		return 0;
	}

	case SSL_ERROR_SYSCALL:
	case SSL_ERROR_SSL:
	default:
		goto do_shutdown;
	}

	return 0;

	do_shutdown:

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF [%d] - SSL_FATAL [%d] Shutting down\n",
			conn_hnd->socket_fd, conn_hnd->flags.socket_eof, conn_hnd->flags.ssl_fatal_error);

	/* Either EOF or SSL fatal error, dispatch upper layer close event */
	if (((conn_hnd->flags.socket_eof) || (conn_hnd->flags.ssl_fatal_error)) && (!conn_hnd->flags.close_request))
		CommEvTCPServerConnDispatchEventByFD(conn_hnd->parent_srv, conn_hnd->socket_fd, 0, thrd_id, CONN_EVENT_CLOSE);

	/* Invoke internal shutdown to clean up CONN_HND */
	CommEvTCPServerConnInternalShutdown(conn_hnd);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTCPServerConnTransferFinishEvent(int fd, int write_sz, int thread_id, void *pend_req_ptr, void *cb_data)
{
	CommEvUNIXWriteRequest *write_req	= pend_req_ptr;
	MemBuffer *transfer_data_mb			= cb_data;
	CommEvUNIXClient *unix_client		= write_req->parent_ptr;
	CommEvTCPServerConn *conn_hnd		= write_req->ack_cbdata;
	CommEvTCPServer *srv_ptr			= conn_hnd->parent_srv;

	/* Destroy transfer data MB */
	MemBufferDestroy(transfer_data_mb);

	/* Write failed, shutdown CONN_HND */
	if (write_sz < 0)
	{
		BRB_ASSERT_FMT(srv_ptr->kq_base, (conn_hnd->flags.conn_hnd_in_transfer), "Received TRANSFER FAIL for CONN_HND [%d] - Not in transfer\n", conn_hnd->socket_fd);

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - UNIX_TRANSFER failed on WRITE_SLOT [%d]\n", conn_hnd->socket_fd, write_req->req_id);

		/* Clean up from TRANSFER data */
		DLinkedListDelete(&srv_ptr->transfer.list, &conn_hnd->transfer.node);
		memset(&conn_hnd->transfer.tv, 0, sizeof(struct timeval));
		conn_hnd->transfer.io_loop				= -1;
		conn_hnd->flags.conn_hnd_in_transfer	= 0;

		/* Invoke internal shutdown to clean up CONN_HND */
		CommEvTCPServerConnInternalShutdown(conn_hnd);
	}

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - UNIX_TRANSFER OK on WRITE_SLOT [%d] - Waiting ACK\n", conn_hnd->socket_fd, write_req->req_id);
	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerConnTransferACKEvent(int ack_code, void *pend_req_ptr, void *cb_data)
{
	CommEvUNIXWriteRequest *write_req	= pend_req_ptr;
	CommEvUNIXClient *unix_client		= write_req->parent_ptr;
	CommEvTCPServerConn *conn_hnd		= cb_data;
	CommEvTCPServer *srv_ptr			= conn_hnd->parent_srv;
	int pending_reply_count				= (unix_client->counters.req_sent_with_ack - unix_client->counters.reply_ack);

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "UNIX/CONN FD [%d]-[%d] - UNIX_CLID [%d] - PENDING [%d] - ACK_CODE [%d]\n",
			unix_client->socket_fd, conn_hnd->socket_fd, unix_client->cli_id_onpool, pending_reply_count, ack_code);

	/* This SHOULD NEVER HAPPEN */
	if (conn_hnd->socket_fd < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "UNIX/CONN FD [%d]-[%d] - Received ACK for CLOSED CLIENT\n", unix_client->socket_fd, conn_hnd->socket_fd);
		return 0;

	}

	BRB_ASSERT_FMT(srv_ptr->kq_base, (conn_hnd->flags.conn_hnd_in_transfer), "Received ACK for CONN_HND [%d] - Not in transfer\n", conn_hnd->socket_fd);

	/* Clean up from TRANSFER data */
	DLinkedListDelete(&srv_ptr->transfer.list, &conn_hnd->transfer.node);
	memset(&conn_hnd->transfer.tv, 0, sizeof(struct timeval));
	conn_hnd->transfer.io_loop				= -1;
	conn_hnd->flags.conn_hnd_in_transfer	= 0;

	/* Invoke internal shutdown to clean up CONN_HND */
	CommEvTCPServerConnInternalShutdown(conn_hnd);
	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerConnTransferGCTimer(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServerConn *conn_hnd;
	int listener_id;
	int timeout_ms;

	CommEvTCPServer *srv_ptr			= cb_data;
	EvKQBase *ev_base					= srv_ptr->kq_base;
	DLinkedListNode *node				= srv_ptr->transfer.list.head;
	int timeout_count					= 0;
	int delta_ms						= 0;

	/* Reset TIMED_ID */
	srv_ptr->transfer.gc_timerid = -1;

	/* Transfer list is empty, leave */
	if (DLINKED_LIST_ISEMPTY(srv_ptr->transfer.list))
		return 0;

	/* Walk list of PENDING TRANSFER of CONN_HND */
	while (1)
	{
		/* No node, stop */
		if (!node)
			break;

		/* Grab CONN_HND and point to next node */
		conn_hnd		= node->data;
		node			= node->next;

		BRB_ASSERT(ev_base, (conn_hnd->flags.conn_hnd_in_transfer), "CONN_HND not in transfer being processed in TRANSFER_GC\n");

		/* Grab CONN_HND specific DATA */
		listener_id		= conn_hnd->listener->slot_id;
		timeout_ms		= srv_ptr->cfg[listener_id].timeout.transfer_ms;
		delta_ms		= EvKQBaseTimeValSubMsec(&conn_hnd->transfer.tv, &ev_base->stats.cur_invoke_tv);

		/* Entry TIMEDOUT */
		if (delta_ms >= timeout_ms)
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - UNIX_TRANSFER timed out with delta [%d / %d]\n",
					conn_hnd->socket_fd, delta_ms, timeout_ms);

			/* Clean up from TRANSFER data */
			DLinkedListDelete(&srv_ptr->transfer.list, &conn_hnd->transfer.node);
			memset(&conn_hnd->transfer.tv, 0, sizeof(struct timeval));
			conn_hnd->transfer.io_loop				= -1;
			conn_hnd->flags.conn_hnd_in_transfer	= 0;

			/* Invoke internal shutdown to clean up CONN_HND */
			CommEvTCPServerConnInternalShutdown(conn_hnd);
			timeout_count++;
			continue;
		}

		continue;
	}

	/* Reschedule timer ID */
	srv_ptr->transfer.gc_timerid = EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_VOLATILE, 1000, CommEvTCPServerConnTransferGCTimer, srv_ptr);
	return 1;
}
/**************************************************************************************************************************/

