/*
 * ev_kq_base.c
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

/* Private prototypes */
static int EvKQInvokeKQFDCallbacks(EvKQBase *kq_base, long invoke_ts);
static void EvKQBaseEnqueueEvChg(EvKQBase *kq_base, unsigned int fd, int ev_type, int action, void *udata);

/* Private event update prototypes */
static void EvKQBaseUpdateReadEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data);
static void EvKQBaseUpdateWriteEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data);
static void EvKQBaseUpdateFileMonEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data);
static void EvKQBaseUpdateEOFEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data);
static void EvKQBaseUpdateErrorEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data);
static void EvKQBaseUpdateDeferCheckReadEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data);
static void EvKQBaseUpdateDeferCheckWriteEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data);

static void EvKQBaseKEventInvoke(EvKQBase *kq_base);
static int EvKQBaseTimeSkewDetect(EvKQBase *kq_base, int timeout_ms);

/* Static global stuff */
pthread_mutex_t glob_giant_mutex			= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t glob_kqbase_loaded	= 0;

/**************************************************************************************************************************/
EvKQBase *EvKQBaseNew(EvKQBaseConf *kq_conf)
{
	EvKQBase *kq_base 	= NULL;
	int kqueue_fd	 	= 0;

	/* Lock global GIANT - Will initialize if first KQ_BASE created */
	MUTEX_LOCK(glob_giant_mutex, "EVBASE_GIANT_GLOB_MUTEX");

	/* First load for this process area, initialize some stuff */
	if (!glob_kqbase_loaded)
	{
		/* Mark as loaded */
		glob_kqbase_loaded = 1;

		/* Initialize SSH2 and SSL */
		libssh2_init (0);
		SSL_load_error_strings ();
		SSL_library_init ();
		OpenSSL_add_all_algorithms();
	}

	/* Unlock global GIANT */
	MUTEX_UNLOCK(glob_giant_mutex, "EVBASE_GIANT_GLOB_MUTEX");

	/* Invoke the kernel for a new k_queue base */
	kqueue_fd			= kqueue();
	glob_sinal_log_base	= NULL;

	/* Failed creating KQ_BASE, bail out */
	if (kqueue_fd < 0)
		return NULL;

	/* Create a new kqueue_event base */
	kq_base										= calloc(1, sizeof(EvKQBase));
	kq_base->kq_base							= kqueue_fd;
	kq_base->kq_thrd_id							= pthread_self();
	kq_base->flags.mt_engine					= ((kq_conf && KQ_BASE_MULTI_THREADED_ENGINE == kq_conf->engine_type) ? 1 : 0);
	kq_base->defer.interval_check_ms			= 200;
	kq_base->skew_min_detect_sec				= 30;

	/* Timeout timers */
	kq_base->timeout.tv_nsec					= KQEV_IOLOOP_MAXINTERVAL_NS;
	kq_base->timeout.tv_sec						= 0;

	/* Initialize incoming event array */
	kq_base->ev_arr.event_arr					= calloc(KQEV_ARR_GROW_STEP, sizeof(struct kevent));
	kq_base->ev_arr.event_arr_cap				= KQEV_ARR_GROW_STEP;

	/* Initialize inter_IO_loop change array */
	kq_base->ke_chg.chg_arr						= calloc(KQCHG_ARR_GROW_STEP, sizeof(struct kevent));
	kq_base->ke_chg.chg_arr_cap 				= KQCHG_ARR_GROW_STEP - 1;
	kq_base->ke_chg.chg_arr_off 				= 0;

	/* Load KQ_CONF into KQ_BASE */
	kq_base->kq_conf.arena.fd_start				= ((kq_conf && kq_conf->arena.fd_start > 0) ? kq_conf->arena.fd_start : 512);
	kq_base->kq_conf.arena.timer_max			= ((kq_conf && kq_conf->arena.timer_max > 0) ? kq_conf->arena.timer_max : 65535);
	kq_base->kq_conf.timeout.event_loop_ms		= ((kq_conf && kq_conf->timeout.event_loop_ms > 0) ? kq_conf->timeout.event_loop_ms : 50);
	kq_base->kq_conf.error_count_max			= ((kq_conf && kq_conf->error_count_max > 0) ? kq_conf->error_count_max : 10);
	kq_base->kq_conf.kq_thread.count_start		= ((kq_conf && kq_conf->kq_thread.count_start > 1) ? kq_conf->kq_thread.count_start : 2);
	kq_base->kq_conf.kq_thread.count_max		= ((kq_conf && kq_conf->kq_thread.count_max > 1) ? kq_conf->kq_thread.count_max : 2);
	kq_base->kq_conf.aio.max_slots				= ((kq_conf && kq_conf->aio.max_slots > 128) ? kq_conf->aio.max_slots : 128);
	kq_base->kq_conf.job.max_slots				= ((kq_conf && kq_conf->job.max_slots > 128) ? kq_conf->job.max_slots : KQJOB_DEFAULT_COUNT);
	kq_base->kq_conf.onoff.close_linger			= ((kq_conf && kq_conf->onoff.close_linger) ? 1 : 0);

	/* Get monotonic TIMESPEC */
	clock_gettime(CLOCK_MONOTONIC, &kq_base->stats.monotonic_tp);

	/* Initialize DEFER and REG_OBJ lists */
	DLinkedListInit(&kq_base->defer.read_list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&kq_base->defer.write_list, BRBDATA_THREAD_UNSAFE);
	DLinkedListInit(&kq_base->reg_obj.list, BRBDATA_THREAD_UNSAFE);

	/* Initialize TIMER and FD arenas */
	EvKQBaseTimerArenaNew(kq_base, kq_base->kq_conf.arena.timer_max);
	EvKQBaseFDArenaNew(kq_base);

	/* Initialize JOB engine */
	EvKQJobsEngineInit(kq_base, kq_base->kq_conf.job.max_slots);

	/* Initialize EV_AIO_QUEUE */
	EvAIOReqQueueInit(kq_base, &kq_base->aio.queue, (kq_conf ? kq_conf->aio.max_slots : 1024),
			(kq_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), AIOREQ_QUEUE_SLOTTED);

	return kq_base;
}
/**************************************************************************************************************************/
void EvKQBaseDestroy(EvKQBase *kq_base)
{
	if (!kq_base)
		return;

	/* Destroy all upper layer objects */
	EvKQBaseObjectDestroyAll(kq_base);

	/* Destroy pending JOBs */
	EvKQJobsEngineDestroy(kq_base);

	/* Destroy FD and timer ARENA */
	EvKQBaseFDArenaDestroy(kq_base);

	/* IMPORTANT - IMPORTANT - IMPORTANT */
	/* Some internal resources, like jobs, use timers, so, timers MUST BE destroyed AFTER */
	EvKQBaseTimerArenaDestroy(kq_base);

	/* Destroy internal EV_AIO queue */
	EvAIOReqQueueClean(&kq_base->aio.queue);

	/* Close the KQUEUE */
	close(kq_base->kq_base);
	kq_base->kq_base = -1;

	/* Free LIBSSH stuff */
	libssh2_exit();

	/* Free tables */
	free(kq_base->ev_arr.event_arr);
	free(kq_base->ke_chg.chg_arr);

	/* Destroy LOG_BASE */
	if (kq_base->log_base)
		EvKQBaseLogBaseDestroy(kq_base->log_base);

	kq_base->log_base			= NULL;
	kq_base->ev_arr.event_arr	= NULL;
	kq_base->ke_chg.chg_arr		= NULL;
	free(kq_base);

	return;
}
/**************************************************************************************************************************/
int EvKQBaseDispatchOnce(EvKQBase *kq_base, int timeout_ms)
{
	struct timeval invoke_tv 	= {0};
	int kq_retcode 				= 0;

	int timeout_auto	= ((KQ_BASE_TIMEOUT_AUTO == timeout_ms) ? 1 : 0);
	int timeout_cur		= (timeout_auto ? KQEV_IOLOOP_MININTERVAL_MS : timeout_ms); //KQEV_IOLOOP_MAXINTERVAL_MS

	/* Break lookup, shutdown requested */
	if (kq_base->flags.do_shutdown)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "EV_BASE [%p] - Shutting down...\n", kq_base);
		//break;
		return 0;
	}

	/* We are crashing - Do not dispatch KQ */
	if (kq_base->flags.crashing_with_sig)
	{
		usleep(500);
		//continue;
		return 1;
	}

	/* Too many TIMEOUTs, move to MAXINTERVAL */
	if ((timeout_auto) && (kq_base->stats.cur_timeout_count > 3))
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_VERBOSE, LOGCOLOR_YELLOW, "EV_BASE [%p] - Too many timeouts, will move to MAX_INT [%d / %d]\n",
				kq_base, KQEV_IOLOOP_MAXINTERVAL_MS);

		/* Invoke k_queue with max timeout in mili_seconds */
		kq_retcode = EvKQInvokeKQueueOnce(kq_base, KQEV_IOLOOP_MAXINTERVAL_MS);
	}
	else
	{
		/* Invoke k_queue with max timeout in mili_seconds */
		kq_retcode = EvKQInvokeKQueueOnce(kq_base, timeout_ms);
	}

	/* Check return code for this event loop */
	switch (kq_retcode)
	{
	case COMM_KQ_TIMEOUT:
	{
		//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_VERBOSE, LOGCOLOR_GREEN, "EV_BASE [%p] - KQ_TIMEOUT [%d / %d]\n", kq_base,
		//		kq_base->stats.cur_error_count, kq_base->kq_conf.error_count_max);


		/* Dispatch internal event */
		EvKQBaseInternalEventDispatch(kq_base, 0, -1, KQ_BASE_INTERNAL_EVENT_KEVENT_TIMEOUT);

		/* INC timeout_count and RESET ERR_COUNT */
		kq_base->stats.cur_timeout_count++;
		kq_base->stats.cur_error_count = 0;
		break;
	}
	case COMM_KQ_OK:
	{
		//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EV_BASE [%p] - KQ_OK [%d / %d]\n", kq_base,
		//		kq_base->stats.cur_error_count, kq_base->kq_conf.error_count_max);

		/* Reset ERR_COUNT and TIMEOUT_COUNT */
		kq_base->stats.cur_timeout_count	= 0;
		kq_base->stats.cur_error_count		= 0;
		break;
	}

	case COMM_KQ_ERROR:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "EV_BASE [%p] - KQ_ERROR [%d / %d]\n", kq_base,
				kq_base->stats.cur_error_count, kq_base->kq_conf.error_count_max);

		/* Dispatch internal event */
		EvKQBaseInternalEventDispatch(kq_base, 0, -1, KQ_BASE_INTERNAL_EVENT_KEVENT_ERROR);

		/* Too many errors, shutdown this KQ_BASE */
		if (kq_base->stats.cur_error_count >= kq_base->kq_conf.error_count_max)
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "EV_BASE [%p] - KQ_ERROR [%d / %d]- Will shutdown...\n",
					kq_base, kq_base->stats.cur_error_count, kq_base->kq_conf.error_count_max);

			kq_base->flags.do_shutdown = 1;
		}

		/* Touch error count */
		kq_base->stats.cur_error_count++;
		break;
	}

	default:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "EV_BASE [%p] - Undefined KQ_STATE - Will shutdown...\n", kq_base);

		/* This should NEVER happen */
		kq_base->flags.do_shutdown = 1;
		break;
	}

	}

	/* Calculate latency of this IO_LOOP */
	gettimeofday(&invoke_tv, NULL);
	kq_base->stats.evloop_latency_ms = EvKQBaseTimeValSubMsec(&kq_base->stats.cur_invoke_tv, &invoke_tv);

	//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EV_BASE [%p] - IO loop latency [%d ms]\n", kq_base, kq_base->stats.evloop_latency_ms);

	return 1;
}
/**************************************************************************************************************************/
void EvKQBaseDispatch(EvKQBase *kq_base, int timeout_ms)
{
	int timeout_auto	= ((KQ_BASE_TIMEOUT_AUTO == timeout_ms) ? 1 : 0);
	int timeout_cur		= (timeout_auto ? KQEV_IOLOOP_MININTERVAL_MS : timeout_ms); //KQEV_IOLOOP_MAXINTERVAL_MS

	/* Clear SHUTDOWN flag before digging into EV_LOOP */
	kq_base->flags.do_shutdown = 0;

	/* Loop forever */
	while (1)
	{
		/* If dispatch ONE returns 0, its time to break IO loop */
		if (!EvKQBaseDispatchOnce(kq_base, timeout_ms))
			break;

		continue;
	}

	return;
}
/**************************************************************************************************************************/
int EvKQInvokeKQueueOnce(EvKQBase *kq_base, int timeout_ms)
{
	/* Adjust IO loop timeout for this IO_LOOP and get current SYSTEM_TIME */
	EvKQBaseAdjustIOLoopTimeout(kq_base, timeout_ms);
	gettimeofday(&kq_base->stats.cur_invoke_tv, NULL);
	clock_gettime(CLOCK_MONOTONIC, &kq_base->stats.monotonic_tp);

	/* First RUN - Save TIMEVAL */
	if (kq_base->stats.first_invoke_tv.tv_sec <= 0)
		memcpy(&kq_base->stats.first_invoke_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* Detect time skew just before updating internal SEC and USEC from TV and then SYNC with TS_SEC and TS_USEC */
	EvKQBaseTimeSkewDetect(kq_base, timeout_ms);

	/* Dispatch QUEUED jobs and DEFER list */
	EvKQJobsDispatch(kq_base);
	EvKQBaseDeferDispatch(kq_base);

	/* Invoke the KERNEL KEVENT MECHANISM to retrieve active events */
	EvKQBaseKEventInvoke(kq_base);

	/* Something went seriously wrong with that sys_call */
	if (kq_base->ev_arr.event_cur_count < 0)
		return COMM_KQ_ERROR;

	/* k_event time outed */
	if (kq_base->ev_arr.event_cur_count == 0)
		return COMM_KQ_TIMEOUT;

	//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "EV_BASE [%p] - EV_COUNT/CAP [%d /%d]\n",
	//		kq_base, kq_base->ev_arr.event_cur_count, kq_base->ev_arr.event_arr_cap);

	/* Invoke individual FD CBs */
	EvKQInvokeKQFDCallbacks(kq_base, kq_base->stats.cur_invoke_tv.tv_sec);

	/* Grow received event list if it became full in this IO loop */
	EVBASE_EV_ARR_GROW_IF_NEEDED(kq_base);

	return COMM_KQ_OK;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
void EvKQBaseSetEvent(EvKQBase *kq_base, int fd, int ev_type, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;

	/* Do not allow invalid FDs in this routine */
	if (fd < 0)
		return;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	switch (ev_type)
	{
	/******************************************************************/
	case COMM_EV_READ:
	{
		/* Update events for read filter */
		EvKQBaseUpdateReadEvent(kq_base, fd, action, cb_handler, cb_data);
		break;
	}
	/******************************************************************/
	case COMM_EV_WRITE:
	{
		/* Update events for write filter */
		EvKQBaseUpdateWriteEvent(kq_base, fd, action, cb_handler, cb_data);
		break;
	}
	/******************************************************************/
	case COMM_EV_FILEMON:
	{
		/* Update events for VNODE filter */
		EvKQBaseUpdateFileMonEvent(kq_base, fd, action, cb_handler, cb_data);
		break;
	}
	/******************************************************************/
	case COMM_EV_EOF:
	{
		/* Update events for EOF filter */
		EvKQBaseUpdateEOFEvent(kq_base, fd, action, cb_handler, cb_data);
		break;
	}
	/******************************************************************/
	case COMM_EV_ERROR:
	{
		/* Update events for error filter */
		EvKQBaseUpdateErrorEvent(kq_base, fd, action, cb_handler, cb_data);
		break;
	}
	/******************************************************************/
	case COMM_EV_DEFER_CHECK_READ:
	{
		/* Update events for defer read filter */
		EvKQBaseUpdateDeferCheckReadEvent(kq_base, fd, action, cb_handler, cb_data);
		break;
	}
	/******************************************************************/
	case COMM_EV_DEFER_CHECK_WRITE:
	{
		/* Update events for defer write filter */
		EvKQBaseUpdateDeferCheckWriteEvent(kq_base, fd, action, cb_handler, cb_data);
		break;
	}
	/******************************************************************/


	}

	return;
}
/**************************************************************************************************************************/
void EvKQBaseClearEvents(EvKQBase *kq_base, int fd)
{
	int i;

	/* Sanity check */
	if (fd < 0)
		return;

	/* Clear all events */
	for (i = COMM_EV_READ; i < COMM_EV_LASTITEM; i++)
		EvKQBaseSetEvent(kq_base, fd, i, COMM_ACTION_DELETE, NULL, NULL);

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW,	"FD [%d] - [%s] - Clear ALL EVENTS\n", fd, EvKQBaseFDDescriptionGetByFD(kq_base, fd));

	return;
}
/**************************************************************************************************************************/
void EvKQBaseAdjustIOLoopTimeout(EvKQBase *kq_base, int interval_ms)
{
	/* Keep between MIN and MAX */
	if (interval_ms < KQEV_IOLOOP_MININTERVAL_MS)
		interval_ms = KQEV_IOLOOP_MININTERVAL_MS;

	if (interval_ms > KQEV_IOLOOP_MAXINTERVAL_MS)
		interval_ms = KQEV_IOLOOP_MAXINTERVAL_MS;

	/* KQUEUE/KEVENT timeout timers */
	kq_base->timeout.tv_sec		= interval_ms / 1000;
	kq_base->timeout.tv_nsec	= (interval_ms % 1000) * 1000000;

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
int EvKQBaseDispatchEventWrite(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size)
{
	return EvKQBaseFDEventInvoke(kq_base, kq_fd, KQ_CB_HANDLER_WRITE, data_size, -1, kq_base);

//	EvBaseKQGenericEventPrototype *ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_WRITE];
//	EvBaseKQCBH *write_cb_handler			= ev_proto->cb_handler_ptr;
//	void *write_cb_data						= ev_proto->cb_data_ptr;
//	int has_write_ev						= ev_proto->flags.enabled;
//	int data_write							= 0;
//
//	/* Disabled event or no CB_H, bail out */
//	if ((!ev_proto->flags.enabled) || (!write_cb_handler))
//		return 0;
//
//	/* Touch time_stamp */
//	memcpy(&ev_proto->run.tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
//
//	/* Disable any timeout related info */
//	EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, COMM_EV_TIMEOUT_WRITE);
//	EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, COMM_EV_TIMEOUT_BOTH);
//
//	/* Volatile event, un_mark write_enabled flag */
//	if (!ev_proto->flags.persist)
//		ev_proto->flags.enabled	= 0;
//
//	/* Invoke the call_back handler */
//	if ((data_size > 0) && (has_write_ev))
//		data_write = write_cb_handler(kq_fd->fd.num, data_size, -1, write_cb_data, kq_base);
//
//	return data_write;
}
/**************************************************************************************************************************/
int EvKQBaseDispatchEventRead(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size)
{
	return EvKQBaseFDEventInvoke(kq_base, kq_fd, KQ_CB_HANDLER_READ, data_size, -1, kq_base);

//	EvBaseKQGenericEventPrototype *ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_READ];
//	EvBaseKQCBH *read_cb_handler			= ev_proto->cb_handler_ptr;
//	void *read_cb_data						= ev_proto->cb_data_ptr;
//	int has_read_ev							= ev_proto->flags.enabled;
//	int data_read							= 0;
//
//	/* Disabled event or no CB_H, bail out */
//	if ((!ev_proto->flags.enabled) || (!read_cb_handler))
//		return 0;
//
//	/* Touch time_stamp */
//	memcpy(&ev_proto->run.tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
//
//	/* Disable any timeout related info */
//	EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, COMM_EV_TIMEOUT_READ);
//	EvKQBaseTimeoutClearByKQFD(kq_base, kq_fd, COMM_EV_TIMEOUT_BOTH);
//
//	/* Volatile event, un_mark read_enabled flag */
//	if (!ev_proto->flags.persist)
//		ev_proto->flags.enabled	= 0;
//
//	/* Invoke the call_back handler */
//	if ((data_size > 0) && (has_read_ev))
//		data_read = read_cb_handler(kq_fd->fd.num, data_size, -1, read_cb_data, kq_base);
//
//	return data_read;
}
/**************************************************************************************************************************/
int EvKQBaseDispatchEventWriteEOF(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size)
{
	EvBaseKQGenericEventPrototype *ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_EOF];
	EvBaseKQCBH *eof_cb_handler				= ev_proto->cb_handler_ptr;
	void *eof_cb_data						= ev_proto->cb_data_ptr;
	int data_read							= 0;

	/* NOT IMPLEMENTED */

	return 1;
}
/**************************************************************************************************************************/
int EvKQBaseDispatchEventReadEOF(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size)
{
	return EvKQBaseFDEventInvoke(kq_base, kq_fd, KQ_CB_HANDLER_EOF, data_size, -1, kq_base);

//	EvBaseKQGenericEventPrototype *ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_EOF];
//	EvBaseKQCBH *eof_cb_handler				= ev_proto->cb_handler_ptr;
//	void *eof_cb_data						= ev_proto->cb_data_ptr;
//	int data_read							= 0;
//
//	/* Disabled event or no CB_H, bail out */
//	if ((!ev_proto->flags.enabled) || (!eof_cb_handler))
//		return 0;
//
//	/* Volatile event, un_mark read_enabled flag */
//	if (!ev_proto->flags.persist)
//		ev_proto->flags.enabled	= 0;
//
//	/* Invoke the call_back handler */
//	data_read = eof_cb_handler(kq_fd->fd.num, data_size, -1, eof_cb_data, kq_base);
//
//	return data_read;
}
/**************************************************************************************************************************/
int EvKQBaseDispatchEventReadError(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size)
{
	return EvKQBaseFDEventInvoke(kq_base, kq_fd, KQ_CB_HANDLER_READ_ERROR, data_size, -1, kq_base);

//	EvBaseKQGenericEventPrototype *ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR];
//	EvBaseKQCBH *error_cb_handler			= ev_proto->cb_handler_ptr;
//	void *error_cb_data						= ev_proto->cb_data_ptr;
//	int data_read							= 0;
//
//	/* Disabled event or no CB_H, bail out */
//	if ((!ev_proto->flags.enabled) || (!error_cb_handler))
//		return 0;
//
//	/* Volatile event, un_mark read_enabled flag */
//	if (!ev_proto->flags.persist)
//		ev_proto->flags.enabled	= 0;
//
//	/* Invoke the call_back handler */
//	data_read = error_cb_handler(kq_fd->fd.num, data_size, -1, error_cb_data, kq_base);
//
//	return data_read;
}
/**************************************************************************************************************************/
int EvKQBaseAssert(EvKQBase *kq_base, const char *func_str, char *file_str, int line, char *msg, ...)
{
	char assert_buf[KQBASE_LOGGER_MAX_LOGLINE_SZ];
	va_list args;
	int msg_sz;
	int i;

	EvKQBaseLogBase *log_base	= kq_base->log_base;
	char *assert_str			= (char*)&assert_buf;
	int offset					= 0;

	/* If there is no log base attached to KQ_BASE, check if there is a GLOB_SIGNAL_LOGBASE */
	if (!log_base)
		log_base = glob_sinal_log_base;

	/* Close STDIN, OUT and ERR ASAP to signal any IPC controller that we are going down */
	if (!kq_base->flags.assert_soft)
	{
		close(KQBASE_STDIN);
		close(KQBASE_STDOUT);
		close(KQBASE_STDERR);

		/* Jump into USER CRASH_CB, if it exists */
		if (kq_base->crash_cb)
			kq_base->crash_cb(kq_base, 6);
	}

	/* There is a log base, dispatch ASSERT LOG */
	if (log_base)
	{
		/* Build ASSERT message, if its formatted */
		va_start(args, msg);
		offset = snprintf((assert_str + offset), (sizeof(assert_buf) - offset), "ASSERTION FAILED -> ");
		offset += vsnprintf((assert_str + offset), (sizeof(assert_buf) - offset), msg, args);
		assert_str[offset] = '\0';
		va_end(args);

		/* Dump assert MESSAGE and begin DUMP logs if operator asked us to */
		EvKQBaseLoggerAdd(log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, file_str, func_str, line, assert_str);
		EvKQBaseLoggerMemDumpOnCrash(log_base);
	}

	/* Soft assert, return right now */
	if (kq_base->flags.assert_soft)
		return 1;

	/* Restore default signals */
	EvKQBaseDefaultSignals(kq_base);

	/* HARD ASS ASSERT - Actually ABORT */
	abort();
	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQInvokeKQFDCallbacks(EvKQBase *kq_base, long invoke_ts)
{
	EvBaseKQGenericEventPrototype *ev_proto;
	EvAIOReq *aio_req;
	struct kevent *kev_ptr;
	void *target_cbdata;
	int target_int_data;
	int target_filter;
	int target_flags;
	int target_fflags;
	int cbreturn_data;
	int fd_will_defer;
	int target_fd;
	int i;

	EvBaseKQFileDesc *kq_fd				= NULL;
	EvBaseKQCBH *filemon_cb_handler		= NULL;
	EvBaseKQCBH *signal_cb_handler		= NULL;
	void *filemon_cb_data				= NULL;
	void *signal_cb_data				= NULL;

	/* Grab a pointer to the start of event_arr */
	kev_ptr = kq_base->ev_arr.event_arr;

	/* Parse returned event array and invoke each call_back */
	for (i = 0; i < kq_base->ev_arr.event_cur_count; i++)
	{
		/* We are crashing. Cancel any further event */
		if (kq_base->flags.crashing_with_sig)
			break;

		/* Get event file descriptor, int_data and cb_data */
		target_fd		= (int) kev_ptr[i].ident;
		target_int_data	= (int)	kev_ptr[i].data;
		target_cbdata	= (void*)kev_ptr[i].udata;
		target_flags	= (int)	kev_ptr[i].flags;
		target_fflags	= (int)	kev_ptr[i].fflags;
		target_filter	= (int) kev_ptr[i].filter;

		/* Signal and timer events wont use FD */
		if ((EVFILT_SIGNAL != target_filter) && (EVFILT_TIMER != target_filter) && (EVFILT_AIO != target_filter))
		{
			/* Grab KQ_FD from internal arena and LOCK it, while we process the CB functions */
			kq_fd		= EvKQBaseFDGrabFromArena(kq_base, target_fd);
			MemArenaLockByID(kq_base->fd.arena, kq_fd->fd.num);
		}

		/* Grab event filter */
		switch (kev_ptr[i].filter)
		{
		/******************************************************************/
		case EVFILT_READ:
		{
			/* Grab EV_PROTOTYPE from within it */
			ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_READ];

			/* Mark EOF and ERROR flags on FD */
			kq_fd->flags.so_read_eof	= (kev_ptr[i].flags & EV_EOF) ? 1 : 0;
			kq_fd->flags.so_read_error	= (target_flags & EV_ERROR) ? 1 : 0;

			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN,
					"FD [%d] - Read event of [%d] bytes - CB_FUNC [%p] - PERSIST [%d] - ENABLED [%d] - EOF [%d] - DEFER [%d] - CLOSED [%d / %d]\n",
					target_fd, target_int_data, kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_handler_ptr, kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.persist,
					kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled, kq_fd->flags.so_read_eof, kq_fd->flags.defer_read, kq_fd->flags.closed, kq_fd->flags.closing);

			/* Event is disabled, bail out */
			if (!ev_proto->flags.enabled)
				break;

			/* If upper layer want to use the DEFER_READ mechanism, invoke upper layer READ_DEFER_CHECKER */
			fd_will_defer	= EvKQBaseDeferReadCheckByKQFD(kq_base, kq_fd, target_int_data);

			/* EvKQBaseDeferReadCheckByKQFD has CLOSED FD beneath our feet, bail out */
			if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
				break;

			/* FD is being READ_DEFERED, bail out */
			if (kq_fd->flags.defer_read)
			{
				KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - DEFERING READ of [%d] bytes\n", target_fd, target_int_data);
				break;
			}

			/* Dispatch read event to upper layer */
			cbreturn_data = EvKQBaseDispatchEventRead(kq_base, kq_fd, target_int_data);

			/* EvKQBaseDispatchEventRead has CLOSED FD beneath our feet, bail out */
			if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
				break;

			/* KEVENT returned EOF FLAGs and this FD wants to be notified of EOF */
			if (kq_fd->flags.so_read_eof)
				EvKQBaseDispatchEventReadEOF(kq_base, kq_fd, (target_int_data - cbreturn_data));

			break;
		}
		/******************************************************************/
		case EVFILT_WRITE:
		{
			/* Grab EV_PROTOTYPE from within it */
			ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_WRITE];

			/* Mark EOF and ERROR flags on FD */
			kq_fd->flags.so_write_eof	= (kev_ptr[i].flags & EV_EOF) ? 1 : 0;
			kq_fd->flags.so_write_error	= (target_flags & EV_ERROR) ? 1 : 0;

			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - [%s] - WRITE_EVENT - INT_DATA [%d] - FLAGS [%d] - FFLAGS [%d] - KEV_ERROR [%d]\n",
					target_fd, EvKQBaseFDDescriptionGetByFD(kq_base, target_fd), target_int_data, kev_ptr[i].flags,
					target_fflags, (target_flags & EV_ERROR), (kev_ptr[i].flags & EV_EOF));

			/* Event is disabled, bail out */
			if (!ev_proto->flags.enabled)
				break;

			/* If upper layer want to use the DEFER_WRITE mechanism, invoke upper layer WRITE_DEFER_CHECKER */
			fd_will_defer	= EvKQBaseDeferWriteCheckByKQFD(kq_base, kq_fd, target_int_data);

			/* EvKQBaseDeferWriteCheckByKQFD has CLOSED FD beneath our feet, bail out */
			if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
				break;

			/* FD is being WRITE_DEFERED, bail out */
			if (kq_fd->flags.defer_write)
			{
				KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - DEFERING WRITE of [%d] bytes\n", target_fd, target_int_data);
				break;
			}

			/* Dispatch write event to upper layer */
			cbreturn_data = EvKQBaseDispatchEventWrite(kq_base, kq_fd, target_int_data);

			/* EvKQBaseDispatchEventWrite has CLOSED FD beneath our feet, bail out */
			if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
				break;

			/* KEVENT returned EOF FLAGs and this FD wants to be notified of EOF */
			if (kq_fd->flags.so_write_eof)
				EvKQBaseDispatchEventWriteEOF(kq_base, kq_fd, (target_int_data - cbreturn_data));

			break;
		}
		/******************************************************************/
		case EVFILT_VNODE:
		{
			/* Grab EV_PROTOTYPE from within it */
			ev_proto	= &kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON];

			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - [%s] - VNODE EVENT - INT_DATA [%d] - FLAGS [%d] - FFLAGS [%d] - KEV_ERROR [%d]\n",
					target_fd, EvKQBaseFDDescriptionGetByFD(kq_base, target_fd), target_int_data, kev_ptr[i].flags,
					target_fflags, (target_flags & EV_ERROR), (kev_ptr[i].flags & EV_EOF));

			filemon_cb_handler	= kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_handler_ptr;
			filemon_cb_data		= kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_data_ptr;

			/* Volatile event, un_mark has_write and write_ev_enabled */
			if (!kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.persist)
			{
				/* NULLify cb_handlers */
				kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_handler_ptr = NULL;
				kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_data_ptr = NULL;

				/* Mark new state */
				kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled = 0;
				kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.persist = 0;
			}

			if (filemon_cb_handler)
				filemon_cb_handler(target_fd, target_fflags, -1, filemon_cb_data, kq_base);

			break;
		}
		/******************************************************************/
		case EVFILT_SIGNAL:
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - [%s] - SIGNAL_EVENT - INT_DATA [%d] - FLAGS [%d] - FFLAGS [%d] - KEV_ERROR [%d]\n",
					target_fd, EvKQBaseFDDescriptionGetByFD(kq_base, target_fd), target_int_data, kev_ptr[i].flags,
					target_fflags, (target_flags & EV_ERROR), (kev_ptr[i].flags & EV_EOF));

			signal_cb_handler	= kq_base->sig_handler[target_fd].cb_handler_ptr;
			signal_cb_data		= kq_base->sig_handler[target_fd].cb_data_ptr;

			if (!kq_base->sig_handler[target_fd].flags.persist)
			{
				kq_base->sig_handler[target_fd].cb_handler_ptr	= NULL;
				kq_base->sig_handler[target_fd].cb_data_ptr		= NULL;

				/* UNMARK persist and enable */
				kq_base->sig_handler[target_fd].flags.persist	= 0;
				kq_base->sig_handler[target_fd].flags.enabled	= 0;
			}

			/* Invoke the CALLBACK handler */
			if (signal_cb_handler)
				signal_cb_handler(target_fd, target_int_data, -1, signal_cb_data, kq_base);

			break;
		}
		/******************************************************************/
		case EVFILT_AIO:
		{
			/* Grab AIO_REQ and FD from reference table */
			aio_req		= target_cbdata;
			kq_fd		= EvKQBaseFDGrabFromArena(kq_base, aio_req->fd);

			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EVFILT_AIO - FD [%d] - INT_DATA [%d] - CB_DATA [%p] - FLAGS [%d] - FFLAGS [%d] - FILTER [%d]\n",
					aio_req->fd, target_int_data, target_cbdata, target_flags, target_fflags, target_filter);

			/* Check if request has finished OK */
			EvKQBaseAIOFileGeneric_FinishCheck(kq_base, aio_req);
			break;
		}
		/******************************************************************/
		case EVFILT_TIMER:
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "TIMER_ID [%d] - TIMER_EVENT - INT_DATA [%d] - FLAGS [%d] - FFLAGS [%d] - KEV_ERROR [%d]\n",
					target_fd, target_int_data, kev_ptr[i].flags, target_fflags, (target_flags & EV_ERROR), (kev_ptr[i].flags & EV_EOF));

			/* Error on TIMER_ID filter */
			if (target_flags & EV_ERROR)
			{
				KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "TIMER_ID [%d] - ERROR [%d]\n", target_fd, target_int_data);
				break;
			}

			/* TARGET_FD will hold TIMER_ID */
			EvKQBaseTimerDispatch(kq_base, target_fd, target_int_data);
			break;

		}
		/******************************************************************/
		default:
		{
			KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Unexpected event: [%d]\n", target_fd, kev_ptr[i].filter);
			break;
		}
		/******************************************************************/
		} /* SWITCH CASE EVENT */

		/* Unlock the arena where this FD lives in */
		if ((EVFILT_SIGNAL != target_filter) && (EVFILT_TIMER != target_filter) && (EVFILT_AIO != target_filter))
			MemArenaUnlockByID(kq_base->fd.arena, kq_fd->fd.num);
		continue;
	}

	return i;
}
/**************************************************************************************************************************/
static void EvKQBaseEnqueueEvChg(EvKQBase *kq_base, unsigned int fd, int ev_type, int action, void *udata)
{
	struct kevent *kev_ptr = NULL;
	struct timespec timeout;

	/* Timeout timers */
	timeout.tv_sec	= 0;
	timeout.tv_nsec = 1;

	EVBASE_CHGLST_GROW_IF_NEEDED(kq_base);
	kev_ptr = &kq_base->ke_chg.chg_arr[(kq_base->ke_chg.chg_arr_off)];
	kq_base->ke_chg.chg_arr_off++;

	switch (ev_type)
	{
	/******************************************************************/
	case COMM_EV_READ:
	{
		switch (action)
		{
		/***********************************************/
		case COMM_ACTION_DELETE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_READ, EV_DELETE, 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ADD_PERSIST:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_READ, EV_ADD, 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ADD_TRANSITION:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_READ, (EV_ADD | EV_CLEAR), 0, 0, udata);
			break;
		}

		/***********************************************/
		case COMM_ACTION_ADD_VOLATILE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_READ, (EV_ADD | EV_ONESHOT), 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ENABLE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_READ, EV_ENABLE, 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_DISABLE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_READ, EV_DISABLE, 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_CLEAR:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_READ, EV_CLEAR, 0, 0, udata);
			break;
		}
		/***********************************************/
		}

		break;
	}
	/******************************************************************/
	case COMM_EV_WRITE:
	{
		switch (action)
		{
		/***********************************************/
		case COMM_ACTION_DELETE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_WRITE, EV_DELETE, 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ADD_PERSIST:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_WRITE, EV_ADD, 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ADD_VOLATILE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_WRITE, (EV_ADD | EV_ONESHOT), 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ENABLE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_WRITE, EV_ENABLE, 0, 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_DISABLE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_WRITE, EV_DISABLE, 0, 0, udata);
			break;
		}
		/***********************************************/
		}

		break;
	}
	/******************************************************************/
	case COMM_EV_FILEMON:
	{

		switch (action)
		{
		/***********************************************/
		case COMM_ACTION_DELETE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_VNODE, EV_DELETE, (NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME), 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ADD_PERSIST:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_VNODE, EV_ADD, (NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME), 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ADD_VOLATILE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_VNODE, (EV_ADD | EV_ONESHOT), (NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME), 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_ENABLE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_VNODE, EV_ENABLE, (NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME), 0, udata);
			break;
		}
		/***********************************************/
		case COMM_ACTION_DISABLE:
		{
			/* Fill kev_ptr index */
			EV_SET(kev_ptr, (uintptr_t) fd, EVFILT_VNODE, EV_DISABLE, (NOTE_DELETE | NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_LINK | NOTE_RENAME), 0, udata);
			break;
		}
		/***********************************************/
		}

		break;
	}


	}

	return;

}
/**************************************************************************************************************************/
static void EvKQBaseUpdateReadEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	switch (action)
	{
	/******************************************************************/
	/* Action is ADD_VOLATILE. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs - If FD is being DEFERED, do not ALLOW ADD and ENABLE events to be ADDED */
		if (cb_handler)
		{
			/* Grab cb_handler and cb_data */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled	 	= 1;

			/* Volatile event */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.persist		= 0;

			/* Enqueue event change - Send kq_base as user_data */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_READ, action, kq_base);
		}

		break;
	}
	/******************************************************************/
	/* Action is ADD_PERSIST. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_TRANSITION:
	case COMM_ACTION_ADD_PERSIST:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled	 	= 1;

			/* Persistent event */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.persist		= 1;

			/* Enqueue event change - Send kq_base as user_data */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_READ, action, kq_base);
		}

		break;
	}
	/******************************************************************/
	/* Action is DELETE. Clear cb_handler and cb_data
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		/* Just enqueue DELETE if enabled */
		if (kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_READ, action, kq_base);
		}

		kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_handler_ptr	= NULL;
		kq_fd->cb_handler[KQ_CB_HANDLER_READ].cb_data_ptr		= NULL;

		/* Mark new state */
		kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled	 	= 0;
		kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.persist		= 0;

		break;

	}
	/******************************************************************/
	/* Action is ENABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		/* If not enabled, enqueue an enable event to change list */
		if (!kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_READ, action, kq_base);

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled = 1;
		}
		break;
	}
	/******************************************************************/
	/* Action is DISABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		/* If not disabled, enqueue a disable event to change list */
		if (kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_READ, action, kq_base);

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ].flags.enabled = 0;
		}
		break;

	}
	/******************************************************************/
	case COMM_ACTION_CLEAR:
	{
		/* Enqueue event change */
		EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_READ, action, kq_base);
		break;
	}
	/******************************************************************/
	}

	return;
}
/**************************************************************************************************************************/
static void EvKQBaseUpdateWriteEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	switch (action)
	{
	/******************************************************************/
	/* Action is ADD_VOLATILE. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs - DO NOT allow if we are DEFERING WRITE */
		if  (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled = 1;

			/* Volatile event */
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.persist = 0;

			/* Enqueue event change - Send kq_base as user_data */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_WRITE, action, kq_base);
		}

		break;
	}
	/******************************************************************/
	/* Action is ADD_PERSIST. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_PERSIST:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs - DO NOT allow if we are DEFERING WRITE */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled = 1;

			/* Persistent event */
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.persist = 1;

			/* Enqueue event change - Send kq_base as user_data */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_WRITE, action, kq_base);
		}

		break;
	}
	/******************************************************************/
	/* Action is DELETE. Clear cb_handler and cb_data
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		/* Just enqueue DELETE if enabled */
		if (kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_WRITE, action, kq_base);
		}

		kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_handler_ptr	= NULL;
		kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].cb_data_ptr		= NULL;

		/* Mark new state */
		kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled = 0;
		kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.persist = 0;
		break;

	}
	/******************************************************************/
	/* Action is ENABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		/* This FD is being WRITE DEFERED, do not allow to ENABLE write event */
		if (kq_fd->flags.defer_write)
			break;

		/* If not enabled, enqueue an enable event to change list */
		if (!kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_WRITE, action, kq_base);

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled = 1;
		}

		break;
	}
	/******************************************************************/
	/* Action is DISABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		/* If not disabled, enqueue a disable event to change list */
		if (kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_WRITE, action, kq_base);

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_WRITE].flags.enabled = 0;
		}

		break;
	}
	/******************************************************************/
	}

	return;
}
/**************************************************************************************************************************/
static void EvKQBaseUpdateEOFEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	switch (action)
	{
	/******************************************************************/
	/* Action is ADD. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF event set - [COMM_ACTION_ADD_VOLATILE]\n", fd);

			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].cb_data_ptr	= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.persist = 0;

		}

		break;
	}
	case COMM_ACTION_ADD_PERSIST:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF event set - [COMM_ACTION_ADD_PERSIST]\n", fd);

			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].cb_data_ptr	= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.persist = 1;
		}

		break;
	}
	/******************************************************************/
	/* Action is DELETE. Clear cb_handler and cb_data
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF event set - [COMM_ACTION_DELETE]\n", fd);

		kq_fd->cb_handler[KQ_CB_HANDLER_EOF].cb_handler_ptr	= NULL;
		kq_fd->cb_handler[KQ_CB_HANDLER_EOF].cb_data_ptr	= NULL;

		/* Mark new state */
		kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.enabled = 0;
		kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.persist = 0;

		break;

	}
	/******************************************************************/
	/* Action is ENABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF event set - [COMM_ACTION_ENABLE]\n", fd);

		if (!kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.enabled)
			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.enabled	= 1;

		break;
	}
	/******************************************************************/
	/* Action is DISABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF event set - [COMM_ACTION_DISABLE]\n", fd);

		if (kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.enabled)
			kq_fd->cb_handler[KQ_CB_HANDLER_EOF].flags.enabled	= 0;

		break;

	}
	/******************************************************************/
	}

	return;
}
/**************************************************************************************************************************/
static void EvKQBaseUpdateErrorEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);


	switch (action)
	{
	/******************************************************************/
	/* Action is ADD. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.persist = 0;
		}

		break;
	}
	case COMM_ACTION_ADD_PERSIST:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.persist = 0;
		}

		break;
	}
	/******************************************************************/
	/* Action is DELETE. Clear cb_handler and cb_data
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].cb_handler_ptr	= NULL;
		kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].cb_data_ptr		= NULL;

		/* Mark new state */
		kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.enabled = 0;
		kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.persist = 0;
		break;

	}
	/******************************************************************/
	/* Action is ENABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		if (!kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.enabled)
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.enabled	= 1;
		break;
	}
	/******************************************************************/
	/* Action is DISABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		if (kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.enabled)
			kq_fd->cb_handler[KQ_CB_HANDLER_READ_ERROR].flags.enabled	= 0;
		break;

	}
	/******************************************************************/
	}

	return;
}
/**************************************************************************************************************************/
static void EvKQBaseUpdateDeferCheckReadEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;

	EvBaseKQCBH *read_cb_handler		= NULL;
	void *read_cb_data					= NULL;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	switch (action)
	{
	/******************************************************************/
	/* Action is ADD. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.persist = 0;

			/* Save UNIX_TS of when we begin DEFERING READ this FD */
			memcpy(&kq_fd->defer.read.set_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
		}

		break;
	}
	/******************************************************************/
	case COMM_ACTION_ADD_PERSIST:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.persist = 1;

			/* Save UNIX_TS of when we begin DEFERING READ this FD */
			memcpy(&kq_fd->defer.read.set_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
		}

		break;
	}
	/******************************************************************/
	/* Action is DELETE. Clear cb_handler and cb_data
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_handler_ptr	= NULL;
		kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].cb_data_ptr		= NULL;

		/* Mark new state */
		kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled = 0;
		kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.persist = 0;

		/* This FD is currently being DEFERED, remove from DEFER_CHECK list and clean flag */
		EvKQBaseDeferReadRemoveByKQFD(kq_base, kq_fd);

		break;

	}
	/******************************************************************/
	/* Action is ENABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		if (!kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled)
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled	= 1;

		break;
	}
	/******************************************************************/
	/* Action is DISABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		if (kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled)
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_READ].flags.enabled	= 0;
		break;

	}
	/******************************************************************/

	}

	return;
}
/**************************************************************************************************************************/
static void EvKQBaseUpdateDeferCheckWriteEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	switch (action)
	{
	/******************************************************************/
	/* Action is ADD. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.persist = 0;

			/* Save UNIX_TS of when we begin DEFERING WRITE this FD */
			memcpy(&kq_fd->defer.write.set_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
		}

		break;
	}
	case COMM_ACTION_ADD_PERSIST:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_data_ptr		= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.persist = 1;

			/* Save UNIX_TS of when we begin DEFERING WRITE this FD */
			memcpy(&kq_fd->defer.write.set_tv, &kq_base->stats.cur_invoke_tv, sizeof(struct timeval));
		}

		break;
	}
	/******************************************************************/
	/* Action is DELETE. Clear cb_handler and cb_data
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_handler_ptr	= NULL;
		kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].cb_data_ptr		= NULL;

		/* Mark new state */
		kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled = 0;
		kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.persist = 0;

		/* Clean TS */
		memset(&kq_fd->defer.write.begin_tv, 0, sizeof(struct timeval));
		memset(&kq_fd->defer.write.check_tv, 0, sizeof(struct timeval));
		memset(&kq_fd->defer.write.set_tv, 0, sizeof(struct timeval));
		break;

	}
	/******************************************************************/
	/* Action is ENABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		if (!kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled)
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled	= 1;
		break;
	}
	/******************************************************************/
	/* Action is DISABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		if (kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled)
			kq_fd->cb_handler[KQ_CB_HANDLER_DEFER_CHECK_WRITE].flags.enabled	= 0;

		break;

	}
	/******************************************************************/

	}
	return;
}
/**************************************************************************************************************************/
static void EvKQBaseUpdateFileMonEvent(EvKQBase *kq_base, int fd, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	EvBaseKQFileDesc *kq_fd;

	/* Grab FD from reference table */
	kq_fd = EvKQBaseFDGrabFromArena(kq_base, fd);

	switch (action)
	{
	/******************************************************************/
	/* Action is ADD_VOLATILE. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_data_ptr	= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.persist = 0;

			/* Enqueue event change - Send kq_base as user_data */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_FILEMON, action, kq_base);
		}

		break;
	}
	/******************************************************************/
	/* Action is ADD_PERSIST. Check for valid cb_handler and cb_data and enqueue change
	/******************************************************************/
	case COMM_ACTION_ADD_PERSIST:
	{
		/* We have a valid cb_handler pointer. Fill in FD call_backs */
		if (cb_handler)
		{
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_handler_ptr	= cb_handler;
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_data_ptr	= cb_data;

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled = 1;
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.persist = 1;

			/* Enqueue event change - Send kq_base as user_data */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_FILEMON, action, kq_base);
		}

		break;
	}
	/******************************************************************/
	/* Action is DELETE. Clear cb_handler and cb_data
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		/* Just enqueue DELETE if enabled */
		if (kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_FILEMON, action, kq_base);
		}

		kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_handler_ptr	= NULL;
		kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].cb_data_ptr	= NULL;

		/* Mark new state */
		kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled = 0;
		kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.persist = 0;
		break;

	}
	/******************************************************************/
	/* Action is ENABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		/* If not enabled, enqueue an enable event to change list */
		if (!kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_FILEMON, action, kq_base);

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled = 1;
		}
		break;
	}
	/******************************************************************/
	/* Action is DISABLE. Mark it
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		/* If not disabled, enqueue a disable event to change list */
		if (kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled)
		{
			/* Enqueue event change */
			EvKQBaseEnqueueEvChg(kq_base, fd, COMM_EV_FILEMON, action, kq_base);

			/* Mark new state */
			kq_fd->cb_handler[KQ_CB_HANDLER_FILEMON].flags.enabled = 0;
		}
		break;
	}
	/******************************************************************/
	}

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void EvKQBaseKEventInvoke(EvKQBase *kq_base)
{
	//KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Dispatching [%d] events - Invoke count [%lld]\n", kq_base->ev_arr.event_cur_count, kq_base->stats.kq_invoke_count);

	/* Invoke the kernel event notification mechanism using just ke_chg 0 */
	kq_base->ev_arr.event_cur_count = kevent(kq_base->kq_base, kq_base->ke_chg.chg_arr, kq_base->ke_chg.chg_arr_off,
			kq_base->ev_arr.event_arr, kq_base->ev_arr.event_arr_cap, &kq_base->timeout);

	/* Get change list A back into index 0 */
	kq_base->ke_chg.chg_arr_off = 0;

	/* Update invoke count */
	kq_base->stats.kq_invoke_count++;

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int EvKQBaseTimeSkewDetect(EvKQBase *kq_base, int timeout_ms)
{
	int delta_sec;
	int delta_usec;

	/* First engine turn, bail out */
	if (0 == kq_base->stats.cur_invoke_ts_sec)
	{
		/* Grab a copy of local_invoke_ts safely out of ev_base and sync base stamps */
		kq_base->stats.cur_invoke_ts_sec	= kq_base->stats.cur_invoke_tv.tv_sec;
		kq_base->stats.cur_invoke_ts_usec	= kq_base->stats.cur_invoke_tv.tv_usec;
		return 0;
	}

	/* Calculate local delta */
	delta_sec	= kq_base->stats.cur_invoke_tv.tv_sec - kq_base->stats.cur_invoke_ts_sec;
	delta_usec	= kq_base->stats.cur_invoke_tv.tv_usec - kq_base->stats.cur_invoke_ts_usec;

	/* Time SKEW detected, dispatch internal event */
	if ((delta_sec < 0) || (delta_sec > kq_base->skew_min_detect_sec))
	{
		/* Dispatch internal event */
		EvKQBaseInternalEventDispatch(kq_base, delta_sec, -1, KQ_BASE_INTERNAL_EVENT_TIMESKEW);

		/* Grab a copy of local_invoke_ts safely out of ev_base and sync base stamps */
		kq_base->stats.cur_invoke_ts_sec	= kq_base->stats.cur_invoke_tv.tv_sec;
		kq_base->stats.cur_invoke_ts_usec	= kq_base->stats.cur_invoke_tv.tv_usec;
		return 1;
	}

	/* Grab a copy of local_invoke_ts safely out of ev_base and sync base stamps */
	kq_base->stats.cur_invoke_ts_sec	= kq_base->stats.cur_invoke_tv.tv_sec;
	kq_base->stats.cur_invoke_ts_usec	= kq_base->stats.cur_invoke_tv.tv_usec;

	return 0;
}
/**************************************************************************************************************************/


