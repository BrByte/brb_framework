/*
 * comm_ssh_client.c
 *
 *  Created on: 2013-12-02
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2013 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#include "../include/libbrb_ev_kq.h"

/* AIO events */
static EvBaseKQCBH CommEvSSHClientEventConnect;
static EvBaseKQCBH CommEvSSHClientEventEof;
static EvBaseKQCBH CommEvSSHClientEventRead;
static EvBaseKQCBH CommEvSSHClientEventWrite;

static EvBaseKQCBH CommEvSSHClientEventSSHNegotiate;
static EvBaseKQCBH CommEvSSHClientTimerConnectTimeout;
static EvBaseKQCBH CommEvSSHClientTimerReconnect;

static void CommEvSSHClientDestroyConnReadAndWriteBuffers(CommEvSSHClient *ev_sshclient);
static void CommEvSSHClientInternalDisconnect(CommEvSSHClient *ev_sshclient);
static void CommEvSSHClientEnqueueAndKickWriteQueue(CommEvSSHClient *ev_sshclient, EvAIOReq *aio_req);
static void CommEvSSHClientEnqueueHeadAndKickWriteQueue(CommEvSSHClient *ev_sshclient, EvAIOReq *aio_req);
static void CommEvSSHClientAddrInitFromAReply(CommEvSSHClient *ev_sshclient);
static int CommEvSSHAsyncConnect(CommEvSSHClient *ev_sshclient);
static void CommEvSSHClientEventDispatchInternal(CommEvSSHClient *ev_sshclient, int data_sz, int thrd_id, int ev_type);
static void CommEvSSHClientAddrInit(CommEvSSHClient *ev_sshclient, char *host, unsigned short port);
static void CommEvSSHClientAddrInitFromAReply(CommEvSSHClient *ev_sshclient);
static int CommEvSSHClientGetDirection(CommEvSSHClient *ev_sshclient, int res);
static int CommEvSSHClientCheckIfNeedDNSLookup(CommEvSSHClient *ev_sshclient);

static CommEvDNSResolverCBH CommEvSSHClientDNSResolverCB;
static int CommEvSSHClientTimersCancelAll(CommEvSSHClient *ev_sshclient);


/**************************************************************************************************************************/
CommEvSSHClient *CommEvSSHClientNew(EvKQBase *kq_base)
{
	CommEvSSHClient *ev_sshclient;
	int socket_fd;

	/* Create socket and set it to non_blocking */
	socket_fd 									= EvKQBaseSocketTCPNew(kq_base);

	/* Check if created socket is ok */
	if (socket_fd < 0)
		return NULL;

	ev_sshclient								= calloc(1, sizeof(CommEvSSHClient));
	ev_sshclient->kq_base						= kq_base;

	ev_sshclient->socket_fd						= socket_fd;
	ev_sshclient->socket_state					= COMM_CLIENT_STATE_DISCONNECTED;

	/* Initialize timer IDs */
	ev_sshclient->timers.reconnect_after_close_id	= -1;
	ev_sshclient->timers.reconnect_after_timeout_id	= -1;
	ev_sshclient->timers.reconnect_on_fail_id		= -1;
	ev_sshclient->timers.calculate_datarate_id		= -1;

	/* Default read/write handlers */
	ev_sshclient->io_func[COMM_EV_IOFUNC_READ]	= (CommEvIOFunc*)read;
	ev_sshclient->io_func[COMM_EV_IOFUNC_WRITE]	= (CommEvIOFunc*)write;

	/* Initialize WRITE_QUEUE */
	EvAIOReqQueueInit(ev_sshclient->kq_base, &ev_sshclient->write_queue, 4096,
			(ev_sshclient->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

	/* Set it to non_blocking and save it into newly allocated client */
	EvKQBaseSocketSetNonBlock(ev_sshclient->kq_base, ev_sshclient->socket_fd);

	/* Set socket description */
	EvKQBaseFDDescriptionSetByFD(ev_sshclient->kq_base, ev_sshclient->socket_fd , "BRB_EV_COMM - SSH client");

	return ev_sshclient;
}
/**************************************************************************************************************************/
void CommEvSSHClientDestroy(CommEvSSHClient *ev_sshclient)
{
	if (!ev_sshclient)
		return;

	/* Close the socket and cancel any pending events */
	if (ev_sshclient->socket_fd > -1)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLOSING\n", ev_sshclient->socket_fd);
		EvKQBaseSocketClose(ev_sshclient->kq_base, ev_sshclient->socket_fd);
		ev_sshclient->socket_fd = -1;
	}

	/* Cancel all pending timers */
	CommEvSSHClientTimersCancelAll(ev_sshclient);

	/* Cancel pending DNS request */
	if (ev_sshclient->dnsreq_id	> -1)
	{
		//printf("CommEvTCPClientDestroy - Will cancel pending request\n");
		CommEvDNSCancelPendingRequest(ev_sshclient->resolv_base, ev_sshclient->dnsreq_id);
	}

	/* Free WILLY */
	free(ev_sshclient);

	return;
}
/**************************************************************************************************************************/
int CommEvSSHClientConnect(CommEvSSHClient *ev_sshclient, CommEvSSHClientConf *ev_sshclient_conf)
{
	int op_status = 0;

	/* Sanity check */
	if ((!ev_sshclient) || (!ev_sshclient_conf->hostname))
		return 0;

	/* Load user_name and password and HOSTNAME */
	strncpy((char*)&ev_sshclient->cfg.username, ev_sshclient_conf->username, sizeof(ev_sshclient->cfg.username));
	strncpy((char*)&ev_sshclient->cfg.password, ev_sshclient_conf->password, sizeof(ev_sshclient->cfg.password));
	strncpy((char*)&ev_sshclient->cfg.hostname, ev_sshclient_conf->hostname, sizeof(ev_sshclient->cfg.hostname));
	ev_sshclient->cfg.port = ev_sshclient_conf->port;

	/* Check now if user sent us a HOSTNAME and we have no resolver base defined - Will set flags.need_dns_lookup */
	if (CommEvSSHClientCheckIfNeedDNSLookup(ev_sshclient) && (!ev_sshclient_conf->resolv_base))
		return 0;

	/* Load resolver base */
	ev_sshclient->resolv_base					= ev_sshclient_conf->resolv_base;

	/* Load flags */
	ev_sshclient->flags.notify_conn_progress	= ev_sshclient_conf->flags.notify_conn_progress;
	ev_sshclient->flags.reconnect_on_timeout	= ev_sshclient_conf->flags.reconnect_on_timeout;
	ev_sshclient->flags.reconnect_on_close		= ev_sshclient_conf->flags.reconnect_on_close;
	ev_sshclient->flags.reconnect_on_fail		= ev_sshclient_conf->flags.reconnect_on_fail;
	ev_sshclient->flags.check_known_hosts		= ev_sshclient_conf->flags.check_known_hosts;
	ev_sshclient->flags.reconnect_new_dnslookup		= ev_sshclient_conf->flags.reconnect_new_dnslookup;
	ev_sshclient->flags.reconnect_balance_on_ips	= ev_sshclient_conf->flags.reconnect_balance_on_ips;

	/* Load timeout information */
	ev_sshclient->timeout.connect_ms	= ev_sshclient_conf->timeout.connect_ms;
	ev_sshclient->timeout.read_ms		= ev_sshclient_conf->timeout.read_ms;
	ev_sshclient->timeout.write_ms		= ev_sshclient_conf->timeout.write_ms;

	/* Load retry timers information */
	ev_sshclient->retry_times.reconnect_after_timeout_ms	= ev_sshclient_conf->retry_times.reconnect_after_timeout_ms;
	ev_sshclient->retry_times.reconnect_after_close_ms		= ev_sshclient_conf->retry_times.reconnect_after_close_ms;
	ev_sshclient->retry_times.reconnect_on_fail_ms			= ev_sshclient_conf->retry_times.reconnect_on_fail_ms;

	/* Copy KNOWN HOSTS file location, if supplied */
	if (ev_sshclient_conf->known_hosts_filepath)
		strncpy((char*)&ev_sshclient->cfg.known_hosts_path, ev_sshclient_conf->known_hosts_filepath, sizeof(ev_sshclient->cfg.known_hosts_path));

	/* Check if host needs to be resolved - If RESOLV fails, invoke COMM_CLIENT_STATE_CONNECT_FAILED_DNS */
	if (ev_sshclient->flags.need_dns_lookup)
	{
		//printf("CommEvSSHClientConnect - Begin LOOKUP of hostname [%s]\n", ev_sshclient_conf->hostname);
		ev_sshclient->dnsreq_id = CommEvDNSGetHostByName(ev_sshclient->resolv_base, ev_sshclient_conf->hostname, CommEvSSHClientDNSResolverCB, ev_sshclient);

		if (ev_sshclient->dnsreq_id	> -1)
		{
			ev_sshclient->socket_state	= COMM_CLIENT_STATE_RESOLVING_DNS;
			return 1;
		}
		else
			return 0;
	}
	/* No need to resolve, plain old IP address */
	else
	{
		/* Initialize address */
		CommEvSSHClientAddrInit(ev_sshclient, ev_sshclient_conf->hostname, ev_sshclient_conf->port);

		/* Issue ASYNC connect */
		op_status = CommEvSSHAsyncConnect(ev_sshclient);
	}

	return op_status;
}
/**************************************************************************************************************************/
void CommEvSSHClientResetFD(CommEvSSHClient *ev_sshclient)
{
	int old_socketfd;

	if (ev_sshclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
		return;

	/* Save a copy of old socket_fd */
	old_socketfd = ev_sshclient->socket_fd;

	/* Create a new socket */
	ev_sshclient->socket_fd = EvKQBaseSocketTCPNew(ev_sshclient->kq_base);

	/* Set it to non_blocking and save it into newly allocated client */
	EvKQBaseSocketSetNonBlock(ev_sshclient->kq_base, ev_sshclient->socket_fd);

	/* Will close socket and cancel any pending events of old_socketfd */
	if (old_socketfd > 0)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLOSING\n", ev_sshclient->socket_fd);
		EvKQBaseSocketClose(ev_sshclient->kq_base, old_socketfd);
	}

	/* Set socket description */
	EvKQBaseFDDescriptionSetByFD(ev_sshclient->kq_base, ev_sshclient->socket_fd, "BRB_EV_COMM - SSH client");

	return;
}
/**************************************************************************************************************************/
int CommEvSSHClientReconnect(CommEvSSHClient *ev_sshclient)
{
	int op_status;


	/* Sanity check */
	if (!ev_sshclient)
		return 0;

	if (ev_sshclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
		return 0;

	/* Reset FD */
	CommEvSSHClientResetFD(ev_sshclient);

	/* If we are connecting to a HOSTNAME FQDN, check if our DNS entry is expired, or if upper layers asked for new lookup on reconnect */
	if ( (ev_sshclient->flags.need_dns_lookup) && (ev_sshclient->flags.reconnect_new_dnslookup || (ev_sshclient->dns.expire_ts <= ev_sshclient->kq_base->stats.cur_invoke_ts_sec) ))
	{
		ev_sshclient->dnsreq_id = CommEvDNSGetHostByName(ev_sshclient->resolv_base, (char*)&ev_sshclient->cfg.hostname, CommEvSSHClientDNSResolverCB, ev_sshclient);

		if (ev_sshclient->dnsreq_id	> -1)
		{
			ev_sshclient->socket_state	= COMM_CLIENT_STATE_RESOLVING_DNS;
			return 1;
		}
		else
			return 0;
	}
	/* Just connect */
	else
	{
		/* Client wants to rotate balance on multiple IP entries if possible, do it */
		if (ev_sshclient->flags.reconnect_balance_on_ips && (ev_sshclient->dns.a_reply.ip_count > 1))
			CommEvSSHClientAddrInitFromAReply(ev_sshclient);

		/* Begin ASYNC connect */
		op_status = CommEvSSHAsyncConnect(ev_sshclient);

		return op_status;
	}


	return op_status;
}
/**************************************************************************************************************************/
void CommEvSSHClientDisconnect(CommEvSSHClient *ev_sshclient)
{
	/* Free up SSH layer */
	if (ev_sshclient->ssh.channel)
		libssh2_channel_free(ev_sshclient->ssh.channel);

	if (ev_sshclient->ssh.session)
	{
		libssh2_session_disconnect(ev_sshclient->ssh.session, "Normal Shutdown, Thank you for playing");

		/* Destroy SSH session */
		libssh2_session_free(ev_sshclient->ssh.session);
	}

	ev_sshclient->ssh.session = NULL;
	ev_sshclient->ssh.channel = NULL;

	/* Will close socket and cancel any pending events of ev_sshclient->socket_fd, including the close event */
	if (ev_sshclient->socket_fd > 0)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLOSING\n", ev_sshclient->socket_fd);
		EvKQBaseSocketClose(ev_sshclient->kq_base, ev_sshclient->socket_fd);
		ev_sshclient->socket_fd = -1;
	}

	/* Cancel all pending timers */
	CommEvSSHClientTimersCancelAll(ev_sshclient);

	/* Cancel pending DNS request */
	if (ev_sshclient->dnsreq_id	> -1)
	{
		//printf("CommEvTCPClientDestroy - Will cancel pending request\n");
		CommEvDNSCancelPendingRequest(ev_sshclient->resolv_base, ev_sshclient->dnsreq_id);
	}

	/* Destroy read and write buffers */
	CommEvSSHClientDestroyConnReadAndWriteBuffers(ev_sshclient);

	/* Adjust new client state */
	ev_sshclient->socket_state			= COMM_CLIENT_STATE_DISCONNECTED;
	ev_sshclient->socket_fd				= -1;

	/* Set flags */
	ev_sshclient->flags.authenticated	= 0;
	ev_sshclient->flags.handshake		= 0;
	ev_sshclient->flags.session_opened	= 0;

	return;
}
/**************************************************************************************************************************/
void CommEvSSHClientAIOWriteAndDestroyMemBuffer(CommEvSSHClient *ev_sshclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_sshclient->write_queue, ev_sshclient->socket_fd, ev_sshclient, MemBufferDeref(mem_buf), MemBufferGetSize(mem_buf), 0, NULL, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Populate destroy data */
	aio_req->destroy_func		= (EvAIOReqDestroyFunc*)MemBufferDestroy;
	aio_req->destroy_cbdata		= mem_buf;

	/* Enqueue and begin writing ASAP */
	CommEvSSHClientEnqueueAndKickWriteQueue(ev_sshclient, aio_req);

	return;
}
/**************************************************************************************************************************/
void CommEvSSHClientAIOWriteMemBuffer(CommEvSSHClient *ev_sshclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_sshclient->write_queue, ev_sshclient->socket_fd, ev_sshclient, MemBufferDeref(mem_buf), MemBufferGetSize(mem_buf), 0, NULL, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue and begin writing ASAP */
	CommEvSSHClientEnqueueAndKickWriteQueue(ev_sshclient, aio_req);

	return;
}
/**************************************************************************************************************************/
void CommEvSSHClientAIOWriteStringFmt(CommEvSSHClient *ev_sshclient, CommEvTCPClientCBH *finish_cb, void *finish_cbdata, char *string, ...)
{
	EvAIOReq *aio_req;
	va_list args;
	char *buf_ptr;
	int buf_sz;
	int msg_len;

	/* Probe message size */
	va_start(args, string);
	msg_len = vsnprintf(NULL, 0, string, args);
	va_end(args);

	/* Create a new buffer to hold it */
	buf_ptr = malloc(msg_len + 16);

	/* Write it into buffer and NULL terminate it */
	va_start( args, string );
	buf_sz = vsnprintf(buf_ptr, (msg_len + 1), string, args);
	buf_ptr[buf_sz] = '\0';
	va_end(args);

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_sshclient->write_queue, ev_sshclient->socket_fd, ev_sshclient, buf_ptr, buf_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue and begin writing ASAP */
	CommEvSSHClientEnqueueAndKickWriteQueue(ev_sshclient, aio_req);

	return;

}
/**************************************************************************************************************************/
void CommEvSSHClientAIOWriteString(CommEvSSHClient *ev_sshclient, char *string, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;
	int string_sz = strlen(string);

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_sshclient->write_queue, ev_sshclient->socket_fd, ev_sshclient, strdup(string), string_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue and begin writing ASAP */
	CommEvSSHClientEnqueueAndKickWriteQueue(ev_sshclient, aio_req);
	return;

}
/**************************************************************************************************************************/
void CommEvSSHClientAIOWrite(CommEvSSHClient *ev_sshclient, char *data, unsigned long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_sshclient->write_queue, ev_sshclient->socket_fd, ev_sshclient, data, data_sz, 0, NULL, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue and begin writing ASAP */
	CommEvSSHClientEnqueueAndKickWriteQueue(ev_sshclient, aio_req);
	return;

}
/**************************************************************************************************************************/
int CommEvSSHClientEventIsSet(CommEvSSHClient *ev_sshclient, CommEvTCPClientEventCodes ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_CLIENT_EVENT_LASTITEM)
		return 0;

	if (ev_sshclient->events[ev_type].cb_handler_ptr && ev_sshclient->events[ev_type].flags.enabled)
		return 1;

	return 0;
}
/**************************************************************************************************************************/
void CommEvSSHClientEventSet(CommEvSSHClient *ev_sshclient, CommEvTCPClientEventCodes ev_type, CommEvTCPClientCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= COMM_CLIENT_EVENT_LASTITEM)
		return;

	/* Set event */
	ev_sshclient->events[ev_type].cb_handler_ptr	= cb_handler;
	ev_sshclient->events[ev_type].cb_data_ptr		= cb_data;

	/* Mark enabled */
	ev_sshclient->events[ev_type].flags.enabled		= 1;

	return;
}
/**************************************************************************************************************************/
void CommEvSSHClientEventCancel(CommEvSSHClient *ev_sshclient, CommEvTCPClientEventCodes ev_type)
{
	/* Set event */
	ev_sshclient->events[ev_type].cb_handler_ptr		= NULL;
	ev_sshclient->events[ev_type].cb_data_ptr			= NULL;

	/* Mark disabled */
	ev_sshclient->events[ev_type].flags.enabled			= 0;

	/* Update kqueue_event */
	switch (ev_type)
	{
	case COMM_CLIENT_EVENT_READ:
		EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);
		break;
	}

	return;
}
/**************************************************************************************************************************/
void CommEvSSHClientEventCancelAll(CommEvSSHClient *ev_sshclient)
{
	int i;

	/* Cancel all possible events */
	for (i = 0; i < COMM_CLIENT_EVENT_LASTITEM; i++)
	{
		CommEvSSHClientEventCancel(ev_sshclient, i);
	}

	return;
}
/**************************************************************************************************************************/
void CommEvSSHClientTimeoutSet(CommEvSSHClient *ev_sshclient, int type, int time, EvBaseKQCBH *timeout_cb, void *cb_data)
{
	//EvKQBaseTimeoutSet(ev_sshclient->kq_base, ev_sshclient->socket_fd, type, time, timeout_cb, cb_data);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvSSHClientInternalDisconnect(CommEvSSHClient *ev_sshclient)
{
	/* Free up SSH layer */
	if (ev_sshclient->ssh.channel)
		libssh2_channel_free(ev_sshclient->ssh.channel);

	if (ev_sshclient->ssh.session)
	{
		libssh2_session_disconnect(ev_sshclient->ssh.session, "Normal Shutdown, Thank you for playing");

		/* Destroy SSH session */
		libssh2_session_free(ev_sshclient->ssh.session);
	}

	ev_sshclient->ssh.session = NULL;
	ev_sshclient->ssh.channel = NULL;

	/* Will close socket and cancel any pending events of ev_sshclient->socket_fd, including the close event */
	if (ev_sshclient->socket_fd > 0)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLOSING\n", ev_sshclient->socket_fd);
		EvKQBaseSocketClose(ev_sshclient->kq_base, ev_sshclient->socket_fd);
		ev_sshclient->socket_fd = -1;
	}

	/* Destroy read and write buffers */
	CommEvSSHClientDestroyConnReadAndWriteBuffers(ev_sshclient);

	/* If client want to be reconnected automatically after a CONN_TIMEOUT, honor it */
	if ( (COMM_CLIENT_STATE_CONNECT_FAILED_TIMEOUT == ev_sshclient->socket_state) && ev_sshclient->flags.reconnect_on_timeout)
	{
		ev_sshclient->timers.reconnect_after_timeout_id = EvKQBaseTimerAdd(ev_sshclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_sshclient->retry_times.reconnect_after_timeout_ms > 0) ? ev_sshclient->retry_times.reconnect_after_timeout_ms : COMM_SSH_CLIENT_RECONNECT_TIMEOUT_DEFAULT_MS),
				CommEvSSHClientTimerReconnect, ev_sshclient);
	}
	/* If client want to be reconnected automatically after an EOF, honor it */
	else if (ev_sshclient->flags.reconnect_on_close)
	{
		ev_sshclient->timers.reconnect_after_close_id = EvKQBaseTimerAdd(ev_sshclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_sshclient->retry_times.reconnect_after_close_ms > 0) ? ev_sshclient->retry_times.reconnect_after_close_ms : COMM_SSH_CLIENT_RECONNECT_CLOSE_DEFAULT_MS),
				CommEvSSHClientTimerReconnect, ev_sshclient);
	}

	/* Adjust new client state */
	ev_sshclient->socket_state			= COMM_CLIENT_STATE_DISCONNECTED;
	ev_sshclient->socket_fd				= -1;

	/* Set flags */
	ev_sshclient->flags.authenticated	= 0;
	ev_sshclient->flags.handshake		= 0;
	ev_sshclient->flags.session_opened	= 0;

	return;
}
/**************************************************************************************************************************/
static void CommEvSSHClientEnqueueHeadAndKickWriteQueue(CommEvSSHClient *ev_sshclient, EvAIOReq *aio_req)
{
	/* Enqueue it in conn_queue */
	EvAIOReqQueueEnqueueHead(&ev_sshclient->write_queue, aio_req);

	/* Do not ADD a write event if we are disconnected, as it will overlap our internal connect event */
	if (ev_sshclient->socket_state != COMM_CLIENT_STATE_CONNECTED)
		return;

	/* Ask the event base to write */
	EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventWrite, ev_sshclient);

	return;
}
/**************************************************************************************************************************/
static void CommEvSSHClientEnqueueAndKickWriteQueue(CommEvSSHClient *ev_sshclient, EvAIOReq *aio_req)
{
	int ssh_bytes_written;

	/* Allow upper layers to transform data */
	//EvAIOReqTransform(&ev_sshclient->transform, write_queue, aio_req);

	/* Do not ADD a write event if we are disconnected, as it will overlap our internal connect event */
	if (ev_sshclient->socket_state != COMM_CLIENT_STATE_CONNECTED)
		goto enqueue;

	/* Try to write right now to SSH tunnel */
	ssh_bytes_written = libssh2_channel_write(ev_sshclient->ssh.channel, aio_req->data.ptr, aio_req->data.size);

	/* Error reading - Check errors */
	if (ssh_bytes_written < 0)
	{
		/* Re_enqueue for writing */
		if (LIBSSH2_ERROR_EAGAIN == ssh_bytes_written)
			goto enqueue;
		else
			goto channel_close;
	}
	/* Wrote OK, destroy AIO request and bail out */
	else
	{
		/* Clear any WRITE timeout as we will never get cleared by BASE event */
		EvKQBaseTimeoutClear(ev_sshclient->kq_base, ev_sshclient->socket_fd, KQ_CB_TIMEOUT_WRITE);

		/* Invoke notification CALLBACKS */
		EvAIOReqInvokeCallBacksAndDestroy(aio_req, 1, ev_sshclient->socket_fd, ssh_bytes_written, -1, ev_sshclient);
		return;
	}


	/* Label to ENQUEUE request */
	enqueue:

	/* Enqueue it in conn_queue */
	EvAIOReqQueueEnqueue(&ev_sshclient->write_queue, aio_req);

	/* Do not ADD a write event if we are disconnected, as it will overlap our internal connect event */
	if (ev_sshclient->socket_state != COMM_CLIENT_STATE_CONNECTED)
		return;

	/* Ask the event base to write */
	EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventWrite, ev_sshclient);

	return;

	/* Label to deal with closed channels */
	channel_close:

	/* Dispatch internal event */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, 0, -1, COMM_CLIENT_EVENT_CLOSE);

	/* Do the internal disconnect and destroy IO buffers */
	CommEvSSHClientInternalDisconnect(ev_sshclient);

	return;
}
/**************************************************************************************************************************/
static void CommEvSSHClientAddrInit(CommEvSSHClient *ev_sshclient, char *host, unsigned short port)
{
	/* Clean conn_addr structure for later use */
	memset(&ev_sshclient->conn_addr, 0, sizeof(struct sockaddr_in));

	/* Fill in the stub sockaddr_in structure */
	ev_sshclient->conn_addr.sin_family		= AF_INET;
	ev_sshclient->conn_addr.sin_addr.s_addr	= inet_addr(host);
	ev_sshclient->conn_addr.sin_port		= htons(port);


	return;
}
/**************************************************************************************************************************/
static void CommEvSSHClientAddrInitFromAReply(CommEvSSHClient *ev_sshclient)
{
	/* Clean conn_addr structure for later use */
	memset(&ev_sshclient->conn_addr, 0, sizeof(struct sockaddr_in));

	/* Fill in the stub sockaddr_in structure */
	ev_sshclient->conn_addr.sin_family		= AF_INET;
	ev_sshclient->conn_addr.sin_port		= htons(ev_sshclient->cfg.port);

	/* Balance on IPs */
	if (ev_sshclient->flags.reconnect_balance_on_ips)
	{
		/* Copy currently pointed address */
		memcpy(&ev_sshclient->conn_addr.sin_addr.s_addr, &ev_sshclient->dns.a_reply.ip_arr[ev_sshclient->dns.cur_idx].addr, sizeof(struct in_addr));

		/* Adjust INDEX to rotate ROUND ROBIN */
		if (ev_sshclient->dns.cur_idx == (ev_sshclient->dns.a_reply.ip_count - 1) )
			ev_sshclient->dns.cur_idx = 0;
		else
			ev_sshclient->dns.cur_idx++;

	}
	else
	{
		memcpy(&ev_sshclient->conn_addr.sin_addr.s_addr, &ev_sshclient->dns.a_reply.ip_arr[0].addr, sizeof(struct in_addr));
	}


	return;
}
/**************************************************************************************************************************/
static int CommEvSSHClientReconnectTimer(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSSHClient *ev_sshclient = cb_data;

	/* Try to reconnect */
	CommEvSSHClientReconnect(ev_sshclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvSSHAsyncConnect(CommEvSSHClient *ev_sshclient)
{
	int op_status;

	/* Connect new_socket */
	op_status = connect(ev_sshclient->socket_fd, (struct sockaddr*)&ev_sshclient->conn_addr, sizeof(struct sockaddr_in));

	/* Success connecting */
	if (0 == op_status)
		goto conn_success;

	if (op_status < 0)
	{
		switch(errno)
		{
		/* Already connected, bail out */
		case EALREADY:
		case EINTR:
		{
			//printf("CommEvSSHAsyncConnect - ALREADY CONNECTED\n");
			return 1;
		}

		case EINPROGRESS:
			goto conn_success;

		default:
			goto conn_failed;
		}
	}

	/* Success beginning connection */
	conn_success:

	//printf("CommEvSSHAsyncConnect - SUCCESS BEGIN CONNECTING\n");

	/* Schedule WRITE event to catch ASYNC connect */
	EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventConnect, ev_sshclient);

	/* Set client state */
	ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECTING;

	/* If upper layers want CONNECT_TIMEOUT, set timeout on WRITE event on low level layer */
	if (ev_sshclient->timeout.connect_ms > 0)
		EvKQBaseTimeoutSet(ev_sshclient->kq_base, ev_sshclient->socket_fd, KQ_CB_TIMEOUT_WRITE, ev_sshclient->timeout.connect_ms, CommEvSSHClientTimerConnectTimeout, ev_sshclient);


	return 1;

	/* Failed to begin connection */
	conn_failed:

	//printf("CommEvSSHAsyncConnect - FAILED BEGIN CONNECTING\n");

	/* Failed connecting */
	ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_CONNECT_SYSCALL;

	/* Dispatch the internal event */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, 0, -1, COMM_CLIENT_EVENT_CONNECT);

	/* Upper layers want a reconnect retry if CONNECTION FAILS */
	if (!ev_sshclient->flags.reconnect_on_fail)
		return 0;


	/* Will close socket and cancel any pending events of ev_tcpclient->socket_fd, including the close event */
	if (ev_sshclient->socket_fd > 0)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLOSING\n", ev_sshclient->socket_fd);
		EvKQBaseSocketClose(ev_sshclient->kq_base, ev_sshclient->socket_fd);
		ev_sshclient->socket_fd = -1;
	}

	/* Destroy read and write buffers */
	CommEvSSHClientDestroyConnReadAndWriteBuffers(ev_sshclient);

	ev_sshclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;
	ev_sshclient->socket_fd		= -1;

	/* Schedule RECONNECT timer */
	ev_sshclient->timers.reconnect_on_fail_id = EvKQBaseTimerAdd(ev_sshclient->kq_base, COMM_ACTION_ADD_VOLATILE,
			((ev_sshclient->retry_times.reconnect_on_fail_ms > 0) ? ev_sshclient->retry_times.reconnect_on_fail_ms : COMM_SSH_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
			CommEvSSHClientReconnectTimer, ev_sshclient);

	return 0;
}
/**************************************************************************************************************************/
static int CommEvSSHClientHandshake(CommEvSSHClient *ev_sshclient)
{
	int op_status;
	int dir_code;

	/* Already HANDSHAKED, bail out */
	if (ev_sshclient->flags.handshake)
		return 1;

	/* Do handshake */
	op_status	= libssh2_session_handshake(ev_sshclient->ssh.session, ev_sshclient->socket_fd);
	dir_code	= CommEvSSHClientGetDirection(ev_sshclient, op_status);

	/* libssh2_session_handshake will return ZERO on SUCCESS, NEGATIVE on FAIL */
	if (!op_status)
	{
		ev_sshclient->flags.handshake = 1;
		return 1;
	}
	/* Failed HANDSHAKING, PROCESS SSH2 errors */
	else
	{
		switch(dir_code)
		{
		case COMM_SSHCLIENT_DIRECTION_EAGAIN:
		{
			//printf("CommEvSSHClientHandshaking - Handshaking...\n");
			return -1;
		}
		case COMM_SSHCLIENT_DIRECTION_FAIL:
		{
			//printf("CommEvSSHClientHandshaking - FAILED handshaking\n");
			return 0;

		}
		case COMM_SSHCLIENT_DIRECTION_FINISH:
		{
			//printf("CommEvSSHClientHandshaking - FINISH handshaking\n");
			ev_sshclient->flags.handshake = 1;
			return 1;
		}
		}
	}

	/* Default return is FAIL */
	return 0;
}
/**************************************************************************************************************************/
static int CommEvSSHClientKnownHosts(CommEvSSHClient *ev_sshclient)
{
	struct libssh2_knownhost *host;
	const char *fingerprint;
	size_t len;
	int type;

	/* Get now hosts and write if new HOSTKEY session */
	if (!ev_sshclient->flags.known_host)
	{
		ev_sshclient->ssh.known_hosts = libssh2_knownhost_init(ev_sshclient->ssh.session);

		/* read all hosts from here */
		libssh2_knownhost_readfile(ev_sshclient->ssh.known_hosts, "known_hosts", LIBSSH2_KNOWNHOST_FILE_OPENSSH);

		/* store all known hosts to here */
		libssh2_knownhost_writefile(ev_sshclient->ssh.known_hosts, "dumpfile", LIBSSH2_KNOWNHOST_FILE_OPENSSH);

		fingerprint = libssh2_session_hostkey(ev_sshclient->ssh.session, &len, &type);

		if (!fingerprint)
		{
			//printf("CommEvSSHClientKnownHosts - No fingerprint found!\n");
			return 0;
		}

		int check = libssh2_knownhost_checkp(ev_sshclient->ssh.known_hosts, ev_sshclient->cfg.hostname, ev_sshclient->cfg.port, fingerprint, len, LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW, &host);

		//printf("CommEvSSHClientEventSSHNegotiate - Host check: %d, key: %s\n", check,  (check <= LIBSSH2_KNOWNHOST_CHECK_MISMATCH) ? host->key : "<none>");

		libssh2_knownhost_free(ev_sshclient->ssh.known_hosts);

		ev_sshclient->flags.known_host = 1;

		//printf("CommEvSSHClientKnownHosts - Known Host OK!\n");
		return 1;
	}

	return 1;
}
/**************************************************************************************************************************/
static int CommEvSSHClientAuthUser(CommEvSSHClient *ev_sshclient)
{
	int op_status;
	int dir_code;

	/* Already AUTH, bail out */
	if (ev_sshclient->flags.authenticated)
		return 1;

	/* Sent USER/PASS pair */
	op_status	= libssh2_userauth_password(ev_sshclient->ssh.session, (char*)&ev_sshclient->cfg.username, (char*)&ev_sshclient->cfg.password);
	dir_code	= CommEvSSHClientGetDirection(ev_sshclient, op_status);

	/* libssh2_session_handshake will return ZERO on SUCCESS, NEGATIVE on FAIL */
	if (!op_status)
	{
		goto done_auth;
	}
	/* Failed AUTHORIZING USER, PROCESS SSH2 errors */
	else
	{
		switch(dir_code)
		{
		case COMM_SSHCLIENT_DIRECTION_EAGAIN:
		{
			//printf("CommEvSSHClientAuthUser - Authenticating...\n");
			return -1;
		}
		case COMM_SSHCLIENT_DIRECTION_FAIL:
		{
			//printf("CommEvSSHClientAuthUser - FAILED authenticating\n");
			return 0;

		}
		case COMM_SSHCLIENT_DIRECTION_FINISH:
		{
			done_auth:

			/* Check if we are authenticated */
			ev_sshclient->flags.authenticated = libssh2_userauth_authenticated(ev_sshclient->ssh.session);
			//printf("CommEvSSHClientAuthUser - COMM_SSHCLIENT_DIRECTION_FINISH [%d]\n", ev_sshclient->flags.authenticated);

			if (!ev_sshclient->flags.authenticated)
			{
				//printf("CommEvSSHClientAuthUser - Authentication failed!\n");
				return 0;
			}
			else
			{
				//printf("CommEvSSHClientAuthUser - Authentication OK!\n");
				return 1;
			}
			break;
		}
		}
	}

	return 0;
}
/**************************************************************************************************************************/
static int CommEvSSHClientOpenSession(CommEvSSHClient *ev_sshclient)
{
	int op_status;
	int dir_code;

	/* Already opened, bail out */
	if (ev_sshclient->flags.session_opened)
	{
		//printf("CommEvSSHClientOpenSession - Already OPEN, bail out\n");
		return 1;
	}

	/* Open session */
	ev_sshclient->ssh.channel	= libssh2_channel_open_session(ev_sshclient->ssh.session);
	op_status					= libssh2_session_last_error(ev_sshclient->ssh.session, NULL, NULL, 0);
	dir_code					= CommEvSSHClientGetDirection(ev_sshclient, op_status);

	/* Channel OPENED OK */
	if (ev_sshclient->ssh.channel)
	{
		ev_sshclient->flags.session_opened = 1;
		return 1;
	}
	/* Failed opening CHANNEL, PROCESS SSH2 errors */
	else
	{
		switch(dir_code)
		{
		case COMM_SSHCLIENT_DIRECTION_EAGAIN:
		{
			//printf("CommEvSSHClientOpenSession - Opening...\n");
			return -1;
		}
		case COMM_SSHCLIENT_DIRECTION_FAIL:
		{
			//printf("CommEvSSHClientOpenSession - FAILED opening session\n");
			return 0;

		}
		case COMM_SSHCLIENT_DIRECTION_FINISH:
		{
			//printf("CommEvSSHClientOpenSession - FINISH opening session\n");
			ev_sshclient->flags.session_opened = 1;
			return 1;
		}
		}
	}

	return 0;
}
/**************************************************************************************************************************/
static int CommEvSSHClientEventSSHNegotiate(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSSHClient *ev_sshclient	= cb_data;
	int op_status;

	/* Handshake */
	op_status = CommEvSSHClientHandshake(ev_sshclient);

	/* Do the SSH handshake if not done yet */
	if (!op_status)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "FAILED [%d]\n", op_status);

		/* Set client error state */
		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SECURE_TUNNEL;
		goto conn_failed;
	}
	/* Negotiating */
	else if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "IN PROGRESS [%d]\n", op_status);

		/* Set client state */
		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SECURE_TUNNEL;
		goto conn_in_progress;
	}

	/* Process known hosts - Use flag */
	if ( ev_sshclient->flags.check_known_hosts && (!CommEvSSHClientKnownHosts(ev_sshclient)))
	{
		/* Set client error state */
		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SECURE_TUNNEL;
		goto conn_failed;
	}

	/* Authorize user */
	op_status = CommEvSSHClientAuthUser(ev_sshclient);

	if (!op_status)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "FAILED\n");

		/* Set client error state */
		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_AUTHENTICATING_SECURE_TUNNEL;
		goto conn_failed;
	}
	/* Negotiating */
	else if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "IN PROGRESS\n");

		/* Set client state */
		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SECURE_TUNNEL;
		goto conn_in_progress;
	}

	/* Authorize user */
	op_status = CommEvSSHClientOpenSession(ev_sshclient);

	/* Open session */
	if (!op_status)
	{
		/* Set client error state */
		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SECURE_TUNNEL;
		goto conn_failed;

	}
	/* Negotiating */
	else if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "IN PROGRESS\n");

		/* Set client state */
		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SECURE_TUNNEL;
		goto conn_in_progress;
	}

	//printf("CommEvSSHClientEventSSHNegotiate - DONE\n");

	/* OK, client is ONLINE */
	ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECTED;

	/* Set interactive shell session */
	libssh2_channel_shell(ev_sshclient->ssh.channel);

	/* Schedule IO events */
	EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventRead, ev_sshclient);
	EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventEof, ev_sshclient);

	/* Dispatch the internal event */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

	return 1;

	/* Label to notify progress, reschedule TIMEOUT and leave */
	conn_in_progress:

	/* Reschedule CONNECT TIMEOUT, if any */
	if (ev_sshclient->timeout.connect_ms > 0)
		EvKQBaseTimeoutSet(ev_sshclient->kq_base, ev_sshclient->socket_fd, KQ_CB_TIMEOUT_WRITE, ev_sshclient->timeout.connect_ms, CommEvSSHClientTimerConnectTimeout, ev_sshclient);

	/* Notify in-progress event */
	if (ev_sshclient->flags.notify_conn_progress)
		CommEvSSHClientEventDispatchInternal(ev_sshclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

	return 0;

	/* Label to notify failed connection */
	conn_failed:

	//printf("CommEvSSHClientEventSSHNegotiate - FAILED\n");

	/* Failed opening session, dispatch connect event with error */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

	/* Invoke disconnect */
	CommEvSSHClientInternalDisconnect(ev_sshclient);

	/* Upper layers want a reconnect retry if CONNECTION FAILS */
	if (!ev_sshclient->flags.reconnect_on_fail)
		return 0;

	/* Will close socket and cancel any pending events of ev_sshclient->socket_fd, including the close event */
	if (ev_sshclient->socket_fd > 0)
	{
		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLOSING\n", ev_sshclient->socket_fd);
		EvKQBaseSocketClose(ev_sshclient->kq_base, ev_sshclient->socket_fd);
		ev_sshclient->socket_fd = -1;
	}

	/* Destroy read and write buffers */
	CommEvSSHClientDestroyConnReadAndWriteBuffers(ev_sshclient);

	ev_sshclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;
	ev_sshclient->socket_fd		= -1;

	/* Schedule RECONNECT timer */
	ev_sshclient->timers.reconnect_after_timeout_id = EvKQBaseTimerAdd(ev_sshclient->kq_base, COMM_ACTION_ADD_VOLATILE,
			((ev_sshclient->retry_times.reconnect_after_timeout_ms > 0) ? ev_sshclient->retry_times.reconnect_after_timeout_ms : COMM_SSH_CLIENT_RECONNECT_TIMEOUT_DEFAULT_MS),
			CommEvSSHClientTimerReconnect, ev_sshclient);

	return 0;
}
/**************************************************************************************************************************/
static int CommEvSSHClientGetDirection(CommEvSSHClient *ev_sshclient, int res)
{
	int direction;

	switch(res)
	{
	/* FATAL errors */
	case LIBSSH2_ERROR_SOCKET_NONE: 		// The socket is invalid
	case LIBSSH2_ERROR_BANNER_SEND: 		// Unable to send banner to remote host.
	case LIBSSH2_ERROR_KEX_FAILURE: 		// Encryption key exchange with the remote host failed.
	case LIBSSH2_ERROR_SOCKET_SEND: 		// Unable to send data on socket.
	case LIBSSH2_ERROR_SOCKET_DISCONNECT: 	// The socket was disconnected.
	case LIBSSH2_ERROR_PROTO: 				// An invalid SSH protocol response was received on the socket.
	{
		ev_sshclient->ssh.last_err.code = libssh2_session_last_errno(ev_sshclient->ssh.session);
		libssh2_session_last_error(ev_sshclient->ssh.session, &ev_sshclient->ssh.last_err.str, &ev_sshclient->ssh.last_err.str_sz, 0);

		KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_ORANGE, "FATAL LIBSSH2 ERROR [%d]/[%d] - [%s]\n", res, ev_sshclient->ssh.last_err.code, ev_sshclient->ssh.last_err.str);

		return COMM_SSHCLIENT_DIRECTION_FAIL;
	}
	/* Marked for non-blocking I/O but the call would block */
	case LIBSSH2_ERROR_EAGAIN:
	{
		//printf("CommEvSSHClientGetDirection - Marked for non-blocking I/O but the call would block.\n");

		/* Now make sure we wait in the correct direction */
		direction = libssh2_session_block_directions(ev_sshclient->ssh.session);

		if (direction & LIBSSH2_SESSION_BLOCK_INBOUND)
			EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventSSHNegotiate, ev_sshclient);

		if (direction & LIBSSH2_SESSION_BLOCK_OUTBOUND)
			EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventSSHNegotiate, ev_sshclient);

		return COMM_SSHCLIENT_DIRECTION_EAGAIN;
	}
	}

	return COMM_SSHCLIENT_DIRECTION_FINISH;
}
/**************************************************************************************************************************/
static void CommEvSSHClientEventDispatchInternal(CommEvSSHClient *ev_sshclient, int data_sz, int thrd_id, int ev_type)
{
	CommEvTCPClientCBH *cb_handler		= NULL;
	void *cb_handler_data				= NULL;

	/* Grab callback_ptr */
	cb_handler = ev_sshclient->events[ev_type].cb_handler_ptr;

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Grab data for this CBH */
		cb_handler_data = ev_sshclient->events[ev_type].cb_data_ptr;

		/* Jump into CBH * */
		cb_handler(ev_sshclient->socket_fd, data_sz, thrd_id, cb_handler_data, ev_sshclient);
	}

	/* Hook on events of our interest, such as CONNECT (for timed out reconnection) and EOF (for automatic reconnection) */
	switch (ev_type)
	{
	case COMM_CLIENT_EVENT_CONNECT:
	{
		break;
	}
	case COMM_CLIENT_EVENT_CLOSE:
	{
		break;
	}
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvSSHClientDestroyConnReadAndWriteBuffers(CommEvSSHClient *ev_sshclient)
{
	/* Destroy any pending write event */
	EvAIOReqQueueClean(&ev_sshclient->write_queue);

	/* Destroy any data buffered on read_stream */
	MemBufferDestroy(ev_sshclient->read_buffer);

	ev_sshclient->read_buffer = NULL;

	return;

}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvSSHClientTimerReconnect(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSSHClient *ev_sshclient = cb_data;

	KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - TIMER_ID [%d]\n", ev_sshclient->socket_fd, timer_id);

	/* Disable timer ID on CLIENT */
	if (timer_id == ev_sshclient->timers.reconnect_after_close_id)
		ev_sshclient->timers.reconnect_after_close_id = -1;
	else if (timer_id == ev_sshclient->timers.reconnect_after_timeout_id)
		ev_sshclient->timers.reconnect_after_timeout_id = -1;
	else if (timer_id == ev_sshclient->timers.reconnect_on_fail_id)
		ev_sshclient->timers.reconnect_on_fail_id = -1;

	/* Begin reconnection procedure */
	CommEvSSHClientReconnect(ev_sshclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvSSHClientTimerConnectTimeout(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSSHClient *ev_sshclient = cb_data;

	/* Set client state */
	ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_TIMEOUT;

	/* Cancel and close any pending event associated with SSH client FD, to avoid double notification */
	EvKQBaseClearEvents(ev_sshclient->kq_base, ev_sshclient->socket_fd);
	EvKQBaseTimeoutClearAll(ev_sshclient->kq_base, ev_sshclient->socket_fd);

	/* Dispatch internal event */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

	/* Disconnect any pending stuff - Will schedule reconnect if upper layers asked for it */
	CommEvSSHClientInternalDisconnect(ev_sshclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvSSHClientEventConnect(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSSHClient *ev_sshclient = cb_data;

	/* Query the kernel for the current socket state */
	ev_sshclient->socket_state = CommEvUtilsFDCheckState(fd);

	/* Connection not yet completed, reschedule and return */
	if (COMM_CLIENT_STATE_CONNECTING == ev_sshclient->socket_state)
	{
		EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventConnect, ev_sshclient);
		return 0;
	}
	/* If connected OK, begin SSH handshake */
	else if (COMM_CLIENT_STATE_CONNECTED == ev_sshclient->socket_state)
	{
		/* Initialize WRITE_QUEUE if not already initialized*/
		EvAIOReqQueueInit(ev_sshclient->kq_base, &ev_sshclient->write_queue, 4096,
				(ev_sshclient->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

		/* Set client state */
		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SECURE_TUNNEL;

		/* Create a session instance and  tell libssh2 we want it all done non-blocking */
		if (!ev_sshclient->ssh.session)
		{
			ev_sshclient->ssh.session = libssh2_session_init();

			if (!ev_sshclient->ssh.session)
			{
				ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SECURE_TUNNEL;

				/* Failed opening session, dispatch connect event with error */
				CommEvSSHClientEventDispatchInternal(ev_sshclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

				/* Invoke disconnect */
				CommEvSSHClientInternalDisconnect(ev_sshclient);

				return 0;
			}

			libssh2_session_set_blocking(ev_sshclient->ssh.session, 0);
		}

		/* Begin SSH negotiation handshake */
		CommEvSSHClientEventSSHNegotiate(fd, data_sz, thrd_id, ev_sshclient, base_ptr);

		return 1;
	}
	/* For some reason client failed connecting, notify upper layers */
	else
	{
		//printf("CommEvSSHClientEventConnect - FAILED\n");

		ev_sshclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_UNKNWON;

		/* Failed opening session, dispatch connect event with error */
		CommEvSSHClientEventDispatchInternal(ev_sshclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

		/* Invoke disconnect */
		CommEvSSHClientInternalDisconnect(ev_sshclient);
	}

	return 0;
}
/**************************************************************************************************************************/
static int CommEvSSHClientEventRead(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvSSHClient *ev_sshclient	= cb_data;
	char read_buf[8092];
	int bytes_ssh_read;

	int total_ssh_read = 0;
	int last_error = 0;
	int i = 0;

	/* Read from SSH tunnel */
	do
	{
		bytes_ssh_read = libssh2_channel_read( ev_sshclient->ssh.channel, (char*)&read_buf, sizeof(read_buf));

		//printf("CommEvSSHClientEventRead - [%d] - SSH_READ [%d] - TCP_READ_SZ [%d]\n", i++, bytes_ssh_read, read_sz);

		/* Error reading - Check errors */
		if (bytes_ssh_read < 0)
		{
			last_error = bytes_ssh_read;

			if (LIBSSH2_ERROR_EAGAIN != bytes_ssh_read)
				goto channel_close;
		}
		/* Nothing to read, bail out */
		else if (0 == bytes_ssh_read)
			break;

		/* Create a new read_stream object */
		if (!ev_sshclient->read_buffer)
			ev_sshclient->read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (read_sz + 1));

		/* Add SSH buffer into plain read_buffer */
		if (bytes_ssh_read > 0)
		{
			MemBufferAdd(ev_sshclient->read_buffer, &read_buf, bytes_ssh_read);
			total_ssh_read += bytes_ssh_read;
		}
	}

	while (bytes_ssh_read > 0);

	//printf("CommEvSSHClientEventRead - TOTAL_SSH_READ [%d] - TCP_READ_SZ [%d] - LAST_ERROR [%d]\n", total_ssh_read, read_sz, last_error);

	/* If TCP has data on wire, and SSH read zero bytes, and last error is not EAGAIN, then channel is closed */
	if ( (read_sz > 0) && (0 == total_ssh_read) && ((0 == last_error) || (LIBSSH2_ERROR_EAGAIN != last_error)) )
		goto channel_close;

	/* Dispatch internal event */
	if (total_ssh_read > 0)
		CommEvSSHClientEventDispatchInternal(ev_sshclient, total_ssh_read, thrd_id, COMM_CLIENT_EVENT_READ);

	/* Reschedule read event - Upper layers could have closed this socket, so just RESCHEDULE READ if we are still ONLINE */
	if (ev_sshclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
		EvKQBaseSetEvent(ev_sshclient->kq_base, ev_sshclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventRead, ev_sshclient);

	return read_sz;

	/* Label to deal with closed channels */
	channel_close:

	/* Dispatch internal event */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, 0, thrd_id, COMM_CLIENT_EVENT_CLOSE);

	/* Do the internal disconnect and destroy IO buffers */
	CommEvSSHClientInternalDisconnect(ev_sshclient);

	return 0;

}
/**************************************************************************************************************************/
static int CommEvSSHClientEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvSSHClient *ev_sshclient	= cb_data;
	EvAIOReq *cur_aio_req;
	int ssh_bytes_written;
	int direction;

	/* Grab aio_req unit */
	cur_aio_req	= EvAIOReqQueueDequeue(&ev_sshclient->write_queue);

	if (!cur_aio_req)
		return 0;

	/* Write it to SSH tunnel */
	ssh_bytes_written = libssh2_channel_write(ev_sshclient->ssh.channel, cur_aio_req->data.ptr, cur_aio_req->data.size);

	//	printf("CommEvSSHClientGetDirection - [%d] written [%d] want to write [%d] can write\n", bytes_written, cur_aio_req->data_sz, read_sz);

	/* Error reading - Check errors */
	if (ssh_bytes_written < 0)
	{
		/* Re_enqueue for writing */
		if (LIBSSH2_ERROR_EAGAIN == ssh_bytes_written)
		{
			CommEvSSHClientEnqueueHeadAndKickWriteQueue(ev_sshclient, cur_aio_req);
			return 0;
		}
		else
			goto channel_close;
	}

	/* Invoke notification CALLBACKS */
	EvAIOReqInvokeCallBacksAndDestroy(cur_aio_req, 1, fd, ssh_bytes_written, thrd_id, ev_sshclient);
	return 1;

	/* Label to deal with closed channels */
	channel_close:

	/* Dispatch internal event */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, 0, thrd_id, COMM_CLIENT_EVENT_CLOSE);

	/* Do the internal disconnect and destroy IO buffers */
	CommEvSSHClientInternalDisconnect(ev_sshclient);

	return 0;
}
/**************************************************************************************************************************/
static int CommEvSSHClientEventEof(int fd, int buf_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSSHClient *ev_sshclient = cb_data;

	/* Do not close for now, there is data pending read */
	if (buf_read_sz > 0)
	{
		EvKQBaseSetEvent(ev_sshclient->kq_base, fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvSSHClientEventEof, ev_sshclient);
		return 0;
	}

	/* Dispatch internal event */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, buf_read_sz, thrd_id, COMM_CLIENT_EVENT_CLOSE);

	/* Do the internal disconnect and destroy IO buffers */
	CommEvSSHClientInternalDisconnect(ev_sshclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvSSHClientCheckIfNeedDNSLookup(CommEvSSHClient *ev_sshclient)
{
	int dots_found;
	int state;
	int i;

	enum
	{
		HOSTNAME_IS_IP,
		HOSTNAME_IS_FQDN,
	};

	/* Check if request URL is FQDN or IP */
	for (i = 0, dots_found = 0, state = HOSTNAME_IS_IP; ev_sshclient->cfg.hostname[i] != '\0'; i++)
	{
		/* Seen a dot on domain */
		if ('.' == ev_sshclient->cfg.hostname[i] )
			dots_found++;

		/* An IP can not have more than three dot chars, set as FQDN and stop */
		if (dots_found > 3)
		{
			state = HOSTNAME_IS_FQDN;
			break;
		}

		/* Not a dot, not a number, not a port char, set to FQDN and bail out */
		if ((!isdigit(ev_sshclient->cfg.hostname[i])) && (ev_sshclient->cfg.hostname[i] != '.') && (ev_sshclient->cfg.hostname[i] != ':'))
		{
			state = HOSTNAME_IS_FQDN;
			break;
		}

	}

	/* Set flags and return */
	if  (HOSTNAME_IS_FQDN == state)
	{
		//printf("CommEvSSHClientCheckIfNeedDNSLookup - HOSTNAME_IS_FQDN\n");
		ev_sshclient->flags.need_dns_lookup = 1;
		return 1;
	}
	else
	{
		//printf("CommEvSSHClientCheckIfNeedDNSLookup - HOSTNAME_IS_IP\n");
		ev_sshclient->flags.need_dns_lookup = 0;
		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
static void CommEvSSHClientDNSResolverCB(void *ev_dns_ptr, void *req_cb_data, void *a_reply_ptr, int code)
{
	int i;
	int lower_seen_ttl				= 0;
	DNSAReply *a_reply				= a_reply_ptr;
	CommEvSSHClient *ev_sshclient	= req_cb_data;

	/* Resolve succeeded OK, begin ASYNC connection procedure */
	if (a_reply->ip_count > 0)
	{
		//printf("CommEvSSHClientDNSResolverCB - Host [%s] - IP resolved - CODE [%d] - [%d] ADDRESS\n", ev_sshclient->cfg.hostname, code, a_reply->ip_count);

		for (i = 0; i < a_reply->ip_count; i++)
		{
			//printf("CommEvSSHClientDNSResolverCB - Index [%d] - IP_ADDR [%s] - TTL [%d]\n", i, inet_ntoa(a_reply->ip_arr[i].addr), a_reply->ip_arr[i].ttl);

			/* First iteration, initialize values */
			if (0 == i)
				lower_seen_ttl = a_reply->ip_arr[i].ttl;
			/* There is a smaller TTL, keep it */
			else if (a_reply->ip_arr[i].ttl < lower_seen_ttl)
				lower_seen_ttl = a_reply->ip_arr[i].ttl;
		}

		/* Save DNS reply */
		memcpy(&ev_sshclient->dns.a_reply, a_reply, sizeof(DNSAReply));

		/* Set expire based on lower TTL seen on array */
		ev_sshclient->dns.expire_ts = ev_sshclient->kq_base->stats.cur_invoke_ts_sec + lower_seen_ttl;
		ev_sshclient->dns.cur_idx	= 0;

		/* Load destination IP based on A reply */
		CommEvSSHClientAddrInitFromAReply(ev_sshclient);

		/* Begin ASYNC connection */
		CommEvSSHAsyncConnect(ev_sshclient);

		return;
	}

	/* Resolve failed, notify upper layers */

	//printf("CommEvSSHClientDNSResolverCB - FAILED [%d]\n", a_reply->ip_count);

	/* Set flags */
	ev_sshclient->socket_state	= COMM_CLIENT_STATE_CONNECT_FAILED_DNS;
	ev_sshclient->dns.expire_ts = -1;

	/* Dispatch internal event */
	CommEvSSHClientEventDispatchInternal(ev_sshclient, 0, -1, COMM_CLIENT_EVENT_CONNECT);

	/* Upper layers want a reconnect retry if CONNECTION FAILS */
	if (ev_sshclient->flags.reconnect_on_fail)
	{
		/* Will close socket and cancel any pending events of ev_sshclient->socket_fd, including the close event */
		if (ev_sshclient->socket_fd > 0)
		{
			KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLOSING\n", ev_sshclient->socket_fd);
			EvKQBaseSocketClose(ev_sshclient->kq_base, ev_sshclient->socket_fd);
			ev_sshclient->socket_fd = -1;
		}

		/* Destroy read and write buffers */
		CommEvSSHClientDestroyConnReadAndWriteBuffers(ev_sshclient);

		ev_sshclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;
		ev_sshclient->socket_fd		= -1;

		/* Schedule RECONNECT timer */
		ev_sshclient->timers.reconnect_on_fail_id = EvKQBaseTimerAdd(ev_sshclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_sshclient->retry_times.reconnect_on_fail_ms > 0) ? ev_sshclient->retry_times.reconnect_on_fail_ms : COMM_SSH_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
				CommEvSSHClientReconnectTimer, ev_sshclient);
	}

	return;
}
/**************************************************************************************************************************/
static int CommEvSSHClientTimersCancelAll(CommEvSSHClient *ev_sshclient)
{
	KQBASE_LOG_PRINTF(ev_sshclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Will cancel timers - CALCULATE_ID [%d]\n",
			ev_sshclient->socket_fd, ev_sshclient->timers.calculate_datarate_id);

	/* DELETE all pending timers */
	if (ev_sshclient->timers.reconnect_after_close_id > -1)
		EvKQBaseTimerCtl(ev_sshclient->kq_base, ev_sshclient->timers.reconnect_after_close_id, COMM_ACTION_DELETE);

	if (ev_sshclient->timers.reconnect_after_timeout_id > -1)
		EvKQBaseTimerCtl(ev_sshclient->kq_base, ev_sshclient->timers.reconnect_after_timeout_id, COMM_ACTION_DELETE);

	if (ev_sshclient->timers.reconnect_on_fail_id > -1)
		EvKQBaseTimerCtl(ev_sshclient->kq_base, ev_sshclient->timers.reconnect_on_fail_id, COMM_ACTION_DELETE);

	if (ev_sshclient->timers.calculate_datarate_id > -1)
		EvKQBaseTimerCtl(ev_sshclient->kq_base, ev_sshclient->timers.calculate_datarate_id, COMM_ACTION_DELETE);

	ev_sshclient->timers.reconnect_after_close_id	= -1;
	ev_sshclient->timers.reconnect_after_timeout_id	= -1;
	ev_sshclient->timers.reconnect_on_fail_id		= -1;
	ev_sshclient->timers.calculate_datarate_id		= -1;

	return 1;
}
/**************************************************************************************************************************/
















