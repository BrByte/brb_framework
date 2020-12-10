/*
 * test_comm_client.c
 *
 *  Created on: 2016-05-28
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
#define CLIENT_COUNT 1

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
EvDNSResolverBase *glob_ev_dns;

CommEvTCPClientConf glob_tcpclient_conf;
CommEvTCPClient *glob_tcpclient_arr[CLIENT_COUNT];
char glob_payload[65535];
int glob_state;
int glob_cur_idx;

static EvBaseKQCBH mainMultiConnTimerTick;
static CommEvTCPClientCBH PlainClientEventsConnectEvent;
static CommEvTCPClientCBH PlainClientEventsDataEvent;
static CommEvTCPClientCBH PlainClientEventsCloseEvent;

static int mainCreateDNSBase(void);
/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvKQBaseLogBaseConf log_conf;
	EvKQBaseConf kq_conf;
	int i;

	glob_state		= 0;
	glob_cur_idx	= 0;

	/* Clean STACK */
	memset(&glob_tcpclient_conf, 0, sizeof(CommEvTCPClientConf));
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&kq_conf, 0, sizeof(EvKQBaseConf));
	memset(&glob_payload, 'A', sizeof(glob_payload));

	/* Configure logs */
	log_conf.fileout_pathstr		= "./test_client.log";
	log_conf.flags.double_write		= 1;
	log_conf.flags.mem_keep_logs	= 1;
	log_conf.flags.dump_on_signal	= 1;

	/* Create event base and log base */
	glob_ev_base				= EvKQBaseNew(&kq_conf);
	glob_log_base				= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_ev_base->log_base		= glob_log_base;

	mainCreateDNSBase();

	/* Fill in TCPCLIENT CONF */
	glob_tcpclient_conf.resolv_base			= glob_ev_dns;
	glob_tcpclient_conf.log_base			= glob_log_base;
	glob_tcpclient_conf.cli_proto			= COMM_CLIENTPROTO_SSL;
	glob_tcpclient_conf.hostname			= "smsgateway.me";
	glob_tcpclient_conf.sni_hostname_str	= "smsgateway.me";
	glob_tcpclient_conf.port				= 443;

	/* Alter TCP_CLIENT flags to allow lower layers to destroy it */
//	glob_tcpclient_conf.flags.destroy_after_close	= 1;
//	glob_tcpclient_conf.flags.reconnect_on_close	= 0;
//	glob_tcpclient_conf.flags.reconnect_on_fail		= 0;
//	glob_tcpclient_conf.flags.reconnect_on_timeout	= 0;
//	glob_tcpclient_conf.flags.bindany_active		= 1;

	/* Set timeout information */
	glob_tcpclient_conf.timeout.connect_ms						= 30000;
	glob_tcpclient_conf.retry_times.reconnect_after_timeout_ms	= 5000;
	glob_tcpclient_conf.retry_times.reconnect_after_close_ms	= 10000;
	glob_tcpclient_conf.retry_times.reconnect_on_fail_ms		= 5000;

	/* Set flags */
	glob_tcpclient_conf.flags.reconnect_on_timeout				= 1;
	glob_tcpclient_conf.flags.reconnect_on_fail					= 1;
	glob_tcpclient_conf.flags.reconnect_on_close				= 1;
	glob_tcpclient_conf.flags.reconnect_balance_on_ips			= 1;

	/* Create TCP clients */
	for (i = 0; i < CLIENT_COUNT; i++)
	{
		glob_tcpclient_arr[i]			= CommEvTCPClientNew(glob_ev_base);
		glob_tcpclient_arr[i]->user_int = i;

		CommEvTCPClientEventSet(glob_tcpclient_arr[i], COMM_CLIENT_EVENT_CONNECT, PlainClientEventsConnectEvent, NULL);
		CommEvTCPClientEventSet(glob_tcpclient_arr[i], COMM_CLIENT_EVENT_CLOSE, PlainClientEventsCloseEvent, NULL);
		CommEvTCPClientEventSet(glob_tcpclient_arr[i], COMM_CLIENT_EVENT_READ, PlainClientEventsDataEvent, NULL);
		continue;
	}

	/* Schedule timer for next request */
	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 10, mainMultiConnTimerTick, NULL);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, KQ_BASE_TIMEOUT_AUTO);

	return 1;
}
/**************************************************************************************************************************/
static int mainCreateDNSBase(void)
{
	EvDNSResolverConf dns_conf;

	memset(&dns_conf, 0, sizeof(EvDNSResolverConf));

	/* Fill up DNS configuration */
	dns_conf.dns_ip_str			= "8.8.8.8";
	dns_conf.dns_port			= 53;
	dns_conf.lookup_timeout_ms	= 5000;
	dns_conf.retry_timeout_ms	= 500;
	dns_conf.retry_count		= 10;

	glob_ev_dns					= CommEvDNSResolverBaseNew(glob_ev_base, &dns_conf);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int mainMultiConnTimerTick(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	int op_status;
	unsigned short random			= (arc4random() % 32);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "TIMER_ID [%d] - Random [%d]\n", timer_id, random);

	for (; (glob_cur_idx < CLIENT_COUNT) && (random > 0); glob_cur_idx++, random--)
	{
		/* Create TCP clients */
		if (!glob_tcpclient_arr[glob_cur_idx])
		{
			glob_tcpclient_arr[glob_cur_idx]			= CommEvTCPClientNew(glob_ev_base);
			glob_tcpclient_arr[glob_cur_idx]->user_int = glob_cur_idx;

			CommEvTCPClientEventSet(glob_tcpclient_arr[glob_cur_idx], COMM_CLIENT_EVENT_CONNECT, PlainClientEventsConnectEvent, NULL);
			CommEvTCPClientEventSet(glob_tcpclient_arr[glob_cur_idx], COMM_CLIENT_EVENT_CLOSE, PlainClientEventsCloseEvent, NULL);
			CommEvTCPClientEventSet(glob_tcpclient_arr[glob_cur_idx], COMM_CLIENT_EVENT_READ, PlainClientEventsDataEvent, NULL);
		}
		else if (COMM_CLIENT_STATE_CONNECTED == glob_tcpclient_arr[glob_cur_idx]->socket_state)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Client [%d] - Already connected, skip\n", glob_cur_idx);

			continue;
		}

		/* Connect */
		op_status		= CommEvTCPClientConnect(glob_tcpclient_arr[glob_cur_idx], &glob_tcpclient_conf);
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Firing client [%d] - Random [%u] - Status [%d]\n", glob_cur_idx, random, op_status);
		continue;
	}

	if (glob_cur_idx == CLIENT_COUNT)
		glob_cur_idx = 0;

	/* Reschedule TIMER */
//	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, random, mainMultiConnTimerTick, NULL);

	return 1;
}
/**************************************************************************************************************************/
static void PlainClientEventsConnectEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	int op_status;
	CommEvTCPClient *ev_tcpcli      = base_ptr;
	unsigned short random			= (arc4random() % (sizeof(glob_payload) - 16));

	assert(ev_tcpcli->socket_fd == fd);

	if (COMM_CLIENT_STATE_CONNECTED == ev_tcpcli->socket_state)
	{
		/* Write PAYLOAD */
		op_status = CommEvTCPClientAIOWrite(ev_tcpcli, (char*)&glob_payload, random, NULL, NULL);
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Connect OK IP [%s] - Wrote [%d] bytes - STATUS [%d]\n",
				fd, ev_tcpcli->cfg.hostname, random, op_status);

	}
	else
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Connect FAILED IP [%s]\n", fd, ev_tcpcli->cfg.hostname);
	}

	return;
}
/**************************************************************************************************************************/
static void PlainClientEventsDataEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpcli      = base_ptr;
	short random = arc4random() % 3;

	if (1 == random)
	{
		/* Dettach from array, will create a new one */
		glob_tcpclient_arr[ev_tcpcli->user_int] = NULL;

		/* Issue a disconnect request */
		CommEvTCPClientDisconnectRequest(ev_tcpcli);
	}
	else
		CommEvTCPClientAIOWriteString(ev_tcpcli, "aaaaaaaaaaaaaaa", NULL, NULL);


	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Data event with [%d] bytes- Issue disconnect\n", fd, to_read_sz);

	return;
}
/**************************************************************************************************************************/
static void PlainClientEventsCloseEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpcli      = base_ptr;

	glob_tcpclient_arr[ev_tcpcli->user_int] = NULL;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Close event with [%d] bytes\n", fd, to_read_sz);
	return;
}
/**************************************************************************************************************************/
