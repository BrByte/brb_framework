/*
 * ev_kq_file_mapped.c
 *
 *  Created on: 2015-01-24
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2015 BrByte Software (Oliveira Alves & Amorim LTDA)
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE./


/* XXX TODO: Make META_UPDATE operations ASYNC */

#include "../include/libbrb_core.h"

static int EvKQFileMappedCloseFiles(EvKQFileMapped *file_mapped);
static int EvKQFileMappedOpenFiles(EvKQFileMapped *file_mapped);
static int EvKQFileMappedAlignedWrite(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state, char *data, long data_sz, long block_idx);
static int EvKQFileMappedPartialDataProcess(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state, char *data, long data_sz, long block_idx, long rel_offset);
static int EvKQFileMappedPartialDrop(EvKQFileMapped *file_mapped, MemBuffer *page_part_mb);

static EvKQFileMappedAIOStateNotify *EvKQFileMappedAIOStateNotifyNew(EvKQFileMappedAIOState *aio_state);
static int EvKQFileMappedAIOStateNotifyDestroy(EvKQFileMappedAIOStateNotify *notify_node);
static int EvKQFileMappedAIOStateNotifyDestroyAll(EvKQFileMappedAIOState *aio_state);
static int EvKQFileMappedAIOStateNotifyInvokeAll(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state);

static EvKQFileMappedAIOState *EvKQFileMappedAIOStateNew(EvKQFileMapped *file_mapped);
static int EvKQFileMappedAIOStateDestroy(EvKQFileMappedAIOState *aio_state);
static int EvKQFileMappedAIOStatePendingCancelAll(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state);
static int EvKQFileMappedAIOStatePendingAdd(EvKQFileMappedAIOState *aio_state, EvAIOReq *aio_req, int aio_opcode);
static int EvKQFileMappedAIOStateInvokeAndDestroy(EvKQFileMappedAIOState *aio_state);
static int EvKQFileMappedAIOStateDestroyAll(EvKQFileMapped *file_mapped);
static int EvKQFileMappedAIOStateCheckFinish(EvKQFileMapped *file_mapped, EvAIOReq *aio_req, int error);
static int EvKQFileMappedInternalClean(EvKQFileMapped *file_mapped);
static int EvKQFileMappedMetaDataUpdateAll(EvKQFileMapped *file_mapped);
static int EvKQFileMappedMetaDataAuxUpdate(EvKQFileMapped *file_mapped);
static int EvKQFileMappedMetaDataRawUpdate(EvKQFileMapped *file_mapped);

static EvAIOReqCBH EvKQFileMappedAIOWriteMetaAuxDataFinishCB;
static EvAIOReqCBH EvKQFileMappedAIOWriteMetaRawDataFinishCB;
static EvAIOReqCBH EvKQFileMappedAIOWriteRawDataFinishCB;

static EvAIOReqCBH EvKQFileMappedAIOReadMetaAuxDataFinishCB;
static EvAIOReqCBH EvKQFileMappedAIOReadMetaDataFinishCB;
static EvAIOReqCBH EvKQFileMappedAIOReadRawDataFinishCB;

static EvAIOReqCBH EvKQFileMappedAIOGenericUnlockCB;
static EvAIOReqDestroyFunc EvKQFileMappedAIO_MemBufferDestroyFunc;

/**************************************************************************************************************************/
EvKQFileMapped *EvKQFileMappedNew(EvKQBase *ev_base, EvKQFileMappedConf *conf)
{
	EvKQFileMapped *file_mapped;
	int op_status;

	file_mapped = calloc(1, sizeof(EvKQFileMapped));
	op_status	= EvKQFileMappedInit(ev_base, file_mapped, conf);

	/* Failed initializing FILEMAPPED */
	if (op_status < 0)
	{
		free(file_mapped);
		return NULL;
	}

	/* Set we have been created with NEW */
	file_mapped->flags.created_with_new = 1;

	return file_mapped;
}
/**************************************************************************************************************************/
int EvKQFileMappedInit(EvKQBase *ev_base, EvKQFileMapped *file_mapped, EvKQFileMappedConf *conf)
{
	int op_status;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Threaded AIO need Threaded AIO BASE */
	if ((conf->flags.threaded_aio) && (!conf->thrdaio_base))
		return 0;

	/* Grab LOG_BASE from CONF if not already SET */
	if (!file_mapped->log_base)
		file_mapped->log_base = conf->log_base;

	/* We must have PATH for DATA and META, AUX_META is optional */
	assert(conf->path_meta_str);
	assert(conf->path_data_str);

	/* Populate data */
	file_mapped->ev_base				= ev_base;
	file_mapped->thrdaio_base			= conf->thrdaio_base;
	file_mapped->data.page_sz			= conf->page_sz;
	file_mapped->mutex					= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	file_mapped->flags.thread_safe		= ev_base->flags.mt_engine;
	file_mapped->flags.threaded_aio		= conf->flags.threaded_aio;
	file_mapped->flags.truncate_file	= conf->flags.truncate_file;

	/* Save path for METADATA and RAWDATA */
	file_mapped->meta.path_aux_str		= (conf->path_meta_aux_str ? strdup(conf->path_meta_aux_str) : NULL);
	file_mapped->meta.path_str			= strdup(conf->path_meta_str);
	file_mapped->data.path_str			= strdup(conf->path_data_str);

	/* Open files with TRUNC ON */
	op_status							= EvKQFileMappedOpenFiles(file_mapped);

	/* Failed opening META or RAW data files, bail out */
	if (op_status < 0)
		return op_status;

	/* Create META_BITMAP */
	file_mapped->meta.bitmap		= DynBitMapNew(BRBDATA_THREAD_UNSAFE, 32);

	/* Initialize list to hold partial DATA */
	DLinkedListInit(&file_mapped->data.partial_mb_list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&file_mapped->data.aio_state_list, BRBDATA_THREAD_UNSAFE);

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Created MAPPED_FILE with META_FD [%d]-[%s] and RAW_FD [%d]-[%s]\n",
			file_mapped->meta.fd, file_mapped->meta.path_str, file_mapped->data.fd, file_mapped->data.path_str);

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedClean(EvKQFileMapped *file_mapped)
{
	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Reference count still holds, bail out */
	if (file_mapped->ref_count-- > 0)
		return file_mapped->ref_count;

	/* Invoke internal clean mechanism */
	EvKQFileMappedInternalClean(file_mapped);

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedDestroy(EvKQFileMapped *file_mapped)
{
	/* Sanity check */
	if (!file_mapped)
		return 0;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Called to destroy FILE_MAPPED with LOCK_COUNT [%d]\n",
			file_mapped->data.fd, file_mapped->ref_count);

	/* Set flags as destroyed */
	file_mapped->flags.destroyed = 1;

	/* Reference count still holds, bail out */
	if (file_mapped->ref_count-- > 0)
		return file_mapped->ref_count;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - FILE_MAPPED destroyed\n", file_mapped->data.fd);

	/* Clean and free */
	EvKQFileMappedInternalClean(file_mapped);
	free(file_mapped);

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedLock(EvKQFileMapped *file_mapped)
{
	/* Sanity checks */
	if (!file_mapped)
		return 0;

	file_mapped->ref_count++;
	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Locked! CUR_REF [%d]\n", file_mapped->data.fd, file_mapped->ref_count);

	return file_mapped->ref_count;

}
/**************************************************************************************************************************/
int EvKQFileMappedUnlock(EvKQFileMapped *file_mapped)
{
	int will_destroy;

	/* Sanity checks */
	if (!file_mapped)
		return 0;

	/* Reference count still holds, bail out */
	if (file_mapped->ref_count-- > 0)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Unlocked! CUR_REF [%d]\n", file_mapped->data.fd, file_mapped->ref_count);
		return file_mapped->ref_count;
	}

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Unlocked! CUR_REF [%d] - Will destroy\n",
			file_mapped->data.fd, file_mapped->ref_count);

	/* Save flag to decide if we will free this object */
	will_destroy = file_mapped->flags.created_with_new;
	EvKQFileMappedInternalClean(file_mapped);

	if (will_destroy)
		free(file_mapped);

	return -1;
}
/**************************************************************************************************************************/
int EvKQFileMappedOpen(EvKQFileMapped *file_mapped)
{
	int op_status;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Already OPEN */
	if (file_mapped->flags.opened)
		return 1;

	/* There is a request to close this FILE_MAPPED, just UNMARK it and leave */
	if (file_mapped->flags.close_request)
	{
		/* Make sure we have OPEN FDs to BACKEND data */
		assert(file_mapped->meta.fd > 0);
		assert(file_mapped->data.fd > 0);

		/* AUX file exists, check FD */
		if (file_mapped->flags.meta_aux_file)
			assert(file_mapped->meta.fd_aux > 0);

		/* Reset FLAG */
		file_mapped->flags.close_request = 0;
		return 1;
	}

	/* Open files with TRUNC OFF */
	op_status = EvKQFileMappedOpenFiles(file_mapped);

	/* Failed opening META or RAW data files, bail out */
	if (op_status < 0)
		return 0;

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedClose(EvKQFileMapped *file_mapped)
{
	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Set close request flags */
	file_mapped->flags.close_request = 1;

	/* Invoke METADATA update - Will close if CLOSE_REQUEST flags is seen */
	EvKQFileMappedMetaDataUpdateAll(file_mapped);
	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedCompleted(EvKQFileMapped *file_mapped)
{
	MemBuffer *page_part_mb;
	DLinkedListNode *node;
	EvAIOReq aio_req_pending;
	long bitmap_higher_seen;
	int begin_complete;
	int op_status;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Clear pending IO_REQ store */
	memset(&aio_req_pending, 0, sizeof(EvAIOReq));

	/* Make sure we have all the START bytes */
	begin_complete = DynBitMapCheckMultiBlocks(file_mapped->meta.bitmap, 0, DynBitMapGetHigherSeen(file_mapped->meta.bitmap));

	/* Incomplete initial blocks */
	if (!begin_complete)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_CYAN, "FD [%d] - Not complete, has not FIRST PAGE!\n", file_mapped->data.fd);

		/* Set flag and leave */
		file_mapped->flags.wants_complete = 1;
		return 0;
	}

	/* There is more than one page flying around, and this should NEVER happen */
	if (file_mapped->data.partial_mb_list.size > 1)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Not complete, has MULTIPLE PARTIALS - HIGH [%d] - IN_LIST [%d]\n",
				file_mapped->data.fd, DynBitMapGetHigherSeen(file_mapped->meta.bitmap), file_mapped->data.partial_mb_list.size);

		/* Dump all pages to help the coder make more sense of what kind of shit just happened */
		for (node = file_mapped->data.partial_mb_list.head; node; node = node->next)
		{
			page_part_mb = node->data;
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "Remaining BLOCK_ID [%d] with [%d] bytes\n", page_part_mb->user_int, MemBufferGetSize(page_part_mb));
			continue;
		}

		/* Set flag and leave */
		file_mapped->flags.wants_complete = 1;
		return 0;
	}

	/* Append last partial item */
	if (file_mapped->data.partial_mb_list.size)
	{
		/* Grab page and make sure its the LAST one */
		page_part_mb		= file_mapped->data.partial_mb_list.head->data;
		bitmap_higher_seen	= DynBitMapGetHigherSeen(file_mapped->meta.bitmap);

		/* Make sure its the last item */
		if (bitmap_higher_seen != page_part_mb->user_int)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED,
					"FD [%d] - HIGHER_SEEN [%ld] - Wrong LAST_PAGE with [%d] bytes at IDX [%d] with ABSOLUTE_OFFSET [%ld]\n",
					file_mapped->data.fd, bitmap_higher_seen, MemBufferGetSize(page_part_mb), page_part_mb->user_int, file_mapped->data.cur_offset);

			return 0;
		}

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Writing LAST [%d] bytes from BLOCK_IDX [%d] with ABSOLUTE offset [%ld]\n",
				file_mapped->data.fd, MemBufferGetSize(page_part_mb), page_part_mb->user_int, file_mapped->data.cur_offset);


		/* We are running THREADED_AIO */
		if (file_mapped->flags.threaded_aio)
		{
			/* Add to MAIN_FILE */
			op_status = ThreadAIOFileWrite(file_mapped->thrdaio_base, &aio_req_pending, file_mapped->data.fd, MemBufferDeref(page_part_mb),
					MemBufferGetSize(page_part_mb), file_mapped->data.cur_offset, EvKQFileMappedAIOGenericUnlockCB, file_mapped);
		}
		/* We are running POSIX_AIO */
		else
		{
			/* Add to MAIN_FILE */
			op_status = EvKQBaseAIOFileWrite(file_mapped->ev_base, &aio_req_pending, file_mapped->data.fd, MemBufferDeref(page_part_mb),
					MemBufferGetSize(page_part_mb), file_mapped->data.cur_offset, EvKQFileMappedAIOGenericUnlockCB, file_mapped);
		}

		/* Failed writing to file */
		if (AIOREQ_FAILED == op_status)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed writing LAST [%d] bytes of data at INDEX [%d] with ABSOLUTE_OFFSET [%ld]\n",
					file_mapped->data.fd, MemBufferGetSize(page_part_mb), page_part_mb->user_int, file_mapped->data.cur_offset);

			return 0;
		}

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - Completed with PARTIAL\n", file_mapped->data.fd);

		/* Lock while writing and set Last block as complete */
		EvKQFileMappedLock(file_mapped);
		DynBitMapBitSet(file_mapped->meta.bitmap, page_part_mb->user_int);
	}
	else
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Completed with no partial\n", file_mapped->data.fd);

	/* Mark mapped_mb as completed */
	file_mapped->flags.complete				= 1;
	file_mapped->flags.pct_string_update	= 1;

	/* Invoke METADATA update */
	EvKQFileMappedMetaDataUpdateAll(file_mapped);

	return 1;
}
/**************************************************************************************************************************/
long EvKQFileMappedGetSize(EvKQFileMapped *file_mapped)
{
	/* Sanity check */
	if (!file_mapped)
		return 0;

	return file_mapped->data.cur_offset;
}
/**************************************************************************************************************************/
int EvKQFileMappedMetaAuxWrite(EvKQFileMapped *file_mapped, MemBuffer *aux_mb)
{
	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Nothing to check, bail out */
	if (!aux_mb)
		return 0;

	/* Already writing METADATA, enqueue it on UPDATE list */
	if (file_mapped->flags.meta_aux_writing)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Already writing AUX_META - PEND [%d] - LOCK_COUNT [%d] - SWAP!\n",
				file_mapped->data.fd, file_mapped->flags.meta_raw_writing, file_mapped->ref_count);

		/* Destroy any pending previous METADATA buffer and ENQUEUE new one */
		MemBufferDestroy(file_mapped->meta.last_mb_aux);
		file_mapped->meta.last_mb_aux = aux_mb;
		return 1;
	}

	/* Set AUX_META and set flags we need UPDATE */
	MemBufferDestroy(file_mapped->meta.data_aux_mb);
	file_mapped->meta.data_aux_mb			= aux_mb;
	file_mapped->flags.meta_aux_need_update = 1;

	/* Kick META_UPDATE ON_DISK */
	EvKQFileMappedMetaDataAuxUpdate(file_mapped);

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOStateNotifyFinishAdd(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long finish_size, long owner_id)
{
	EvKQFileMappedAIOStateNotify *notify_node;

	/* Sanity check */
	if ((!aio_state) || (!finish_cb))
		return 0;

	/* Unable to add finish notify on canceled AIO_STATE */
	if (aio_state->flags.cancelled)
		return 0;

	/* Create a new NOTIFY NODE and populate it */
	notify_node				= EvKQFileMappedAIOStateNotifyNew(aio_state);
	notify_node->cb_func	= finish_cb;
	notify_node->cb_data	= finish_cbdata;
	notify_node->data_sz	= finish_size;
	notify_node->owner_id	= owner_id;

	/* Enqueue it to NOTIFY_LIST */
	DLinkedListAddTail(&aio_state->finish.list, &notify_node->node, notify_node);

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOStateNotifyFinishCancelByOwner(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state, long owner_id)
{
	EvKQFileMappedAIOStateNotify *notify_node;
	DLinkedListNode *node ;
	int cancel_count = 0;

	/* Sanity check */
	if (!aio_state)
		return 0;

	/* Destroy all pending notification data */
	for (node = aio_state->finish.list.head; node; node = node->next)
	{
		notify_node = node->data;

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - NOTIFY_NODE_ID [%ld] - TARGET_ID [%ld] - CANCELED [%d]\n",
				file_mapped->data.fd, notify_node->owner_id, owner_id, notify_node->flags.cancelled);

		/* Owner found, and not canceled. Cancel */
		if ((notify_node->owner_id == owner_id) && (!notify_node->flags.cancelled))
		{
			notify_node->flags.cancelled = 1;
			cancel_count++;
		}

		continue;
	}

	return cancel_count;
}
/**************************************************************************************************************************/
EvKQFileMapped *EvKQFileMappedAIOLoad(EvKQBase *ev_base, EvKQFileMappedConf *conf, EvKQFileMappedFinishCB *finish_cb, void *finish_cbdata, long aiostate_owner_id)
{
	EvKQFileMappedAIOState *aio_state;
	EvKQFileMapped *file_mapped;
	EvAIOReq meta_aux_aio_req;
	EvAIOReq meta_aio_req;

	int aio_cancel_count;
	int op_status;

	/* Sanity check */
	if (!conf)
		return NULL;

	/* Threaded AIO need Threaded AIO BASE */
	if ((conf->flags.threaded_aio) && (!conf->thrdaio_base))
		return NULL;

	/* We must have PATH for DATA and META, AUX_META is optional */
	assert(conf->path_meta_str);
	assert(conf->path_data_str);

	/* Clean up stack */
	memset(&meta_aux_aio_req, 0, sizeof(EvAIOReq));
	memset(&meta_aio_req, 0, sizeof(EvAIOReq));

	/* Create a new FILE_MAPPED to hold data */
	file_mapped = calloc(1, sizeof(EvKQFileMapped));

	/* Populate data */
	file_mapped->ev_base				= ev_base;
	file_mapped->log_base				= conf->log_base;
	file_mapped->thrdaio_base			= conf->thrdaio_base;
	file_mapped->data.page_sz			= conf->page_sz;
	file_mapped->mutex					= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	file_mapped->flags.thread_safe		= ev_base->flags.mt_engine;
	file_mapped->flags.threaded_aio		= conf->flags.threaded_aio;

	/* Save path for METADATA and RAWDATA */
	file_mapped->meta.path_aux_str		= (conf->path_meta_aux_str ? strdup(conf->path_meta_aux_str) : NULL);
	file_mapped->meta.path_str			= strdup(conf->path_meta_str);
	file_mapped->data.path_str			= strdup(conf->path_data_str);

	/* Set we have been created with NEW */
	file_mapped->flags.created_with_new = 1;
	file_mapped->flags.truncate_file	= 0;

	/* Open files with TRUNC OFF */
	op_status							= EvKQFileMappedOpenFiles(file_mapped);

	/* Failed opening META or RAW data files, bail out */
	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed opening files for PATH [%s]\n", file_mapped->data.path_str);
		file_mapped->err_code = op_status;
		goto fail;
	}

	/* Initialize list to hold partial DATA */
	DLinkedListInit(&file_mapped->data.partial_mb_list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&file_mapped->data.aio_state_list, BRBDATA_THREAD_UNSAFE);

	/* Emit the AIO_READ for MAIN_META */
	op_status = ThreadAIOFileReadToMemBuffer(file_mapped->thrdaio_base, &meta_aio_req, file_mapped->meta.fd, NULL, -1, 0, EvKQFileMappedAIOReadMetaDataFinishCB, file_mapped);

	/* Failed opening MAIN_META */
	if (AIOREQ_FAILED == op_status)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD_META [%d] - Failed AIO_READ for MAIN_META_LOAD\n", file_mapped->meta.fd);
		file_mapped->err_code = FILEMAPPED_ERROR_OPENING_METADATA;
		goto fail;
	}
	/* Lock while reading */
	else
		EvKQFileMappedLock(file_mapped);

	/* Emit the AIO_READ for AUX_META */
	if (file_mapped->flags.meta_aux_file)
	{
		op_status = ThreadAIOFileReadToMemBuffer(file_mapped->thrdaio_base, &meta_aux_aio_req, file_mapped->meta.fd_aux, NULL, -1, 0,
				EvKQFileMappedAIOReadMetaAuxDataFinishCB, file_mapped);

		/* Failed opening AUX_META */
		if (AIOREQ_FAILED == op_status)
		{
			/* Cancel previously EMITED AIO_READ for MAIN_META */
			aio_cancel_count		= EvAIOReqQueueCancelAllByFD(&file_mapped->thrdaio_base->req_queue, file_mapped->meta.fd);
			file_mapped->err_code	= FILEMAPPED_ERROR_OPENING_AUX_METADATA;

			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD_META_AUX [%d] - Failed AIO_READ for AUX_META_LOAD - Canceled [%d] pending AIO for META_FD [%d]\n",
					file_mapped->meta.fd_aux, aio_cancel_count, file_mapped->meta.fd);

			goto fail;
		}
		/* Lock while reading */
		else
			EvKQFileMappedLock(file_mapped);
	}

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Begin loading MAPPED_FILE with META_FD [%d]-[%s] and RAW_FD [%d]-[%s]\n",
			file_mapped->meta.fd, file_mapped->meta.path_str, file_mapped->data.fd, file_mapped->data.path_str);

	/* Create a new AIO STATE for this AIO_READ and populate it with FINISH_CB data */
	aio_state					= EvKQFileMappedAIOStateNew(file_mapped);
	aio_state->owner_id			= aiostate_owner_id;
	aio_state->finish.cb_func	= finish_cb;
	aio_state->finish.cb_data	= finish_cbdata;
	aio_state->finish.data_sz	= 0;

	/* Save both pending READ_REQ for this AIO_STATE */
	EvKQFileMappedAIOStatePendingAdd(aio_state, &meta_aio_req, AIOREQ_OPCODE_READ_MB);
	EvKQFileMappedAIOStatePendingAdd(aio_state, &meta_aux_aio_req, AIOREQ_OPCODE_READ_MB);

	/* Touch statistics */
	file_mapped->stats.pending_meta_read++;
	file_mapped->stats.pending_meta_read++;

	/* Set error as NONE and bail */
	file_mapped->err_code		= FILEMAPPED_ERROR_NONE;
	file_mapped->load_aiostate	= aio_state;
	file_mapped->flags.loading	= 1;
	return file_mapped;

	/* TAG for failed code path */
	fail:

	/* Close any file left open */
	EvKQFileMappedCloseFiles(file_mapped);

	/* Release PATH_DATA */
	if (file_mapped->meta.path_aux_str)
		free(file_mapped->meta.path_aux_str);

	free(file_mapped->meta.path_str);
	free(file_mapped->data.path_str);

	free(file_mapped);
	return NULL;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOReadToMemBuffer(EvKQFileMapped *file_mapped, long offset, long data_sz, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long aiostate_owner_id)
{
	EvKQFileMappedAIOState *aio_state;
	EvAIOReq aio_req_pending;
	int op_status;

	/* Sanity check */
	if (!file_mapped)
		return (-FILEMAPPED_AIO_ERROR);

	/* Make sure we are OPENED */
	assert(file_mapped->flags.opened);

	/* Clear pending IO_REQ store */
	memset(&aio_req_pending, 0, sizeof(EvAIOReq));

	/* Create a new AIO STATE for this AIO_READ and populate it with FINISH_CB data */
	aio_state					= EvKQFileMappedAIOStateNew(file_mapped);
	aio_state->owner_id			= aiostate_owner_id;
	aio_state->finish.cb_func	= finish_cb;
	aio_state->finish.cb_data	= finish_cbdata;
	aio_state->finish.data_sz	= data_sz;

	/* Populate READ pointers for FINISH AIO_STATE */
	aio_state->read.data_ptr	= NULL;
	aio_state->read.data_sz		= data_sz;

	/* We are running THREADED_AIO */
	if (file_mapped->flags.threaded_aio)
	{
		/* Begin READING DATA */
		op_status = ThreadAIOFileReadToMemBuffer(file_mapped->thrdaio_base, &aio_req_pending, file_mapped->data.fd, NULL, data_sz, offset,
				EvKQFileMappedAIOReadRawDataFinishCB, file_mapped);
	}
	else
	{
		/* NOT IMPLEMENTED */
		assert(0);
	}

	/* Failed reading from file */
	switch (op_status)
	{
	case AIOREQ_FAILED:
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed reading [%d] bytes of data at OFFSET [%ld]\n",
				file_mapped->data.fd, data_sz, offset);

		/* Delete AIO_STATE from list before destroying it */
		DLinkedListDelete(&file_mapped->data.aio_state_list, &aio_state->node);

		/* Destroy current AIO_STATE and return ERROR */
		EvKQFileMappedAIOStateDestroy(aio_state);
		return (-FILEMAPPED_AIO_ERROR);
	}
	/* Pending AIO_REQ, update AIO_STATE */
	case AIOREQ_FINISHED:
	case AIOREQ_PENDING:
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_STATE [%p] - OP_STATUS [%d] - Pending AIO_REQ at [%d]\n",
				file_mapped->data.fd, aio_state, op_status, aio_req_pending.id);

		/* Lock while reading */
		EvKQFileMappedLock(file_mapped);

		/* Save pending WRITE_REQ for this AIO_STATE */
		EvKQFileMappedAIOStatePendingAdd(aio_state, &aio_req_pending, AIOREQ_OPCODE_READ_MB);
		file_mapped->stats.pending_raw_read++;
		break;
	}
	/* This should not happen */
	default:
		assert(0);
	}

	/* Return ANY of the AIO_IDs in AIO_STATE. This will be enough to find AIO_STATE back from its hell hole */
	return aio_state->pending_req[0].id;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIORead(EvKQFileMapped *file_mapped, char *data, long offset, long data_sz, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long aiostate_owner_id)
{
	EvKQFileMappedAIOState *aio_state;
	EvAIOReq aio_req_pending;
	int op_status;

	/* Sanity check */
	if (!file_mapped)
		return (-FILEMAPPED_AIO_ERROR);

	/* Make sure we are OPENED */
	assert(file_mapped->flags.opened);

	/* Clear pending IO_REQ store */
	memset(&aio_req_pending, 0, sizeof(EvAIOReq));

	/* Create a new AIO STATE for this AIO_READ and populate it with FINISH_CB data */
	aio_state					= EvKQFileMappedAIOStateNew(file_mapped);
	aio_state->owner_id			= aiostate_owner_id;
	aio_state->finish.cb_func	= finish_cb;
	aio_state->finish.cb_data	= finish_cbdata;
	aio_state->finish.data_sz	= data_sz;

	/* Populate READ pointers for FINISH AIO_STATE */
	aio_state->read.data_ptr	= data;
	aio_state->read.data_sz		= data_sz;

	/* We are running THREADED_AIO */
	if (file_mapped->flags.threaded_aio)
	{
		/* Begin READING DATA */
		op_status = ThreadAIOFileRead(file_mapped->thrdaio_base, &aio_req_pending, file_mapped->data.fd, data, data_sz, offset,
				EvKQFileMappedAIOReadRawDataFinishCB, file_mapped);
	}
	else
	{
		/* Begin READING DATA */
		op_status = EvKQBaseAIOFileRead(file_mapped->ev_base, &aio_req_pending, file_mapped->data.fd, data, data_sz, offset,
				EvKQFileMappedAIOReadRawDataFinishCB, file_mapped);
	}

	/* Failed reading from file */
	switch (op_status)
	{
	case AIOREQ_FAILED:
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed reading [%d] bytes of data at OFFSET [%ld]\n",
				file_mapped->data.fd, data_sz, offset);

		/* Delete AIO_STATE from list before destroying it */
		DLinkedListDelete(&file_mapped->data.aio_state_list, &aio_state->node);

		/* Destroy current AIO_STATE and return ERROR */
		EvKQFileMappedAIOStateDestroy(aio_state);
		return (-FILEMAPPED_AIO_ERROR);
	}
	/* Pending AIO_REQ, update AIO_STATE */
	case AIOREQ_FINISHED:
	case AIOREQ_PENDING:
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_STATE [%p] - OP_STATUS [%d] - Pending AIO_REQ at [%d]\n",
				file_mapped->data.fd, aio_state, op_status, aio_req_pending.id);

		/* Lock while reading */
		EvKQFileMappedLock(file_mapped);

		/* Save pending WRITE_REQ for this AIO_STATE */
		EvKQFileMappedAIOStatePendingAdd(aio_state, &aio_req_pending, AIOREQ_OPCODE_READ);
		break;
	}
	}

	/* Return ANY of the AIO_IDs in AIO_STATE. This will be enough to find AIO_STATE back from its hell hole */
	return aio_state->pending_req[0].id;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOAdd(EvKQFileMapped *file_mapped, char *data, long data_sz, EvKQFileMappedFinishCB *finish_cb, void *finish_cbdata, long aiostate_owner_id)
{
	int op_status;

	/* Sanity check */
	if (!file_mapped)
		return FILEMAPPED_AIO_ERROR;

	/* Write and update internal offset */
	op_status = EvKQFileMappedAIOWrite(file_mapped, data, file_mapped->data.cur_offset, data_sz, finish_cb, finish_cbdata, aiostate_owner_id);

	/* Failed AIO_WRITE SYNC */
	if (op_status < 0)
		return op_status;

	/* Update offset */
	file_mapped->data.cur_offset += data_sz;
	return op_status;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOWriteMemBufferAndDestroy(EvKQFileMapped *file_mapped, MemBuffer *data_mb, long offset, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long aiostate_owner_id)
{
	EvKQFileMappedAIOState *aio_state;
	int req_id;

	char *data_ptr	= MemBufferDeref(data_mb);
	long data_sz	= MemBufferGetSize(data_mb);

	/* Sanity check */
	if (!file_mapped)
		return FILEMAPPED_AIO_ERROR;

	/* Issue AIO_WRITE */
	req_id = EvKQFileMappedAIOWrite(file_mapped, data_ptr, offset, data_sz, finish_cb, finish_cbdata, aiostate_owner_id);

	/* Write either failed_sync or finished_sync, check */
	if (req_id < 0)
		return req_id;

	/* Left pending AIO_WRITE requests, grab back AIO_STATE to input destroy CB_FUNC into it */
	aio_state = EvKQFileMappedAIOStateGetByAIOReqID(file_mapped, req_id);
	assert(aio_state);

	/* Populate destroy data */
	aio_state->destroy.cb_func = EvKQFileMappedAIO_MemBufferDestroyFunc;
	aio_state->destroy.cb_data = data_mb;

	return req_id;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOWrite(EvKQFileMapped *file_mapped, char *data, long offset, long data_sz, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long aiostate_owner_id)
{
	EvKQFileMappedAIOState *aio_state;
	char *rounded_data_ptr;
	int delayed_count;
	long rounded_data_sz;
	long rounded_data_offset;
	long block_idx_lower;
	long block_idx_higher;
	long block_idx_begin;
	long block_idx_finish;
	long block_count_total;
	long block_size_trail;
	long block_size_finish;
	long block_off_relative;

	int aligned_status	= -1;
	int trail_status	= -1;
	int head_status		= -1;

	/* Sanity check */
	if (!file_mapped)
		return (-FILEMAPPED_AIO_ERROR);

	/* Make sure we are OPENED */
	assert(file_mapped->flags.opened);

	/* Calculate LOWER block INDEX, TOTAL block count and how many bytes are incomplete in trail data */
	block_idx_lower		= (offset / file_mapped->data.page_sz);
	block_idx_higher	= ((offset + data_sz) / file_mapped->data.page_sz);
	block_count_total	= (data_sz / file_mapped->data.page_sz);
	block_off_relative	= (offset % file_mapped->data.page_sz);

	/* Calculate MISALIGNED data on the TAIL and HEAD of BUFFER */
	block_size_trail	= ((block_off_relative > 0) ? (file_mapped->data.page_sz - block_off_relative) : 0);
	block_size_trail	= ((block_size_trail > data_sz) ? data_sz : block_size_trail);
	block_size_finish	= ((data_sz - block_size_trail) % file_mapped->data.page_sz);

	/* Calculate block INDEXEs where ALIGNED data BEGIN and FINISH */
	block_idx_begin		= ((block_size_trail > 0) ? (block_idx_lower + 1) : block_idx_lower);
	block_idx_finish	= ((block_size_finish > 0) ? (block_idx_higher + 1) : block_idx_higher);

	/* Calculate ROUNDED ALIGNED data begin PTR and SIZE */
	rounded_data_ptr	= (data + block_size_trail);
	rounded_data_sz		= (data_sz - (block_size_trail + block_size_finish));
	rounded_data_offset	= (block_idx_higher * file_mapped->data.page_sz);

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN,
			"FD [%d] - ABS_OFFSET [%ld] - DATA_PAGE_SZ [%ld / %d] | BLOCK_LOW/BEGIN/HIGH/FINISH_IDX [%d / %d/ %d / %d] | BLOCK_COUNT [%d] - "
			"TRAIL/FINISH_DATA_SZ [%d / %d] - DATA -> [%d]-[%d]\n", file_mapped->data.fd, offset, data_sz, file_mapped->data.page_sz, block_idx_lower,
			block_idx_begin, block_idx_higher, block_idx_finish, block_count_total, block_size_trail, block_size_finish, rounded_data_sz,
			rounded_data_offset /*rounded_data_ptr*/);

	/* Create a new write STATE for this AIO_WRITE and populate it with FINISH_CB data */
	aio_state					= EvKQFileMappedAIOStateNew(file_mapped);
	aio_state->owner_id			= aiostate_owner_id;
	aio_state->finish.cb_func	= finish_cb;
	aio_state->finish.cb_data	= finish_cbdata;
	aio_state->finish.data_sz	= data_sz;

	/* Issue the ALIGNED write invoke if there is DATA to write */
	if (rounded_data_sz > 0)
		aligned_status = EvKQFileMappedAlignedWrite(file_mapped, aio_state, rounded_data_ptr, rounded_data_sz, block_idx_begin);

	/* There is TRAIL data, save it */
	if (block_size_trail > 0)
		trail_status = EvKQFileMappedPartialDataProcess(file_mapped, aio_state, data, block_size_trail, block_idx_lower, block_off_relative);

	/* There is FINISH data, save it */
	if (block_size_finish > 0)
		head_status = EvKQFileMappedPartialDataProcess(file_mapped, aio_state, (rounded_data_ptr + rounded_data_sz), block_size_finish, block_idx_higher, 0);

	/* Update higher seen offset */
	if (file_mapped->data.cur_offset < (offset + data_sz))
		file_mapped->data.cur_offset = (offset + data_sz);

	/* Need PCT string UPDATE */
	file_mapped->flags.pct_string_update	= 1;

	/* Some of the writes has failed - Just LOG, finish CB will set error flags */
	if ((!aligned_status) || (!head_status) || (!trail_status))
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - [%d] bytes - Some writes FAILED! ALIGNED [%d] - HEAD [%d] - TRAIL [%d] - PEND AIO [%d]\n",
				file_mapped->data.fd, data_sz, aligned_status, head_status, trail_status, aio_state->req_count_pending);

		/* Delete AIO_STATE from list before destroying it */
		DLinkedListDelete(&file_mapped->data.aio_state_list, &aio_state->node);

		/* Destroy current AIO_STATE and return ERROR */
		EvKQFileMappedAIOStatePendingCancelAll(file_mapped, aio_state);
		EvKQFileMappedAIOStateDestroy(aio_state);
		return (-FILEMAPPED_AIO_ERROR);
	}

	/* Pending requests should not be NEGATIVE! */
	assert(aio_state->req_count_pending >= 0);

	/* If there is no PENDING AIO, invoke FINISH CB and drop AIO_STATE */
	if (!aio_state->req_count_pending)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - [%d] bytes - No pending request, ALIGN [%d] - HEAD [%d] - TRAIL [%d] - Finishing\n",
				file_mapped->data.fd, data_sz, aligned_status, head_status, trail_status);

		/* Invoke finish CB and destroy state */
		EvKQFileMappedAIOStateInvokeAndDestroy(aio_state);
		return (-FILEMAPPED_AIO_FINISHED);
	}
	else
	{
		/* Set DELAYED notification for this AIO_STATE, so we can SAVE CPU cycles avoiding notification of UNFINISHED objects */
		delayed_count = EvKQFileMappedAIOStateSetDelayedNotify(aio_state);

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN,
				"FD [%d] - [%d] bytes - Finished with [%d] pending AIO request at [%p] - Delayed [%d] - ID_ARR [%d]-[%d]-[%d]-[%d] - AIO_LIST [%ld]\n",
				file_mapped->data.fd, data_sz, aio_state->req_count_pending, aio_state, delayed_count, aio_state->pending_req[0].id,
				aio_state->pending_req[1].id, aio_state->pending_req[2].id, aio_state->pending_req[3].id, file_mapped->data.aio_state_list.size);
	}

	/* Return ANY of the AIO_IDs in AIO_STATE. This will be enough to find AIO_STATE back from its hell hole */
	return aio_state->pending_req[0].id;
}
/**************************************************************************************************************************/
MemBuffer *EvKQFileMappedPartialGetByIdx(EvKQFileMapped *file_mapped, long block_idx)
{
	MemBuffer *page_part_mb;
	DLinkedListNode *node;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - HEAD [%p]\n",
			file_mapped->data.fd, file_mapped->data.partial_mb_list.head);

	/* Walk all partial blocks searching for desired BLOCK */
	for (node = file_mapped->data.partial_mb_list.head; node; node = node->next)
	{
		/* Grab Partial MB */
		page_part_mb 	= node->data;

		/* Block found, STOP */
		if (block_idx == page_part_mb->user_int)
			return page_part_mb;

		continue;
	}

	return NULL;
}
/**************************************************************************************************************************/
int EvKQFileMappedPartialClean(EvKQFileMapped *file_mapped)
{
	DLinkedListNode *node;
	MemBuffer *cur_mb;
	int destroy_count;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Point to partial list HEAD */
	node			= file_mapped->data.partial_mb_list.head;
	destroy_count	= 0;

	/* Destroy all pending partial MEMBUFFERs */
	while (node)
	{
		cur_mb	= node->data;
		node	= node->next;

		/* Destroy and touch counter */
		MemBufferDestroy(cur_mb);
		destroy_count++;
		continue;
	}

	/* Reset partial data list */
	DLinkedListReset(&file_mapped->data.partial_mb_list);
	return destroy_count;
}
/**************************************************************************************************************************/
long EvKQFileMappedPartialGetSize(EvKQFileMapped *file_mapped)
{
	MemBuffer *page_part_mb;
	DLinkedListNode *node;

	long partial_sz = 0;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Nothing on list, leave */
	if (DLINKED_LIST_ISEMPTY(file_mapped->data.partial_mb_list))
		return 0;

	for (node = file_mapped->data.partial_mb_list.head; node; node = node->next)
	{
		page_part_mb	= node->data;
		partial_sz		+= MemBufferGetSize(page_part_mb);

		continue;
	}

	return partial_sz;
}
/**************************************************************************************************************************/
int EvKQFileMappedCheckBytes(EvKQFileMapped *file_mapped, long base, long offset)
{
	MemBuffer *page_part_mb;
	long base_byte_off;
	long offset_byte_off;
	long block_idx_begin;
	long block_idx_finish;
	long page_part_mb_sz;
	int have_bytes;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Base MUST >= ZERO */
	if (base < 0)
		base = 0;

	/* If offset < Base, overwrite to size of MemBuffer MAIN  */
	if (offset < base)
		offset = file_mapped->data.cur_offset;

	/* Calculate indexes - CEIL the last block to upper next */
	block_idx_begin		= (base / file_mapped->data.page_sz);
	block_idx_finish 	= (offset / file_mapped->data.page_sz);

	/* Calculate OFFSETs - From inter-block offset */
	base_byte_off		= (base % file_mapped->data.page_sz);
	offset_byte_off 	= (offset % file_mapped->data.page_sz);

	/* Base is partial request */
	if (base_byte_off > 0)
	{
		/* Walk our partial PAGE list and check if we have some partial data for this offset lying around */
		page_part_mb 	= EvKQFileMappedPartialGetByIdx(file_mapped, block_idx_begin);
		page_part_mb_sz = MemBufferGetSize(page_part_mb);

		/* We found this partial */
		if (page_part_mb)
		{
			/* Offset of partial is greater than base */
			if (base_byte_off < page_part_mb->user_long)
				return 0;

			/* Check if we are searching for the same block */
			if ((block_idx_begin == block_idx_finish))
			{
				/* We not satisfy the size to read offset */
				if (page_part_mb_sz < (offset_byte_off + 1))
					return 0;
				else
					return 1;
			}

			/* First partial, is broken */
			if (page_part_mb_sz < file_mapped->data.page_sz)
				return 0;

			block_idx_begin++;
		}
	}


	/* Offset is partial request */
	if (offset_byte_off > 0)
	{
		/* Walk our partial PAGE list and check if we have some partial data for this offset lying around */
		page_part_mb 	= EvKQFileMappedPartialGetByIdx(file_mapped, block_idx_finish);
		page_part_mb_sz = MemBufferGetSize(page_part_mb);

		/* We found this partial */
		if (page_part_mb)
		{
			/* Partial don't have first bytes */
			if (page_part_mb->user_long > 0)
				return 0;

			/* We not satisfy the size to read offset */
			if (page_part_mb_sz < (offset_byte_off + 1))
				return 0;

			block_idx_finish--;
		}
	}

	/* Check if We have this blocks complete */
	have_bytes = DynBitMapCheckMultiBlocks(file_mapped->meta.bitmap, block_idx_begin, block_idx_finish);

	return have_bytes;
}
/**************************************************************************************************************************/
int EvKQFileMappedGetValidBytes(EvKQFileMapped *file_mapped, long base, long offset, EvKQFileMappedValidBytes *valid_bytes)
{
	DynBitMapValidBlockIdx valid_block_idx;
	MemBuffer *page_part_mb;
	long valid_blocks;
	long block_idx_begin;
	long block_idx_finish;
	long mapped_mb_sz		= file_mapped->data.cur_offset;
	long found_byte_base 	= 0;
	long found_byte_offset	= 0;
	long page_part_mb_sz 	= 0;

	/* Initialize */
	valid_bytes->first_byte = -1;
	valid_bytes->last_byte 	= -1;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Sanity check values */
	if ((base > mapped_mb_sz))
		return 0;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - LOOKUP - BASE [%ld] - OFFSET [%ld]\n", file_mapped->data.fd, base, offset);

	/* Base MUST >= ZERO */
	if (base < 0)
		base 			= 0;

	/* If offset < Base, overwrite to size of MemBuffer MAIN  */
	if (offset < base)
		offset 			= (file_mapped->data.cur_offset - 1);

	/* Calculate indexes - CEIL the last block to upper next */
	block_idx_begin		= (base / file_mapped->data.page_sz);
	block_idx_finish 	= (offset / file_mapped->data.page_sz);

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - VALID_CHECK - BASE: IDX [%ld] B: [%ld] - OFFSET: IDX [%ld] B: [%ld] - CUR [%ld] - PG [%d]\n",
			file_mapped->data.fd, block_idx_begin, base, block_idx_finish, offset, file_mapped->data.cur_offset, file_mapped->data.page_sz);

	/* Search which blocks are valid */
	valid_blocks 		= DynBitMapGetValidBlocks(file_mapped->meta.bitmap, block_idx_begin, block_idx_finish, &valid_block_idx);

	/* We have valid PAGE blocks that will satisfy this request */
	if (valid_blocks)
	{
		/* Calculate first and last valid bytes within the requested data chain */
		valid_bytes->first_byte = (valid_block_idx.first_idx * file_mapped->data.page_sz);
		valid_bytes->last_byte 	= (valid_block_idx.last_idx * file_mapped->data.page_sz) + (file_mapped->data.page_sz - 1);

		/* We found base into complete blocks */
		if (valid_bytes->first_byte <= base)
		{
			valid_bytes->first_byte	= base;
			found_byte_base			= 1;
		}

		/* We found offset into complete blocks */
		if (valid_bytes->last_byte >= offset)
		{
			valid_bytes->last_byte	= offset;
			found_byte_offset		= 1;
		}

		/* Grab current valid blocks INDEXES */
		block_idx_begin 	= (valid_block_idx.first_idx - 1);
		block_idx_finish 	= (valid_block_idx.last_idx + 1);

	}
	/* There is not enough blocks to satisfy this request */
	else
	{
		block_idx_finish	= block_idx_begin;
	}

	/* Check for first bytes into partial */
	if (!found_byte_base)
	{
		/* Grab partial */
		page_part_mb 	= EvKQFileMappedPartialGetByIdx(file_mapped, block_idx_begin);
		page_part_mb_sz = MemBufferGetSize(page_part_mb);

		/* We found a partial */
		if (page_part_mb)
		{
			/* Base of partial is greater than base - Sanity check last byte information */
			valid_bytes->first_byte = (block_idx_begin * file_mapped->data.page_sz) + page_part_mb->user_long;
			valid_bytes->first_byte	= (valid_bytes->first_byte < base) ? -1 : valid_bytes->first_byte;

			/* Check if we are searching for the same block */
			if ((block_idx_begin == block_idx_finish))
			{
				valid_bytes->last_byte 	= (block_idx_begin * file_mapped->data.page_sz) + (page_part_mb_sz - 1);
				found_byte_offset 		= 1;
			}
		}
	}

	/* Check for last bytes into partial */
	if (!found_byte_offset)
	{
		/* Walk our partial PAGE list and check if we have some partial data for this offset lying around */
		page_part_mb 	= EvKQFileMappedPartialGetByIdx(file_mapped, block_idx_finish);
		page_part_mb_sz = MemBufferGetSize(page_part_mb);

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - LOOKUP BYTE_OFFSET [%p] - [%ld] - BLOCK [%ld]\n",
					file_mapped->data.fd, page_part_mb, page_part_mb ? page_part_mb->user_long : -1, block_idx_finish);

		/* We found this partial */
		if (page_part_mb)
		{
			/* Partial don't have first bytes */
			if (page_part_mb->user_long == 0)
				valid_bytes->last_byte 	= (block_idx_finish * file_mapped->data.page_sz) + (page_part_mb_sz - 1);
		}
	}

	/* Correct last byte info */
	valid_bytes->last_byte	= (valid_bytes->last_byte < base) ? -1 : valid_bytes->last_byte;

	/* Give up, we have nothing */
	if ((valid_bytes->first_byte < 0) || valid_bytes->last_byte < 0)
		return 0;

	/* Populate absolute valid range information */
	valid_bytes->first_byte 	= (valid_bytes->first_byte < base) ? base : valid_bytes->first_byte;
	valid_bytes->last_byte 		= (valid_bytes->last_byte > offset) ? offset : valid_bytes->last_byte;

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedCheckLastBlock(EvKQFileMapped *file_mapped, long total_sz)
{
	EvAIOReq aio_req_pending;
	int op_status;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Grab Partial by block index */
	long last_block_sz			= (total_sz % file_mapped->data.page_sz);
	long last_page_idx			= (total_sz / file_mapped->data.page_sz);
	MemBuffer *page_part_mb 	= EvKQFileMappedPartialGetByIdx(file_mapped, last_page_idx);
	long  page_part_mb_sz		= MemBufferGetSize(page_part_mb);

	/* Clear pending IO_REQ store */
	memset(&aio_req_pending, 0, sizeof(EvAIOReq));

	/* Have last part, check is its complete */
	if (page_part_mb)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Check last block ID [%d] - PAGE_SZ [%d] - PAGE_OFF [%ld] - SIZES [%ld] -> [%ld]\n",
				last_page_idx, file_mapped->data.page_sz, page_part_mb->user_long, page_part_mb_sz, last_block_sz);

		/* Partial don't have first bytes */
		if (page_part_mb->user_long > 0)
			return 0;

		if (page_part_mb_sz >= last_block_sz)
		{
			/* We are running THREADED_AIO */
			if (file_mapped->flags.threaded_aio)
			{
				/* Write main aligned data */
				op_status = ThreadAIOFileWrite(file_mapped->thrdaio_base, &aio_req_pending, file_mapped->data.fd,  MemBufferDeref(page_part_mb), last_block_sz,
						(total_sz - last_block_sz), EvKQFileMappedAIOGenericUnlockCB, file_mapped);
			}
			/* We are running POSIX_AIO */
			else
			{
				/* Write main aligned data */
				op_status = EvKQBaseAIOFileWrite(file_mapped->ev_base, &aio_req_pending, file_mapped->data.fd,  MemBufferDeref(page_part_mb), last_block_sz,
						(total_sz - last_block_sz), EvKQFileMappedAIOGenericUnlockCB, file_mapped);
			}

			/* Failed writing to file */
			if (AIOREQ_FAILED == op_status)
			{
				KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed writing [%d] bytes of data at INDEX [%d] with ABSOLUTE_OFFSET [%ld]\n",
						file_mapped->data.fd, last_block_sz, last_page_idx, (total_sz - last_block_sz));
				return 0;
			}

			/* Set Last block as complete */
			EvKQFileMappedLock(file_mapped);
			DynBitMapBitSet(file_mapped->meta.bitmap, last_page_idx);
		}
	}

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedGenerateStringPCT(EvKQFileMapped *file_mapped, char *ret_mask, int ret_mask_size, long total_sz)
{
	int active_factor;
	int active_factor_mod;
	int bit_active;
	long i;
	long j;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Already update, bail out */
	if (!file_mapped->flags.pct_string_update)
		return 0;

	DynBitMap *dyn_bitmap  		= file_mapped->meta.bitmap;
	int last_page_idx			= (total_sz / file_mapped->data.page_sz);
	int cc						= 0;
	int ci						= 0;
	int step_idx 				= 0;
	int step_offset 			= 0;

	/* Sanity check */
	if (!dyn_bitmap || dyn_bitmap->bitmap.higher_seen <= 0)
		return 0;

	/* Calculate how many continuous blocks will set a single mask byte */
	active_factor 		= (last_page_idx < ret_mask_size) ? 1 : (last_page_idx / ret_mask_size);
	active_factor_mod	= (last_page_idx % ret_mask_size);

	/* Walk all DOTs */
	for (i = 0; i < last_page_idx; i++)
	{
		bit_active	= DynBitMapBitTest(dyn_bitmap, i);
		cc			+= bit_active ? 1 : 0;

		if (((i % active_factor) == 0) || (i == last_page_idx))
		{
			step_offset = ((i + 1) * ret_mask_size) / last_page_idx;

			/* Print in chars the progress */
			for (ci = step_idx; ci < step_offset, ci < ret_mask_size; ci++)
				ret_mask[ci]	= cc > 0 ? '1' : '0';

			cc 			= 0;
			step_idx	= step_offset;

			continue;
		}

		continue;
	}

	/* Do not need update */
	file_mapped->flags.pct_string_update	= 0;
	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
EvKQFileMappedAIOState *EvKQFileMappedAIOStateGetByAIOReqID(EvKQFileMapped *file_mapped, int aioreq_id)
{
	EvKQFileMappedAIOState *aio_state;
	DLinkedListNode *node;
	int i;

	/* REMOVE ME */
	int limit_out	= 8092;
	int limit_in	= 8092;

	/* Sanity check */
	if (!file_mapped)
		return NULL;

	/* Sanity check */
	if (aioreq_id < 0)
		return NULL;

	/* No item on list, bail out */
	if (!file_mapped->data.aio_state_list.head)
		return NULL;

	/* Walk all write states searching for this AIO_REQ_ID */
	for (node = file_mapped->data.aio_state_list.head; node; node = node->next)
	{
		aio_state = node->data;

		/* Pending requests should not be NEGATIVE! */
		assert(aio_state->req_count_pending >= 0);

		/* REMOVE ME */
		assert(limit_out-- > 0);
		limit_in	= 8092;

		/* Search for AIO_REQID inside this write state */
		for (i = 0; i < aio_state->req_count_pending; i++)
		{
			/* REMOVE ME */
			assert(limit_in-- > 0);

			/* Reseted ID, bail */
			if (aio_state->pending_req[i].id < 0)
				continue;

			/* Found it, mark finished */
			if (aio_state->pending_req[i].id == aioreq_id)
				return aio_state;

			continue;
		}

		continue;
	}

	return NULL;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOStateSetDelayedNotify(EvKQFileMappedAIOState *aio_state)
{
	EvAIOReq *aio_req;
	int aioreq_id;
	int delay_count;
	int i;

	/* Sanity check */
	if (!aio_state)
		return 0;

	EvKQFileMapped *file_mapped = aio_state->parent_map;
	EvKQBase *ev_base			= file_mapped->ev_base;

	/* Search for AIO_REQID inside this write state */
	for (i = 0, delay_count = 0; i < aio_state->req_count_pending; i++)
	{
		aioreq_id =	aio_state->pending_req[i].id;

		/* Reseted ID, bail out */
		if (aioreq_id < 0)
			continue;

		/* Grab AIO_REQ back from QUEUE */
		aio_req = EvAIOReqQueueGrabByID(&ev_base->aio.queue, aioreq_id);

		/* Set delayed notify flag */
		if (aio_req)
		{
			aio_req->flags.aio_delayed_notify = 1;
			delay_count++;
		}

		continue;
	}

	return delay_count;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOStateCancelByAIOReqID(EvKQFileMapped *file_mapped, int aioreq_id)
{
	EvKQFileMappedAIOState *aio_state;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	/* Search aio_state by AIO_REQ inside this FILE_MAPPED */
	aio_state = EvKQFileMappedAIOStateGetByAIOReqID(file_mapped, aioreq_id);

	/* Not found, bail out */
	if (!aio_state)
		return 0;

	/* Set flags */
	aio_state->flags.cancelled = 1;

	return 1;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOStateCancelAllByOwner(EvKQFileMapped *file_mapped, long owner_id)
{
	EvKQFileMappedAIOState *aio_state;
	DLinkedListNode *node;
	int aio_delta;

	EvKQFileMappedAIOState *aio_state_load	= (file_mapped ? file_mapped->load_aiostate : NULL);
	int cancel_count						= 0;
	int aio_pend_count						= 0;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - OWNER_ID [%ld] - AIO_COUNT [%ld] - LOAD_AIO PTR/OWNER_ID/FINISH_SZ [%p / %ld / %ld]\n",
			(file_mapped ? file_mapped->data.fd : -1), owner_id, (file_mapped ? file_mapped->data.aio_state_list.size : -1), aio_state_load,
			(aio_state_load ? aio_state_load->owner_id : -1), (aio_state_load ? aio_state_load->finish.list.size : -1));

	/* If there is a LOAD_AIO_STATE, cancel right now */
	if (aio_state_load)
	{
		/* Owner found, cancel */
		if ((aio_state_load->owner_id == owner_id) && (!aio_state_load->flags.cancelled))
		{
			/* Calculate how many AIO requests are pending for this AIO_STATE */
			aio_delta = (aio_state_load->req_count_pending - aio_state_load->req_count_finished);
			assert(aio_delta >= 0);

			/* For each pending AIO request, we should have a LOCK on this FILE_MAP, so account it */
			aio_pend_count	+= aio_delta;

			/* Set flags */
			aio_state_load->flags.cancelled = 1;
			cancel_count++;
		}

		/* Cancel any FINISH_NOTIFY pending ID that matches this OWNER_ID */
		cancel_count += EvKQFileMappedAIOStateNotifyFinishCancelByOwner(file_mapped, aio_state_load, owner_id);
	}

	/* Walk all write states searching for this AIO_REQ_ID */
	for (node = file_mapped->data.aio_state_list.head; node; node = node->next)
	{
		aio_state = node->data;

		/* Owner found, cancel */
		if ((aio_state->owner_id == owner_id) && (!aio_state->flags.cancelled))
		{
			/* Calculate how many AIO requests are pending for this AIO_STATE */
			aio_delta = (aio_state->req_count_pending - aio_state->req_count_finished);
			assert(aio_delta >= 0);

			/* For each pending AIO request, we should have a LOCK on this FILE_MAP, so account it */
			aio_pend_count	+= aio_delta;

			/* Set flags */
			aio_state->flags.cancelled = 1;
			cancel_count++;
		}

		/* Cancel any FINISH_NOTIFY pending ID that matches this OWNER_ID */
		cancel_count += EvKQFileMappedAIOStateNotifyFinishCancelByOwner(file_mapped, aio_state, owner_id);
		continue;
	}

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Cancelled AIO_STATE with [%d] pending AIO_REQs\n", file_mapped->data.fd, aio_pend_count);
	return cancel_count;
}
/**************************************************************************************************************************/
int EvKQFileMappedAIOStateDumpAll(EvKQFileMapped *file_mapped)
{
	EvKQFileMappedAIOState *aio_state;
	DLinkedListNode *node;

	/* Sanity check */
	if (!file_mapped)
		return 0;

	if (file_mapped->data.aio_state_list.size > 0)
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Will DUMP [%d] pending AIO_STATEs\n", file_mapped->data.aio_state_list.size);

	/* Walk all write states searching for this AIO_REQ_ID */
	for (node = file_mapped->data.aio_state_list.head; node; node = node->next)
	{
		aio_state = node->data;

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "AIO_STATE [%p] - REQ_COUNT_FINSIH [%d / %d] - REQ_ID [%d / %d / %d / %d]\n", aio_state,
				aio_state->req_count_pending, aio_state->req_count_finished,
				aio_state->pending_req[0].id, aio_state->pending_req[1].id, aio_state->pending_req[2].id, aio_state->pending_req[3].id);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQFileMappedCloseFiles(EvKQFileMapped *file_mapped)
{
	/* There are pending references to this MAP, set close flags - Allow > 1 because someone might call CLOSE and THEN UNLOCK it */
	if (file_mapped->ref_count > 1)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Locked [%d]! Will not close\n", file_mapped->data.fd, file_mapped->ref_count);
		return 0;
	}

	/* There are pending references to this MAP, set close flags */
	if (file_mapped->flags.meta_raw_writing)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Writing META! Will not close\n", file_mapped->data.fd);
		return 0;
	}

	/* There are pending references to this MAP, set close flags */
	if (!file_mapped->flags.opened)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Already CLOSED! Will not close\n", file_mapped->data.fd);
		return 0;
	}

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - META_FD [%d] - Will close\n",
			file_mapped->data.fd, file_mapped->meta.fd);

	/* Close FDs - TODO: Maybe this should be done inside a THREAD_CONTEXT */
	EvKQBaseAIOFileClose(file_mapped->ev_base, file_mapped->meta.fd);
	EvKQBaseAIOFileClose(file_mapped->ev_base, file_mapped->data.fd);
	EvKQBaseAIOFileClose(file_mapped->ev_base, file_mapped->meta.fd_aux);

	/* Reset FDs */
	file_mapped->meta.fd_aux			= -1;
	file_mapped->meta.fd				= -1;
	file_mapped->data.fd				= -1;

	/* Set flags we are CLOSED */
	file_mapped->flags.opened			= 0;
	file_mapped->flags.close_request	= 0;

	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedOpenFiles(EvKQFileMapped *file_mapped)
{
	int local_flags = (file_mapped->flags.truncate_file ?  (O_RDWR | O_CREAT | O_TRUNC | O_NONBLOCK) : (O_RDWR | O_CREAT | O_NONBLOCK));

	/* Initialize FDs */
	file_mapped->meta.fd		= -1;
	file_mapped->data.fd		= -1;
	file_mapped->meta.fd_aux	= -1;

	/* Open FD for META - Will hold bitmap and FILEMAPPED_META */
	file_mapped->meta.fd = EvKQBaseAIOFileOpen(file_mapped->ev_base, file_mapped->meta.path_str, local_flags, 0644);

	/* Failed opening, STOP */
	if (file_mapped->meta.fd < 0)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed opening METADATA FD for file mapped with ERRNO [%d]\n", errno);
		return (-FILEMAPPED_ERROR_OPENING_METADATA);
	}

	/* Open FD for DATA */
	file_mapped->data.fd = EvKQBaseAIOFileOpen(file_mapped->ev_base, file_mapped->data.path_str, local_flags, 0644);

	/* Failed opening, STOP */
	if (file_mapped->data.fd < 0)
	{
		/* Close previously opened METADATA FD */
		close(file_mapped->meta.fd);
		file_mapped->meta.fd	= -1;
		file_mapped->data.fd	= -1;

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed opening RAWDATA FD for file mapped with ERRNO [%d]\n", errno);
		return (-FILEMAPPED_ERROR_OPENING_RAWDATA);
	}

	/* There is AUXILIARY META PATH - Will contain HTTP_REQ and REPL in case of HTTP file for example */
	if (file_mapped->meta.path_aux_str)
	{
		/* Open FD for AUX META */
		file_mapped->meta.fd_aux = EvKQBaseAIOFileOpen(file_mapped->ev_base, file_mapped->meta.path_aux_str, local_flags, 0644);

		/* Failed opening, STOP */
		if (file_mapped->meta.fd_aux < 0)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed opening AUX METADATA FD for file mapped with ERRNO [%d]\n", errno);

			/* Close previously opened META_FD */
			close(file_mapped->meta.fd);
			close(file_mapped->data.fd);

			/* Reset FDs */
			file_mapped->meta.fd		= -1;
			file_mapped->data.fd		= -1;
			file_mapped->meta.fd_aux	= -1;

			return (-FILEMAPPED_ERROR_OPENING_AUX_METADATA);
		}

		/* We have a META_AUXFILE to work with */
		file_mapped->flags.meta_aux_file = 1;
	}

	/* Set flags we are OPEN */
	file_mapped->flags.opened = 1;
	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAlignedWrite(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state, char *data, long data_sz, long block_idx)
{
	EvAIOReq aio_req_pending;
	MemBuffer *page_part_mb;
	int op_status;
	int i;

	long abs_offset			= (block_idx * file_mapped->data.page_sz);
	long block_count		= (data_sz / file_mapped->data.page_sz);
	long block_upper_idx	= block_count + block_idx;

	/* Clear pending IO_REQ store */
	memset(&aio_req_pending, 0, sizeof(EvAIOReq));

	/* Write must be ALIGNED to PAGE_SIZE */
	assert((data_sz % file_mapped->data.page_sz) == 0);

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Writing [%d] bytes from BLOCK_IDX [%d] with ABSOLUTE offset [%ld]\n",
			file_mapped->data.fd, data_sz, block_idx, abs_offset);

	/* We are running THREADED_AIO */
	if (file_mapped->flags.threaded_aio)
	{
		/* Write main aligned data - Finish CALLBACK will never be called in the same IO. If it finishes, a job will CB it on next IO */
		op_status = ThreadAIOFileWrite(file_mapped->thrdaio_base, &aio_req_pending, file_mapped->data.fd, data, data_sz, abs_offset,
				EvKQFileMappedAIOWriteRawDataFinishCB, file_mapped);
	}
	else
	{
		/* Write main aligned data - Finish CALLBACK will never be called in the same IO. If it finishes, a job will CB it on next IO */
		op_status = EvKQBaseAIOFileWrite(file_mapped->ev_base, &aio_req_pending, file_mapped->data.fd, data, data_sz, abs_offset,
				EvKQFileMappedAIOWriteRawDataFinishCB, file_mapped);
	}

	/* Failed writing to file */
	switch (op_status)
	{
	case AIOREQ_FAILED:
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed writing [%d] bytes of data at INDEX [%d] with ABSOLUTE_OFFSET [%ld]\n",
				file_mapped->data.fd, data_sz, block_idx, abs_offset);
		return 0;
	}
	/* Pending AIO_REQ, update AIO_STATE */
	case AIOREQ_FINISHED:
	case AIOREQ_PENDING:
	{
		/* Lock STORE_ENTRY while we WRITE RAW_DATA */
		EvKQFileMappedLock(file_mapped);

		/* No write state, bail out */
		if (!aio_state)
			break;

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_STATE [%p] - OP_STATUS [%d] - Pending AIO_REQ at [%d]\n",
				file_mapped->data.fd, aio_state, op_status, aio_req_pending.id);

		/* Save pending WRITE_REQ for this AIO_STATE */
		EvKQFileMappedAIOStatePendingAdd(aio_state, &aio_req_pending, AIOREQ_OPCODE_WRITE);
		break;
	}
	}

	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedPartialDataProcess(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state, char *data, long data_sz, long block_idx, long rel_offset)
{
	EvAIOReq aio_req_pending;
	MemBuffer *page_part_mb;
	int op_status;

	long abs_offset			= (block_idx * file_mapped->data.page_sz);
	long partial_mb_off 	= 0;
	long page_part_new_sz 	= 0;
	long page_part_mb_off 	= 0;
	long page_part_mb_sz	= 0;
	long block_exists		= DynBitMapBitTest(file_mapped->meta.bitmap, block_idx);

	/* Clear pending IO_REQ store */
	memset(&aio_req_pending, 0, sizeof(EvAIOReq));

	/* Block already complete, do not try to write */
	if (block_exists)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "BLOCK [%d] - Already COMPLETE\n", block_idx);
		return 1;
	}

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Processing partial data - BLOCK [%d] - DATA_SZ [%d] - REL_OFFSET [%d]\n",
			file_mapped->data.fd, block_idx, data_sz, rel_offset);

	/* Grab Partial by block index */
	page_part_mb = EvKQFileMappedPartialGetByIdx(file_mapped, block_idx);

	if (page_part_mb)
	{
		page_part_mb_sz 	= MemBufferGetSize(page_part_mb);
		page_part_mb_off	= (page_part_mb->user_long + page_part_mb_sz);
		page_part_new_sz	= (data_sz + rel_offset);

		/* This is a append */
		if (rel_offset < page_part_mb->user_long)
		{
			/* Return, this data not meet the block */
			if (page_part_new_sz < page_part_mb->user_long)
			{
				KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "BLOCK [%d] - Data do not FIT\n", block_idx);
				return 1;
			}
		}

		/* Off set must be equal the partial data off */
		if (rel_offset > page_part_mb_off)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "BLOCK [%d] - Relative offset is greater than PAGE_SIZE\n", block_idx);
			return 1;
		}

		/* This is a prepend */
		if (rel_offset > page_part_mb->user_long)
		{
			/* This is a overwrite, skip out, we already have this bytes */
			if (page_part_new_sz < page_part_mb_off)
			{
				KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "BLOCK [%d] - Do not OVERWRITE, already have these bytes\n", block_idx);
				return 1;
			}
		}

		/* Grab new offset */
		partial_mb_off = (rel_offset > page_part_mb->user_long ? page_part_mb->user_long : rel_offset);
	}
	else
	{
		/* Create a new buffer the size of a page, and stuff data into it */
		page_part_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, file_mapped->data.page_sz);

		/* Add trail MB to partial list */
		DLinkedListAdd(&file_mapped->data.partial_mb_list, &page_part_mb->node, page_part_mb);
		partial_mb_off = rel_offset;
	}

	/* Save block index and relative offset - NEGATIVE FOR TRAIL, POSITIVE FOR FINISH */
	page_part_mb->user_long	= partial_mb_off;
	page_part_mb->user_int	= block_idx;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Writing [%d] bytes on PAGE_PART with IDX [%d] at REL_OFFSET [%d]\n",
			file_mapped->data.fd, data_sz, block_idx, rel_offset);

	/* Stuff data into buffer of a page */
	MemBufferOffsetWrite(page_part_mb, rel_offset, data, data_sz);
	page_part_mb_sz = MemBufferGetSize(page_part_mb);

	/* Page has complete */
	if ((page_part_mb_sz - partial_mb_off) >= file_mapped->data.page_sz)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Writing [%d] bytes ALIGNED data at INDEX [%d] with ABSOLUTE_OFFSET [%ld]\n",
				file_mapped->data.fd, data_sz, block_idx, abs_offset);

		/* Fill it into main, remove MB from partial_list and destroy it */
		op_status = EvKQFileMappedAlignedWrite(file_mapped, aio_state, MemBufferDeref(page_part_mb), file_mapped->data.page_sz, block_idx);

		/* Failed writing to file */
		if (!op_status)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed writing [%d] bytes of data at INDEX [%d] with ABSOLUTE_OFFSET [%ld]\n",
					file_mapped->data.fd, data_sz, block_idx, abs_offset);
			return 0;
		}
	}
	/* Write NON aligned data */
	else
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Writing [%d] bytes of NON_ALIGNED data at INDEX [%d] with ABSOLUTE_OFFSET [%ld] and REL_OFFSET [%ld]\n",
				file_mapped->data.fd, data_sz, block_idx, abs_offset, rel_offset);

		/* We are running THREADED_AIO */
		if (file_mapped->flags.threaded_aio)
		{
			/* Schedule write to file */
			op_status = ThreadAIOFileWrite(file_mapped->thrdaio_base, &aio_req_pending, file_mapped->data.fd, data, data_sz,
					(abs_offset + rel_offset), EvKQFileMappedAIOWriteRawDataFinishCB, file_mapped);
		}
		else
		{
			/* Schedule write to file */
			op_status = EvKQBaseAIOFileWrite(file_mapped->ev_base, &aio_req_pending, file_mapped->data.fd, data, data_sz,
					(abs_offset + rel_offset), EvKQFileMappedAIOWriteRawDataFinishCB, file_mapped);
		}

		/* Failed writing to file */
		switch(op_status)
		{
		case AIOREQ_FAILED:
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed writing [%d] bytes of data at INDEX [%d] with ABSOLUTE_OFFSET [%ld]\n",
					file_mapped->data.fd, data_sz, block_idx, abs_offset);
			return 0;
		}
		/* Pending AIO_REQ, update AIO_STATE */
		case AIOREQ_FINISHED:
		case AIOREQ_PENDING:
		{
			/* Lock STORE_ENTRY while we WRITE RAW_DATA */
			EvKQFileMappedLock(file_mapped);

			/* No write state, bail out */
			if (!aio_state)
				break;

			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_STATE [%p] - OP_STATUS [%d] - Pending AIO_REQ at [%d]\n",
					file_mapped->data.fd, aio_state, op_status, aio_req_pending.id);

			/* Some hard assertions here */
			assert(aio_state->parent_map);
			assert(file_mapped == aio_state->parent_map);

			/* Save pending WRITE_REQ for this AIO_STATE */
			EvKQFileMappedAIOStatePendingAdd(aio_state, &aio_req_pending, AIOREQ_OPCODE_WRITE);
			break;
		}
		}
	}

	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedPartialDrop(EvKQFileMapped *file_mapped, MemBuffer *page_part_mb)
{
	/* Remove MB from List and destroy it */
	DLinkedListDelete(&file_mapped->data.partial_mb_list, &page_part_mb->node);
	MemBufferDestroy(page_part_mb);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static EvKQFileMappedAIOStateNotify *EvKQFileMappedAIOStateNotifyNew(EvKQFileMappedAIOState *aio_state)
{
	EvKQFileMappedAIOStateNotify *notify_node;

	notify_node				= calloc(1, sizeof(EvKQFileMappedAIOStateNotify));
	notify_node->aio_state	= aio_state;

	return notify_node;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStateNotifyDestroy(EvKQFileMappedAIOStateNotify *notify_node)
{
	/* Sanity check */
	if (!notify_node)
		return 0;

	free(notify_node);
	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStateNotifyDestroyAll(EvKQFileMappedAIOState *aio_state)
{
	EvKQFileMappedAIOStateNotify *notify_node;
	DLinkedListNode *node = aio_state->finish.list.head;

	/* Destroy all pending notification data */
	while (node)
	{
		if (!node)
			break;

		notify_node = node->data;
		node		= node->next;

		EvKQFileMappedAIOStateNotifyDestroy(notify_node);
		continue;
	}

	/* Reset FINISH list */
	DLinkedListReset(&aio_state->finish.list);

	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStateNotifyInvokeAll(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state)
{
	EvKQFileMappedAIOStateNotify *notify_node;
	DLinkedListNode *node = aio_state->finish.list.head;

	/* Destroy all pending notification data */
	while (node)
	{
		if (!node)
			break;

		notify_node = node->data;
		node		= node->next;

		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - AIO_STATE [%p] - AIO_OWNER [%d] - NOTIFY_OWNER [%d] - CANCELLED [%d]\n",
				file_mapped->data.fd, aio_state, aio_state->owner_id, notify_node->owner_id, notify_node->flags.cancelled);

		/* Skip canceled NOTIFY NODE */
		if (notify_node->flags.cancelled)
			continue;

		/* Invoke NOTIFY_CB and destroy NODE */
		assert(notify_node->cb_func);
		notify_node->cb_func(file_mapped, aio_state, notify_node->cb_data, notify_node->data_sz);
		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static EvKQFileMappedAIOState *EvKQFileMappedAIOStateNew(EvKQFileMapped *file_mapped)
{
	EvKQFileMappedAIOState *aio_state;
	int i;

	/* Create and enqueue to LINKED LIST */
	aio_state = calloc(1, sizeof(EvKQFileMappedAIOState));
	aio_state->parent_map = file_mapped;
	DLinkedListAdd(&file_mapped->data.aio_state_list, &aio_state->node, aio_state);

	/* Initialize pending request ID */
	for (i = 0; i < FILE_MAPPED_MAX_PENIDNG_AIO_PER_WRITE; i++)
		aio_state->pending_req[i].id = -1;

	return aio_state;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStateDestroy(EvKQFileMappedAIOState *aio_state)
{
	EvKQFileMapped *file_mapped;

	/* Sanity check */
	if (!aio_state)
		return 0;

	/* Make sure we belong to NO LIST */
	assert(!aio_state->node.next);
	assert(!aio_state->node.prev);

	/* Grab FM */
	file_mapped = aio_state->parent_map;
	aio_state->parent_map = NULL;

	/* Destroy and DETTACH read MB */
	MemBufferDestroy(aio_state->read.mb);
	aio_state->read.mb = NULL;

	/* Invoke destroy function */
	if (aio_state->destroy.cb_func)
		aio_state->destroy.cb_func(aio_state->destroy.cb_data);

	/* Destroy any pending attached notification */
	EvKQFileMappedAIOStateNotifyDestroyAll(aio_state);

	/* Free WILLY */
	free(aio_state);
	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStatePendingCancelAll(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state)
{
	int cancel_count;
	int i;

	/* Search for AIO_REQID inside this write state */
	for (cancel_count = 0, i = 0; i < aio_state->req_count_pending; i++)
	{
		/* Reseted ID, bail */
		if (aio_state->pending_req[i].id < 0)
			continue;

		/* Cancel AIO_REQ by ID considering AIO_BASE */
		if (file_mapped->flags.threaded_aio)
			ThreadAIOBaseCancelByReqID(file_mapped->thrdaio_base, aio_state->pending_req[i].id);
		else
			EvKQBaseAIOCancelByReqID(file_mapped->ev_base, aio_state->pending_req[i].id);

		/* Each AIO request should have a corresponding LOCK on this FILE_MAPPED, so unlock it */
		EvKQFileMappedUnlock(file_mapped);

		/* Mark as canceled */
		aio_state->pending_req[i].id = -1;
		continue;
	}

	return cancel_count;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStatePendingAdd(EvKQFileMappedAIOState *aio_state, EvAIOReq *aio_req, int aio_opcode)
{
	EvKQFileMappedAIOState *aio_state_lookup;

	EvKQFileMapped *file_mapped	= aio_state->parent_map;
	int aio_req_id				= aio_req->id;

	/* Search to find if we have this same AIO_REQ_ID around this FILE_MAP */
	aio_state_lookup = EvKQFileMappedAIOStateGetByAIOReqID(file_mapped, aio_req_id);

	/* This should REALLY not happen */
	if (aio_state_lookup)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "Duplicated AIO_ID [%d] FOUND - STATE_LOOKUP [%p] - STATE_CURRENT [%p]\n",
				aio_req_id, aio_state_lookup, aio_state);

		assert(0);
	}

	/* Save pending REQ for this AIO_STATE */
	aio_state->pending_req[aio_state->req_count_pending].id	= aio_req_id;
	aio_state->req_count_pending++;

	/* Touch global FILE_MAPPED pending writes */
	if (aio_opcode == AIOREQ_OPCODE_READ)
		file_mapped->stats.pending_raw_read++;
	else if (aio_opcode == AIOREQ_OPCODE_WRITE)
		file_mapped->stats.pending_raw_write++;

	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStateInvokeAndDestroy(EvKQFileMappedAIOState *aio_state)
{
	EvKQFileMapped *file_mapped;

	/* Sanity check */
	if (!aio_state)
		return 0;

	/* There should be no PENDING_REQ */
	assert(aio_state->req_count_finished == aio_state->req_count_pending);

	/* Grab parent and SYNC_BITMAP */
	file_mapped = aio_state->parent_map;

	/* Delete AIO_STATE from list before invoking CB_FUNC */
	DLinkedListDelete(&file_mapped->data.aio_state_list, &aio_state->node);

	/* Invoke finish_cb if this AIO_STATE has not been canceled */
	if ((!aio_state->flags.cancelled) && (aio_state->finish.cb_func))
	{
		assert(aio_state->finish.cb_func);
		aio_state->finish.cb_func(file_mapped, aio_state, aio_state->finish.cb_data, aio_state->finish.data_sz);
	}

	/* Invoke all enqueued FINISH events */
	EvKQFileMappedAIOStateNotifyInvokeAll(file_mapped, aio_state);

	/* Destroy state */
	EvKQFileMappedAIOStateDestroy(aio_state);
	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStateDestroyAll(EvKQFileMapped *file_mapped)
{
	EvKQFileMappedAIOState *aio_state;
	DLinkedListNode *node;

	/* Point to HEAD */
	node = file_mapped->data.aio_state_list.head;

	while (node)
	{
		/* No more NODEs, stop */
		if (!node)
			break;

		/* Grab AIO_STATE and point to NEXT node */
		aio_state	= node->data;
		node		= node->next;

		EvKQFileMappedAIOStateDestroy(aio_state);
		continue;
	}

	/* Reset partial data list */
	DLinkedListReset(&file_mapped->data.aio_state_list);
	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedAIOStateCheckFinish(EvKQFileMapped *file_mapped, EvAIOReq *aio_req, int error)
{
	EvKQFileMappedAIOState *aio_state;
	DLinkedListNode *node;
	int found;
	int i;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - CHECK FINISH - ID [%d] - [%s]\n",
			aio_req->fd, aio_req->id, glob_aioreqcode_str[aio_req->aio_opcode]);

	/* Walk all write states searching for this AIO_REQ_ID */
	for (found = 0, aio_state = NULL, node = file_mapped->data.aio_state_list.head; node; node = node->next)
	{
		aio_state = node->data;

		/* Search for AIO_REQID inside this write state */
		for (i = 0; i < aio_state->req_count_pending; i++)
		{
			/* Reseted ID, bail */
			if (aio_state->pending_req[i].id < 0)
				continue;

			/* Found it, mark finished */
			if (aio_state->pending_req[i].id == aio_req->id)
			{
				KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE,
						"FD [%d] - OWNER [%d] - INDEX [%d] - AIO_STATE PTR/OWNER [%d]-[%p / %ld] - ID [%d] - %s - Marked DONE - PEND [%d] - FINISHED [%d] - DELTA [%d]\n",
						aio_req->fd, aio_state->owner_id, i, aio_state->flags.cancelled, aio_state, aio_state->owner_id, aio_req->id, glob_aioreqcode_str[aio_req->aio_opcode],
						aio_state->req_count_pending, aio_state->req_count_finished, (aio_state->req_count_pending - aio_state->req_count_finished));

				/* Make sure this flag has not been marked as DONE */
				assert(!aio_state->pending_req[i].flags.done);

				/* AIO finished with error */
				if (error > 0)
				{
					/* Copy AIO_REQ error to pending request and set global flag to indicate at least one AIO req finished with error */
					aio_state->pending_req[i].error		= aio_req->err;
					aio_state->flags.error				= 1;

					/* Touch error statistics */
					aio_state->req_count_error++;
				}

				/* Mark as DONE */
				aio_state->pending_req[i].id			= -1;
				aio_state->pending_req[i].flags.done	= 1;

				/* Increment FINISHED count and mark FOUND */
				aio_state->req_count_finished++;
				found = 1;

				break;
			}
			continue;
		}

		/* Found, STOP */
		if (found)
			break;

		continue;
	}

	/* Make sure we found this AIO_ID and FINISHED count is SMALLER or EQUAL PENDING count */
	assert(found);
	assert(aio_state->req_count_finished <= aio_state->req_count_pending);

	/* Finished AIO_STATE, notify */
	if (aio_state->req_count_finished == aio_state->req_count_pending)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW,
				"FD [%d] - OWNER [%d] - AIO_REQ FD/ID [%d]-[%d] - [%d]-[%s] - Finished ALL PENDING AIO - AIO LIST/OWNER [%d]-[%ld / %ld] - FM_LOCK [%d] - LOADING [%d]\n",
				file_mapped->data.fd, aio_state->owner_id, aio_req->fd, aio_req->id, aio_req->aio_opcode, glob_aioreqcode_str[aio_req->aio_opcode], aio_state->flags.cancelled,
				file_mapped->data.aio_state_list.size, aio_state->owner_id, file_mapped->ref_count, file_mapped->flags.loading);

		/* NULLify LOADING_AIOSTATE in case we was loading */
		if (file_mapped->flags.loading)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - AIO_REQ FD/ID [%d]-[%d] - AIOSTATE OWNER/CANCEL [%ld / %d] - Finished loading meta\n",
					file_mapped->data.fd, aio_req->fd, aio_req->id, aio_state->owner_id, aio_state->flags.cancelled);

			/* If we are loading, this should be our AIO_STATE */
			assert(aio_state == file_mapped->load_aiostate);

			/* Clean up LOAD_AIOSTATE and flags */
			file_mapped->load_aiostate = NULL;
			file_mapped->flags.loading = 0;
		}

		/* Attach generic DATA_PTR to READ_MB of AIO_STATE in case we are READ_MB - Lock it, as it will be destroyed once AIO_REQ is destroyed */
		if (AIOREQ_OPCODE_READ_MB == aio_req->aio_opcode)
		{
			aio_state->read.mb	= (MemBuffer*)aio_req->data.ptr;
			MemBufferLock(aio_state->read.mb);
		}

		/* Invoke and destroy aio_state */
		EvKQFileMappedAIOStateInvokeAndDestroy(aio_state);
	}
	else
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - ID [%d] - %s - AIO_STATE not FINISHED - PEND/FINISH [%d / %d] - "
				"AIO_LIST [%ld] - FM_LOCK [%d]\n", aio_req->fd, aio_req->id, glob_aioreqcode_str[aio_req->aio_opcode],	aio_state->req_count_pending,
				aio_state->req_count_finished, file_mapped->data.aio_state_list.size, file_mapped->ref_count);


	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQFileMappedInternalClean(EvKQFileMapped *file_mapped)
{
	EvAIOReq aio_req_meta_aux;
	EvAIOReq aio_req_meta;
	EvAIOReq aio_req_data;
	int op_status_meta_aux;
	int op_status_meta;
	int op_status_data;

	/* Clean up all pending writes and partial data buffers */
	EvKQFileMappedAIOStateDestroyAll(file_mapped);
	EvKQFileMappedPartialClean(file_mapped);

	/* Destroy METADATA bitmap */
	DynBitMapDestroy(file_mapped->meta.bitmap);

	/* Destroy any pending META_MB */
	MemBufferDestroy(file_mapped->meta.data_aux_mb);
	MemBufferDestroy(file_mapped->meta.last_mb_aux);
	MemBufferDestroy(file_mapped->meta.last_mb_raw);

	/* There is THREADED_AIO being DONE, cancel AIO requests inside THREAD_POOL_AIO_QUEUE */
	if (file_mapped->flags.threaded_aio)
	{
		EvAIOReqQueueCancelAllByFD(&file_mapped->thrdaio_base->req_queue, file_mapped->meta.fd_aux);
		EvAIOReqQueueCancelAllByFD(&file_mapped->thrdaio_base->req_queue, file_mapped->meta.fd);
		EvAIOReqQueueCancelAllByFD(&file_mapped->thrdaio_base->req_queue, file_mapped->data.fd);
	}

	/* XXX TODO: (Make this ASYNC) - Close FDs - This will cancel EVENT_BASED AIO for these FDs */
	EvKQBaseAIOFileClose(file_mapped->ev_base, file_mapped->meta.fd_aux);
	EvKQBaseAIOFileClose(file_mapped->ev_base, file_mapped->meta.fd);
	EvKQBaseAIOFileClose(file_mapped->ev_base, file_mapped->data.fd);

	/* Upper layers want AIO_UNLINK of store files after destroying */
	if (file_mapped->flags.unlink_when_destroy)
	{
		/* Emit AIO_UNLINKs - PATH_AUX is optional */
		if (file_mapped->meta.path_aux_str)
			op_status_meta_aux	= ThreadAIOFileUnlink(file_mapped->thrdaio_base, &aio_req_meta_aux, file_mapped->meta.path_aux_str, NULL, NULL);

		op_status_meta		= ThreadAIOFileUnlink(file_mapped->thrdaio_base, &aio_req_meta, file_mapped->meta.path_str, NULL, NULL);
		op_status_data		= ThreadAIOFileUnlink(file_mapped->thrdaio_base, &aio_req_data, file_mapped->data.path_str, NULL, NULL);

		/* PATH_AUX is optional */
		if ((file_mapped->meta.path_aux_str) && (AIOREQ_FAILED == op_status_meta_aux))
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed deleting META_AUX [%s] with ERRNO [%d]\n",
					file_mapped->meta.path_aux_str, aio_req_meta_aux.err);

		if (AIOREQ_FAILED == op_status_meta)
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed deleting META [%s] with ERRNO [%d]\n",
					file_mapped->meta.path_str, aio_req_meta.err);

		if (AIOREQ_FAILED == op_status_data)
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed deleting DATA [%s] with ERRNO [%d]\n",
					file_mapped->data.path_str, aio_req_data.err);
	}

	/* Release PATH_DATA */
	if (file_mapped->meta.path_aux_str)
		free(file_mapped->meta.path_aux_str);

	free(file_mapped->meta.path_str);
	free(file_mapped->data.path_str);

	/* Running thread safe, destroy MUTEX */
	if (file_mapped->flags.thread_safe)
		MUTEX_DESTROY(file_mapped->mutex, "EVKQ_FILE_MAPPED");

	/* Reset data */
	file_mapped->mutex				= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	file_mapped->data.path_str		= NULL;
	file_mapped->meta.bitmap		= NULL;
	file_mapped->meta.path_str		= NULL;
	file_mapped->meta.path_aux_str	= NULL;
	file_mapped->meta.data_aux_mb	= NULL;
	file_mapped->meta.last_mb_aux	= NULL;
	file_mapped->meta.last_mb_raw	= NULL;

	/* Reset FDs */
	file_mapped->meta.fd_aux		= -1;
	file_mapped->meta.fd			= -1;
	file_mapped->data.fd			= -1;

	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedMetaDataUpdateAll(EvKQFileMapped *file_mapped)
{
	/* Begin updating RAW_META */
	EvKQFileMappedMetaDataRawUpdate(file_mapped);

	/* There is META_AUX open, AND there is AUX_METADATA to update */
	if ((file_mapped->flags.meta_aux_file) && (file_mapped->flags.meta_aux_need_update))
		EvKQFileMappedMetaDataAuxUpdate(file_mapped);

	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedMetaDataAuxUpdate(EvKQFileMapped *file_mapped)
{
	MemBuffer *aux_mb;
	int op_status;

	/* Need no update, STOP */
	if (!file_mapped->flags.meta_aux_need_update)
		return 0;

	assert(file_mapped->meta.data_aux_mb);
	assert(file_mapped->meta.fd_aux > 0);

	/* Grab AUX_MB */
	aux_mb							= file_mapped->meta.data_aux_mb;
	file_mapped->meta.data_aux_mb	= NULL;

	if (0 == MemBufferGetSize(aux_mb))
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_AUX_FD [%d] - Writing EMPTY AUX_META\n", file_mapped->meta.fd_aux);

	/* Emit the AIO_WRITE */
	op_status = ThreadAIOFileWriteFromMemBufferAndDestroy(file_mapped->thrdaio_base, NULL, aux_mb, file_mapped->meta.fd_aux, NULL, MemBufferGetSize(aux_mb), 0,
			EvKQFileMappedAIOWriteMetaAuxDataFinishCB, file_mapped);

	/* Failed AIO_WRITE */
	if (AIOREQ_FAILED == op_status)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_AUX_FD [%d] - Failed AIO_WRITE of AUX_META\n", file_mapped->meta.fd_aux);

		/* Put AUX_META back into file_mapped and leave */
		file_mapped->meta.data_aux_mb	= aux_mb;
		return 0;
	}
	else
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "META_AUX_FD [%d] - Begin AIO_WRITE of AUX_META - PEND [%d] - LOCK_COUNT [%d]\n",
				file_mapped->meta.fd_aux, file_mapped->flags.meta_aux_writing, file_mapped->ref_count);

		/* Lock STORE_ENTRY while we WRITE META */
		EvKQFileMappedLock(file_mapped);

		/* Update STATUS data and set flags as writing to avoid simultaneous thread-independent writes */
		file_mapped->stats.pending_meta_write++;
		file_mapped->flags.meta_aux_writing		= 1;
		file_mapped->flags.meta_aux_need_update = 0;

		return 1;
	}


	return 1;
}
/**************************************************************************************************************************/
static int EvKQFileMappedMetaDataRawUpdate(EvKQFileMapped *file_mapped)
{
	EvKQFileMappedMetaData file_mapped_meta;
	MetaData metadata;
	int op_status;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "META_FD [%d] - Packed META, will begin\n", file_mapped->meta.fd);

	MemBuffer *packed_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

	/* Clean up stack */
	memset(&file_mapped_meta, 0, sizeof(EvKQFileMappedMetaData));
	memset(&metadata, 0, sizeof(MetaData));

	/* Fill in DATA */
	file_mapped_meta.version		= 1;
	file_mapped_meta.cur_offset		= file_mapped->data.cur_offset;
	file_mapped_meta.page_sz		= file_mapped->data.page_sz;
	file_mapped_meta.flags.complete = file_mapped->flags.complete;

	/* Pack data into METADATA SERIALIZER and DUMP it into MEMBUFFER */
	MetaDataItemAdd(&metadata, FILEMAPPED_META_MAIN, 0, &file_mapped_meta, sizeof(EvKQFileMappedMetaData));
	DynBitMapMetaDataPack(file_mapped->meta.bitmap, &metadata, FILEMAPPED_META_DYN_BITMAP);
	MetaDataPack(&metadata, packed_mb);

	/* Clean UP METDATA - Data is already serialized on MB */
	MetaDataClean(&metadata);

	/* Already writing METADATA, enqueue it on UPDATE list */
	if (file_mapped->flags.meta_raw_writing)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "META_FD [%d] - Already writing META - PEND [%d] - LOCK_COUNT [%d] - SWAP!\n",
				file_mapped->meta.fd, file_mapped->flags.meta_raw_writing, file_mapped->ref_count);

		/* Destroy any pending previous METADATA buffer and ENQUEUE new one */
		MemBufferDestroy(file_mapped->meta.last_mb_raw);
		file_mapped->meta.last_mb_raw = packed_mb;
		return 1;
	}

	//EvKQBaseLoggerHexDump(file_mapped->log_base, LOGTYPE_INFO, MemBufferDeref(packed_mb), MemBufferGetSize(packed_mb), 4, 8);

	/* Emit the AIO_WRITE */
	op_status = ThreadAIOFileWriteFromMemBufferAndDestroy(file_mapped->thrdaio_base, NULL, packed_mb, file_mapped->meta.fd, NULL, MemBufferGetSize(packed_mb), 0,
			EvKQFileMappedAIOWriteMetaRawDataFinishCB, file_mapped);

	/* Failed AIO_WRITE */
	if (AIOREQ_FAILED == op_status)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_FD [%d] - Failed AIO_WRITE of META\n", file_mapped->meta.fd);
		MemBufferDestroy(packed_mb);
		return 0;
	}
	else
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "META_FD [%d] - Begin AIO_WRITE of META - PEND [%d] - LOCK_COUNT [%d]\n",
				file_mapped->meta.fd, file_mapped->flags.meta_raw_writing, file_mapped->ref_count);

		/* Lock STORE_ENTRY while we WRITE META */
		EvKQFileMappedLock(file_mapped);

		/* Update STATUS data and set flags as writing to avoid simultaneous thread-independent writes */
		file_mapped->stats.pending_meta_write++;
		file_mapped->flags.meta_raw_writing = 1;

		return 1;
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void EvKQFileMappedAIOWriteMetaAuxDataFinishCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	MemBuffer *meta_aux_mb;
	int op_status;

	EvKQFileMapped *file_mapped = cb_data;
	EvAIOReq *aio_req			= aio_req_ptr;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "META_AUX_FD [%d] - Finished AIO_WRITE of AUX_META - PEND_MB [%p]\n",
			file_mapped->meta.fd_aux, file_mapped->meta.last_mb_aux);

	/* Failed AIO_WRITE */
	if ((aio_req->flags.aio_failed) || (size <= 0))
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_AUX_FD [%d] - AIO_REQ [%d] - Failed writing AUX_META\n",
				file_mapped->meta.fd_aux, aio_req->id);
	}

	/* There are pending METADATA write requests, issue them */
	if (file_mapped->meta.last_mb_aux)
	{
		/* Swap pointers */
		meta_aux_mb						= file_mapped->meta.last_mb_aux;
		file_mapped->meta.last_mb_aux	= NULL;

		if (0 == MemBufferGetSize(meta_aux_mb))
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_AUX_FD [%d] - Writing EMPTY LAST AUX_META\n", file_mapped->meta.fd_aux);

		/* Emit the AIO_WRITE */
		op_status = ThreadAIOFileWriteFromMemBufferAndDestroy(file_mapped->thrdaio_base, NULL, meta_aux_mb, file_mapped->meta.fd_aux, NULL, MemBufferGetSize(meta_aux_mb), 0,
				EvKQFileMappedAIOWriteMetaAuxDataFinishCB, file_mapped);

		/* Failed AIO_WRITE */
		if (AIOREQ_FAILED == op_status)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_AUX_FD [%d] - Failed AIO_WRITE of META\n", file_mapped->meta.fd_aux);
			MemBufferDestroy(meta_aux_mb);
			return;
		}
		else
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "META_AUX_FD [%d] - Begin AIO_WRITE of PENDING_AUX_META\n", file_mapped->meta.fd_aux);
			return;
		}

		return;
	}

	/* Reset flags and TOUCH STATS */
	file_mapped->flags.meta_aux_writing = 0;
	file_mapped->stats.pending_meta_write--;

	/* Close has been invoked, try to close it now */
	if (file_mapped->flags.close_request)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "META_AUX_FD [%d] - CLOSE_REQ set, invoking CLOSE\n", file_mapped->meta.fd_aux);
		EvKQFileMappedCloseFiles(file_mapped);
	}

	/* Unlock STORE_ENTRY - After doing that, DO NOT TOUCH FILE_MAPPED, because it can already be destroyed */
	EvKQFileMappedUnlock(file_mapped);
	return;
}
/**************************************************************************************************************************/
static void EvKQFileMappedAIOWriteMetaRawDataFinishCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	MemBuffer *meta_mb;
	int op_status;

	EvKQFileMapped *file_mapped = cb_data;
	EvAIOReq *aio_req			= aio_req_ptr;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "META_FD [%d] - Finished AIO_WRITE of META - PEND_MB [%p]\n",
			file_mapped->meta.fd, file_mapped->meta.last_mb_raw);

	/* Failed AIO_WRITE */
	if ((aio_req->flags.aio_failed) || (size <= 0))
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_AUX_FD [%d] - AIO_REQ [%d] - Failed writing META_RAW_DATA\n",
				file_mapped->meta.fd_aux, aio_req->id);
	}

	/* There are pending METADATA write requests, issue them */
	if (file_mapped->meta.last_mb_raw)
	{
		/* Swap pointers */
		meta_mb							= file_mapped->meta.last_mb_raw;
		file_mapped->meta.last_mb_raw	= NULL;

		/* Emit the AIO_WRITE */
		op_status = ThreadAIOFileWriteFromMemBufferAndDestroy(file_mapped->thrdaio_base, NULL, meta_mb, file_mapped->meta.fd, NULL, MemBufferGetSize(meta_mb), 0,
				EvKQFileMappedAIOWriteMetaRawDataFinishCB, file_mapped);

		/* Failed AIO_WRITE */
		if (AIOREQ_FAILED == op_status)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_FD [%d] - Failed AIO_WRITE of META\n", file_mapped->meta.fd);
			MemBufferDestroy(meta_mb);
			return;
		}
		else
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "META_FD [%d] - Begin AIO_WRITE of PENDING_META\n", file_mapped->meta.fd);
			return;
		}

		return;
	}

	/* Reset flags and TOUCH STATS */
	file_mapped->flags.meta_raw_writing = 0;
	file_mapped->stats.pending_meta_write--;

	/* Close has been invoked, try to close it now */
	if (file_mapped->flags.close_request)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "META_FD [%d] - CLOSE_REQ set, invoking CLOSE\n", file_mapped->meta.fd);
		EvKQFileMappedCloseFiles(file_mapped);
	}

	/* Unlock STORE_ENTRY - After doing that, DO NOT TOUCH FILE_MAPPED, because it can already be destroyed */
	EvKQFileMappedUnlock(file_mapped);
	return;
}
/**************************************************************************************************************************/
static void EvKQFileMappedAIOWriteRawDataFinishCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	MemBuffer *page_part_mb;
	long block_idx_lower;
	long block_idx_higher;
	long block_count_total;
	long block_size_trail;
	long block_size_finish;
	long block_off_relative;
	int i;

	EvKQFileMapped *file_mapped = cb_data;
	EvAIOReq *aio_req			= aio_req_ptr;

	/* Touch statistics */
	file_mapped->stats.pending_raw_write--;

	/* Write failed, bail out */
	if ((aio_req->flags.aio_failed) || (size <= 0))
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - ID [%d] - %s FAILED  with size [%d]\n",
				fd, aio_req->id, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ) ? "AIO_READ" : "AIO_WRITE"), size);

		/* Check AIO_STATE FINISH with ERROR_FLAG = 1 */
		EvKQFileMappedAIOStateCheckFinish(file_mapped, aio_req, 1);
		EvKQFileMappedUnlock(file_mapped);
		return;
	}

	/* Calculate LOWER block INDEX, TOTAL block count and how many bytes are incomplete in trail data */
	block_idx_lower		= (aio_req->data.offset / file_mapped->data.page_sz);
	block_idx_higher	= ((aio_req->data.offset + aio_req->data.size) / file_mapped->data.page_sz);
	block_count_total	= (aio_req->data.size / file_mapped->data.page_sz);
	block_off_relative	= (aio_req->data.offset % file_mapped->data.page_sz);

	/* Calculate MISALIGNED data on the TAIL and HEAD of BUFFER */
	block_size_trail	= ((block_off_relative > 0) ? (file_mapped->data.page_sz - block_off_relative) : 0);
	block_size_trail	= ((block_size_trail > aio_req->data.size) ? aio_req->data.size : block_size_trail);
	block_size_finish	= ((aio_req->data.size - block_size_trail) % file_mapped->data.page_sz);

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN,
			"FD [%d] - ID [%d] - %s OK - SIZE_OFF [%ld / %ld] - BLOCK_BEGIN_FINISH_COUNT [%d / %d / %d] - PENDING_WRITE_READ [%d / %d] - AIO_LIST [%ld]\n",
			fd, aio_req->id, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ) ? "AIO_READ" : "AIO_WRITE"), aio_req->data.size, aio_req->data.offset, block_idx_lower,
			block_idx_higher, block_count_total, file_mapped->stats.pending_raw_write, file_mapped->stats.pending_raw_read, file_mapped->data.aio_state_list.size);

	/* Update bitmap complete written blocks */
	for (i = block_idx_lower; ((i < block_idx_higher) && (block_count_total > 0)); block_count_total--, i++)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Updating BITMAP_ID [%d]\n", fd, i);

		/* Set BITMAP for BLOCK_ID */
		DynBitMapBitSet(file_mapped->meta.bitmap, i);

		/* Try to find partial for this INDEX */
		page_part_mb = EvKQFileMappedPartialGetByIdx(file_mapped, i);

		/* Partial found, DROP it */
		if (page_part_mb)
			EvKQFileMappedPartialDrop(file_mapped, page_part_mb);

		continue;
	}

	/* Check AIO_STATE FINISH with ERROR_FLAG = 0 */
	EvKQFileMappedAIOStateCheckFinish(file_mapped, aio_req, 0);

	/* No more pending WRITEs - SYNC META */
	if (0 == file_mapped->stats.pending_raw_write)
	{
		/* Complete has been invoked and there were some pending WRITEs, try to complete it now */
		if (file_mapped->flags.wants_complete)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SIZE [%d] - Auto invoking COMPLETE because WANTS_COMPLETE is SET\n", fd, size);

			/* Invoke complete and reset flag - Complete will SYNC META */
			EvKQFileMappedCompleted(file_mapped);
			file_mapped->flags.wants_complete = 0;
			goto leave;
		}

		/* SYNC META - This will close files if request is SET */
		EvKQFileMappedMetaDataUpdateAll(file_mapped);
	}

	/* Unlock and leave */
	leave:
	EvKQFileMappedUnlock(file_mapped);
	return;
}
/**************************************************************************************************************************/
static void EvKQFileMappedAIOReadMetaAuxDataFinishCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	EvKQFileMapped *file_mapped = cb_data;
	EvAIOReq *aio_req			= aio_req_ptr;
	MemBuffer *read_mb			= (MemBuffer*)aio_req->data.ptr;
	char *read_data_ptr			= MemBufferDeref(read_mb);
	int read_data_sz			= MemBufferGetSize(read_mb);
	int aio_ret_code			= aio_req->ret;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "META_AUX_FD [%d] - Finished AIO_READ of AUX_META - [%d]-[%s]\n",
			file_mapped->meta.fd_aux, read_data_sz, read_data_ptr);

	assert(file_mapped->err_code == FILEMAPPED_ERROR_NONE);
	assert(file_mapped->flags.loading == 1);
	assert(file_mapped->meta.fd_aux > 0);

	/* Touch statistics */
	file_mapped->stats.pending_meta_read--;

	/* Read failed, bail out */
	if ((aio_req->flags.aio_failed) || (size <= 0))
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "DATA_FD [%d] - ID [%d] - AIO_READ FAILED  with size [%d]\n", fd, aio_req->id, size);

		/* Check AIO_STATE FINISH with ERROR_FLAG = 1 */
		EvKQFileMappedAIOStateCheckFinish(file_mapped, aio_req, 1);
		EvKQFileMappedUnlock(file_mapped);
		return;
	}

	/* Lock BUFFER and attach AUX_MB to FILE_MAPPED - AIO_DESTROY will unlock MB */
	MemBufferLock(read_mb);
	file_mapped->meta.data_aux_mb	= read_mb;

	/* Check AIO_STATE FINISH with ERROR_FLAG = 0 */
	EvKQFileMappedAIOStateCheckFinish(file_mapped, aio_req, 0);
	EvKQFileMappedUnlock(file_mapped);
	return;
}
/**************************************************************************************************************************/
static void EvKQFileMappedAIOReadMetaDataFinishCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	MetaData metadata;
	int op_status;

	EvKQFileMappedMetaData *file_mapped_meta	= NULL;
	EvKQFileMapped *file_mapped					= cb_data;
	EvAIOReq *aio_req							= aio_req_ptr;
	MemBuffer *read_mb							= (MemBuffer*)aio_req->data.ptr;
	char *read_data_ptr							= MemBufferDeref(read_mb);
	int read_data_sz							= MemBufferGetSize(read_mb);
	int aio_ret_code							= aio_req->ret;

	KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "META_FD [%d] - Finished AIO_READ of MAIN_META - [%d]-[%s]\n",
			file_mapped->meta.fd, read_data_sz, read_data_ptr);

	assert(file_mapped->err_code == FILEMAPPED_ERROR_NONE);
	assert(file_mapped->flags.loading == 1);
	assert(file_mapped->meta.fd > 0);

	/* Touch statistics */
	file_mapped->stats.pending_meta_read--;

	/* Read failed, bail out */
	if ((aio_req->flags.aio_failed) || (size <= 0))
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "DATA_FD [%d] - ID [%d] - %s FAILED  with size [%d]\n",
				fd, aio_req->id, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ) ? "AIO_READ" : "AIO_WRITE"), size);

		/* Check AIO_STATE FINISH with ERROR_FLAG = 1 */
		EvKQFileMappedAIOStateCheckFinish(file_mapped, aio_req, 1);
		EvKQFileMappedUnlock(file_mapped);
		return;
	}

	/* Clean up stack and unpack METADATA */
	memset(&metadata, 0, sizeof(MetaData));
	op_status = MetaDataUnpack(&metadata, read_mb, NULL);

	/* Failed unpacking META */
	if (METADATA_UNPACK_SUCCESS != op_status)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_FD [%d] - Failed unpacking META with ERROR [%d]\n", file_mapped->meta.fd, op_status);
	}
	else
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "META_FD [%d] - Meta unpack OK\n", file_mapped->meta.fd);
	}

	/* Search for MAIN_META inside PACKED_METADATA */
	file_mapped_meta			= MetaDataItemFindByID(&metadata, FILEMAPPED_META_MAIN, 0);

	/* Invalid MAIN_META or not present */
	if (!file_mapped_meta)
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "META_FD [%d] - Failed unpacking MAIN META with ERROR [%d]\n", file_mapped->meta.fd);
	}
	else
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "META_FD [%d] - Meta unpack OK - VERSION [%d] - COMPLETE [%d]\n",
				file_mapped->meta.fd, file_mapped_meta->version, file_mapped_meta->flags.complete);

		/* Populate back FILE_MAPPED data */
		file_mapped->data.page_sz		= file_mapped_meta->page_sz;
		file_mapped->data.cur_offset	= file_mapped_meta->cur_offset;
		file_mapped->flags.complete		= file_mapped_meta->flags.complete;
	}

	/* Load BITMAP back from METADATA */
	file_mapped->meta.bitmap	= DynBitMapMetaDataUnPackFromMeta(&metadata);

	/* Clean UP METDATA and META_MB */
	MetaDataClean(&metadata);

	/* Check AIO_STATE FINISH with ERROR_FLAG = 0 */
	EvKQFileMappedAIOStateCheckFinish(file_mapped, aio_req, 0);
	EvKQFileMappedUnlock(file_mapped);
	return;
}
/**************************************************************************************************************************/
static void EvKQFileMappedAIOReadRawDataFinishCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	EvKQFileMapped *file_mapped = cb_data;
	EvAIOReq *aio_req			= aio_req_ptr;

	/* Touch statistics */
	file_mapped->stats.pending_raw_read--;

	/* Read failed, bail out */
	if ((aio_req->flags.aio_failed) || (size <= 0))
	{
		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "DATA_FD [%d] - ID [%d] - AIO_READ FAILED  with size [%d]\n", fd, aio_req->id, size);

		/* Check AIO_STATE FINISH with ERROR_FLAG = 1 */
		EvKQFileMappedAIOStateCheckFinish(file_mapped, aio_req, 1);

		/* Unlock after reading */
		EvKQFileMappedUnlock(file_mapped);
		return;
	}

	/* Check AIO_STATE FINISH with ERROR_FLAG = 0 */
	EvKQFileMappedAIOStateCheckFinish(file_mapped, aio_req, 0);

	/* Unlock after reading */
	EvKQFileMappedUnlock(file_mapped);
	return;
}
/**************************************************************************************************************************/
static void EvKQFileMappedAIOGenericUnlockCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	EvKQFileMapped *file_mapped = cb_data;
	EvAIOReq *aio_req			= aio_req_ptr;

	EvKQFileMappedUnlock(file_mapped);
	return;
}
/**************************************************************************************************************************/
static void EvKQFileMappedAIO_MemBufferDestroyFunc(void *mb_ptr)
{
	MemBuffer *mb = mb_ptr;
	assert(mb);

	MemBufferDestroy(mb);
	return;
}
/**************************************************************************************************************************/
