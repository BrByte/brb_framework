/*
 * test_timer.c
 *
 *  Created on: 2016-09-04
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2016 BrByte Software (Oliveira Alves & Amorim LTDA)
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

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
int glob_timerid;

static EvBaseKQCBH mainTimerEvent;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvKQBaseLogBaseConf log_conf;
	EvKQBaseConf kq_conf;

	/* Clean STACK */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&kq_conf, 0, sizeof(EvKQBaseConf));

	/* Configure this KQ_BASE */
	kq_conf.job.max_slots		= 65535;
	kq_conf.aio.max_slots		= 65535;

	/* Create event base and log base */
	glob_ev_base				= EvKQBaseNew(&kq_conf);
	glob_log_base				= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	//glob_log_base->log_level	= LOGTYPE_WARNING;
	glob_ev_base->log_base		= glob_log_base;

	glob_timerid = EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2, mainTimerEvent, NULL);
	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "PID [%d] - Begin on TIMER_ID [%d]\n", getpid(), glob_timerid);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, KQ_BASE_TIMEOUT_AUTO);


	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int mainTimerEvent(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	/* Reschedule TIMER */
	glob_timerid = EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2, mainTimerEvent, NULL);
	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "PID [%d] - Reschedule on TIMER_ID [%d]\n", getpid(), glob_timerid);
	return 1;
}
/**************************************************************************************************************************/
