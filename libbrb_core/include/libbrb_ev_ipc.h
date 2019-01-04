/*
 * libbrb_ev_ipc.h
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

#ifndef LIBBRB_IPC_H_
#define LIBBRB_IPC_H_

#define IPC_SELFSYNC_MAX_TOKEN_SZ 				16
#define IPC_IOBUF_SZ							65535
#define HANDSHAKE_BUF_SZ						128
#define HANDSHAKE_STRING    					"IPC_HANDSHAKE\n"
#define HANDSHAKE_STRING_SZ 					13
#define STDIN									0
#define STDOUT									1
#define STDERR									2

typedef enum {
	IPC_FDTYPE_PARENT_READ,
	IPC_FDTYPE_PARENT_WRITE,
	IPC_FDTYPE_PARENT_ERROR,
	IPC_FDTYPE_CHILD_READ,
	IPC_FDTYPE_CHILD_WRITE,
	IPC_FDTYPE_CHILD_ERROR,
	IPC_FDTYPE_LASTITEM
} EvIPCStreamFDType;

typedef enum {
	IPC_EVENT_READ,
	IPC_EVENT_EXECUTE,
	IPC_EVENT_CLOSE,
	IPC_EVENT_ERROR,
	IPC_EVENT_LASTITEM
} EvIPCEventCodes;

typedef void IPCEvCBH(int, int, int, void *, void *);
typedef void IPCEvDestroyFunc(void *);

typedef struct _EvIPCBase
{

	struct _EvKQBase *parent_ev_base;
	struct _EvKQBaseLogBase *log_base;
	int fd_arr[IPC_FDTYPE_LASTITEM];
	int child_pid;
	char target_name[128];

	MemBuffer *read_buffer;
	MemBuffer *error_buffer;
	EvAIOReqQueue write_queue;

	struct
	{
		IPCEvCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct
		{
			unsigned int enabled:1;
			unsigned int mutex_init:1;
		} flags;
	} events[IPC_EVENT_LASTITEM];

	struct
	{
		unsigned int ready:1;
	} flags;
} EvIPCBase;


typedef struct _EvIPCBaseChildConf
{
	struct
	{
		char *token_str;
		int max_buffer_sz;
	} self_sync;

} EvIPCBaseChildConf;

typedef struct _EvIPCBaseChild
{

	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;
	EvAIOReqQueue write_queue;

	MemBuffer *partial_read_buffer;
	MemBuffer *read_buffer;

	struct
	{
		IPCEvCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct
		{
			unsigned int enabled:1;
			unsigned int mutex_init:1;
		} flags;
	} events[IPC_EVENT_LASTITEM];

	struct
	{
		char token_str_buf[IPC_SELFSYNC_MAX_TOKEN_SZ];
		int token_str_sz;
		int max_buffer_sz;
	} self_sync;

	struct
	{
		unsigned int ready:1;
		unsigned int self_sync:1;
	} flags;

} EvIPCBaseChild;



/* IPC_BASE -> EXECUTOR SIDE */
EvIPCBase *EvIPCBaseNew(struct _EvKQBase *kq_base);
void EvIPCBaseDestroy(EvIPCBase *ipc_base);
int EvIPCBaseSystemWithStrArr(struct _EvKQBase *ev_base, StringArray *cmd_parts_strarr);
int EvIPCBaseSystem(struct _EvKQBase *ev_base, char *cmd, ...);
int EvIPCBaseExecute(EvIPCBase *ipc_base, char *prog, char **args);
void EvIPCWriteStringFmt(EvIPCBase *ipc_base, IPCEvCBH *finish_cb, void *finish_cbdata, char *string, ...);
void EvIPCWriteMemBuffer(EvIPCBase *ipc_base, IPCEvCBH *finish_cb, void *finish_cbdata, MemBuffer *mb_ptr);
void EvIPCBaseEventCancel(EvIPCBase *ipc_base, int ev_type);
void EvIPCBaseEventCancelAll(EvIPCBase *ipc_base);
int EvIPCBaseEventIsSet(EvIPCBase *ipc_base, int ev_type);
void EvIPCBaseEventSet(EvIPCBase *ipc_base, int ev_type, IPCEvCBH *cb_handler, void *cb_data);

/* IPC_BASE CHILD -> EXECUTED SIDE */
EvIPCBaseChild *EvIPCBaseChildNew(struct _EvKQBase *kq_base, EvIPCBaseChildConf *ipc_base_child_conf);
void EvIPCBaseChildInit(struct _EvKQBase *kq_base, EvIPCBaseChild *ipc_base_child);
void EvIPCBaseChildDestroy(EvIPCBaseChild *ipc_base_child);
void EvIPCBaseChildWriteStringFmt(EvIPCBaseChild *ipc_base_child, IPCEvCBH *finish_cb, void *finish_cbdata, char *string, ...);
void EvIPCBaseChildWriteMemBuffer(EvIPCBaseChild *ipc_base_child, IPCEvCBH *finish_cb, void *finish_cbdata, MemBuffer *mb_ptr);
void EvIPCBaseChildWrite(EvIPCBaseChild *ipc_base_child, IPCEvCBH *finish_cb, void *finish_cbdata, char *buf_ptr, long buf_sz);
void EvIPCWriteOffsetMemBuffer(EvIPCBase *ipc_base, IPCEvCBH *finish_cb, void *finish_cbdata, MemBuffer *mb_ptr, int offset);
void EvIPCBaseChildEventCancel(EvIPCBaseChild *ipc_base_child, int ev_type);
void EvIPCBaseChildEventCancelAll(EvIPCBaseChild *ipc_base_child);
int EvIPCBaseChildEventIsSet(EvIPCBaseChild *ipc_base_child, int ev_type);
void EvIPCBaseChildEventSet(EvIPCBaseChild *ipc_base_child, int ev_type, IPCEvCBH *cb_handler, void *cb_data);






#endif /* LIBBRB_IPC_H_ */
