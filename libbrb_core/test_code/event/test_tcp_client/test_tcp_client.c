/*
 * test_tcp_client.c
 *
 *  Created on: 2014-01-28
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

static CommEvTCPClientCBH TCPClientEventRead;
static CommEvTCPClientCBH TCPClientEventClose;
static CommEvTCPClientCBH TCPClientEventConnect;

static CommEvTCPClientCBH TCPClientEventWriteFinish;

static EvBaseKQCBH TCPClientSendCMDTimer;
static void TCPClientInitResolvBase(void);

static int TcpClientTest_CreateLogBase(void);

EvKQBase *glob_ev_base;
EvDNSResolverBase *glob_ev_dns;
EvKQBaseLogBase *glob_log_base;

/**************************************************************************************************************************/
static int TcpClientTest_CreateLogBase(void)
{
	EvKQBaseLogBaseConf log_conf;

	/* Clean stack space */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
//	log_conf.fileout_pathstr		= VOIPR_SMS_LOG_PATH;
	log_conf.flags.double_write		= 1;

	/* Initialize log base */
	glob_log_base					= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
//	glob_log_base					= NULL;

	return 1;
}
/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	CommEvTCPClient *ev_tcpclient;
	CommEvTCPClientConf ev_tcpclient_conf;
	struct sockaddr_in src_addr;
	int op_status;
	int use_ssl;

	memset(&src_addr, 0, sizeof(struct sockaddr_in));

//	/* Fill in the stub sockaddr_in structure */
//	src_addr.sin_family			= AF_INET;
//	src_addr.sin_addr.s_addr	= inet_addr("192.168.140.180");
//	//src_addr.sin_port			= htons(port);

	/* Create event base */
	glob_ev_base 				= EvKQBaseNew(NULL);


	TcpClientTest_CreateLogBase();
	TCPClientInitResolvBase();

	if (argc < 4)
	{
		printf("Usage - %s (ssl|plain) hostname port\n", argv[0]);
		exit(0);
	}

	/* Clean configuration structure for TCP client */
	memset(&ev_tcpclient_conf, 0, sizeof(CommEvTCPClientConf));

	/* Grab protocol */
	if (!strncmp(argv[1], "ssl", 3))
	{
		printf("USING SSL\n");

		ev_tcpclient_conf.cli_proto = COMM_CLIENTPROTO_SSL;
	}

//	ev_tcpclient_conf.log_base		= glob_log_base;
	ev_tcpclient_conf.resolv_base	= glob_ev_dns;
	ev_tcpclient_conf.hostname		= argv[2];
	ev_tcpclient_conf.port			= atoi(argv[3]);

	memcpy(&ev_tcpclient_conf.src_addr, (struct sockaddr *)&src_addr, sizeof(struct sockaddr_in));

	//ev_tcpclient_conf.src_addr		= (struct sockaddr *)&src_addr;

	ev_tcpclient_conf.flags.bindany_active = 1;

	/* Set NULL CYPHER */
	ev_tcpclient_conf.flags.ssl_null_cypher = 0;

	/* Set flags */
	ev_tcpclient_conf.flags.reconnect_on_timeout 				= 1;
	ev_tcpclient_conf.flags.reconnect_on_close	 				= 1;

	ev_tcpclient_conf.flags.reconnect_new_dnslookup				= 0;
	ev_tcpclient_conf.flags.reconnect_balance_on_ips			= 1;

	ev_tcpclient_conf.retry_times.reconnect_after_timeout_ms	= 1000;
	ev_tcpclient_conf.retry_times.reconnect_after_close_ms		= 1000;

	/* Set timeout information */
	ev_tcpclient_conf.timeout.connect_ms = 100000;

	ev_tcpclient = CommEvTCPClientNew(glob_ev_base);

	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_CONNECT, TCPClientEventConnect, NULL);
	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_CLOSE, TCPClientEventClose, NULL);
	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_READ, TCPClientEventRead, NULL);

	op_status = CommEvTCPClientConnect(ev_tcpclient, &ev_tcpclient_conf);

	if (!op_status)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "Connection to [%s] on port [%d] failed\n", ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);

		exit(0);
	}

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	exit(0);
}
/**************************************************************************************************************************/
/**/
/**/
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
//	int valid_data				= 0;
//	char *hash_str				= NULL;

	/* Validate RC4_MD5 header */
//	valid_data	= EvAIOReqTransform_RC4_MD5_DataValidate(tcp_client->read_buffer);
//	hash_str	= EvAIOReqTransform_RC4_MD5_DataHashString(tcp_client->read_buffer);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] - DATA [%d]-[#%s#]\n",
			fd, MemBufferGetSize(tcp_client->iodata.read_buffer), MemBufferDeref(tcp_client->iodata.read_buffer));

	MemBufferClean(tcp_client->iodata.read_buffer);

	return;
}
/**************************************************************************************************************************/
static void TCPClientEventClose(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *tcp_client = base_ptr;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] - DATA [%d]-[%s]\n",
			fd, MemBufferGetSize(tcp_client->iodata.read_buffer), MemBufferDeref(tcp_client->iodata.read_buffer));
	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] - CLOSED\n", fd);

	return;
}
/**************************************************************************************************************************/
static void TCPClientEventConnect(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	MemBuffer *req_mb;
	CommEvTCPClient *ev_tcpclient = base_ptr;

	if (COMM_CLIENT_STATE_CONNECTED == ev_tcpclient->socket_state)
	{
		/* Load CRYPTO_RC4 TRANSFORM */
//		EvAIOReqTransform_CryptoEnable(&ev_tcpclient->transform, COMM_CRYPTO_FUNC_RC4_MD5, "cryptokey", strlen("cryptokey"));

		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "Connected to [%s] on port [%d], sending request\n",
				ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);

		req_mb 			= MemBufferNew(8092, BRBDATA_THREAD_UNSAFE);
//		MemBufferPrintf(req_mb, "ENCRYPTED HELLO FROM CLIENT\n");

		MemBufferPrintf(req_mb, "-v begin\r\n");
		MemBufferPrintf(req_mb, "177.52.73.1\r\n");
		MemBufferPrintf(req_mb, "157.240.222.35\r\n");
		MemBufferPrintf(req_mb, "52.20.168.249\r\n");
		MemBufferPrintf(req_mb, "1.1.1.1\r\n");
		MemBufferPrintf(req_mb, "end\r\n");
//		//printf("TCPClientEventConnect - FD [%d] connected, sending req\n", fd);
		CommEvTCPClientAIOWriteAndDestroyMemBuffer(ev_tcpclient, req_mb, NULL, NULL);

//		CommEvTCPClientAIOWriteString(ev_tcpclient, "UBCT1 stats_noreset \n", TCPClientEventWriteFinish, NULL);

//		CommEvTCPClientAIOWriteString(ev_tcpclient, "stats", NULL, NULL);

		/* Schedule send command timer */
//		EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2000, TCPClientSendCMDTimer, ev_tcpclient);
	}
	else
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] FAILED connecting to [%s] - STATE [%d]\n",
				fd, ev_tcpclient->cfg.hostname, ev_tcpclient->socket_state);
	}


	return;
}
/**************************************************************************************************************************/
static int TCPClientSendCMDTimer(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	MemBuffer *req_mb;
	CommEvTCPClient *ev_tcpclient = cb_data;

	if (COMM_CLIENT_STATE_CONNECTED == ev_tcpclient->socket_state)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, " FD [%d] - Sending request\n", ev_tcpclient->socket_fd);

		req_mb = MemBufferNew(8092, BRBDATA_THREAD_UNSAFE);

		MemBufferPrintf(req_mb, "ENCRYPTED REQUEST ABCDEFGH IJKLMNOPQ 99\n");

		CommEvTCPClientAIOWriteAndDestroyMemBuffer(ev_tcpclient, req_mb, NULL, NULL);

		/* Schedule send command timer */
		EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2000, TCPClientSendCMDTimer, ev_tcpclient);
	}

	return 1;
}
/**************************************************************************************************************************/
static void TCPClientEventWriteFinish(int fd, int write_bytes, int thrd_id, void *cb_data, void *base_ptr)
{

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "FD [%d] - WRITE [%d]\n",
			fd, write_bytes);


	return;
}
/**************************************************************************************************************************/

