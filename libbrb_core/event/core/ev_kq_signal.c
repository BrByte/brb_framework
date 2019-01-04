/*
 * ev_kq_signal.c
 *
 *  Created on: 2012-11-19
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

static void EvKQBaseEnqueueSignalChg(EvKQBase *kq_base, unsigned int signal, int action, void *udata);
static EvBaseKQCBH EvKQBaseGenericSignalCBH;
static void EvKQBaseCrashSignalCBH(int signal_code);
static int EvKQBaseCrashAtomicHandler(EvKQBase *kq_base, int signal_code);

/* Will be initialized by set signal */
static EvKQBase *glob_signal_kqbase 	= NULL;
static int glob_signal_thrd_id			= -1;

/**************************************************************************************************************************/
/* Public event set interface
/**************************************************************************************************************************/
void EvKQBaseSignalLogBaseSet(EvKQBaseLogBase *log_base)
{
	glob_sinal_log_base	= log_base;
	return;
}
/**************************************************************************************************************************/
void EvKQBaseSetSignal(EvKQBase *kq_base, int signal, int action, EvBaseKQCBH *cb_handler, void *cb_data)
{
	/* Do not allow invalid SIGNALs in this routine */
	if ((signal < EV_SIGHUP) || (signal >= EV_SIGLASTITEM))
		return;

	/* Save a reference of KQ_BASE to access inside signal handlers */
	glob_signal_kqbase	= kq_base;
	glob_signal_thrd_id = pthread_self();

	switch (action)
	{
	/******************************************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* Set cb_handler and cb_data */
		kq_base->sig_handler[signal].cb_handler_ptr		= cb_handler;
		kq_base->sig_handler[signal].cb_data_ptr		= cb_data;

		/* Mark volatile and enable */
		kq_base->sig_handler[signal].flags.persist		= 0;
		kq_base->sig_handler[signal].flags.enabled		= 1;

		/* Enqueue the change into k_queue change array */
		EvKQBaseEnqueueSignalChg(kq_base, signal, action, kq_base);

		break;
	}
	/******************************************************************/
	case COMM_ACTION_ADD_PERSIST:
	{
		/* Set cb_handler and cb_data */
		kq_base->sig_handler[signal].cb_handler_ptr		= cb_handler;
		kq_base->sig_handler[signal].cb_data_ptr		= cb_data;

		/* Mark persist and enable */
		kq_base->sig_handler[signal].flags.persist		= 1;
		kq_base->sig_handler[signal].flags.enabled		= 1;

		/* Enqueue the change into k_queue change array */
		EvKQBaseEnqueueSignalChg(kq_base, signal, action, kq_base);

		break;
	}
	/******************************************************************/
	case COMM_ACTION_ENABLE:
	{
		if (!kq_base->sig_handler[signal].flags.enabled)
		{
			kq_base->sig_handler[signal].flags.enabled		= 1;

			/* Enqueue the change into k_queue change array */
			EvKQBaseEnqueueSignalChg(kq_base, signal, action, kq_base);
		}
		break;
	}
	/******************************************************************/
	case COMM_ACTION_DISABLE:
	{
		if (kq_base->sig_handler[signal].flags.enabled)
		{
			kq_base->sig_handler[signal].flags.enabled		= 0;

			/* Enqueue the change into k_queue change array */
			EvKQBaseEnqueueSignalChg(kq_base, signal, action, kq_base);
		}
		break;
	}
	/******************************************************************/
	case COMM_ACTION_DELETE:
	{
		if (kq_base->sig_handler[signal].flags.enabled)
		{
			/* Set cb_handler and cb_data */
			kq_base->sig_handler[signal].cb_handler_ptr		= NULL;
			kq_base->sig_handler[signal].cb_data_ptr		= NULL;

			/* Mark persist and enable */
			kq_base->sig_handler[signal].flags.persist		= 0;
			kq_base->sig_handler[signal].flags.enabled		= 0;

			/* Enqueue the change into k_queue change array */
			EvKQBaseEnqueueSignalChg(kq_base, signal, action, kq_base);
		}

		break;

	}
	/******************************************************************/
	}

	return;
}
/**************************************************************************************************************************/
void EvKQBaseIgnoreSignals(EvKQBase *kq_base)
{
	sigset_t new;
	int i;

	/* Save a reference of KQ_BASE to access inside signal handlers */
	glob_signal_kqbase	= kq_base;
	glob_signal_thrd_id = pthread_self();

	sigemptyset(&new);

	/* Walk from zero thru EV_SIGLASTITEM intercepting selected signal numbers */
	for (i = 1; i <= EV_SIGLASTITEM; i++)
	{
		signal(i, SIG_IGN);
		sigaddset(&new, i);
		EvKQBaseSetSignal(kq_base, i, COMM_ACTION_DELETE, NULL, NULL);
		continue;
	}

	/* Block all signals for this THREAD */
	pthread_sigmask(SIG_BLOCK, &new, NULL);
	return;
}
/**************************************************************************************************************************/
void EvKQBaseInterceptSignals(EvKQBase *kq_base)
{
	int i;

	/* Save a reference of KQ_BASE to access inside signal handlers */
	glob_signal_kqbase	= kq_base;
	glob_signal_thrd_id = pthread_self();

	/* Walk from zero thru EV_SIGLASTITEM intercepting selected signal numbers */
	for (i = 1; i <= EV_SIGTERM; i++)
	{
		/* Do not intercept this signal - SIGUSR1 and 2 are PROFILER_DEFAULT, SIG_KILL and SIG_STOP are not INTERCEPTABLE */
		if (EV_SIGCHLD == i || EV_SIGUSR1 == i || EV_SIGUSR2 == i || EV_SIGKILL == i || EV_SIGSTOP == i)
			continue;

		/* Also, send CRASH signals to internal CRASH notifier */
		if (EV_SIGBUS == i || EV_SIGSEGV == i || EV_SIGABRT == i || EV_SIGFPE == i)
		{
			signal(i, EvKQBaseCrashSignalCBH);
			continue;
		}

		/* Ignore regular signal and intercept signal using event base */
		signal(i, SIG_IGN);
		EvKQBaseSetSignal(kq_base, i, COMM_ACTION_ADD_VOLATILE, EvKQBaseGenericSignalCBH, NULL);

		continue;
	}

	/* Ask the kernel to reap ZOOMBIES for us */
	sigaction(EV_SIGCHLD, &(struct sigaction){.sa_handler = SIG_IGN}, NULL);

	return;
}
/**************************************************************************************************************************/
void EvKQBaseDefaultSignals(EvKQBase *kq_base)
{
	int i						= 0;

	/* Walk from zero thru EV_SIGLASTITEM intercepting selected signal numbers */
	for (i = 1; i <= EV_SIGTERM; i++)
	{
		/* Do not intercept this signal - SIG_KILL and SIG_STOP are not INTERCEPTABLE */
		if (EV_SIGKILL == i || EV_SIGSTOP == i)
			continue;

		/* Set back to SYS default crash handlers */
		signal(i, SIG_DFL);
		continue;
	}

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void EvKQBaseEnqueueSignalChg(EvKQBase *kq_base, unsigned int signal, int action, void *udata)
{
	struct kevent *kev_ptr;

	/* Check if change list need to grow to save more filter changes */
	EVBASE_CHGLST_GROW_IF_NEEDED(kq_base);
	kev_ptr = &kq_base->ke_chg.chg_arr[(kq_base->ke_chg.chg_arr_off)];

	switch (action)
	{
	/***********************************************/
	case COMM_ACTION_DELETE:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)signal, EVFILT_SIGNAL, EV_DELETE, 0, 0, udata);
		break;
	}
	/***********************************************/
	case COMM_ACTION_ADD_PERSIST:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)signal, EVFILT_SIGNAL, EV_ADD, 0, 0, udata);
		break;
	}
	/***********************************************/
	case COMM_ACTION_ADD_VOLATILE:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)signal, EVFILT_SIGNAL, (EV_ADD | EV_ONESHOT), 0, 0, udata);
		break;
	}
	/***********************************************/
	case COMM_ACTION_ENABLE:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)signal, EVFILT_SIGNAL, EV_ENABLE, 0, 0, udata);
		break;
	}
	/***********************************************/
	case COMM_ACTION_DISABLE:
	{
		/* Fill kev_ptr index */
		EV_SET(kev_ptr, (uintptr_t)signal, EVFILT_SIGNAL, EV_DISABLE, 0, 0, udata);
		break;
	}
	/***********************************************/
	}

	/* Update chg_arr index */
	kq_base->ke_chg.chg_arr_off++;
	return;
}
/**************************************************************************************************************************/
static int EvKQBaseGenericSignalCBH(int signal, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	const EvBaseSignalDescriptor *signal_desc;
	EvKQBase *kq_base = base_ptr;

	if (signal >= EV_SIGLASTITEM)
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "Received signal [%d] - UNKNOWN SIGNAL\n", signal);
		return 0;
	}

	signal_desc = &kqbase_glob_signal_table[signal];

	switch(signal_desc->action_code)
	{
	case EV_SIGACTION_TERMINATE_PROCESS:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "Received signal [%d]-[%s] - Graceful shutdown\n", signal, signal_desc->signal_str);
		kq_base->flags.do_shutdown = 1;
		break;
	}

	case EV_SIGACTION_DUMP_CORE:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Received signal [%d]-[%s] - Dumping the core\n", signal, signal_desc->signal_str);
		abort();
		break;
	}

	case EV_SIGACTION_DISCARD_SIGNAL:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Received signal [%d]-[%s] - Discarding\n", signal, signal_desc->signal_str);
		break;
	}

	case EV_SIGACTION_STOP_PROCESS:
	{
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "Received signal [%d]-[%s] - Graceful shutdown\n", signal, signal_desc->signal_str);
		kq_base->flags.do_shutdown = 1;
		break;
	}
	default:
		KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "Received signal [%d]-[%s] - UNEXPECTED SIGNAL\n", signal, signal_desc->signal_str);
		abort();
		break;

	}

	/* Intercept signal using event base */
	EvKQBaseSetSignal(kq_base, signal, COMM_ACTION_ADD_VOLATILE, EvKQBaseGenericSignalCBH, NULL);

	return 1;
}
/**************************************************************************************************************************/
static void EvKQBaseCrashSignalCBH(int signal_code)
{
	EvKQBaseLogBase *log_base	= glob_sinal_log_base;
	int i						= 0;

	/* Restore default signals */
	EvKQBaseDefaultSignals(glob_signal_kqbase);

	/* No event base set, leave */
	if (!glob_signal_kqbase)
		return;

	/* We need to be atomic */
	if (glob_signal_kqbase->flags.crashing_with_sig)
		return;

	/* We are coming down */
	glob_signal_kqbase->flags.crashing_with_sig = 1;

	/* Close STDIN, OUT and ERR ASAP to signal any IPC controller that we are going down */
	close(KQBASE_STDIN);
	close(KQBASE_STDOUT);
	close(KQBASE_STDERR);

	/* Jump into USER CRASH_CB, if it exists */
	if (glob_signal_kqbase->crash_cb)
		glob_signal_kqbase->crash_cb(glob_signal_kqbase, signal_code);

	/* INTERNAL ATOMIC CRASH */
	EvKQBaseCrashAtomicHandler(glob_signal_kqbase, signal_code);

	/* Crash */
	abort();

	/* Never reached */
	return;
}
/**************************************************************************************************************************/
static int EvKQBaseCrashAtomicHandler(EvKQBase *kq_base, int signal_code)
{
	EvKQBaseLogBase *log_base = glob_sinal_log_base;

	KQBASE_LOG_PRINTF(log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "CRASH - Dying with signal [%d]\n", signal_code);

	/* Begin DUMP logs if operator asked us to */
	EvKQBaseLoggerMemDumpOnCrash(log_base);
	return 1;
}
/**************************************************************************************************************************/
