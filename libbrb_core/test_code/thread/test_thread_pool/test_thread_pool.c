/*
 * test_thread_pool.c
 *
 *  Created on: 2013-09-01
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

#include <libbrb_data.h>
#include <libbrb_ev_kq.h>


static int mainThreadPoolBaseInit(void);
static EvBaseKQCBH mainTimerEvent;
static ThreadInstanceJobCB_RetFLOAT ThreadRetFloatJob;
static ThreadInstanceJobFinishCB ThreadRetFloatJobFinish;

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
ThreadPoolBase *glob_thread_pool;

/**************************************************************************************************************************/
int main(void)
{
	EvKQBaseLogBaseConf log_conf;
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	log_conf.flags.double_write			= 1;

	/* Create event base */
	glob_ev_base	= EvKQBaseNew(NULL);
	glob_log_base	= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	mainThreadPoolBaseInit();

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "ThreadPool test on PID [%d]\n", getpid());

	/* Add timer to schedule periodic jobs to THREAD_POOL */
	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 1000, mainTimerEvent, glob_thread_pool);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	return 1;
}
/**************************************************************************************************************************/
static int mainThreadPoolBaseInit(void)
{
	ThreadPoolBaseConfig thread_pool_conf;

	/* Clean up stack */
	memset(&thread_pool_conf, 0, sizeof(ThreadPoolBaseConfig));

	/* Set thread pool configuration */
	thread_pool_conf.worker_count_start 		= 1;
	thread_pool_conf.worker_count_grow			= 1;
	thread_pool_conf.worker_count_max			= 16;
	thread_pool_conf.worker_stack_sz			= (256 * 1024);
	thread_pool_conf.job_max_count				= 8092;
	//thread_pool_conf.log_base					= glob_log_base;
	thread_pool_conf.flags.kevent_finish_notify	= 1;

	glob_thread_pool							= ThreadPoolBaseNew(glob_ev_base, &thread_pool_conf);

	return 1;
}

/**************************************************************************************************************************/
static int mainTimerEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	ThreadPoolJobProto job_proto;
	int job_id;
	int i;

	ThreadPoolBase *thread_pool = cb_data;

	memset(&job_proto, 0, sizeof(ThreadPoolJobProto));

	job_proto.retval_type			= THREAD_JOB_RETVAL_FLOAT;
	job_proto.job_cbh_ptr			= (ThreadInstanceJobCB_Generic *)ThreadRetFloatJob;
	job_proto.job_finish_cbh_ptr	= ThreadRetFloatJobFinish;

	for (i = 0; i < 16; i++)
	{
		job_id = ThreadPoolJobEnqueue(thread_pool, &job_proto);

		if (job_id < 0)
			break;

		//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "Thread JOB [%d] - ENQUEUED\n", job_id);
		continue;
	}

	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 1000, mainTimerEvent, thread_pool);
	return 1;
}
/**************************************************************************************************************************/
static int ThreadRetFloatJobFinish(void *thread_job_ptr, void *notused)
{
	ThreadPoolInstanceJob *thread_job = thread_job_ptr;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "Thread JOB [%d] finish - Executed on THRD_ID [%d] - With result [%.2f]\n",
			thread_job->job_id, thread_job->run_thread_id, thread_job->job_retvalue.float_value);

	return 1;
}
/**************************************************************************************************************************/
static float ThreadRetFloatJob(void *thread_job_ptr, void *notused)
{
	int i;

	ThreadPoolInstanceJob *thread_job	= thread_job_ptr;
	float float_result					= 9/4.5;
	int random_sleep					= (arc4random() % 200);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Thread JOB [%d] - Running on THRD_ID [%d] - Generating result [%.2f]\n",
			thread_job->job_id, thread_job->run_thread_id, float_result);

	/* Just some code to do nothing */
	for (i = 0; i < 65535; i++)
	{
		unsigned short size	= arc4random();
		if (size == 0)
			size = 1;

		char *a				= calloc(1, size);
		memset(a, 0, size - 1);
		free(a);
		continue;
	}

	usleep(random_sleep);

	return float_result;
}
/**************************************************************************************************************************/
