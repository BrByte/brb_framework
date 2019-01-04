/*
 * ev_kq_jobs.c
 *
 *  Created on: 2013-12-06
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

#include "../include/libbrb_ev_kq.h"

static EvKQQueuedJob *EvKQJobsAddInternal(EvKQBase *kq_base, int action, char *time_str, int ioloop_count, EvBaseKQJobCBH *job_cb_handler, void *job_cbdata);
static int EvKQJobsTimedParse(EvKQQueuedJob *kq_job, char *time_str);
static int EvKQJobsTimedWillDispatch(EvKQQueuedJob *kq_job, int time_mask, int week_day);
static int EvKQJobsDispatchTimed(EvKQBase *kq_base);

static EvBaseKQCBH EvKQJobsTimerEvent;

/**************************************************************************************************************************/
int EvKQJobsEngineInit(EvKQBase *kq_base, int max_job_count)
{
	/* Sanity check */
	if (!kq_base)
		return 0;

	/* Initialize MEM_SLOT to hold JOBs */
	MemSlotBaseInit(&kq_base->queued_job.memslot, (sizeof(EvKQQueuedJob) + 1), (max_job_count + 1),
			kq_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE);

	/* Add timed job timer */
	kq_base->queued_job.timer_id = -1;

	return 1;
}
/**************************************************************************************************************************/
int EvKQJobsEngineDestroy(EvKQBase *kq_base)
{
	/* Sanity check */
	if (!kq_base)
		return 0;

	/* Remove timed jobs timer */
	if (kq_base->queued_job.timer_id > -1)
	{
		EvKQBaseTimerCtl(kq_base, kq_base->queued_job.timer_id, COMM_ACTION_DELETE);
		kq_base->queued_job.timer_id = -1;
	}

	/* Clean up MEM_SLOTs */
	MemSlotBaseClean(&kq_base->queued_job.memslot);
	return 1;
}
/**************************************************************************************************************************/
int EvKQJobsDispatch(EvKQBase *kq_base)
{
	DLinkedList *list;
	DLinkedListNode *node;
	EvKQQueuedJob *kq_job;
	EvBaseKQJobCBH *job_cbh;
	void *job_cbdata;

	int dispatched_jobs = 0;

	/* JOB list is empty */
	if (MemSlotBaseIsEmptyList(&kq_base->queued_job.memslot, JOB_TYPE_IOLOOP))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_VERBOSE, LOGCOLOR_YELLOW, "EV_BASE [%p] - Nothing to dispatch\n", kq_base);
		return 0;
	}

	/* Point to JOB list HEAD */
	list	= &kq_base->queued_job.memslot.list[JOB_TYPE_IOLOOP];
	node	= list->head;

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Will dispatch [%d] jobs\n", list->size);

	/* Walk JOB list dispatching */
	while (node)
	{
		/* Grab JOB from MEM_SLOT */
		kq_job	= MemSlotBaseSlotData(node->data);
		node	= node->next;

		/* Make sure its ACTIVE */
		assert(kq_job->flags.active);

		/* Disabled, move on */
		if (!kq_job->flags.enabled)
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "JOB_ID [%d] - CUR_IOLOOP [%d] - IOLOOP_INVOKE_INTERVAL [%d] - Ignoring disabled JOB\n",
					kq_job->job.id, kq_job->job.count.ioloop_cur, kq_job->job.count.ioloop_target);
			continue;
		}

		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "JOB_ID [%d] - CUR_IOLOOP [%d] - IOLOOP_INVOKE_INTERVAL [%d]\n",
				kq_job->job.id, kq_job->job.count.ioloop_cur, kq_job->job.count.ioloop_target);

		/* Time to execute this JOB */
		if (kq_job->job.count.ioloop_cur >= kq_job->job.count.ioloop_target)
		{
			job_cbh		= kq_job->job.cb_func;
			job_cbdata	= kq_job->job.cb_data;

			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "JOB_ID [%d] - JOB_LOOP_CUR [%d] - JOB_LOOP_TARGET [%d] - Will dispatch CB at [%p]\n",
					kq_job->job.id, kq_job->job.count.ioloop_cur, kq_job->job.count.ioloop_target, job_cbh);

			/* Execute job */
			if (!kq_job->flags.canceled)
				job_cbh(kq_job, job_cbdata);

			/* Release this job */
			if (!kq_job->flags.persist)
			{
				/* Release KQ_JOB and clean IT */
				memset(kq_job, 0, sizeof(EvKQQueuedJob));
				MemSlotBaseSlotFree(&kq_base->queued_job.memslot, kq_job);
			}
			/* Zero out invoke count */
			else
				kq_job->job.count.ioloop_cur = 0;

			/* Increment dispatched jobs */
			dispatched_jobs++;
		}
		else
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "JOB_ID [%d] - JOB_LOOP_CUR [%d] - JOB_LOOP_TARGET [%d] - Couting...\n",
					kq_job->job.id, kq_job->job.count.ioloop_cur, kq_job->job.count.ioloop_target);

			kq_job->job.count.ioloop_cur++;
		}

		continue;
	}

	return dispatched_jobs;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int EvKQJobsAddTimed(EvKQBase *kq_base, int action, int day, char *time_str, EvBaseKQJobCBH *job_cb_handler, void *job_cbdata)
{
	EvKQQueuedJob *kq_job;
	int job_id;

	/* Sanity check */
	if ((!job_cb_handler) || (!time_str))
		return -1;

	if (day > JOB_TIME_WDAY_LAST_ITEM)
		return -1;

	/* Just volatile or persistent action */
	if ((action != JOB_ACTION_ADD_VOLATILE) && (action != JOB_ACTION_ADD_PERSIST))
		return -1;

	/* Add new JOB */
	kq_job = EvKQJobsAddInternal(kq_base, action, time_str, -1, job_cb_handler, job_cbdata);

	/* Too many jobs enqueued - Failed to grab a new JOB_ID, leave */
	if (!kq_job)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "EV_BASE [%p] - Unable to enqueue more JOBs\n", kq_base);
		return -1;
	}

	/* Save target day and leave */
	kq_job->job.time.target_day = day;
	return kq_job->job.id;
}
/**************************************************************************************************************************/
int EvKQJobsAdd(EvKQBase *kq_base, int action, int ioloop_count, EvBaseKQJobCBH *job_cb_handler, void *job_cbdata)
{
	EvKQQueuedJob *kq_job;

	/* Sanity check */
	if ((!job_cb_handler) || (ioloop_count < 0))
		return -1;

	/* Just volatile or persistent action */
	if ((action != JOB_ACTION_ADD_VOLATILE) && (action != JOB_ACTION_ADD_PERSIST))
		return -1;

	/* Add new JOB */
	kq_job = EvKQJobsAddInternal(kq_base, action, NULL, ioloop_count, job_cb_handler, job_cbdata);

	/* Too many jobs enqueued - Failed to grab a new JOB_ID, leave */
	if (!kq_job)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "EV_BASE [%p] - Unable to enqueue more JOBs\n", kq_base);
		return -1;
	}

	return kq_job->job.id;
}
/**************************************************************************************************************************/
int EvKQJobsCtl(EvKQBase *kq_base, int action, int kq_job_id)
{
	EvKQQueuedJob *kq_job;

	/* Uninitialized JOB_ID, bail out */
	if (kq_job_id < 0)
		return 0;

	/* Invalid action, bail out */
	if (action > JOB_ACTION_LASTITEM)
		return 0;

	/* Grab a new job from arena */
	kq_job = MemSlotBaseSlotGrabByID(&kq_base->queued_job.memslot, kq_job_id);
	assert(kq_job->flags.active);

	/* Switch on ACTION */
	switch (action)
	{
	case JOB_ACTION_DELETE:
	{
		/* Release KQ_JOB and clean IT */
		memset(kq_job, 0, sizeof(EvKQQueuedJob));
		MemSlotBaseSlotFree(&kq_base->queued_job.memslot, kq_job);
		return 1;
	}

	case JOB_ACTION_ENABLE:		kq_job->flags.enabled	= 1; return 1;
	case JOB_ACTION_DISABLE:	kq_job->flags.enabled	= 0; return 1;
	default:					assert(0);
	}

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
char *EvKQJobsDerefCBDataByID(EvKQBase *kq_base, int kq_job_id)
{
	EvKQQueuedJob *kq_job;
	char *ret_addr;

	/* Uninitialized JOB_ID, bail out */
	if (kq_job_id < 0)
		return NULL;

	/* Grab a new job from arena */
	kq_job		= MemSlotBaseSlotGrabByID(&kq_base->queued_job.memslot, kq_job_id);
	assert(kq_job->flags.active);

	/* Calculate return address */
	ret_addr	= (char*)&kq_job->generic_cbbuf;

	return ret_addr;
}
/**************************************************************************************************************************/
char *EvKQJobsDerefCBData(EvKQQueuedJob *kq_job)
{
	char *ret_addr;

	/* Point to RET_ADDR */
	ret_addr	= (char*)&kq_job->generic_cbbuf;

	return ret_addr;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static EvKQQueuedJob *EvKQJobsAddInternal(EvKQBase *kq_base, int action, char *time_str, int ioloop_count, EvBaseKQJobCBH *job_cb_handler, void *job_cbdata)
{
	EvKQQueuedJob *kq_job;
	int op_status;

	/* Grab a new job from arena */
	kq_job			= MemSlotBaseSlotGrab(&kq_base->queued_job.memslot);

	/* Too many jobs enqueued - Failed to grab a new JOB_ID, leave */
	if (!kq_job)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "EV_BASE [%p] - Unable to enqueue more JOBs\n", kq_base);
		return NULL;
	}

	/* Make sure not in USE and clean structure */
	assert(!kq_job->flags.active);
	memset(kq_job, 0, sizeof(EvKQQueuedJob));

	/* Populate KQ_JOB */
	kq_job->ev_base					= kq_base;
	kq_job->job.id					= MemSlotBaseSlotGetID(kq_job);
	kq_job->job.count.ioloop_target	= ioloop_count;
	kq_job->job.time.invoke_day		= -1;
	kq_job->job.cb_func				= job_cb_handler;
	kq_job->job.cb_data				= job_cbdata;
	kq_job->flags.persist			= ((JOB_ACTION_ADD_PERSIST == action) ? 1 : 0);
	kq_job->flags.active			= 1;
	kq_job->flags.enabled			= 1;

	/* This is a TIME event */
	if (time_str)
	{
		/* Now parse time string */
		op_status = EvKQJobsTimedParse(kq_job, time_str);

		/* Failed parsing */
		if (!op_status)
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "EV_BASE [%p] - Failed parsing time string [%s]\n", kq_base, time_str);

			/* Release KQ_JOB and clean IT */
			memset(kq_job, 0, sizeof(EvKQQueuedJob));
			MemSlotBaseSlotFree(&kq_base->queued_job.memslot, kq_job);
			return NULL;
		}

		/* Switch to TIMED list */
		MemSlotBaseSlotListIDSwitch(&kq_base->queued_job.memslot, MemSlotBaseSlotGetID(kq_job), JOB_TYPE_TIMED);
		assert(ioloop_count < 0);

		/* Add timed job timer */
		if (kq_base->queued_job.timer_id < 0)
			kq_base->queued_job.timer_id = EvKQBaseTimerAdd(kq_base, COMM_ACTION_ADD_PERSIST, 1000, EvKQJobsTimerEvent, NULL);

		/* Save time string and set as TIME_JOB */
		strlcpy((char*)&kq_job->job.time.str, time_str, sizeof(kq_job->job.time.str));
		kq_job->flags.job_timed			= 1;

		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EV_BASE [%p] - JOB_ID [%d] - Timed Job added to [%s]\n",
				kq_base, kq_job->job.id, kq_job->job.time.str);
	}
	else
	{
		/* Set as IOLOOP_JOB */
		kq_job->flags.job_ioloop		= 1;
		assert(ioloop_count >= 0);

		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "EV_BASE [%p] - JOB_ID [%d] - IO loop job added to run after [%d] IOLOOPs\n",
				kq_base, kq_job->job.id, kq_job->job.count.ioloop_target);
	}

	return kq_job;
}
/**************************************************************************************************************************/
static int EvKQJobsTimedParse(EvKQQueuedJob *kq_job, char *time_str)
{
	StringArray *time_strarr;
	int time_arrsz;
	int hour;
	int min;
	int sec;
	char *hour_str;
	char *min_str;
	char *sec_str;

	EvKQBase *kq_base	= kq_job->ev_base;

	/* Explode time string */
	time_strarr = StringArrayExplodeStr(time_str, ":", NULL, NULL);
	time_arrsz	= StringArrayGetElemCount(time_strarr);

	/* Invalid item count */
	if (time_arrsz != 3)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "EV_BASE [%p] - Invalid time string [%s]\n", kq_base, time_str);

		/* Destroy time string */
		StringArrayDestroy(time_strarr);
		return 0;
	}

	/* Grab data */
	hour_str	= StringArrayGetDataByPos(time_strarr, 0);
	min_str		= StringArrayGetDataByPos(time_strarr, 1);
	sec_str		= StringArrayGetDataByPos(time_strarr, 2);

	/* Convert data */
	hour		= atoi(hour_str);
	min			= atoi(min_str);
	sec			= atoi(sec_str);

	/* Destroy time string */
	StringArrayDestroy(time_strarr);

	/* Calculate TIME_MASK for this time */
	kq_job->job.time.mask = ((hour * 10000) + (min * 100) + sec);

	/* Invalid HOUR, MINUTE or SECOND */
	if ((hour < 0) || (hour > 24) || (min < 0) || (min > 60) || (sec < 0) || (sec > 60))
		return 0;

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "TIME_STR [%s] - HOUR [%02d] - MIN [%02d] - SEC [%02d] - MASK [%06d]\n",
			time_str, hour, min, sec, kq_job->job.time.mask);

	return 1;
}
/**************************************************************************************************************************/
static int EvKQJobsTimedWillDispatch(EvKQQueuedJob *kq_job, int time_mask, int week_day)
{
	EvKQBase *kq_base	= kq_job->ev_base;
	int time_delta		= (time_mask - kq_job->job.time.mask);
	int invoke_delta	= ((kq_job->job.time.invoke_ts > 0) ? (kq_base->stats.cur_invoke_ts_sec - kq_job->job.time.invoke_ts) : -1);

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "JOB_ID [%d] - JOB_TIME_MASK [%06d] - CUR_TIME_MASK [%06d] - TIME/INVOKE DELTA [%d / %d] - DAY [%d / %d]\n",
			kq_job->job.id, kq_job->job.time.mask, time_mask, time_delta, invoke_delta, week_day, kq_job->job.time.invoke_day);

	/* Disabled, move on */
	if (!kq_job->flags.enabled)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "JOB_ID [%d] - Disabled\n", kq_job->job.id);
		return 0;
	}

	/* Monday to Friday */
	if (JOB_TIME_WDAY_MONDAY_FRIDAY == kq_job->job.time.target_day)
	{
		if ((week_day < JOB_TIME_WDAY_MONDAY) || (week_day > JOB_TIME_WDAY_FRIDAY))
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "JOB_ID [%d] - Will not run - Wants MONDAY_FRIDAY - WEEK_DAY [%d]\n",
					kq_job->job.id, week_day);
			return 0;
		}
	}

	/* Not everyday, and today is not THE day! */
	if ((kq_job->job.time.target_day != JOB_TIME_WDAY_EVERYDAY) && (week_day != kq_job->job.time.target_day))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "JOB_ID [%d] - Not today - CUR [%d] - TARGET [%d]\n",
							kq_job->job.id, week_day, kq_job->job.time.target_day);

		return 0;
	}

	/* Time to execute this JOB */
	if ((time_delta >= 0) && (time_delta < 60))
	{
		/* Safety cap, invoked today */
		if (kq_job->job.time.invoke_day	== week_day)
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "JOB_ID [%d] - Will not invoke - Invoked TODAY [%d / %d]\n",
					kq_job->job.id, kq_job->job.time.invoke_day, week_day);
			return 0;
		}

		/* Safety cap, recently invoked */
		if ((invoke_delta > -1) && (invoke_delta < 120))
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "JOB_ID [%d] - Will not invoke - Too small DELTA [%d]\n",
					kq_job->job.id, invoke_delta);
			return 0;
		}

		/* Lets run!! */
		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
static int EvKQJobsDispatchTimed(EvKQBase *kq_base)
{
	DLinkedList *list;
	DLinkedListNode *node;
	EvKQQueuedJob *kq_job;
	EvBaseKQJobCBH *job_cbh;
	struct tm *cur_time;
	void *job_cbdata;
	int time_mask;
	int week_day;

	int dispatched_jobs = 0;

	/* JOB list is empty */
	if (MemSlotBaseIsEmptyList(&kq_base->queued_job.memslot, JOB_TYPE_TIMED))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Empty list [%d]\n", JOB_TYPE_TIMED);
		return 0;
	}

	/* Point to JOB list HEAD */
	list		= &kq_base->queued_job.memslot.list[JOB_TYPE_TIMED];
	node		= list->head;

	/* Grab local time, week day and calculate TIME_MASK */
	cur_time 	= localtime((time_t*) &kq_base->stats.cur_invoke_ts_sec);
	week_day	= cur_time->tm_wday;
	time_mask	= ((cur_time->tm_hour * 10000) + (cur_time->tm_min * 100) + cur_time->tm_sec);

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Will dispatch [%d] jobs - TIME [%06d]\n", list->size, time_mask);

	/* Walk JOB list dispatching */
	while (node)
	{
		/* Grab JOB from MEM_SLOT */
		kq_job	= MemSlotBaseSlotData(node->data);
		node	= node->next;

		/* Make sure its ACTIVE */
		assert(kq_job->flags.active);

		/* Time to execute this JOB */
		if (EvKQJobsTimedWillDispatch(kq_job, time_mask, week_day))
		{
			/* Calculate last invoke delta */
			job_cbh			= kq_job->job.cb_func;
			job_cbdata		= kq_job->job.cb_data;

			/* Execute job */
			if (!kq_job->flags.canceled)
				job_cbh(kq_job, job_cbdata);

			/* Release this job */
			if (!kq_job->flags.persist)
			{
				/* Release KQ_JOB and clean IT */
				memset(kq_job, 0, sizeof(EvKQQueuedJob));
				MemSlotBaseSlotFree(&kq_base->queued_job.memslot, kq_job);
			}
			/* Touch last invoke TS */
			else
			{
				kq_job->job.time.invoke_ts	= kq_base->stats.cur_invoke_ts_sec;
				kq_job->job.time.invoke_day	= week_day;
			}

			/* Increment dispatched jobs */
			dispatched_jobs++;
		}

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQJobsTimerEvent(int timer_id, int not_used, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *kq_base	= base_ptr;

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "TID [%d] - Jobs timer event\n", timer_id);
	EvKQJobsDispatchTimed(kq_base);

	return 1;
}
/**************************************************************************************************************************/
