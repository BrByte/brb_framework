/*
 * thrd_pool_base.c
 *
 *  Created on: 2013-12-11
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

#include <libbrb_core.h>

static int ThreadPoolBaseThreadsGrowIfNeeded(ThreadPoolBase *thread_pool);
static int ThreadPoolBaseThreadsLauch(ThreadPoolBase *thrd_base, int count);
static int ThreadPoolBaseNotifyPipeInit(ThreadPoolBase *thrd_base);
static EvBaseKQCBH ThreadPoolBaseNotifyPipeEventRead;

static void ThreadPoolInstanceExecute(ThreadPoolBase *thrd_base, ThreadPoolInstance *thrd_instance, int thrdid_onpool);
static void ThreadPoolInstanceBlockSignals(void);
static int ThreadPoolInstanceNotifyFinish(ThreadPoolBase *thread_pool, ThreadPoolInstanceJob *thread_job);
static ThreadInstanceEntryPoint ThreadPoolInstanceMainLoop;

/**************************************************************************************************************************/
ThreadPoolBase *ThreadPoolBaseNew(EvKQBase *ev_base, ThreadPoolBaseConfig *thread_pool_conf)
{
	ThreadPoolBase *thread_pool;

	/* Create a new thread pool and save EV_BASE reference */
	thread_pool = calloc(1, sizeof(ThreadPoolBase));
	thread_pool->ev_base = ev_base;

	/* Load LOG_BASE */
	if (thread_pool_conf->log_base)
		thread_pool->log_base	= thread_pool_conf->log_base;

	/* Copy flags */
	memcpy(&thread_pool->flags, &thread_pool_conf->flags, sizeof(thread_pool->flags));

	/* Set start and max worker count for this pool */
	thread_pool->config.worker_count_start	= (thread_pool_conf->worker_count_start ? thread_pool_conf->worker_count_start : 1);
	thread_pool->config.worker_count_grow	= (thread_pool_conf->worker_count_grow ? thread_pool_conf->worker_count_grow : thread_pool->config.worker_count_start);
	thread_pool->config.worker_count_max	= (thread_pool_conf->worker_count_max ? thread_pool_conf->worker_count_max : thread_pool->config.worker_count_start);
	thread_pool->config.job_max_count		= (thread_pool_conf->job_max_count ? thread_pool_conf->job_max_count : THREAD_POOL_JOBS_MAX_ENQUEUED_COUNT);
	thread_pool->config.worker_stack_sz		= thread_pool_conf->worker_stack_sz;

	/* Initialize INSTANCEs and JOBs MEMSLOT */
	MemSlotBaseInit(&thread_pool->instances.memslot, (sizeof(ThreadPoolInstance) + 1), (thread_pool->config.worker_count_max + 1), BRBDATA_THREAD_UNSAFE);
	MemSlotBaseInit(&thread_pool->jobs.memslot, (sizeof(ThreadPoolInstanceJob) + 1), (thread_pool->config.job_max_count + 1), BRBDATA_THREAD_SAFE);

	/* Initialize NOTIFY PIPE FD pair */
	if ((thread_pool->ev_base) && (thread_pool->flags.kevent_finish_notify))
		ThreadPoolBaseNotifyPipeInit(thread_pool);

	/* Launch THREADs */
	ThreadPoolBaseThreadsLauch(thread_pool, thread_pool->config.worker_count_start);

	return thread_pool;
}
/**************************************************************************************************************************/
void ThreadPoolBaseDestroy(ThreadPoolBase *thread_pool)
{
	ThreadPoolInstance *thrd_instance;
	DLinkedListNode *node;
	MemSlotBase *instance_memsl;
	int thread_alive;
	int i;

	/* Sanity check */
	if (!thread_pool)
		return;

	/* Grab THREAD MEM_SLOT */
	instance_memsl = &thread_pool->instances.memslot;

	/* Signal ALL threads to shutdown */
	for (node = instance_memsl->list[0].head; node; node = node->next)
	{
		/* Grab and set shutdown flag */
		thrd_instance						= MemSlotBaseSlotData(node->data);
		thrd_instance->flags.do_shutdown	= 1;
		continue;
	}

	/* TAG to wait thread shutdown */
	sync_wait:

	/* Block waiting shutdown - TODO: schedule timer to avoid blocking inside IO loop */
	for (thread_alive = 0, node = instance_memsl->list[0].head; node; node = node->next)
	{
		thrd_instance = MemSlotBaseSlotData(node->data);

		/* Thread is still alive */
		if (!thrd_instance->flags.done_shutdown)
		{
			thread_alive = 1;
			break;
		}

		continue;
	}

	/* Some threads are still alive, keep waiting */
	if (thread_alive)
		goto sync_wait;

	/* Clean MEM_SLOT_BASE of THREAD_INSTANCE pool */
	MemSlotBaseClean(&thread_pool->instances.memslot);
	MemSlotBaseClean(&thread_pool->jobs.memslot);

	free(thread_pool);

	return;
}
/**************************************************************************************************************************/
int ThreadPoolAllBusy(ThreadPoolBase *thread_pool)
{
	ThreadPoolInstance *thrd_instance;
	int i;

	/* Start first worker threads */
	for (i = 0; i < thread_pool->instances.memslot.list[0].size; i++)
	{
		/* Grab instance from arena and launch */
		thrd_instance = MemSlotBaseSlotGrabByID(&thread_pool->instances.memslot, i);

		/* This thread is FREE, return FALSE */
		if (THREAD_INSTANCE_STATE_FREE == thrd_instance->thread_state)
			return 0;

		continue;
	}

	/* ALL busy, return TRUE */
	return 1;
}
/**************************************************************************************************************************/
int ThreadPoolJobCancel(ThreadPoolBase *thread_pool, int job_id)
{
	ThreadPoolInstanceJob *thread_job;

	/* Sanity check */
	if (job_id < 0)
		return 0;

	/* Grab job data and MARK as CANCELLED */
	thread_job = MemSlotBaseSlotGrabByID(&thread_pool->jobs.memslot, job_id);
	thread_job->flags.cancelled = 1;

	return 1;
}
/**************************************************************************************************************************/
int ThreadPoolJobEnqueue(ThreadPoolBase *thread_pool, ThreadPoolJobProto *job_proto)
{
	ThreadPoolInstanceJob *thread_job;
	int local_udata_sz;

	/* Sanitize */
	if (!thread_pool)
		return -1;

	EvKQBase *ev_base		= thread_pool->ev_base;

	/* Grow THREAD POOL if NEEDED */
	ThreadPoolBaseThreadsGrowIfNeeded(thread_pool);

	/* Request a JOB SLOT from MEM_SLOT */
	thread_job = MemSlotBaseSlotGrabAndLeaveLocked(&thread_pool->jobs.memslot);

	/* No more THREAD_JOB slots - No need to UNLOCK */
	if (!thread_job)
	{
		KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Too many enqueued JOBs [%ld]\n",  MemSlotBaseSlotListSizeAll(&thread_pool->jobs.memslot));
		return -1;
	}

	assert(!thread_job->flags.in_use);
	memset(thread_job, 0, sizeof(ThreadPoolInstanceJob));

	/* Fill in JOB data from JOB_PROTO */
	thread_job->parent_base							= thread_pool;
	thread_job->job_id								= MemSlotBaseSlotGetID((char*)thread_job);
	thread_job->retval_type							= job_proto->retval_type;
	thread_job->job_callback.job_cbdata				= job_proto->job_cbdata;
	thread_job->finish_callback.cbh_ptr				= job_proto->job_finish_cbh_ptr;
	thread_job->finish_callback.cbdata				= job_proto->job_finish_cbdata;

	/* Load simple user data */
	thread_job->user_data							= job_proto->user_data;
	thread_job->user_long							= job_proto->user_long;
	thread_job->user_int							= job_proto->user_int;

	/* Assign generic job, correct function casting will be done inside thread loop - Mark flags as IN_USE */
	thread_job->job_callback.cb_handler.ret_none	= job_proto->job_cbh_ptr;
	thread_job->flags.in_use						= 1;

	/* Save ENQUEUE_TV */
	if (ev_base)
		memcpy(&thread_job->tv.enqueue, &ev_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* If there is any UDATA, copy it to job context */
	if (job_proto->udata_sz)
	{
		/* Adjust user_data size, if any and copy to internal context buffer */
		local_udata_sz = (job_proto->udata_sz < THREAD_POOL_JOBS_MAX_USERDATA_SIZE) ? job_proto->udata_sz : (THREAD_POOL_JOBS_MAX_USERDATA_SIZE - 1);
		memcpy(&thread_job->user_data_buf, job_proto->udata, local_udata_sz);
	}

	KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Enqueued JOB_ID [%d]\n", thread_job->job_id);

	/* Release the FUCKING LOCK and BROADCAST COND_SIGNAL to the thread POOL */
	MemSlotBaseUnlock(&thread_pool->jobs.memslot);
	pthread_cond_signal(&thread_pool->jobs.memslot.thrd_cond.cond);

	return thread_job->job_id;
}
/**************************************************************************************************************************/
int ThreadPoolJobSimple(ThreadPoolBase *thread_pool, ThreadInstanceJobCB_Generic *job_cbh_ptr, ThreadInstanceJobFinishCB *job_finish_cbh_ptr, int retval_type, void *cb_data, void *cb_finish, void *user_data)
{
	ThreadPoolJobProto job_proto;
	int thrd_id;

	/* Clean up stack */
	memset(&job_proto, 0, sizeof(ThreadPoolJobProto));

	/* Now launch a thread job to prepare this table IO */
	job_proto.retval_type			= retval_type;
	job_proto.job_cbh_ptr			= job_cbh_ptr;
	job_proto.job_cbdata			= cb_data;

	job_proto.job_finish_cbh_ptr	= job_finish_cbh_ptr;
	job_proto.job_finish_cbdata		= cb_finish;
	job_proto.user_data				= user_data;

	/* Launch job, use id to lock */
	thrd_id 						= ThreadPoolJobEnqueue(thread_pool, &job_proto);

	return thrd_id;
}
/**************************************************************************************************************************/
int ThreadPoolJobConsumeReplies(ThreadPoolBase *thread_pool)
{
	ThreadPoolInstanceJob *thread_job;
	int consume_count = 0;

	/* Consume all replies */
	while (1)
	{
		thread_job = MemSlotBaseSlotPointToHead(&thread_pool->jobs.memslot, THREAD_LIST_JOB_DONE);

		/* Nothing else to dispatch, STOP */
		if (!thread_job)
		{
			KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Finished consuming [%d] REPLIEs\n", consume_count);
			break;
		}

		assert(!thread_job->flags.in_use);
		assert(thread_job->flags.finished);

		/* Notify finish CALLBACK, if we have not been canceled */
		if ((thread_job->finish_callback.cbh_ptr) && (!thread_job->flags.cancelled))
			thread_job->finish_callback.cbh_ptr(thread_job, (thread_job->finish_callback.cbdata ? thread_job->finish_callback.cbdata : thread_job->job_callback.job_cbdata));

		KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Main THREAD - Invoked CB_FUNC for JOB_ID [%d] - CB [%p]/[%p]/[%p]\n",
				thread_job->job_id, thread_job->finish_callback.cbh_ptr, thread_job->finish_callback.cbdata, thread_job->job_callback.job_cbdata);

		/* Free used slot for this job */
		MemSlotBaseSlotFree(&thread_pool->jobs.memslot, thread_job);
		consume_count++;
		continue;
	}

	KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Main THREAD - Consumed [%d] replies\n", consume_count);
	return consume_count;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int ThreadPoolBaseThreadsGrowIfNeeded(ThreadPoolBase *thrd_base)
{
	int all_busy;

	/* Maximum size reached, bail out */
	if (thrd_base->instances.memslot.list[0].size >= thrd_base->config.worker_count_max)
		return 0;

	/* Check if all threads are busy */
	all_busy = ThreadPoolAllBusy(thrd_base);

	/* No need to grow, we have free workers */
	if (!all_busy)
		return 0;

	KQBASE_LOG_PRINTF(thrd_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "THREAD_POOL growing from [%d] to [%d] workers\n",
			thrd_base->instances.memslot.list[0].size, (thrd_base->instances.memslot.list[0].size + thrd_base->config.worker_count_grow));

	/* Launch a few more threads */
	ThreadPoolBaseThreadsLauch(thrd_base, thrd_base->config.worker_count_grow);

	return 1;
}
/**************************************************************************************************************************/
static int ThreadPoolBaseThreadsLauch(ThreadPoolBase *thrd_base, int count)
{
	ThreadPoolInstance *thrd_instance;
	int i;

	/* Start first worker threads */
	for (i = 0; i < count; i++)
	{
		/* Grab instance from arena and launch */
		thrd_instance = MemSlotBaseSlotGrab(&thrd_base->instances.memslot);

		/* Failed to grab slot for instance */
		if (!thrd_instance)
			break;

		/* Execute THREAD */
		ThreadPoolInstanceExecute(thrd_base, thrd_instance, i);
		continue;
	}

	return count;
}
/**************************************************************************************************************************/
static int ThreadPoolBaseNotifyPipeInit(ThreadPoolBase *thrd_base)
{
#if !defined(linux)
	/* Initialize done pipe signal */
	pipe(thrd_base->notify_pipe);
#endif
	/* Set description */
	EvKQBaseFDDescriptionSetByFD(thrd_base->ev_base, thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_MAIN], "BRB_THREAD - Completion PIPE - MAIN");
	EvKQBaseFDDescriptionSetByFD(thrd_base->ev_base, thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_THREADS], "BRB_THREAD - Completion PIPE - MAIN");

	/* Set it to non_blocking */
	EvKQBaseSocketSetNonBlock(thrd_base->ev_base, thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_MAIN]);
	EvKQBaseSocketSetNonBlock(thrd_base->ev_base, thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_THREADS]);

	/* Set CLOSE_ON_EXEC */
	EvKQBaseSocketSetCloseOnExec(thrd_base->ev_base, thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_MAIN]);
	EvKQBaseSocketSetCloseOnExec(thrd_base->ev_base, thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_THREADS]);

	/* Schedule READ EVENT for NOTIFY_PIPE */
	EvKQBaseSetEvent(thrd_base->ev_base, thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_MAIN], COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, ThreadPoolBaseNotifyPipeEventRead, thrd_base);

	KQBASE_LOG_PRINTF(thrd_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "MAIN_FD [%d] - THREADS_FD [%d]\n",
			thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_MAIN], thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_THREADS]);

	return 1;
}
/**************************************************************************************************************************/
static int ThreadPoolBaseNotifyPipeEventRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	char read_buf[65535];
	int packet_count;
	int read_sz;
	int i;

	EvKQBase *ev_base						= base_ptr;
	ThreadPoolBase *thrd_base				= cb_data;
	ThreadPoolNotifyData *notify_data_arr	= (ThreadPoolNotifyData*)&read_buf;

	/* Drain PIPE data */
	read_sz			= read(fd, notify_data_arr, (sizeof(read_buf) - 1));
	packet_count	= (read_sz / sizeof(ThreadPoolNotifyData));

	KQBASE_LOG_PRINTF(thrd_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CAN_READ_SZ [%d] - READ_SZ [%d] - PKT_COUNT [%d] - Finish NOTIFY\n",
			fd, can_read_sz, read_sz, packet_count);

	/* Consume ALL replies */
	ThreadPoolJobConsumeReplies(thrd_base);

	/* Reschedule READ EVENT for NOTIFY_PIPE */
	EvKQBaseSetEvent(thrd_base->ev_base, thrd_base->notify_pipe[THREAD_NOTIFY_PIPE_MAIN], COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, ThreadPoolBaseNotifyPipeEventRead, thrd_base);

	return 1;
}
/**************************************************************************************************************************/
static void ThreadPoolInstanceExecute(ThreadPoolBase *thrd_base, ThreadPoolInstance *thrd_instance, int thrdid_onpool)
{
	int op_status;
	struct sched_param globsched;

	/* Set schedule priority 1 for thread 0 */
	globsched.sched_priority	= 1;
	pthread_t main_thread		= pthread_self();

	/* Save local data and set THREAD state */
	thrd_instance->thread_id_pool	= thrdid_onpool;
	thrd_instance->thread_pool		= thrd_base;
	thrd_instance->thread_state		= THREAD_INSTANCE_STATE_STARTING;

	/* Set schedule parameters */
	pthread_setschedparam(main_thread, SCHED_OTHER, &globsched);

	/* Initialize attributes and set scope */
	pthread_attr_init(&thrd_instance->thread_attr);
	pthread_attr_setscope(&thrd_instance->thread_attr, PTHREAD_SCOPE_SYSTEM);

	/* Set schedule priority 3 for other threads */
	globsched.sched_priority = 3;
	pthread_attr_setschedparam(&thrd_instance->thread_attr, &globsched);

	/* Give each thread a bigger 512KB stack, should be more than sufficient */
	if (thrd_base->config.worker_stack_sz > 0)
		pthread_attr_setstacksize(&thrd_instance->thread_attr, thrd_base->config.worker_stack_sz);

	/* Launch thread */
	op_status = pthread_create(&thrd_instance->thread_id_sys, &thrd_instance->thread_attr, (ThreadInstanceEntryPoint*)ThreadPoolInstanceMainLoop, (void*)thrd_instance);

	/* Thread launch failed */
	if (op_status != 0)
		thrd_instance->thread_state	= THREAD_INSTANCE_STATE_FAILED;

	return;
}
/**************************************************************************************************************************/
static void ThreadPoolInstanceBlockSignals(void)
{
	sigset_t new;

	sigfillset(&new);
	pthread_sigmask(SIG_BLOCK, &new, NULL);

	return;
}
/**************************************************************************************************************************/
static int ThreadPoolInstanceNotifyFinish(ThreadPoolBase *thread_pool, ThreadPoolInstanceJob *thread_job)
{
	ThreadPoolNotifyData notify_data;
	int op_status;

	/* Cleanup and FILL notify DATA */
	memset(&notify_data, 0, sizeof(ThreadPoolNotifyData));
	notify_data.job_id		= thread_job->job_id;
	notify_data.thrd_id 	= thread_job->run_thread_id;

	/* Write to NOTIFY_PIPE */
	op_status 				= write(thread_pool->notify_pipe[THREAD_NOTIFY_PIPE_THREADS], (char*)&notify_data, sizeof(ThreadPoolNotifyData));

	/* Data can be destroyed in write, ThreadPoolBaseNotifyPipeEventRead -> ThreadPoolJobConsumeReplies -> MemSlotBaseSlotFree */
	KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Thread [%d] - Finished JOB_ID [%d] - OP_STATUS [%d]\n",
			notify_data.thrd_id, notify_data.job_id, op_status);

	return op_status;
}
/**************************************************************************************************************************/
static void *ThreadPoolInstanceMainLoop(void *thread_instance_ptr)
{
	/* Grab instance PTR and base pool back */
	ThreadPoolInstance *thread_instance	= thread_instance_ptr;
	ThreadPoolBase *thread_pool			= thread_instance->thread_pool;
	EvKQBase *ev_base					= thread_pool->ev_base;
	ThreadPoolInstanceJob *thread_job	= NULL;

	/* DETTACH thread */
	pthread_detach(pthread_self());

	/* Make sure to ignore signals which may possibly get sent to the parent thread.  Causes havoc with mutex's and condition waits otherwise */
	ThreadPoolInstanceBlockSignals();

	/* Jump into main loop */
	while (1)
	{
		/* Set THREAD state and lock JOB list MUTEX */
		thread_instance->thread_state		= THREAD_INSTANCE_STATE_FREE;
		pthread_mutex_lock(&thread_pool->jobs.memslot.thrd_cond.mutex);

		/* Create condition to block for */
		while (!thread_pool->jobs.memslot.list[THREAD_LIST_JOB_PENDING].head)
		{
			KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Hello from thread context [%d]-[%d] - Blocking at pthread_cond_wait\n",
					thread_instance->thread_id_pool, thread_instance->thread_id_sys);

			/* Block waiting for condition */
			pthread_cond_wait(&thread_pool->jobs.memslot.thrd_cond.cond, &thread_pool->jobs.memslot.thrd_cond.mutex);
		}

		/* Grab and remove this job from shared job list and unlock shared job request list MUTEX */
		thread_job = MemSlotBaseSlotPointToHeadAndSwitchListID(&thread_pool->jobs.memslot, THREAD_LIST_JOB_PENDING, THREAD_LIST_JOB_WORKING);
		assert(thread_job);

		KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Thread [%d] - Waking up to dispatch JOB_ID [%d] - IN_USE [%d]\n",
						thread_instance->thread_id_pool, thread_job->job_id, thread_job->flags.in_use);

		pthread_mutex_unlock(&thread_pool->jobs.memslot.thrd_cond.mutex);

		/* Set THREAD state */
		assert(thread_job->flags.in_use);
		thread_instance->thread_state		= THREAD_INSTANCE_STATE_BUSY;

		/* Save BEGIN TV */
		if (ev_base)
			memcpy(&thread_job->tv.begin, &ev_base->stats.cur_invoke_tv, sizeof(struct timeval));

		/* Flag request for a graceful shutdown */
		if (thread_instance->flags.do_shutdown)
			break;

		/* Job has been canceled */
		if (thread_job->flags.cancelled)
			goto clean_up;

		/* Keep a hint of which thread executed this job */
		thread_job->run_thread_id	= thread_instance->thread_id_pool;

		/* Job DO NOT want return value, run and leave */
		if (THREAD_JOB_RETVAL_NONE == thread_job->retval_type)
		{
			/* Execute work */
			thread_job->job_callback.cb_handler.ret_none(thread_job, thread_job->job_callback.job_cbdata);
			goto clean_up;
		}

		/* Job WANTs return value from HANDLER, decide what type is and cast invoke with correct PROTO - Invoke BASED on RET_VAL_TYPE */
		switch(thread_job->retval_type)
		{
		case THREAD_JOB_RETVAL_INT:
		{
			/* Execute work */
			thread_job->job_retvalue.int_value = thread_job->job_callback.cb_handler.ret_int(thread_job, thread_job->job_callback.job_cbdata);
			break;
		}
		case THREAD_JOB_RETVAL_LONG:
		{
			/* Execute work */
			thread_job->job_retvalue.long_value = thread_job->job_callback.cb_handler.ret_long(thread_job, thread_job->job_callback.job_cbdata);
			break;
		}
		case THREAD_JOB_RETVAL_PTR:
		{
			/* Execute work */
			thread_job->job_retvalue.ptr_value = thread_job->job_callback.cb_handler.ret_ptr(thread_job, thread_job->job_callback.job_cbdata);
			break;
		}
		case THREAD_JOB_RETVAL_FLOAT:
		{
			/* Execute work */
			thread_job->job_retvalue.float_value = thread_job->job_callback.cb_handler.ret_float(thread_job, thread_job->job_callback.job_cbdata);
			break;
		}
		}

		/* Save FINISH TV */
		if (ev_base)
			memcpy(&thread_job->tv.finish, &ev_base->stats.cur_invoke_tv, sizeof(struct timeval));

		/* Not in USE anymore */
		thread_job->flags.in_use	= 0;
		thread_job->flags.finished	= 1;

		KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Thread ID [%d] - Finished JOB_ID [%d] - NOTIFY BEGIN\n",
				thread_instance->thread_id_pool, thread_job->job_id);

		/* Add into done queue for further caller examination - WARNING: DO NOT TOUCH THREAD_JOB ANYMORE ONCE YOU DO THIS LIST SWITCH */
		MemSlotBaseSlotListIDSwitchToTail(&thread_pool->jobs.memslot, thread_job->job_id, THREAD_LIST_JOB_DONE);

		/* Write NOTIFY_PIPE to tell MAIN_THREAD we are FINISHED */
		if ((thread_pool->ev_base) && (thread_pool->flags.kevent_finish_notify))
			ThreadPoolInstanceNotifyFinish(thread_pool, thread_job);

		/* Set THREAD state */
		thread_instance->thread_state		= THREAD_INSTANCE_STATE_DONE;
		continue;

		/* TAG to cleanup CANCELLED or NO_RETURN JOBs */
		clean_up:

		/* Save FINISH TV */
		if (ev_base)
			memcpy(&thread_job->tv.finish, &ev_base->stats.cur_invoke_tv, sizeof(struct timeval));

		/* Not in USE anymore and FINISHED */
		thread_job->flags.in_use	= 0;
		thread_job->flags.finished	= 1;

		KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Thread ID [%d] - Finished JOB_ID [%d] - CLEAN UP\n",
				thread_instance->thread_id_pool, thread_job->job_id);

		/* Release SLOT */
		MemSlotBaseSlotFree(&thread_pool->jobs.memslot, thread_job);

		/* Set THREAD state */
		thread_instance->thread_state		= THREAD_INSTANCE_STATE_DONE;
		continue;
	}

	KQBASE_LOG_PRINTF(thread_pool->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Thread ID [%d] - Shutting down\n", thread_instance->thread_id_sys);

	/* Mark shutdown as DONE */
	thread_instance->thread_state			= THREAD_INSTANCE_STATE_SHUTDOWN;
	thread_instance->flags.done_shutdown	= 1;

	return 0;
}
/**************************************************************************************************************************/
