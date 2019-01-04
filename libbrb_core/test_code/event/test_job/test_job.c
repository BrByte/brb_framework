/*
 * test_job.c
 *
 *  Created on: 2015-06-15
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2015 BrByte Software (Oliveira Alves & Amorim LTDA)
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

static EvBaseKQJobCBH main_JobCB;

static int main_ConvertTimeToSeconds(char *time_str);
static int main_ConvertSecondsToTime(int seconds, char *time_str);

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvKQBaseLogBaseConf log_conf;
	EvKQBaseConf kq_conf;
	unsigned char random;
	int job_id;
	int i;
	char job_time_str[10];
	char job_time_init_str[10];
	char job_time_end_str[10];

	int num_jobs;
	int minute_init;
	int minute_end;
	int minute_delta;
	int minute_need;
	int minute_job;
	int job_time_minute;

	/* Clean STACK */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&kq_conf, 0, sizeof(EvKQBaseConf));

	/* Configure this KQ_BASE */
	kq_conf.job.max_slots		= 65535;
	kq_conf.aio.max_slots		= 65535;

	/* Create event base and log base */
	glob_ev_base				= EvKQBaseNew(&kq_conf);
	glob_log_base				= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_log_base->log_level	= LOGTYPE_WARNING;
	glob_ev_base->log_base		= glob_log_base;

	sprintf((char *)&job_time_init_str, "02:50:00");
	sprintf((char *)&job_time_end_str, "04:50:00");

	num_jobs					= 8;
	minute_init 				= main_ConvertTimeToSeconds(job_time_init_str);
	minute_end 					= main_ConvertTimeToSeconds(job_time_end_str);
	minute_delta				= minute_end - minute_init;
	minute_job					= 600;
	minute_need					= num_jobs * minute_job;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "MINUTE - INIT [%d] - END [%d] - DELTA [%d] - NEED [%d]\n",
			minute_init, minute_end, minute_delta, minute_need);

	if (minute_need > minute_delta)
	{
		minute_job 				= minute_delta / num_jobs;

		if (minute_job <= 0)
			minute_job 			= 1;
	}

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "MINUTE - JOB [%d]\n", minute_job);

	int job_min_id 					= 0;

	for (i = 0, job_min_id = 0; i < num_jobs; i++, job_min_id++)
	{
		job_time_minute			= minute_init + (minute_job * job_min_id);

		if (job_time_minute > minute_end)
			job_min_id				= 0;

		job_time_minute			= minute_init + (minute_job * job_min_id);

		main_ConvertSecondsToTime(job_time_minute, (char *)&job_time_str);

		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "JOB ID [%d] - [%s]\n", job_min_id, (char *)&job_time_str);
	}

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "TEST_JOB on PID [%d]\n", getpid());

	job_id = EvKQJobsAddTimed(glob_ev_base, JOB_ACTION_ADD_PERSIST, JOB_TIME_WDAY_EVERYDAY, "07:05:20", main_JobCB, NULL);
	job_id = EvKQJobsAddTimed(glob_ev_base, JOB_ACTION_ADD_PERSIST, JOB_TIME_WDAY_MONDAY, "07:05:20", main_JobCB, NULL);

	job_id = EvKQJobsAdd(glob_ev_base, JOB_ACTION_ADD_VOLATILE, 60, main_JobCB, NULL);
	job_id = EvKQJobsAdd(glob_ev_base, JOB_ACTION_ADD_PERSIST, 60, main_JobCB, NULL);


//	for (i = 0; i < 2; i++)
//	{
//		while(1)
//		{
//			random = arc4random();
//
//			if ((random > 0) && (random < 32))
//				break;
//		}
//
//
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "[%d] - Schedule JOB_ID [%d] - RANDOM [%d]\n", i, job_id, random);
//
//		if (job_id < 0)
//		{
//			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "[%d] - Failed adding JOB\n", i);
//			assert(0);
//		}
//
//		continue;
//	}

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 5);

	return 1;

}
/**************************************************************************************************************************/
static int main_JobCB(void *kq_job_ptr, void *cbdata_ptr)
{
	EvKQQueuedJob *kq_job = kq_job_ptr;
	unsigned char random;
	int job_id;
	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "Job Callback with ID [%d]\n", kq_job->job.id);

//	while(1)
//	{
//		random = arc4random();
//
//		if ((random > 0) && (random < 32))
//			break;
//	}
//
//	job_id = EvKQJobsAdd(glob_ev_base, JOB_ACTION_ADD_VOLATILE, random, main_JobCB, NULL);
//
//	if (job_id < 0)
//	{
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Failed adding JOB - RANDOM [%d]\n", random);
//		assert(0);
//	}
//
//	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Schedule JOB_ID [%d] - RANDOM [%d]\n", job_id, random);


	return 1;
}
/**************************************************************************************************************************/
static int main_ConvertTimeToSeconds(char *time_str)
{
	int time_min;
	int time_sz;

	int time_h;
	int time_i;
	int time_s;

	time_sz			= time_str ? strlen(time_str) : 0;

	/* Check timers a */
	if ((time_sz != 8) || (time_str[2] != ':') || (time_str[5] != ':'))
	{
		return -1;
	}

	/* convert timer */
	time_str[2] 	= '\0';
	time_str[5] 	= '\0';

	time_h			= atoi(time_str);
	time_i			= atoi(&time_str[3]);
	time_s			= atoi(&time_str[6]);

	if ((time_h < 0) || (time_h > 23) || (time_i < 0) || (time_i > 59) || (time_s > 0) || (time_s > 59))
	{
		return -1;
	}

	/* calculate time */
	time_min		= ((time_h * 3600) + (time_i * 60) + time_s);

	return time_min;
}
/**************************************************************************************************************************/
static int main_ConvertSecondsToTime(int seconds, char *time_str)
{
	int time_h;
	int time_i;
	int time_s;

	if (!time_str)
		return 0;

	time_h			= seconds / 3600;
	time_i			= (seconds % 3600) / 60;
	time_s			= (seconds % 60);

	sprintf(time_str, "%0.2d:%0.2d:%0.2d", time_h, time_i, time_s);

	return 1;
}
/**************************************************************************************************************************/
