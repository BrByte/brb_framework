/*
 * test_aiofile.c
 *
 *  Created on: 2014-11-02
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

static EvAIOReqCBH mainAIOFinishCB;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
//	char buf[64];
//
//	printf("HUM: %s\n", CommEvStatisticsRateHumanize(120938210398, (char*)&buf, sizeof(buf)));
//	return 1;


	EvKQBaseLogBaseConf log_conf;
	EvAIOReq dst_aio_req;
	int op_status;
	int file_fd;
	char *dst_buf;
	int i;

	chdir("/brb_main/coredumps");

	/* Clean stack space */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	log_conf.fileout_pathstr				= "./test_aiofile.log";
	log_conf.flags.double_write				= 1;

	/* Create event base */
	glob_ev_base		= EvKQBaseNew(NULL);
	glob_log_base		= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	file_fd				= open("./teste.abc", O_RDWR);
	//glob_ev_base->log_base = glob_log_base;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "AIOFILE_TEST on PID [%d] - FILE ON FD [%d]\n", getpid(), file_fd);

//	/* Emit AIO_WRITE */
//	for (i = 0; i < 16; i++)
//	{
//		dst_buf				= NULL;
//		EvKQBaseAIOFileWrite(glob_ev_base, &dst_aio_req, file_fd, "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", 32, 512, mainAIOFinishCB, dst_buf);
//		continue;
//	}
//
//	/* Emit AIO_READ */
//	for (i = 0; i < 16; i++)
//	{
//		dst_buf				= calloc(1, 1024);
//		EvKQBaseAIOFileRead(glob_ev_base, &dst_aio_req, file_fd, dst_buf, 32, 0, mainAIOFinishCB, dst_buf);
//		continue;
//	}

	for (i = 0; i < 16; i++)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "asdlkjasdlkjsad asdkljasldkjasdlkjsad [%d] \n", 0);
		continue;
	}

	//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "asdlkjasdlkjsad asdkljasldkjasdlkjsad [%d] \n", i);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 5);

	exit(0);

	return 1;
}
/**************************************************************************************************************************/
static void mainAIOFinishCB(int fd, int size, int thrd_id, void *cb_data, void *aio_req_ptr)
{
	EvAIOReq *aio_req = aio_req_ptr;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - %s OK - [%d]-[%s]\n",
			aio_req->fd, ((aio_req->aio_opcode == AIOREQ_OPCODE_READ) ? "AIO_READ" : "AIO_WRITE"), aio_req->data.size, aio_req->data.ptr);

	if (cb_data)
		free(cb_data);

	return;
}
/**************************************************************************************************************************/


