/*
 * test_comm_server.c
 *
 *  Created on: 2016-05-28
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
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

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;
CommEvTCPServer *glob_tcp_srv;

static int mainInitPlainServer(int port);
static int mainInitSSLServer(int port);
static int mainInitAutoDetectServer(int port);

static CommEvTCPServerCBH PlainServerEventsAcceptEvent;
static CommEvTCPServerCBH PlainServerEventsDataEvent;
static CommEvTCPServerCBH PlainServerEventsCloseEvent;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	EvKQBaseLogBaseConf log_conf;
	EvKQBaseConf kq_conf;

	/* Clean STACK */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&kq_conf, 0, sizeof(EvKQBaseConf));

	/* Configure logs */
	log_conf.fileout_pathstr		= "./test_server.log";
	log_conf.flags.double_write		= 1;
	log_conf.flags.mem_keep_logs	= 1;
	log_conf.flags.dump_on_signal	= 1;

	/* Create event base and log base */
	glob_ev_base				= EvKQBaseNew(&kq_conf);
	glob_log_base				= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_tcp_srv				= CommEvTCPServerNew(glob_ev_base);

	//glob_ev_base->log_base		= glob_log_base;

	/* Initialize PLAIN TCP SERVER */
	mainInitPlainServer(999);
	mainInitSSLServer(1000);
	mainInitAutoDetectServer(1001);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, KQ_BASE_TIMEOUT_AUTO);

	/* We are leaving!! */
	EvKQBaseDestroy(glob_ev_base);
	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int mainInitPlainServer(int port)
{
	CommEvTCPServerConf conf_plain;
	int plain_lid;

	/* Clean up stack */
	memset(&conf_plain, 0, sizeof(CommEvTCPServerConf));

	/* Fill in control configuration */
	conf_plain.bind_method			= COMM_SERVER_BINDANY;
	conf_plain.read_mthd			= COMM_SERVER_READ_MEMBUFFER;
	conf_plain.srv_proto			= COMM_SERVERPROTO_PLAIN;
	conf_plain.port					= port;
	conf_plain.flags.reuse_addr		= 1;
	conf_plain.flags.reuse_port		= 1;

	/* Fire up listener ID on TCP server */
	plain_lid	= CommEvTCPServerListenerAdd(glob_tcp_srv, &conf_plain);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "LID [%d] - Plain TCP SERVER on port [%d]\n", plain_lid, port);

	/* Set default accept and data events */
	CommEvTCPServerEventSet(glob_tcp_srv, plain_lid, COMM_SERVER_EVENT_ACCEPT_AFTER, PlainServerEventsAcceptEvent, NULL);
	CommEvTCPServerEventSet(glob_tcp_srv, plain_lid, COMM_SERVER_EVENT_DEFAULT_READ, PlainServerEventsDataEvent, NULL);
	CommEvTCPServerEventSet(glob_tcp_srv, plain_lid, COMM_SERVER_EVENT_DEFAULT_CLOSE, PlainServerEventsCloseEvent, NULL);


	return plain_lid;
}
/**************************************************************************************************************************/
static int mainInitSSLServer(int port)
{

	return 1;
}
/**************************************************************************************************************************/
static int mainInitAutoDetectServer(int port)
{

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void PlainServerEventsAcceptEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *ev_tcpsrv      = base_ptr;
	CommEvTCPServerConn *conn_hnd	= CommEvTCPServerConnArenaGrab(ev_tcpsrv, fd);
	short random = arc4random() % 3;

	if (1 == random)
		CommEvTCPServerConnClose(conn_hnd);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Accept client from IP [%s]\n", fd, conn_hnd->string_ip);
	return;
}
/**************************************************************************************************************************/
static void PlainServerEventsDataEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *ev_tcpsrv      = base_ptr;
	CommEvTCPServerConn *conn_hnd	= CommEvTCPServerConnArenaGrab(ev_tcpsrv, fd);
	short random = arc4random() % 3;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Data event with [%d] bytes\n", fd, to_read_sz);
	CommEvTCPServerConnAIOWriteString(conn_hnd, "aaaaaaaaaaaaaaa", NULL, NULL);

	if (1 == random)
		CommEvTCPServerConnClose(conn_hnd);

	return;
}
/**************************************************************************************************************************/
static void PlainServerEventsCloseEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServer *ev_tcpsrv      = base_ptr;
	CommEvTCPServerConn *conn_hnd	= CommEvTCPServerConnArenaGrab(ev_tcpsrv, fd);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Close event with [%d] bytes\n", fd, to_read_sz);
	return;
}
/**************************************************************************************************************************/
