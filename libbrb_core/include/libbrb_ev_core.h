/*
 * libbrb_ev_core.h
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

#ifndef LIBBRB_EV_CORE_H_
#define LIBBRB_EV_CORE_H_

#define LIBBRB_TTYDEV						"/dev/tty"
#define LIBBRB_EV_KQFD_MAX_DESCRIPTION_SZ	128
#define LIBBRB_EV_KQFD_MAX_HOOKS_PER_EV		8

/******************************************************************************************************/
/*  MUTEX PRIMITIVES
/******************************************************************************************************/
#if WITNESS_MT_KQUEUE
#define MUTEX_INIT(mutex, mutex_name)	fprintf (WITNESS_MT_KQUEUE_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[INIT] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		pthread_mutex_init((pthread_mutex_t*)(&mutex), 0);

#define MUTEX_LOCK(mutex, mutex_name)	fprintf (WITNESS_MT_KQUEUE_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[LOCK] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		pthread_mutex_lock((pthread_mutex_t*)(&mutex));

#define MUTEX_TRYLOCK(mutex, mutex_name, state)	fprintf (WITNESS_MT_KQUEUE_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[TRYLOCK] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		state = pthread_mutex_trylock((pthread_mutex_t*)(&mutex));

#define MUTEX_UNLOCK(mutex, mutex_name)	fprintf (WITNESS_MT_KQUEUE_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[UNLOCK] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		pthread_mutex_unlock((pthread_mutex_t*)(&mutex));

#define MUTEX_DESTROY(mutex, mutex_name) fprintf (WITNESS_MT_KQUEUE_FD, "WITNESS [THRD: %X] - [%s]-[%s]-[DESTROY] -> ([%s] line [%d])\n",\
		pthread_self(), __func__, mutex_name, __FILE__, __LINE__); \
		pthread_mutex_destroy((pthread_mutex_t*)(&mutex));

#else
#define MUTEX_INIT(mutex, mutex_name)			pthread_mutex_init( (pthread_mutex_t*)(&mutex), 0);
#define MUTEX_LOCK(mutex, mutex_name)			pthread_mutex_lock( (pthread_mutex_t*)(&mutex));
#define MUTEX_TRYLOCK(mutex, mutex_name, state)	state = pthread_mutex_trylock( (pthread_mutex_t*)(&mutex));
#define MUTEX_UNLOCK(mutex, mutex_name)			pthread_mutex_unlock( (pthread_mutex_t*)(&mutex));
#define MUTEX_DESTROY(mutex, mutex_name)		pthread_mutex_destroy((pthread_mutex_t*)(&mutex));
#endif
/******************************************************************************************************/
typedef int EvBaseKQCBH(int, int, int, void*, void*);
typedef int EvBaseKQJobCBH(void *, void *);
typedef void *EvBaseThreadMainLoopFunc (void *ptr);
typedef int EvBaseKQObjDestroyCBH(void *, void *);
typedef void EvBaseKQCrashCBH(void *, int);
/******************************************************************************************************/
typedef enum
{
	COMM_KQ_ERROR,
	COMM_KQ_TIMEOUT,
	COMM_KQ_OK,
	COMM_KQ_LASTITEM,
} EvBaseKQStatusCode;

typedef enum
{
	KQ_CB_HOOK_ADD_BEFORE,
	KQ_CB_HOOK_ADD_AFTER,
	KQ_CB_HOOK_ADD_LASTITEM
} EvBaseKQHookAddCodes;

typedef enum
{
	KQ_CB_HANDLER_READ,
	KQ_CB_HANDLER_WRITE,
	KQ_CB_HANDLER_EOF,
	KQ_CB_HANDLER_READ_ERROR,
	KQ_CB_HANDLER_WRITE_ERROR,
	KQ_CB_HANDLER_FILEMON,
	KQ_CB_HANDLER_DEFER_CHECK_READ,
	KQ_CB_HANDLER_DEFER_CHECK_WRITE,
	KQ_CB_HANDLER_LASTITEM
} EvBaseKQCBHandlerIndex;

typedef enum
{
	KQ_CB_TIMEOUT_READ,
	KQ_CB_TIMEOUT_WRITE,
	KQ_CB_TIMEOUT_BOTH,
	KQ_CB_TIMEOUT_LASTITEM
} EvBaseKQCBTimeoutHandlerIndex;

typedef enum
{
	EV_SIGACTION_TERMINATE_PROCESS,
	EV_SIGACTION_DUMP_CORE,
	EV_SIGACTION_DISCARD_SIGNAL,
	EV_SIGACTION_STOP_PROCESS,
	EV_SIGACTION_LASTITEM
} EvBaseSignalActionCodes;

typedef enum
{
	EV_OBJ_DNS_RESOLVER = 0x31337,
	EV_OBJ_FTP_CLIENT,
	EV_OBJ_ICMP_PINGER,
	EV_OBJ_SSH_CLIENT,
	EV_OBJ_TCP_SERVER,
	EV_OBJ_TCP_CLIENT,
	EV_OBJ_TCP_CLIENT_POOL,
	EV_OBJ_UNIX_SERVER,
	EV_OBJ_UNIX_CLIENT,
	EV_OBJ_UNIX_CLIENT_POOL,
	EV_OBJ_LASTITEM
} EvBaseKQObjects;

typedef enum
{
	FD_TYPE_NONE,
	FD_TYPE_LOG,
	FD_TYPE_FILE,
	FD_TYPE_TCP_SOCKET,
	FD_TYPE_UDP_SOCKET,
	FD_TYPE_RAW_SOCKET,
	FD_TYPE_UNIX_SOCKET,
	FD_TYPE_ROUTE_SOCKET,
	FD_TYPE_PIPE,
	FD_TYPE_SERIALPORT,
	FD_TYPE_NETMAP_SOCKET,
	FD_TYPE_LASTITEM
} EvBaseFDType;

typedef enum
{
	KQBASE_STDIN,
	KQBASE_STDOUT,
	KQBASE_STDERR,
	KQBASE_LASTITEM,
} EvKQBaseIODefaultCodes;

typedef enum
{
	EV_UNINITIALIZED,	/* 00 */
	EV_SIGHUP,			/* 01 */
	EV_SIGINT,			/* 02 */
	EV_SIGQUIT,			/* 03 */
	EV_SIGILL,			/* 04 */
	EV_SIGTRAP,			/* 05 */
	EV_SIGABRT,			/* 06 */
	EV_SIGEMT,			/* 07 */
	EV_SIGFPE,			/* 08 */
	EV_SIGKILL,			/* 09 */
	EV_SIGBUS,			/* 10 */
	EV_SIGSEGV,			/* 11 */
	EV_SIGSYS,			/* 12 */
	EV_SIGPIPE,			/* 13 */
	EV_SIGALRM,			/* 14 */
	EV_SIGTERM,			/* 15 */
	EV_SIGURG,			/* 16 */
	EV_SIGSTOP,			/* 17 */
	EV_SIGTSTP,			/* 18 */
	EV_SIGCONT,			/* 19 */
	EV_SIGCHLD,			/* 20 */
	EV_SIGTTIN,			/* 21 */
	EV_SIGTTOU,			/* 22 */
	EV_SIGIO,			/* 23 */
	EV_SIGXCPU,			/* 24 */
	EV_SIGXFSZ,			/* 25 */
	EV_SIGVTALRM,		/* 26 */
	EV_SIGPROF,			/* 27 */
	EV_SIGWINCH,		/* 28 */
	EV_SIGINFO,			/* 29 */
	EV_SIGUSR1,			/* 30 */
	EV_SIGUSR2,			/* 31 */
	EV_SIGTHR,			/* 32 */
	EV_SIGLIBRT,		/* 33 */
	EV_SIGLASTITEM		/* 34 */
} EvBaseSignalCodes;
/*****************************************************/
typedef struct _EvBaseKQGenericEventPrototype
{
	pthread_mutex_t mutex;
	EvBaseKQCBH *cb_handler_ptr;
	void *cb_parent_ptr;
	void *cb_data_ptr;
	int ev_code;

	struct
	{
		struct timeval tv;
		unsigned long count;
		unsigned long loop;
		unsigned long ts;
	} run;

	struct
	{
		unsigned int persist:1;
		unsigned int enabled:1;
		unsigned int mutex_init:1;
	} flags;

} EvBaseKQGenericEventPrototype;
/*****************************************************/
typedef struct _EvBaseKQGenericTimeoutPrototype
{
	EvBaseKQCBH *cb_handler_ptr;
	void *cb_data_ptr;
	long when_ts;

	int timeout_ms;
	int timer_id;
} EvBaseKQGenericTimeoutPrototype;
/*****************************************************/
typedef struct _EvBaseKQGenericDeferPrototype
{
	struct
	{
		DLinkedListNode node;
		long pending_bytes;
		long count;

		struct timeval begin_tv;
		struct timeval check_tv;
		struct timeval set_tv;
	} write;

	struct
	{
		DLinkedListNode node;
		long pending_bytes;
		long count;

		struct timeval begin_tv;
		struct timeval check_tv;
		struct timeval set_tv;
	} read;

	struct
	{
		unsigned int foo:1;
	} flags;

} EvBaseKQGenericDeferPrototype;
/*****************************************************/
typedef struct _EvBaseKQObject
{
	/* Must be first ITEM!! */
	EvBaseKQObjects code;
	DLinkedListNode node;
	struct _EvKQBase *kq_base;

	struct
	{
		EvBaseKQObjDestroyCBH *destroy_cbh;
		void *destroy_cbdata;
		void *ptr;
		int ref_count;
	} obj;

	struct
	{

		unsigned int registered:1;
		unsigned int destroy_invoked:1;
	} flags;
} EvBaseKQObject;
/*****************************************************/
typedef struct _EvBaseKQFileDescAIOFlags
{
	struct
	{
		struct
		{
			unsigned int pending:1;
			unsigned int error:1;
			unsigned int canceled:1;
			unsigned int eof:1;
		} read;

		struct
		{
			unsigned int pending:1;
			unsigned int error:1;
			unsigned int canceled:1;
			unsigned int eof:1;
		} write;
	} ev;

} EvBaseKQFileDescAIOFlags;

typedef struct _EvBaseKQFileDesc
{
	EvBaseKQGenericEventPrototype cb_handler[KQ_CB_HANDLER_LASTITEM];
	EvBaseKQGenericTimeoutPrototype timeout[KQ_CB_TIMEOUT_LASTITEM];
	EvBaseKQGenericDeferPrototype defer;
	DLinkedListNode node;
	pthread_mutex_t mutex;

	struct
	{
		char *description_str;
		int description_sz;
		int type;
		int num;
		int tos;
	} fd;

	struct
	{
		struct
		{
			unsigned long ioloop_id;
			int job_id;
		} close;
	} notify;

	/* Flags */
	struct
	{
		EvBaseKQFileDescAIOFlags aio;
		unsigned int active:1;
		unsigned int closing:1;
		unsigned int closed:1;
		unsigned int so_reuse_addr:1;
		unsigned int so_reuse_port:1;
		unsigned int so_nonblocking:1;
		unsigned int so_listen:1;
		unsigned int so_closeonexec:1;
		unsigned int so_write_eof:1;
		unsigned int so_read_eof:1;
		unsigned int so_write_error:1;
		unsigned int so_read_error:1;
		unsigned int defer_read:1;
		unsigned int defer_write:1;
		unsigned int defer_read_transition_set:1;
		unsigned int defer_write_transition_set:1;
		unsigned int bind_local:1;
		unsigned int bind_remote:1;
	} flags;

} EvBaseKQFileDesc;
/*****************************************************/
typedef struct _EvBaseKQTimer
{
	int timer_msec;
	int timer_id;

	struct
	{
		EvBaseKQCBH *timer;
		void *timer_data;
	} cb_handler;

	struct
	{
		unsigned int in_use:1;
		unsigned int active:1;
		unsigned int enabled:1;
		unsigned int persist:1;
	} flags;

} EvBaseKQTimer;
/*****************************************************/
typedef struct _EvKQQueuedJob
{
	DLinkedListNode node;
	struct _EvKQBase *ev_base;
	char generic_cbbuf[1024];

	void *user_data;
	long user_long;
	int user_int;

	struct
	{
		EvBaseKQJobCBH *cb_func;
		void *cb_data;
		int id;

		struct
		{
			char str[10]; /* nn:nn:nn */
			long invoke_ts;
			int invoke_day;
			int target_day;
			int mask;
		} time;

		struct
		{
			int ioloop_target;
			int ioloop_cur;
		} count;
	} job;

	struct
	{
		unsigned int active:1;
		unsigned int enabled:1;
		unsigned int persist:1;
		unsigned int canceled:1;
		unsigned int job_ioloop:1;
		unsigned int job_timed:1;
	} flags;

} EvKQQueuedJob;
/*****************************************************/
typedef struct _EvBaseSignalDescriptor
{
	int signal_code;
	int action_code;
	char *signal_str;
	char *action_str;
} EvBaseSignalDescriptor;
/*****************************************************/

/******************************************************************************************************/
static const char *glob_evfilt_name[]	= {"EVFILT_READ", "EVFILT_WRITE", "EVFILT_TIMER", "EVFILT_AIO", "EVFILT_VNODE", "EVFILT_PROC", "EVFILT_SIGNAL",	NULL };

static const EvBaseSignalDescriptor kqbase_glob_signal_table[] =
{
		{EV_UNINITIALIZED, EV_SIGACTION_LASTITEM, "Uninitialized", "Uninitialized"},										/* 00 */
		{EV_SIGHUP, EV_SIGACTION_TERMINATE_PROCESS, "Terminal Line HANGUP", "Terminate Process"},							/* 01 */
		{EV_SIGINT, EV_SIGACTION_TERMINATE_PROCESS, "Interrupt Program", "Terminate Process"},								/* 02 */
		{EV_SIGQUIT, EV_SIGACTION_DUMP_CORE, "Quit Program", "Dump Core"},													/* 03 */
		{EV_SIGILL, EV_SIGACTION_DUMP_CORE, "Illegal Instruction", "Dump Core"},											/* 04 */
		{EV_SIGTRAP, EV_SIGACTION_DUMP_CORE, "Trace/Breakpoint Trap", "Dump Core"},											/* 05 */
		{EV_SIGABRT, EV_SIGACTION_DUMP_CORE, "Abort Program (formerly SIGIOT)", "Dump Core"},								/* 06 */
		{EV_SIGEMT, EV_SIGACTION_DUMP_CORE, "Emulate Instruction Executed", "Dump Core"},									/* 07 */
		{EV_SIGFPE, EV_SIGACTION_DUMP_CORE, "Arithmetic/Floating-point exception", "Dump Core"},							/* 08 */
		{EV_SIGKILL, EV_SIGACTION_TERMINATE_PROCESS, "Kill Program", "Terminate Process"},									/* 09 */
		{EV_SIGBUS, EV_SIGACTION_DUMP_CORE, "Bus ERROR", "Dump Core"},														/* 10 */
		{EV_SIGSEGV, EV_SIGACTION_DUMP_CORE, "Segmentation Fault", "Dump Core"},											/* 11 */
		{EV_SIGSYS, EV_SIGACTION_DUMP_CORE, "Non-existent SYSCALL Invoked", "Dump Core"},									/* 12 */
		{EV_SIGPIPE, EV_SIGACTION_DISCARD_SIGNAL, "Write On A Pipe With No Reader", "Discard Signal"},						/* 13 */
		{EV_SIGALRM, EV_SIGACTION_TERMINATE_PROCESS, "Real-Time Timer Expired", "Terminate Process"},						/* 14 */
		{EV_SIGTERM, EV_SIGACTION_TERMINATE_PROCESS, "Software Termination Signal", "Terminate Process"},					/* 15 */
		{EV_SIGURG, EV_SIGACTION_DISCARD_SIGNAL, "Urgent Condition Present on Socket", "Discard Signal"},					/* 16 */
		{EV_SIGSTOP, EV_SIGACTION_STOP_PROCESS, "STOP (Cannot Be Caught or Ignored)", "Stop Process"},						/* 17 */
		{EV_SIGTSTP, EV_SIGACTION_STOP_PROCESS, "STOP (Signal Generated From Keyboard", "Stop Process"},					/* 18 */
		{EV_SIGCONT, EV_SIGACTION_DISCARD_SIGNAL, "Continue After Stop", "Discard Signal"},									/* 19 */
		{EV_SIGCHLD, EV_SIGACTION_DISCARD_SIGNAL, "Child Status Has Changed", "Discard Signal"},							/* 20 */
		{EV_SIGTTIN, EV_SIGACTION_STOP_PROCESS, "Background	READ Attempted From Control Terminal", "Stop Process"},			/* 21 */
		{EV_SIGTTOU, EV_SIGACTION_STOP_PROCESS, "Background	WRITE Attempted From Control Terminal", "Stop Process"},		/* 22 */
		{EV_SIGIO, EV_SIGACTION_DISCARD_SIGNAL, "I/O is Possible On	a Descriptor", "Discard Signal"},						/* 23 */
		{EV_SIGXCPU, EV_SIGACTION_TERMINATE_PROCESS, "CPU Time Limit Exceeded", "Terminate Process"},						/* 24 */
		{EV_SIGXFSZ, EV_SIGACTION_TERMINATE_PROCESS, "FILE Size Limit Exceeded", "Terminate Process"},						/* 25 */
		{EV_SIGVTALRM, EV_SIGACTION_TERMINATE_PROCESS, "Virtual Time Alarm", "Terminate Process"},							/* 26 */
		{EV_SIGPROF, EV_SIGACTION_DISCARD_SIGNAL, "Profiling Timer Alarm", "Discard Signal"},								/* 27 */
		{EV_SIGWINCH, EV_SIGACTION_DISCARD_SIGNAL, "Window Size Change", "Discard Signal"},									/* 28 */
		{EV_SIGINFO, EV_SIGACTION_DISCARD_SIGNAL, "Status Request From Keyboard", "Discard Signal"},						/* 29 */
		{EV_SIGUSR1, EV_SIGACTION_TERMINATE_PROCESS, "User Defined Signal 1", "Terminate Process"},							/* 30 */
		{EV_SIGUSR2, EV_SIGACTION_TERMINATE_PROCESS, "User Defined Signal 2", "Terminate Process"},							/* 31 */
		{EV_SIGTHR, EV_SIGACTION_TERMINATE_PROCESS, "Thread Interrupt", "Terminate Process"},								/* 32 */
		{EV_SIGLIBRT, EV_SIGACTION_TERMINATE_PROCESS, "Real-Time Library Interrupt", "Terminate Process"},					/* 33 */
		{EV_SIGLASTITEM, EV_SIGACTION_LASTITEM, NULL, NULL}
};
/******************************************************************************************************/


#endif /* LIBBRB_EV_CORE_H_ */
