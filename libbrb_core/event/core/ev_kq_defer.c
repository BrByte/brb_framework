/*
 * ev_kq_defer.c
 *
 *  Created on: 2012-08-28
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

#include "../include/libbrb_ev_kq.h"


static int EvKQBaseDeferReadResetIterFlagsByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
static int EvKQBaseDeferWriteResetIterFlagsByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
static void EvKQBaseDeferBeginCheck(EvKQBase *kq_base);

/**************************************************************************************************************************/
void EvKQBaseDeferDispatch(EvKQBase *kq_base)
{
	int last_defer_ms;

	/* First RUN - Check DEFER FDs */
	if (kq_base->stats.defer_check_invoke_tv.tv_sec <= 0)
	{
		/* Touch TIMEVAL of last defer check and check it */
		memcpy(&kq_base->stats.defer_check_invoke_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
		EvKQBaseDeferBeginCheck(kq_base);

		return;
	}
	/* Already initialized */
	else
	{
		/* Calculate mili_second delta from previous defer check */
		last_defer_ms = EvKQBaseTimeValSubMsec(&kq_base->stats.defer_check_invoke_tv, &kq_base->stats.cur_invoke_tv);

		/* Interval check interval set by upper layer reached, fire up defer check */
		if (last_defer_ms > kq_base->defer.interval_check_ms)
		{
			/* Touch TIMEVAL of last defer check and check it */
			memcpy(&kq_base->stats.defer_check_invoke_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
			EvKQBaseDeferBeginCheck(kq_base);
		}
	}

	return;
}
/**************************************************************************************************************************/
int EvKQBaseDeferResetByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	/* Clean TS */
	memset(&kq_fd->defer.read.begin_tv, 0, sizeof(struct timeval));
	memset(&kq_fd->defer.read.check_tv, 0, sizeof(struct timeval));
	memset(&kq_fd->defer.read.set_tv, 0, sizeof(struct timeval));

	memset(&kq_fd->defer.write.begin_tv, 0, sizeof(struct timeval));
	memset(&kq_fd->defer.write.check_tv, 0, sizeof(struct timeval));
	memset(&kq_fd->defer.write.set_tv, 0, sizeof(struct timeval));

	kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_handler_ptr	= NULL;
	kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_data_ptr		= NULL;
	kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled		= 0;
	kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.persist		= 0;

	kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_handler_ptr	= NULL;
	kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_data_ptr		= NULL;
	kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled	= 0;
	kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.persist	= 0;

	kq_fd->defer.read.pending_bytes										= 0;
	kq_fd->defer.read.count												= 0;
	kq_fd->flags.defer_read_transition_set								= 0;
	kq_fd->flags.defer_read												= 0;

	kq_fd->defer.write.pending_bytes									= 0;
	kq_fd->defer.write.count											= 0;
	kq_fd->flags.defer_write_transition_set								= 0;
	kq_fd->flags.defer_write											= 0;

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseDeferReadCheckByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int event_sz)
{
	EvBaseKQGenericEventPrototype *ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ];
	EvBaseKQCBH *defer_read_cb_handler		= ev_proto->cb_handler_ptr;
	void *defer_read_cb_data				= ev_proto->cb_data_ptr;
	int defer_read							= 0;

	/* DEFER is disabled, or without CB_HANDLER, just bail out */
	if ((!ev_proto->flags.enabled) || (!defer_read_cb_handler))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - DEFER CHECK DISABLED - CBH AT [%p]\n", kq_fd->fd.num, defer_read_cb_handler);
		return 0;
	}

	/* Volatile event, clean and disable it */
	if (!ev_proto->flags.persist)
	{
		ev_proto->cb_handler_ptr	= NULL;
		ev_proto->cb_data_ptr		= NULL;
		ev_proto->flags.enabled		= 0;
	}

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - READ CHECKING DEFER - PERSIST [%d]\n", kq_fd->fd.num, kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.persist);

	/* Invoke the defer check call_back handler */
	if (defer_read_cb_handler)
		defer_read = defer_read_cb_handler(kq_fd->fd.num, event_sz, -1, defer_read_cb_data, kq_base);

	/* OK, upper layers asked for a READ_DEFER, honor it - Will be disabled by CHECK_DEFER */
	if (defer_read)
	{
		/* Already being deferred, bail out */
		if (kq_fd->flags.defer_read)
		{
			/* Save UNIX_TS of when we last check DEFER READ this FD */
			memcpy(&kq_fd->defer.read.check_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
			return 0;
		}

		/* Add dummy TRANSITION event. Need it to catch EOF events received as READ_EV EOF flag */
		if ((!kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.persist) && (!kq_fd->flags.defer_read_transition_set))
		{
			EvKQBaseSetEvent(kq_base, kq_fd->fd.num, COMM_EV_READ, COMM_ACTION_ADD_TRANSITION, kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_handler_ptr, kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_data_ptr);
			kq_fd->flags.defer_read_transition_set = 1;
		}

		/* Save UNIX_TS of when we begin DEFERING READ this FD, link to defer read list for DEFER check on each IO LOOP and leave */
		memcpy(&kq_fd->defer.read.begin_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

		/* Begin defer in this IO loop, add to list */
		DLinkedListAdd(&kq_base->defer.read_list, &kq_fd->defer.read.node, kq_fd);

		/* Save how many pending bytes was last seen on this FD, and set the defer read flag */
		kq_fd->defer.read.pending_bytes = event_sz;
		kq_fd->flags.defer_read			= 1;

		return 1;
	}
	/* Disabled READ DEFER CHECK */
	else
	{
		EvKQBaseDeferReadRemoveByKQFD(kq_base, kq_fd);
		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseDeferReadRemoveByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	int data_read;

	EvBaseKQCBH *read_cb_handler	= kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_handler_ptr;
	void *read_cb_data				= kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_data_ptr;
	int has_read_ev					= kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled;
	int pending_bytes				= kq_fd->defer.read.pending_bytes;

	/* Was not being deferred, bail out */
	if (!kq_fd->flags.defer_read)
		return 0;

	/* Delete from DEFER LIST */
	DLinkedListDelete(&kq_base->defer.read_list, &kq_fd->defer.read.node);

	/* Transition set for volatile event, must REMOVE it and RESCHEDULE READ_EV if we are NOT CLOSING */
	if ((!kq_fd->flags.closing) && (kq_fd->flags.defer_read_transition_set))
	{
		EvKQBaseSetEvent(kq_base, kq_fd->fd.num, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);

		/* Reschedule read event with previously saved CB_H and CB_DATA if it was ENABLED */
		if ((has_read_ev) && (!kq_fd->flags.closing) && (!kq_fd->flags.closed))
			EvKQBaseSetEvent(kq_base, kq_fd->fd.num, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, read_cb_handler, read_cb_data);
	}

	/* Clean READ_DEFER data and try to dispatch any pending READ event LEFT on this DEFER_FD */
	EvKQBaseDeferReadResetIterFlagsByKQFD(kq_base, kq_fd);

	/* Invoke pending READ event for remaining stalled bytes if we s*/
	if ((!kq_fd->flags.closing) && (!kq_fd->flags.closed) && (has_read_ev) && (pending_bytes > 0))
	{
		data_read = EvKQBaseDispatchEventRead(kq_base, kq_fd, pending_bytes);

		/* Give a chance to set defer again */
		EvKQBaseDeferReadCheckByKQFD(kq_base, kq_fd, 0);
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int EvKQBaseDeferWriteCheckByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int event_sz)
{
	EvBaseKQGenericEventPrototype *ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE];
	EvBaseKQCBH *defer_write_cb_handler		= ev_proto->cb_handler_ptr;
	void *defer_write_cb_data				= ev_proto->cb_data_ptr;
	int defer_write							= 0;

	/* DEFER is disabled, or without CB_HANDLER, just bail out */
	if ((!ev_proto->flags.enabled) || (!defer_write_cb_handler))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - DEFER CHECK DISABLED - CBH AT [%p]\n", kq_fd->fd.num, defer_write_cb_handler);
		return 0;
	}

	/* Volatile event, clean and disable it */
	if (!ev_proto->flags.persist)
		ev_proto->flags.enabled	= 0;

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - CHECKING DEFER - PERSIST [%d]\n", kq_fd->fd.num, ev_proto->flags.persist);

	/* Invoke the defer check call_back handler */
	defer_write = defer_write_cb_handler(kq_fd->fd.num, event_sz, -1, defer_write_cb_data, kq_base);

	/* OK, upper layers asked for a WRITE_DEFER, honor it - Will be disabled by CHECK_DEFER */
	if (defer_write)
	{
		/* Already being deferred, bail out */
		if (kq_fd->flags.defer_write)
		{
			/* Save UNIX_TS of when we last check DEFER WRITE this FD */
			memcpy(&kq_fd->defer.write.check_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
			return 0;
		}

		/* Add dummy TRANSITION event. Need it to catch EOF events received as WRITE_EV EOF flag */
		if ((!kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.persist) && (!kq_fd->flags.defer_write_transition_set))
		{
			EvKQBaseSetEvent(kq_base, kq_fd->fd.num, COMM_EV_WRITE, COMM_ACTION_ADD_TRANSITION, kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_handler_ptr, kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_data_ptr);
			kq_fd->flags.defer_write_transition_set = 1;
		}

		/* Save UNIX_TS of when we begin DEFERING WRITE this FD, link to defer write list for DEFER check on each IO LOOP and leave */
		memcpy(&kq_fd->defer.write.begin_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

		/* Begin defer in this IO loop, add to list */
		DLinkedListAdd(&kq_base->defer.write_list, &kq_fd->defer.write.node, kq_fd);

		/* Save how many pending bytes was last seen on this FD, and set the defer write flag */
		kq_fd->defer.write.pending_bytes 	= event_sz;
		kq_fd->flags.defer_write			= 1;

		return 1;
	}
	/* Disabled WRITE DEFER CHECK */
	else
	{
		EvKQBaseDeferWriteRemoveByKQFD(kq_base, kq_fd);
		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
int EvKQBaseDeferWriteRemoveByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	int data_write;

	EvBaseKQCBH *write_cb_handler	= kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_handler_ptr;
	void *write_cb_data				= kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_data_ptr;
	int has_write_ev				= kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled;
	int pending_bytes				= kq_fd->defer.write.pending_bytes;

	/* Was not being deferred, bail out */
	if (!kq_fd->flags.defer_write)
		return 0;

	/* Delete from DEFER LIST */
	DLinkedListDelete(&kq_base->defer.write_list, &kq_fd->defer.write.node);

	/* Transition set for volatile event, remove it */
	if ((!kq_fd->flags.closing) && (kq_fd->flags.defer_write_transition_set))
	{
		EvKQBaseSetEvent(kq_base, kq_fd->fd.num, COMM_EV_WRITE, COMM_ACTION_DELETE, NULL, NULL);

		/* Reschedule WRITE event with previously saved CB_H and CB_DATA if it was ENABLED */
		if ((has_write_ev) && (!kq_fd->flags.closing) && (!kq_fd->flags.closed))
			EvKQBaseSetEvent(kq_base, kq_fd->fd.num, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, write_cb_handler, write_cb_data);
	}

	/* Clean WRITE_DEFER data */
	EvKQBaseDeferWriteResetIterFlagsByKQFD(kq_base, kq_fd);

	/* Invoke pending WRITE event for remaining stalled bytes */
	if ((!kq_fd->flags.closing) && (!kq_fd->flags.closed) && (has_write_ev) && (pending_bytes > 0))
	{
		data_write = EvKQBaseDispatchEventWrite(kq_base, kq_fd, pending_bytes);

		/* Give a chance to set defer again */
		EvKQBaseDeferWriteCheckByKQFD(kq_base, kq_fd, 0);
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQBaseDeferReadResetIterFlagsByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	/* Clean TS */
	memset(&kq_fd->defer.read.begin_tv, 0, sizeof(struct timeval));
	memset(&kq_fd->defer.read.check_tv, 0, sizeof(struct timeval));

	kq_fd->defer.read.pending_bytes			= 0;
	kq_fd->defer.read.count					= 0;
	kq_fd->flags.defer_read_transition_set	= 0;
	kq_fd->flags.defer_read					= 0;

	return 1;
}
/**************************************************************************************************************************/
static int EvKQBaseDeferWriteResetIterFlagsByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd)
{
	/* Clean TS */
	memset(&kq_fd->defer.write.begin_tv, 0, sizeof(struct timeval));
	memset(&kq_fd->defer.write.check_tv, 0, sizeof(struct timeval));

	kq_fd->defer.write.pending_bytes		= 0;
	kq_fd->defer.write.count				= 0;
	kq_fd->flags.defer_write_transition_set	= 0;
	kq_fd->flags.defer_write				= 0;

	return 1;
}
/**************************************************************************************************************************/
static void EvKQBaseDeferBeginCheck(EvKQBase *kq_base)
{
	EvBaseKQFileDesc *kq_fd;
	DLinkedListNode *node;
	int defer_read;
	int defer_write;

	EvBaseKQCBH *read_cb_handler		= NULL;
	EvBaseKQCBH *write_cb_handler		= NULL;
	EvBaseKQCBH *defer_read_cb_handler	= NULL;
	EvBaseKQCBH *defer_write_cb_handler	= NULL;
	void *read_cb_data					= NULL;
	void *write_cb_data					= NULL;
	void *defer_read_cb_data			= NULL;
	void *defer_write_cb_data			= NULL;

	//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "READ_LIST_SZ [%d] - WRITE_LIST_SZ [%d]\n", kq_base->defer.read_list.size, kq_base->defer.write_list.size);

	/* Walk the READ_DEFER list */
	for (node = kq_base->defer.read_list.head; node; node = node->next)
	{
		read_loop_without_move:

		/* Sanity check */
		if ((!node) || (!node->data))
			break;

		kq_fd = node->data;

		/* Disabled, move on */
		if (!kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled)
			continue;

		defer_read_cb_handler	= kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_handler_ptr;
		defer_read_cb_data		= kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_data_ptr;

		/* Invoke the defer check call_back handler */
		if (defer_read_cb_handler)
			defer_read = defer_read_cb_handler(kq_fd->fd.num, kq_fd->defer.read.pending_bytes, -1, defer_read_cb_data, kq_base);

		/* Keep DEFER_READ on this FD */
		if (defer_read)
		{
			/* Save UNIX_TS of when we last check DEFER READ this FD */
			memcpy(&kq_fd->defer.read.check_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
			continue;
		}
		else
		{
			/* Remove from DEFER_CHECK list */
			node = node->next;
			EvKQBaseDeferReadRemoveByKQFD(kq_base, kq_fd);
			goto read_loop_without_move;
		}

		continue;
	}


	/* Walk the WRITE_DEFER list */
	for (node = kq_base->defer.write_list.head; node; node = node->next)
	{
		write_loop_without_move:

		/* Sanity check */
		if ((!node) || (!node->data))
			break;

		kq_fd = node->data;

		/* Disabled, move on */
		if (!kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled)
			continue;

		defer_write_cb_handler	= kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_handler_ptr;
		defer_write_cb_data		= kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_data_ptr;

		/* Invoke the defer check call_back handler */
		if (defer_write_cb_handler)
			defer_write = defer_write_cb_handler(kq_fd->fd.num, kq_fd->defer.write.pending_bytes, -1, defer_write_cb_data, kq_base);

		/* Keep DEFER_WRITE on this FD */
		if (defer_write)
		{
			/* Save UNIX_TS of when we last check DEFER WRITE this FD */
			memcpy(&kq_fd->defer.write.check_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
			continue;
		}
		else
		{
			/* Remove from DEFER_CHECK list */
			node = node->next;
			EvKQBaseDeferWriteRemoveByKQFD(kq_base, kq_fd);
			goto read_loop_without_move;
		}

		continue;
	}


	return;
}
/**************************************************************************************************************************/


