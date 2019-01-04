/*
 * test_icmp.c
 *
 *  Created on: 2016-04-12
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

static EvBaseKQCBH CommEvRAWEventRead;
static EvBaseKQCBH CommEvRAWEventWrite;

EvKQBase *glob_ev_base;
EvICMPBase *glob_icmp_base;
EvKQBaseLogBase *glob_log_base;
int glob_rawfd;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvICMPPeriodicPingerConf pinger_conf;
	EvKQBaseLogBaseConf log_conf;

	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	log_conf.flags.double_write				= 1;

	/* Create event base */
	glob_ev_base				= EvKQBaseNew(NULL);
	glob_icmp_base				= CommEvICMPBaseNew(glob_ev_base);
	glob_log_base				= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_icmp_base->log_base 	= glob_log_base;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "TEST_ICMP begin on PID [%d]\n", getpid());

	/* Create a new RAW socket and set it to non_blocking */
	glob_rawfd 					= EvKQBaseSocketRAWNew(glob_ev_base, AF_INET, IPPROTO_ICMP);
	EvKQBaseSetEvent(glob_ev_base, glob_rawfd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvRAWEventRead, NULL);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvRAWEventRead(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base = base_ptr;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "READ_EV [%d]\n", to_read_sz);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvRAWEventWrite(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "WRITE_EV [%d]\n", to_read_sz);
	return 1;
}
/**************************************************************************************************************************/
