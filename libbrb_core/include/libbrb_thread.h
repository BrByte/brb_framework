/*
 * libbrb_thread.h
 *
 *  Created on: 2013-12-11
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

#ifndef LIBBRB_THRD_H_
#define LIBBRB_THRD_H_

#define THREAD_POOL_JOBS_MAX_ENQUEUED_COUNT 4096
#define THREAD_POOL_JOBS_MAX_USERDATA_SIZE	1024

typedef void *ThreadInstanceEntryPoint(void *);
typedef int ThreadInstanceJobFinishCB(void *, void *);

typedef void ThreadInstanceJobCB_Generic(void *, void *);
typedef void ThreadInstanceJobCB_RetNONE(void *, void *);
typedef void *ThreadInstanceJobCB_RetPTR(void *, void *);
typedef int ThreadInstanceJobCB_RetINT(void *, void *);
typedef long ThreadInstanceJobCB_RetLONG(void *, void *);
typedef float ThreadInstanceJobCB_RetFLOAT(void *, void *);

typedef enum
{
	THREAD_INSTANCE_STATE_STARTING,
	THREAD_INSTANCE_STATE_FREE,
	THREAD_INSTANCE_STATE_BUSY,
	THREAD_INSTANCE_STATE_FAILED,
	THREAD_INSTANCE_STATE_DONE,
	THREAD_INSTANCE_STATE_SHUTDOWN,
	THREAD_INSTANCE_STATE_LASTITEM
} ThreadPoolInstanceCurrentStatus;

typedef enum
{
	THREAD_JOB_RETVAL_NONE,
	THREAD_JOB_RETVAL_INT,
	THREAD_JOB_RETVAL_LONG,
	THREAD_JOB_RETVAL_PTR,
	THREAD_JOB_RETVAL_FLOAT,
	THREAD_JOB_RETVAL_LASTITEM
} ThreadJobReturnValueTypes;

typedef enum
{
	THREAD_NOTIFY_PIPE_MAIN,
	THREAD_NOTIFY_PIPE_THREADS,
	THREAD_NOTIFY_PIPE_LASTITEM,
} ThreadNotifyPipeCode;

typedef enum
{
	THREAD_LIST_JOB_PENDING,
	THREAD_LIST_JOB_WORKING,
	THREAD_LIST_JOB_DONE,
	THREAD_LIST_JOB_LASTITEM,
} ThreadJobListCode;

typedef struct _ThreadPoolInstance
{
	DLinkedListNode node;

	struct _ThreadPoolBase *thread_pool;
	pthread_t thread_id_sys;
	pthread_attr_t thread_attr;

	int thread_state;
	int thread_id_pool;

	struct
	{
		unsigned volatile int init:1;
		unsigned volatile int do_shutdown:1;
		unsigned volatile int done_shutdown:1;
	} flags;

} ThreadPoolInstance;

typedef struct _ThreadPoolJobProto
{
	ThreadInstanceJobCB_Generic *job_cbh_ptr;
	int retval_type;
	void *job_cbdata;

	ThreadInstanceJobFinishCB *job_finish_cbh_ptr;
	void *job_finish_cbdata;

	void *user_data;
	int user_int;
	long user_long;

	char *udata;
	int udata_sz;
} ThreadPoolJobProto;

typedef struct _ThreadPoolNotifyData
{
	int job_id;
	int thrd_id;
} ThreadPoolNotifyData;

typedef struct _ThreadPoolInstanceJob
{
	DLinkedListNode node;
	struct _ThreadPoolBase *parent_base;
	int job_id;
	int run_thread_id;
	int retval_type;

	void *user_data;
	int user_int;
	long user_long;

	struct
	{
		void *job_cbdata;

		union
		{
			ThreadInstanceJobCB_RetNONE *ret_none;
			ThreadInstanceJobCB_RetPTR *ret_ptr;
			ThreadInstanceJobCB_RetINT *ret_int;
			ThreadInstanceJobCB_RetLONG *ret_long;
			ThreadInstanceJobCB_RetFLOAT *ret_float;
		} cb_handler;

	} job_callback;

	struct
	{
		ThreadInstanceJobFinishCB *cbh_ptr;
		void *cbdata;
	} finish_callback;

	struct
	{
		struct timeval enqueue;
		struct timeval begin;
		struct timeval finish;
	} tv;

	struct
	{
		unsigned int in_use:1;
		unsigned int cancelled:1;
		unsigned int finished:1;
	} flags;

	union
	{
		int int_value;
		long long_value;
		char *ptr_value;
		float float_value;
	} job_retvalue;

	char user_data_buf[THREAD_POOL_JOBS_MAX_USERDATA_SIZE];

} ThreadPoolInstanceJob;

typedef struct _ThreadPoolBaseConfig
{
	struct _EvKQBaseLogBase *log_base;

	int worker_count_start;
	int worker_count_grow;
	int worker_count_max;
	int worker_stack_sz;
	int job_max_count;

	struct
	{
		unsigned int kevent_finish_notify:1;
	} flags;

} ThreadPoolBaseConfig;

typedef struct _ThreadPoolBase
{
	struct _EvKQBase *ev_base;
	struct _EvKQBaseLogBase *log_base;
	int notify_pipe[THREAD_NOTIFY_PIPE_LASTITEM];

	struct
	{
		MemSlotBase memslot;
	} jobs;

	struct
	{
		MemSlotBase memslot;
	} instances;

	struct
	{
		int worker_count_start;
		int worker_count_grow;
		int worker_count_max;
		int worker_stack_sz;
		int job_max_count;
	} config;

	struct
	{
		unsigned int kevent_finish_notify:1;
	} flags;

} ThreadPoolBase;


typedef struct _ThreadAIOBaseConf
{
	ThreadPoolBaseConfig pool_conf;
	struct _ThreadPoolBase *thrd_pool;
	struct _EvKQBaseLogBase *log_base;
	int pending_req_max;
	int worker_start;
	int worker_max;

	struct
	{
		unsigned int foo:1;
	} flags;

} ThreadAIOBaseConf;

typedef struct _ThreadAIOBase
{
	ThreadPoolBase *thrd_pool;
	EvAIOReqQueue req_queue;
	struct _EvKQBase *ev_base;
	struct _EvKQBaseLogBase *log_base;

	struct
	{
		struct
		{
			unsigned long tx_count;
			unsigned long rx_count;
			unsigned long bytes;
			unsigned long pending;
			unsigned long cancel;
			unsigned long error;
			unsigned long success_sched;
			unsigned long success_notify;
		} opcode[AIOREQ_OPCODE_LASTITEM];
	} stats;

	struct
	{
		unsigned int foo:1;
	} flags;

} ThreadAIOBase;

/* thread/thrd_pool_base.c */
ThreadPoolBase *ThreadPoolBaseNew(struct _EvKQBase *ev_base, ThreadPoolBaseConfig *thread_pool_conf);
void ThreadPoolBaseDestroy(ThreadPoolBase *thread_pool);
int ThreadPoolAllBusy(ThreadPoolBase *thread_pool);
int ThreadPoolJobCancel(ThreadPoolBase *thread_pool, int job_id);
int ThreadPoolJobEnqueue(ThreadPoolBase *thread_pool, ThreadPoolJobProto *job_proto);
int ThreadPoolJobConsumeReplies(ThreadPoolBase *thread_pool);

/* thread/thrd_aio_base.c */
ThreadAIOBase *ThreadAIOBaseNew(struct _EvKQBase *ev_base, ThreadAIOBaseConf *thrd_aio_base_conf);
int ThreadAIOBaseDestroy(ThreadAIOBase *thrd_aio_base);
int ThreadAIOBaseCancelByReqID(ThreadAIOBase *thrd_aio_base, int req_id);
int ThreadAIOFileOpen(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, int flags, mode_t mode, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileClose(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, int fd, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileReadToMemBuffer(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, int fd, char *path_str, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileRead(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, int fd, char *buffer, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileWriteFromMemBufferAndDestroy(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, MemBuffer *data_mb,
		int fd, char *path_str, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileWriteFromMemBuffer(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, MemBuffer *data_mb,
		int fd, char *path_str, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileWrite(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, int fd, char *buffer, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileStat(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileUnlink(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIOFileTruncate(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, long size, EvAIOReqCBH *finish_cb, void *finish_cbdata);

int ThreadAIODeviceMount(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, char *dev_str, int retry_count, int flags, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int ThreadAIODeviceUnMount(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, char *dev_str, int retry_count, int flags, EvAIOReqCBH *finish_cb, void *finish_cbdata);


#endif /* LIBBRB_THRD_H_ */
