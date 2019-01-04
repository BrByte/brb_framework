/*
 * test_comm_pinger.c
 *
 *  Created on: 2017-01-09
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *
 *
 * Copyright (c) 2017 BrByte Software (Oliveira Alves & Amorim LTDA)
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
CommEvTCPServer *glob_tcp_srv;
EvKQBaseLogBase *glob_log_base;
EvICMPBase *glob_icmp_base;
EvDNSResolverBase *glob_ev_dns;

static CommEvICMPBaseCBH MainTestInitTimeoutCB;
static CommEvICMPBaseCBH MainTestInitReplyCB;
static CommEvICMPBaseCBH MainTestInitDNSResolvCB;
static CommEvICMPBaseCBH MainTestInitDNSFailedCB;
static EvBaseKQCBH ICMPStatsTimer;

static int MainCreateLogBase(void);
static int MainCreateResolvBase(void);
static int MainTestInit(char *dst_str);

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	/* Initialize KQEvent */
	glob_ev_base 		= EvKQBaseNew(NULL);
	glob_icmp_base		= CommEvICMPBaseNew(glob_ev_base);
	int timer_id;

	printf("INITIATE...!\n");

	if (argc < 2)
	{
		printf("Usage - %s ip|domain\n", argv[0]);

		exit(0);
	}

	MainCreateLogBase();
	MainCreateResolvBase();
	MainTestInit(argv[1]);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 10);

	printf("Terminating...!\n");

	exit(0);
}
/**************************************************************************************************************************/
static int MainCreateLogBase(void)
{
	EvKQBaseLogBaseConf log_conf;

	/* Clean log configuration */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));

	log_conf.fileout_pathstr		= "./test_code.log";
	log_conf.flags.double_write		= 1;

	glob_log_base					= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
//	glob_log_base->log_level		= LOGTYPE_WARNING;

	return 1;
}
/**************************************************************************************************************************/
static int MainCreateResolvBase(void)
{
	EvDNSResolverConf dns_conf;
	int kq_retcode;

	memset(&dns_conf, 0, sizeof(EvDNSResolverConf));

	/* Fill up DNS configuration */
	dns_conf.dns_ip_str			= "8.8.8.8";
	dns_conf.dns_port			= 53;
	dns_conf.lookup_timeout_ms	= 500;
	dns_conf.retry_timeout_ms	= 50;
	dns_conf.retry_count		= 10;

	glob_ev_dns					= CommEvDNSResolverBaseNew(glob_ev_base, &dns_conf);

	return 1;
}
/**************************************************************************************************************************/
static int MainTestInit(char *dst_str)
{
	EvICMPPeriodicPinger *icmp_pinger;
	EvICMPPeriodicPingerConf pinger_conf;
	int timer_id;

	/* Clean and populate configuration structure for ICMP pinger */
	memset(&pinger_conf, 0, sizeof(EvICMPPeriodicPingerConf));
	pinger_conf.log_base		= glob_log_base;
	pinger_conf.resolv_base		= glob_ev_dns;

	pinger_conf.interval_ms		= 1000;
	pinger_conf.timeout_ms		= 1000;
	pinger_conf.payload_sz		= 128;
	pinger_conf.reset_count		= 8092;
	pinger_conf.hostname_str	= dst_str;

	/* Initialize periodic pinger */
	icmp_pinger					= CommEvICMPPeriodicPingerNew(glob_icmp_base, &pinger_conf);

	/* Schedule timer for next request */
	timer_id 					= EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_PERSIST, 1456, ICMPStatsTimer, icmp_pinger);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "PID [%d] - PING [%s] - TIMER [%d]-[%d]\n",
			getpid(), pinger_conf.hostname_str, timer_id, icmp_pinger->timer_id);

	CommEvICMPPeriodicPingerEventSet(icmp_pinger, ICMP_EVENT_TIMEOUT, MainTestInitTimeoutCB, NULL);
	CommEvICMPPeriodicPingerEventSet(icmp_pinger, ICMP_EVENT_REPLY, MainTestInitReplyCB, NULL);
	CommEvICMPPeriodicPingerEventSet(icmp_pinger, ICMP_EVENT_DNS_RESOLV, MainTestInitDNSResolvCB, NULL);
	CommEvICMPPeriodicPingerEventSet(icmp_pinger, ICMP_EVENT_DNS_FAILED, MainTestInitDNSFailedCB, NULL);

	return 1;
}
/**************************************************************************************************************************/
static void MainTestInitTimeoutCB(void *icmp_pinger_ptr, void *cb_data, void *icmp_data)
{
	EvICMPPeriodicPinger *icmp_pinger 	= icmp_pinger_ptr;
	char *hostname_str 					= cb_data;
	ICMPReply *icmp_reply				= icmp_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "ICMP_EVENT_TIMEOUT - UID [%u] - IP [%s]\n", icmp_pinger->cfg.unique_id, (char *)&icmp_pinger->cfg.ip_addr_str);

	return;
}
/**************************************************************************************************************************/
static void MainTestInitReplyCB(void *icmp_pinger_ptr, void *cb_data, void *icmp_data)
{
	EvICMPPeriodicPinger *icmp_pinger 	= icmp_pinger_ptr;
	void *data 							= cb_data;
	ICMPReply *icmp_reply				= icmp_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "ICMP_EVENT_REPLY - UID [%u] - IP [%s]\n", icmp_pinger->cfg.unique_id, (char *)&icmp_pinger->cfg.ip_addr_str);

	return;
}
/**************************************************************************************************************************/
static void MainTestInitDNSResolvCB(void *icmp_pinger_ptr, void *cb_data, void *icmp_data)
{
	EvICMPPeriodicPinger *icmp_pinger 	= icmp_pinger_ptr;
	void *data 							= cb_data;
	DNSAReply *a_reply 					= icmp_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "ICMP_EVENT_DNS_RESOLV - UID [%u] - IP [%s]\n", icmp_pinger->cfg.unique_id, (char *)&icmp_pinger->cfg.ip_addr_str);

	return;
}
/**************************************************************************************************************************/
static void MainTestInitDNSFailedCB(void *icmp_pinger_ptr, void *cb_data, void *icmp_data)
{
	EvICMPPeriodicPinger *icmp_pinger 	= icmp_pinger_ptr;
	void *data 							= cb_data;
	DNSAReply *a_reply 					= icmp_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "ICMP_EVENT_DNS_FAILED - UID [%u] - IP [%s]\n", icmp_pinger->cfg.unique_id, (char *)&icmp_pinger->cfg.ip_addr_str);

	return;
}
/**************************************************************************************************************************/
static int ICMPStatsTimer(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvICMPPeriodicPinger *icmp_pinger 	= cb_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "SEQ_ID [%d] - HOP_COUNT [%d] - REQ [%ld] - REPLY [%ld] - LOSS [%.2f] - LATENCY [%.2f]\n",
			icmp_pinger->last.seq_req_id,  icmp_pinger->stats.hop_count, icmp_pinger->stats.request_sent, icmp_pinger->stats.reply_recv,
			icmp_pinger->stats.packet_loss_pct, icmp_pinger->stats.latency_ms);

	return 1;
}
/**************************************************************************************************************************/
