/*
 * thrd_aio_base.c
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

#include <libbrb_core.h>

static ThreadInstanceJobCB_RetINT ThreadAIOEngine_JobDispatcher;
static ThreadInstanceJobFinishCB ThreadAIOEngine_JobFinish;
static int ThreadAIOEngine_JobGenericInitialize(ThreadAIOBase *thrd_aio_base, ThreadPoolJobProto *job_proto, EvAIOReq *aio_req, int aio_opcode);

static int ThreadAIOEngine_OpcodeDispatch(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeOpen(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeClose(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeReadToMemBuffer(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeRead(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeWriteFromMemBuffer(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeWrite(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeStat(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeUnlink(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeTruncate(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeOpenDir(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeMount(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);
static void ThreadAIOEngine_OpcodeUnMount(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req);

/**************************************************************************************************************************/
ThreadAIOBase *ThreadAIOBaseNew(EvKQBase *ev_base, ThreadAIOBaseConf *thrd_aio_base_conf)
{
	ThreadPoolBaseConfig thread_pool_conf;
	ThreadAIOBase *thrd_aio_base;

	int max_pending_req = (thrd_aio_base_conf->pending_req_max ? thrd_aio_base_conf->pending_req_max : 4096);

	/* Create a new THREAD_IO_BASE and link with EV_BASE */
	thrd_aio_base			= calloc(1, sizeof(ThreadAIOBase));
	thrd_aio_base->ev_base	= ev_base;
	thrd_aio_base->log_base = thrd_aio_base_conf->log_base;

	/* Initialize EV_AIO_QUEUE and fire UP internal THREAD_POOL */
	EvAIOReqQueueInit(ev_base, &thrd_aio_base->req_queue, max_pending_req, (ev_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SLOTTED);

	/* Upper layers defined a thread pool to be used, use it */
	if (thrd_aio_base_conf->thrd_pool)
	{
		thrd_aio_base->thrd_pool = thrd_aio_base_conf->thrd_pool;
	}
	/* Fire up a new thread pool to work for us */
	else
	{
		/* Clean up stack and fill up DEFAULTs */
		memset(&thread_pool_conf, 0, sizeof(ThreadPoolBaseConfig));
		thread_pool_conf.worker_count_start		= (thrd_aio_base_conf->worker_start ? thrd_aio_base_conf->worker_start : 4);
		thread_pool_conf.worker_count_max		= (thrd_aio_base_conf->worker_max ? thrd_aio_base_conf->worker_max : 32);
		thread_pool_conf.log_base				= (thrd_aio_base_conf->log_base);

		/* Shoot! */
		thrd_aio_base->thrd_pool = ThreadPoolBaseNew(ev_base, (thrd_aio_base_conf ? &thrd_aio_base_conf->pool_conf : &thread_pool_conf));
	}


	return thrd_aio_base;
}
/**************************************************************************************************************************/
int ThreadAIOBaseDestroy(ThreadAIOBase *thrd_aio_base)
{
	/* Sanity check */
	if (!thrd_aio_base)
		return 0;

	/* Stop and destroy THREAD_POOL */
	ThreadPoolBaseDestroy(thrd_aio_base->thrd_pool);

	free(thrd_aio_base);
	return 1;
}
/**************************************************************************************************************************/
int ThreadAIOBaseCancelByReqID(ThreadAIOBase *thrd_aio_base, int req_id)
{
	EvAIOReq *aio_req;

	/* Sanity check */
	if (req_id < 0)
		return 0;

	/* Search AIO_REQ */
	aio_req = EvAIOReqQueueGrabByID(&thrd_aio_base->req_queue, req_id);
	assert(aio_req->flags.in_use);

	/* Mark as canceled */
	aio_req->flags.cancelled = 1;
	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int ThreadAIOFileOpen(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, int flags, mode_t mode, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, -1, thrd_aio_base, NULL, -1, -1, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued AIO_REQs\n", path_str);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);
	assert(path_str);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_OPEN);

	/* Populate OPEN_SPECIFIC data */
	aio_req->file.path_str		= strdup(path_str);
	aio_req->file.flags			= flags;
	aio_req->file.mode			= mode;
	aio_req->flags.dup_path_str = 1;

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued THREAD_JOBs\n", path_str);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_OPEN].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_OPEN].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIOFileClose(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, int fd, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Sanity check */
	if (fd < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Trying to close NEGATIVE_FD\n", fd);
		goto aio_failed;
	}

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, fd, thrd_aio_base, NULL, -1, -1, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued AIO_REQs\n", fd);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_CLOSE);

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued THREAD_JOBs\n", fd);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_CLOSE].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_CLOSE].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIOFileReadToMemBuffer(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, int fd, char *path_str, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	BRB_ASSERT_FMT(ev_base, ((path_str) || (fd >= 0)), "Path [%s] or FD [%d] needed for this operation\n", (path_str ? path_str : "NULL"), fd);
	BRB_ASSERT_FMT(ev_base, (size != 0), "Invalid size [%ld] for a read operation\n", size);

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, fd, thrd_aio_base, NULL, size, offset, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued AIO_REQs\n", fd);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);

	KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_REQ [%ld]-[%ld]\n", fd, aio_req->data.size, aio_req->data.offset);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_READ_MB);

	/* Create a MEMBUFFER to HOLD incoming data */
	aio_req->data.ptr			= NULL;
	aio_req->file.path_str		= (path_str ? strdup(path_str) : NULL);
	aio_req->flags.dup_path_str = (path_str ? 1 : 0);

	/* Set flags we are READING from a FILE with THREADED_AIO  */
	aio_req->flags.aio_read		= 1;
	aio_req->flags.aio_file		= 1;
	aio_req->flags.aio_threaded = 1;

	KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_REQ [%ld]-[%ld]\n", fd, aio_req->data.size, aio_req->data.offset);

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued THREAD_JOBs\n", fd);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_REQ [%ld]-[%ld]\n", fd, aio_req->data.size, aio_req->data.offset);

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_READ_MB].bytes += size;
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_READ_MB].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_READ_MB].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIOFileRead(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, int fd, char *buffer, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Sanity check */
	if (fd < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Trying to read to NEGATIVE_FD\n", fd);
		goto aio_failed;
	}

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, fd, thrd_aio_base, buffer, size, offset, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued AIO_REQs\n", fd);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_READ);

	/* Set flags we are READING from a FILE with THREADED_AIO  */
	aio_req->flags.aio_read		= 1;
	aio_req->flags.aio_file		= 1;
	aio_req->flags.aio_threaded = 1;

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued THREAD_JOBs\n", fd);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_READ].bytes += size;
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_READ].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_READ].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIOFileWriteFromMemBufferAndDestroy(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, MemBuffer *data_mb,
		int fd, char *path_str, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq aio_req_buf;
	EvAIOReq *aio_req;
	int op_status;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	BRB_ASSERT_FMT(ev_base, ((path_str) || (fd >= 0)), "Path [%s] or FD [%d] needed for this operation\n", (path_str ? path_str : "NULL"), fd);
	BRB_ASSERT_FMT(ev_base, (size != 0), "Invalid size [%ld] for a read operation\n", size);

	/* We will always need AIO_REQ storage from this function */
	if (!dst_aio_req)
	{
		/* Clean up stack */
		memset(&aio_req_buf, 0, sizeof(EvAIOReq));
		dst_aio_req = &aio_req_buf;
	}

	/* Dispatch AIO_WRITE_MB */
	op_status = ThreadAIOFileWriteFromMemBuffer(thrd_aio_base, dst_aio_req, data_mb, fd, path_str, size, offset, finish_cb, finish_cbdata);

	/* Failed writing to MB */
	if (AIOREQ_FAILED == op_status)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Trying to write failed\n", fd);
		return AIOREQ_FAILED;
	}

	/* We must have a valid REQ_ID */
	assert(dst_aio_req->id >= 0);

	/* Grab AIO_REQ back from arena */
	aio_req = EvAIOReqQueueGrabByID(&thrd_aio_base->req_queue, dst_aio_req->id);

	/* Populate destroy data */
	aio_req->destroy_func	= (EvAIOReqDestroyFunc*)MemBufferDestroy;
	aio_req->destroy_cbdata = data_mb;

	return AIOREQ_PENDING;
}
/**************************************************************************************************************************/
int ThreadAIOFileWriteFromMemBuffer(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, MemBuffer *data_mb,
		int fd, char *path_str, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	BRB_ASSERT_FMT(ev_base, ((path_str) || (fd >= 0)), "Path [%s] or FD [%d] needed for this operation\n", (path_str ? path_str : "NULL"), fd);
	BRB_ASSERT_FMT(ev_base, (size != 0), "Invalid size [%ld] for a read operation\n", size);

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, fd, thrd_aio_base, NULL, size, offset, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued AIO_REQs\n", fd);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_WRITE_MB);

	/* Create a MEMBUFFER to HOLD incoming data */
	aio_req->data.ptr			= (char*)data_mb;
	aio_req->file.path_str		= (path_str ? strdup(path_str) : NULL);
	aio_req->flags.dup_path_str = (path_str ? 1 : 0);

	/* Set flags we are WRITING to a FILE with THREADED_AIO */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_file		= 1;
	aio_req->flags.aio_threaded = 1;

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued THREAD_JOBs\n", fd);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].bytes += size;
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIOFileWrite(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, int fd, char *buffer, long size, long offset, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Sanity check */
	if (fd < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Trying to write to NEGATIVE_FD\n", fd);
		goto aio_failed;
	}

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, fd, thrd_aio_base, buffer, size, offset, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued AIO_REQs\n", fd);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_WRITE);

	/* Set flags we are WRITING to a FILE with THREADED_AIO */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_file		= 1;
	aio_req->flags.aio_threaded = 1;

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued THREAD_JOBs\n", fd);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].bytes += size;
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIOFileStat(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, -1, thrd_aio_base, NULL, -1, -1, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued AIO_REQs\n", path_str);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);
	assert(path_str);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_STAT);

	/* Populate STAT_SPECIFIC data */
	aio_req->file.path_str		= strdup(path_str);
	aio_req->flags.dup_path_str = 1;

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued THREAD_JOBs\n", path_str);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_STAT].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_STAT].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIOFileUnlink(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, -1, thrd_aio_base, NULL, -1, -1, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued AIO_REQs\n", path_str);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);
	assert(path_str);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_UNLINK);

	/* Populate UNLINK_SPECIFIC data */
	aio_req->file.path_str		= strdup(path_str);
	aio_req->flags.dup_path_str = 1;

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued THREAD_JOBs\n", path_str);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_UNLINK].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_UNLINK].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIOFileTruncate(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, long size, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, -1, thrd_aio_base, NULL, size, -1, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued AIO_REQs\n", path_str);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);
	assert(path_str);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_TRUNCATE);

	/* Populate TRUNCATE_SPECIFIC data */
	aio_req->file.path_str		= strdup(path_str);
	aio_req->flags.dup_path_str = 1;

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued THREAD_JOBs\n", path_str);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_UNLINK].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_TRUNCATE].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIODeviceMount(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, char *dev_str, int retry_count, int flags, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, -1, thrd_aio_base, NULL, -1, -1, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued AIO_REQs\n", path_str);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);
	assert(retry_count >= 0);
	assert(path_str);
	assert(dev_str);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_MOUNT);

	/* Populate MOUNT_SPECIFIC data */
	aio_req->retry_count		= retry_count;
	aio_req->file.flags			= flags;
	aio_req->file.path_str		= strdup(path_str);
	aio_req->file.dev_str		= strdup(dev_str);
	aio_req->flags.dup_path_str = 1;
	aio_req->flags.dup_dev_str	= 1;

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued THREAD_JOBs\n", path_str);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_MOUNT].tx_count++;
	return AIOREQ_PENDING;

	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_MOUNT].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int ThreadAIODeviceUnMount(ThreadAIOBase *thrd_aio_base, EvAIOReq *dst_aio_req, char *path_str, char *dev_str, int retry_count, int flags, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	ThreadPoolJobProto job_proto;
	EvAIOReq *aio_req;
	int job_id;

	EvKQBase *ev_base			= thrd_aio_base->ev_base;
	EvAIOReqQueue *req_queue	= &thrd_aio_base->req_queue;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&thrd_aio_base->req_queue, -1, thrd_aio_base, NULL, -1, -1, NULL, finish_cb, finish_cbdata);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued AIO_REQs\n", path_str);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);
	assert(path_str || dev_str);
	assert(retry_count >= 0);

	/* Initialize a generic JOB PROTOTYPE for this AIO_REQ */
	ThreadAIOEngine_JobGenericInitialize(thrd_aio_base, &job_proto, aio_req, AIOREQ_OPCODE_UNMOUNT);

	/* Populate UNMOUNT_SPECIFIC data */
	aio_req->retry_count		= retry_count;
	aio_req->file.flags			= flags;
	aio_req->file.path_str		= (path_str ? strdup(path_str) : NULL);
	aio_req->file.dev_str		= (dev_str ? strdup(dev_str) : NULL);
	aio_req->flags.dup_path_str = (path_str ? 1 : 0);
	aio_req->flags.dup_dev_str	= (dev_str ? 1 : 0);

	/* Save pending request AIO_INFO */
	if (dst_aio_req)			memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	/* Enqueue on REQ_QUEUE before THREAD ENQUEUE, remove later if JOB fail - Then, start running job on external THREAD CONTEXT  */
	EvAIOReqQueueEnqueue(req_queue, aio_req);
	job_id = ThreadPoolJobEnqueue(thrd_aio_base->thrd_pool, &job_proto);

	/* No more available THREAD_JOBs */
	if (job_id < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "PATH [%s] - Too many enqueued THREAD_JOBs\n", path_str);

		/* Remove and destroy ITEM from AIO_QUEUE and destroy it */
		EvAIOReqQueueRemoveItem(req_queue, aio_req);
		EvAIOReqDestroy(aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_UNMOUNT].tx_count++;
	return AIOREQ_PENDING;


	/* Touch statistics and leave */
	aio_failed:
	ev_base->stats.aio.opcode[AIOREQ_OPCODE_UNMOUNT].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int ThreadAIOEngine_JobDispatcher(void *thread_job_ptr, void *thread_aio_base_ptr)
{
	ThreadPoolInstanceJob *thread_job	= thread_job_ptr;
	ThreadAIOBase *thrd_aio_base		= thread_aio_base_ptr;
	ThreadPoolBase *thread_pool			= thread_job->parent_base;
	EvAIOReq *aio_req					= thread_job->user_data;
	int op_status						= -1;

	/* Dispatch OPCODE */
	op_status = ThreadAIOEngine_OpcodeDispatch(thrd_aio_base, aio_req);

	return 1;
}
/**************************************************************************************************************************/
static int ThreadAIOEngine_JobFinish(void *thread_job_ptr, void *thread_aio_base_ptr)
{
	ThreadPoolInstanceJob *thread_job	= thread_job_ptr;
	ThreadAIOBase *thrd_aio_base		= thread_aio_base_ptr;
	ThreadPoolBase *thread_pool			= thread_job->parent_base;
	EvAIOReq *aio_req					= thread_job->user_data;
	EvKQBase *ev_base					= thrd_aio_base->ev_base;

	assert ((aio_req->aio_opcode > AIOREQ_OPCODE_NONE) && (aio_req->aio_opcode < AIOREQ_OPCODE_LASTITEM));

	/* Update EV_BASE STATs */
	ev_base->stats.aio.opcode[aio_req->aio_opcode].rx_count++;

	/* Update THREAD_AIOBASE_STATs */
	thrd_aio_base->stats.opcode[aio_req->aio_opcode].rx_count++;

	/* EV_BASE - Recalculate pending count */
	ev_base->stats.aio.opcode[aio_req->aio_opcode].pending =
			(ev_base->stats.aio.opcode[aio_req->aio_opcode].tx_count - ev_base->stats.aio.opcode[aio_req->aio_opcode].rx_count);

	/* THRD_AIO_BASE - Recalculate pending count */
	thrd_aio_base->stats.opcode[aio_req->aio_opcode].pending =
			(thrd_aio_base->stats.opcode[aio_req->aio_opcode].tx_count - thrd_aio_base->stats.opcode[aio_req->aio_opcode].rx_count);

	/* Touch BASE statistics */
	if (aio_req->ret > -1)
	{
		/* Update THREAD_AIOBASE_STATs */
		thrd_aio_base->stats.opcode[aio_req->aio_opcode].success_sched++;
		thrd_aio_base->stats.opcode[aio_req->aio_opcode].success_notify++;

		/* Update EV_BASE STATs */
		ev_base->stats.aio.opcode[aio_req->aio_opcode].success_sched++;
		ev_base->stats.aio.opcode[aio_req->aio_opcode].success_notify++;
	}
	else
	{
		thrd_aio_base->stats.opcode[aio_req->aio_opcode].error++;
		ev_base->stats.aio.opcode[aio_req->aio_opcode].error++;
	}

	/* Remove from AIO_QUEUE */
	EvAIOReqQueueRemoveItem(&thrd_aio_base->req_queue, aio_req);

	/* Invoke notification CALLBACKS and destroy current AIO_REQ */
	EvAIOReqInvokeCallBacksAndDestroy(aio_req, 0, aio_req->fd, aio_req->data.size, thread_job->run_thread_id, aio_req);

	return 1;
}
/**************************************************************************************************************************/
static int ThreadAIOEngine_JobGenericInitialize(ThreadAIOBase *thrd_aio_base, ThreadPoolJobProto *job_proto, EvAIOReq *aio_req, int aio_opcode)
{
	/* Clean up stack */
	memset(job_proto, 0, sizeof(ThreadPoolJobProto));

	job_proto->retval_type			= THREAD_JOB_RETVAL_INT;
	job_proto->job_cbh_ptr			= (ThreadInstanceJobCB_Generic*)ThreadAIOEngine_JobDispatcher;
	job_proto->job_cbdata			= thrd_aio_base;
	job_proto->job_finish_cbh_ptr	= ThreadAIOEngine_JobFinish;
	job_proto->user_data			= aio_req;
	aio_req->aio_opcode				= aio_opcode;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int ThreadAIOEngine_OpcodeDispatch(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{

	/* Dispatch OPCODE to correct handler function */
	switch (aio_req->aio_opcode)
	{
	case AIOREQ_OPCODE_OPEN:
		ThreadAIOEngine_OpcodeOpen(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_CLOSE:
		ThreadAIOEngine_OpcodeClose(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_READ_MB:
		ThreadAIOEngine_OpcodeReadToMemBuffer(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_READ:
		ThreadAIOEngine_OpcodeRead(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_WRITE_MB:
		ThreadAIOEngine_OpcodeWriteFromMemBuffer(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_WRITE:
		ThreadAIOEngine_OpcodeWrite(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_STAT:
		ThreadAIOEngine_OpcodeStat(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_UNLINK:
		ThreadAIOEngine_OpcodeUnlink(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_TRUNCATE:
		ThreadAIOEngine_OpcodeTruncate(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_OPENDIR:
		ThreadAIOEngine_OpcodeOpenDir(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_MOUNT:
		ThreadAIOEngine_OpcodeMount(thrd_aio_base, aio_req);
		break;
	case AIOREQ_OPCODE_UNMOUNT:
		ThreadAIOEngine_OpcodeUnMount(thrd_aio_base, aio_req);
		break;

		/* This should never happen */
	default:
		assert(0);
		return 0;
	}

	/* AIO FAILED, set flags */
	if (aio_req->ret < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - AIO_REQID [%d] - OPCODE [%d]-[%s] - Failed with ERR [%d]\n",
				aio_req->fd, aio_req->id, aio_req->aio_opcode, glob_aioreqcode_str[aio_req->aio_opcode], errno);
		aio_req->flags.aio_failed = 1;
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeOpen(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	/* Issue blocking OPEN */
	aio_req->ret = open(aio_req->file.path_str, aio_req->file.flags, aio_req->file.mode);

	/* AIO_REQ will be FD in case of OPEN */
	aio_req->fd	 = aio_req->ret;
	aio_req->err = errno;
	return;
}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeClose(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	aio_req->ret = close(aio_req->fd);
	aio_req->err = errno;
	return;
}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeReadToMemBuffer(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	MemBuffer *dst_mb;
	struct stat statbuf;

	char *data_ptr		= NULL;
	int file_fd			= -1;

	assert(aio_req->data.offset >= 0);
	memset(&statbuf, 0, sizeof(struct stat));

	/* If size was set to -1, we will STAT file and read it FULLY */
	if (aio_req->data.size < 0)
	{
		if (aio_req->file.path_str)
			aio_req->ret = stat(aio_req->file.path_str, &statbuf);
		else
			aio_req->ret = fstat(aio_req->fd, &statbuf);

		/* STAT failed */
		if (aio_req->ret < 0)
		{
			KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "AIO_REQID [%d] - Failed stating file [%s]\n", aio_req->id, aio_req->file.path_str);

			/* Save global ERRNO and leave */
			aio_req->err				= errno;
			return;
		}

		/* Set SIZE to FILE_SIZE */
		aio_req->data.size	= statbuf.st_size;
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "AIO_REQID [%d] - Stated size [%ld] bytes\n", aio_req->id, aio_req->data.size);
	}

	/* Allocate data for temporary READ_STORE */
	data_ptr	= calloc(1, (aio_req->data.size + 16));

	/* Operate on FD */
	if (aio_req->fd >= 0)
	{
		aio_req->ret		= pread(aio_req->fd, data_ptr, aio_req->data.size, aio_req->data.offset);
		aio_req->err		= errno;

		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_VERBOSE, LOGCOLOR_CYAN, "AIO_REQID [%d] - Read by FD [%d] - SIZE [%d] - OFF [%d] - RET [%d] - ERR [%d]\n",
				aio_req->id, aio_req->fd, aio_req->data.size, aio_req->data.offset, aio_req->ret, aio_req->err);

		/* Create MB and add data */
		if (aio_req->ret > 0)
		{
			dst_mb				= MemBufferNew(BRBDATA_THREAD_UNSAFE, (aio_req->ret + 16));
			aio_req->data.ptr	= (char*)dst_mb;

			/* Populate destroy data */
			aio_req->destroy_func	= (EvAIOReqDestroyFunc*)MemBufferUnlock;
			aio_req->destroy_cbdata = aio_req->data.ptr;

			MemBufferAdd(dst_mb, data_ptr, aio_req->ret);
		}
		else
		{
			/* Save global ERRNO and leave */
			aio_req->err				= errno;

			KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "AIO_REQID [%d] - Read by FD [%d] FAILED - SIZE [%d] - RET [%d] - ERR [%d]\n",
					aio_req->id, aio_req->fd, aio_req->data.size, aio_req->ret, aio_req->err);
		}

		/* Destroy data and leave */
		free(data_ptr);
		return;
	}

	/* Operate on PATH */
	assert(aio_req->file.path_str);
	file_fd = open(aio_req->file.path_str, O_RDONLY);

	/* Error opening FILE */
	if (file_fd < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "AIO_REQID [%d] - Failed opening file [%s]\n", aio_req->id, aio_req->file.path_str);

		/* Save global ERRNO and leave */
		aio_req->err				= errno;
		aio_req->ret				= -1;

		free(data_ptr);
		return;
	}

	/* File opened OK, READ */
	aio_req->ret		= pread(file_fd, data_ptr, aio_req->data.size, aio_req->data.offset);
	aio_req->err		= errno;

	/* Create MB and add data */
	if (aio_req->ret > 0)
	{
		dst_mb				= MemBufferNew(BRBDATA_THREAD_UNSAFE, (aio_req->ret + 16));
		aio_req->data.ptr	= (char*)dst_mb;

		/* Populate destroy data */
		aio_req->destroy_func	= (EvAIOReqDestroyFunc*)MemBufferUnlock;
		aio_req->destroy_cbdata = aio_req->data.ptr;

		MemBufferAdd(dst_mb, data_ptr, aio_req->ret);
	}
	else
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "AIO_REQID [%d] - Read by PATH [%s] FAILED - SIZE [%d] - RET [%d] - ERR [%d]\n",
				aio_req->id, aio_req->file.path_str, aio_req->data.size, aio_req->ret, aio_req->err);

		/* Save global ERRNO and leave */
		aio_req->err	= errno;
	}

	/* Clean up and leave */
	close(file_fd);
	free(data_ptr);
	return;
}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeRead(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	assert(aio_req->data.offset >= 0);
	aio_req->ret = pread(aio_req->fd, aio_req->data.ptr, aio_req->data.size, aio_req->data.offset);
	aio_req->err = errno;
	return;
}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeWriteFromMemBuffer(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	MemBuffer *src_mb	= (MemBuffer*)aio_req->data.ptr;
	char *data_ptr		= MemBufferDeref(src_mb);
	long data_sz		= MemBufferGetSize(src_mb);
	int file_fd			= -1;

	assert(aio_req->data.offset >= 0);

	/* Operate on FD */
	if (aio_req->fd >= 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "AIO_REQID [%d] - Writing directly to FD [%d]\n", aio_req->id, aio_req->fd);

		aio_req->ret		= pwrite(aio_req->fd, data_ptr, data_sz, aio_req->data.offset);
		aio_req->err		= errno;
		return;
	}

	/* Operate on PATH */
	assert(aio_req->file.path_str);
	file_fd = open(aio_req->file.path_str, (O_WRONLY | O_CREAT), 0644);

	/* Error opening FILE */
	if (file_fd < 0)
	{
		KQBASE_LOG_PRINTF(thrd_aio_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "AIO_REQID [%d] - Failed opening file [%s]\n", aio_req->id, aio_req->file.path_str);

		aio_req->ret	= -1;
		aio_req->err	= errno;
		return;
	}

	aio_req->ret = pwrite(file_fd, data_ptr, data_sz, aio_req->data.offset);
	aio_req->err = errno;
	close(file_fd);
	return;

}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeWrite(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	assert(aio_req->data.offset >= 0);

	aio_req->ret = pwrite(aio_req->fd, aio_req->data.ptr, aio_req->data.size, aio_req->data.offset);
	aio_req->err = errno;
	return;
}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeStat(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	aio_req->ret = stat(aio_req->file.path_str, &aio_req->file.statbuf);
	aio_req->err = errno;
	return;
}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeUnlink(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	aio_req->ret = unlink(aio_req->file.path_str);
	aio_req->err = errno;
	return;
}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeTruncate(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	aio_req->ret = truncate(aio_req->file.path_str, aio_req->data.size);
	aio_req->err = errno;
	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeOpenDir(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
	/* NOT IMPLEMENTED */
	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeMount(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
#if !defined(linux)
	struct ufs_args ufsargs;
	int retry_count = aio_req->retry_count;

	/* Clean UFS_ARGS */
	memset(&ufsargs, 0, sizeof(ufsargs));

	/* We should have DEV and PATH */
	assert(aio_req->file.dev_str);
	assert(aio_req->file.path_str);

	/* Create target path and CHOWN it */
	mkdir(aio_req->file.path_str, 0222);
	chown(aio_req->file.path_str, 100, 100);

	/* Set device path */
	ufsargs.fspec = aio_req->file.dev_str;

	/* Try RETRY_TIMEs */
	while (retry_count-- >= 0)
	{
		/* Dispatch MOUNT */
		aio_req->ret = mount("ufs", aio_req->file.path_str, aio_req->file.flags, (caddr_t)&ufsargs);
		aio_req->err = errno;

		/* Failed - Sleep for a bit and RETRY */
		if (aio_req->ret < 0)
		{
			sleep(1);
			continue;
		}
		/* Done, stop */
		else
			break;

		continue;
	}
#endif
	return;
}
/**************************************************************************************************************************/
static void ThreadAIOEngine_OpcodeUnMount(ThreadAIOBase *thrd_aio_base, EvAIOReq *aio_req)
{
#if !defined(linux)
	int retry_count = aio_req->retry_count;

	/* We should have DEV or PATH */
	assert(aio_req->file.dev_str || aio_req->file.path_str);

	/* Try RETRY_TIMEs */
	while (retry_count-- >= 0)
	{
		/* Dispatch UNMOUNT */
		aio_req->ret = unmount((aio_req->file.path_str ? aio_req->file.path_str : aio_req->file.dev_str), aio_req->file.flags);
		aio_req->err = errno;

		/* Failed - Sleep for a bit and RETRY */
		if (aio_req->ret < 0)
		{
			sleep(1);
			continue;
		}
		/* Done, stop */
		else
			break;

		continue;
	}
#endif
	return;
}
/**************************************************************************************************************************/
