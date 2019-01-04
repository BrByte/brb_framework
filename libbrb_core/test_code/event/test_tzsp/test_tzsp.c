/*
 * test_tzsp_server.c
 *
 *  Created on: 2015-05-16
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

static int mainTZSPServerInit(void);

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
CommEvTZSPServer *glob_tzsp_server;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvKQBaseLogBaseConf log_conf;
	int op_status;

	/* Clean stack area */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	log_conf.flags.double_write	= 1;

	/* Create event base */
	glob_ev_base		= EvKQBaseNew(NULL);
	glob_log_base		= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "TEST_TZSP_SERVER initialized on PID [%d]\n", getpid());

	/* Fire UP TZSP server side */
	mainTZSPServerInit();

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);
	exit(0);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int mainTZSPServerInit(void)
{
	CommEvTZSPServerConf ev_tzsp_server_conf;
	int op_status;

	/* Clean up stack */
	memset(&ev_tzsp_server_conf, 0, sizeof(CommEvTZSPServerConf));

	/* Create TZSP server and attach LOG_BASE */
	glob_tzsp_server			= CommEvTZSPServerNew(glob_ev_base);
	glob_tzsp_server->log_base	= glob_log_base;

	/* Fire TZSP server up */
	op_status = CommEvTZSPServerInit(glob_tzsp_server, &ev_tzsp_server_conf);

	return op_status;
}
/**************************************************************************************************************************/

