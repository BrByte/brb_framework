/*
 * test_ssh_client.c
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

static CommEvTCPClientCBH SSHClientEventRead;
static CommEvTCPClientCBH SSHClientEventClose;
static CommEvTCPClientCBH SSHClientEventConnect;
static EvBaseKQCBH SSHClientSendCMDTimer;

EvKQBase *glob_ev_base;
EvDNSResolverBase *glob_ev_dns;

/**************************************************************************************************************************/
int main(void)
{
	CommEvSSHClient *ev_sshclient;
	CommEvSSHClientConf ev_sshclient_conf;
	EvDNSResolverConf dns_conf;

	/* Creave event base */
	glob_ev_base = EvKQBaseNew(NULL);

	memset(&dns_conf, 0, sizeof(EvDNSResolverConf));

	/* Fill up DNS configuration */
	dns_conf.dns_ip_str			= "8.8.8.8";
	dns_conf.dns_port			= 53;
	dns_conf.lookup_timeout_ms	= 500;
	dns_conf.retry_timeout_ms	= 50;
	dns_conf.retry_count		= 10;

	glob_ev_dns	= CommEvDNSResolverBaseNew(glob_ev_base, &dns_conf);


	/* Clean configuration structure for SSH client */
	memset(&ev_sshclient_conf, 0, sizeof(CommEvSSHClientConf));

	ev_sshclient_conf.resolv_base	= glob_ev_dns;
	ev_sshclient_conf.hostname		= "ssh.domain.com";
	ev_sshclient_conf.username		= "root";
	ev_sshclient_conf.password		= "root";
	ev_sshclient_conf.port			= 6922;

	/* Set flags */
	ev_sshclient_conf.flags.notify_conn_progress = 0;
	ev_sshclient_conf.flags.reconnect_on_timeout = 1;
	ev_sshclient_conf.flags.reconnect_on_close	 = 1;

	ev_sshclient_conf.retry_times.reconnect_after_timeout_ms	= 5000;
	ev_sshclient_conf.retry_times.reconnect_after_close_ms		= 5000;

	/* Set timeout information */
	ev_sshclient_conf.timeout.connect_ms = 5000;


	ev_sshclient	= CommEvSSHClientNew(glob_ev_base);


	CommEvSSHClientEventSet(ev_sshclient, COMM_CLIENT_EVENT_CONNECT, SSHClientEventConnect, NULL);
	CommEvSSHClientEventSet(ev_sshclient, COMM_CLIENT_EVENT_CLOSE, SSHClientEventClose, NULL);
	CommEvSSHClientEventSet(ev_sshclient, COMM_CLIENT_EVENT_READ, SSHClientEventRead, NULL);

	CommEvSSHClientConnect(ev_sshclient, &ev_sshclient_conf);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	exit(0);
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void SSHClientEventRead(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSSHClient *ssh_client = base_ptr;

	printf("SSHClientEventRead - FD [%d] - DATA_SZ [%d] DATA [%s]\n", fd, action, MemBufferDeref(ssh_client->read_buffer));

	MemBufferClean(ssh_client->read_buffer);

	return;
}
/**************************************************************************************************************************/
static void SSHClientEventClose(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSSHClient *ssh_client = base_ptr;

	printf("SSHClientEventClose - FD [%d] - CLOSED\n", fd);

	return;
}
/**************************************************************************************************************************/
static void SSHClientEventConnect(int fd, int action, int thrd_id, void *cb_data, void *base_ptr)
{
	MemBuffer *req_mb;
	CommEvSSHClient *ev_sshclient = base_ptr;

	if (COMM_CLIENT_STATE_CONNECTED == ev_sshclient->socket_state)
	{
		MemBuffer *req_mb = MemBufferNew(8092, BRBDATA_THREAD_UNSAFE);
		MemBufferPrintf(req_mb, "date\n");

		printf("SSHClientEventConnect - FD [%d] connected, sending req\n", fd);
		CommEvSSHClientAIOWriteAndDestroyMemBuffer(ev_sshclient, req_mb, NULL, NULL);

		/* Schedule send command timer */
		EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2000, SSHClientSendCMDTimer, ev_sshclient);
	}
	else if (ev_sshclient->socket_state > COMM_CLIENT_STATE_CONNECTED)
	{
		printf("SSHClientEventConnect - FD [%d] FAILED connecting - STATE [%d]\n", fd, ev_sshclient->socket_state);
	}
	else
	{
		printf("SSHClientEventConnect - FD [%d] HANDSHAKING - STATE [%d]\n", fd, ev_sshclient->socket_state);
	}

	return;
}
/**************************************************************************************************************************/
static int SSHClientSendCMDTimer(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	MemBuffer *req_mb;
	CommEvSSHClient *ev_sshclient = cb_data;

	if (COMM_CLIENT_STATE_CONNECTED == ev_sshclient->socket_state)
	{
		printf("SSHClientSendCMDTimer - [%d] - Sending request to [%s]\n", ev_sshclient->socket_fd, inet_ntoa(ev_sshclient->conn_addr.sin_addr));

		req_mb = MemBufferNew(8092, BRBDATA_THREAD_UNSAFE);
		MemBufferPrintf(req_mb, "exit\n");

		CommEvSSHClientAIOWriteAndDestroyMemBuffer(ev_sshclient, req_mb, NULL, NULL);

		/* Schedule send command timer */
		EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2000, SSHClientSendCMDTimer, ev_sshclient);
	}

	return 1;
}
/**************************************************************************************************************************/

