/*
 * ev_kq_aio_req.c
 *
 *  Created on: 2013-02-22
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

static int EvAIOReqDoDestroy(EvAIOReq *aio_req);

/**************************************************************************************************************************/
EvAIOReqQueue *EvAIOReqQueueNew(EvKQBase *ev_base, int max_slots, int queue_mt, int queue_type)
{
	EvAIOReqQueue *aio_req_queue = calloc(1, sizeof(EvAIOReqQueue));

	/* Initialize request QUEUE */
	EvAIOReqQueueInit(ev_base, aio_req_queue, max_slots, queue_mt, queue_type);

	return aio_req_queue;
}
/**************************************************************************************************************************/
void EvAIOReqQueueInit(EvKQBase *ev_base, EvAIOReqQueue *aio_req_queue, int max_slots, int queue_mt, int queue_type)
{
	/* Sanity check */
	if (!ev_base)
		return;

	/* Already initialized, bail out */
	if (aio_req_queue->flags.queue_init)
		return;

	/* Populate AIO_REQ_QUEUE */
	aio_req_queue->ev_base				= ev_base;
	aio_req_queue->max_slots			= max_slots;
	aio_req_queue->stats.queue_sz 		= 0;
	aio_req_queue->flags.mt_engine		= ((AIOREQ_QUEUE_MT_SAFE == queue_mt) ? 1 : 0);
	aio_req_queue->flags.queue_slotted	= ((AIOREQ_QUEUE_SLOTTED == queue_type) ? 1 : 0);
	aio_req_queue->flags.queue_init		= 1;

	/* Initialize internal MEM_SLOT */
	if (aio_req_queue->flags.queue_slotted)
		MemSlotBaseInit(&aio_req_queue->memslot, (sizeof(EvAIOReq) + 1), max_slots, BRBDATA_THREAD_UNSAFE);

	AIOREQ_QUEUE_MUTEX_INIT(aio_req_queue);

	return;
}
/**************************************************************************************************************************/
void EvAIOReqQueueDestroy(EvAIOReqQueue *aio_req_queue)
{
	EvAIOReq *aio_req;

	if (!aio_req_queue)
		return;

	/* Invoke common clean */
	EvAIOReqQueueClean(aio_req_queue);
	AIOREQ_QUEUE_MUTEX_DESTROY(aio_req_queue);

	free(aio_req_queue);

	return;
}
/**************************************************************************************************************************/
void EvAIOReqQueueClean(EvAIOReqQueue *aio_req_queue)
{
	EvAIOReq *aio_req;

	/* Sanity check */
	if (!aio_req_queue)
		return;

	/* Not initialized, bail out */
	if (!aio_req_queue->flags.queue_init)
		return;

	AIOREQ_QUEUE_MUTEX_LOCK(aio_req_queue);

	/* Destroy all enqueued aio_reqs */
	while ((aio_req = DLinkedListPopHead(&aio_req_queue->aio_req_list)))
	{
		/* Invoke finish CB with error */
		if (!aio_req_queue->flags.cancelled)
			EvAIOReqInvokeCallBacks(aio_req, 1, aio_req->fd, -1, -1, aio_req->parent_ptr);

		/* Now destroy it */
		EvAIOReqDestroy(aio_req);
		continue;
	}

	aio_req_queue->stats.queue_sz 		= 0;
	aio_req_queue->aio_req_list.head	= NULL;
	aio_req_queue->aio_req_list.tail	= NULL;
	aio_req_queue->flags.queue_init		= 0;

	/* Clean up memory slot arena */
	if (aio_req_queue->flags.queue_slotted)
		MemSlotBaseClean(&aio_req_queue->memslot);

	AIOREQ_QUEUE_MUTEX_UNLOCK(aio_req_queue);

	return;
}
/**************************************************************************************************************************/
long EvAIOReqQueueGetQueueSize(EvAIOReqQueue *aio_req_queue)
{
	return aio_req_queue->stats.queue_sz;
}
/**************************************************************************************************************************/
long EvAIOReqQueueGetQueueCount(EvAIOReqQueue *aio_req_queue)
{
	return aio_req_queue->aio_req_list.size;
}
/**************************************************************************************************************************/
void EvAIOReqQueueEnqueueHead(EvAIOReqQueue *aio_req_queue, EvAIOReq *aio_req)
{

	AIOREQ_QUEUE_MUTEX_LOCK(aio_req_queue);

	/* Save current IOLOOP */
	aio_req->ioloop_queue = aio_req_queue->ev_base->stats.kq_invoke_count;

	BRB_ASSERT_FMT(aio_req_queue->ev_base, (aio_req->data.size != 0), "Invalid AIO_REQ [%ld / %ld]\n",
			aio_req->data.size, aio_req->data.offset);

//	if (aio_req->data.size <= 0)
//	{
//		KQBASE_LOG_PRINTF(aio_req_queue->ev_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - AIO_OPCODE [%d] - Invalid AIO_REQ [%ld / %ld]\n", aio_req->fd, aio_req->aio_opcode,
//				aio_req->data.size, aio_req->data.offset);
//	}

	/* Add to internal list HEAD and touch QUEUE_SZ */
	DLinkedListAdd(&aio_req_queue->aio_req_list, &aio_req->node, aio_req);
	aio_req_queue->stats.queue_sz += ((aio_req->data.size > 0) ? aio_req->data.size : 0);

	AIOREQ_QUEUE_MUTEX_UNLOCK(aio_req_queue);

	return;
}
/**************************************************************************************************************************/
void EvAIOReqQueueEnqueue(EvAIOReqQueue *aio_req_queue, EvAIOReq *aio_req)
{

	AIOREQ_QUEUE_MUTEX_LOCK(aio_req_queue);

	/* Save current IOLOOP */
	aio_req->ioloop_queue = aio_req_queue->ev_base->stats.kq_invoke_count;

	BRB_ASSERT_FMT(aio_req_queue->ev_base, (aio_req->data.size != 0), "Invalid AIO_REQ [%ld / %ld]\n", aio_req->data.size, aio_req->data.offset);

//	if (aio_req->data.size <= 0)
//	{
//		KQBASE_LOG_PRINTF(aio_req_queue->ev_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - AIO_OPCODE [%d] - Invalid AIO_REQ [%ld / %ld]\n", aio_req->fd, aio_req->aio_opcode,
//				aio_req->data.size, aio_req->data.offset);
//	}

	/* Add to internal list TAIL and touch QUEUE_SZ */
	DLinkedListAddTail(&aio_req_queue->aio_req_list, &aio_req->node, aio_req);
	aio_req_queue->stats.queue_sz += ((aio_req->data.size > 0) ? aio_req->data.size : 0);
	aio_req_queue->stats.total_sz += ((aio_req->data.size > 0) ? aio_req->data.size : 0);

	AIOREQ_QUEUE_MUTEX_UNLOCK(aio_req_queue);

	return;
}
/**************************************************************************************************************************/
void EvAIOReqQueueRemoveItem(EvAIOReqQueue *aio_req_queue, EvAIOReq *aio_req)
{

	AIOREQ_QUEUE_MUTEX_LOCK(aio_req_queue);

	/* Add to internal list TAIL and touch QUEUE_SZ */
	DLinkedListDelete(&aio_req_queue->aio_req_list, &aio_req->node);
	aio_req_queue->stats.queue_sz -= ((aio_req->data.size > 0) ? aio_req->data.size : 0);

	AIOREQ_QUEUE_MUTEX_UNLOCK(aio_req_queue);

	return;
}
/**************************************************************************************************************************/
EvAIOReq *EvAIOReqQueuePointToHead(EvAIOReqQueue *aio_req_queue)
{
	EvAIOReq *aio_req;
	DLinkedList *list;

	/* Sanity check */
	if (!aio_req_queue)
		return NULL;

	/* Empty list */
	if (DLINKED_LIST_ISEMPTY(aio_req_queue->aio_req_list))
		return NULL;

	/* Point to list */
	list	= &aio_req_queue->aio_req_list;
	aio_req = list->head->data;

	return aio_req;
}
/**************************************************************************************************************************/
EvAIOReq *EvAIOReqQueueDequeue(EvAIOReqQueue *aio_req_queue)
{
	EvAIOReq *aio_req;

	/* Nothing to check for, bail out */
	AIOREQ_QUEUE_MUTEX_LOCK(aio_req_queue);

	if (DLINKED_LIST_ISEMPTY(aio_req_queue->aio_req_list))
	{
		AIOREQ_QUEUE_MUTEX_UNLOCK(aio_req_queue);
		return NULL;
	}

	/* Remove from list HEAD and touch QUEUE_SZ */
	aio_req = DLinkedListPopHead(&aio_req_queue->aio_req_list);
	aio_req_queue->stats.queue_sz -= ((aio_req->data.size > 0) ? aio_req->data.size : 0);

	AIOREQ_QUEUE_MUTEX_UNLOCK(aio_req_queue);

	return aio_req;
}
/**************************************************************************************************************************/
int EvAIOReqQueueIsEmpty(EvAIOReqQueue *aio_req_queue)
{
	if (DLINKED_LIST_ISEMPTY(aio_req_queue->aio_req_list))
		return 1;

	return 0;
}
/**************************************************************************************************************************/
int EvAIOReqQueueCancelAllByFD(EvAIOReqQueue *aio_req_queue, int target_fd)
{
	DLinkedListNode *node;
	EvAIOReq *aio_req;
	EvBaseKQFileDesc *kq_fd;
	int i;

	int cancel_count = 0;

	/* Sanity check */
	if (target_fd < 0)
		return 0;

	/* Grab FD from reference table - Clean up FD */
	kq_fd = EvKQBaseFDGrabFromArena(aio_req_queue->ev_base, target_fd);

	AIOREQ_QUEUE_MUTEX_LOCK(aio_req_queue);

	/* Nothing to cancel, bail out */
	if (DLINKED_LIST_ISEMPTY(aio_req_queue->aio_req_list))
	{
		KQBASE_LOG_PRINTF(aio_req_queue->log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "TARGET_FD [%d] - Empty REQ_QUEUE\n", target_fd);

		AIOREQ_QUEUE_MUTEX_UNLOCK(aio_req_queue);
		return 0;
	}

	/* Cancel all AIO requests that match FD */
	for (i = 0, node = aio_req_queue->aio_req_list.head; node; node = node->next, i++)
	{
		aio_req = node->data;

		KQBASE_LOG_PRINTF(aio_req_queue->log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "[%d] - CUR_FD [%d] | TARGET_FD [%d]\n", i, aio_req->fd, target_fd);

		/* FD match, mark as canceled */
		if (target_fd == aio_req->fd)
		{
			/* This is a FILE and we are doing KERNEL_AIO, cancel this AIO_CB */
			if ((FD_TYPE_FILE == kq_fd->fd.type) && (!aio_req->flags.aio_threaded))
				aio_cancel(aio_req->fd, &aio_req->aiocb);

			/* Set flags as canceled */
			aio_req->flags.cancelled = 1;
			cancel_count++;
		}

		continue;
	}

	AIOREQ_QUEUE_MUTEX_UNLOCK(aio_req_queue);
	return cancel_count;
}
/**************************************************************************************************************************/
EvAIOReq *EvAIOReqQueueGrabByID(EvAIOReqQueue *aio_req_queue, int aioreq_id)
{
	EvAIOReq *aio_req;

	/* Sanity check */
	if (aioreq_id < 0)
		return NULL;

	/* Just for SLOTTED and INITIALIZED queues */
	assert(aio_req_queue->flags.queue_slotted);
	assert(aio_req_queue->flags.queue_init);

	/* Grab AIO_REQ */
	aio_req = MemSlotBaseSlotGrabByID(&aio_req_queue->memslot, aioreq_id);
	return aio_req;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
EvAIOReq *EvAIOReqNew(EvAIOReqQueue *aio_req_queue, int fd, void *parent_ptr, void *data, long data_sz, long offset,
		EvAIOReqDestroyFunc *destroy_func, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* AIO_QUEUE must exist and be INITIALIZED */
	assert(aio_req_queue);
	assert(aio_req_queue->flags.queue_init);

	/* Grab from MEM_SLOT or ALLOC */
	aio_req =  (aio_req_queue->flags.queue_slotted) ? MemSlotBaseSlotGrab(&aio_req_queue->memslot) : calloc(1, sizeof(EvAIOReq));

	/* Failed creating NEW AIO_REQ, bail out */
	if (!aio_req)
		return NULL;

	assert(!aio_req->flags.in_use);
	memset(aio_req, 0, sizeof(EvAIOReq));

	/* Populate data */
	aio_req->fd				= fd;
	aio_req->id				= ((aio_req_queue->flags.queue_slotted) ? MemSlotBaseSlotGetID(aio_req) : -1);
	aio_req->ioloop_create	= aio_req_queue->ev_base->stats.kq_invoke_count;
	aio_req->parent_ptr		= parent_ptr;
	aio_req->data.ptr		= data;
	aio_req->data.size		= data_sz;
	aio_req->data.offset	= offset;
	aio_req->destroy_func	= destroy_func;
	aio_req->finish_cb		= finish_cb;
	aio_req->finish_cbdata	= finish_cbdata;
	aio_req->parent_queue	= aio_req_queue;
	aio_req->flags.in_use	= 1;

	return aio_req;
}
/**************************************************************************************************************************/
int EvAIOReqDestroy(EvAIOReq *aio_req)
{
	EvAIOReqDestroyFunc *destroy_func;
	EvAIOReqQueue *aio_req_queue;

	/* Sanity check */
	if (!aio_req)
		return 0;

	/* Set flags as destroyed */
	aio_req->flags.destroyed = 1;

	/* Reference count still holds, bail out */
	if (aio_req->lock_count-- > 0)
		return aio_req->lock_count;

	/* Proceed to destroy this AIO_REQ */
	EvAIOReqDoDestroy(aio_req);
	return -1;
}
/**************************************************************************************************************************/
long EvAIOReqGetMissingSize(EvAIOReq *aio_req)
{
	long size;

	/* Calculate missing size, either from direct RAW data or from transformed temporary store */
	if (aio_req->flags.transformed)
		size = (MemBufferGetSize(aio_req->transformed_mb) - aio_req->data.offset);
	else
		size = (aio_req->data.size - aio_req->data.offset);

	return size;
}
/**************************************************************************************************************************/
char *EvAIOReqGetDataPtr(EvAIOReq *aio_req)
{
	char *base_ptr;

	/* Grab data from transformed store data or directly by pointed data */
	if (aio_req->flags.transformed)
		base_ptr = MemBufferOffsetDeref(aio_req->transformed_mb, aio_req->data.offset);
	else
		base_ptr = aio_req->data.ptr + aio_req->data.offset;

	return base_ptr;
}
/**************************************************************************************************************************/
void EvAIOReqInvokeCallBacksAndDestroy(EvAIOReq *aio_req, int delay, int fd, int size, int thrd_id, void *base_ptr)
{
	/* Invoke pending CALLBACKs (FINISH and PRE_FINISH) and destroy or leave AIO_REQ */
	EvAIOReqInvokeCallBacks(aio_req, delay, fd, size, thrd_id, base_ptr);
	EvAIOReqDestroy(aio_req);

	return;
}
/**************************************************************************************************************************/
void EvAIOReqInvokeCallBacks(EvAIOReq *aio_req, int delay, int fd, int size, int thrd_id, void *base_ptr)
{
	/* Request has been canceled, do not invoke CB_FUNCs - WARNING: pre_finish_cb will be used for cleanup in some cases (like COMM_TCP_CLIENT and SERVER) */
	if (aio_req->flags.cancelled)
		return;

	/* Dispatch write finish event, if there is anyone interested */
	if (aio_req->finish_cb)
		aio_req->finish_cb(fd, size, thrd_id, aio_req->finish_cbdata, base_ptr);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvAIOReqDoDestroy(EvAIOReq *aio_req)
{
	EvAIOReqDestroyFunc *destroy_func;
	EvAIOReqQueue *aio_req_queue;

	/* Grab parent queue of this AIO_REQ */
	assert(aio_req);
	assert(aio_req->parent_queue);
	aio_req_queue = aio_req->parent_queue;

	/* Invoke destroy function, if any */
	if (aio_req->destroy_func)
		aio_req->destroy_func(aio_req->destroy_cbdata ? aio_req->destroy_cbdata : aio_req->data.ptr);

	/* Destroy PATH_STR of file, its a DUP */
	if (aio_req->flags.dup_path_str)
	{
		assert(aio_req->file.path_str);
		free(aio_req->file.path_str);

		/* Reset PATH info */
		aio_req->flags.dup_path_str = 0;
		aio_req->file.path_str		= NULL;
	}

	/* Destroy DEVICE_STR of file, its a DUP */
	if (aio_req->flags.dup_dev_str)
	{
		assert(aio_req->file.dev_str);
		free(aio_req->file.dev_str);

		/* Reset PATH info */
		aio_req->flags.dup_dev_str	= 0;
		aio_req->file.dev_str		= NULL;
	}


	/* Not in USE anymore */
	aio_req->flags.in_use	= 0;

	/* Destroy transformed MB, if any */
	MemBufferDestroy(aio_req->transformed_mb);

	/* Get rid of this AIO_REQ */
	if (aio_req_queue->flags.queue_slotted)
		MemSlotBaseSlotFree(&aio_req_queue->memslot, aio_req);
	else
		free(aio_req);

	return 1;
}
/**************************************************************************************************************************/

