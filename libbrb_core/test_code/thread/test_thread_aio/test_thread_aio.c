/*
 * test_thread_aio.c
 *
 *  Created on: 2014-11-03
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
#include <libbrb_core.h>

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
ThreadAIOBase *glob_thrdaio_base;

static int mainThreadAIOBaseInit(void);
static EvAIOReqCBH mainAIOFinishCB;
static EvAIOReqCBH mainAIOFinishCB_MB;

int glob_loop_count = 16;

/**************************************************************************************************************************/
int main(void)
{
	EvAIOReq dst_aio_req;
	EvKQBaseLogBaseConf log_conf;
	char *dst_buf;
	int file_fd;
	int i;

	char *random_data = "slakjdalskdjsalkdjsalkdjsalkdjasd";

	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));

	log_conf.flags.double_write			= 1;

	/* Create event base */
	glob_ev_base	= EvKQBaseNew(NULL);
	glob_log_base	= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
//	file_fd			= open("./teste.abc", O_RDWR);
//	assert(file_fd > 0);

	mainThreadAIOBaseInit();

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "ThreadPool test on PID [%d]\n", getpid());

//	/* Emit AIO_WRITE */
//	for (i = 0; i < glob_loop_count; i++)
//	{
//		dst_buf				= NULL;
//		ThreadAIOFileWrite(glob_thrdaio_base, &dst_aio_req, file_fd, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 32, 0, mainAIOFinishCB, dst_buf);
//		continue;
//	}

//	ThreadAIOFileReadToMemBuffer(glob_thrdaio_base, &dst_aio_req, -1, "./teste.abc", -1, 0, mainAIOFinishCB_MB, NULL);

	MemBuffer *mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);
	MemBufferAdd(mb, random_data, strlen(random_data));

	ThreadAIOFileWriteFromMemBufferAndDestroy(glob_thrdaio_base, &dst_aio_req, mb, -1, "./data.bin", MemBufferGetSize(mb), 0, NULL, NULL);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int mainThreadAIOBaseInit(void)
{
	ThreadAIOBaseConf thread_aio_conf;
	ThreadPoolBaseConfig *thread_pool_conf;

	/* Clean up stack */
	memset(&thread_aio_conf, 0, sizeof(ThreadAIOBaseConf));

	/* Set thread pool configuration inside AIO_THREAD_CONF */
	thread_pool_conf								= &thread_aio_conf.pool_conf;
	thread_pool_conf->worker_count_start 			= 4;
	thread_pool_conf->worker_count_grow				= 2;
	thread_pool_conf->worker_count_max				= 16;
	thread_pool_conf->worker_stack_sz				= (256 * 1024);
	thread_pool_conf->job_max_count					= 8092;
	thread_pool_conf->log_base						= glob_log_base;
	thread_pool_conf->flags.kevent_finish_notify	= 1;

	/* Create a new THREAD_AIO_BASE */
	glob_thrdaio_base = ThreadAIOBaseNew(glob_ev_base, &thread_aio_conf);
	glob_thrdaio_base->log_base = glob_log_base;

	return 1;
}
/**************************************************************************************************************************/
static void mainAIOFinishCB_MB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	EvAIOReq *aio_req	= aio_req_ptr;
	MemBuffer *read_mb	= (MemBuffer*)aio_req->data.ptr;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - %s OK - [%d]-[%s]\n",
			aio_req->fd, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ_MB) ? "THRD_AIO_READ" : "THRD_AIO_WRITE"), aio_req->data.size, MemBufferDeref(read_mb));

	MemBufferDestroy(read_mb);
	aio_req->data.ptr = NULL;
	return;
}
/**************************************************************************************************************************/
static void mainAIOFinishCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	EvAIOReq dst_aio_req;
	EvAIOReq *aio_req	= aio_req_ptr;
	static int count	= 0;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - %s OK - [%d]-[%s] - COUNT [%d]\n",
			aio_req->fd, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ) ? "THRD_AIO_READ" : "THRD_AIO_WRITE"), aio_req->data.size, aio_req->data.ptr, count);
	count++;

	if (count >= glob_loop_count)
	{
		ThreadAIOFileReadToMemBuffer(glob_thrdaio_base, &dst_aio_req, aio_req->fd, NULL, 32, 0, mainAIOFinishCB_MB, NULL);

	}


	if (cb_data)
		free(cb_data);
	return;
}
/**************************************************************************************************************************/
