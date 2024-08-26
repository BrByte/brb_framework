/*
 * comm_tcp_client_read.c
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

static int CommEvTCPClientSelfSyncReadBuffer(CommEvTCPClient *ev_tcpclient, int orig_read_sz, int thrd_id);

/**************************************************************************************************************************/
int CommEvTCPClientEventRead(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base							= base_ptr;
	CommEvTCPClient *ev_tcpclient				= cb_data;
	EvBaseKQFileDesc *kq_fd						= EvKQBaseFDGrabFromArena(ev_base, fd);
	CommEvTCPClientEventPrototype *ev_proto		= &ev_tcpclient->events[COMM_CLIENT_EVENT_READ];
	int data_read								= 0;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Read event of [%d] bytes\n", fd, read_sz);

	/* This is a closing connection, bail out */
	if (read_sz <= 0)
		return 0;

	/* This READ_EV is HOOKED */
	if (ev_proto->flags.hooked)
	{
		BRB_ASSERT_FMT(ev_base, (ev_proto->cb_hook_ptr), "FD [%d] - Read event has HOOK flag, but no HOOK_CBH\n", ev_tcpclient->socket_fd);

		/* Jump into HOOK code */
		data_read = ev_proto->cb_hook_ptr(fd, read_sz, thrd_id, cb_data, base_ptr);

		/* We are CLOSED, bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return data_read;

		/* Dispatch an internal READ_EV so upper layer get notified after HOOKED code finish */
		if (data_read > 0)
			CommEvTCPClientEventDispatchInternal(ev_tcpclient, read_sz, thrd_id, COMM_CLIENT_EVENT_READ);
	}
	/* Read buffer and invoke CBH - WARNING: This may destroy TCPCLIENT under our feet */
	else
		data_read = CommEvTCPClientProcessBuffer(ev_tcpclient, read_sz, thrd_id, NULL, 0);

	/* We are CLOSED, bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return data_read;

	/* NON fatal error while reading this FD, reschedule and leave */
//	if (-1 == data_read)
//	{
//		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Non fatal error while trying to read [%d] bytes\n", fd, read_sz);
//	}

	/* Touch statistics */
	if (data_read > 0)
	{
		ev_tcpclient->statistics.total[COMM_CURRENT].byte_rx	+= data_read;
		ev_tcpclient->statistics.total[COMM_CURRENT].packet_rx	+= 1;
	}

	/* Reschedule read event - Upper layers could have closed this socket, so just RESCHEDULE READ if we are still ONLINE */
	if (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventRead, ev_tcpclient);

	return data_read;
}
/**************************************************************************************************************************/
int CommEvTCPClientEventEof(int fd, int buf_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_tcpclient->kq_base, fd);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF - [%d] bytes left in kernel buffer - [%d] bytes left in MEM_MB - ER [%d] - Flags [%u]\n",
			fd, buf_read_sz, ev_tcpclient->iodata.read_buffer ? MemBufferGetSize(ev_tcpclient->iodata.read_buffer) : -1, kq_fd->flags.so_read_error, kq_fd->fd.fflags);

	/* Mark SOCKET_EOF for this client */
	ev_tcpclient->flags.socket_eof 	= 1;

	/* There is a close request for this client. Upper layers do not care any longer, just finish */
	if (ev_tcpclient->flags.close_request)
		goto finish;

	/* Do not close for now, there is data pending read */
	if ((buf_read_sz > 0) && (!ev_tcpclient->flags.peek_on_read) && (!ev_tcpclient->flags.ssl_zero_ret))
	{
		EvKQBaseSetEvent(ev_tcpclient->kq_base, fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventEof, ev_tcpclient);
		return 0;
	}

	/* Set current state */
	ev_tcpclient->socket_state 		= COMM_CLIENT_STATE_DISCONNECTED;

	/* Dispatch internal event */
	CommEvTCPClientEventDispatchInternal(ev_tcpclient, buf_read_sz, thrd_id, COMM_CLIENT_EVENT_CLOSE);

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return 0;

	/* Either destroy or disconnect client, as requested by operator flags */
	finish:
	COMM_EV_TCP_CLIENT_INTERNAL_FINISH(ev_tcpclient);
	return 1;
}/**************************************************************************************************************************/
int CommEvTCPClientProcessBuffer(CommEvTCPClient *ev_tcpclient, int read_sz, int thrd_id, char *read_buf, int read_buf_sz)
{
	EvKQBase *ev_base			= ev_tcpclient->kq_base;
	EvBaseKQFileDesc *kq_fd		= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);
	MemBuffer *transformed_mb	= NULL;
	int data_read				= 0;

	char *transform_ptr;
	long data_read_cur;

	/* Read into operator defined structure */
	switch (ev_tcpclient->read_mthd)
	{
	/*********************************************************************/
	case COMM_CLIENT_READ_MEMBUFFER:
	{
		/* Create a new read_buffer object */
		if (!ev_tcpclient->iodata.read_buffer)
		{
			/* Check if have partial buffer, and as read is empty, just switch pointers and set partial to NULL */
			if (ev_tcpclient->iodata.partial_read_buffer)
			{
				ev_tcpclient->iodata.read_buffer			= ev_tcpclient->iodata.partial_read_buffer;
				ev_tcpclient->iodata.partial_read_buffer 	= NULL;
			}
			/* Create a new read buffer */
			else
				ev_tcpclient->iodata.read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (read_sz + 1));
		}

		/* There is a partial buffer left, copy it back to read buffer */
		if (ev_tcpclient->iodata.partial_read_buffer)
		{
			MemBufferAdd(ev_tcpclient->iodata.read_buffer, MemBufferDeref(ev_tcpclient->iodata.partial_read_buffer), MemBufferGetSize(ev_tcpclient->iodata.partial_read_buffer));
			MemBufferClean(ev_tcpclient->iodata.partial_read_buffer);
		}

		/* Absorb data from READ_BUFFER */
		if (read_buf_sz > 0)
		{
			/* Allow upper layers to perform data transformation */
			transformed_mb		= EvAIOReqTransform_ReadData(&ev_tcpclient->transform, read_buf, read_buf_sz);

			/* No transformed MB, append read_buf */
			if (!transformed_mb)
			{
				/* Add Read Buffer into read_buffer */
				MemBufferAdd(ev_tcpclient->iodata.read_buffer, read_buf, read_buf_sz);
			}

			data_read = read_buf_sz;
		}
		/* Grab data from FD directly into read_buffer, zero-copy */
		else
		{
			/* Get current size */
			data_read_cur 	= MemBufferGetSize(ev_tcpclient->iodata.read_buffer);

			/* Read data from FD */
			data_read 		= MemBufferAppendFromFD(ev_tcpclient->iodata.read_buffer, read_sz, ev_tcpclient->socket_fd, (ev_tcpclient->flags.peek_on_read ? MSG_PEEK : 0));

			/* Read OK */
			if (data_read > 0)
			{
				transform_ptr		= MemBufferDeref(ev_tcpclient->iodata.read_buffer);
				transform_ptr 		= transform_ptr + data_read_cur;

				/* Allow upper layers to perform data transformation */
				transformed_mb		= EvAIOReqTransform_ReadData(&ev_tcpclient->transform, transform_ptr, data_read);

				/* Correct size to add transformed MB */
				if (transformed_mb)
					ev_tcpclient->iodata.read_buffer->size = data_read_cur;
			}
			/* Failed reading FD */
			else
			{
//				/* NON_FATAL error */
//				if ((!kq_fd->flags.so_read_eof) && (errno == EINTR || errno == EAGAIN))
//					return -1;
//
//				KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Error reading [%d] of [%d] bytes - ERRNO [%d]\n",
//						ev_tcpclient->socket_fd, data_read, read_sz, errno);
				return -1;
			}
		}

		/* Data has been transformed, add to READ_BUFFER */
		if (transformed_mb)
		{
			/* Add Read Buffer into read_buffer */
			MemBufferAdd(ev_tcpclient->iodata.read_buffer, MemBufferDeref(transformed_mb), MemBufferGetSize(transformed_mb));
			MemBufferDestroy(transformed_mb);
		}

		break;
	}
	/*********************************************************************/
	case COMM_CLIENT_READ_MEMSTREAM:
	{
		/* Create a new read_stream object */
		if (!ev_tcpclient->iodata.read_stream)
		{
			if (ev_base->flags.mt_engine)
				ev_tcpclient->iodata.read_stream = MemStreamNew(8092, MEMSTREAM_MT_SAFE);
			else
				ev_tcpclient->iodata.read_stream = MemStreamNew(8092, MEMSTREAM_MT_UNSAFE);
		}

		/* Absorb data from READ_BUFFER */
		if (read_buf_sz > 0)
		{
			/* Add SSL buffer into plain read_stream */
			MemStreamWrite(ev_tcpclient->iodata.read_stream, read_buf, read_buf_sz);
			data_read = read_buf_sz;
		}
		/* Grab data from FD directly into read_stream, zero-copy */
		else
			data_read = MemStreamGrabDataFromFD(ev_tcpclient->iodata.read_stream, read_sz, ev_tcpclient->socket_fd);

		/* Grab data from FD directly into read_stream, zero-copy */
		data_read = MemStreamGrabDataFromFD(ev_tcpclient->iodata.read_stream, read_sz, ev_tcpclient->socket_fd);

		break;
	}
	/*********************************************************************/
	}

	/* Upper layers asked for self_sync, so invoke it */
	if (ev_tcpclient->cfg.flags.self_sync)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - SELF_SYNC with [%d] bytes\n", ev_tcpclient->socket_fd, read_sz);
		CommEvTCPClientSelfSyncReadBuffer(ev_tcpclient, read_sz, thrd_id);
	}
	/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatching internal event for [%d] bytes on WIRE\n", ev_tcpclient->socket_fd, read_sz);

		/* Dispatch internal event */
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, read_sz, thrd_id, COMM_CLIENT_EVENT_READ);

	}

	return data_read;
}
/**************************************************************************************************************************/
static int CommEvTCPClientSelfSyncReadBuffer(CommEvTCPClient *ev_tcpclient, int orig_read_sz, int thrd_id)
{
	EvKQBase *ev_base			= ev_tcpclient->kq_base;

	char *partial_ptr;
	char *read_buffer_ptr;
	char *request_str_ptr;
	int request_str_sz;
	int remaining_sz;
	int i, j;

	char *token_str				= (char*)&ev_tcpclient->cfg.self_sync.token_str_buf;
	int max_buffer_sz			= ev_tcpclient->cfg.self_sync.max_buffer_sz;
	int token_sz				= ev_tcpclient->cfg.self_sync.token_str_sz;
	int token_found				= 0;

	/* Flag not set, or buffer is empty, bail out */
	if ((!ev_tcpclient->cfg.flags.self_sync) || (!ev_tcpclient->iodata.read_buffer))
		return 0;

	/* Make sure buffer if NULL terminated before dealing with it as a string */
	MemBufferPutNULLTerminator(ev_tcpclient->iodata.read_buffer);

	/* Get information about original string encoded request array */
	request_str_ptr	= MemBufferDeref(ev_tcpclient->iodata.read_buffer);
	request_str_sz	= MemBufferGetSize(ev_tcpclient->iodata.read_buffer);

	/* Sanity check */
	if (request_str_sz < token_sz)
		return 0;

	//printf("CommEvTCPClientSelfSyncReadBuffer - REQ_SZ [%d] - TOKEN_SZ [%d]\n", request_str_sz, token_sz);
	//EvKQBaseLogHexDump(request_str_ptr, request_str_sz, 8, 4);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_GREEN, "FD [%d] - Searching for token [%u] with [%d] bytes\n",
			ev_tcpclient->socket_fd, token_str[0], token_sz);

	/* Start searching the buffer */
	for (j = 0, i = (request_str_sz); i >= 0; i--, j++)
	{
		/* Found finish of token, compare full token versus buffer */
		if ( ((j >= token_sz) && (request_str_ptr[i] == token_str[0])) && (!memcmp(&request_str_ptr[i], token_str, token_sz)) )
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_YELLOW, "FD [%d] - Token found at [%d] - Buffer size is [%d]\n",
					ev_tcpclient->socket_fd, j, request_str_sz);

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
			if (!ev_tcpclient->iodata.partial_read_buffer)
				ev_tcpclient->iodata.partial_read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (remaining_sz + 1));

			/* Point to remaining data */
			read_buffer_ptr						= MemBufferOffsetDeref(ev_tcpclient->iodata.read_buffer, (request_str_sz - remaining_sz));
			ev_tcpclient->iodata.read_buffer->size		-= remaining_sz;

			/* Save left over into partial buffer and NULL terminate read buffer to new size */
			MemBufferAdd(ev_tcpclient->iodata.partial_read_buffer, read_buffer_ptr, remaining_sz);
			MemBufferPutNULLTerminator(ev_tcpclient->iodata.read_buffer);

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_GREEN, "FD [%d] - Read buffer adjusted to [%d] bytes - Partial holds [%d] remaining bytes\n",
					ev_tcpclient->socket_fd, MemBufferGetSize(ev_tcpclient->iodata.read_buffer), MemBufferGetSize(ev_tcpclient->iodata.partial_read_buffer));

		}
	}

	/* Dispatch upper layer read event if token has been found or if we reached our maximum allowed buffer size */
	if ( (token_found) || ((max_buffer_sz > 0) && (request_str_sz >= max_buffer_sz)) )
	{
		char *aux_ptr00 = MemBufferDeref(ev_tcpclient->iodata.read_buffer);
		char *aux_ptr01 = MemBufferDeref(ev_tcpclient->iodata.partial_read_buffer);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_CYAN, "FD [%d] - MAX_BUF [%d] - READ_BUF [%d]-[%s] - PARTIAL [%d]-[%s]\n",
				ev_tcpclient->socket_fd, max_buffer_sz, MemBufferGetSize(ev_tcpclient->iodata.read_buffer), aux_ptr00 ? aux_ptr00 : "NULL",
						MemBufferGetSize(ev_tcpclient->iodata.partial_read_buffer), aux_ptr01 ? aux_ptr01 : "NULL");

		/* Dispatch internal event */
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, orig_read_sz, thrd_id, COMM_CLIENT_EVENT_READ);
	}
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_YELLOW, "FD [%d] - READ_BUF [%d] bytes - TOKEN NOT FOUND, WILL NOT DISPATCH NOW\n",
				ev_tcpclient->socket_fd, MemBufferGetSize(ev_tcpclient->iodata.read_buffer));
	}

	return 1;
}
/**************************************************************************************************************************/
