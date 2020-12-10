/*
 * comm_tcp_aio.c
 *
 *  Created on: 2016-10-06
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *
 *
 * Copyright (c) 2016 BrByte Software (Oliveira Alves & Amorim LTDA)
 * Todos os direitos reservados. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "../include/libbrb_core.h"

/**************************************************************************************************************************/
int CommEvTCPAIOWrite(struct _EvKQBase *ev_base, struct _EvKQBaseLogBase *log_base, CommEvStatistics *stats, CommEvTCPIOData *iodata, CommEvTCPIOResult *ioret, void *parent,
		int can_write_sz, int invoke_cb)
{
	EvBaseKQFileDesc *kq_fd;
	EvAIOReq *aio_req_deq;
	EvAIOReq *aio_req;
	long wanted_write_sz;
	long possible_write_sz;
	int wrote_sz;
	char *data_ptr;

	/* Initialize IO_RESULT */
	ioret->aio_total_sz = 0;
	ioret->aio_count	= 0;

	/* Grab HEAD AIO request */
	write_again:
	aio_req			= EvAIOReqQueuePointToHead(&iodata->write_queue);

	/* Nothing left to write */
	if (!aio_req)
		return COMM_TCP_AIO_WRITE_FINISHED;

	/* Grab AIO_REQ FD underneath KQ_FD */
	kq_fd				= EvKQBaseFDGrabFromArena(ev_base, aio_req->fd);

	/* Calculate current data size and offset */
	wanted_write_sz		= EvAIOReqGetMissingSize(aio_req);
	data_ptr			= EvAIOReqGetDataPtr(aio_req);
	possible_write_sz	= ((can_write_sz < wanted_write_sz) ? can_write_sz : wanted_write_sz);

	/* This AIO_REQ wants to write zero bytes. Just dequeue */
	if (0 == possible_write_sz)
	{
		KQBASE_LOG_PRINTF(log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - AIO_ID [%d] - CAN [%d] - WANT [%d] - POSSIBLE [%d] - POS [%ld / %ld] - Zero Possible!\n",
				aio_req->fd, aio_req->id, can_write_sz, wanted_write_sz, possible_write_sz, aio_req->data.offset, aio_req->data.size);
		goto finished;
	}

	/* Issue write call - Either what we want to write, if possible. Otherwise, write what kernel tells us we can */
	wrote_sz			= write(aio_req->fd, data_ptr, possible_write_sz);

	/* The write was interrupted by a signal or we were not able to write any data to it, reschedule and return. */
	if (-1 == wrote_sz)
	{
		/* Save ERRNO */
		aio_req->err = errno;

		/* NON_FATAL error */
		if ((!kq_fd->flags.so_write_eof) && (errno == EINTR || errno == EAGAIN))
		{
			KQBASE_LOG_PRINTF(log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_ID [%d] - Non fatal write error - [%s] - CAN [%d] - TRIED [%d] - TOTAL [%d]\n",
					aio_req->fd, aio_req->id, (errno == EINTR ? "EINTR" : "EAGAIN"), can_write_sz, possible_write_sz, ioret->aio_total_sz);
			return COMM_TCP_AIO_WRITE_NEEDED;
		}

		KQBASE_LOG_PRINTF(log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "FD [%d] - AIO_ID [%d] - Fatal write error - CAN [%d] - TRIED [%d] - TOTAL [%d] - ERR [%d]\n",
				aio_req->fd, aio_req->id, can_write_sz, possible_write_sz, ioret->aio_total_sz, aio_req->err);
		return COMM_TCP_AIO_WRITE_ERR_FATAL;
	}
	/* This should not happen */
	else if (0 == wrote_sz)
	{
		KQBASE_LOG_PRINTF(log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - AIO_ID [%d] - TRIED [%d] - POS [%ld / %ld] - Syscall write returned ZERO\n",
				aio_req->fd, aio_req->id, possible_write_sz, aio_req->data.offset, aio_req->data.size);
	}

	/* TOUCH statistics */
	stats->total[COMM_CURRENT].byte_tx		+= wrote_sz;
	stats->total[COMM_CURRENT].packet_tx	+= 1;

	/* Write_ok, update offset and counter */
	aio_req->data.offset					+= wrote_sz;
	ioret->aio_total_sz						+= wrote_sz;
	can_write_sz							-= wrote_sz;

	/* Write is complete */
	if (aio_req->data.offset >= aio_req->data.size)
	{
		finished:

		KQBASE_LOG_PRINTF(log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - AIO_ID [%d] - Finished writing [%d] bytes - Wanted [%ld] - Offset is now [%ld]\n",
				aio_req->fd, aio_req->id, wrote_sz, wanted_write_sz, aio_req->data.offset);

		/* Remove AIO_REQ from WRITE_QUEUE */
		EvAIOReqQueueDequeue(&iodata->write_queue);

		/* Invoke WRITE_CB if set to do it */
		if (invoke_cb)
		{
			KQBASE_LOG_PRINTF(log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - AIO_ID [%d] - Will invoke CB_FUNC at [%p / %p]\n",
					aio_req->fd, aio_req->id, aio_req->finish_cb, aio_req->finish_cbdata);
			EvAIOReqInvokeCallBacks(aio_req, 1, aio_req->fd, aio_req->data.offset, -1, parent);
		}

		/* Destroy AIO_REQ */
		EvAIOReqDestroy(aio_req);

		/* Closed flag set, we are already destroyed, just bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return COMM_TCP_AIO_WRITE_FINISHED;

		/* Still can write some more bytes */
		if (can_write_sz > 0)
			goto write_again;

		/* If there are no AIO_REQ left, reply finished. Otherwise, NEEDED */
		if (EvAIOReqQueueIsEmpty(&iodata->write_queue))
			return COMM_TCP_AIO_WRITE_FINISHED;

		return COMM_TCP_AIO_WRITE_NEEDED;
	}

	KQBASE_LOG_PRINTF(log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - AIO_ID [%d] - CAN [%d] - Wrote [%d] bytes - POS [%ld / %ld]\n",
			aio_req->fd, aio_req->id, can_write_sz, wrote_sz, aio_req->data.offset, aio_req->data.size);

	return COMM_TCP_AIO_WRITE_NEEDED;
}
/**************************************************************************************************************************/
int CommEvTCPAIOSSLWrite(struct _EvKQBase *ev_base, struct _EvKQBaseLogBase *log_base, CommEvStatistics *stats, CommEvTCPIOData *iodata, CommEvTCPIOResult *ioret, void *parent,
		int can_write_sz, int invoke_cb)
{

	EvBaseKQFileDesc *kq_fd;
	EvAIOReq *aio_req_deq;
	EvAIOReq *aio_req;
	long wanted_write_sz;
	long possible_write_sz;
	int wrote_sz;
	char *data_ptr;

	/* Initialize IO_RESULT */
	ioret->aio_total_sz = 0;
	ioret->aio_count	= 0;

	/* Grab HEAD AIO request */
	write_again:
	aio_req			= EvAIOReqQueuePointToHead(&iodata->write_queue);

	/* Nothing left to write */
	if (!aio_req)
		return COMM_TCP_AIO_WRITE_FINISHED;

	/* Grab AIO_REQ FD underneath KQ_FD */
	kq_fd				= EvKQBaseFDGrabFromArena(ev_base, aio_req->fd);

	/* Calculate current data size and offset */
	wanted_write_sz		= EvAIOReqGetMissingSize(aio_req);
	data_ptr			= EvAIOReqGetDataPtr(aio_req);
	possible_write_sz	= ((can_write_sz < wanted_write_sz) ? can_write_sz : wanted_write_sz);

	/* Issue write call - Either what we want to write, if possible. Otherwise, write what kernel tells us we can */
	wrote_sz			= write(aio_req->fd, data_ptr, possible_write_sz);



	return 1;
}
/**************************************************************************************************************************/


