/*
 * libbrb_ev_utils.h
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
 */

#ifndef LIBBRB_EV_UTILS_H_
#define LIBBRB_EV_UTILS_H_

#define FILE_MAPPED_MAX_PATH_STR_SZ				64
#define FILE_MAPPED_MAX_PENIDNG_AIO_PER_WRITE	16

typedef int EvKQFileMappedFinishCB(void*, void *, void*, long); /* FILEMAPPED, WRITE_STATE, CBDATA, IO_SIZE */

typedef enum
{
	FILEMAPPED_ERROR_NONE,
	FILEMAPPED_ERROR_OPENING_METADATA,
	FILEMAPPED_ERROR_OPENING_AUX_METADATA,
	FILEMAPPED_ERROR_OPENING_RAWDATA,
	FILEMAPPED_ERROR_OTHER,
	FILEMAPPED_ERROR_LASTITEM,
} EvKQFileMappedErrorCodes;

typedef enum
{
	FILEMAPPED_AIO_NONE,
	FILEMAPPED_AIO_ERROR,
	FILEMAPPED_AIO_PENDING,
	FILEMAPPED_AIO_FINISHED,
	FILEMAPPED_AIO_LASTITEM
} EvKQFileMappedIOCodes;

typedef enum
{
	FILEMAPPED_META_NONE,
	FILEMAPPED_META_MAIN,
	FILEMAPPED_META_DYN_BITMAP,
	FILEMAPPED_META
} EvKQFileMappedMetaCodes;

typedef struct _EvKQFileMappedValidBytes
{
	long first_byte;
	long last_byte;
} EvKQFileMappedValidBytes;

typedef struct _EvKQFileMappedMetaData
{
	int version;
	long cur_offset;
	long page_sz;

	struct
	{
		unsigned int complete:1;
	} flags;
} EvKQFileMappedMetaData;

typedef struct _EvKQFileMappedConf
{
	EvKQBaseLogBase *log_base;
	ThreadAIOBase *thrdaio_base;
	char *path_meta_aux_str;
	char *path_meta_str;
	char *path_data_str;
	int page_sz;

	struct
	{
		unsigned int threaded_aio:1;
		unsigned int truncate_file:1;
	} flags;

} EvKQFileMappedConf;

typedef struct _EvKQFileMappedAIOStateNotify
{
	DLinkedListNode node;
	EvKQFileMappedFinishCB *cb_func;
	struct _EvKQFileMappedAIOState *aio_state;
	long owner_id;
	void *cb_data;
	long data_sz;

	struct
	{
		unsigned int cancelled:1;
	} flags;
} EvKQFileMappedAIOStateNotify;

typedef struct _EvKQFileMappedAIOState
{
	DLinkedListNode node;
	struct _EvKQFileMapped *parent_map;
	long owner_id;
	int req_count_pending;
	int req_count_finished;
	int req_count_error;

	struct
	{
		int error;
		int id;

		struct
		{
			unsigned int done:1;
		} flags;
	} pending_req[FILE_MAPPED_MAX_PENIDNG_AIO_PER_WRITE];

	struct
	{
		MemBuffer *mb;
		char *data_ptr;
		long data_sz;
	} read;

	struct
	{
		EvKQFileMappedFinishCB *cb_func;
		DLinkedList list;
		void *cb_data;
		long data_sz;
	} finish;

	struct
	{
		EvAIOReqDestroyFunc *cb_func;
		void *cb_data;
	} destroy;

	struct
	{
		unsigned int error:1;
		unsigned int cancelled:1;
	} flags;

} EvKQFileMappedAIOState;

typedef struct _EvKQFileMapped
{
	EvKQBase *ev_base;
	ThreadAIOBase *thrdaio_base;
	EvKQBaseLogBase *log_base;
	EvKQFileMappedAIOState *load_aiostate;

	pthread_mutex_t mutex;
	int ref_count;
	int err_code;

	long user_long;
	int user_int;
	void *user_data;

	struct
	{
		DLinkedList partial_mb_list;
		DLinkedList aio_state_list;
		char *path_str;
		long cur_offset;
		int page_sz;
		int fd;
	} data;

	struct
	{
		MemBuffer *last_mb_raw;
		MemBuffer *last_mb_aux;
		MemBuffer *data_aux_mb;
		DynBitMap *bitmap;
		char *path_aux_str;
		char *path_str;
		int fd;
		int fd_aux;
	} meta;

	struct
	{
		long pending_meta_read;
		long pending_meta_write;
		long pending_raw_read;
		long pending_raw_write;
	} stats;

	struct
	{
		unsigned int thread_safe:1;
		unsigned int threaded_aio:1;
		unsigned int truncate_file:1;
		unsigned int created_with_new:1;
		unsigned int pct_string_update:1;
		unsigned int wants_complete:1;
		unsigned int complete:1;
		unsigned int opened:1;
		unsigned int destroyed:1;
		unsigned int loading:1;
		unsigned int close_request:1;
		unsigned int meta_raw_writing:1;
		unsigned int meta_aux_writing:1;
		unsigned int meta_aux_file:1;
		unsigned int meta_aux_need_update:1;
		unsigned int unlink_when_destroy:1;
	} flags;

} EvKQFileMapped;


/* ev_kq_filemapped */
EvKQFileMapped *EvKQFileMappedNew(EvKQBase *ev_base, EvKQFileMappedConf *conf);
int EvKQFileMappedInit(EvKQBase *ev_base, EvKQFileMapped *file_mapped, EvKQFileMappedConf *conf);
int EvKQFileMappedClean(EvKQFileMapped *file_mapped);
int EvKQFileMappedDestroy(EvKQFileMapped *file_mapped);
int EvKQFileMappedLock(EvKQFileMapped *file_mapped);
int EvKQFileMappedUnlock(EvKQFileMapped *file_mapped);
int EvKQFileMappedOpen(EvKQFileMapped *file_mapped);
int EvKQFileMappedClose(EvKQFileMapped *file_mapped);
int EvKQFileMappedCompleted(EvKQFileMapped *file_mapped);
long EvKQFileMappedGetSize(EvKQFileMapped *file_mapped);
int EvKQFileMappedMetaAuxWrite(EvKQFileMapped *file_mapped, MemBuffer *aux_mb);
int EvKQFileMappedAIOStateNotifyFinishAdd(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long finish_size, long owner_id);
int EvKQFileMappedAIOStateNotifyFinishCancelByOwner(EvKQFileMapped *file_mapped, EvKQFileMappedAIOState *aio_state, long owner_id);


EvKQFileMapped *EvKQFileMappedAIOLoad(EvKQBase *ev_base, EvKQFileMappedConf *conf, EvKQFileMappedFinishCB *finish_cb, void *finish_cbdata, long aiostate_owner_id);
int EvKQFileMappedAIOReadToMemBuffer(EvKQFileMapped *file_mapped, long offset, long data_sz, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long aiostate_owner_id);
int EvKQFileMappedAIORead(EvKQFileMapped *file_mapped, char *data, long offset, long data_sz, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long aiostate_owner_id);
int EvKQFileMappedAIOAdd(EvKQFileMapped *file_mapped, char *data, long data_sz, EvKQFileMappedFinishCB *finish_cb, void *finish_cbdata, long aiostate_owner_id);
int EvKQFileMappedAIOWriteMemBufferAndDestroy(EvKQFileMapped *file_mapped, MemBuffer *data_mb, long offset, EvKQFileMappedFinishCB *finish_cb,
		void *finish_cbdata, long aiostate_owner_id);
int EvKQFileMappedAIOWrite(EvKQFileMapped *file_mapped, char *data, long offset, long data_sz, EvKQFileMappedFinishCB *finish_cb, void *finish_cbdata, long aiostate_owner_id);
MemBuffer *EvKQFileMappedPartialGetByIdx(EvKQFileMapped *file_mapped, long block_idx);
int EvKQFileMappedPartialClean(EvKQFileMapped *file_mapped);
long EvKQFileMappedPartialGetSize(EvKQFileMapped *file_mapped);
int EvKQFileMappedCheckBytes(EvKQFileMapped *file_mapped, long base, long offset);
int EvKQFileMappedGetValidBytes(EvKQFileMapped *file_mapped, long base, long offset, EvKQFileMappedValidBytes *valid_bytes);
int EvKQFileMappedCheckLastBlock(EvKQFileMapped *file_mapped, long total_sz);
int EvKQFileMappedGenerateStringPCT(EvKQFileMapped *file_mapped, char *ret_mask, int ret_mask_size, long total_sz);

EvKQFileMappedAIOState *EvKQFileMappedAIOStateGetByAIOReqID(EvKQFileMapped *file_mapped, int aioreq_id);
int EvKQFileMappedAIOStateSetDelayedNotify(EvKQFileMappedAIOState *aio_state);
int EvKQFileMappedAIOStateCancelByAIOReqID(EvKQFileMapped *file_mapped, int aioreq_id);
int EvKQFileMappedAIOStateCancelAllByOwner(EvKQFileMapped *file_mapped, long owner_id);
int EvKQFileMappedAIOStateDumpAll(EvKQFileMapped *file_mapped);

/* ev_kq_daemon.c */
void EvKQBaseDaemonForkAndReload(EvKQBase *kq_base, char *path_str, char *human_str);
void EvKQBaseDaemonForkAndDetach(EvKQBase *kq_base);


#endif /* LIBBRB_EV_UTILS_H_ */
