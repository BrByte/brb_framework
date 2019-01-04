/*
 * test_test_unit.c
 *
 *  Created on: 2012-08-29
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2012 BrByte Software (Oliveira Alves & Amorim LTDA)
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

EvKQBase *ev_base;
struct sockaddr_in clientaddr;

void timer_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr);
void signal_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr);

void read_cb(int fd, int write_sz, int thrd_id, void *cb_data, void *base_ptr);
void eof_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr);
void error_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr);
void timer_cb2(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr);
void timeout_cb(int fd, int write_sz, int thrd_id, void *cb_data, void *base_ptr);

void server_before_accept(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr);
void server_after_accept(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr);

/**************************************************************************************************************************/
int
_setnonblock_fd(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;

	return 1;
}
/**************************************************************************************************************************/
void timeout_cb(int fd, int write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	return;
}
/**************************************************************************************************************************/
void write_cb(int fd, int write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	int len;
	char *cmd = "GET /index.html HTTP/1.0\r\n\r\n";

	EvKQBase *ev_base = base_ptr;

	/* write into socket */
	len = write(fd, cmd, strlen(cmd));

	//usleep(10);

	if (len > 0)
	{
		printf("WRITE EVENT - OK - [%d] bytes writen on FD [%d] by thrd_id [%d]\n", len, fd, thrd_id);

//		EvKQBaseSetTimeout(ev_base, fd, COMM_EV_TIMEOUT_READ, 10, timeout_cb, NULL);
//		EvKQBaseSetTimeout(ev_base, fd, COMM_EV_TIMEOUT_WRITE, 10, timeout_cb, NULL);
//		EvKQBaseSetTimeout(ev_base, fd, COMM_EV_TIMEOUT_BOTH, 10, timeout_cb, NULL);
//
//		EvKQBaseSetEvent(ev_base, fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, read_cb, NULL);
//		EvKQBaseSetEvent(ev_base, fd, COMM_EV_ERROR, COMM_ACTION_ADD_VOLATILE, error_cb, NULL);
//		EvKQBaseSetEvent(ev_base, fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, eof_cb, NULL);
	}
	else
	{
		//printf("WRITE EVENT - ERROR CODE [%d] on FD [%d] by thrd_id [%d]\n", len, fd, thrd_id);
		EvKQBaseSocketClose(ev_base, fd);
	}
}
/**************************************************************************************************************************/
void eof_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	char read_buf[8092];
	int read_sz;
	EvKQBase *ev_base = base_ptr;

	printf ("EOF EVENT - [%d] on FD [%d] from thrd_id [%d]\n", to_read_sz, fd, thrd_id);

	EvKQBaseSocketClose(ev_base, fd);

	return;
}
/**************************************************************************************************************************/
void error_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	char read_buf[8092];
	int read_sz;
	EvKQBase *ev_base = base_ptr;

	printf ("ERROR EVENT - [%d] on FD [%d] from thrd_id [%d]\n", to_read_sz, fd, thrd_id);

	close(fd);

	return;
}
/**************************************************************************************************************************/
void timer_cb2(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{

	int i;

	//printf("timer_cb2 - TIMER event on timer_id [%d] from thread_id [%d]\n", fd, thrd_id);

	//	EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_VOLATILE, 500, timer_cb2, NULL);

	//	sleep(3);

	int new_socket;

	for (i = 0; i < 10; i++)
	{
		/* Create a new socket */
		new_socket = socket(PF_INET, SOCK_STREAM, 0);

		if (new_socket > 0)
		{
			if (_setnonblock_fd(new_socket) > 0)
			{
				/* Connect new_socket */
				connect(new_socket, (struct sockaddr*)&clientaddr, sizeof(struct sockaddr));

//				EvKQBaseSetEvent(ev_base, new_socket, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, write_cb, NULL);
			}
		}
		else
			break;
	}

	sleep(1);
	return;
}
/**************************************************************************************************************************/
void timer_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base = base_ptr;
	static int count = 0;
	long i;

	char *abc;

	//kq_thrd = &ev_base->thread_pool.pool[thrd_id];
	printf("timer_cb - TIMER event on timer_id [%d] from thread_id [%d]\n", fd, thrd_id);

	//EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_PERSIST, 100, timer_cb, NULL);
	//sleep(3);
	//return;

	//	sleep(3);
	//	return;
	//	for (i = 0; i < 999999; i++)
	//	{
	//		abc = malloc(i);
	//		free(abc);
	//	}

	//printf("timer_cb - reconnect\n");

	int new_socket;

	for (i = 0; i < 20; i++)
	{
		/* Create a new socket */
		new_socket = socket(PF_INET, SOCK_STREAM, 0);

		if (new_socket > 0)
		{
			if (_setnonblock_fd(new_socket) > 0)
			{
				/* Connect new_socket */
				connect(new_socket, (struct sockaddr*)&clientaddr, sizeof(struct sockaddr));

//				EvKQBaseSetEvent(ev_base, new_socket, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, write_cb, NULL);
			}
		}
		else
			break;
	}

	//	EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_VOLATILE, 25, timer_cb, NULL);
	//sleep(1);
	return;

	//	sleep(5);
	//	EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_PERSIST, 100, timer_cb, NULL);


	if (fd == 1 && count == 0)
	{
		printf("cancel timer 0\n");
		EvKQBaseTimerCtl(ev_base, 0, COMM_ACTION_DISABLE);
		count = 1;

		return;
	}

	if (fd == 1 && count == 1)
	{
		printf("enable timer 0\n");
		EvKQBaseTimerCtl(ev_base, 0, COMM_ACTION_ENABLE);
		count = 0;

		return;
	}

	return;

}
/**************************************************************************************************************************/
void read_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	//char read_buf[to_read_sz + 10];
	int read_sz;
	//CommEvTCPServer *srv_ptr	= base_ptr;

	//	/* Grab a connection handler from server internal arena */
	//	CommEvTCPServerConn *conn_hnd = CommEvTCPServerConnArenaGrab(srv_ptr, fd);
	//
	//	printf ("READ EVENT - [%d] bytes ready to read on FD [%d] from thrd_id [%d] - Stream size is [%lu] - Node count is [%d]\n",
	//			to_read_sz, fd, thrd_id, MemStreamGetDataSize(conn_hnd->read_stream), MemStreamGetNodeCount(conn_hnd->read_stream));

	//MemStreamClean(conn_hnd->read_stream);

	//close(fd);

	EvKQBaseSocketClose(ev_base, fd);

	return;

	/* Clean stack space */
	//memset(&read_buf, 0, to_read_sz + 2);

	//read_sz = read(fd, read_buf, to_read_sz - 1);
	//
	//	if (read_sz < 0)
	//	{
	//		//printf("READ EVENT - ERROR or ZERO READ - [%d] on FD [%d]\n", read_sz, fd);
	//		close(fd);
	//	}
	//	else
	//	{
	//		printf("READ EVENT - OK - [%d] bytes from FD [%d]\n", read_sz, fd);
	//		//EvKQBaseSetEvent(ev_base, fd, COMM_EV_READ,  COMM_ACTION_ADD_VOLATILE, read_cb, NULL);
	//
	//		return;
	//	}

	//printf("buffer -> %s\n", read_buf);

	//	close(fd);
}
/**************************************************************************************************************************/
void _SignalHandler(int signal)
{
	//printf ("RECEIVED SIGNAL [%d]\n", signal);
	return;
}
/**************************************************************************************************************************/
int IgnoreSignals(void)
{
	int i;

	signal(SIGPIPE, SIG_IGN);


	return 1;
}
/**************************************************************************************************************************/
void server_before_accept(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	printf("server_before_accept - EVENT\n");
}
/**************************************************************************************************************************/
void server_after_accept(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	int size = 1024 * 1024;
	char buffer[1024 * 1024];
	int i;
	CommEvTCPServer *tcp_srv = base_ptr;
	CommEvTCPServerConn *conn_hnd = NULL;

	//fd++;

	/* Grab a connection handler from server internal arena */
	conn_hnd = CommEvTCPServerConnArenaGrab(tcp_srv, fd);



	/* Fill buffer */
	memset(&buffer, 0, size - 8);
	memset(&buffer, 'a', size  - 64);

	printf("server_after_accept - fd [%d] - srv_ptr at [%p] - conn_hnd at [%p] - from [%s]\n", fd, tcp_srv, conn_hnd, conn_hnd->string_ip);

	//	CommEvTCPServerConnAIOWriteString(conn_hnd, (char*)&buffer, NULL, NULL);
	//	CommEvTCPServerConnAIOWriteString(conn_hnd, (char*)&buffer, NULL, NULL);
	//	CommEvTCPServerConnAIOWriteString(conn_hnd, (char*)&buffer, NULL, NULL);
	//
	//	printf("server_after_accept - EVENT\n");
	//
	//	sleep(1);
}
/**************************************************************************************************************************/
void signal_cb(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	printf("signal_cb - received signal [%d] on thrd_id [%d], read_sz [%d]\n", fd, thrd_id, to_read_sz);

	return;
}
/**************************************************************************************************************************/
void filemon_cb(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	printf("filemon_cb - received action [%d] on fd [%d] - thrd_id [%d]\n", action, fd, thrd_id);




	switch(action)
	{
	case NOTE_DELETE: printf("filemon_cb - ACTION IS DELETE\n"); break;

	case NOTE_WRITE:  printf("filemon_cb - ACTION IS WRITE\n"); break;

	case NOTE_EXTEND:  printf("filemon_cb - ACTION IS EXTEND\n"); break;

	case NOTE_ATTRIB:   printf("filemon_cb - ACTION IS ATTRIB\n"); break;

	case NOTE_LINK:     printf("filemon_cb - ACTION IS LINK\n"); break;

	case NOTE_RENAME:   printf("filemon_cb - ACTION IS RENAME\n"); break;
	}

//	EvKQBaseSetEvent(ev_base, fd, COMM_EV_FILEMON, COMM_ACTION_ADD_VOLATILE, filemon_cb, NULL);

	return;
}
/**************************************************************************************************************************/
static void cbh(void *icmp_base_ptr, void *req_cb_data_ptr, void *icmp_reply_ptr)
{
	EvICMPBase *icmp_base0 = icmp_base_ptr;
	ICMPReply *icmp_reply = icmp_reply_ptr;
	struct timeval timeval_now;
	struct timeval *timeval_reply = (struct timeval *)&icmp_reply->icmp_packet->payload;
	int latency;

	int i;

	/* No reply packet, this is a timeout */
	if (!icmp_reply->icmp_packet)
	{
		//CommEvICMPEchoSendRequest(icmp_base0, "10.10.10.34", 0, i, 5000, 500, cbh, NULL);
		printf("ICMP Timed out - Seq ID [%d]\n", icmp_reply->icmp_seq);
		return;
	}

	/* Initialize local time and target_addr info */
	gettimeofday((struct timeval*)&timeval_now, NULL);

	latency = CommEvICMPtvSubMsec(timeval_reply, &timeval_now);

	//CommEvICMPEchoSendRequest(icmp_base0, "10.10.10.34", 0, i, 5000, 500, cbh, NULL);

	printf("ICMP Echo reply from [%s] - Seq ID [%d] - Size [%d] - Latency [%d] ms\n", inet_ntoa(icmp_reply->ip_header->source_ip),
			icmp_reply->icmp_packet->icmp_seq, icmp_reply->icmp_payload_sz, latency);

}
/**************************************************************************************************************************/
void cli_read_cbh(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient = base_ptr;
	printf("cli_read_cbh - FD [%d] - DATA_SZ [%d] DATA [%s]\n", fd, action, MemBufferDeref(ev_tcpclient->iodata.read_buffer));
}
/**************************************************************************************************************************/
void cli_close_cbh(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient = base_ptr;
	printf("cli_close_cbh - FD [%d] - CLOSED\n", fd);
}
/**************************************************************************************************************************/
void cli_connect_cbh(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient = base_ptr;

	MemBuffer *req_mb = MemBufferNew(8092, BRBDATA_THREAD_UNSAFE);
	MemBufferPrintf(req_mb, "GET / \r\nHTTP 1.0\r\nHost: www.google.com.br\r\n");

	if (COMM_CLIENT_STATE_CONNECTED == ev_tcpclient->socket_state)
	{
		printf("cli_connect_cbh - FD [%d] connected, sending req\n", fd);
		CommEvTCPClientAIOWriteMemBuffer(ev_tcpclient, req_mb, NULL, NULL);
	}
	else
		printf("cli_connect_cbh - FD [%d] FAILED connecting\n", fd);
}
/**************************************************************************************************************************/
void ipc_read_cbh(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	EvIPCBase *ipc_base = base_ptr;
	printf("ipc_read_cbh - FD [%d] - DATA_SZ [%d] DATA [%s]\n", fd, action, MemBufferDeref(ipc_base->read_buffer));
}
/**************************************************************************************************************************/
void ipc_close_cbh(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	EvIPCBase *ipc_base = base_ptr;
	printf("ipc_close_cbh - FD [%d] - DATA_SZ [%d] DATA [%s]\n", fd, action, MemBufferDeref(ipc_base->read_buffer));
}
/**************************************************************************************************************************/
int main(void)
{

	EvIPCBase *ipc_base;
	EvICMPBase *icmp_base0;

	CommEvTCPClient *ev_tcpclient;
	CommEvTCPServer *tcp_srv;
	int i;
	int kq_retcode;
	int timer_id;
	int timer_id2;

	char *args[8];

	int op_status = 0;

//	IgnoreSignals();

	struct in_addr target_addr;
	struct in_addr target_addr1;

	args[0] 	= "/usr/bin/gdb";
	args[1] 	= NULL;


	ev_base 	= EvKQBaseNew(NULL);

	ipc_base = EvIPCBaseNew(ev_base);

	EvIPCBaseEventSet(ipc_base, IPC_EVENT_READ, ipc_read_cbh, NULL);
	EvIPCBaseEventSet(ipc_base, IPC_EVENT_CLOSE, ipc_close_cbh, NULL);

	EvIPCBaseExecute(ipc_base, "/usr/bin/gdb", args);

	EvIPCWriteStringFmt(ipc_base, NULL, NULL, "help\n");



//	ev_tcpclient = CommEvTcpClientNew(ev_base, COMM_CLIENT_READ_MEMBUFFER, COMM_CLIENTPROTO_SSL);
//
//	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_CONNECT, cli_connect_cbh, NULL);
//	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_CLOSE, cli_close_cbh, NULL);
//	CommEvTCPClientEventSet(ev_tcpclient, COMM_CLIENT_EVENT_READ, cli_read_cbh, NULL);
//
//	CommEvTcpClientConnect(ev_tcpclient, "74.125.229.248", 443);



	//	icmp_base0 = CommEvICMPBaseNew(ev_base);
	//
	//	for (i = 0; i < 10; i++)
	//	{
	//		CommEvICMPEchoSendRequest(icmp_base0, "10.10.10.34", 0, i, 500, 500, cbh, NULL);
	//	}


	EvKQBaseDispatch(ev_base, 5);
	EvKQBaseDestroy(ev_base);

	return 0;


	clientaddr.sin_family = AF_INET;
	clientaddr.sin_port = htons(3128);
	clientaddr.sin_addr.s_addr = inet_addr("10.10.10.50");

	int a;

	//sleep(1);

	//for (i = 0; i < 10; i++)
//	a = EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_PERSIST, 100, timer_cb, NULL);
//	a = EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_PERSIST, 100, timer_cb2, NULL);
	//a = EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_VOLATILE, 100, timer_cb, NULL);

	//printf ("a - added timer id [%d]\n", a);

	//int b = EvKQBaseTimerAdd(ev_base, COMM_ACTION_ADD_VOLATILE, 2000, timer_cb2, NULL);

	EvKQBaseDispatch(ev_base, 5);
	EvKQBaseDestroy(ev_base);

	//	printf ("b - added timer id [%d]\n", b);
	return 0;


	int file_fd = open("/teste.abc", (O_WRONLY | O_TRUNC | O_CREAT ));

	printf ("file opened at fd [%d]\n", file_fd);

//	EvKQBaseSetEvent(ev_base, file_fd, COMM_EV_FILEMON, COMM_ACTION_ADD_VOLATILE, filemon_cb, NULL);
	//	signal(8, SIG_IGN);
	//
	//	EvKQBaseSetSignal(ev_base, 8, COMM_ACTION_ADD_PERSIST, signal_cb, NULL);
	//	tcp_srv = CommEvTCPServerNew(ev_base, 1199, COMM_SERVER_BINDANY);
	//
	//	CommEvTCPServerSetEvent(tcp_srv, COMM_SERVER_EVENT_ACCEPT_BEFORE, server_before_accept, NULL);
	//	CommEvTCPServerSetEvent(tcp_srv, COMM_SERVER_EVENT_ACCEPT_AFTER, server_after_accept, NULL);
	//	CommEvTCPServerSetEvent(tcp_srv, COMM_SERVER_EVENT_DEFAULT_READ, read_cb, NULL);
	//
	//
	//	op_status = CommEvTCPServerInit(tcp_srv);
	//
	//	if (op_status == COMM_SERVER_INIT_OK)
	//		printf("init ok on port 1199\n");
	//	else
	//		printf("failed to init\n");

	EvKQBaseDispatch(ev_base, 5);
	EvKQBaseDestroy(ev_base);

	return 0;
}
/**************************************************************************************************************************/
