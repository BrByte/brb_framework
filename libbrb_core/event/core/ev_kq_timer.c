/*
 * ev_kq_timer.c
 *
 *  Created on: 2012-08-31
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

#include "../include/libbrb_core.h"

static void EvKQBaseEnqueueTimerChg(EvKQBase *kq_base, unsigned int timer_id, int interval, int action, void *udata);


/**************************************************************************************************************************/
void EvKQBaseTimerArenaNew(EvKQBase *kq_base, int max_timer_count)
{
	/* Create new SLOT_BASE to hold timers */
	MemSlotBaseInit(&kq_base->timer.memslot, sizeof(EvBaseKQTimer), 65535, BRBDATA_THREAD_UNSAFE);
	return;
}
/**************************************************************************************************************************/
void EvKQBaseTimerArenaDestroy(EvKQBase *kq_base)
{
	/* Clear timer arena */
	MemSlotBaseClean(&kq_base->timer.memslot);
	return;
}
/**************************************************************************************************************************/
EvBaseKQTimer *EvKQBaseTimerNewFromArena(EvKQBase *kq_base)
{
	EvBaseKQTimer *kq_timer;

	/* Grab a new timer slot */
	kq_timer = MemSlotBaseSlotGrab(&kq_base->timer.memslot);

	/* Unable to create more timers */
	if (!kq_timer)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Unable to create new timer. Total [%ld]\n",
				MemSlotBaseSlotListSizeAll(&kq_base->timer.memslot));

		return NULL;
	}

	/* Initialize timer data */
	kq_timer->timer_id		= MemSlotBaseSlotGetID(kq_timer);
	kq_timer->flags.in_use	= 1;

	return kq_timer;
}
/**************************************************************************************************************************/
EvBaseKQTimer *EvKQBaseTimerGrabFromArena(EvKQBase *kq_base, int timer_id)
{
	EvBaseKQTimer *kq_timer;
	MemArena *slot_arena;

	/* Sanity check */
	if (timer_id < 0)
		return NULL;

	/* Grab timer slot */
	kq_timer = MemSlotBaseSlotGrabByID(&kq_base->timer.memslot, timer_id);

	/* Inactive TIMER ID */
	if (!kq_timer->flags.in_use)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "Trying to grab INACTIVE TIMER_ID [%d / %d]\n", timer_id, kq_timer->timer_id);

		/* Release from internal arena */
		slot_arena = kq_base->timer.memslot.arena;
		MemArenaReleaseByID(slot_arena, timer_id);
		return NULL;
	}

	return kq_timer;
}
/**************************************************************************************************************************/
int EvKQBaseTimerReleaseFromArena(EvKQBase *kq_base, EvBaseKQTimer *kq_timer)
{
	int timer_id = kq_timer->timer_id;

	/* Clean up and free timer SLOT */
	memset(kq_timer, 0, sizeof(EvBaseKQTimer));
	MemSlotBaseSlotFree(&kq_base->timer.memslot, kq_timer);
	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int EvKQBaseTimerAdd(EvKQBase *kq_base, int action, int interval_ms, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQTimer *kq_timer;

	/* Sanity check */
	if ((!kq_base) || (!cb_handler) || (interval_ms < 0))
		return -1;

	/* Action MUST be ADD_VOLATILE or ADD_PERSIST */
	if ((COMM_ACTION_ADD_VOLATILE != action) && (COMM_ACTION_ADD_PERSIST != action))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Invalid action [%d] for timer add\n", action);
		return -1;
	}

	/* Grab a new timer slot from arena */
	kq_timer = EvKQBaseTimerNewFromArena(kq_base);

	/* Unable to create new timer */
	if (!kq_timer)
		return -1;

	BRB_ASSERT_FMT(kq_base, (kq_timer->flags.in_use), "KQ_TIMER [%d] - Not in use!", kq_timer->timer_id);
	BRB_ASSERT_FMT(kq_base, (!kq_timer->flags.active), "KQ_TIMER [%d] - Already active!", kq_timer->timer_id);

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Added [%s] TIMER_ID [%d] with interval [%d] msec\n",
			((COMM_ACTION_ADD_VOLATILE == action) ? "VOLATILE" : "PERSIST"), kq_timer->timer_id, interval_ms);

	/* Fill in TIMER data */
	kq_timer->cb_handler.timer		= cb_handler;
	kq_timer->cb_handler.timer_data	= cb_data;
	kq_timer->timer_msec			= interval_ms;
	kq_timer->flags.active			= 1;
	kq_timer->flags.enabled			= 1;
	kq_timer->flags.persist			= ((COMM_ACTION_ADD_VOLATILE == action) ? 0 : 1);

	/* Enqueue event change - Send kq_base as user_data */
	EvKQBaseEnqueueTimerChg(kq_base, kq_timer->timer_id, interval_ms, action, kq_base);

	/* Adjust IO_LOOP timers */
	EvKQBaseAdjustIOLoopTimeout(kq_base, interval_ms);
	return kq_timer->timer_id;
}
/**************************************************************************************************************************/
void EvKQBaseTimerCtl(EvKQBase *kq_base, int timer_id, int action)
{
	EvBaseKQTimer *kq_timer;

	/* Sanity check */
	if (timer_id < 0)
		return;

	/* Grab kq_timer from table */
	kq_timer = EvKQBaseTimerGrabFromArena(kq_base, timer_id);

	/* Un_existant timer, bail out */
	if (!kq_timer)
		return;

	BRB_ASSERT_FMT(kq_base, (kq_timer->flags.in_use), "KQ_TIMER [%d] - Not in use!", kq_timer->timer_id);
	BRB_ASSERT_FMT(kq_base, (kq_timer->flags.active), "KQ_TIMER [%d] - Not active!", kq_timer->timer_id);
	BRB_ASSERT_FMT(kq_base, (timer_id == kq_timer->timer_id), "KQ_TIMER [%d] - TIMER_ID [%d] - Mismatch!", kq_timer->timer_id, timer_id);

	/* Do the requested ACTION */
	switch (action)
	{
	case COMM_ACTION_DISABLE:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "TIMER_ID [%d] - Disabled\n", kq_timer->timer_id);

		/* Timer is active and not enabled */
		if (kq_timer->flags.enabled)
		{
			/* Enqueue event change - Send kq_base as user_data */
			EvKQBaseEnqueueTimerChg(kq_base, timer_id, kq_timer->timer_msec, action, kq_base);
			kq_timer->flags.enabled	= 0;
		}

		break;
	}
	case COMM_ACTION_ENABLE:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "TIMER_ID [%d] - Enabled\n", kq_timer->timer_id);

		/* Timer is active and not enabled */
		if (!kq_timer->flags.enabled)
		{
			/* Enqueue event change - Send kq_base as user_data */
			EvKQBaseEnqueueTimerChg(kq_base, timer_id, kq_timer->timer_msec, action, kq_base);

			/* Adjust IO_LOOP timers */
			EvKQBaseAdjustIOLoopTimeout(kq_base, kq_timer->timer_msec);
			kq_timer->flags.enabled	= 1;
		}

		break;
	}
	case COMM_ACTION_DELETE:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "TIMER_ID [%d] - Deleted\n", kq_timer->timer_id);

		/* Enqueue event change - Send kq_base as user_data */
		EvKQBaseEnqueueTimerChg(kq_base, timer_id, kq_timer->timer_msec, action, kq_base);
		EvKQBaseTimerReleaseFromArena(kq_base, kq_timer);
		break;
	}
	}

	return;
}
/**************************************************************************************************************************/
int EvKQBaseTimerDispatch(EvKQBase *kq_base, int timer_id, int int_data)
{
	EvBaseKQTimer *kq_timer;
	EvBaseKQCBH *timer_cb_handler;
	void *timer_cb_data;

	/* Grab TIMER from reference table */
	kq_timer = EvKQBaseTimerGrabFromArena(kq_base, timer_id);

	/* Someone might have DELETED or DISABLED this TIMER in this very same IO loop, so check flags before moving on */
	if ((!kq_timer) || (!kq_timer->flags.active) || (!kq_timer->flags.enabled))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "TIMER_ID [%d] - Called to dispatch UNEXISTANT timer\n", timer_id);
		return 0;
	}

	if ((!kq_timer->flags.active) || (!kq_timer->flags.enabled))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "TIMER_ID [%d] - Called to dispatch INACTIVE timer [%d / %d]\n",
				timer_id, kq_timer->flags.active, kq_timer->flags.enabled);
		return 0;
	}

	/* Grab CB_HANDLER and DATA */
	timer_cb_handler	= kq_timer->cb_handler.timer;
	timer_cb_data		= kq_timer->cb_handler.timer_data;

	if (!timer_cb_handler)
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "TIMER_ID [%d] - TIMER with NULL CB_HANDLER\n", timer_id);
	else
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "TIMER_ID [%d] - Will invoke CB_FUNCTION at [%p] - PERSIST [%d] - ENABLED [%d] - ACTIVE [%d]\n",
				timer_id, timer_cb_handler, kq_timer->flags.persist, kq_timer->flags.enabled, kq_timer->flags.active);

	/* One shot timer, remove it after invoke - DO NOT need to remove from KQUEUE, as it was EV_SETd with ONE_SHOT */
	if (!kq_timer->flags.persist)
		EvKQBaseTimerReleaseFromArena(kq_base, kq_timer);

	/* Invoke the CALLBACK handler */
	if (timer_cb_handler)
		timer_cb_handler(timer_id, int_data, -1, timer_cb_data, kq_base);

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseTimeValSubMsec(struct timeval *when, struct timeval *now)
{
	return ((now->tv_sec - when->tv_sec) * 1000) +	((now->tv_usec - when->tv_usec) / 1000);
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void EvKQBaseEnqueueTimerChg(EvKQBase *kq_base, unsigned int timer_id, int interval_ms, int action, void *udata)
{
	struct kevent *kev_ptr;

	/* Check if change list need to grow to save more filter changes */
	EVBASE_CHGLST_GROW_IF_NEEDED(kq_base);

	//kev_ptr = kq_base->ke_chg[ke_chg_idx].chg_arr + kq_base->ke_chg[ke_chg_idx].chg_arr_off;
	kev_ptr = &kq_base->ke_chg.chg_arr[(kq_base->ke_chg.chg_arr_off)];

	switch (action)
	{
	/***********************************************/
	case COMM_ACTION_DELETE:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)timer_id, EVFILT_TIMER, EV_DELETE, 0, interval_ms, udata);
		break;
	}
	/***********************************************/
	case COMM_ACTION_ADD_PERSIST:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)timer_id, EVFILT_TIMER, EV_ADD, 0, interval_ms, udata);
		break;
	}
	/***********************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)timer_id, EVFILT_TIMER, (EV_ADD | EV_ONESHOT), 0, interval_ms, udata);
		break;
	}
	/***********************************************/
	case COMM_ACTION_ENABLE:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)timer_id, EVFILT_TIMER, EV_ENABLE, 0, interval_ms, udata);
		break;
	}
	/***********************************************/
	case COMM_ACTION_DISABLE:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)timer_id, EVFILT_TIMER, EV_DISABLE, 0, interval_ms, udata);
		break;
	}
	/***********************************************/
	}

	/* Update chg_arr index */
	kq_base->ke_chg.chg_arr_off++;
	return;
}
/**************************************************************************************************************************/
