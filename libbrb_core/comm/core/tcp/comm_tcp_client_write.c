/*
 * comm_tcp_client_write.c
 *
 *  Created on: 2012-01-11
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

static void CommEvTCPClientEnqueueAndKickWriteQueue(CommEvTCPClient *ev_tcpclient, EvAIOReq *aio_req);

/**************************************************************************************************************************/
int CommEvTCPClientEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base							= base_ptr;
	CommEvTCPClient *ev_tcpclient				= cb_data;
	EvBaseKQFileDesc *kq_fd						= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);
	CommEvTCPClientEventPrototype *ev_proto		= &ev_tcpclient->events[COMM_CLIENT_EVENT_WRITE];
	int total_wrote_sz							= 0;

	CommEvTCPIOResult ioret;
	int op_status;

	/* This WRITE_EV is HOOKED */
	if (ev_proto->flags.hooked)
	{
		BRB_ASSERT_FMT(ev_base, (ev_proto->cb_hook_ptr), "FD [%d] - Write event has HOOK flag, but no HOOK_CBH\n", ev_tcpclient->socket_fd);

		/* Jump into HOOK code */
		total_wrote_sz = ev_proto->cb_hook_ptr(fd, can_write_sz, thrd_id, cb_data, base_ptr);

		/* We are CLOSED, bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return total_wrote_sz;

		/* Nothing left on WRITE_QUEUE */
		if (EvAIOReqQueueIsEmpty(&ev_tcpclient->iodata.write_queue))
		{
			/* Reset pending write flag */
			ev_tcpclient->flags.pending_write = 0;

			/* Has close request, invoke */
			if (ev_tcpclient->flags.close_request)
				COMM_EV_TCP_CLIENT_FINISH(ev_tcpclient);

			return total_wrote_sz;
		}
		else
		{
			/* Reschedule write event and SET pending WRITE FLAG */
			EvKQBaseSetEvent(ev_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventWrite, ev_tcpclient);
			ev_tcpclient->flags.pending_write = 1;
			return total_wrote_sz;
		}
	}
	/* NOT HOOKED - Invoke IO mechanism to write data */
	else
		op_status = CommEvTCPAIOWrite(ev_base, ev_tcpclient->log_base, &ev_tcpclient->statistics, &ev_tcpclient->iodata, &ioret, ev_tcpclient, can_write_sz,
				(!ev_tcpclient->flags.close_request));

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return ioret.aio_total_sz;

	/* Jump into FSM */
	switch (op_status)
	{
	case COMM_TCP_AIO_WRITE_NEEDED:
	{
		/* Reschedule write event and SET pending WRITE FLAG */
		EvKQBaseSetEvent(ev_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventWrite, ev_tcpclient);
		ev_tcpclient->flags.pending_write = 1;

		return ioret.aio_total_sz;
	}

	/* All writes FINISHED */
	case COMM_TCP_AIO_WRITE_FINISHED:
	{
		/* Reset pending write flag */
		ev_tcpclient->flags.pending_write = 0;

		/* Upper layers requested to close after writing all */
		if (ev_tcpclient->flags.close_request)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Upper layer set CLOSE_REQUEST, write buffer is empty - [%s]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->flags.destroy_after_close ? "DESTROYING" : "CLOSING");

			/* Destroy or disconnected, based on flag */
			COMM_EV_TCP_CLIENT_FINISH(ev_tcpclient);
		}

		return ioret.aio_total_sz;
	}
	case COMM_TCP_AIO_WRITE_ERR_FATAL:
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Fatal write error - ERRNO [%d]-[%s]\n",
					ev_tcpclient->socket_fd, errno, strerror(errno));

		/* Has close request, invoke */
		if (ev_tcpclient->flags.close_request)
		{
			COMM_EV_TCP_CLIENT_FINISH(ev_tcpclient);
			return ioret.aio_total_sz;
		}

		return ioret.aio_total_sz;
	}
	default:
		BRB_ASSERT_FMT(ev_base, 0, "Undefined state [%d]\n", op_status);
		return ioret.aio_total_sz;
	}

	return ioret.aio_total_sz;
}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteVectored(CommEvTCPClient *ev_tcpclient, EvAIOReqIOVectorData *vector_table, int vector_table_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReqIOVectorData *cur_iovec;
	EvAIOReq *aio_req;
	int i;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Write all vectors on table */
	for (i = 0; i < vector_table_sz; i++)
	{
		/* Grab current offset data */
		cur_iovec = &vector_table[i];

		/* Last WRITE, create with FINISH CB INFO */
		if ((i - 1) == vector_table_sz)
			aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, (cur_iovec->data_ptr + cur_iovec->offset),
					cur_iovec->size, 0, NULL, finish_cb, finish_cbdata);
		/* Create NO_FINISH AIO_REQ for initial WRITEs*/
		else
			aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, (cur_iovec->data_ptr + cur_iovec->offset),
					cur_iovec->size, 0, NULL, NULL, NULL);

		/* No more AIO slots, STOP */
		if (!aio_req)
			return 0;

		/* Set flags we are WRITING to a SOCKET */
		aio_req->flags.aio_write	= 1;
		aio_req->flags.aio_socket	= 1;

		/* Enqueue it and begin writing ASAP */
		CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteAndDestroyMemBuffer(CommEvTCPClient *ev_tcpclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, MemBufferDeref(mem_buf),
			MemBufferGetSize(mem_buf), 0, NULL, finish_cb, finish_cbdata);

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

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteMemBuffer(CommEvTCPClient *ev_tcpclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, MemBufferDeref(mem_buf),
			MemBufferGetSize(mem_buf), 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteStringFmt(CommEvTCPClient *ev_tcpclient, CommEvTCPClientCBH *finish_cb, void *finish_cbdata, char *string, ...)
{
	EvAIOReq *aio_req;
	va_list args;
	char *buf_ptr;
	int buf_sz;
	int msg_len;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
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
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, buf_ptr, buf_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

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

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteString(CommEvTCPClient *ev_tcpclient, char *string, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;
	int string_sz = strlen(string);

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, strdup(string),
			string_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);
	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteAndFree(CommEvTCPClient *ev_tcpclient, char *data, unsigned long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, data, data_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);
	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWrite(CommEvTCPClient *ev_tcpclient, char *data, unsigned long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, data, data_sz, 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

	return 1;
}
/**************************************************************************************************************************/
static void CommEvTCPClientEnqueueAndKickWriteQueue(CommEvTCPClient *ev_tcpclient, EvAIOReq *aio_req)
{
	/* Close request - Silently drop */
	if (ev_tcpclient->flags.close_request)
	{
		EvAIOReqDestroy(aio_req);
		return;
	}

	/* Allow upper layers to transform data */
	EvAIOReqTransform_WriteData(&ev_tcpclient->transform, &ev_tcpclient->iodata.write_queue, aio_req);

	/* Enqueue it in conn_queue */
	EvAIOReqQueueEnqueue(&ev_tcpclient->iodata.write_queue, aio_req);

	/* Do not ADD a write event if we are disconnected, as it will overlap our internal connect event */
	if (ev_tcpclient->socket_state != COMM_CLIENT_STATE_CONNECTED)
		return;

	/* If there is ENQUEUED data, schedule WRITE event and LEAVE, as we need to PRESERVE WRITE ORDER */
	if (ev_tcpclient->flags.pending_write)
	{
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE,
				(COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto) ? CommEvTCPClientEventSSLWrite : CommEvTCPClientEventWrite, ev_tcpclient);
		return;
	}
	/* Try to write on this very same IO LOOP */
	else
	{
		if (COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto)
			CommEvTCPClientEventSSLWrite(ev_tcpclient->socket_fd, 8092, -1, ev_tcpclient, ev_tcpclient->kq_base);
		else
			CommEvTCPClientEventWrite(ev_tcpclient->socket_fd, 8092, -1, ev_tcpclient, ev_tcpclient->kq_base);

		return;
	}

	return;
}
/**************************************************************************************************************************/
