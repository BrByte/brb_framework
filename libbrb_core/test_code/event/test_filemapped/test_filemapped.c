/*
 * test_filemapped.c
 *
 *  Created on: 2015-01-24
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
ThreadAIOBase *glob_thrdaio_base;

pthread_mutex_t mutex		= (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
static int glob_loop_count	= 6;
static int glob_req_count	= 0;
static int glob_cur_offset	= 0;

static int mainThreadAIOBaseInit(void);
static int mainTestRead(EvKQFileMapped *file_mapped);
static int mainTestWrite(EvKQFileMapped *file_mapped);

static EvKQFileMappedFinishCB mainFinishReadIO_MB;
static EvKQFileMappedFinishCB mainFinishWriteIO;
static EvKQFileMappedFinishCB mainFinishReadIO;
static EvKQFileMappedFinishCB mainFinishLoad;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	MemBuffer *aux_mb_00;
	MemBuffer *aux_mb_01;
	MemBuffer *aux_mb_02;

	EvKQFileMapped *file_mapped;
	EvKQFileMappedConf map_conf;
	EvKQBaseLogBaseConf log_conf;
	EvKQBaseConf kq_conf;

	if (argc <= 1)
	{
		printf("USAGE - [%s] read|write\n", argv[0]);
		exit(0);
	}
	/* Clean STACK */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&map_conf, 0, sizeof(EvKQFileMappedConf));
	memset(&kq_conf, 0, sizeof(EvKQBaseConf));

	/* Populate LOG configuration */
	log_conf.flags.double_write	= 1;
	log_conf.flags.thread_safe	= 1;
	kq_conf.job.max_slots		= 65535;
	kq_conf.aio.max_slots		= 65535;

	/* Create event base and log base */
	glob_ev_base				= EvKQBaseNew(&kq_conf);
	glob_log_base				= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_log_base->log_level	= LOGTYPE_WARNING;

	glob_ev_base->log_base	= glob_log_base;

	/* Initialize THREAD BASE */
	mainThreadAIOBaseInit();

	/* Populate MAP configuration */
	map_conf.thrdaio_base		= glob_thrdaio_base;
	map_conf.log_base			= glob_log_base;
	map_conf.page_sz			= 64;
	map_conf.path_meta_str		= "./filemap.meta";
	map_conf.path_meta_aux_str	= "./filemap.aux";
	map_conf.path_data_str		= "./filemap.data";
	map_conf.flags.threaded_aio = 1;

	if ('r' == argv[1][0])
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "TEST_FILE_MAPPED on PID [%d] - Reading\n", getpid());
		file_mapped = EvKQFileMappedAIOLoad(glob_ev_base, &map_conf, mainFinishLoad, file_mapped, 30);

		//EvKQFileMappedAIOStateNotifyFinishAdd(file_mapped->load_aiostate, mainFinishLoad, file_mapped, 0, -1);
	}
	else
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "TEST_FILE_MAPPED on PID [%d] - Writing\n", getpid());

		/* Create a new FILEMAPPED */
		file_mapped		= EvKQFileMappedNew(glob_ev_base, &map_conf);
		aux_mb_00		= MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);
		aux_mb_01		= MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);
		aux_mb_02		= MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

		if (!file_mapped)
		{
			KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "Failed mapping file on PID [%d]\n", getpid());
			exit(0);
		}

		MemBufferPrintf(aux_mb_00, "yyyyyyyyyyyyyyyy\n");
		MemBufferPrintf(aux_mb_01, "yyyyyyyyyyyyyyyyyyywwwwwwwwwwwwwwwwwwwww\n");
		MemBufferPrintf(aux_mb_02, "yyyyyyyyyyyyyyyyyyywwwwwwwwwwwwwwwwwwwwwzzzzzzzzzzzzzzz0\n");

		/* Do some write tests to file_mapped */
		mainTestWrite(file_mapped);
		EvKQFileMappedMetaAuxWrite(file_mapped, aux_mb_00);
		EvKQFileMappedMetaAuxWrite(file_mapped, aux_mb_01);
		EvKQFileMappedMetaAuxWrite(file_mapped, aux_mb_02);

		/* Request FILE_CLOSE */
		EvKQFileMappedClose(file_mapped);
	}

	EvKQFileMappedDestroy(file_mapped);

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

	/* Load THREAD_AIO_CONF */
	thread_aio_conf.log_base						= glob_log_base;

	/* Set thread pool configuration inside AIO_THREAD_CONF */
	thread_pool_conf								= &thread_aio_conf.pool_conf;
	thread_pool_conf->worker_count_start 			= 4;
	thread_pool_conf->worker_count_grow				= 4;
	thread_pool_conf->worker_count_max				= 4;
	thread_pool_conf->worker_stack_sz				= 262144;
	thread_pool_conf->job_max_count					= 8092;
	thread_pool_conf->flags.kevent_finish_notify	= 1;

	/* Add VERBOSITY to THREAD_POOL */
	//thread_pool_conf->log_base						= glob_log_base;

	/* Create a new THREAD_AIO_BASE */
	glob_thrdaio_base = ThreadAIOBaseNew(glob_ev_base, &thread_aio_conf);

	return 1;
}
/**************************************************************************************************************************/
static int mainTestRead(EvKQFileMapped *file_mapped)
{
	int read_id;
	char *dst_data;

	/* Issue AIO_READ */
	glob_req_count++;
	dst_data = calloc(1, 1024);
	read_id = EvKQFileMappedAIORead(file_mapped, dst_data, 0, 32, mainFinishReadIO, file_mapped, 15);

	glob_req_count++;
	dst_data = calloc(1, 1024);
	read_id = EvKQFileMappedAIORead(file_mapped, dst_data, 32, 32, mainFinishReadIO, file_mapped, 15);

	glob_req_count++;
	dst_data = calloc(1, 1024);
	read_id = EvKQFileMappedAIORead(file_mapped, dst_data, 64, 32, mainFinishReadIO, file_mapped, 15);

	return 1;
}
/**************************************************************************************************************************/
static int mainTestWrite(EvKQFileMapped *file_mapped)
{
	MemBuffer *mb;
	char *data_a = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	char *data_b = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	char *data_c = "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";

	int data_a_sz = strlen(data_a);
	int data_b_sz = strlen(data_b);
	int data_c_sz = strlen(data_c);

	int id_a;
	int id_b;
	int id_c;
	int i;

	//	/* Issue multiple AIO writes */
	//	for (i = 0; i < glob_loop_count; i++)
	//	{
	//		id_a = EvKQFileMappedAIOWrite(file_mapped, data_a, glob_cur_offset, data_a_sz, mainFinishWriteIO, file_mapped, 15);
	//
	//		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "WRITE STATE [%d]\n", id_a);
	//		glob_cur_offset += data_a_sz;
	//		glob_req_count++;
	//		continue;
	//	}
	//
	//	/* Issue multiple AIO writes */
	//	for (i = 0; i < glob_loop_count; i++)
	//	{
	//		id_b = EvKQFileMappedAIOWrite(file_mapped, data_b, glob_cur_offset, data_b_sz, mainFinishWriteIO, file_mapped, 10);
	//		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "WRITE STATE [%d]\n", id_b);
	//		glob_cur_offset += data_b_sz;
	//		glob_req_count++;
	//		continue;
	//	}
	//
	//	/* Issue multiple AIO writes */
	//	for (i = 0; i < glob_loop_count; i++)
	//	{
	//		id_c = EvKQFileMappedAIOWrite(file_mapped, data_c, glob_cur_offset, data_c_sz, mainFinishWriteIO, file_mapped, 15);
	//		KQBASE_LOG_PRINTF(file_mapped->log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "WRITE STATE [%d]\n", id_c);
	//		glob_cur_offset += data_c_sz;
	//		glob_req_count++;
	//		continue;
	//	}

	for (i = 0; i < glob_loop_count; i++)
	{
		mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);
		MemBufferAdd(mb, data_a, data_a_sz);

		EvKQFileMappedAIOWriteMemBufferAndDestroy(file_mapped, mb, glob_cur_offset, mainFinishWriteIO, file_mapped, 15);
		glob_cur_offset += data_a_sz;
		glob_req_count++;
		continue;
	}

	mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);
	MemBufferAdd(mb, "FINISH#", strlen("FINISH#"));

	EvKQFileMappedAIOWriteMemBufferAndDestroy(file_mapped, mb, glob_cur_offset, mainFinishWriteIO, file_mapped, strlen("FINISH#"));
	glob_cur_offset += strlen("FINISH#");
	glob_req_count++;


	//EvKQFileMappedAIOStateCancelByAIOReqID(file_mapped, id_a);
	//EvKQFileMappedAIOStateCancelByAIOReqID(file_mapped, id_b);
	//EvKQFileMappedAIOStateCancelByAIOReqID(file_mapped, id_c);
	//EvKQFileMappedAIOStateCancelAllByOwner(file_mapped, 15);

	return 1;
}
/**************************************************************************************************************************/
static int mainFinishReadIO_MB(void *filemap_ptr, void *aio_state_ptr, void *cb_data, long io_sz)
{
	EvKQFileMapped *file_mapped			= filemap_ptr;
	EvKQFileMappedAIOState *aio_state	= aio_state_ptr;

	glob_req_count--;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "Finish CB with [%ld] bytes - ID [%d] - AIO_STATE ERR [%d] - PENDING_REQ [%d] - AIO_LIST [%ld] - DATA -> [%s]\n",
			io_sz, aio_state->pending_req[0].id, aio_state->flags.error, glob_req_count, file_mapped->data.aio_state_list.size, MemBufferDeref(aio_state->read.mb));

	MemBufferDestroy(aio_state->read.mb);
	return 1;
}
/**************************************************************************************************************************/
static int mainFinishReadIO(void *filemap_ptr, void *aio_state_ptr, void *cb_data, long io_sz)
{
	EvKQFileMapped *file_mapped			= filemap_ptr;
	EvKQFileMappedAIOState *aio_state	= aio_state_ptr;

	glob_req_count--;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "Finish CB with [%ld] bytes - ID [%d] - AIO_STATE ERR [%d] - PENDING_REQ [%d] - AIO_LIST [%ld] - DATA -> [%s]\n",
			io_sz, aio_state->pending_req[0].id, aio_state->flags.error, glob_req_count, file_mapped->data.aio_state_list.size, aio_state->read.data_ptr);

	free(aio_state->read.data_ptr);
	return 1;
}
/**************************************************************************************************************************/
static int mainFinishWriteIO(void *filemap_ptr, void *aio_state_ptr, void *cb_data, long io_sz)
{
	EvKQFileMapped *file_mapped			= filemap_ptr;
	EvKQFileMappedAIOState *aio_state	= aio_state_ptr;
	int complete_status;

	glob_req_count--;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "Finish CB with [%ld] bytes - ID [%d] - AIO_STATE ERR [%d] - PENDING_REQ [%d] - AIO_LIST [%ld]\n", io_sz,
			aio_state->pending_req[0].id, aio_state->flags.error, glob_req_count, file_mapped->data.aio_state_list.size);

	/* FINISHED */
	if (0 == glob_req_count)
	{
		/* Dump any leftover of last incomplete block, and invoke COMPLETE */
		EvKQFileMappedCheckLastBlock(file_mapped, glob_cur_offset);
		complete_status = EvKQFileMappedCompleted(file_mapped);

		KQBASE_LOG_PRINTF(glob_log_base, (complete_status ? LOGTYPE_DEBUG : LOGTYPE_CRITICAL), (complete_status ? LOGCOLOR_GREEN : LOGCOLOR_RED),
				"COMPLETE_STATUS [%d] - Finished writing [%d] bytes of TOTAL [%d] bytes\n", complete_status, io_sz, glob_cur_offset);

		/* Do some read tests to file_mapped */
		mainTestRead(file_mapped);
	}

	return 1;
}
/**************************************************************************************************************************/
static int mainFinishLoad(void *filemap_ptr, void *aio_state_ptr, void *cb_data, long io_sz)
{
	EvKQFileMapped *file_mapped			= filemap_ptr;
	EvKQFileMappedAIOState *aio_state	= aio_state_ptr;
	int read_id;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW,
			"Finish LOADING with [%ld] bytes - ID [%d] - ERR [%d] - CUR_OFF [%ld] - COMPLETE [%d] - PEND_META [%d] - AUX_META [%s]\n", io_sz,
				aio_state->pending_req[0].id, aio_state->flags.error, file_mapped->data.cur_offset, file_mapped->flags.complete,
				file_mapped->stats.pending_meta_read, MemBufferDeref(file_mapped->meta.data_aux_mb));

	read_id = EvKQFileMappedAIOReadToMemBuffer(file_mapped, 0, 32, mainFinishReadIO_MB, file_mapped, 15);

	return 1;
}
/**************************************************************************************************************************/
