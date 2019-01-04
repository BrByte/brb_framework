/*
 * ev_ipc_base.c
 *
 *  Created on: 2013-05-08
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
static void EvIPCBaseCloseFDArr(EvKQBase *kq_base, int *fd_arr);
static int EvIPCBaseCreateUnixStreamPair(EvKQBase *kq_base, EvIPCBase *ipc_base, int *fd_arr, int iobuf_sz);
static void EvIPCEnqueueAndKickQueue(EvIPCBase *ipc_base, EvAIOReq *aio_req);
static void EvIPCKickConnWriteQueue(EvIPCBase *ipc_base);
static int EvIPCBaseEventDispatchInternal(EvIPCBase *ipc_base, int data_sz, int thrd_id, int ev_type);

/* Internal events */
static EvBaseKQCBH EvICPBaseEventRead;
static EvBaseKQCBH EvICPBaseEventReadError;
static EvBaseKQCBH EvICPBaseEventWrite;
static EvBaseKQCBH EvICPBaseEventClose;

static IPCEvCBH EvIPCBaseSystemInternalClose;
static EvBaseKQJobCBH EvIPCBaseExecuteEventJob;

/**************************************************************************************************************************/
EvIPCBase *EvIPCBaseNew(EvKQBase *kq_base)
{
	EvIPCBase *ipc_base;

	/* Create the IPC base */
	ipc_base					= calloc(1, sizeof(EvIPCBase));
	ipc_base->parent_ev_base	= kq_base;

	/* Initialize QUEUE */
	EvAIOReqQueueInit(ipc_base->parent_ev_base, &ipc_base->write_queue, 1024,
			(ipc_base->parent_ev_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

	return ipc_base;
}
/**************************************************************************************************************************/
void EvIPCBaseDestroy(EvIPCBase *ipc_base)
{
	/* Sanity check */
	if (!ipc_base)
		return;

	/* Close any possible open FD on the stream */
	EvIPCBaseCloseFDArr(ipc_base->parent_ev_base, (int*)&ipc_base->fd_arr);
	EvAIOReqQueueClean(&ipc_base->write_queue);
	MemBufferDestroy(ipc_base->read_buffer);
	MemBufferDestroy(ipc_base->error_buffer);

	/* Free WILLY */
	free(ipc_base);

	return;

}
/**************************************************************************************************************************/
int EvIPCBaseSystemWithStrArr(EvKQBase *ev_base, StringArray *cmd_parts_strarr)
{
	EvIPCBase *ipc_base;
	char *cmd_str;
	char *args[128];
	int cmd_sz;
	int op_status;

	/* Create a new IPC base for this worker */
	ipc_base = EvIPCBaseNew(ev_base);

	/* Internal close event to destroy this IPC base */
	EvIPCBaseEventSet(ipc_base, IPC_EVENT_CLOSE, EvIPCBaseSystemInternalClose, NULL);

	/* Get args for EvIPCBaseExecute */
	STRINGARRAY_FOREACH(cmd_parts_strarr, cmd_str, cmd_sz)
	{
		args[_count_]		= cmd_str;
		args[_count_ + 1]	= NULL;
	}

	/* Run the worker engine */
	op_status = EvIPCBaseExecute(ipc_base, StringArrayGetDataByPos(cmd_parts_strarr, 0), args);

	return op_status;

}
/**************************************************************************************************************************/
int EvIPCBaseSystem(EvKQBase *ev_base, char *cmd, ...)
{
	StringArray *cmd_parts_strarr;
	EvIPCBase *ipc_base;
	va_list args_va;
	char *cmd_str;
	char *args[128];
	char buf[8092];
	int cmd_sz;
	int buf_sz;
	int op_status;

	/* Format it into buffer */
	va_start( args_va, cmd );
	buf_sz = vsnprintf((char*)&buf, (sizeof(buf) - 1), cmd, args_va);
	va_end(args_va);

	/* NULL terminate it */
	buf[buf_sz] = '\0';

	cmd_parts_strarr = StringArrayExplodeStr((char*)&buf, " ", NULL, NULL);

	/* Create a new IPC base for this worker */
	ipc_base = EvIPCBaseNew(ev_base);

	/* Internal close event to destroy this IPC base */
	EvIPCBaseEventSet(ipc_base, IPC_EVENT_CLOSE, EvIPCBaseSystemInternalClose, NULL);

	/* Get args for EvIPCBaseExecute */
	STRINGARRAY_FOREACH(cmd_parts_strarr, cmd_str, cmd_sz)
	{
		args[_count_]		= cmd_str;
		args[_count_ + 1]	= NULL;
	}

	/* Run the worker engine */
	op_status = EvIPCBaseExecute(ipc_base, StringArrayGetDataByPos(cmd_parts_strarr, 0), args);

	StringArrayDestroy(cmd_parts_strarr);

	return op_status;
}
/**************************************************************************************************************************/
int EvIPCBaseExecute(EvIPCBase *ipc_base, char *prog, char **args)
{
	EvKQBase *kq_base;
	int child_pid;
	int op_status;
	int handshake_read_sz;
	int handshake_write_sz;
	char handshake_buf[HANDSHAKE_BUF_SZ];

	/* Create the new IPC base and save target program name */
	kq_base = ipc_base->parent_ev_base;
	strncpy((char*)&ipc_base->target_name, prog, sizeof(ipc_base->target_name));

	/* Create the connected socket pair */
	op_status = EvIPCBaseCreateUnixStreamPair(kq_base, ipc_base, (int*)&ipc_base->fd_arr, IPC_IOBUF_SZ);

	/* Failed, bail out */
	if (!op_status)
	{
		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "Failed to create SOCKET_PAIR - OP_STATUS [%d]\n", op_status);
		return 0;
	}

	/* AAAAAAAAAARRGH */
	child_pid = fork();

	/* There was an error forking, bail out */
	if (child_pid < 0)
	{
		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "Failed to FORK - OP_STATUS [%d]\n", child_pid);

		/* Close any possible open FD on the stream */
		EvIPCBaseCloseFDArr(ipc_base->parent_ev_base, (int*)&ipc_base->fd_arr);
		return 0;
	}

	/* PARENT execution path */
	if (child_pid > 0)
	{
		/* Close shared socket with CHILD */
		close(ipc_base->fd_arr[IPC_FDTYPE_CHILD_READ]);

		/* If read and write are different, close write */
		if ( (ipc_base->fd_arr[IPC_FDTYPE_CHILD_READ] != ipc_base->fd_arr[IPC_FDTYPE_CHILD_WRITE]) && (ipc_base->fd_arr[IPC_FDTYPE_CHILD_WRITE] >= 0)  )
			close(ipc_base->fd_arr[IPC_FDTYPE_CHILD_WRITE]);

		/* If read and error are different, close error */
		if ( (ipc_base->fd_arr[IPC_FDTYPE_CHILD_READ] != ipc_base->fd_arr[IPC_FDTYPE_CHILD_ERROR]) && (ipc_base->fd_arr[IPC_FDTYPE_CHILD_ERROR] >= 0)  )
			close(ipc_base->fd_arr[IPC_FDTYPE_CHILD_ERROR]);

		/* Mark as not initialized */
		ipc_base->fd_arr[IPC_FDTYPE_CHILD_READ]		= -1;
		ipc_base->fd_arr[IPC_FDTYPE_CHILD_WRITE]	= -1;
		ipc_base->fd_arr[IPC_FDTYPE_CHILD_ERROR]	= -1;

		/* Clean handshake buffer */
		memset(&handshake_buf, 0, HANDSHAKE_BUF_SZ);

		/* WARNING: THIS WILL BLOCK - Try to read the handshake, block until target PID is alive and write to US */
		handshake_read_sz = read(ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ], &handshake_buf, HANDSHAKE_BUF_SZ - 1);

		/* Failed to read handshake */
		if (handshake_read_sz < 0)
		{
			KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "PARENT handshake read test failed - Returned [%d]\n", handshake_read_sz);

			/* Close any possible open FD on the stream */
			EvIPCBaseCloseFDArr(ipc_base->parent_ev_base, (int*)&ipc_base->fd_arr);
			return 0;
		}
		/* This is not our handshake, bail out */
		else if ((handshake_read_sz >= HANDSHAKE_STRING_SZ) && strncmp(handshake_buf, HANDSHAKE_STRING, HANDSHAKE_STRING_SZ))
		{
			KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "PARENT handshake read test failed - [Wrong handshake (%10s)]\n", handshake_buf);

			/* Close any possible open FD on the stream */
			EvIPCBaseCloseFDArr(ipc_base->parent_ev_base, (int*)&ipc_base->fd_arr);
			return 0;
		}

		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "PARENT Handshake OK, read [%d] bytes\n", handshake_read_sz);

		/* Grab the child PID and set flags as READY */
		ipc_base->child_pid		= child_pid;
		ipc_base->flags.ready	= 1;

		/* Set parent FDs to non_blocking mode */
		EvKQBaseSocketSetNonBlock(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ]);
		EvKQBaseSocketSetNonBlock(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_WRITE]);
		EvKQBaseSocketSetNonBlock(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_ERROR]);

		/* Set parent FDs to CLOSE_ON_EXEC mode */
		EvKQBaseSocketSetCloseOnExec(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ]);
		EvKQBaseSocketSetCloseOnExec(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_WRITE]);
		EvKQBaseSocketSetCloseOnExec(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_ERROR]);

		/* Schedule IO events */
		EvKQBaseSetEvent(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ], COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventRead, ipc_base);
		EvKQBaseSetEvent(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ], COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventClose, ipc_base);
		EvKQBaseSetEvent(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_ERROR], COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventReadError, ipc_base);

		/* Set description */
		EvKQBaseFDDescriptionSetByFD(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ], "BRB_EV - IPC_BASE - PARENT_READ_FD");
		EvKQBaseFDDescriptionSetByFD(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_WRITE], "BRB_EV - IPC_BASE - PARENT_WRITE_FD");
		EvKQBaseFDDescriptionSetByFD(kq_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_ERROR], "BRB_EV - IPC_BASE - PARENT_ERROR_FD");

		/* Schedule JOB to dispatch EXECUTE event on NEXT IO LOOP */
		EvKQJobsAdd(kq_base, JOB_ACTION_ADD_VOLATILE, 0, EvIPCBaseExecuteEventJob, ipc_base);
		return 1;
	}
	/* Here we are running as CHILD */
	else
	{
		int shadow_read;
		int shadow_write;
		int shadow_error;

		/* Close shared socket with PARENT */
		close(ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ]);

		/* If read and write are different, close write */
		if ( (ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ] != ipc_base->fd_arr[IPC_FDTYPE_PARENT_WRITE]) && (ipc_base->fd_arr[IPC_FDTYPE_PARENT_WRITE] >= 0)  )
			close(ipc_base->fd_arr[IPC_FDTYPE_PARENT_WRITE]);

		/* If read and error are different, close error */
		if ( (ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ] != ipc_base->fd_arr[IPC_FDTYPE_PARENT_ERROR]) && (ipc_base->fd_arr[IPC_FDTYPE_PARENT_ERROR] >= 0)  )
			close(ipc_base->fd_arr[IPC_FDTYPE_PARENT_ERROR]);

		/* Mark as not initialized */
		ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ]	= -1;
		ipc_base->fd_arr[IPC_FDTYPE_PARENT_WRITE]	= -1;
		ipc_base->fd_arr[IPC_FDTYPE_PARENT_ERROR]	= -1;

		/* Write the handshake to PARENT */
		handshake_write_sz = write(ipc_base->fd_arr[IPC_FDTYPE_CHILD_WRITE], HANDSHAKE_STRING, strlen(HANDSHAKE_STRING));

		/* Failed handshaking with PARENT */
		if (handshake_write_sz < 0)
		{
			KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "CHILD handshake read test failed - Returned [%d]\n", handshake_write_sz);

			/* Close any possible open FD on the stream */
			EvIPCBaseCloseFDArr(kq_base, (int*)&ipc_base->fd_arr);

			/* Good bye cruel world */
			exit(1);
		}

		/* Do the black magic and kidnap STDIN, STDOUT and STDERR */
		shadow_read		= dup(ipc_base->fd_arr[IPC_FDTYPE_CHILD_READ]);
		shadow_write	= dup(ipc_base->fd_arr[IPC_FDTYPE_CHILD_WRITE]);
		shadow_error	= dup(ipc_base->fd_arr[IPC_FDTYPE_CHILD_ERROR]);

		close(ipc_base->fd_arr[IPC_FDTYPE_CHILD_READ]);
		close(ipc_base->fd_arr[IPC_FDTYPE_CHILD_WRITE]);
		close(ipc_base->fd_arr[IPC_FDTYPE_CHILD_ERROR]);

		/* Kidnap STDIN and STDOUT */
		dup2(shadow_read, 0);
		dup2(shadow_write, 1);
		dup2(shadow_error, 2);

		close(shadow_read);
		close(shadow_write);
		close(shadow_error);

		/* Execute target, controlled binary */
		setsid();
		execvp(prog, args);

		/* Good bye cruel world */
		exit(1);

		return 1;
	}


	return 0;
}
/**************************************************************************************************************************/
void EvIPCWriteStringFmt(EvIPCBase *ipc_base, IPCEvCBH *finish_cb, void *finish_cbdata, char *string, ...)
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
	aio_req = EvAIOReqNew(&ipc_base->write_queue, -1, ipc_base, buf_ptr, buf_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue it and begin writing ASAP */
	EvIPCEnqueueAndKickQueue(ipc_base, aio_req);

	return;
}
/**************************************************************************************************************************/
void EvIPCWriteMemBuffer(EvIPCBase *ipc_base, IPCEvCBH *finish_cb, void *finish_cbdata, MemBuffer *mb_ptr)
{
	EvAIOReq *aio_req;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ipc_base->write_queue, -1, ipc_base, MemBufferDeref(mb_ptr), MemBufferGetSize(mb_ptr), 0, NULL, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue it and begin writing ASAP */
	EvIPCEnqueueAndKickQueue(ipc_base, aio_req);

	return;
}
/**************************************************************************************************************************/
void EvIPCWriteOffsetMemBuffer(EvIPCBase *ipc_base, IPCEvCBH *finish_cb, void *finish_cbdata, MemBuffer *mb_ptr, int offset)
{
	EvAIOReq *aio_req;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ipc_base->write_queue, -1, ipc_base, MemBufferOffsetDeref(mb_ptr, offset), (MemBufferGetSize(mb_ptr) - offset), 0, NULL, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue it and begin writing ASAP */
	EvIPCEnqueueAndKickQueue(ipc_base, aio_req);

	return;
}
/**************************************************************************************************************************/
void EvIPCBaseEventCancel(EvIPCBase *ipc_base, int ev_type)
{
	/* Set event */
	ipc_base->events[ev_type].cb_handler_ptr		= NULL;
	ipc_base->events[ev_type].cb_data_ptr			= NULL;

	/* Mark disabled */
	ipc_base->events[ev_type].flags.enabled			= 0;

	return;
}
/**************************************************************************************************************************/
void EvIPCBaseEventCancelAll(EvIPCBase *ipc_base)
{
	int i;

	/* Cancel all possible events */
	for (i = 0; i < IPC_EVENT_LASTITEM; i++)
	{
		EvIPCBaseEventCancel(ipc_base, i);
	}

	return;
}
/**************************************************************************************************************************/
int EvIPCBaseEventIsSet(EvIPCBase *ipc_base, int ev_type)
{
	/* Sanity check */
	if (ev_type >= IPC_EVENT_LASTITEM)
		return 0;

	if (ipc_base->events[ev_type].cb_handler_ptr && ipc_base->events[ev_type].flags.enabled)
		return 1;

	return 0;
}
/**************************************************************************************************************************/
void EvIPCBaseEventSet(EvIPCBase *ipc_base, int ev_type, IPCEvCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= IPC_EVENT_LASTITEM)
		return;

	/* Set event */
	ipc_base->events[ev_type].cb_handler_ptr	= cb_handler;
	ipc_base->events[ev_type].cb_data_ptr		= cb_data;

	/* Mark enabled */
	ipc_base->events[ev_type].flags.enabled		= 1;

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void EvIPCBaseCloseFDArr(EvKQBase *kq_base, int *fd_arr)
{
	int i;

	/* Close PARENT FDs, if any */
	if (fd_arr[IPC_FDTYPE_PARENT_READ] > 0)
		EvKQBaseSocketClose(kq_base, fd_arr[IPC_FDTYPE_PARENT_READ]);

	if ( (fd_arr[IPC_FDTYPE_PARENT_READ] != fd_arr[IPC_FDTYPE_PARENT_WRITE]) && (fd_arr[IPC_FDTYPE_PARENT_WRITE] > 0)  )
		EvKQBaseSocketClose(kq_base, fd_arr[IPC_FDTYPE_PARENT_WRITE]);

	if ( (fd_arr[IPC_FDTYPE_PARENT_READ] != fd_arr[IPC_FDTYPE_PARENT_ERROR]) && (fd_arr[IPC_FDTYPE_PARENT_ERROR] > 0)  )
		EvKQBaseSocketClose(kq_base, fd_arr[IPC_FDTYPE_PARENT_ERROR]);

	/* Close CHILD FDs, if any */
	if (fd_arr[IPC_FDTYPE_CHILD_READ] > 0)
		EvKQBaseSocketClose(kq_base, fd_arr[IPC_FDTYPE_CHILD_READ]);

	if ( (fd_arr[IPC_FDTYPE_CHILD_READ] != fd_arr[IPC_FDTYPE_CHILD_WRITE]) && (fd_arr[IPC_FDTYPE_CHILD_WRITE] > 0)  )
		EvKQBaseSocketClose(kq_base, fd_arr[IPC_FDTYPE_CHILD_WRITE]);

	if ( (fd_arr[IPC_FDTYPE_CHILD_READ] != fd_arr[IPC_FDTYPE_CHILD_ERROR]) && (fd_arr[IPC_FDTYPE_CHILD_ERROR] > 0)  )
		EvKQBaseSocketClose(kq_base, fd_arr[IPC_FDTYPE_CHILD_ERROR]);

	/* UnInit out fd_arr */
	fd_arr[IPC_FDTYPE_PARENT_READ]	= -1;
	fd_arr[IPC_FDTYPE_PARENT_WRITE] = -1;
	fd_arr[IPC_FDTYPE_PARENT_ERROR] = -1;

	fd_arr[IPC_FDTYPE_CHILD_READ]	= -1;
	fd_arr[IPC_FDTYPE_CHILD_WRITE]	= -1;
	fd_arr[IPC_FDTYPE_CHILD_ERROR]	= -1;

	return;

}
/**************************************************************************************************************************/
static int EvIPCBaseCreateUnixStreamPair(EvKQBase *kq_base, EvIPCBase *ipc_base, int *fd_arr, int iobuf_sz)
{
	int std_fds[2];
	int err_fds[2];

	int op_status;

	/* Create standard socket pair for STDIN and STDOUT IO */
	op_status = socketpair(AF_UNIX, SOCK_STREAM, 0, std_fds);

	/* Operation failed */
	if ((op_status < 0) || (std_fds[0] < 0) || (std_fds[1] < 0))
	{
		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "Failed to create socket_pair for STDIN/STDOUT - OP_STATUS [%d]\n", op_status);

		/* Close any possible open FD on the stream */
		EvIPCBaseCloseFDArr(kq_base, fd_arr);
		return 0;
	}

	/* Create standard socket pair for STDERR IO */
	op_status = socketpair(AF_UNIX, SOCK_STREAM, 0, err_fds);

	/* Operation failed */
	if ((op_status < 0) || (err_fds[0] < 0) || (err_fds[1] < 0))
	{
		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "Failed to create socket_pair for STDERR\n", op_status);

		/* Close any possible open FD on the stream */
		EvIPCBaseCloseFDArr(kq_base, fd_arr);
		return 0;
	}


	/* Set the buffer size for STDIN and STDOUT */
	setsockopt(std_fds[0], SOL_SOCKET, SO_SNDBUF, (void*) &iobuf_sz, sizeof(int));
	setsockopt(std_fds[0], SOL_SOCKET, SO_RCVBUF, (void*) &iobuf_sz, sizeof(int));
	setsockopt(std_fds[1], SOL_SOCKET, SO_SNDBUF, (void*) &iobuf_sz, sizeof(int));
	setsockopt(std_fds[1], SOL_SOCKET, SO_RCVBUF, (void*) &iobuf_sz, sizeof(int));

	/* Set the buffer size for STDERR */
	setsockopt(err_fds[0], SOL_SOCKET, SO_SNDBUF, (void*) &iobuf_sz, sizeof(int));
	setsockopt(err_fds[0], SOL_SOCKET, SO_RCVBUF, (void*) &iobuf_sz, sizeof(int));
	setsockopt(err_fds[1], SOL_SOCKET, SO_SNDBUF, (void*) &iobuf_sz, sizeof(int));
	setsockopt(err_fds[1], SOL_SOCKET, SO_RCVBUF, (void*) &iobuf_sz, sizeof(int));

	/* No delay for these */
	EvKQBaseSocketSetNoDelay(kq_base, std_fds[0]);
	EvKQBaseSocketSetNoDelay(kq_base, std_fds[1]);
	EvKQBaseSocketSetNoDelay(kq_base, err_fds[0]);
	EvKQBaseSocketSetNoDelay(kq_base, err_fds[1]);

	/* Set pointers */
	fd_arr[IPC_FDTYPE_PARENT_READ] 	= std_fds[0];
	fd_arr[IPC_FDTYPE_PARENT_WRITE]	= std_fds[0];
	fd_arr[IPC_FDTYPE_PARENT_ERROR]	= err_fds[0];

	fd_arr[IPC_FDTYPE_CHILD_READ] 	= std_fds[1];
	fd_arr[IPC_FDTYPE_CHILD_WRITE] 	= std_fds[1];
	fd_arr[IPC_FDTYPE_CHILD_ERROR] 	= err_fds[1];

	return 1;
}
/**************************************************************************************************************************/
static void EvIPCEnqueueAndKickQueue(EvIPCBase *ipc_base, EvAIOReq *aio_req)
{
	/* Enqueue it in conn_queue */
	EvAIOReqQueueEnqueue(&ipc_base->write_queue, aio_req);

	/* Ask the event base to write */
	EvIPCKickConnWriteQueue(ipc_base);

	return;
}
/**************************************************************************************************************************/
static void EvIPCKickConnWriteQueue(EvIPCBase *ipc_base)
{
	/* Ask the event base to write */
	EvKQBaseSetEvent(ipc_base->parent_ev_base, ipc_base->fd_arr[IPC_FDTYPE_PARENT_WRITE], COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventWrite, ipc_base);

	return;
}
/**************************************************************************************************************************/
static int EvIPCBaseEventDispatchInternal(EvIPCBase *ipc_base, int data_sz, int thrd_id, int ev_type)
{
	IPCEvCBH *cb_handler		= NULL;
	void *cb_handler_data		= NULL;

	/* Grab callback_ptr */
	cb_handler = ipc_base->events[ev_type].cb_handler_ptr;

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Grab data for this CBH */
		cb_handler_data = ipc_base->events[ev_type].cb_data_ptr;

		/* Jump into CBH. Base for this event is EvIPCBase* */
		cb_handler(ipc_base->fd_arr[IPC_FDTYPE_PARENT_READ], data_sz, thrd_id, cb_handler_data, ipc_base);

		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvICPBaseEventReadError(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvIPCBase *ipc_base;
	EvKQBase *ev_base;
	int data_read;
	int op_status;

	/* Grab data back */
	ipc_base	= cb_data;
	ev_base		= ipc_base->parent_ev_base;

	/* This is a closing connection, bail out */
	if (!read_sz)
	{
		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Zero SIZED READ for target [%s]\n", ipc_base->target_name);
		return 0;
	}

	/* Create a new read_stream object */
	if (!ipc_base->error_buffer)
		ipc_base->error_buffer = MemBufferNew(8092, (ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));

	/* Grab data from FD directly into error_buffer, zero-copy */
	data_read = MemBufferAppendFromFD(ipc_base->error_buffer, read_sz, fd, 0);

	KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "[%d]-[%s]\n", MemBufferGetSize(ipc_base->error_buffer), MemBufferDeref(ipc_base->error_buffer));

	/* Dispatch internal event */
	op_status = EvIPCBaseEventDispatchInternal(ipc_base, data_read, thrd_id, IPC_EVENT_ERROR);

	/* No upper layer event for this READ, clean buffer */
	if (!op_status)
		MemBufferClean(ipc_base->error_buffer);

	/* Reschedule read event */
	EvKQBaseSetEvent(ev_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventReadError, ipc_base);

	return data_read;
}
/**************************************************************************************************************************/
static int EvICPBaseEventRead(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvIPCBase *ipc_base;
	EvKQBase *ev_base;
	int data_read;
	int op_status;

	/* Grab data back */
	ipc_base	= cb_data;
	ev_base		= ipc_base->parent_ev_base;

	/* This is a closing connection, bail out */
	if (!read_sz)
	{
		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Zero SIZED READ for target [%s]\n", ipc_base->target_name);
		return 0;
	}

	/* Create a new read_stream object */
	if (!ipc_base->read_buffer)
		ipc_base->read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (read_sz + 1));

	/* Grab data from FD directly into read_buffer, zero-copy */
	data_read = MemBufferAppendFromFD(ipc_base->read_buffer, read_sz, fd, 0);

	KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "[%d]-[%s]\n", MemBufferGetSize(ipc_base->read_buffer), MemBufferDeref(ipc_base->read_buffer));

	/* Dispatch internal event */
	op_status = EvIPCBaseEventDispatchInternal(ipc_base, data_read, thrd_id, IPC_EVENT_READ);

	/* No upper layer event for this READ, clean buffer */
	if (!op_status)
		MemBufferClean(ipc_base->read_buffer);

	/* Reschedule read event */
	EvKQBaseSetEvent(ev_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventRead, ipc_base);

	return data_read;
}
/**************************************************************************************************************************/
static int EvICPBaseEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOReq *cur_aio_req;

	char *data_ptr;
	int still_can_write_sz;
	unsigned long wanted_write_sz;
	int wrote_sz;
	int total_wrote_sz = 0;

	EvKQBase *ev_base				= base_ptr;
	EvIPCBase *ipc_base				= cb_data;

	/* Label used to write multiple aio_reqs in the same write_window, if possible */
	write_again:

	/* Grab aio_req unit */
	cur_aio_req			= EvAIOReqQueueDequeue(&ipc_base->write_queue);

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
		EvAIOReqQueueEnqueueHead(&ipc_base->write_queue, cur_aio_req);
		cur_aio_req->err = errno;

		if (errno == EINTR || errno == EAGAIN)
		{
			/* Reschedule write event and return */
			EvKQBaseSetEvent(ev_base, fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventWrite, ipc_base);

			return total_wrote_sz;
		}

		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - FATAL WRITE ERROR - CAN_WRITE_SZ [%d]\n", fd, can_write_sz);
		return total_wrote_sz;
	}

	/* Write_ok, update offset and counter */
	cur_aio_req->data.offset	+= wrote_sz;
	total_wrote_sz				+= wrote_sz;

	/* Write is complete */
	if (cur_aio_req->data.offset >= cur_aio_req->data.size)
	{
		//KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - FULL write of [%d] bytes - Wanted [%lu] - Offset is now [%lu] on THREAD [%d]\n",
		//						fd, wrote_sz, wanted_write_sz, cur_aio_req->write_offset, thrd_id);

		/* Dispatch write finish event, if there is anyone interested */
		if (cur_aio_req->finish_cb)
			cur_aio_req->finish_cb(fd, cur_aio_req->data.offset, thrd_id, cur_aio_req->finish_cbdata, ipc_base);

		/* Destroy current aio_req */
		EvAIOReqDestroy(cur_aio_req);

		/* Next aio_req exists, loop writing some more or reschedule write event */
		if (!EvAIOReqQueueIsEmpty(&ipc_base->write_queue))
		{
			DLinkedList *aio_req_list = &ipc_base->write_queue.aio_req_list;

			/* Get a reference to next element */
			cur_aio_req = (EvAIOReq*)aio_req_list->head->data;

			/* Calculate wanted data size and how many bytes we have left to write */
			wanted_write_sz		= cur_aio_req->data.size - cur_aio_req->data.offset;
			still_can_write_sz	= can_write_sz - total_wrote_sz;

			/* Keep writing as many aio_reqs the kernel allow us to */
			if (wanted_write_sz <= still_can_write_sz)
			{
				KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - We can still write [%d] bytes qnd next aio_req wants [%d] bytes, loop to grab it\n",
						fd, still_can_write_sz, wanted_write_sz);
				goto write_again;
			}
			/* No more room to write, reschedule write event */
			else
			{
				KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - We can still write [%d] bytes and next aio_req wants [%d] bytes, RESCHEDULE WRITE_EV\n",
						fd, still_can_write_sz, wanted_write_sz);

				/* Reschedule write event */
				EvKQBaseSetEvent(ev_base, fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventWrite, ipc_base);
			}
		}
	}
	else
	{
		KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - PARTIAL write of [%d] bytes - Offset is now [%d]\n", fd, wrote_sz, cur_aio_req->data.offset);

		/* REENQUEUE and reschedule write event */
		EvAIOReqQueueEnqueueHead(&ipc_base->write_queue, cur_aio_req);
		EvKQBaseSetEvent(ev_base, fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, EvICPBaseEventWrite, ipc_base);
	}

	return total_wrote_sz;

}
/**************************************************************************************************************************/
static int EvICPBaseEventClose(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvIPCBase *ipc_base;
	EvKQBase *ev_base;

	/* Grab data back */
	ipc_base	= cb_data;
	ev_base		= ipc_base->parent_ev_base;

	/* Still pending data, bail out */
	if (read_sz > 0)
		return 0;

	KQBASE_LOG_PRINTF(ipc_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Close event on IPC [%p] with base [%p] - orig_base is [%p]\n", fd, ipc_base, ev_base, base_ptr);

	/* Close any possible open FD on the stream */
	EvIPCBaseCloseFDArr(ev_base, (int*)&ipc_base->fd_arr);

	/* IPC base is no longer ready */
	ipc_base->flags.ready = 0;

	/* Dispatch internal event */
	EvIPCBaseEventDispatchInternal(ipc_base, read_sz, thrd_id, IPC_EVENT_CLOSE);

	return 1;
}
/**************************************************************************************************************************/
static void EvIPCBaseSystemInternalClose(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvIPCBase *ipc_base = base_ptr;

	EvIPCBaseDestroy(ipc_base);
	return;
}
/**************************************************************************************************************************/
static int EvIPCBaseExecuteEventJob(void *job, void *cbdata_ptr)
{
	EvIPCBase *ipc_base = cbdata_ptr;

	/* Dispatch internal event */
	EvIPCBaseEventDispatchInternal(ipc_base, 0, -1, IPC_EVENT_EXECUTE);

	return 1;
}
/**************************************************************************************************************************/

