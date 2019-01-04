/*
 * ev_kq_timeout.c
 *
 *  Created on: 2012-11-03
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2012 BrByte Software (Oliveira Alves & Amorim LTDA)
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

/*
 * Timeout events will be invoked from a kqueue_timer created at EvKQBaseNew.
 *
 * It will be invoked either serially or into multiple thread contexts, depending
 * on the kind of engine initialized on EvKQBaseNew.
 */

#include "../include/libbrb_ev_kq.h"

static EvBaseKQCBH EvKQBaseTimeoutInternalEvent;

/**************************************************************************************************************************/
/* Public event set interface
/**************************************************************************************************************************/
void EvKQBaseTimeoutSet(EvKQBase *kq_base, int fd, int timeout_type, int timeout_ms, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;
	int local_cur_invoke_ts = 0;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return;

	/* Do not allow a timeout_type greater than KQ_CB_TIMEOUT_LASTITEM */
	if (timeout_type >= KQ_CB_TIMEOUT_LASTITEM)
		return;

	/* Grab a local copy of current invoke TIMESTAMP */
	local_cur_invoke_ts = kq_base->stats.cur_invoke_ts_sec;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Timer already set, bail out */
	if (kq_fd->timeout[timeout_type].timer_id > -1)
		return;

	//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - TIMEOUT_MS [%d] - TIMEOUT_TYPE [%d]\n", fd, timeout_ms, timeout_type);

	/* Caller sent a valid cb_handler pointer. Add the event */
	if (cb_handler)
	{
		/* Touch cb_ptr and cb_data */
		kq_fd->timeout[timeout_type].cb_handler_ptr		= cb_handler;
		kq_fd->timeout[timeout_type].cb_data_ptr		= cb_data;

		/* Touch TIMESTAMPS */
		kq_fd->timeout[timeout_type].when_ts			= local_cur_invoke_ts + (timeout_ms * 1000);
		kq_fd->timeout[timeout_type].timeout_ms			= timeout_ms;

		/* Add a timeout timer for this FD */
		kq_fd->timeout[timeout_type].timer_id			= EvKQBaseTimerAdd(kq_base, COMM_ACTION_ADD_VOLATILE, timeout_ms, EvKQBaseTimeoutInternalEvent, kq_fd);


	}
	/* Caller sent a NULL cb_handler pointer. Remove the event */
	else
	{
		/* Delete timeout timer for this FD */
		if (kq_fd->timeout[timeout_type].timer_id > -1)
			EvKQBaseTimerCtl(kq_base, kq_fd->timeout[timeout_type].timer_id, COMM_ACTION_DELETE);

		/* Touch cb_ptr and cb_data */
		kq_fd->timeout[timeout_type].cb_handler_ptr		= cb_handler;
		kq_fd->timeout[timeout_type].cb_data_ptr		= cb_data;

		/* Touch TIMESTAMPS */
		kq_fd->timeout[timeout_type].when_ts			= -1;
		kq_fd->timeout[timeout_type].timeout_ms			= -1;
		kq_fd->timeout[timeout_type].timer_id 			= -1;
	}

	return;
}
/**************************************************************************************************************************/
int EvKQBaseTimeoutInitAllByFD(EvKQBase *kq_base, int fd)
{
	EvBaseKQFileDesc *kq_fd;
	int op_status;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Issue the clear */
	EvKQBaseTimeoutInitAllByKQFD(kq_base, kq_fd);

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseTimeoutInitAllByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	int i;

	/* Sanity check */
	if (!kq_fd)
		return 0;

	/* Set all to UNINITIALIZED */
	for (i = 0; i < KQ_CB_TIMEOUT_LASTITEM; i++)
	{
		/* Set it to disabled */
		kq_fd->timeout[i].when_ts		= -1;
		kq_fd->timeout[i].timeout_ms	= -1;
		kq_fd->timeout[i].timer_id 		= -1;

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseTimeoutClearByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int timeout_type)
{
	/* Sanity check */
	if (!kq_fd)
		return 0;

	/* Do not allow a timeout_type greater than KQ_CB_TIMEOUT_LASTITEM */
	if (timeout_type >= KQ_CB_TIMEOUT_LASTITEM)
		return 0;

	/* No active timer, bail out */
	if (kq_fd->timeout[timeout_type].timer_id < 0)
		return 0;

	//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - TIMEOUT TYPE [%d]\n", kq_fd->fd_num, timeout_type);

	/* Delete timeout timer for this FD */
	EvKQBaseTimerCtl(kq_base, kq_fd->timeout[timeout_type].timer_id, COMM_ACTION_DELETE);

	/* Touch cb_ptr and cb_data */
	kq_fd->timeout[timeout_type].cb_handler_ptr		= NULL;
	kq_fd->timeout[timeout_type].cb_data_ptr		= NULL;

	/* Set it to disabled */
	kq_fd->timeout[timeout_type].when_ts		= -1;
	kq_fd->timeout[timeout_type].timeout_ms		= -1;
	kq_fd->timeout[timeout_type].timer_id 		= -1;

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseTimeoutClear(EvKQBase *kq_base, int fd, int timeout_type)
{
	EvBaseKQFileDesc *kq_fd;
	int op_status;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return 0;

	/* Do not allow a timeout_type greater than KQ_CB_TIMEOUT_LASTITEM */
	if (timeout_type >= KQ_CB_TIMEOUT_LASTITEM)
		return 0;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	/* Issue the clear */
	op_status = EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, timeout_type);

	return op_status;
}
/**************************************************************************************************************************/
int EvKQBaseTimeoutClearAll(EvKQBase *kq_base, int fd)
{
	int i;

	/* Sanity check */
	if (fd < 0)
		return 0;

	EvKQBaseTimeoutClear(kq_base, fd, COMM_EV_TIMEOUT_READ);
	EvKQBaseTimeoutClear(kq_base, fd, COMM_EV_TIMEOUT_WRITE);
	EvKQBaseTimeoutClear(kq_base, fd, COMM_EV_TIMEOUT_BOTH);

	return 1;
}
/**************************************************************************************************************************/
static int EvKQBaseTimeoutInternalEvent(int timer_id, int not_used, int thrd_id, void *cb_data, void *base_ptr)
{
	int i;

	EvKQBase *kq_base 		= base_ptr;
	EvBaseKQFileDesc *kq_fd = cb_data;
	int timeout_type		= -1;

	EvBaseKQCBH *user_cb_handler	= NULL;
	void *user_cb_data				= NULL;

	/* Search what timer we are hitting now */
	for (i = 0; i < KQ_CB_TIMEOUT_LASTITEM; i++)
	{
		/* Found timer ID */
		if (kq_fd->timeout[i].timer_id == timer_id)
		{
			timeout_type = i;
			break;
		}
	}

	/* WARNING, unexpected timer tick - This should not happen */
	if (timeout_type < 0)
		return 0;

	/* WARNING, unexpected timer tick - This should not happen */
	assert(timeout_type >= 0);

	/* Grab pointers to cb_handler and data */
	user_cb_handler	= kq_fd->timeout[timeout_type].cb_handler_ptr;
	user_cb_data	= kq_fd->timeout[timeout_type].cb_data_ptr;

	/* Touch cb_ptr and cb_data */
	kq_fd->timeout[timeout_type].cb_handler_ptr		= NULL;
	kq_fd->timeout[timeout_type].cb_data_ptr		= NULL;

	/* Set it to disabled */
	kq_fd->timeout[timeout_type].when_ts		= -1;
	kq_fd->timeout[timeout_type].timeout_ms		= -1;
	kq_fd->timeout[timeout_type].timer_id 		= -1;

	/* Lock the arena where this FD lives in */
	MemArenaLockByID(kq_base->fd.arena, kq_fd->fd.num);

	/* Invoke the callback_handler */
	if (user_cb_handler)
		user_cb_handler(kq_fd->fd.num, timeout_type, -1, user_cb_data, kq_base);

	/* Unlock the arena where this FD lives in */
	MemArenaUnlockByID(kq_base->fd.arena, kq_fd->fd.num);
	return 1;
}
/**************************************************************************************************************************/
