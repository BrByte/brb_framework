/*
 * libbrb_ev_kq.h
 *
 *  Created on: 2012-08-29
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

#ifndef LIBBRB_EV_KQ_H_
#define LIBBRB_EV_KQ_H_

#include <time.h>

#include <sys/types.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/event.h>

#include <aio.h>
#include <sys/mount.h>
#if !defined(__linux__)
#include <ufs/ufs/ufsmount.h>
#endif

#include <netinet/in.h>
#include <netinet/tcp.h>

/* OpenSSL */
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/conf.h>
#include <openssl/bio.h>
#include <openssl/objects.h>
#include <openssl/asn1.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pkcs12.h>

/* OpenSSH */
#include <libssh2.h>

/* BrByte DATA framework */
#include "libbrb_data.h"

/* Internal includes */
#include "libbrb_ev_aio.h"
#include "libbrb_ev_core.h"
#include "libbrb_ev_comm.h"
#include "libbrb_ev_ipc.h"
#include "libbrb_ev_logger.h"
#include "libbrb_thread.h"

/* Mutex WITNESS control */
#define WITNESS_MT_KQUEUE				0
#define WITNESS_MT_KQUEUE_FD			stdout

/* These are start values. They will be dynamically adjusted */
#define KQCHG_ARR_GROW_STEP				4096
#define KQEV_ARR_GROW_STEP				4096
#define TIMER_ARR_GROW_STEP				1024
#define KQJOB_DEFAULT_COUNT				1024
#define KQBASE_LOGGER_MAX_LOGLINE_SZ	65535 * 4
#define KQEV_IOLOOP_MAXINTERVAL_MS		100 			/* 100ms */
#define KQEV_IOLOOP_MININTERVAL_MS		3 				/* 1ms */
#define KQEV_IOLOOP_MAXINTERVAL_NS		100000  		/* 100ms */
#define KQEV_IOLOOP_MININTERVAL_NS		3000  			/* 1ms */

#define KQEV_TIMEVAL_DELTA(when, now) ((now->tv_sec - when->tv_sec) * 1000 + (now->tv_usec - when->tv_usec) / 1000)

//#define EVFILT_READ		(-1)
//#define EVFILT_WRITE		(-2)
//#define EVFILT_AIO		(-3)	/* attached to aio requests */
//#define EVFILT_VNODE		(-4)	/* attached to vnodes */
//#define EVFILT_PROC		(-5)	/* attached to struct proc */
//#define EVFILT_SIGNAL		(-6)	/* attached to struct proc */
//#define EVFILT_TIMER		(-7)	/* timers */
//#define EVFILT_NETDEV		(-8)	/* network devices */

///* data/hint flags for EVFILT_NETDEV, shared with userspace */
//#define NOTE_LINKUP	0x0001			/* link is up */
//#define NOTE_LINKDOWN	0x0002			/* link is down */
//#define NOTE_LINKINV	0x0004			/* link state is invalid */


/******************************************************************************************************/
/*  KERNEL -> USERLAND EVENT ARRAY STUFF - BEGIN
/******************************************************************************************************/
#define EVBASE_EV_ARR_GROW_IF_NEEDED(ev_base)\
		if ((ev_base->ev_arr.event_arr_cap - 1) < ev_base->ev_arr.event_cur_count)\
		{\
			ev_base->ev_arr.event_arr	= realloc(ev_base->ev_arr.event_arr,\
					(((KQEV_ARR_GROW_STEP + ev_base->ev_arr.event_arr_cap) + 1) * sizeof(struct kevent)));\
					memset(&ev_base->ev_arr.event_arr[ev_base->ev_arr.event_arr_cap], 0, KQEV_ARR_GROW_STEP * sizeof(struct kevent));\
					ev_base->ev_arr.event_arr_cap += KQEV_ARR_GROW_STEP;\
		}
/******************************************************************************************************/
/* USERLAND -> KERNEL CHANGE LIST STUFF - BEGIN
/******************************************************************************************************/
#define EVBASE_CHGLST_GROW_IF_NEEDED(ev_base) \
		if ((ev_base->ke_chg.chg_arr_cap - 1) < ev_base->ke_chg.chg_arr_off) { \
			ev_base->ke_chg.chg_arr		= realloc(ev_base->ke_chg.chg_arr, (((KQCHG_ARR_GROW_STEP + ev_base->ke_chg.chg_arr_cap) + 1) * sizeof(struct kevent))); \
			memset(&ev_base->ke_chg.chg_arr[ev_base->ke_chg.chg_arr_cap], 0, KQCHG_ARR_GROW_STEP * sizeof(struct kevent)); \
			ev_base->ke_chg.chg_arr_cap += KQCHG_ARR_GROW_STEP; }
/******************************************************************************************************/
/* FD STUFF - BEGIN
/******************************************************************************************************/

typedef enum
{
	KQ_BASE_SERIAL_ENGINE,
	KQ_BASE_MULTI_THREADED_ENGINE
} EvBaseKQEngineType;

typedef enum
{
	KQ_BASE_TIMEOUT_AUTO = -1,
	KQ_BASE_TIMEOUT_LASTITEM
} EvKQBaseEngineTimeoutCode;

typedef enum
{
	KQ_BASE_INTERNAL_EVENT_KEVENT_TIMEOUT,
	KQ_BASE_INTERNAL_EVENT_KEVENT_ERROR,
	KQ_BASE_INTERNAL_EVENT_TIMESKEW,
	KQ_BASE_INTERNAL_EVENT_LASTITEM,
} EvBaseKQEngineInternalEvents;

/* WARNING: KQ_CB_TIMEOUT must be aligned with the first events of EvBaseKQEventCode because they will be used as
 * call_back indexes into timeout call_back array. Be sure you know what you are doing if you want to play with these */
typedef enum
{
	COMM_EV_TIMEOUT_READ,
	COMM_EV_TIMEOUT_WRITE,
	COMM_EV_TIMEOUT_BOTH,
	COMM_EV_READ,
	COMM_EV_WRITE,
	COMM_EV_EOF,
	COMM_EV_ERROR,
	COMM_EV_TIMER,
	COMM_EV_FILEMON,
	COMM_EV_DEFER_CHECK_READ,
	COMM_EV_DEFER_CHECK_WRITE,
	COMM_EV_LASTITEM
} EvBaseKQEventCode;

typedef enum
{
	COMM_ACTION_DELETE,
	COMM_ACTION_ADD_VOLATILE,
	COMM_ACTION_ADD_PERSIST,
	COMM_ACTION_ADD_TRANSITION,
	COMM_ACTION_ENABLE,
	COMM_ACTION_DISABLE,
	COMM_ACTION_CLEAR,
	COMM_ACTION_LASTITEM,
} EvBaseKQActionCode;

typedef enum
{
	JOB_ACTION_DELETE,
	JOB_ACTION_ADD_VOLATILE,
	JOB_ACTION_ADD_PERSIST,
	JOB_ACTION_ENABLE,
	JOB_ACTION_DISABLE,
	JOB_ACTION_LASTITEM,
} EvBaseKQJobCode;

typedef enum
{
	JOB_TIME_WDAY_SUNDAY,
	JOB_TIME_WDAY_MONDAY,
	JOB_TIME_WDAY_TUESDAY,
	JOB_TIME_WDAY_WEDNESDAY,
	JOB_TIME_WDAY_THURSDAY,
	JOB_TIME_WDAY_FRIDAY,
	JOB_TIME_WDAY_SATURDAY,
	JOB_TIME_WDAY_MONDAY_FRIDAY,
	JOB_TIME_WDAY_EVERYDAY,
	JOB_TIME_WDAY_LAST_ITEM,
} EvKQBaseJobWeekDays;

typedef enum
{
	JOB_TYPE_IOLOOP,
	JOB_TYPE_TIMED,
	JOB_TYPE_LASTITEM
} EvKQBaseJobTypes;


/*****************************************************/
typedef struct _EvKQBaseConf
{
	EvBaseKQEngineType engine_type;
	int error_count_max;

	struct
	{
		int fd_start;
		int timer_max;
	} arena;

	struct
	{
		int event_loop_ms;
	} timeout;

	struct
	{
		int count_start;
		int count_max;
	} kq_thread;

	struct
	{
		int max_slots;
	} job;

	struct
	{
		int max_slots;
	} aio;

	struct
	{
		unsigned int close_linger:1;
	} onoff;

} EvKQBaseConf;
/*****************************************************/
typedef struct _EvKQBaseStats
{
	struct timeval first_invoke_tv;
	struct timeval cur_invoke_tv;
	struct timeval defer_check_invoke_tv;
	struct timespec monotonic_tp;

	unsigned long kq_invoke_count;
	unsigned long cur_invoke_ts_sec;
	unsigned long cur_invoke_ts_usec;
	unsigned long cur_error_count;
	unsigned long cur_timeout_count;
	int evloop_latency_ms;

	struct
	{
		struct
		{
			unsigned long open_total;
			unsigned long close_total;
			unsigned long open_current;
		} file;

		struct
		{
			unsigned long tx_count;
			unsigned long rx_count;
			unsigned long bytes;
			unsigned long pending;
			unsigned long cancel;
			unsigned long error;
			unsigned long success_sched;
			unsigned long success_notify;
		} opcode[AIOREQ_OPCODE_LASTITEM];
	} aio;

} EvKQBaseStats;

typedef struct _EvKQBase
{
	EvKQBaseLogBase *log_base;
	EvKQBaseConf kq_conf;
	EvBaseKQGenericEventPrototype sig_handler[EV_SIGLASTITEM];
	EvBaseKQGenericEventPrototype internal_ev[KQ_BASE_INTERNAL_EVENT_LASTITEM];
	EvKQBaseStats stats;
	EvBaseKQCrashCBH *crash_cb;


	struct timespec timeout;
	int skew_min_detect_sec;
	int kq_base;
	int kq_thrd_id;
	int ke_chg_idx;

	struct
	{
		DLinkedList list;
		MemArena *arena;
	} fd;

	struct
	{
		MemSlotBase memslot;
		int smallest_interval;
	} timer;

	struct
	{
		MemSlotBase memslot;
		int timer_id;
	} queued_job;

	struct
	{
		DLinkedList list;
	} reg_obj;

	struct
	{
		DLinkedList read_list;
		DLinkedList write_list;
		int interval_check_ms;
	} defer;

	struct
	{
		pthread_mutex_t mutex;
		struct kevent *event_arr;
		int event_arr_cap;
		int event_cur_count;
	} ev_arr;

	struct
	{
		pthread_mutex_t mutex;
		struct kevent *chg_arr;
		int chg_arr_cap;
		int chg_arr_off;
	} ke_chg;

	struct
	{
		EvAIOReqQueue queue;
	} aio;

	/* Flags */
	struct
	{
		unsigned int open:1;
		unsigned int mt_engine:1;
		unsigned int do_shutdown:1;
		unsigned int fd_ev_thrd_bind:1;
		unsigned int defer_read_eof:1;
		unsigned int defer_write_eof:1;
		unsigned int crashing_with_sig:1;
		unsigned int assert_soft:1;
	} flags;

} EvKQBase;
/*****************************************************/
EvKQBaseLogBase *glob_sinal_log_base;
/*****************************************************/

/* ev_kq_base.c */
EvKQBase *EvKQBaseNew(EvKQBaseConf *kq_conf);
void EvKQBaseDestroy(EvKQBase *kq_base);
int EvKQBaseDispatchOnce(EvKQBase *kq_base, int timeout_ms);
void EvKQBaseDispatch(EvKQBase *kq_base, int timeout_ms);
int EvKQInvokeKQueueOnce(EvKQBase *kq_base, int timeout_ms);
void EvKQBaseSetEvent(EvKQBase *kq_base, int fd, int ev_type, int action, EvBaseKQCBH *cb_handler, void *cb_data);
void EvKQBaseClearEvents(EvKQBase *kq_base, int fd);
void EvKQBaseAdjustIOLoopTimeout(EvKQBase *kq_base, int interval_ms);
int EvKQBaseDispatchEventAIO(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, EvAIOReq *aio_req);
int EvKQBaseDispatchEventWrite(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size);
int EvKQBaseDispatchEventRead(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size);
int EvKQBaseDispatchEventWriteEOF(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size);
int EvKQBaseDispatchEventReadEOF(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size);
int EvKQBaseDispatchEventReadError(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int data_size);
int EvKQBaseAssert(EvKQBase *kq_base, const char *func_str, char *file_str, int line, char *msg, ...);

/* ev_kq_object.c */
int EvKQBaseObjectDestroyAll(EvKQBase *kq_base);
int EvKQBaseObjectRegister(EvKQBase *kq_base, EvBaseKQObject *kq_obj);
int EvKQBaseObjectUnregister(EvBaseKQObject *kq_obj);

/* ev_kq_fd.c */
void EvKQBaseFDArenaNew(EvKQBase *kq_base);
void EvKQBaseFDArenaDestroy(EvKQBase *kq_base);
EvBaseKQFileDesc *EvKQBaseFDGrabFromArena(EvKQBase *kq_base, int fd);
int EvKQBaseFDEventInvoke(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int ev_code, int ev_sz, int thrd_id, void *parent);
int EvKQBaseFDGenericInit(EvKQBase *kq_base, int fd, int type);
int EvKQBaseFileFDInit(EvKQBase *kq_base, int fd);
int EvKQBaseSerialPortFDInit(EvKQBase *kq_base, int fd);

int EvKQBaseSocketGenericNew(EvKQBase *kq_base, int af, int style, int protocol, int type);
int EvKQBaseSocketRouteNew(EvKQBase *kq_base, int proto);
int EvKQBaseSocketRAWNew(EvKQBase *kq_base, int af, int proto);
int EvKQBaseSocketTCPNew(EvKQBase *kq_base);
int EvKQBaseSocketUNIXNew(EvKQBase *kq_base);
int EvKQBaseSocketUDPNew(EvKQBase *kq_base);
int EvKQBaseSocketRawNew(EvKQBase *kq_base);
int EvKQBaseSocketCustomNew(EvKQBase *kq_base, int v6);
int EvKQBaseSocketNetmapNew(EvKQBase *kq_base, int flags);
int EvKQBaseSocketUDPNewAndBind(EvKQBase *kq_base, struct in_addr *bindip, unsigned short port);
int EvKQBaseSocketRawNewAndBind(EvKQBase *kq_base);
int EvKQBaseSocketCustomNewAndBind(EvKQBase *kq_base, struct in_addr *bindip, unsigned short port, int v6);


void EvKQBaseSocketClose(EvKQBase *kq_base, int fd);
int EvKQBaseFDCleanupByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
int EvKQBaseFDReadBufferDrain(EvKQBase *kq_base, int fd);
int EvKQBaseSocketBufferReadSizeGet(EvKQBase *kq_base, int fd);
int EvKQBaseSocketBufferWriteSizeGet(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetNoDelay(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetBroadcast(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetKeepAlive(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetLinger(EvKQBase *kq_base, int fd, int linger_sec);
char *EvKQBaseFDDescriptionGetByFD(EvKQBase *kq_base, int fd);
void EvKQBaseFDDescriptionClearByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
void EvKQBaseFDDescriptionClearByFD(EvKQBase *kq_base, int fd);
void EvKQBaseFDDescriptionSetByFD(EvKQBase *kq_base, int fd, char *description, ...);
void EvKQBaseFDDescriptionSet(EvBaseKQFileDesc *kq_fd, char *description, ...);
void EvKQBaseFDDescriptionAppend(EvBaseKQFileDesc *kq_fd, char *description, ...);
int EvKQBaseSocketDrain(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetCloseOnExec(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetNonBlock(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetBlocking(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetReuseAddr(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetReusePort(EvKQBase *kq_base, int fd);
int EvKQBaseSocketSetTCPBufferSize(EvKQBase *kq_base, int fd, int size);
int EvKQBaseSocketSetDstAddr(EvKQBase *kq_base, int fd);

int EvKQBaseSocketSetTOS(EvKQBase *kq_base, int fd, int tos);
int EvKQBaseSocketGetTOS(EvKQBase *kq_base, int fd);
int EvKQBaseSocketBindLocal(EvKQBase *kq_base, int fd, struct sockaddr *sock_addr);
int EvKQBaseSocketBindRemote(EvKQBase *kq_base, int fd, struct sockaddr *sock_addr);

/* ev_kq_signal.c */
void EvKQBaseSignalLogBaseSet(EvKQBaseLogBase *log_base);
void EvKQBaseSetSignal(EvKQBase *kq_base, int signal, int action, EvBaseKQCBH *cb_handler, void *cb_data);
void EvKQBaseIgnoreSignals(EvKQBase *kq_base);
void EvKQBaseInterceptSignals(EvKQBase *kq_base);
void EvKQBaseDefaultSignals(EvKQBase *kq_base);

/* ev_kq_timeout.c */
void EvKQBaseTimeoutSet(EvKQBase *kq_base, int fd, int timeout_type, int timeout_ms, EvBaseKQCBH *cb_handler, void *cb_data);
int EvKQBaseTimeoutInitAllByFD(EvKQBase *kq_base, int fd);
int EvKQBaseTimeoutInitAllByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
int EvKQBaseTimeoutClearByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int timeout_type);
int EvKQBaseTimeoutClear(EvKQBase *kq_base, int fd, int timeout_type);
int EvKQBaseTimeoutClearAll(EvKQBase *kq_base, int fd);

/* ev_kq_timer.c */
void EvKQBaseTimerArenaNew(EvKQBase *kq_base, int max_timer_count);
void EvKQBaseTimerArenaDestroy(EvKQBase *kq_base);
EvBaseKQTimer *EvKQBaseTimerGrabFromArena(EvKQBase *kq_base, int timer_id);
EvBaseKQTimer *EvKQBaseTimerNewFromArena(EvKQBase *kq_base);
int EvKQBaseTimerReleaseFromArena(EvKQBase *kq_base, EvBaseKQTimer *kq_timer);
int EvKQBaseTimerAdd(EvKQBase *kq_base, int action, int interval_ms, EvBaseKQCBH *cb_handler, void *cb_data);
void EvKQBaseTimerCtl(EvKQBase *kq_base, int timer_id, int action);
int EvKQBaseTimerDispatch(EvKQBase *kq_base, int timer_id, int int_data);
int EvKQBaseTimeValSubMsec(struct timeval *when, struct timeval *now);

/* ev_kq_jobs.c */
int EvKQJobsEngineInit(EvKQBase *kq_base, int max_job_count);
int EvKQJobsEngineDestroy(EvKQBase *kq_base);
char *EvKQJobsDerefCBDataByID(EvKQBase *kq_base, int kq_job_id);
char *EvKQJobsDerefCBData(EvKQQueuedJob *kq_job);
int EvKQJobsDispatch(EvKQBase *kq_base);
int EvKQJobsAddTimed(EvKQBase *kq_base, int action, int day, char *time_str, EvBaseKQJobCBH *job_cb_handler, void *job_cbdata);
int EvKQJobsAdd(EvKQBase *kq_base, int action, int ioloop_count, EvBaseKQJobCBH *job_cb_handler, void *job_cbdata);
int EvKQJobsCtl(EvKQBase *kq_base, int action, int kq_job_id);

/* ev_kq_defer.c */
void EvKQBaseDeferDispatch(EvKQBase *kq_base);
int EvKQBaseDeferResetByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
int EvKQBaseDeferReadCheckByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int event_sz);
int EvKQBaseDeferReadRemoveByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);
int EvKQBaseDeferWriteCheckByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd, int event_sz);
int EvKQBaseDeferWriteRemoveByKQFD(EvKQBase *kq_base, EvBaseKQFileDesc *kq_fd);

/* ev_kq_ievents.c */
int EvKQBaseInternalEventSet(EvKQBase *kq_base, int ev_type, int action, EvBaseKQCBH *cb_handler, void *cb_data);
int EvKQBaseInternalEventDispatch(EvKQBase *kq_base, int ev_data, int thrd_id, int ev_type);

#endif /* LIBBRB_EV_KQ_H_ */

