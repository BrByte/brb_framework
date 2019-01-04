/*
 * ev_kq_aio_file.c
 *
 *  Created on: 2014-02-22
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

#include "../include/libbrb_core.h"

static int EvKQBaseAIOFileGenericInit(EvKQBase *kq_base, EvAIOReq *aio_req, int aio_opcode);
static EvBaseKQJobCBH EvKQBaseAIOFileNotifyFinishJob;

/**************************************************************************************************************************/
int EvKQBaseAIOFileOpen(EvKQBase *kq_base, char *path, int flags, int mode)
{
	int file_fd;

	/* Try to open file */
	file_fd = open(path, flags, mode);

	/* Failed opening file */
	if (file_fd < 0)
		return file_fd;

	/* Touch statistics */
	kq_base->stats.aio.file.open_total++;
	kq_base->stats.aio.file.open_current++;

	/* Initialize and set description */
	EvKQBaseFileFDInit(kq_base, file_fd);
	EvKQBaseFDDescriptionSetByFD(kq_base, file_fd, "Open file [%s]", path);

	/* Set it to NON_BLOCKING */
	EvKQBaseSocketSetNonBlock(kq_base, file_fd);
	return file_fd;
}
/**************************************************************************************************************************/
int EvKQBaseAIOFileClose(EvKQBase *kq_base, int file_fd)
{
	EvBaseKQFileDesc *kq_fd;

	if (file_fd < 0)
		return 0;

	/* Lookup KQ_FD */
	kq_fd	= EvKQBaseFDGrabFromArena(kq_base, file_fd);

	/* This should never happen */
	if (kq_fd->fd.type != FD_TYPE_FILE)
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_CRITICAL,  LOGCOLOR_RED, "FILE_FD [%d] - Unexpected type [%d] - Was expecting [%d]\n", file_fd, kq_fd->fd.type, FD_TYPE_FILE);

	assert(FD_TYPE_FILE == kq_fd->fd.type);

	/* Cancel all pending AIO_REQUESTs */
	EvAIOReqQueueCancelAllByFD(&kq_base->aio.queue, file_fd);

	/* Close FD */
	EvKQBaseSocketClose(kq_base, file_fd);

	/* Touch statistics */
	kq_base->stats.aio.file.close_total++;
	kq_base->stats.aio.file.open_current--;

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseAIOFileWrite(EvKQBase *kq_base, EvAIOReq *dst_aio_req, int file_fd, char *src_buf, long size, long offset, EvAIOReqCBH *finish_cb, void *cb_data)
{
	EvAIOReq *aio_req;
	int op_status;

	EvBaseKQFileDesc *kq_fd	= EvKQBaseFDGrabFromArena(kq_base, file_fd);

	/* Sanity check */
	if (file_fd < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Trying to write to NEGATIVE_FD\n", file_fd);
		goto aio_failed;
	}

	/* Create new AIO_REQ and invoke common initialization */
	aio_req = EvAIOReqNew(&kq_base->aio.queue, file_fd, kq_base, src_buf, size, offset, NULL, finish_cb, cb_data);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued AIO_REQs\n", file_fd);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);

	/* Initialize */
	EvKQBaseAIOFileGenericInit(kq_base, aio_req, AIOREQ_OPCODE_WRITE);
	EvAIOReqQueueEnqueue(&kq_base->aio.queue, aio_req);

	/* Set flags we are WRITING to a FILE */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_file		= 1;

	/* Dispatch AIO read to the kernel */
	op_status = aio_write(&aio_req->aiocb);

	/* AIO_WRITE failed synchronously */
	if (-1 == op_status)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - AIO_WRITE failed synchronously\n", file_fd);

		/* Set error flag */
		kq_fd->flags.aio.ev.write.error		= 1;
		kq_fd->flags.aio.ev.write.pending	= 0;

		/* Save global ERRNO and leave */
		aio_req->flags.aio_failed	= 1;
		aio_req->err				= errno;

		/* Invoke notification CALLBACKS and destroy current AIO_REQ */
		EvAIOReqQueueRemoveItem(&kq_base->aio.queue, aio_req);
		EvAIOReqInvokeCallBacksAndDestroy(aio_req, 1, aio_req->fd, -1, -1, aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].bytes += size;
	kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].tx_count++;

	/* Check if request has any immediate error or has finished in this same IO loop */
	op_status = EvKQBaseAIOFileGeneric_FinishCheck(kq_base, aio_req);

	/* Save pending request AIO_INFO */
	if (((AIOREQ_PENDING == op_status) || (AIOREQ_FINISHED == op_status)) && (dst_aio_req))
		memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	return op_status;

	/* Touch statistics and leave */
	aio_failed:
	kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int EvKQBaseAIOFileRead(EvKQBase *kq_base, EvAIOReq *dst_aio_req, int file_fd, char *dst_buf, long size, long offset, EvAIOReqCBH *finish_cb, void *cb_data)
{
	EvAIOReq *aio_req;
	int op_status;

	EvBaseKQFileDesc *kq_fd	= EvKQBaseFDGrabFromArena(kq_base, file_fd);

	/* Sanity check */
	if (file_fd < 0)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Trying to read to NEGATIVE_FD\n", file_fd);
		goto aio_failed;
	}

	/* Create new AIO_REQ and invoke common initialization */
	aio_req = EvAIOReqNew(&kq_base->aio.queue, file_fd, kq_base, dst_buf, size, offset, NULL, finish_cb, cb_data);

	/* No more available AIO_REQ */
	if (!aio_req)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Too many enqueued AIO_REQs\n", file_fd);
		goto aio_failed;
	}

	/* AIO_REQ must be slotted and have an ID */
	assert(aio_req->id >= 0);

	/* Initialize */
	EvKQBaseAIOFileGenericInit(kq_base, aio_req, AIOREQ_OPCODE_READ);
	EvAIOReqQueueEnqueue(&kq_base->aio.queue, aio_req);

	/* Set flags we are READING from a FILE */
	aio_req->flags.aio_read		= 1;
	aio_req->flags.aio_file		= 1;

	/* Dispatch AIO read to the kernel */
	op_status = aio_read(&aio_req->aiocb);

	/* AIO_READ failed synchronously */
	if (-1 == op_status)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - AIO_READ failed synchronously\n", file_fd);

		/* Set error flag */
		kq_fd->flags.aio.ev.read.error		= 1;
		kq_fd->flags.aio.ev.read.pending	= 0;

		/* Save global ERRNO and leave */
		aio_req->flags.aio_failed	= 1;
		aio_req->err				= errno;

		/* Invoke notification CALLBACKS and destroy current AIO_REQ */
		EvAIOReqQueueRemoveItem(&kq_base->aio.queue, aio_req);
		EvAIOReqInvokeCallBacksAndDestroy(aio_req, 1, aio_req->fd, -1, -1, aio_req);
		goto aio_failed;
	}

	/* Touch statistics before finish check */
	kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].bytes += size;
	kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].tx_count++;

	/* Check if request has any immediate error or has finished in this same IO loop */
	op_status = EvKQBaseAIOFileGeneric_FinishCheck(kq_base, aio_req);

	/* Save pending request AIO_INFO */
	if ((AIOREQ_PENDING == op_status) && (dst_aio_req))
		memcpy(dst_aio_req, aio_req, sizeof(EvAIOReq));

	return op_status;

	/* Touch statistics and leave */
	aio_failed:
	kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].error++;
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
int EvKQBaseAIOCancelByReqID(EvKQBase *kq_base, int req_id)
{
	EvAIOReq *aio_req;

	/* Sanity check */
	if (req_id < 0)
		return 0;

	/* Search AIO_REQ */
	aio_req = EvAIOReqQueueGrabByID(&kq_base->aio.queue, req_id);
	assert(aio_req->flags.in_use);

	/* Mark as canceled */
	aio_req->flags.cancelled = 1;
	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseAIOFileGeneric_FinishCheck(EvKQBase *kq_base, EvAIOReq *aio_req)
{
	int op_status;
	int job_id;
	EvBaseKQFileDesc *kq_fd	= EvKQBaseFDGrabFromArena(kq_base,  aio_req->fd);

	assert(aio_req->id >= 0);

	/* Invoke AIO_ERROR */
	op_status = aio_error(&aio_req->aiocb);

	/* Failed invoking AIO_ERROR */
	if (-1 == op_status)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - AIO_ERROR failed synchronously\n", aio_req->fd);
		goto process_error;
	}

	/* Check AIO_ERROR further. Only accepted error is EINPROGRESS */
	if (op_status != 0)
	{
		/* Request is pending */
		if (EINPROGRESS == op_status)
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - %s ID [%d] - In progress\n",
					aio_req->fd, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ) ? "AIO_READ" : "AIO_WRITE"), aio_req->id);

			switch (aio_req->aio_opcode)
			{
			case AIOREQ_OPCODE_READ:
			{
				kq_fd->flags.aio.ev.read.error		= 0;
				kq_fd->flags.aio.ev.read.pending	= 1;
				break;
			}
			case AIOREQ_OPCODE_WRITE:
			{
				kq_fd->flags.aio.ev.write.error		= 0;
				kq_fd->flags.aio.ev.write.pending	= 1;
				break;
			}

			}

			return AIOREQ_PENDING;
		}
		/* Request has been canceled */
		else if (ECANCELED == op_status)
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - %s ID [%d] - Canceled\n",
					aio_req->fd, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ) ? "AIO_READ" : "AIO_WRITE"), aio_req->id);

			switch (aio_req->aio_opcode)
			{
			case AIOREQ_OPCODE_READ:
			{
				/* Touch statistics */
				kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].rx_count++;
				kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].cancel++;

				/* Recalculate pending count */
				kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].pending =
						(kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].tx_count - kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].rx_count);

				/* Set flags */
				kq_fd->flags.aio.ev.read.error		= 0;
				kq_fd->flags.aio.ev.read.pending	= 0;
				kq_fd->flags.aio.ev.write.canceled	= 1;

				break;
			}
			case AIOREQ_OPCODE_WRITE:
			{
				/* Touch statistics */
				kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].rx_count++;
				kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].cancel++;

				/* Recalculate pending count */
				kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].pending =
						(kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].tx_count - kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].rx_count);

				/* Set flags */
				kq_fd->flags.aio.ev.write.error		= 0;
				kq_fd->flags.aio.ev.write.pending	= 0;
				kq_fd->flags.aio.ev.write.canceled	= 1;

				break;
			}
			}

			/* Destroy AIO_REQ without invoking CALLBACKs */
			EvAIOReqQueueRemoveItem(&kq_base->aio.queue, aio_req);
			EvAIOReqDestroy(aio_req);
			return AIOREQ_CANCELED;
		}

		goto process_error;
	}

	/* Invoke AIO_RETURN to check if we have finished this READ_REQ immediately */
	op_status = aio_return(&aio_req->aiocb);

	/* Failed invoking AIO_ERROR */
	if (-1 == op_status)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - AIO_RETURN failed synchronously\n",  aio_req->fd);
		goto process_error;
	}

	/* EOF detected, set flag */
	if (op_status == 0)
	{
		switch (aio_req->aio_opcode)
		{
		case AIOREQ_OPCODE_READ:	kq_fd->flags.aio.ev.read.eof	= 1; break;
		case AIOREQ_OPCODE_WRITE:	kq_fd->flags.aio.ev.write.eof	= 1; break;
		}
	}

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - %s OK - ID [%d] - [%d]-[%s]\n",
			aio_req->fd, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ) ? "AIO_READ" : "AIO_WRITE"), aio_req->id, aio_req->data.size, aio_req->data.ptr);

	/* Set finish flag */
	switch (aio_req->aio_opcode)
	{
	case AIOREQ_OPCODE_READ:
	{
		/* Touch statistics */
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].rx_count++;

		/* Recalculate pending count */
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].pending =
				(kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].tx_count - kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].rx_count);

		/* Set flags */
		kq_fd->flags.aio.ev.read.error		= 0;
		kq_fd->flags.aio.ev.read.pending	= 0;
		break;
	}
	case AIOREQ_OPCODE_WRITE:
	{
		/* Touch statistics */
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].rx_count++;

		/* Recalculate pending count */
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].pending =
				(kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].tx_count - kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].rx_count);

		/* Set flags */
		kq_fd->flags.aio.ev.write.error		= 0;
		kq_fd->flags.aio.ev.write.pending	= 0;
		break;
	}
	}

	/* Remove finished AIO from QUEUE */
	EvAIOReqQueueRemoveItem(&kq_base->aio.queue, aio_req);

	/* This AIO_REQ belongs to a past IO loop, just invoke FINISH directly */
	if ((!aio_req->flags.aio_delayed_notify) && (aio_req->ioloop_create < kq_base->stats.kq_invoke_count))
	{
		/* Touch statistics */
		kq_base->stats.aio.opcode[aio_req->aio_opcode].success_notify++;

		/* Invoke notification CALLBACKS and destroy current AIO_REQ */
		EvAIOReqInvokeCallBacksAndDestroy(aio_req, 1, aio_req->fd, aio_req->data.size, -1, aio_req);
		return AIOREQ_FINISHED;
	}
	/* This AIO_REQ belongs to this very same IO loop, schedule NOTIFICATION JOB */
	else
	{
		job_id = EvKQJobsAdd(kq_base, JOB_ACTION_ADD_VOLATILE, 1, EvKQBaseAIOFileNotifyFinishJob, aio_req);

		/* Failed to add notification JOB - Destroy AIO_REQ otherwise we will LEAK!! */
		if (job_id < 0)
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - Failed to add FINISH_NOTIFY job\n", aio_req->fd);
			EvAIOReqDestroy(aio_req);
			return AIOREQ_FINISHED;
		}

		/* Touch statistics */
		kq_base->stats.aio.opcode[aio_req->aio_opcode].success_sched++;
		return AIOREQ_FINISHED;
	}

	return AIOREQ_FINISHED;

	/* TAG to process FAILED requests */
	process_error:

	/* Save global ERRNO */
	aio_req->err				= errno;
	aio_req->flags.aio_failed	= 1;

	/* Set error flag */
	switch (aio_req->aio_opcode)
	{
	case AIOREQ_OPCODE_READ:
	{
		/* Touch statistics */
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].rx_count++;
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].error++;

		/* Recalculate pending count */
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].pending =
				(kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].tx_count - kq_base->stats.aio.opcode[AIOREQ_OPCODE_READ].rx_count);

		/* Set flags */
		kq_fd->flags.aio.ev.read.error		= 1;
		kq_fd->flags.aio.ev.read.pending	= 0;
		break;
	}
	case AIOREQ_OPCODE_WRITE:
	{
		/* Touch statistics */
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].rx_count++;
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].error++;

		/* Recalculate pending count */
		kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].pending =
				(kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].tx_count - kq_base->stats.aio.opcode[AIOREQ_OPCODE_WRITE].rx_count);

		/* Set flags */
		kq_fd->flags.aio.ev.write.error		= 1;
		kq_fd->flags.aio.ev.write.pending	= 0;
		break;
	}
	}

	/* Invoke notification CALLBACKS and destroy current AIO_REQ */
	EvAIOReqQueueRemoveItem(&kq_base->aio.queue, aio_req);
	EvAIOReqInvokeCallBacksAndDestroy(aio_req, 1, aio_req->fd, -1, -1, aio_req);
	return AIOREQ_FAILED;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQBaseAIOFileGenericInit(EvKQBase *kq_base, EvAIOReq *aio_req, int aio_opcode)
{

	/* Populate common AIO_CB data */
	aio_req->aiocb.aio_fildes							= aio_req->fd;
	aio_req->aiocb.aio_offset							= aio_req->data.offset;
	aio_req->aiocb.aio_buf								= aio_req->data.ptr;
	aio_req->aiocb.aio_nbytes							= aio_req->data.size;

	/* Populate KEVENT AIO_CB data */
	aio_req->aiocb.aio_sigevent.sigev_signo				= kq_base->kq_base;
#if !defined(__linux__)
	aio_req->aiocb.aio_sigevent.sigev_notify			= SIGEV_KEVENT;
	aio_req->aiocb.aio_sigevent.sigev_value.sigval_ptr	= aio_req;
#endif

	/* Save our AIO_OPCODE */
	aio_req->aio_opcode									= aio_opcode;

	return 1;
}
/**************************************************************************************************************************/
static int EvKQBaseAIOFileNotifyFinishJob(void *job_ptr, void *cbdata_ptr)
{
	EvKQQueuedJob *kq_job 	= job_ptr;
	EvAIOReq *aio_req 		= cbdata_ptr;
	EvKQBase *ev_base		= kq_job->ev_base;

	/* Touch statistics */
	ev_base->stats.aio.opcode[aio_req->aio_opcode].success_notify++;

	/* Recalculate pending count */
	ev_base->stats.aio.opcode[aio_req->aio_opcode].pending =
			(ev_base->stats.aio.opcode[aio_req->aio_opcode].tx_count - ev_base->stats.aio.opcode[aio_req->aio_opcode].rx_count);

	/* Invoke notification CALLBACKS and destroy current AIO_REQ */
	EvAIOReqInvokeCallBacksAndDestroy(aio_req, 1, aio_req->fd, aio_req->data.size, -1, aio_req);

	return 1;
}
/**************************************************************************************************************************/
