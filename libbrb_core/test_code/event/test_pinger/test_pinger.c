/*
 * test_pinger.c
 *
 *  Created on: 2014-03-17
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

#include <libbrb_data.h>
#include <libbrb_ev_kq.h>

EvKQBase *glob_ev_base;
EvICMPBase *glob_icmp_base;
EvKQBaseLogBase *glob_log_base;
EvICMPPeriodicPinger *glob_icmp_pinger;

static EvBaseKQCBH ICMPStatsTimer;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvICMPPeriodicPingerConf pinger_conf;
	EvKQBaseLogBaseConf log_conf;

	/* Clean up stack */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&pinger_conf, 0, sizeof(EvICMPPeriodicPingerConf));
	log_conf.flags.double_write	= 1;

	/* Create event base */
	glob_ev_base	= EvKQBaseNew(NULL);
	glob_icmp_base	= CommEvICMPBaseNew(glob_ev_base);
	glob_log_base	= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);

	//glob_ev_base->log_base		= glob_log_base;
	//glob_icmp_base->log_base	= glob_log_base;
	//pinger_conf.log_base	= glob_log_base;

	if (argc < 2)
	{
		printf("Usage - %s ip [interval timeout payload_sz]\n", argv[0]);
		exit(0);
	}


	pinger_conf.interval_ms = 1000;
	pinger_conf.timeout_ms  = 1000;
	pinger_conf.payload_sz	= 1024;

	pinger_conf.target_ip_str	= argv[1];
	pinger_conf.interval_ms		= argv[2] ? atoi(argv[2]) : 1000;
	pinger_conf.timeout_ms		= argv[3] ? atoi(argv[3]) : 1000;
	pinger_conf.payload_sz		= argv[4] ? atoi(argv[4]) : 128;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Sending ICMP_ECHO to [%s]\n", argv[1]);

	/* Create a new PINGER base */
	glob_icmp_pinger = CommEvICMPPeriodicPingerNew(glob_icmp_base, &pinger_conf);

	/* Schedule timer for next request */
	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_PERSIST, 500, ICMPStatsTimer, glob_icmp_pinger);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	exit(0);
}
/**************************************************************************************************************************/
static int ICMPStatsTimer(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvICMPPeriodicPinger *icmp_pinger = cb_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "SEQ_ID [%d] - HOP_COUNT [%d] - REQ [%ld] - REPLY [%ld] - LOSS [%.2f] - LATENCY [%.2f]\n",
			icmp_pinger->last.seq_req_id,  icmp_pinger->stats.hop_count, icmp_pinger->stats.request_sent, icmp_pinger->stats.reply_recv,
			icmp_pinger->stats.packet_loss_pct, icmp_pinger->stats.latency_ms);



	return 1;
}
/**************************************************************************************************************************/
