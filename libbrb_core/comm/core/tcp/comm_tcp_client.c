/*
 * comm_tcp_client.c
 *
 *  Created on: 2012-01-11
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

#include "../include/libbrb_core.h"

/* AIO events */
static EvBaseKQCBH CommEvTCPClientEventConnect;
static EvBaseKQCBH CommEvTCPClientEventEof;
static EvBaseKQCBH CommEvTCPClientEventRead;
static EvBaseKQCBH CommEvTCPClientEventWrite;

/* Timer events */
static EvBaseKQCBH CommEvTCPClientRatesCalculateTimer;
static EvBaseKQCBH CommEvTCPClientReconnectTimer;
static EvBaseKQCBH CommEvTCPClientTimerConnectTimeout;

/* SSL support */
static EvBaseKQCBH CommEvTCPClientSSLShutdown;
static EvBaseKQCBH CommEvTCPClientEventSSLRead;
static EvBaseKQCBH CommEvTCPClientEventSSLWrite;

/* DNS support */
static CommEvDNSResolverCBH CommEvTCPClientDNSResolverCB;

static void CommEvTCPClientDestroyConnReadAndWriteBuffers(CommEvTCPClient *ev_tcpclient);
static void CommEvTCPClientAddrInitFromAReply(CommEvTCPClient *ev_tcpclient);
static void CommEvTCPClientEnqueueAndKickWriteQueue(CommEvTCPClient *ev_tcpclient, EvAIOReq *aio_req);
static void CommEvTCPClientEventDispatchInternal(CommEvTCPClient *ev_tcpclient, int data_sz, int thrd_id, int ev_type);
static int CommEvTCPClientAsyncConnect(CommEvTCPClient *ev_tcpclient);
static void CommEvTCPClientInternalDisconnect(CommEvTCPClient *ev_tcpclient);
static int CommEvTCPClientCheckIfNeedDNSLookup(CommEvTCPClient *ev_tcpclient);
static int CommEvTCPClientProcessBuffer(CommEvTCPClient *ev_tcpclient, int read_sz, int thrd_id, char *read_buf, int read_buf_sz);
static int CommEvTCPClientSelfSyncReadBuffer(CommEvTCPClient *ev_tcpclient, int orig_read_sz, int thrd_id);
static int CommEvTCPClientTimersCancelAll(CommEvTCPClient *ev_tcpclient);
static EvBaseKQObjDestroyCBH CommEvTCPClientObjectDestroyCBH;
static EvBaseKQJobCBH CommEvTCPClientSSLShutdownJob;

/**************************************************************************************************************************/
CommEvTCPClient *CommEvTCPClientNew(EvKQBase *kq_base)
{
	CommEvTCPClient *ev_tcpclient;
	int op_status;

	ev_tcpclient	= calloc(1, sizeof(CommEvTCPClient));
	op_status		= CommEvTCPClientInit(kq_base, ev_tcpclient, -1);

	/* Error initializing client, bail out */
	if (COMM_CLIENT_INIT_OK != op_status)
	{
		free(ev_tcpclient);
		return NULL;
	}

	return ev_tcpclient;
}
/**************************************************************************************************************************/
CommEvTCPClient *CommEvTCPClientNewUNIX(EvKQBase *kq_base)
{
	CommEvTCPClient *ev_tcpclient;
	int op_status;

	ev_tcpclient							= calloc(1, sizeof(CommEvTCPClient));
	ev_tcpclient->cfg.flags.unix_socket 	= 1;
	op_status								= CommEvTCPClientInit(kq_base, ev_tcpclient, -1);

	/* Error initializing client, bail out */
	if (COMM_CLIENT_INIT_OK != op_status)
	{
		free(ev_tcpclient);
		return NULL;
	}

	return ev_tcpclient;
}
/**************************************************************************************************************************/
int CommEvTCPClientInit(EvKQBase *kq_base, CommEvTCPClient *ev_tcpclient, int cli_id_onpool)
{
	int socket_fd;

	/* Create socket and set it to non_blocking */
	if (ev_tcpclient->cfg.flags.unix_socket)
		socket_fd = EvKQBaseSocketUNIXNew(kq_base);
	else
		socket_fd = EvKQBaseSocketTCPNew(kq_base);

	/* Check if created socket is OK */
	if (socket_fd < 0)
		return COMM_CLIENT_FAILURE_SOCKET;

	ev_tcpclient->kq_base						= kq_base;
	ev_tcpclient->socket_fd						= socket_fd;
	ev_tcpclient->cli_id_onpool					= cli_id_onpool;

	/* Populate KQ_BASE object structure */
	ev_tcpclient->kq_obj.code					= (ev_tcpclient->cfg.flags.unix_socket) ? EV_OBJ_UNIX_CLIENT : EV_OBJ_TCP_CLIENT;
	ev_tcpclient->kq_obj.obj.ptr				= ev_tcpclient;
	ev_tcpclient->kq_obj.obj.destroy_cbh		= CommEvTCPClientObjectDestroyCBH;
	ev_tcpclient->kq_obj.obj.destroy_cbdata		= NULL;

	/* Register KQ_OBJECT */
	EvKQBaseObjectRegister(kq_base, &ev_tcpclient->kq_obj);

	/* Set default READ METHOD and READ PROTO, can be override by CONNECT function */
	ev_tcpclient->read_mthd 					= COMM_CLIENT_READ_MEMBUFFER;
	ev_tcpclient->cli_proto						= COMM_CLIENTPROTO_PLAIN;
	ev_tcpclient->socket_state					= COMM_CLIENT_STATE_DISCONNECTED;

	ev_tcpclient->dnsreq_id						= -1;
	ev_tcpclient->ssldata.shutdown_jobid		= -1;

	/* Initialize all timers */
	ev_tcpclient->timers.reconnect_after_close_id	= -1;
	ev_tcpclient->timers.reconnect_after_timeout_id	= -1;
	ev_tcpclient->timers.reconnect_on_fail_id		= -1;
	ev_tcpclient->timers.calculate_datarate_id		= -1;

	/* Set it to non_blocking and save it into newly allocated client */
	EvKQBaseSocketSetNonBlock(kq_base, ev_tcpclient->socket_fd);

	/* Set description */
	EvKQBaseFDDescriptionSetByFD(kq_base, ev_tcpclient->socket_fd, "BRB_EV_COMM - TCP_CLIENT");

	/* Initialize WRITE_QUEUE */
	EvAIOReqQueueInit(ev_tcpclient->kq_base, &ev_tcpclient->iodata.write_queue, 4096,
			(ev_tcpclient->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

	return COMM_CLIENT_INIT_OK;
}
/**************************************************************************************************************************/
void CommEvTCPClientDestroy(CommEvTCPClient *ev_tcpclient)
{
	/* Sanity check */
	if (!ev_tcpclient)
		return;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Destroying at [%p]\n", ev_tcpclient->socket_fd, ev_tcpclient);

	/* Clean up structure */
	CommEvTCPClientClean(ev_tcpclient);

	/* Free WILLY */
	free(ev_tcpclient);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPClientClean(CommEvTCPClient *ev_tcpclient)
{
	/* Sanity check */
	if (!ev_tcpclient)
		return;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Cleaning at [%p]\n", ev_tcpclient->socket_fd, ev_tcpclient);

	/* Unregister object */
	EvKQBaseObjectUnregister(&ev_tcpclient->kq_obj);

	/* Release SSL related objects */
	if (COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto)
	{
		if (ev_tcpclient->ssldata.ssl_handle)
		{
			SSL_shutdown (ev_tcpclient->ssldata.ssl_handle);
			SSL_free (ev_tcpclient->ssldata.ssl_handle);
			ev_tcpclient->ssldata.ssl_handle = NULL;
		}
		if (ev_tcpclient->ssldata.ssl_context)
		{
			SSL_CTX_free (ev_tcpclient->ssldata.ssl_context);
			ev_tcpclient->ssldata.ssl_context = NULL;
		}

		if (ev_tcpclient->ssldata.x509_cert)
		{
			X509_free(ev_tcpclient->ssldata.x509_cert);
			ev_tcpclient->ssldata.x509_cert = NULL;
		}
	}

	/* Cancel pending DNS request */
	if (ev_tcpclient->dnsreq_id	> -1)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] -  Will cancel pending DNS_REQ_ID [%d]\n", ev_tcpclient->socket_fd, ev_tcpclient->dnsreq_id);

		/* Cancel pending DNS request and set it to -1 */
		CommEvDNSCancelPendingRequest(ev_tcpclient->resolv_base, ev_tcpclient->dnsreq_id);
		ev_tcpclient->dnsreq_id = -1;
	}

	/* Cancel any pending SSL SHUTDOWN JOB */
	EvKQJobsCtl(ev_tcpclient->kq_base, JOB_ACTION_DELETE, ev_tcpclient->ssldata.shutdown_jobid);
	ev_tcpclient->ssldata.shutdown_jobid = -1;

	/* Cancel any possible timer or events */
	CommEvTCPClientTimersCancelAll(ev_tcpclient);
	CommEvTCPClientEventCancelAll(ev_tcpclient);

	/* Destroy read and write buffers */
	CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

	/* Close the socket and cancel any pending events */
	EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
	ev_tcpclient->socket_fd = -1;

	return;
}
/**************************************************************************************************************************/
int CommEvTCPClientConnect(CommEvTCPClient *ev_tcpclient, CommEvTCPClientConf *ev_tcpclient_conf)
{
	int op_status		= 0;

	/* Sanity check */
	if (!ev_tcpclient)
		return 0;

	/* Sanity check */
	if (!ev_tcpclient_conf->hostname)
		return 0;

	/* Save a copy of HOSTNAME and PORT */
	strncpy((char*)&ev_tcpclient->cfg.hostname, ev_tcpclient_conf->hostname, sizeof(ev_tcpclient->cfg.hostname));

	/* Clean up flags in case we are reusing this client context */
	memset(&ev_tcpclient->flags, 0, sizeof(ev_tcpclient->flags));

	/* Load client protocol and read method information */
	ev_tcpclient->cli_proto 	= ev_tcpclient_conf->cli_proto;
	ev_tcpclient->read_mthd 	= ev_tcpclient_conf->read_mthd;

	/* Copy flags and info */
	ev_tcpclient->cfg.port 									= ev_tcpclient_conf->port;

	/* Load resolver base */
	ev_tcpclient->resolv_base								= ev_tcpclient_conf->resolv_base;
	ev_tcpclient->log_base									= ev_tcpclient_conf->log_base;

	/* Load flags */
	ev_tcpclient->flags.reconnect_on_timeout				= ev_tcpclient_conf->flags.reconnect_on_timeout;
	ev_tcpclient->flags.reconnect_on_close					= ev_tcpclient_conf->flags.reconnect_on_close;
	ev_tcpclient->flags.reconnect_on_fail					= ev_tcpclient_conf->flags.reconnect_on_fail;
	ev_tcpclient->flags.reconnect_new_dnslookup				= ev_tcpclient_conf->flags.reconnect_new_dnslookup;
	ev_tcpclient->flags.reconnect_balance_on_ips			= ev_tcpclient_conf->flags.reconnect_balance_on_ips;
	ev_tcpclient->flags.destroy_after_connect_fail			= ev_tcpclient_conf->flags.destroy_after_connect_fail;
	ev_tcpclient->flags.destroy_after_close					= ev_tcpclient_conf->flags.destroy_after_close;
	ev_tcpclient->flags.calculate_datarate					= ev_tcpclient_conf->flags.calculate_datarate;
	ev_tcpclient->flags.ssl_null_cypher						= ev_tcpclient_conf->flags.ssl_null_cypher;
	ev_tcpclient->flags.bindany_active						= ev_tcpclient_conf->flags.bindany_active;

	/* Load timeout information */
	ev_tcpclient->timeout.connect_ms						= ev_tcpclient_conf->timeout.connect_ms;
	ev_tcpclient->timeout.read_ms							= ev_tcpclient_conf->timeout.read_ms;
	ev_tcpclient->timeout.write_ms							= ev_tcpclient_conf->timeout.write_ms;

	/* Load retry timers information */
	ev_tcpclient->retry_times.reconnect_after_timeout_ms	= ev_tcpclient_conf->retry_times.reconnect_after_timeout_ms;
	ev_tcpclient->retry_times.reconnect_after_close_ms		= ev_tcpclient_conf->retry_times.reconnect_after_close_ms;
	ev_tcpclient->retry_times.reconnect_on_fail_ms			= ev_tcpclient_conf->retry_times.reconnect_on_fail_ms;

	/* Check now if user sent us a HOSTNAME and we have no resolver base defined - Will set flags.need_dns_lookup */
	if (CommEvTCPClientCheckIfNeedDNSLookup(ev_tcpclient) && (!ev_tcpclient_conf->resolv_base))
		return 0;

	/* If there is no socket, create one */
	if (ev_tcpclient->socket_fd < 0)
	{
		/* Create socket and set it to non_blocking */
		if (ev_tcpclient->cfg.flags.unix_socket)
			ev_tcpclient->socket_fd = EvKQBaseSocketUNIXNew(ev_tcpclient->kq_base);
		else
			ev_tcpclient->socket_fd = EvKQBaseSocketTCPNew(ev_tcpclient->kq_base);

		EvKQBaseSocketSetNonBlock(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
	}

	assert(ev_tcpclient->socket_fd >= 0);

	/* Create a new SSL client context if there is NONE */
	if (COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto)
	{
		if (!ev_tcpclient->ssldata.ssl_context)
			ev_tcpclient->ssldata.ssl_context	= SSL_CTX_new(SSLv23_client_method());

		/* Failed creating SSL context */
		if (!ev_tcpclient->ssldata.ssl_context)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed creating SSL_CONTEXT\n", ev_tcpclient->socket_fd);
			return 0;
		}

		/* NULL CYPHER asked */
		if (ev_tcpclient->flags.ssl_null_cypher)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Using NULL CYPHER SSL\n", ev_tcpclient->socket_fd);

			if (!SSL_CTX_set_cipher_list(ev_tcpclient->ssldata.ssl_context, "aNULL"))
			{
				KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed setting NULL CIPHER\n", ev_tcpclient->socket_fd);
				return 0;
			}
		}

		if (!ev_tcpclient->ssldata.ssl_handle)
			ev_tcpclient->ssldata.ssl_handle	= SSL_new(ev_tcpclient->ssldata.ssl_context);

		/* Failed creating SSL HANDLE */
		if (!ev_tcpclient->ssldata.ssl_handle)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed creating SSL_HANDLE\n", ev_tcpclient->socket_fd);
			return 0;
		}

		/* Load TLS/SNI extension */
		if (ev_tcpclient_conf->sni_hostname_str)
		{
			/* Save a copy of HOSTNAME for TLS Server Name Indication */
			strncpy((char*)&ev_tcpclient->cfg.sni_hostname, ev_tcpclient_conf->sni_hostname_str, sizeof(ev_tcpclient->cfg.sni_hostname));
			SSL_set_tlsext_host_name(ev_tcpclient->ssldata.ssl_handle, ev_tcpclient_conf->sni_hostname_str);
		}

		/* Attach to SOCKET_FD */
		SSL_set_fd(ev_tcpclient->ssldata.ssl_handle, ev_tcpclient->socket_fd);
		ev_tcpclient->ssldata.ssl_negotiatie_trycount	= 0;
		ev_tcpclient->flags.ssl_enabled					= 1;
	}

	/* Set description */
	EvKQBaseFDDescriptionSetByFD(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, "BRB_EV_COMM - TCP_CLIENT - [%s:%d] - SSL [%d]",
			ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, ev_tcpclient->flags.ssl_enabled);

	/* Check if self sync is active, then grab token to check when buffer finish */
	if (ev_tcpclient_conf->self_sync.token_str)
	{
		/* Copy token into buffer and calculate token size */
		strncpy((char*)&ev_tcpclient->cfg.self_sync.token_str_buf, ev_tcpclient_conf->self_sync.token_str, sizeof(ev_tcpclient->cfg.self_sync.token_str_buf));
		ev_tcpclient->cfg.self_sync.token_str_sz = strlen(ev_tcpclient_conf->self_sync.token_str);

		/* Mark self sync as active for this server and save max buffer limit */
		ev_tcpclient->cfg.self_sync.max_buffer_sz	= ev_tcpclient_conf->self_sync.max_buffer_sz;
		ev_tcpclient->cfg.flags.self_sync			= 1;
	}

	/* Save a copy of SRC_ADDRESS if UPPER LAYERS supplied it */
	if (ev_tcpclient_conf->src_addr.sin_addr.s_addr > 0)
	{
		memcpy(&ev_tcpclient->src_addr, &ev_tcpclient_conf->src_addr, sizeof(struct sockaddr_in));
		ev_tcpclient->flags.source_addr_present = 1;

		/* Now BIND to local SOURCE, LOCAL or REMOVE, as set by FLAGS */
		if (ev_tcpclient_conf->flags.bindany_active)
			EvKQBaseSocketBindRemote(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, (struct sockaddr *)&ev_tcpclient->src_addr);
		else
			EvKQBaseSocketBindLocal(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, (struct sockaddr *)&ev_tcpclient->src_addr);
	}

	/* Check if host needs to be resolved - If RESOLV fails, invoke COMM_CLIENT_STATE_CONNECT_FAILED_DNS */
	if (ev_tcpclient->flags.need_dns_lookup)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Begin LOOKUP of HOSTNAME [%s]\n", ev_tcpclient->socket_fd, ev_tcpclient_conf->hostname);

		/* Start ASYNC DNS lookup */
		ev_tcpclient->dnsreq_id = CommEvDNSGetHostByName(ev_tcpclient->resolv_base, ev_tcpclient_conf->hostname, CommEvTCPClientDNSResolverCB, ev_tcpclient);

		if (ev_tcpclient->dnsreq_id	> -1)
		{
			ev_tcpclient->socket_state	= COMM_CLIENT_STATE_RESOLVING_DNS;
			return 1;
		}
		else
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed on DNS lookup\n", ev_tcpclient->socket_fd);
			return 0;
		}
	}
	/* No need to resolve, plain old IP address */
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - WONT NEED DNS LOOKUP - UNIX [%d] - ADDRESS [%s]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.flags.unix_socket, ev_tcpclient_conf->hostname);

		/* Initialize address */
		if (ev_tcpclient->cfg.flags.unix_socket)
			CommEvTCPClientAddrInitUnix(ev_tcpclient, ev_tcpclient_conf->hostname);
		else
			CommEvTCPClientAddrInit(ev_tcpclient, ev_tcpclient_conf->hostname, ev_tcpclient_conf->port);

		/* Issue ASYNC connect */
		op_status 		= CommEvTCPClientAsyncConnect(ev_tcpclient);
	}

	return op_status;
}
/**************************************************************************************************************************/
void CommEvTCPClientResetFD(CommEvTCPClient *ev_tcpclient)
{
	int old_socketfd;

	/* Sanity check */
	if (!ev_tcpclient)
		return;

	if (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
		return;

	/* Save a copy of old socket_fd and create a new one */
	old_socketfd			= ev_tcpclient->socket_fd;

	/* Create socket and set it to non_blocking */
	if (ev_tcpclient->cfg.flags.unix_socket)
		ev_tcpclient->socket_fd = EvKQBaseSocketUNIXNew(ev_tcpclient->kq_base);
	else
		ev_tcpclient->socket_fd = EvKQBaseSocketTCPNew(ev_tcpclient->kq_base);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Reset to NEW_FD [%d]\n", old_socketfd, ev_tcpclient->socket_fd);

	/* Set it to non_blocking and save it into newly allocated client */
	EvKQBaseSocketSetNonBlock(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	/* Set context to new FD and reset negotiation count */
	if ((COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto) && (ev_tcpclient->ssldata.ssl_handle))
	{
		SSL_set_fd(ev_tcpclient->ssldata.ssl_handle, ev_tcpclient->socket_fd);
		ev_tcpclient->ssldata.ssl_negotiatie_trycount	= 0;
	}

	/* Will close socket and cancel any pending events of old_socketfd */
	if (old_socketfd > 0)
		EvKQBaseSocketClose(ev_tcpclient->kq_base, old_socketfd);

	return;
}
/**************************************************************************************************************************/
int CommEvTCPClientResetConn(CommEvTCPClient *ev_tcpclient)
{
	if ((ev_tcpclient->socket_state == COMM_CLIENT_STATE_DISCONNECTED) || (ev_tcpclient->socket_state > COMM_CLIENT_STATE_CONNECTED))
		return 0;

	/* Disconnect */
	CommEvTCPClientDisconnect(ev_tcpclient);

	/* Schedule RECONNECT timer */
	ev_tcpclient->timers.reconnect_after_close_id =	EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE, 1, CommEvTCPClientReconnectTimer, ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientReconnect(CommEvTCPClient *ev_tcpclient)
{
	int op_status = 0;

	/* Sanity check */
	if (!ev_tcpclient)
		return 0;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Will try reconnect\n", ev_tcpclient->socket_fd);

	if ((ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTING) || (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SSL))
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Already connecting\n", ev_tcpclient->socket_fd);
		return 0;
	}

	if (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Will not reconnect, already connected\n", ev_tcpclient->socket_fd);
		return 0;
	}

	/* Reset this client SOCKET_FD */
	CommEvTCPClientResetFD(ev_tcpclient);

	/* Failed creating SOCKET, schedule new try */
	if (ev_tcpclient->socket_fd < 0)
		goto failed;

	/* If we are connecting to a HOSTNAME FQDN, check if our DNS entry is expired, or if upper layers asked for new lookup on reconnect */
	if ( (ev_tcpclient->flags.need_dns_lookup) && (ev_tcpclient->flags.reconnect_new_dnslookup || (ev_tcpclient->dns.expire_ts == -1) ||
			(ev_tcpclient->dns.expire_ts <= ev_tcpclient->kq_base->stats.cur_invoke_ts_sec) ))
	{
		/* Begin DNS lookup */
		ev_tcpclient->dnsreq_id		= CommEvDNSGetHostByName(ev_tcpclient->resolv_base, ev_tcpclient->cfg.hostname, CommEvTCPClientDNSResolverCB, ev_tcpclient);

		/* Begin DNS lookup */
		if (ev_tcpclient->dnsreq_id	> -1)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Begin DNS lookup at ID [%d]\n", ev_tcpclient->socket_fd, ev_tcpclient->dnsreq_id);
			ev_tcpclient->socket_state	= COMM_CLIENT_STATE_RESOLVING_DNS;
			return 1;
		}
		else
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - DNS lookup failed\n", ev_tcpclient->socket_fd);
			goto failed;
		}
	}
	/* Just connect */
	else
	{
		/* Client wants to rotate balance on multiple IP entries if possible, do it */
		if (ev_tcpclient->flags.reconnect_balance_on_ips && (ev_tcpclient->dns.a_reply.ip_count > 1))
			CommEvTCPClientAddrInitFromAReply(ev_tcpclient);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Begin ASYNC connect\n", ev_tcpclient->socket_fd);

		/* Begin ASYNC connect */
		op_status = CommEvTCPClientAsyncConnect(ev_tcpclient);
		return op_status;
	}

	return op_status;

	failed:

	/* Flag to reconnect on FAIL not set */
	if (!ev_tcpclient->flags.reconnect_on_fail)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Will not auto-reconnect, flag is disabled\n", ev_tcpclient->socket_fd);
		return 0;
	}

	/* Schedule RECONNECT timer */
	ev_tcpclient->timers.reconnect_on_fail_id =	EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
			((ev_tcpclient->retry_times.reconnect_on_fail_ms > 0) ? ev_tcpclient->retry_times.reconnect_on_fail_ms : COMM_TCP_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
			CommEvTCPClientReconnectTimer, ev_tcpclient);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
			ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_on_fail_id);
	return 0;
}
/**************************************************************************************************************************/
void CommEvTCPClientDisconnect(CommEvTCPClient *ev_tcpclient)
{
	/* Sanity check */
	if (!ev_tcpclient)
		return;

	//if ((ev_tcpclient->socket_state == COMM_CLIENT_STATE_DISCONNECTED) || (ev_tcpclient->socket_state > COMM_CLIENT_STATE_CONNECTED))
	//	return;

	/* Release SSL related objects */
	if (COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto)
	{
		if (ev_tcpclient->ssldata.x509_cert)
		{
			X509_free(ev_tcpclient->ssldata.x509_cert);
			ev_tcpclient->ssldata.x509_cert = NULL;
		}
	}

	/* Destroy IO data buffers and cancel any pending event */
	CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);
	CommEvTCPClientTimersCancelAll(ev_tcpclient);

	/* Will close socket and cancel any pending events of ev_tcpclient->socket_fd, including the close event */
	EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	/* Set new state */
	ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;
	ev_tcpclient->socket_fd		= -1;

	return;
}
/**************************************************************************************************************************/
int CommEvTCPClientDisconnectRequest(CommEvTCPClient *ev_tcpclient)
{
	EvKQBase *ev_base							= ev_tcpclient->kq_base;
	EvBaseKQFileDesc *kq_fd						= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);
	EvBaseKQGenericEventPrototype *write_proto	= kq_fd ? &kq_fd->cb_handler[KQ_CB_HANDLER_WRITE] : NULL;

	/* Sanity check */
	if (!ev_tcpclient)
		return 0;

	/* Mark flags to close as soon as we finish writing all */
	ev_tcpclient->flags.close_request = 1;

	/* No write event set, close immediately */
	if ((!write_proto) || (!write_proto->flags.enabled))
		goto close;

	/* Client write QUEUE is empty */
	if (EvAIOReqQueueIsEmpty(&ev_tcpclient->iodata.write_queue))
		goto close;

	/* We are already disconnected */
	if (ev_tcpclient->socket_state != COMM_CLIENT_STATE_CONNECTED)
		goto close;


	return 1;

	/* TAG for CLOSE */
	close:

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN,
			"FD [%d] - Immediate [%s]\n", ev_tcpclient->socket_fd, ev_tcpclient->flags.destroy_after_close ? "DESTORY" : "DISCONNECT");

	/* Destroy or disconnected, based on flag */
	COMM_EV_TCP_CLIENT_FINISH(ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteVectored(CommEvTCPClient *ev_tcpclient, EvAIOReqIOVectorData *vector_table, int vector_table_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReqIOVectorData *cur_iovec;
	EvAIOReq *aio_req;
	int i;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Write all vectors on table */
	for (i = 0; i < vector_table_sz; i++)
	{
		/* Grab current offset data */
		cur_iovec = &vector_table[i];

		/* Last WRITE, create with FINISH CB INFO */
		if ((i - 1) == vector_table_sz)
			aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, (cur_iovec->data_ptr + cur_iovec->offset),
					cur_iovec->size, 0, NULL, finish_cb, finish_cbdata);
		/* Create NO_FINISH AIO_REQ for initial WRITEs*/
		else
			aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, (cur_iovec->data_ptr + cur_iovec->offset),
					cur_iovec->size, 0, NULL, NULL, NULL);

		/* No more AIO slots, STOP */
		if (!aio_req)
			return 0;

		/* Set flags we are WRITING to a SOCKET */
		aio_req->flags.aio_write	= 1;
		aio_req->flags.aio_socket	= 1;

		/* Enqueue it and begin writing ASAP */
		CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

		continue;
	}

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteAndDestroyMemBuffer(CommEvTCPClient *ev_tcpclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, MemBufferDeref(mem_buf),
			MemBufferGetSize(mem_buf), 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Populate destroy data */
	aio_req->destroy_func		= (EvAIOReqDestroyFunc*)MemBufferDestroy;
	aio_req->destroy_cbdata		= mem_buf;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteMemBuffer(CommEvTCPClient *ev_tcpclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, MemBufferDeref(mem_buf),
			MemBufferGetSize(mem_buf), 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteStringFmt(CommEvTCPClient *ev_tcpclient, CommEvTCPClientCBH *finish_cb, void *finish_cbdata, char *string, ...)
{
	EvAIOReq *aio_req;
	va_list args;
	char *buf_ptr;
	int buf_sz;
	int msg_len;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

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
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, buf_ptr, buf_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
	{
		free(buf_ptr);
		return 0;
	}

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);

	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteString(CommEvTCPClient *ev_tcpclient, char *string, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;
	int string_sz = strlen(string);

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, strdup(string),
			string_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);
	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWriteAndFree(CommEvTCPClient *ev_tcpclient, char *data, unsigned long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, data, data_sz, 0, (EvAIOReqDestroyFunc*)free, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;
	aio_req->flags.dup_data		= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);
	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientAIOWrite(CommEvTCPClient *ev_tcpclient, char *data, unsigned long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Close request */
	if (ev_tcpclient->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&ev_tcpclient->iodata.write_queue, ev_tcpclient->socket_fd, ev_tcpclient, data, data_sz, 0, NULL, finish_cb, finish_cbdata);

	/* No more AIO slots, STOP */
	if (!aio_req)
		return 0;

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_socket	= 1;

	/* Enqueue and begin writing ASAP */
	CommEvTCPClientEnqueueAndKickWriteQueue(ev_tcpclient, aio_req);
	return 1;

}
/**************************************************************************************************************************/
int CommEvTCPClientSSLShutdownBegin(CommEvTCPClient *ev_tcpclient)
{
	/* Already shutting down, bail out */
	if (ev_tcpclient->flags.ssl_shuting_down)
		return 0;

	/* Set flags as shutting down */
	ev_tcpclient->flags.ssl_shuting_down = 1;

	/* Schedule SSL shutdown JOB for NEXT IO LOOP */
	ev_tcpclient->ssldata.shutdown_jobid = EvKQJobsAdd(ev_tcpclient->kq_base, JOB_ACTION_ADD_VOLATILE, 0, CommEvTCPClientSSLShutdownJob, ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientEventIsSet(CommEvTCPClient *ev_tcpclient, CommEvTCPClientEventCodes ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_CLIENT_EVENT_LASTITEM)
		return 0;

	if (ev_tcpclient->events[ev_type].cb_handler_ptr && ev_tcpclient->events[ev_type].flags.enabled)
		return 1;

	return 0;
}
/**************************************************************************************************************************/
void CommEvTCPClientEventSet(CommEvTCPClient *ev_tcpclient, CommEvTCPClientEventCodes ev_type, CommEvTCPClientCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= COMM_CLIENT_EVENT_LASTITEM)
		return;

	/* Set event */
	ev_tcpclient->events[ev_type].cb_handler_ptr	= cb_handler;
	ev_tcpclient->events[ev_type].cb_data_ptr		= cb_data;

	/* Mark enabled */
	ev_tcpclient->events[ev_type].flags.enabled		= 1;

	/* Do not update underlying events if we are not connected */
	if (ev_tcpclient->socket_state != COMM_CLIENT_STATE_CONNECTED)
		return;

	/* Reschedule READ EVENT if EV_READ has been activated */
	if (COMM_CLIENT_EVENT_READ == ev_type)
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE,
				((COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto) ? CommEvTCPClientEventSSLRead : CommEvTCPClientEventRead), ev_tcpclient);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPClientEventCancel(CommEvTCPClient *ev_tcpclient, CommEvTCPClientEventCodes ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_CLIENT_EVENT_LASTITEM)
		return;

	/* Set event */
	ev_tcpclient->events[ev_type].cb_handler_ptr		= NULL;
	ev_tcpclient->events[ev_type].cb_data_ptr			= NULL;

	/* Mark disabled */
	ev_tcpclient->events[ev_type].flags.enabled			= 0;

	/* Update underlying event */
	if (COMM_CLIENT_EVENT_READ == ev_type)
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPClientEventCancelAll(CommEvTCPClient *ev_tcpclient)
{
	int i;

	/* Cancel all possible events */
	for (i = 0; i < COMM_CLIENT_EVENT_LASTITEM; i++)
	{
		CommEvTCPClientEventCancel(ev_tcpclient, i);
	}

	return;
}
/**************************************************************************************************************************/
void CommEvTCPClientAddrInit(CommEvTCPClient *ev_tcpclient, char *host, unsigned short port)
{
	struct sockaddr_in *ipv4_addr;
	/* Clean dst_addr structure for later use */
	memset(&ev_tcpclient->dst_addr, 0, sizeof(ev_tcpclient->dst_addr));

	ipv4_addr 							= (struct sockaddr_in *)&ev_tcpclient->dst_addr;

	/* Fill in the stub sockaddr_in structure */
	ev_tcpclient->dst_addr.ss_family	= AF_INET;
//	ev_tcpclient->dst_addr.ss_len 		= sizeof(struct sockaddr_in);

	ipv4_addr->sin_addr.s_addr			= inet_addr(host);
	ipv4_addr->sin_port					= htons(port);

	/* Save access data */
	strncpy((char*)&ev_tcpclient->cfg.hostname, host, sizeof(ev_tcpclient->cfg.hostname));
	ev_tcpclient->cfg.port 				= port;

	return;
}
/**************************************************************************************************************************/
void CommEvTCPClientAddrInitUnix(CommEvTCPClient *ev_tcpclient, char *unix_path)
{
	struct sockaddr_un *unix_addr;
	/* Clean dst_addr structure for later use */
	memset(&ev_tcpclient->dst_addr, 0, sizeof(ev_tcpclient->dst_addr));

	unix_addr 							= (struct sockaddr_un *)&ev_tcpclient->dst_addr;

	/* Fill in the stub sockaddr_in structure */
	ev_tcpclient->dst_addr.ss_family	= AF_UNIX;
	ev_tcpclient->dst_addr.ss_len 		= sizeof(struct sockaddr_un);

//	unix_addr->sun_len 					= sizeof(struct sockaddr_un);

	strncpy(unix_addr->sun_path, unix_path, sizeof(unix_addr->sun_path));

	/* Save access data */
	strncpy((char*)&ev_tcpclient->cfg.hostname, unix_path, sizeof(ev_tcpclient->cfg.hostname));
	ev_tcpclient->cfg.port 				= 0;

	return;
}
/**************************************************************************************************************************/
int CommEvTCPClientCheckState(int socket_fd)
{
	int op_status = -1;
	int err = 0;
	socklen_t errlen = sizeof(err);

	op_status = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &errlen);

	if (op_status == 0)
	{
		switch (err)
		{

		case 0:
		case EISCONN:
			return COMM_CLIENT_STATE_CONNECTED;

		case EINPROGRESS:
			/* FALL TROUGHT */
		case EWOULDBLOCK:
			/* FALL TROUGHT */
		case EALREADY:
			/* FALL TROUGHT */
		case EINTR:
			return COMM_CLIENT_STATE_CONNECTING;

		case ECONNREFUSED:
			return COMM_CLIENT_STATE_CONNECT_FAILED_REFUSED;

		default:
			break;

		}
	}

	return COMM_CLIENT_STATE_CONNECT_FAILED_UNKNWON;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvTCPClientAddrInitFromAReply(CommEvTCPClient *ev_tcpclient)
{
	struct sockaddr_in *ipv4_addr;

	/* Clean dst_addr structure for later use */
	memset(&ev_tcpclient->dst_addr, 0, sizeof(ev_tcpclient->dst_addr));

	ipv4_addr 							= (struct sockaddr_in *)&ev_tcpclient->dst_addr;

	/* Fill in the stub sockaddr_in structure */
	ev_tcpclient->dst_addr.ss_family	= AF_INET;
//	ev_tcpclient->dst_addr.ss_len		= sizeof(struct sockaddr_in);

	/* Fill in the stub sockaddr_in structure */
	ipv4_addr->sin_port					= htons(ev_tcpclient->cfg.port);

	/* Balance on IPs */
	if (ev_tcpclient->flags.reconnect_balance_on_ips)
	{
		/* Copy currently pointed address */
		memcpy(&ipv4_addr->sin_addr.s_addr, &ev_tcpclient->dns.a_reply.ip_arr[ev_tcpclient->dns.cur_idx].addr, sizeof(struct in_addr));

		/* Adjust INDEX to rotate ROUND ROBIN */
		if (ev_tcpclient->dns.cur_idx == (ev_tcpclient->dns.a_reply.ip_count - 1) )
			ev_tcpclient->dns.cur_idx = 0;
		else
			ev_tcpclient->dns.cur_idx++;

	}
	else
	{
		memcpy(&ipv4_addr->sin_addr.s_addr, &ev_tcpclient->dns.a_reply.ip_arr[0].addr, sizeof(struct in_addr));
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvTCPClientEnqueueAndKickWriteQueue(CommEvTCPClient *ev_tcpclient, EvAIOReq *aio_req)
{
	/* Close request - Silently drop */
	if (ev_tcpclient->flags.close_request)
	{
		EvAIOReqDestroy(aio_req);
		return;
	}

	/* Allow upper layers to transform data */
	EvAIOReqTransform_WriteData(&ev_tcpclient->transform, &ev_tcpclient->iodata.write_queue, aio_req);

	/* Enqueue it in conn_queue */
	EvAIOReqQueueEnqueue(&ev_tcpclient->iodata.write_queue, aio_req);

	/* Do not ADD a write event if we are disconnected, as it will overlap our internal connect event */
	if (ev_tcpclient->socket_state != COMM_CLIENT_STATE_CONNECTED)
		return;

	/* If there is ENQUEUED data, schedule WRITE event and LEAVE, as we need to PRESERVE WRITE ORDER */
	if (ev_tcpclient->flags.pending_write)
	{
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE,
				(COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto) ? CommEvTCPClientEventSSLWrite : CommEvTCPClientEventWrite, ev_tcpclient);
		return;
	}
	/* Try to write on this very same IO LOOP */
	else
	{
		if (COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto)
			CommEvTCPClientEventSSLWrite(ev_tcpclient->socket_fd, 8092, -1, ev_tcpclient, ev_tcpclient->kq_base);
		else
			CommEvTCPClientEventWrite(ev_tcpclient->socket_fd, 8092, -1, ev_tcpclient, ev_tcpclient->kq_base);

		return;
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvTCPClientEventDispatchInternal(CommEvTCPClient *ev_tcpclient, int data_sz, int thrd_id, int ev_type)
{
	CommEvTCPClientCBH *cb_handler		= NULL;
	void *cb_handler_data				= NULL;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - EV_ID [%d] - EV_SIZE [%d]\n", ev_tcpclient->socket_fd, ev_type, data_sz);

	/* Mute upper layer events, client has request shutdown */
	if (ev_tcpclient->flags.close_request)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Wont dispatch - CLOSE_REQUEST\n", ev_tcpclient->socket_fd, data_sz);
		return;
	}

	/* Grab callback_ptr */
	cb_handler	= ev_tcpclient->events[ev_type].cb_handler_ptr;

	/* Touch time stamps */
	ev_tcpclient->events[ev_type].last_ts = ev_tcpclient->kq_base->stats.cur_invoke_ts_sec;
	memcpy(&ev_tcpclient->events[ev_type].last_tv, &ev_tcpclient->kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Will INVOKE - EV_ID [%d] with [%d] bytes\n", ev_tcpclient->socket_fd, ev_type, data_sz);

		/* Grab data for this CBH */
		cb_handler_data = ev_tcpclient->events[ev_type].cb_data_ptr;

		/* Jump into CBH. Base for this event is CommEvTCPServer* */
		cb_handler(ev_tcpclient->socket_fd, data_sz, thrd_id, cb_handler_data, ev_tcpclient);
	}
	else
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - No CB_HANDLER for EV_ID [%d] with [%d] bytes\n", ev_tcpclient->socket_fd, ev_type, data_sz);

	return;
}
/**************************************************************************************************************************/
static void CommEvTCPClientDestroyConnReadAndWriteBuffers(CommEvTCPClient *ev_tcpclient)
{
	/* Destroy any pending write event */
	EvAIOReqQueueClean(&ev_tcpclient->iodata.write_queue);

	/* Destroy any data buffered on read_stream */
	if (ev_tcpclient->iodata.read_stream)
		MemStreamDestroy(ev_tcpclient->iodata.read_stream);
	if (ev_tcpclient->iodata.partial_read_stream)
		MemStreamDestroy(ev_tcpclient->iodata.partial_read_stream);

	/* Destroy any data buffered on read_buffer */
	if (ev_tcpclient->iodata.read_buffer)
		MemBufferDestroy(ev_tcpclient->iodata.read_buffer);
	if (ev_tcpclient->iodata.partial_read_buffer)
		MemBufferDestroy(ev_tcpclient->iodata.partial_read_buffer);

	ev_tcpclient->iodata.read_stream = NULL;
	ev_tcpclient->iodata.read_buffer = NULL;

	ev_tcpclient->iodata.partial_read_stream = NULL;
	ev_tcpclient->iodata.partial_read_buffer = NULL;

	return;
}
/**************************************************************************************************************************/
static int CommEvTCPClientEventEof(int fd, int buf_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_tcpclient->kq_base, fd);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - EOF - [%d] bytes left in kernel buffer - [%d] bytes left in MEM_MB\n",
			fd, buf_read_sz, ev_tcpclient->iodata.read_buffer ? MemBufferGetSize(ev_tcpclient->iodata.read_buffer) : -1);

	/* Mark SOCKET_EOF for this client */
	ev_tcpclient->flags.socket_eof = 1;

	/* There is a close request for this client. Upper layers do not care any longer, just finish */
	if (ev_tcpclient->flags.close_request)
		goto finish;

	/* Do not close for now, there is data pending read */
	if ((buf_read_sz > 0) && (!ev_tcpclient->flags.peek_on_read))
	{
		EvKQBaseSetEvent(ev_tcpclient->kq_base, fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventEof, ev_tcpclient);
		return 0;
	}

	/* Dispatch internal event */
	CommEvTCPClientEventDispatchInternal(ev_tcpclient, buf_read_sz, thrd_id, COMM_CLIENT_EVENT_CLOSE);

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return 0;

	/* Either destroy or disconnect client, as requested by operator flags */
	finish:
	COMM_EV_TCP_CLIENT_INTERNAL_FINISH(ev_tcpclient);
	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base							= base_ptr;
	CommEvTCPClient *ev_tcpclient				= cb_data;
	EvBaseKQFileDesc *kq_fd						= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);
	CommEvTCPClientEventPrototype *ev_proto		= &ev_tcpclient->events[COMM_CLIENT_EVENT_WRITE];
	int total_wrote_sz							= 0;

	CommEvTCPIOResult ioret;
	int op_status;

	/* This WRITE_EV is HOOKED */
	if (ev_proto->flags.hooked)
	{
		BRB_ASSERT_FMT(ev_base, (ev_proto->cb_hook_ptr), "FD [%d] - Write event has HOOK flag, but no HOOK_CBH\n", ev_tcpclient->socket_fd);

		/* Jump into HOOK code */
		total_wrote_sz = ev_proto->cb_hook_ptr(fd, can_write_sz, thrd_id, cb_data, base_ptr);

		/* We are CLOSED, bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return total_wrote_sz;

		/* Nothing left on WRITE_QUEUE */
		if (EvAIOReqQueueIsEmpty(&ev_tcpclient->iodata.write_queue))
		{
			/* Reset pending write flag */
			ev_tcpclient->flags.pending_write = 0;

			/* Has close request, invoke */
			if (ev_tcpclient->flags.close_request)
				COMM_EV_TCP_CLIENT_FINISH(ev_tcpclient);

			return total_wrote_sz;
		}
		else
		{
			/* Reschedule write event and SET pending WRITE FLAG */
			EvKQBaseSetEvent(ev_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventWrite, ev_tcpclient);
			ev_tcpclient->flags.pending_write = 1;
			return total_wrote_sz;
		}
	}
	/* NOT HOOKED - Invoke IO mechanism to write data */
	else
		op_status = CommEvTCPAIOWrite(ev_base, ev_tcpclient->log_base, &ev_tcpclient->statistics, &ev_tcpclient->iodata, &ioret, ev_tcpclient, can_write_sz,
				(!ev_tcpclient->flags.close_request));

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return ioret.aio_total_sz;

	/* Jump into FSM */
	switch (op_status)
	{
	case COMM_TCP_AIO_WRITE_NEEDED:
	{
		/* Reschedule write event and SET pending WRITE FLAG */
		EvKQBaseSetEvent(ev_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventWrite, ev_tcpclient);
		ev_tcpclient->flags.pending_write = 1;

		return ioret.aio_total_sz;
	}

	/* All writes FINISHED */
	case COMM_TCP_AIO_WRITE_FINISHED:
	{
		/* Reset pending write flag */
		ev_tcpclient->flags.pending_write = 0;

		/* Upper layers requested to close after writing all */
		if (ev_tcpclient->flags.close_request)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Upper layer set CLOSE_REQUEST, write buffer is empty - [%s]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->flags.destroy_after_close ? "DESTROYING" : "CLOSING");

			/* Destroy or disconnected, based on flag */
			COMM_EV_TCP_CLIENT_FINISH(ev_tcpclient);
		}

		return ioret.aio_total_sz;
	}
	case COMM_TCP_AIO_WRITE_ERR_FATAL:
	{
		/* Has close request, invoke */
		if (ev_tcpclient->flags.close_request)
			COMM_EV_TCP_CLIENT_FINISH(ev_tcpclient);

		return ioret.aio_total_sz;
	}
	default:
		BRB_ASSERT_FMT(ev_base, 0, "Undefined state [%d]\n", op_status);
		return ioret.aio_total_sz;
	}

	return ioret.aio_total_sz;
}
/**************************************************************************************************************************/
static int CommEvTCPClientEventRead(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base							= base_ptr;
	CommEvTCPClient *ev_tcpclient				= cb_data;
	EvBaseKQFileDesc *kq_fd						= EvKQBaseFDGrabFromArena(ev_base, fd);
	CommEvTCPClientEventPrototype *ev_proto		= &ev_tcpclient->events[COMM_CLIENT_EVENT_READ];
	int data_read								= 0;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Read event of [%d] bytes\n", fd, read_sz);

	/* This is a closing connection, bail out */
	if (read_sz <= 0)
		return 0;

	/* This READ_EV is HOOKED */
	if (ev_proto->flags.hooked)
	{
		BRB_ASSERT_FMT(ev_base, (ev_proto->cb_hook_ptr), "FD [%d] - Read event has HOOK flag, but no HOOK_CBH\n", ev_tcpclient->socket_fd);

		/* Jump into HOOK code */
		data_read = ev_proto->cb_hook_ptr(fd, read_sz, thrd_id, cb_data, base_ptr);

		/* We are CLOSED, bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return data_read;

		/* Dispatch an internal READ_EV so upper layer get notified after HOOKED code finish */
		if (data_read > 0)
			CommEvTCPClientEventDispatchInternal(ev_tcpclient, read_sz, thrd_id, COMM_CLIENT_EVENT_READ);
	}
	/* Read buffer and invoke CBH - WARNING: This may destroy TCPCLIENT under our feet */
	else
		data_read = CommEvTCPClientProcessBuffer(ev_tcpclient, read_sz, thrd_id, NULL, 0);

	/* We are CLOSED, bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return data_read;

	/* NON fatal error while reading this FD, reschedule and leave */
//	if (-1 == data_read)
//	{
//		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - Non fatal error while trying to read [%d] bytes\n", fd, read_sz);
//	}

	/* Touch statistics */
	if (data_read > 0)
	{
		ev_tcpclient->statistics.total[COMM_CURRENT].byte_rx	+= data_read;
		ev_tcpclient->statistics.total[COMM_CURRENT].packet_rx	+= 1;
	}

	/* Reschedule read event - Upper layers could have closed this socket, so just RESCHEDULE READ if we are still ONLINE */
	if (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventRead, ev_tcpclient);

	return data_read;
}
/**************************************************************************************************************************/
static int CommEvTCPClientEventConnect(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);
	int pending_conn				= 0;

	/* Query the kernel for the current socket state */
	ev_tcpclient->socket_state = CommEvTCPClientCheckState(fd);

	/* Connection not yet completed, reschedule and return */
	if (COMM_CLIENT_STATE_CONNECTING == ev_tcpclient->socket_state)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CONNECTING...!\n", ev_tcpclient->socket_fd);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventConnect, ev_tcpclient);
		return 0;
	}

	/* SSL - TCP socket connected OK. Begin SSL negotiation and switch socket_state */
	if ((COMM_CLIENT_STATE_CONNECTED == ev_tcpclient->socket_state) && (COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto))
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Connected, begin SSL handshake\n", ev_tcpclient->socket_fd);

		/* Set client state */
		ev_tcpclient->socket_state = COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SSL;

		/* Initialize WRITE_QUEUE if its not already initialized */
		EvAIOReqQueueInit(ev_tcpclient->kq_base, &ev_tcpclient->iodata.write_queue, 4096,
				(ev_tcpclient->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

		/* Begin SSL negotiation handshake */
		CommEvTCPClientEventSSLNegotiate(fd, data_sz, thrd_id, ev_tcpclient, base_ptr);

		return 0;
	}
	/* PLAIN - Do not matter if PLAIN connection succeeded or failed, just dispatch internal event and give a chance to operator define READ and CLOSE call_backs */
	else
	{
		/* If connected OK, set READ and EOF events */
		if (COMM_CLIENT_STATE_CONNECTED == ev_tcpclient->socket_state)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Connected!\n", ev_tcpclient->socket_fd);

			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventRead, ev_tcpclient);
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventEof, ev_tcpclient);

			/* Initialize WRITE_QUEUE if its not already initialized */
			EvAIOReqQueueInit(ev_tcpclient->kq_base, &ev_tcpclient->iodata.write_queue, 4096,
					(ev_tcpclient->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

			/* Schedule DATARATE CALCULATE TIMER timer */
			if (ev_tcpclient->flags.calculate_datarate)
			{
				ev_tcpclient->timers.calculate_datarate_id = EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
						((ev_tcpclient->retry_times.calculate_datarate_ms > 0) ? ev_tcpclient->retry_times.calculate_datarate_ms : COMM_TCP_CLIENT_CALCULATE_DATARATE_DEFAULT_MS),
						CommEvTCPClientRatesCalculateTimer, ev_tcpclient);

				KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule DATARATE at TIMER_ID [%d]\n",
						ev_tcpclient->socket_fd, ev_tcpclient->timers.calculate_datarate_id);
			}
			else
				ev_tcpclient->timers.calculate_datarate_id = -1;
		}

		/* Dispatch the internal event - This could destroy EV_TCPCLIENT under our feet */
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatch CONNECT\n", ev_tcpclient->socket_fd);
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

		/* Closed flag set, we are already destroyed, just bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return 0;

		/* Connection has failed */
		if (ev_tcpclient->socket_state > COMM_CLIENT_STATE_CONNECTED)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Connection FAILED!\n", ev_tcpclient->socket_fd);

			/* Upper layers want a full DESTROY if CONNECTION FAILS */
			if (ev_tcpclient->flags.destroy_after_connect_fail)
			{
				KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Destroy as set by flag - DESTROY_AFTER_FAIL\n", ev_tcpclient->socket_fd);

				CommEvTCPClientDestroy(ev_tcpclient);
			}
			/* Upper layers want a reconnect retry if CONNECTION FAILS */
			else if ((ev_tcpclient->flags.reconnect_on_fail) && (ev_tcpclient->socket_state > COMM_CLIENT_STATE_CONNECTED))
			{
				/* Will close socket and cancel any pending events of ev_tcpclient->socket_fd, including the close event */
				if ( ev_tcpclient->socket_fd >= 0)
					EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

				/* Destroy read and write buffers */
				CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

				/* Schedule RECONNECT timer */
				ev_tcpclient->timers.reconnect_on_fail_id =	EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
						((ev_tcpclient->retry_times.reconnect_on_fail_ms > 0) ? ev_tcpclient->retry_times.reconnect_on_fail_ms : COMM_TCP_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
						CommEvTCPClientReconnectTimer, ev_tcpclient);

				KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
						ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_on_fail_id);

				/* Set flags and STATE */
				ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;
				ev_tcpclient->socket_fd		= -1;
			}
			else
			{
				KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - NO ACTION TAKEN!\n", ev_tcpclient->socket_fd);
			}
		}

		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
int CommEvTCPClientEventSSLNegotiate(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);
	int op_status;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - SSL_HANDSHAKE try [%d]\n",
			ev_tcpclient->socket_fd, ev_tcpclient->ssldata.ssl_negotiatie_trycount);

	/* Increment count */
	ev_tcpclient->ssldata.ssl_negotiatie_trycount++;

	/* Too many negotiation retries, give up */
	if (ev_tcpclient->ssldata.ssl_negotiatie_trycount > 50)
		goto negotiation_failed;

	/* Clear libSSL errors and invoke SSL handshake mechanism */
	ERR_clear_error();
	op_status = SSL_connect(ev_tcpclient->ssldata.ssl_handle);

	/* Failed to connect on this try, check what is going on */
	if (op_status <= 0)
	{
		int ssl_error = SSL_get_error(ev_tcpclient->ssldata.ssl_handle, op_status);

		switch (ssl_error)
		{

		case SSL_ERROR_WANT_READ:
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLNegotiate, ev_tcpclient);
			return 0;

		case SSL_ERROR_WANT_WRITE:
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLNegotiate, ev_tcpclient);
			return 0;

		default:
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - SSL handshake failed with SSL_ERROR [%d]\n",
					ev_tcpclient->socket_fd, ssl_error);
			goto negotiation_failed;
			return 0;
		}
		}
	}
	/* SSL connected OK */
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL handshake OK\n", ev_tcpclient->socket_fd);

		/* Set state as CONNECTED and grab PEER certificate information */
		ev_tcpclient->socket_state		= COMM_CLIENT_STATE_CONNECTED;
		ev_tcpclient->ssldata.x509_cert = SSL_get_peer_certificate(ev_tcpclient->ssldata.ssl_handle);

		/* XXX TODO: Check if SUBJECT_NAME or ALTERNATIVE_NAMEs match SNI host we connect to - Only protection to MITM */

		/* Schedule DATARATE CALCULATE TIMER timer */
		if (ev_tcpclient->flags.calculate_datarate)
		{
			ev_tcpclient->timers.calculate_datarate_id = EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
					((ev_tcpclient->retry_times.calculate_datarate_ms > 0) ? ev_tcpclient->retry_times.calculate_datarate_ms : COMM_TCP_CLIENT_CALCULATE_DATARATE_DEFAULT_MS),
					CommEvTCPClientRatesCalculateTimer, ev_tcpclient);

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule DATARATE at TIMER_ID [%d]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->timers.calculate_datarate_id);
		}
		else
		{
			ev_tcpclient->timers.calculate_datarate_id = -1;
		}

		/* Dispatch the internal event */
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatch CONNECT\n", ev_tcpclient->socket_fd);
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

		/* Closed flag set, we are already destroyed, just bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return 0;

		ev_tcpclient->ssldata.ssl_negotiatie_trycount = 0;

		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventEof, ev_tcpclient);

		return 1;
	}

	/* Negotiation failed label */
	negotiation_failed:

	/* Mark fail state and close socket */
	ev_tcpclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SSL;

	/* Dispatch the internal event */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatch CONNECT\n", ev_tcpclient->socket_fd);
	CommEvTCPClientEventDispatchInternal(ev_tcpclient, data_sz, thrd_id, COMM_CLIENT_EVENT_CONNECT);

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return 0;

	/* Internal CLEANUP */
	CommEvTCPClientInternalDisconnect(ev_tcpclient);

	/* Upper layers want a full DESTROY if CONNECTION FAILS */
	if (ev_tcpclient->flags.destroy_after_connect_fail)
	{
		CommEvTCPClientDestroy(ev_tcpclient);
	}
	/* Upper layers want a reconnect retry if CONNECTION FAILS */
	else if (ev_tcpclient->flags.reconnect_on_fail)
	{
		/* Will close socket and cancel any pending events of ev_tcpclient->socket_fd, including the close event */
		if ( ev_tcpclient->socket_fd >= 0)
		{
			EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
			ev_tcpclient->socket_fd = -1;
		}

		/* Destroy read and write buffers */
		CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

		ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;
		ev_tcpclient->socket_fd		= -1;

		/* Schedule RECONNECT timer */
		ev_tcpclient->timers.reconnect_on_fail_id =	EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_tcpclient->retry_times.reconnect_on_fail_ms > 0) ? ev_tcpclient->retry_times.reconnect_on_fail_ms : COMM_TCP_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
				CommEvTCPClientReconnectTimer, ev_tcpclient);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_on_fail_id);
	}

	return 0;
}
/**************************************************************************************************************************/
static int CommEvTCPClientSSLShutdown(int fd, int can_data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	char junk_buf[1024];
	int shutdown_state;
	int ssl_error;

	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Trying to shut down\n", ev_tcpclient->socket_fd);

	/* EOF set from PEER_SIDE, bail out */
	if ((kq_fd) && (kq_fd->flags.so_read_eof || kq_fd->flags.so_write_eof))
	{
		ev_tcpclient->flags.socket_eof = 1;
		goto do_shutdown;
	}

	/* Too many shutdown retries, bail out */
	if (ev_tcpclient->ssldata.ssl_shutdown_trycount++ > 10)
		goto do_shutdown;

	/* Not yet initialized, bail out */
	if (!SSL_is_init_finished(ev_tcpclient->ssldata.ssl_handle))
		goto do_shutdown;

	/* Clear SSL error queue and invoke shutdown */
	ERR_clear_error();
	shutdown_state = SSL_shutdown(ev_tcpclient->ssldata.ssl_handle);

	/* Check shutdown STATE - https://www.openssl.org/docs/ssl/SSL_shutdown.html */
	switch(shutdown_state)
	{
	/* The shutdown was not successful because a fatal error occurred either at the protocol level or a connection failure occurred. It can also occur if action is need to continue
	 * the operation for non-blocking BIOs. Call SSL_get_error(3) with the return value ret to find out the reason. */
	case -1:
	{
		goto check_error;
	}

	/* The shutdown is not yet finished. Call SSL_shutdown() for a second time, if a bidirectional shutdown shall be performed. The output of SSL_get_error(3) may
	 * be misleading, as an erroneous SSL_ERROR_SYSCALL may be flagged even though no error occurred. */
	case 0:
	{
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
		return 0;
	}
	/*	The shutdown was successfully completed. The ``close notify'' alert was sent and the peer's ``close notify'' alert was received. */
	case 1:
	default:
		goto do_shutdown;

	}

	/* TAG to examine SSL error and decide what to do */
	check_error:

	/* Grab error from SSL */
	ssl_error = SSL_get_error(ev_tcpclient->ssldata.ssl_handle, shutdown_state);

	switch (ssl_error)
	{
	case SSL_ERROR_WANT_READ:
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_WANT_READ\n", ev_tcpclient->socket_fd);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
		return 0;
	}
	case SSL_ERROR_WANT_WRITE:
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_WANT_WRITE\n", ev_tcpclient->socket_fd);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
		return 0;
	}
	case SSL_ERROR_ZERO_RETURN:
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SSL_ERROR_ZERO_RETURN\n", ev_tcpclient->socket_fd);
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
		return 0;
	}

	case SSL_ERROR_SYSCALL:
	case SSL_ERROR_SSL:
	default:
		goto do_shutdown;
	}

	return 0;

	do_shutdown:

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Shutting down\n", ev_tcpclient->socket_fd);

	/* Dispatch event before destroying IO structures to give a chance for the operator to use it */
	if (((ev_tcpclient->flags.socket_eof) || (ev_tcpclient->flags.ssl_fatal_error)) && (!ev_tcpclient->flags.close_request))
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, 0, thrd_id, COMM_CLIENT_EVENT_CLOSE);

	/*  Reset shutdown retry count and flags */
	ev_tcpclient->ssldata.ssl_shutdown_trycount = 0;
	ev_tcpclient->flags.ssl_shuting_down 		= 0;

	/* Either destroy or disconnect client, as requested by operator flags */
	COMM_EV_TCP_CLIENT_INTERNAL_FINISH(ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientEventSSLRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);

	char read_buf[COMM_TCP_SSL_READ_BUFFER_SZ];
	int ssl_bytes_read;
	int data_read;
	int ssl_error;

	/* Nothing to read, bail out */
	if (0 == can_read_sz)
		return 0;

	/* Clear libSSL errors and read from SSL tunnel */
	ERR_clear_error();
	ssl_bytes_read = SSL_read(ev_tcpclient->ssldata.ssl_handle, &read_buf, sizeof(read_buf) - 1);

	/* Touch RAW-SIDE statistics */
	ev_tcpclient->statistics.total[COMM_CURRENT].packet_rx	+= 1;

	/* Check errors */
	if (ssl_bytes_read <= 0)
	{
		ssl_error = SSL_get_error(ev_tcpclient->ssldata.ssl_handle, ssl_bytes_read);

		switch (ssl_error)
		{
		case SSL_ERROR_WANT_READ:
		{
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);
			return 0;
		}
		case SSL_ERROR_WANT_WRITE:
		{
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);
			return 0;
		}
		/* Receive peer close notification, move to shutdown */
		case SSL_ERROR_NONE:
		case SSL_ERROR_ZERO_RETURN:
		{
			EvKQBaseSetEvent(ev_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);
			return 0;
		}

		case SSL_ERROR_SYSCALL:
		default:
		{
			/* Mark flags as fatal error */
			ev_tcpclient->flags.ssl_fatal_error = 1;

			/* Trigger the SSL shutdown mechanism (post-2008) */
			CommEvTCPClientSSLShutdownBegin(ev_tcpclient);
			return can_read_sz;
		}

		}
	}
	/* Read into operator defined structure */
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - SUCCESS READING [%d] bytes - SZ [%d]\n",
				ev_tcpclient->socket_fd, ssl_bytes_read, can_read_sz);

		data_read = CommEvTCPClientProcessBuffer(ev_tcpclient, can_read_sz, thrd_id,  (char *)&read_buf, ssl_bytes_read);

		/* We are CLOSED, bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return data_read;

//		/* NON fatal error while reading this FD, reschedule and leave */
//		if (-1 == data_read)
//		{
//			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - Non fatal error while trying to read [%d] bytes\n", fd, data_read);
//		}

		/* Touch SSL-SIDE and RAW-SIDE statistics */
		if (data_read > 0)
		{
			ev_tcpclient->statistics.total[COMM_CURRENT].ssl_byte_rx	+= ssl_bytes_read;
			ev_tcpclient->statistics.total[COMM_CURRENT].byte_rx		+= ssl_bytes_read;
		}
	}

	/* Reschedule read event - Upper layers could have closed this socket, so just RESCHEDULE READ if we are still ONLINE */
	if ( ((!kq_fd->flags.closed) && (!kq_fd->flags.closing)) && (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED))
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLRead, ev_tcpclient);

	/* SSL read must return zero so lower layers do not get tricked by missing bytes in kernel */
	return 0;
}
/**************************************************************************************************************************/
static int CommEvTCPClientEventSSLWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOReq *cur_aio_req;
	int ssl_bytes_written;
	int ssl_error;

	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	int total_ssl_bytes_written		= 0;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);

	/* Client is going down, bail out */
	if (can_write_sz <= 0)
		return 0;

	assert(ev_tcpclient->ssldata.ssl_handle);

	/* Grab aio_req unit */
	cur_aio_req	= EvAIOReqQueueDequeue(&ev_tcpclient->iodata.write_queue);

	/* Nothing to write, bail out */
	if (!cur_aio_req)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Empty WRITE_LIST.. STOP\n",	fd);

		/* Reset pending write flag */
		ev_tcpclient->flags.pending_write = 0;
		return total_ssl_bytes_written;
	}

	/* Touch statistics */
	ev_tcpclient->statistics.total[COMM_CURRENT].packet_tx	+= 1;

	/* Clear libSSL errors and write it to SSL tunnel */
	ERR_clear_error();
	ssl_bytes_written = SSL_write(ev_tcpclient->ssldata.ssl_handle, cur_aio_req->data.ptr, cur_aio_req->data.size);

	/* Failed writing to SSL tunnel */
	if (ssl_bytes_written <= 0)
	{
		/* Grab SSL error to process */
		ssl_error = SSL_get_error(ev_tcpclient->ssldata.ssl_handle, ssl_bytes_written);

		/* Push AIO_REQ queue back for writing and set pending write flag */
		EvAIOReqQueueEnqueueHead(&ev_tcpclient->iodata.write_queue, cur_aio_req);
		ev_tcpclient->flags.pending_write = 1;

		/* Decide based on SSL error */
		switch (ssl_error)
		{
		case SSL_ERROR_WANT_READ:
		{

			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLWrite, ev_tcpclient);
			return 0;
		}
		case SSL_ERROR_WANT_WRITE:
		{
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLWrite, ev_tcpclient);
			return 0;
		}

		/* Receive peer close notification, move to shutdown */
		case SSL_ERROR_NONE:
		case SSL_ERROR_ZERO_RETURN:
		{
			EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLWrite, ev_tcpclient);
			return 0;
		}

		case SSL_ERROR_SYSCALL:
		default:
		{
			/* Mark flags as fatal error and pending write */
			ev_tcpclient->flags.ssl_fatal_error = 1;

			/* Trigger the SSL shutdown mechanism (post-2008) */
			CommEvTCPClientSSLShutdownBegin(ev_tcpclient);
			return 0;
		}
		}

		return 0;
	}

	/* Touch RAW and SSL statistics */
	ev_tcpclient->statistics.total[COMM_CURRENT].byte_tx		+= cur_aio_req->data.size;
	ev_tcpclient->statistics.total[COMM_CURRENT].ssl_byte_tx	+= ssl_bytes_written;
	total_ssl_bytes_written										+= ssl_bytes_written;

	/* Invoke notification CALLBACKS if not CLOSE_REQUEST and then destroy AIO REQ */
	if (!ev_tcpclient->flags.close_request)
		EvAIOReqInvokeCallBacks(cur_aio_req, 1, fd, ssl_bytes_written, thrd_id, ev_tcpclient);
	EvAIOReqDestroy(cur_aio_req);

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return can_write_sz;

	/* Reschedule write event */
	if (!EvAIOReqQueueIsEmpty(&ev_tcpclient->iodata.write_queue))
	{
		/* SET pending write flag */
		ev_tcpclient->flags.pending_write = 1;
		EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventSSLWrite, ev_tcpclient);

		return can_write_sz;
	}
	/* Write list is empty */
	else
	{
		/* Reset pending write flag */
		ev_tcpclient->flags.pending_write = 0;

		/* Upper layers requested to close after writing all */
		if (ev_tcpclient->flags.close_request)
		{
			KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Upper layer set CLOSE_REQUEST, write buffer is empty - [%s]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->flags.destroy_after_close ? "DESTROYING" : "CLOSING");

			/* Trigger the SSL shutdown mechanism (post-2008) */
			CommEvTCPClientSSLShutdownBegin(ev_tcpclient);
			return can_write_sz;
		}

		return can_write_sz;
	}

	return can_write_sz;
}
/**************************************************************************************************************************/
static int CommEvTCPClientRatesCalculateTimer(int timer_id, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient = cb_data;

	//KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - TIMER_ID [%d] - Will calculate rates at [%p]\n", ev_tcpclient->socket_fd, timer_id, ev_tcpclient);

	if (ev_tcpclient->socket_fd < 0)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_CRITICAL, LOGCOLOR_RED, "FD [%d] - TIMER_ID [%d] - CLIENT IS GONE!! [%p]\n", ev_tcpclient->socket_fd, timer_id, ev_tcpclient);
		abort();
	}

	/* Calculate read rates */
	if (ev_tcpclient->flags.calculate_datarate)
	{
		CommEvStatisticsRateCalculate(ev_tcpclient->kq_base, &ev_tcpclient->statistics, ev_tcpclient->socket_fd, COMM_RATES_READ);
		CommEvStatisticsRateCalculate(ev_tcpclient->kq_base, &ev_tcpclient->statistics, ev_tcpclient->socket_fd, COMM_RATES_WRITE);
		CommEvStatisticsRateCalculate(ev_tcpclient->kq_base, &ev_tcpclient->statistics, ev_tcpclient->socket_fd, COMM_RATES_USER);

		/* Reschedule DATARATE CALCULATE TIMER timer */
		ev_tcpclient->timers.calculate_datarate_id = EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_tcpclient->retry_times.calculate_datarate_ms > 0) ? ev_tcpclient->retry_times.calculate_datarate_ms : COMM_TCP_CLIENT_CALCULATE_DATARATE_DEFAULT_MS),
				CommEvTCPClientRatesCalculateTimer, ev_tcpclient);
	}
	else
		ev_tcpclient->timers.calculate_datarate_id = -1;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientReconnectTimer(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient = cb_data;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Reconnect timer at ID [%d]\n", ev_tcpclient->socket_fd, timer_id);

	/* Disable timer ID on CLIENT */
	if (timer_id == ev_tcpclient->timers.reconnect_after_close_id)
		ev_tcpclient->timers.reconnect_after_close_id = -1;
	else if (timer_id == ev_tcpclient->timers.reconnect_after_timeout_id)
		ev_tcpclient->timers.reconnect_after_timeout_id = -1;
	else if (timer_id == ev_tcpclient->timers.reconnect_on_fail_id)
		ev_tcpclient->timers.reconnect_on_fail_id = -1;

	/* Try to reconnect */
	CommEvTCPClientReconnect(ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientAsyncConnect(CommEvTCPClient *ev_tcpclient)
{
	int op_status;
	EvKQBase *ev_base			= ev_tcpclient->kq_base;
	EvBaseKQFileDesc *kq_fd		= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);
	struct sockaddr* dst_addr 	= (struct sockaddr*)&ev_tcpclient->dst_addr;

	/* Connect new_socket */
//	if (ev_tcpclient->cfg.flags.unix_socket)
	if (dst_addr->sa_family == AF_INET6)
		op_status 				= connect(ev_tcpclient->socket_fd, dst_addr, sizeof(struct sockaddr_in6));
	else if (dst_addr->sa_family == AF_UNIX)
		op_status 				= connect(ev_tcpclient->socket_fd, dst_addr, sizeof(struct sockaddr_un));
	else
		op_status 				= connect(ev_tcpclient->socket_fd, dst_addr, sizeof(struct sockaddr_in));

//	op_status 					= connect(ev_tcpclient->socket_fd, dst_addr, ev_tcpclient->dst_addr.ss_len);

	/* Success connecting */
	if (0 == op_status)
		goto conn_success;

	switch(errno)
	{
	/* Already connected, bail out */
	case EALREADY:
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Already trying to connect!\n", ev_tcpclient->socket_fd);
		return 1;
	}
	case EINPROGRESS:
	case EINTR:
		goto conn_success;

	default:

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - connect SYSCALL returned [%d] - ERRNO [%d] - FAMILY [%d]\n",
				ev_tcpclient->socket_fd, op_status, errno, dst_addr->sa_family);

		/* Failed connecting */
		ev_tcpclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_CONNECT_SYSCALL;

		/* Dispatch the internal event */
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatch CONNECT\n", ev_tcpclient->socket_fd);
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, 0, -1, COMM_CLIENT_EVENT_CONNECT);

		/* Client has been destroyed, bail out */
		if ((kq_fd) && ((kq_fd->flags.closed) || (kq_fd->flags.closing)))
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Will STOP, FD is CLOSED\n", ev_tcpclient->socket_fd);
			return 0;
		}

		/* Upper layers want a full DESTROY if CONNECTION FAILS */
		if (ev_tcpclient->flags.destroy_after_connect_fail)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Will DESTROY, as requested\n", ev_tcpclient->socket_fd);
			CommEvTCPClientDestroy(ev_tcpclient);
		}
		/* Upper layers want a reconnect retry if CONNECTION FAILS */
		else if (ev_tcpclient->flags.reconnect_on_fail)
		{
			/* Set client state */
			ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;

			/* Will close socket and cancel any pending events of ev_tcpclient->socket_fd, including the close event */
			if ( ev_tcpclient->socket_fd >= 0)
			{
				/* Reset this client SOCKET_FD */
				KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Will reset SOCKET, to recycle\n", ev_tcpclient->socket_fd);
				CommEvTCPClientResetFD(ev_tcpclient);
			}

			/* Destroy read and write buffers */
			CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

			/* Schedule RECONNECT timer */
			ev_tcpclient->timers.reconnect_on_fail_id =	EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
					((ev_tcpclient->retry_times.reconnect_on_fail_ms > 0) ? ev_tcpclient->retry_times.reconnect_on_fail_ms : COMM_TCP_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
					CommEvTCPClientReconnectTimer, ev_tcpclient);

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_on_fail_id);
		}

		return 0;
	}

	/* Success beginning connection */
	conn_success:

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Connecting, scheduling WRITE_EV\n", ev_tcpclient->socket_fd);

	/* Schedule WRITE event to catch ASYNC connect */
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientEventConnect, ev_tcpclient);

	/* Set client state */
	ev_tcpclient->socket_state = COMM_CLIENT_STATE_CONNECTING;

	/* If upper layers want CONNECT_TIMEOUT, set timeout on WRITE event on low level layer */
	if (ev_tcpclient->timeout.connect_ms > 0)
		EvKQBaseTimeoutSet(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, KQ_CB_TIMEOUT_WRITE, ev_tcpclient->timeout.connect_ms, CommEvTCPClientTimerConnectTimeout, ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientTimerConnectTimeout(int fd, int timeout_type, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Connection TIMEDOUT\n", ev_tcpclient->socket_fd);

	/* Set client state */
	ev_tcpclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_TIMEOUT;

	/* Cancel and close any pending event associated with TCP client FD, to avoid double notification */
	EvKQBaseClearEvents(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
	EvKQBaseTimeoutClearAll(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	/* Dispatch internal event */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatch CONNECT\n", ev_tcpclient->socket_fd);
	CommEvTCPClientEventDispatchInternal(ev_tcpclient, timeout_type, thrd_id, COMM_CLIENT_EVENT_CONNECT);

	/* Already destroyed, bail out */
	if ((kq_fd) && ((kq_fd->flags.closed) || (kq_fd->flags.closing)))
		return 0;

	/* Disconnect any pending stuff - Will schedule reconnect if upper layers asked for it */
	CommEvTCPClientInternalDisconnect(ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
static void CommEvTCPClientInternalDisconnect(CommEvTCPClient *ev_tcpclient)
{
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Clean up\n", ev_tcpclient->socket_fd);

	/* Will close socket and cancel any pending events of ev_tcpclient->socket_fd, including the close event */
	if ( ev_tcpclient->socket_fd >= 0)
		EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	/* Destroy peer X509 certificate information */
	if (ev_tcpclient->ssldata.x509_cert)
	{
		X509_free(ev_tcpclient->ssldata.x509_cert);
		ev_tcpclient->ssldata.x509_cert = NULL;
	}

	/* Cancel pending DNS request */
	if (ev_tcpclient->dnsreq_id	> -1)
	{
		//printf("CommEvTCPClientDestroy - Will cancel pending request\n");
		CommEvDNSCancelPendingRequest(ev_tcpclient->resolv_base, ev_tcpclient->dnsreq_id);
		ev_tcpclient->dnsreq_id = -1;
	}

	/* Set client STATE */
	ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;
	ev_tcpclient->socket_fd		= -1;

	/* Cancel any possible timer and destroy read and write buffers */
	CommEvTCPClientTimersCancelAll(ev_tcpclient);
	CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

	/* If client want to be reconnected automatically after a CONN_TIMEOUT, honor it */
	if ( (COMM_CLIENT_STATE_CONNECT_FAILED_TIMEOUT == ev_tcpclient->socket_state) && ev_tcpclient->flags.reconnect_on_timeout)
	{
		ev_tcpclient->timers.reconnect_after_timeout_id = EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_tcpclient->retry_times.reconnect_after_timeout_ms > 0) ? ev_tcpclient->retry_times.reconnect_after_timeout_ms : COMM_TCP_CLIENT_RECONNECT_TIMEOUT_DEFAULT_MS),
				CommEvTCPClientReconnectTimer, ev_tcpclient);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Timed out! Auto reconnect in [%d] ms at timer_id [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->retry_times.reconnect_after_timeout_ms, ev_tcpclient->timers.reconnect_after_timeout_id);
	}
	/* If client want to be reconnected automatically after an EOF, honor it */
	else if (ev_tcpclient->flags.reconnect_on_close)
	{
		ev_tcpclient->timers.reconnect_after_close_id =	EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_tcpclient->retry_times.reconnect_after_close_ms > 0) ? ev_tcpclient->retry_times.reconnect_after_close_ms : COMM_TCP_CLIENT_RECONNECT_CLOSE_DEFAULT_MS),
				CommEvTCPClientReconnectTimer, ev_tcpclient);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Unexpectedly closed! Auto reconnect in [%d] ms at timer_id [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->retry_times.reconnect_after_close_ms, ev_tcpclient->timers.reconnect_after_close_id);
	}

	return;
}
/**************************************************************************************************************************/
//static void CommEvTCPClientMemBufferWriteFinish(int fd, int write_sz, int thrd_id, void *cb_data, void *base_ptr)
//{
//	MemBuffer *data_mb = cb_data;
//	MemBufferDestroy(data_mb);
//
//	return;
//}
/**************************************************************************************************************************/
static int CommEvTCPClientCheckIfNeedDNSLookup(CommEvTCPClient *ev_tcpclient)
{
	int dots_found;
	int state;
	int i;

	if (ev_tcpclient->cfg.flags.unix_socket)
		return 0;

	enum
	{
		HOSTNAME_IS_IP,
		HOSTNAME_IS_FQDN,
	};

	/* Check if request URL is FQDN or IP */
	for (i = 0, dots_found = 0, state = HOSTNAME_IS_IP; ev_tcpclient->cfg.hostname[i] != '\0'; i++)
	{
		/* Seen a dot on domain */
		if ('.' == ev_tcpclient->cfg.hostname[i] )
			dots_found++;

		/* An IP can not have more than three dot chars, set as FQDN and stop */
		if (dots_found > 3)
		{
			state = HOSTNAME_IS_FQDN;
			break;
		}

		/* Not a dot, not a number, not a port char, set to FQDN and bail out */
		if ((!isdigit(ev_tcpclient->cfg.hostname[i])) && (ev_tcpclient->cfg.hostname[i] != '.') && (ev_tcpclient->cfg.hostname[i] != ':'))
		{
			state = HOSTNAME_IS_FQDN;
			break;
		}

	}

	/* Set flags and return */
	if  (HOSTNAME_IS_FQDN == state)
	{
		//KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "HOSTNAME_IS_FQDN\n");
		ev_tcpclient->flags.need_dns_lookup = 1;
		return 1;
	}
	else
	{
		//printf("CommEvTCPClientCheckIfNeedDNSLookup - HOSTNAME_IS_IP\n");
		ev_tcpclient->flags.need_dns_lookup = 0;
		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
static void CommEvTCPClientDNSResolverCB(void *ev_dns_ptr, void *req_cb_data, void *a_reply_ptr, int code)
{
	int i;
	int lower_seen_ttl				= 0;
	DNSAReply *a_reply				= a_reply_ptr;
	CommEvTCPClient *ev_tcpclient	= req_cb_data;
	EvKQBase *ev_base				= ev_tcpclient->kq_base;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN,
			"DNS_SLOT [%d] - Host [%s] - IP resolved - CODE [%d] - [%d] ADDRESS\n", ev_tcpclient->dnsreq_id, ev_tcpclient->cfg.hostname, code, a_reply->ip_count);

	/* Cancel DNS_REQ_ID */
	ev_tcpclient->dnsreq_id 		= -1;

	/* Resolve succeeded OK, begin ASYNC connection procedure */
	if (a_reply->ip_count > 0)
	{
		for (i = 0; i < a_reply->ip_count; i++)
		{
//			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Index [%d] - IP_ADDR [%s] - TTL [%d]\n", i, inet_ntoa(a_reply->ip_arr[i].addr), a_reply->ip_arr[i].ttl);

			/* First iteration, initialize values */
			if (0 == i)
				lower_seen_ttl = a_reply->ip_arr[i].ttl;
			/* There is a smaller TTL, keep it */
			else if (a_reply->ip_arr[i].ttl < lower_seen_ttl)
				lower_seen_ttl = a_reply->ip_arr[i].ttl;
		}

		/* Save DNS reply */
		memcpy(&ev_tcpclient->dns.a_reply, a_reply, sizeof(DNSAReply));

		/* Set expire based on lower TTL seen on array */
		ev_tcpclient->dns.expire_ts = ev_tcpclient->kq_base->stats.cur_invoke_ts_sec + lower_seen_ttl;
		ev_tcpclient->dns.cur_idx	= 0;

		/* Load destination IP based on A reply */
		CommEvTCPClientAddrInitFromAReply(ev_tcpclient);

		/* Begin ASYNC connection */
		CommEvTCPClientAsyncConnect(ev_tcpclient);

		return;
	}

	/* Resolve failed, notify upper layers */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FAILED [%d]\n", a_reply->ip_count);

	/* Set flags */
	ev_tcpclient->socket_state	= COMM_CLIENT_STATE_CONNECT_FAILED_DNS;
	ev_tcpclient->dns.expire_ts = -1;

	/* Dispatch internal event */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatch CONNECT\n", ev_tcpclient->socket_fd);
	CommEvTCPClientEventDispatchInternal(ev_tcpclient, 0, -1, COMM_CLIENT_EVENT_CONNECT);

	/* Already destroyed, bail out */
	if ((kq_fd) && ((kq_fd->flags.closed) || (kq_fd->flags.closing)))
		return;

	/* Upper layers want a full DESTROY if CONNECTION FAILS */
	if (ev_tcpclient->flags.destroy_after_connect_fail)
	{
		CommEvTCPClientDestroy(ev_tcpclient);
		return;
	}
	/* Upper layers want a reconnect retry if CONNECTION FAILS */
	else if (ev_tcpclient->flags.reconnect_on_fail)
	{
		/* Set client state */
		ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;

		/* Will close socket and cancel any pending events of ev_tcpclient->socket_fd, including the close event */
		if ( ev_tcpclient->socket_fd >= 0)
		{
			/* Reset this client SOCKET_FD */
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Will reset SOCKET, to recycle\n", ev_tcpclient->socket_fd);
			CommEvTCPClientResetFD(ev_tcpclient);
		}

		/* Destroy read and write buffers */
		CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

		/* Schedule RECONNECT timer */
		ev_tcpclient->timers.reconnect_on_fail_id =	EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE,
				((ev_tcpclient->retry_times.reconnect_on_fail_ms > 0) ? ev_tcpclient->retry_times.reconnect_on_fail_ms : COMM_TCP_CLIENT_RECONNECT_FAIL_DEFAULT_MS),
				CommEvTCPClientReconnectTimer, ev_tcpclient);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_on_fail_id);

	}

	return;
}
/**************************************************************************************************************************/
static int CommEvTCPClientProcessBuffer(CommEvTCPClient *ev_tcpclient, int read_sz, int thrd_id, char *read_buf, int read_buf_sz)
{
	EvKQBase *ev_base			= ev_tcpclient->kq_base;
	EvBaseKQFileDesc *kq_fd		= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);
	MemBuffer *transformed_mb	= NULL;
	int data_read				= 0;

	char *transform_ptr;
	long data_read_cur;

	/* Read into operator defined structure */
	switch (ev_tcpclient->read_mthd)
	{
	/*********************************************************************/
	case COMM_CLIENT_READ_MEMBUFFER:
	{
		/* Create a new read_buffer object */
		if (!ev_tcpclient->iodata.read_buffer)
		{
			/* Check if have partial buffer, and as read is empty, just switch pointers and set partial to NULL */
			if (ev_tcpclient->iodata.partial_read_buffer)
			{
				ev_tcpclient->iodata.read_buffer			= ev_tcpclient->iodata.partial_read_buffer;
				ev_tcpclient->iodata.partial_read_buffer 	= NULL;
			}
			/* Create a new read buffer */
			else
				ev_tcpclient->iodata.read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (read_sz + 1));
		}

		/* There is a partial buffer left, copy it back to read buffer */
		if (ev_tcpclient->iodata.partial_read_buffer)
		{
			MemBufferAdd(ev_tcpclient->iodata.read_buffer, MemBufferDeref(ev_tcpclient->iodata.partial_read_buffer), MemBufferGetSize(ev_tcpclient->iodata.partial_read_buffer));
			MemBufferClean(ev_tcpclient->iodata.partial_read_buffer);
		}

		/* Absorb data from READ_BUFFER */
		if (read_buf_sz > 0)
		{
			/* Allow upper layers to perform data transformation */
			transformed_mb		= EvAIOReqTransform_ReadData(&ev_tcpclient->transform, read_buf, read_buf_sz);

			/* No transformed MB, append read_buf */
			if (!transformed_mb)
			{
				/* Add Read Buffer into read_buffer */
				MemBufferAdd(ev_tcpclient->iodata.read_buffer, read_buf, read_buf_sz);
			}

			data_read = read_buf_sz;
		}
		/* Grab data from FD directly into read_buffer, zero-copy */
		else
		{
			/* Get current size */
			data_read_cur 	= MemBufferGetSize(ev_tcpclient->iodata.read_buffer);

			/* Read data from FD */
			data_read 		= MemBufferAppendFromFD(ev_tcpclient->iodata.read_buffer, read_sz, ev_tcpclient->socket_fd, (ev_tcpclient->flags.peek_on_read ? MSG_PEEK : 0));

			/* Read OK */
			if (data_read > 0)
			{
				transform_ptr		= MemBufferDeref(ev_tcpclient->iodata.read_buffer);
				transform_ptr 		= transform_ptr + data_read_cur;

				/* Allow upper layers to perform data transformation */
				transformed_mb		= EvAIOReqTransform_ReadData(&ev_tcpclient->transform, transform_ptr, data_read);

				/* Correct size to add transformed MB */
				if (transformed_mb)
					ev_tcpclient->iodata.read_buffer->size = data_read_cur;
			}
			/* Failed reading FD */
			else
			{
//				/* NON_FATAL error */
//				if ((!kq_fd->flags.so_read_eof) && (errno == EINTR || errno == EAGAIN))
//					return -1;
//
//				KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Error reading [%d] of [%d] bytes - ERRNO [%d]\n",
//						ev_tcpclient->socket_fd, data_read, read_sz, errno);
				return -1;
			}
		}

		/* Data has been transformed, add to READ_BUFFER */
		if (transformed_mb)
		{
			/* Add Read Buffer into read_buffer */
			MemBufferAdd(ev_tcpclient->iodata.read_buffer, MemBufferDeref(transformed_mb), MemBufferGetSize(transformed_mb));
			MemBufferDestroy(transformed_mb);
		}

		break;
	}
	/*********************************************************************/
	case COMM_CLIENT_READ_MEMSTREAM:
	{
		/* Create a new read_stream object */
		if (!ev_tcpclient->iodata.read_stream)
		{
			if (ev_base->flags.mt_engine)
				ev_tcpclient->iodata.read_stream = MemStreamNew(8092, MEMSTREAM_MT_SAFE);
			else
				ev_tcpclient->iodata.read_stream = MemStreamNew(8092, MEMSTREAM_MT_UNSAFE);
		}

		/* Absorb data from READ_BUFFER */
		if (read_buf_sz > 0)
		{
			/* Add SSL buffer into plain read_stream */
			MemStreamWrite(ev_tcpclient->iodata.read_stream, read_buf, read_buf_sz);
			data_read = read_buf_sz;
		}
		/* Grab data from FD directly into read_stream, zero-copy */
		else
			data_read = MemStreamGrabDataFromFD(ev_tcpclient->iodata.read_stream, read_sz, ev_tcpclient->socket_fd);

		/* Grab data from FD directly into read_stream, zero-copy */
		data_read = MemStreamGrabDataFromFD(ev_tcpclient->iodata.read_stream, read_sz, ev_tcpclient->socket_fd);

		break;
	}
	/*********************************************************************/
	}

	/* Upper layers asked for self_sync, so invoke it */
	if (ev_tcpclient->cfg.flags.self_sync)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - SELF_SYNC with [%d] bytes\n", ev_tcpclient->socket_fd, read_sz);
		CommEvTCPClientSelfSyncReadBuffer(ev_tcpclient, read_sz, thrd_id);
	}
	/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatching internal event for [%d] bytes on WIRE\n", ev_tcpclient->socket_fd, read_sz);

		/* Dispatch internal event */
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, read_sz, thrd_id, COMM_CLIENT_EVENT_READ);

	}

	return data_read;
}
/**************************************************************************************************************************/
static int CommEvTCPClientSelfSyncReadBuffer(CommEvTCPClient *ev_tcpclient, int orig_read_sz, int thrd_id)
{
	EvKQBase *ev_base			= ev_tcpclient->kq_base;

	char *partial_ptr;
	char *read_buffer_ptr;
	char *request_str_ptr;
	int request_str_sz;
	int remaining_sz;
	int i, j;

	char *token_str				= (char*)&ev_tcpclient->cfg.self_sync.token_str_buf;
	int max_buffer_sz			= ev_tcpclient->cfg.self_sync.max_buffer_sz;
	int token_sz				= ev_tcpclient->cfg.self_sync.token_str_sz;
	int token_found				= 0;

	/* Flag not set, or buffer is empty, bail out */
	if ((!ev_tcpclient->cfg.flags.self_sync) || (!ev_tcpclient->iodata.read_buffer))
		return 0;

	/* Make sure buffer if NULL terminated before dealing with it as a string */
	MemBufferPutNULLTerminator(ev_tcpclient->iodata.read_buffer);

	/* Get information about original string encoded request array */
	request_str_ptr	= MemBufferDeref(ev_tcpclient->iodata.read_buffer);
	request_str_sz	= MemBufferGetSize(ev_tcpclient->iodata.read_buffer);

	/* Sanity check */
	if (request_str_sz < token_sz)
		return 0;

	//printf("CommEvTCPClientSelfSyncReadBuffer - REQ_SZ [%d] - TOKEN_SZ [%d]\n", request_str_sz, token_sz);
	//EvKQBaseLogHexDump(request_str_ptr, request_str_sz, 8, 4);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_GREEN, "FD [%d] - Searching for token [%u] with [%d] bytes\n",
			ev_tcpclient->socket_fd, token_str[0], token_sz);

	/* Start searching the buffer */
	for (j = 0, i = (request_str_sz); i >= 0; i--, j++)
	{
		/* Found finish of token, compare full token versus buffer */
		if ( ((j >= token_sz) && (request_str_ptr[i] == token_str[0])) && (!memcmp(&request_str_ptr[i], token_str, token_sz)) )
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_YELLOW, "FD [%d] - Token found at [%d] - Buffer size is [%d]\n",
					ev_tcpclient->socket_fd, j, request_str_sz);

			/* Found token */
			token_found = 1;
			break;
		}

		continue;
	}

	/* Token bas been found */
	if (token_found)
	{
		/* Calculate remaining size past token */
		remaining_sz	= (request_str_sz - i - token_sz);

		/* There is data past token, save it into partial buffer */
		if (remaining_sz > 0)
		{
			/* Create a new partial_read_buffer object */
			if (!ev_tcpclient->iodata.partial_read_buffer)
				ev_tcpclient->iodata.partial_read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (remaining_sz + 1));

			/* Point to remaining data */
			read_buffer_ptr						= MemBufferOffsetDeref(ev_tcpclient->iodata.read_buffer, (request_str_sz - remaining_sz));
			ev_tcpclient->iodata.read_buffer->size		-= remaining_sz;

			/* Save left over into partial buffer and NULL terminate read buffer to new size */
			MemBufferAdd(ev_tcpclient->iodata.partial_read_buffer, read_buffer_ptr, remaining_sz);
			MemBufferPutNULLTerminator(ev_tcpclient->iodata.read_buffer);

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_GREEN, "FD [%d] - Read buffer adjusted to [%d] bytes - Partial holds [%d] remaining bytes\n",
					ev_tcpclient->socket_fd, MemBufferGetSize(ev_tcpclient->iodata.read_buffer), MemBufferGetSize(ev_tcpclient->iodata.partial_read_buffer));

		}
	}

	/* Dispatch upper layer read event if token has been found or if we reached our maximum allowed buffer size */
	if ( (token_found) || ((max_buffer_sz > 0) && (request_str_sz >= max_buffer_sz)) )
	{
		char *aux_ptr00 = MemBufferDeref(ev_tcpclient->iodata.read_buffer);
		char *aux_ptr01 = MemBufferDeref(ev_tcpclient->iodata.partial_read_buffer);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_CYAN, "FD [%d] - MAX_BUF [%d] - READ_BUF [%d]-[%s] - PARTIAL [%d]-[%s]\n",
				ev_tcpclient->socket_fd, max_buffer_sz, MemBufferGetSize(ev_tcpclient->iodata.read_buffer), aux_ptr00 ? aux_ptr00 : "NULL",
						MemBufferGetSize(ev_tcpclient->iodata.partial_read_buffer), aux_ptr01 ? aux_ptr01 : "NULL");

		/* Dispatch internal event */
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, orig_read_sz, thrd_id, COMM_CLIENT_EVENT_READ);
	}
	else
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO,  LOGCOLOR_YELLOW, "FD [%d] - READ_BUF [%d] bytes - TOKEN NOT FOUND, WILL NOT DISPATCH NOW\n",
				ev_tcpclient->socket_fd, MemBufferGetSize(ev_tcpclient->iodata.read_buffer));
	}

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientTimersCancelAll(CommEvTCPClient *ev_tcpclient)
{
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Will cancel timers - RE_AFTER_CLOSE [%d] -  RE_AFTER_TIMEOUT [%d] -  "
			"RE_AFTER_FAIL [%d] - CALCULATE [%d]\n", ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_after_close_id, ev_tcpclient->timers.reconnect_after_timeout_id,
			ev_tcpclient->timers.reconnect_on_fail_id, ev_tcpclient->timers.calculate_datarate_id);

	/* DELETE all pending timers */
	if (ev_tcpclient->timers.reconnect_after_close_id > -1)
		EvKQBaseTimerCtl(ev_tcpclient->kq_base, ev_tcpclient->timers.reconnect_after_close_id, COMM_ACTION_DELETE);

	if (ev_tcpclient->timers.reconnect_after_timeout_id > -1)
		EvKQBaseTimerCtl(ev_tcpclient->kq_base, ev_tcpclient->timers.reconnect_after_timeout_id, COMM_ACTION_DELETE);

	if (ev_tcpclient->timers.reconnect_on_fail_id > -1)
		EvKQBaseTimerCtl(ev_tcpclient->kq_base, ev_tcpclient->timers.reconnect_on_fail_id, COMM_ACTION_DELETE);

	if (ev_tcpclient->timers.calculate_datarate_id > -1)
		EvKQBaseTimerCtl(ev_tcpclient->kq_base, ev_tcpclient->timers.calculate_datarate_id, COMM_ACTION_DELETE);

	ev_tcpclient->timers.reconnect_after_close_id	= -1;
	ev_tcpclient->timers.reconnect_after_timeout_id	= -1;
	ev_tcpclient->timers.reconnect_on_fail_id		= -1;
	ev_tcpclient->timers.calculate_datarate_id		= -1;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientObjectDestroyCBH(void *kq_obj_ptr, void *cb_data)
{
	EvBaseKQObject *kq_obj			= kq_obj_ptr;
	CommEvTCPClient *ev_tcpclient	= kq_obj->obj.ptr;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "Invoked to destroy COMM_EV_TCP_CLIENT at [%p]\n", kq_obj->obj.ptr);

	/* Destroy and clean structure */
	CommEvTCPClientDestroy(ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientSSLShutdownJob(void *job, void *cbdata_ptr)
{
	CommEvTCPClient *ev_tcpclient	= cbdata_ptr;

	/* Reset shutdown JOB ID */
	ev_tcpclient->ssldata.shutdown_jobid = -1;

	/* Clean any pending TIMEOUT */
	EvKQBaseTimeoutClearAll(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	/* Now remove any READ/WRITE events, then remove DEFER and only then restore READ/WRITE events. This will avoid wandering events from being dispatched to SSL_SHUTDOWN */
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_WRITE, COMM_ACTION_DELETE, NULL, NULL);

	/* Cancel LOWER LEVEL DEFER_READ and DEFER_WRITE events */
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_DEFER_CHECK_READ, COMM_ACTION_DELETE, NULL, NULL);
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_DEFER_CHECK_WRITE, COMM_ACTION_DELETE, NULL, NULL);

	/* Redirect READ/WRITE events to SSL_SHUTDOWN */
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);
	EvKQBaseSetEvent(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPClientSSLShutdown, ev_tcpclient);

	/* Fire up SSL_shutdown sequence */
	ev_tcpclient->ssldata.ssl_shutdown_trycount = 0;
	CommEvTCPClientSSLShutdown(ev_tcpclient->socket_fd, 0, -1, ev_tcpclient, ev_tcpclient->kq_base);

	return 1;
}
/**************************************************************************************************************************/
