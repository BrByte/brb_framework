/*
 * ev_ipc_child.c
 *
 *  Created on: 2013-07-01
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

/* Private prototypes */
static int EvIPCBaseChildEventDispatchInternal(EvIPCBaseChild *ipc_base_child, int data_sz, int thrd_id, int ev_type);
static void EvIPCBaseChildEnqueueAndKickQueue(EvIPCBaseChild *ipc_base_child, EvAIOReq *aio_req);
static void EvIPCBaseChildKickConnWriteQueue(EvIPCBaseChild *ipc_base_child);
static int EvICPBaseChildSelfSyncReadBuffer(EvIPCBaseChild *ipc_base_child, int orig_read_sz, int thrd_id);

/* Internal events */
static EvBaseKQCBH EvICPBaseChildEventRead;
static EvBaseKQCBH EvICPBaseChildEventWrite;
static EvBaseKQCBH EvICPBaseChildEventClose;

/**************************************************************************************************************************/
EvIPCBaseChild *EvIPCBaseChildNew(EvKQBase *kq_base, EvIPCBaseChildConf *ipc_base_child_conf)
{
	EvIPCBaseChild *ipc_base_child;

	/* Create the IPC base child */
	ipc_base_child			= calloc(1, sizeof(EvIPCBaseChild));
	ipc_base_child->kq_base	= kq_base;

	/* Load configuration */
	if (ipc_base_child_conf)
	{
		/* Check if self sync is active, then grab token to check when buffer finish */
		if (ipc_base_child_conf->self_sync.token_str)
		{
			/* Copy token into buffer and calculate token size */
			strncpy((char*)&ipc_base_child->self_sync.token_str_buf, ipc_base_child_conf->self_sync.token_str, sizeof(ipc_base_child->self_sync.token_str_buf));
			ipc_base_child->self_sync.token_str_sz = strlen(ipc_base_child_conf->self_sync.token_str);

			/* Mark self sync as active for this server and save max buffer limit */
			ipc_base_child->self_sync.max_buffer_sz	= ipc_base_child_conf->self_sync.max_buffer_sz;
			ipc_base_child->flags.self_sync			= 1;
		}

	}

	/* Create IO buffers */
	EvAIOReqQueueInit(ipc_base_child->kq_base, &ipc_base_child->write_queue, 1024,
			(ipc_base_child->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

	return ipc_base_child;
}
/**************************************************************************************************************************/
void EvIPCBaseChildInit(EvKQBase *kq_base, EvIPCBaseChild *ipc_base_child)
{
	/* Make STDIN, STDOUT and STDERR non blocking */
	EvKQBaseSocketSetNonBlock(kq_base, STDIN);
	EvKQBaseSocketSetNonBlock(kq_base, STDOUT);
	EvKQBaseSocketSetNonBlock(kq_base, STDERR);

	/* Set parent FDs to CLOSE_ON_EXEC mode */
	EvKQBaseSocketSetNoDelay(kq_base, STDIN);
	EvKQBaseSocketSetNoDelay(kq_base, STDOUT);
	EvKQBaseSocketSetNoDelay(kq_base, STDERR);

	/* Set read and close event for STDIN */
	EvKQBaseSetEvent(kq_base, 0, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvICPBaseChildEventRead, ipc_base_child);
	EvKQBaseSetEvent(kq_base, 0, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, EvICPBaseChildEventClose, ipc_base_child);

	return;
}
/**************************************************************************************************************************/
void EvIPCBaseChildDestroy(EvIPCBaseChild *ipc_base_child)
{
	EvKQBase *ev_base	= ipc_base_child->kq_base;

	/* Sanity check */
	if (!ipc_base_child)
		return;

	/* DESTROY IO buffers */
	EvAIOReqQueueClean(&ipc_base_child->write_queue);
	MemBufferDestroy(ipc_base_child->read_buffer);

	/* Make STDIN, STDOUT and STDERR BLOCKING again */
	EvKQBaseSocketSetBlocking(ev_base, STDIN);
	EvKQBaseSocketSetBlocking(ev_base, STDOUT);
	EvKQBaseSocketSetBlocking(ev_base, STDERR);

	/* Now close them */
	EvKQBaseSocketClose(ev_base, STDIN);
	EvKQBaseSocketClose(ev_base, STDOUT);
	EvKQBaseSocketClose(ev_base, STDERR);

	/* Free base */
	free(ipc_base_child);

	return;

}
/**************************************************************************************************************************/
void EvIPCBaseChildWriteStringFmt(EvIPCBaseChild *ipc_base_child, IPCEvCBH *finish_cb, void *finish_cbdata, char *string, ...)
{
	EvAIOReq *aio_req;
	va_list args;
	char *buf_ptr;
	int buf_sz;
	int msg_len;

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
	aio_req = EvAIOReqNew(&ipc_base_child->write_queue, -1, ipc_base_child, buf_ptr, buf_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue it and begin writing ASAP */
	EvIPCBaseChildEnqueueAndKickQueue(ipc_base_child, aio_req);

	return;

}
/**************************************************************************************************************************/
void EvIPCBaseChildWriteMemBuffer(EvIPCBaseChild *ipc_base_child, IPCEvCBH *finish_cb, void *finish_cbdata, MemBuffer *mb_ptr)
{
	EvAIOReq *aio_req;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ipc_base_child->write_queue, -1, ipc_base_child, MemBufferDeref(mb_ptr), MemBufferGetSize(mb_ptr), 0, NULL, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue it and begin writing ASAP */
	EvIPCBaseChildEnqueueAndKickQueue(ipc_base_child, aio_req);

	return;

}
/**************************************************************************************************************************/
void EvIPCBaseChildWrite(EvIPCBaseChild *ipc_base_child, IPCEvCBH *finish_cb, void *finish_cbdata, char *buf_ptr, long buf_sz)
{
	EvAIOReq *aio_req;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ipc_base_child->write_queue, -1, ipc_base_child, buf_ptr, buf_sz, 0, NULL, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue it and begin writing ASAP */
	EvIPCBaseChildEnqueueAndKickQueue(ipc_base_child, aio_req);

	return;
}
/**************************************************************************************************************************/
void EvIPCBaseChildEventCancel(EvIPCBaseChild *ipc_base_child, int ev_type)
{
	/* Set event */
	ipc_base_child->events[ev_type].cb_handler_ptr		= NULL;
	ipc_base_child->events[ev_type].cb_data_ptr			= NULL;

	/* Mark disabled */
	ipc_base_child->events[ev_type].flags.enabled			= 0;

	return;
}
/**************************************************************************************************************************/
void EvIPCBaseChildEventCancelAll(EvIPCBaseChild *ipc_base_child)
{
	int i;

	/* Cancel all possible events */
	for (i = 0; i < IPC_EVENT_LASTITEM; i++)
	{
		EvIPCBaseChildEventCancel(ipc_base_child, i);
	}

	return;
}
/**************************************************************************************************************************/
int EvIPCBaseChildEventIsSet(EvIPCBaseChild *ipc_base_child, int ev_type)
{
	/* Sanity check */
	if (ev_type >= IPC_EVENT_LASTITEM)
		return 0;

	if (ipc_base_child->events[ev_type].cb_handler_ptr && ipc_base_child->events[ev_type].flags.enabled)
		return 1;

	return 0;
}
/**************************************************************************************************************************/
void EvIPCBaseChildEventSet(EvIPCBaseChild *ipc_base_child, int ev_type, IPCEvCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= IPC_EVENT_LASTITEM)
		return;

	/* Set event */
	ipc_base_child->events[ev_type].cb_handler_ptr	= cb_handler;
	ipc_base_child->events[ev_type].cb_data_ptr		= cb_data;

	/* Mark enabled */
	ipc_base_child->events[ev_type].flags.enabled		= 1;

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void EvIPCBaseChildEnqueueAndKickQueue(EvIPCBaseChild *ipc_base_child, EvAIOReq *aio_req)
{
	/* Enqueue it in conn_queue */
	EvAIOReqQueueEnqueue(&ipc_base_child->write_queue, aio_req);

	/* Ask the event base to write */
	EvIPCBaseChildKickConnWriteQueue(ipc_base_child);

	return;
}
/**************************************************************************************************************************/
static void EvIPCBaseChildKickConnWriteQueue(EvIPCBaseChild *ipc_base_child)
{
	/* Ask the event base to write */
	EvKQBaseSetEvent(ipc_base_child->kq_base, STDOUT, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvICPBaseChildEventWrite, ipc_base_child);

	return;
}
/**************************************************************************************************************************/
static int EvIPCBaseChildEventDispatchInternal(EvIPCBaseChild *ipc_base_child, int data_sz, int thrd_id, int ev_type)
{
	IPCEvCBH *cb_handler		= NULL;
	void *cb_handler_data		= NULL;

	/* Grab callback_ptr */
	cb_handler = ipc_base_child->events[ev_type].cb_handler_ptr;

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Grab data for this CBH */
		cb_handler_data = ipc_base_child->events[ev_type].cb_data_ptr;

		/* Jump into CBH. Base for this event is EvIPCBase* */
		cb_handler(STDIN, data_sz, thrd_id, cb_handler_data, ipc_base_child);

		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
static int EvICPBaseChildSelfSyncReadBuffer(EvIPCBaseChild *ipc_base_child, int orig_read_sz, int thrd_id)
{
	EvKQBase *ev_base			= ipc_base_child->kq_base;

	char *partial_ptr;
	char *read_buffer_ptr;
	char *request_str_ptr;
	int request_str_sz;
	int remaining_sz;
	int i, j;

	char *token_str				= (char*)&ipc_base_child->self_sync.token_str_buf;
	int max_buffer_sz			= ipc_base_child->self_sync.max_buffer_sz;
	int token_sz				= ipc_base_child->self_sync.token_str_sz;
	int token_found				= 0;

	/* Flag not set, or buffer is empty, bail out */
	if ((!ipc_base_child->flags.self_sync) || (!ipc_base_child->read_buffer))
		return 0;

	/* Make sure buffer if NULL terminated before dealing with it as a string */
	MemBufferPutNULLTerminator(ipc_base_child->read_buffer);

	/* Get information about original string encoded request array */
	request_str_ptr	= MemBufferDeref(ipc_base_child->read_buffer);
	request_str_sz	= MemBufferGetSize(ipc_base_child->read_buffer);

	/* Sanity check */
	if (request_str_sz < token_sz)
		return 0;

	KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "REQ_SZ [%d] - TOKEN_SZ [%d]\n", request_str_sz, token_sz);
	//EvKQBaseLogHexDump(request_str_ptr, request_str_sz, 8, 4);

	KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO,  LOGCOLOR_GREEN, "FD [%d] - Searching for token [%u] with [%d] bytes on [%d] bytes\n",
			0, token_str[0], token_sz, request_str_sz);

	/* Start searching the buffer */
	for (j = 0, i = (request_str_sz); i >= 0; i--, j++)
	{
		/* Found finish of token, compare full token versus buffer */
		if ( ((j >= token_sz) && (request_str_ptr[i] == token_str[0])) && (!memcmp(&request_str_ptr[i], token_str, token_sz)) )
		{
			KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO,  LOGCOLOR_YELLOW, "FD [%d] - Token found at [%d] - Buffer size is [%d]\n", 0, j, request_str_sz);

			/* Found token */
			token_found = 1;
			break;
		}

		continue;
	}

	/* Token bas been found */
	if (token_found)
	{
		/* Calculate remaining size past token */
		remaining_sz	= (request_str_sz - i - token_sz);

		/* There is data past token, save it into partial buffer */
		if (remaining_sz > 0)
		{
			/* Create a new partial_read_buffer object */
			if (!ipc_base_child->partial_read_buffer)
				ipc_base_child->partial_read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (remaining_sz + 1));

			/* Point to remaining data */
			read_buffer_ptr						= MemBufferOffsetDeref(ipc_base_child->read_buffer, (request_str_sz - remaining_sz));
			ipc_base_child->read_buffer->size	-= remaining_sz;

			/* Save left over into partial buffer and NULL terminate read buffer to new size */
			MemBufferAdd(ipc_base_child->partial_read_buffer, read_buffer_ptr, remaining_sz);
			MemBufferPutNULLTerminator(ipc_base_child->read_buffer);

			KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO,  LOGCOLOR_GREEN, "FD [%d] - Read buffer adjusted to [%d] bytes - Partial holds [%d] remaining bytes\n",
					0, MemBufferGetSize(ipc_base_child->read_buffer), MemBufferGetSize(ipc_base_child->partial_read_buffer));

		}
	}

	/* Dispatch upper layer read event if token has been found or if we reached our maximum allowed buffer size */
	if ( (token_found) || ((max_buffer_sz > 0) && (request_str_sz >= max_buffer_sz)) )
	{
		char *aux_ptr00 = MemBufferDeref(ipc_base_child->read_buffer);
		char *aux_ptr01 = MemBufferDeref(ipc_base_child->partial_read_buffer);

		KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO,  LOGCOLOR_CYAN, "FD [%d] - MAX_BUF [%d] - READ_BUF [%d]-[%s] - PARTIAL [%d]-[%s]\n",
				0, max_buffer_sz, MemBufferGetSize(ipc_base_child->read_buffer), aux_ptr00 ? aux_ptr00 : "NULL",
						MemBufferGetSize(ipc_base_child->partial_read_buffer), aux_ptr01 ? aux_ptr01 : "NULL");

		/* Dispatch internal event */
		EvIPCBaseChildEventDispatchInternal(ipc_base_child, orig_read_sz, thrd_id, IPC_EVENT_READ);
	}
	else
	{
		KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO,  LOGCOLOR_YELLOW, "FD [%d] - READ_BUF [%d] bytes - TOKEN NOT FOUND, WILL NOT DISPATCH NOW\n",
				0, MemBufferGetSize(ipc_base_child->read_buffer));
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvICPBaseChildEventRead(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvIPCBaseChild *ipc_base_child	= cb_data;
	EvKQBase *ev_base				= ipc_base_child->kq_base;
	int data_read					= 0;
	int op_status					= 0;

	/* This is a closing connection, bail out */
	if (read_sz <= 0)
		return 0;

	/* Create a new read_buffer object */
	if (!ipc_base_child->read_buffer)
	{
		/* Check if have partial buffer, and as read is empty, just switch pointers and set partial to NULL */
		if (ipc_base_child->partial_read_buffer)
		{
			ipc_base_child->read_buffer			= ipc_base_child->partial_read_buffer;
			ipc_base_child->partial_read_buffer 	= NULL;
		}
		/* Create a new read buffer */
		else
			ipc_base_child->read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (read_sz + 1));
	}

	/* There is a partial buffer left, copy it back to read buffer */
	if (ipc_base_child->partial_read_buffer)
	{
		MemBufferAdd(ipc_base_child->read_buffer, MemBufferDeref(ipc_base_child->partial_read_buffer), MemBufferGetSize(ipc_base_child->partial_read_buffer));
		MemBufferClean(ipc_base_child->partial_read_buffer);
	}

	/* Grab data from FD directly into read_buffer, zero-copy */
	data_read = MemBufferAppendFromFD(ipc_base_child->read_buffer, read_sz, fd, 0);

	/* Upper layers asked for self_sync, so invoke it */
	if (ipc_base_child->flags.self_sync)
	{
		KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - SELF_SYNC with [%d] bytes\n", fd, read_sz);
		EvICPBaseChildSelfSyncReadBuffer(ipc_base_child, read_sz, thrd_id);
	}
	/* Dispatch internal event */
	else
		op_status = EvIPCBaseChildEventDispatchInternal(ipc_base_child, data_read, thrd_id, IPC_EVENT_READ);

	/* No upper layer event for this READ, clean buffer */
	if (!op_status)
		MemBufferClean(ipc_base_child->read_buffer);

	/* Reschedule read event */
	EvKQBaseSetEvent(ev_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvICPBaseChildEventRead, ipc_base_child);

	//printf("EvICPBaseChildEventRead - [%d]-[%s]\n", MemBufferGetSize(ipc_base->read_buffer), MemBufferDeref(ipc_base->read_buffer));

	return data_read;
}
/**************************************************************************************************************************/
static int EvICPBaseChildEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOReq *cur_aio_req;

	char *data_ptr;
	int still_can_write_sz;
	unsigned long wanted_write_sz;
	int wrote_sz;
	int total_wrote_sz = 0;

	EvKQBase *ev_base				= base_ptr;
	EvIPCBaseChild *ipc_base_child	= cb_data;

	/* Label used to write multiple aio_reqs in the same write_window, if possible */
	write_again:

	/* Grab aio_req unit */
	cur_aio_req			= EvAIOReqQueueDequeue(&ipc_base_child->write_queue);

	/* Nothing to write, bail out */
	if (!cur_aio_req)
		return total_wrote_sz;

	/* Calculate current data size and offset */
	wanted_write_sz	= cur_aio_req->data.size	- cur_aio_req->data.offset;
	data_ptr		= cur_aio_req->data.ptr		+ cur_aio_req->data.offset;

	/* Issue write call - Either what we want to write, if possible. Otherwise, write what kernel tells us we can */
	wrote_sz = write(fd, data_ptr, (can_write_sz < wanted_write_sz) ? can_write_sz : wanted_write_sz);

	/* The write was interrupted by a signal or we were not able to write any data to it, reschedule and return. */
	if (wrote_sz == -1)
	{
		/* RENQUEUE AIO_REQ, either for destruction or for another write attempt */
		EvAIOReqQueueEnqueueHead(&ipc_base_child->write_queue, cur_aio_req);
		cur_aio_req->err = errno;

		if (errno == EINTR || errno == EAGAIN)
		{
			/* Reschedule write event and return */
			EvKQBaseSetEvent(ev_base, fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvICPBaseChildEventWrite, ipc_base_child);

			return total_wrote_sz;
		}

		KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - FATAL WRITE ERROR - CAN_WRITE_SZ [%d]\n", fd, can_write_sz);
		return total_wrote_sz;
	}

	/* Write_ok, update offset and counter */
	cur_aio_req->data.offset	+= wrote_sz;
	total_wrote_sz				+= wrote_sz;

	/* Write is complete */
	if (cur_aio_req->data.offset >= cur_aio_req->data.size)
	{
		KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - FULL write of [%d] bytes - Wanted [%lu] - Offset is now [%lu] on THREAD [%d]\n",
				fd, wrote_sz, wanted_write_sz, cur_aio_req->data.offset, thrd_id);

		/* Dispatch write finish event, if there is anyone interested */
		if (cur_aio_req->finish_cb)
			cur_aio_req->finish_cb(fd, cur_aio_req->data.offset, thrd_id, cur_aio_req->finish_cbdata, ipc_base_child);

		/* Destroy current aio_req */
		EvAIOReqDestroy(cur_aio_req);

		/* Next aio_req exists, loop writing some more or reschedule write event */
		if (!EvAIOReqQueueIsEmpty(&ipc_base_child->write_queue))
		{
			DLinkedList *aio_req_list = &ipc_base_child->write_queue.aio_req_list;

			/* Get a reference to next element */
			cur_aio_req = (EvAIOReq*)aio_req_list->head->data;

			/* Calculate wanted data size and how many bytes we have left to write */
			wanted_write_sz		= cur_aio_req->data.size - cur_aio_req->data.offset;
			still_can_write_sz	= can_write_sz - total_wrote_sz;

			/* Keep writing as many aio_reqs the kernel allow us to */
			if (wanted_write_sz <= still_can_write_sz)
			{
				KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - We can still write [%d] bytes qnd next aio_req wants [%d] bytes, loop to grab it\n",
						fd, still_can_write_sz, wanted_write_sz);
				goto write_again;
			}
			/* No more room to write, reschedule write event */
			else
			{
				KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - We can still write [%d] bytes qnd next aio_req wants [%d] bytes, reeschedule WRITE_EV\n",
						fd, still_can_write_sz, wanted_write_sz);

				/* Reschedule write event */
				EvKQBaseSetEvent(ev_base, fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvICPBaseChildEventWrite, ipc_base_child);
			}
		}
	}
	else
	{
		KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - PARTIAL write of [%d] bytes - Offset is now [%d]\n", fd, wrote_sz, cur_aio_req->data.offset);

		/* REENQUEUE and reschedule write event */
		EvAIOReqQueueEnqueueHead(&ipc_base_child->write_queue, cur_aio_req);
		EvKQBaseSetEvent(ev_base, fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvICPBaseChildEventWrite, ipc_base_child);
	}

	return total_wrote_sz;

}
/**************************************************************************************************************************/
static int EvICPBaseChildEventClose(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvIPCBaseChild *ipc_base_child	= cb_data;
	EvKQBase *ev_base				= ipc_base_child->kq_base;

	/* There is still data pending to read */
	if (read_sz > 0)
		return 0;

	KQBASE_LOG_PRINTF(ipc_base_child->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "Close event on IPC [%p] with base [%p] - orig_base is [%p]\n",
			ipc_base_child, ev_base, base_ptr);

	/* Dispatch internal event */
	EvIPCBaseChildEventDispatchInternal(ipc_base_child, read_sz, thrd_id, IPC_EVENT_CLOSE);

	return 1;
}
/**************************************************************************************************************************/


