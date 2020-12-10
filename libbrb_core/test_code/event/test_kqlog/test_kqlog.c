/*
 * test_kqlog.c
 *
 *  Created on: 2016-05-25
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

static EvBaseKQCBH KqLogDoLogTimerEvent;
static EvBaseKQCBH KqLogCrashTimerEvent;

int glob_log_line = 0;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvKQBaseLogBaseConf log_conf;
	EvKQBaseConf kq_conf;

	/* Clean STACK */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&kq_conf, 0, sizeof(EvKQBaseConf));

	/* Configure logs */
	log_conf.fileout_pathstr		= "./test_kqlog.log";
	log_conf.flags.double_write		= 1;
	log_conf.flags.mem_keep_logs	= 1;
	log_conf.flags.dump_on_signal	= 1;
	//log_conf.flags.autohash_disable	= 1;

	/* Load limits */
//	//log_conf.mem_limit.bytes_total	= 12800;
//	log_conf.mem_limit.lines_total	= 128;

	/* (REMOVE ME) Set big memory limit */
	log_conf.mem_limit.bytes_total = 167772160L;
	log_conf.mem_limit.lines_total = 65535;

	/* Create event base and log base */
	glob_ev_base				= EvKQBaseNew(&kq_conf);
	glob_log_base				= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_log_base->log_level	= LOGTYPE_WARNING;
	glob_ev_base->log_base		= glob_log_base;

	EvKQBaseSignalLogBaseSet(glob_log_base);

	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 3500, KqLogCrashTimerEvent, NULL);
	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 15, KqLogDoLogTimerEvent, NULL);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "LIMIT [%ld]\n", glob_log_base->mem.bytes_total_limit);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, KQ_BASE_TIMEOUT_AUTO);
	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int KqLogDoLogTimerEvent(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	int i;

	for (i = 0; i < 1000; i++)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "EVENT ON TIMER ID [%d] - LOG [%d]\n", timer_id, glob_log_line);

		glob_log_line++;

		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "EVENT ON TIMER ID [%d] - LOG [%d]\n", timer_id, glob_log_line);

		glob_log_line++;

		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "EVENT ON TIMER ID [%d] - LOG [%d]\n", timer_id, glob_log_line);

		glob_log_line++;
	}

	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 15, KqLogDoLogTimerEvent, NULL);

	return 1;
}
/**************************************************************************************************************************/
static int KqLogCrashTimerEvent(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	if (glob_log_line > (glob_log_base->mem.lines_total_limit + 1000))
		assert(0);

	BRB_ASSERT(glob_ev_base, (0), "Crash timer CB fired\n");

	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 3500, KqLogCrashTimerEvent, NULL);

	return 1;
}
/**************************************************************************************************************************/
