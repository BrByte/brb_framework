/*
 * test_tcp_voter.c
 *
 *  Created on: 2014-11-27
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE./
 */

#include <libbrb_core.h>

static void TCPClientForgeSourceIP(struct sockaddr_in *src_addr);

static CommEvTCPClientCBH TCPClientEventRead;
static CommEvTCPClientCBH TCPClientEventClose;
static CommEvTCPClientCBH TCPClientEventConnect;
static EvBaseKQCBH TCPClientSendCMDTimer;
static void TCPClientInitResolvBase(void);
static int TCPClientVoteSend(CommEvTCPClient *ev_tcpclient);

EvKQBase *glob_ev_base;
EvDNSResolverBase *glob_ev_dns;
EvKQBaseLogBase *glob_log_base;
int glob_vote_timerid;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	CommEvTCPClient *ev_tcpclient;
	CommEvTCPClientConf ev_tcpclient_conf;
	EvKQBaseLogBaseConf log_conf;
	struct sockaddr_in src_addr;
	int op_status;
	int use_ssl;

	/* Clean up stack */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&src_addr, 0, sizeof(struct sockaddr_in));
	log_conf.flags.double_write				= 1;

	/* Create event base */
	glob_ev_base	= EvKQBaseNew(NULL);
	glob_log_base	= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_ev_base->log_base = glob_log_base;


	char buf[128];
	int test = snprintf((char*)&buf, sizeof(buf), "%s-%s#", "1234567890", "1234567890");

	buf[test] = '\0';

	printf("test [%d] - [%s]\n", test, buf);
	return 0;


//	/* Initialize common bases */
//	TCPClientInitResolvBase();
//	TCPClientForgeSourceIP(&src_addr);
//
//	/* Clean configuration structure for TCP client */
//	memset(&ev_tcpclient_conf, 0, sizeof(CommEvTCPClientConf));
//	memcpy(&ev_tcpclient_conf.src_addr, (struct sockaddr *)&src_addr, sizeof(struct sockaddr_in));
//
//	ev_tcpclient_conf.resolv_base	= glob_ev_dns;
//	ev_tcpclient_conf.hostname		= "servidor.com.br";
//	ev_tcpclient_conf.port			= 80;
//
//	/* Set flags */
//	ev_tcpclient_conf.flags.reconnect_on_timeout				= 1;
//	ev_tcpclient_conf.flags.reconnect_on_close					= 1;
//	ev_tcpclient_conf.flags.reconnect_on_fail					= 1;
//	ev_tcpclient_conf.flags.reconnect_new_dnslookup				= 1;
//	ev_tcpclient_conf.flags.reconnect_balance_on_ips			= 1;
//	ev_tcpclient_conf.flags.bindany_active						= 1;
//	ev_tcpclient_conf.retry_times.reconnect_after_timeout_ms	= 500;
//	ev_tcpclient_conf.retry_times.reconnect_after_close_ms		= 500;
//	ev_tcpclient_conf.retry_times.reconnect_on_fail_ms			= 500;
//
//
//	/* Set timeout information */
//	ev_tcpclient_conf.timeout.connect_ms						= 5 * 1000;
//
//	/* Create a new TCP client */
//	ev_tcpclient = CommEvTCPClientNew(glob_ev_base);
//
//	/* Set callback functions and connect */
//	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_CONNECT, TCPClientEventConnect, ev_tcpclient);
//	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_CLOSE, TCPClientEventClose, ev_tcpclient);
//	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_READ, TCPClientEventRead, ev_tcpclient);
//	op_status = CommEvTCPClientConnect(ev_tcpclient, &ev_tcpclient_conf);
//
//	/* Failed connecting, STOP */
//	if (!op_status)
//	{
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "Connection to [%s] on port [%d] failed\n", ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);
//		exit(0);
//	}
//
//	/* Schedule send request timer */
//	glob_vote_timerid = EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2000, TCPClientSendCMDTimer, ev_tcpclient);
//
	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "TCP_VOTER begin on PID [%d]\n", getpid());

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	exit(0);
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void TCPClientForgeSourceIP(struct sockaddr_in *src_addr)
{
	return;

	/* Fill in the stub sockaddr_in structure */
	src_addr->sin_family		= AF_INET;
	src_addr->sin_addr.s_addr	= inet_addr("192.168.140.180");
	src_addr->sin_port			= htons(80);

	return;
}
/**************************************************************************************************************************/
static void TCPClientInitResolvBase(void)
{
	EvDNSResolverConf dns_conf;

	memset(&dns_conf, 0, sizeof(EvDNSResolverConf));

	/* Fill up DNS configuration */
	dns_conf.dns_ip_str			= "8.8.8.8";
	dns_conf.dns_port			= 53;
	dns_conf.lookup_timeout_ms	= 500;
	dns_conf.retry_timeout_ms	= 50;
	dns_conf.retry_count		= 10;

	glob_ev_dns	= CommEvDNSResolverBaseNew(glob_ev_base, &dns_conf);

	return;
}
/**************************************************************************************************************************/
static void TCPClientEventRead(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *tcp_client = base_ptr;
	int valid_data				= 0;
	char *hash_str				= NULL;

	//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - DATA [%d]\n", fd, MemBufferGetSize(tcp_client->read_buffer));
	MemBufferClean(tcp_client->iodata.read_buffer);

	return;
}
/**************************************************************************************************************************/
static void TCPClientEventClose(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *tcp_client = base_ptr;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Transport closed, will reconnect\n", fd);

	return;
}
/**************************************************************************************************************************/
static void TCPClientEventConnect(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient	= base_ptr;
	MemBuffer *req_mb				= MemBufferNew(8092, BRBDATA_THREAD_UNSAFE);

	if (COMM_CLIENT_STATE_CONNECTED == ev_tcpclient->socket_state)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Connected to [%s] on port [%d], transport ONLINE\n", ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);
	}
	else
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] FAILED connecting to [%s] - STATE [%d]\n", fd, ev_tcpclient->cfg.hostname, ev_tcpclient->socket_state);
	}


	return;
}
/**************************************************************************************************************************/
static int TCPClientSendCMDTimer(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	MemBuffer *req_mb;
	CommEvTCPClient *ev_tcpclient = cb_data;
	unsigned short random_interval;

	/* Loop until a correct value is OK */
	while (1)
	{
		random_interval = (arc4random() % 10);

		if (random_interval > 5)
			break;

		continue;
	}

	if (COMM_CLIENT_STATE_CONNECTED == ev_tcpclient->socket_state)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Transport connected to [%s] on port [%d], voting NOW - New vote will be in [%u] seconds\n",
				ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, random_interval);

		/* Schedule send command timer */
		glob_vote_timerid = EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, (random_interval * 1000), TCPClientSendCMDTimer, ev_tcpclient);
		TCPClientVoteSend(ev_tcpclient);

	}
	else
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "Transport not connected to [%s] on port [%d] - Try again in 5 seconds\n",
						ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, random_interval);

		/* Schedule send command timer */
		glob_vote_timerid = EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, (5 * 1000), TCPClientSendCMDTimer, ev_tcpclient);
	}

	return 1;
}
/**************************************************************************************************************************/
static int TCPClientVoteSend(CommEvTCPClient *ev_tcpclient)
{
	MemBuffer *req_mb;
	unsigned short a, b, c, d;
	unsigned short candidate = arc4random() % 100;

	/* Select between candidates */
	candidate = 1; //(candidate > 4 ? 2 : 3);

	/* Generate a real looking IP */
	while (1)
	{
		 a = arc4random() % 210;
		 b = arc4random() % 250;
		 c = arc4random() % 250;
		 d = arc4random() % 250;

		 if (a < 50 || b < 50)
			 continue;

		 break;
	}


	req_mb = MemBufferNew(8092, BRBDATA_THREAD_UNSAFE);

	/* Create HTTP header */
	MemBufferPrintf(req_mb, "POST /wp-admin/admin-ajax.php HTTP 1.1\r\n");
	MemBufferPrintf(req_mb, "Host: xxxxxxxxxxxxxxxxxxxxxx.com.br\r\n");
	MemBufferPrintf(req_mb, "Connection: keep-alive\r\n");
	MemBufferPrintf(req_mb, "Content-Length: 69\r\n");
	MemBufferPrintf(req_mb, "Accept: */*\r\n");
	MemBufferPrintf(req_mb, "Origin: http://xxxxxxxxxxxxxxxxxxxxxx.com.br\r\n");
	MemBufferPrintf(req_mb, "X-Requested-With: XMLHttpRequest\r\n");
	MemBufferPrintf(req_mb, "X-Forwarded-For: %u.%u.%u.%u\r\n", a, b, c, d);
	MemBufferPrintf(req_mb, "User-Agent: Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/39.0.2171.71 Safari/537.36\r\n");
	MemBufferPrintf(req_mb, "Content-Type: application/x-www-form-urlencoded; charset=UTF-8\r\n");
	MemBufferPrintf(req_mb, "Referer: http://xxxxxxxxxxxxxxxxxxxxxx.com.br/yyyyyyyyyyyyyyyyyyy-2/\r\n");
	MemBufferPrintf(req_mb, "Accept-Encoding: gzip, deflate\r\n");
	MemBufferPrintf(req_mb, "Accept-Language: pt-BR,pt;q=0.8,en-US;q=0.6,en;q=0.4\r\n");

	/* Close HTTP headers */
	MemBufferPrintf(req_mb, "\r\n");
	MemBufferPrintf(req_mb, "action=polls&view=process&poll_id=2&poll_2=%02d&poll_2_nonce=xxxxxxxxxxx", candidate);

	/* Dispatch request */
	CommEvTCPClientAIOWriteAndDestroyMemBuffer(ev_tcpclient, req_mb, NULL, NULL);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Voted for [%s] using IP [%u.%u.%u.%u]\n",
			ev_tcpclient->socket_fd,  candidate == 7 ? "BRENDA" : "GABI", a, b, c, d);

	return 1;
}
/**************************************************************************************************************************/
