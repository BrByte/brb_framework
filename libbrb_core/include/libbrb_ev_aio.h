/*
 * libbrb_ev_aio.h
 *
 *  Created on: 2014-09-12
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2014 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#ifndef LIBBRB_EV_AIO_H_
#define LIBBRB_EV_AIO_H_

#define AIOREQ_QUEUE_MUTEX_INIT(aioreq_queue) 				if (aioreq_queue->flags.mt_engine && !aioreq_queue->flags.mutex_init) { \
		MUTEX_INIT (aioreq_queue->mutex, "AIOREQ_QUEUE_MUTEX") aioreq_queue->flags.mutex_init = 1; }
#define AIOREQ_QUEUE_MUTEX_DESTROY(aioreq_queue)			if (aioreq_queue->flags.mt_engine && aioreq_queue->flags.mutex_init) { \
		MUTEX_DESTROY (aioreq_queue->mutex, "AIOREQ_QUEUE_MUTEX") aioreq_queue->flags.mutex_init = 0; }

#define AIOREQ_QUEUE_MUTEX_LOCK(aioreq_queue)				if (aioreq_queue->flags.mt_engine) MUTEX_LOCK (aioreq_queue->mutex, "AIOREQ_QUEUE_MUTEX")
#define AIOREQ_QUEUE_MUTEX_TRYLOCK(aioreq_queue, state)		if (aioreq_queue->flags.mt_engine) 	MUTEX_TRYLOCK (aioreq_queue->mutex, "AIOREQ_QUEUE_MUTEX", state)
#define AIOREQ_QUEUE_MUTEX_UNLOCK(aioreq_queue) 			if (aioreq_queue->flags.mt_engine) MUTEX_UNLOCK (aioreq_queue->mutex, "AIOREQ_QUEUE_MUTEX")

typedef enum
{
	AIOREQ_QUEUE_MT_UNSAFE,
	AIOREQ_QUEUE_MT_SAFE,
} EvAIOReqQueueMTState;

typedef enum
{
	AIOREQ_QUEUE_SIMPLE,
	AIOREQ_QUEUE_SLOTTED,
	AIOREQ_QUEUE_LASTITEM
} EvAIOReqQueueType;

typedef enum
{
	AIOREQ_UNKNWON,
	AIOREQ_FAILED,
	AIOREQ_PENDING,
	AIOREQ_CANCELED,
	AIOREQ_FINISHED,
	AIOREQ_LASTITEM,
} EvAIOReqStatusCode;

typedef enum
{
	AIOREQ_OPCODE_NONE,
	AIOREQ_OPCODE_OPEN,
	AIOREQ_OPCODE_READ,
	AIOREQ_OPCODE_READ_MB,
	AIOREQ_OPCODE_WRITE,
	AIOREQ_OPCODE_WRITE_MB,
	AIOREQ_OPCODE_CLOSE,
	AIOREQ_OPCODE_UNLINK,
	AIOREQ_OPCODE_TRUNCATE,
	AIOREQ_OPCODE_OPENDIR,
	AIOREQ_OPCODE_STAT,
	AIOREQ_OPCODE_MOUNT,
	AIOREQ_OPCODE_UNMOUNT,
	AIOREQ_OPCODE_LASTITEM,
} EvAIOReqCode;

typedef enum
{
	CRYPTO_OPERATION_READ,
	CRYPTO_OPERATION_WRITE,
	CRYPTO_OPERATION_LASTITEM,
} EvAIOTransformCryptoOpCode;

typedef void EvAIOReqCBH(int, int, int, void*, void*);
typedef void EvAIOReqDestroyFunc(void*);

static char *glob_aioreqcode_str[] = {"AIOREQ_OPCODE_NONE", "AIOREQ_OPCODE_OPEN", "AIOREQ_OPCODE_READ", "AIOREQ_OPCODE_READ_MB",
		"AIOREQ_OPCODE_WRITE", "AIOREQ_OPCODE_WRITE_MB", "AIOREQ_OPCODE_CLOSE", "AIOREQ_OPCODE_UNLINK", "AIOREQ_OPCODE_TRUNCATE", "AIOREQ_OPCODE_OPENDIR",
		"AIOREQ_OPCODE_STAT", "AIOREQ_OPCODE_MOUNT", "AIOREQ_OPCODE_UNMOUNT","AIOREQ_OPCODE_LASTITEM", NULL};

/******************************************************************************************************/
typedef struct _EvAIOReq
{
	DLinkedListNode node;
	EvAIOReqDestroyFunc *destroy_func;
	EvAIOReqCBH *finish_cb;
	MemBuffer *transformed_mb;
	struct _EvAIOReqQueue *parent_queue;
	struct aiocb aiocb;

	void *finish_cbdata;
	void *destroy_cbdata;
	void *parent_ptr;

	void *user_data;
	int user_int;
	long user_long;

	long ioloop_create;
	long ioloop_queue;

	int retry_count;
	int lock_count;
	int aio_opcode;

	long ret;
	int err;
	int fd;
	int id;

	struct
	{
		struct timeval enqueue;
		struct timeval begin;
		struct timeval finish;
	} tv;

	struct
	{
		struct stat statbuf;
		mode_t mode;
		char *path_str;
		char *dev_str;
		int flags;
	} file;

	struct
	{
		char *ptr;
		long size;
		long offset;
	} data;

	struct
	{
		unsigned int in_use:1;
		unsigned int aio_read:1;
		unsigned int aio_write:1;
		unsigned int aio_socket:1;
		unsigned int aio_file:1;
		unsigned int aio_serial:1;
		unsigned int aio_threaded:1;
		unsigned int aio_failed:1;
		unsigned int aio_delayed_notify:1;
		unsigned int dup_data:1;
		unsigned int dup_path_str;
		unsigned int dup_dev_str;
		unsigned int transformed:1;
		unsigned int cancelled:1;
		unsigned int destroyed:1;
	} flags;

} EvAIOReq;
/*****************************************************/
typedef struct _EvAIOReqQueue
{
	DLinkedList aio_req_list;
	MemSlotBase memslot;
	struct _EvKQBase *ev_base;
	struct _EvKQBaseLogBase *log_base;
	pthread_mutex_t mutex;
	int max_slots;

	struct
	{
		unsigned long queue_sz;
		unsigned long total_sz;
	} stats;

	struct
	{
		unsigned int mt_engine:1;
		unsigned int mutex_init:1;
		unsigned int queue_init:1;
		unsigned int queue_slotted:1;
		unsigned int cancelled:1;
	} flags;

} EvAIOReqQueue;
/******************************************************************************************************/
typedef struct _EvAIOReqIOVectorData
{
	char *data_ptr;
	long offset;
	long size;

} EvAIOReqIOVectorData;
/******************************************************************************************************/
typedef struct _EvAIOReqBase
{
	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;


	int a;
} EvAIOReqBase;

/******************************************************************************************************/

/* ev_kq_aio_req.c */
EvAIOReqQueue *EvAIOReqQueueNew(struct _EvKQBase *ev_base, int max_slots, int queue_mt, int queue_type);
void EvAIOReqQueueInit(struct _EvKQBase *ev_base, EvAIOReqQueue *aio_req_queue, int max_slots, int queue_mt, int queue_type);
void EvAIOReqQueueDestroy(EvAIOReqQueue *aio_req_queue);
void EvAIOReqQueueClean(EvAIOReqQueue *aio_req_queue);
long EvAIOReqQueueGetQueueSize(EvAIOReqQueue *aio_req_queue);
long EvAIOReqQueueGetQueueCount(EvAIOReqQueue *aio_req_queue);
void EvAIOReqQueueEnqueueHead(EvAIOReqQueue *aio_req_queue, EvAIOReq *aio_req);
void EvAIOReqQueueEnqueue(EvAIOReqQueue *aio_req_queue, EvAIOReq *aio_req);
int EvAIOReqQueueIsEmpty(EvAIOReqQueue *aio_req_queue);
int EvAIOReqQueueCancelAllByFD(EvAIOReqQueue *aio_req_queue, int target_fd);
EvAIOReq * EvAIOReqQueueGrabByID(EvAIOReqQueue *aio_req_queue, int aioreq_id);
void EvAIOReqQueueRemoveItem(EvAIOReqQueue *aio_req_queue, EvAIOReq *aio_req);
EvAIOReq *EvAIOReqQueuePointToHead(EvAIOReqQueue *aio_req_queue);
EvAIOReq * EvAIOReqQueueDequeue(EvAIOReqQueue *aio_req_queue);
EvAIOReq *EvAIOReqNew(EvAIOReqQueue *aio_req_queue, int fd, void *parent_ptr, void *data, long data_sz, long offset, EvAIOReqDestroyFunc *destroy_func,
		EvAIOReqCBH *finish_cb, void *finish_cbdata);
long EvAIOReqGetMissingSize(EvAIOReq *aio_req);
char *EvAIOReqGetDataPtr(EvAIOReq *aio_req);
void EvAIOReqInvokeCallBacksAndDestroy(EvAIOReq *aio_req, int delay, int fd, int size, int thrd_id, void *base_ptr);
void EvAIOReqInvokeCallBacks(EvAIOReq *aio_req, int delay, int fd, int size, int thrd_id, void *base_ptr);
int EvAIOReqDestroy(EvAIOReq *aio_req);
int EvAIOReqTransform_WriteData(void *transform_info_ptr, EvAIOReqQueue *aio_req_queue, EvAIOReq *aio_req);
MemBuffer *EvAIOReqTransform_ReadData(void *transform_info_ptr, char *in_data_ptr, long data_sz);
int EvAIOReqTransform_CryptoEnable(void *transform_info_ptr, int algo_code, char *key_ptr, int key_sz);
int EvAIOReqTransform_CryptoDisable(void *transform_info_ptr);
char *EvAIOReqTransform_RC4_MD5_DataHashString(MemBuffer *transformed_mb);
int EvAIOReqTransform_RC4_MD5_DataValidate(MemBuffer *transformed_mb);

/* ev_kq_aio_file.c */
int EvKQBaseAIOFileOpen(struct _EvKQBase *kq_base, char *path, int flags, int mode);
int EvKQBaseAIOFileClose(struct _EvKQBase *kq_base, int file_fd);
int EvKQBaseAIOFileWrite(struct _EvKQBase *kq_base, EvAIOReq *dst_aio_req, int file_fd, char *src_buf, long size, long offset, EvAIOReqCBH *finish_cb, void *cb_data);
int EvKQBaseAIOFileRead(struct _EvKQBase *kq_base, EvAIOReq *dst_aio_req, int file_fd, char *dst_buf, long size, long offset, EvAIOReqCBH *finish_cb, void *cb_data);
int EvKQBaseAIOCancelByReqID(struct _EvKQBase *kq_base, int req_id);
int EvKQBaseAIOFileGeneric_FinishCheck(struct _EvKQBase *kq_base, EvAIOReq *aio_req);

#endif /* LIBBRB_EV_AIO_H_ */
