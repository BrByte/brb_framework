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

/* Timer events */
static EvBaseKQCBH CommEvTCPClientRatesCalculateTimer;
static EvBaseKQCBH CommEvTCPClientReconnectTimer;
static EvBaseKQCBH CommEvTCPClientTimerConnectTimeout;

/* DNS support */
static CommEvDNSResolverCBH CommEvTCPClientDNSResolverCB;
static void CommEvTCPClientStartFD(CommEvTCPClient *ev_tcpclient);

static void CommEvTCPClientAddrInitFromAReply(CommEvTCPClient *ev_tcpclient);
static int CommEvTCPClientAsyncConnect(CommEvTCPClient *ev_tcpclient);
static int CommEvTCPClientTimersCancelAll(CommEvTCPClient *ev_tcpclient);
static int CommEvTCPClientBindSource(CommEvTCPClient *ev_tcpclient);

static EvBaseKQObjDestroyCBH CommEvTCPClientObjectDestroyCBH;

/**************************************************************************************************************************/
CommEvTCPClient *CommEvTCPClientNew(EvKQBase *kq_base)
{
	CommEvTCPClient *ev_tcpclient;
	int op_status;

	ev_tcpclient							= calloc(1, sizeof(CommEvTCPClient));
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
//	int socket_fd;

	ev_tcpclient->socket_fd						= -1;

	/* Set default READ METHOD and READ PROTO, can be override by CONNECT function */
	ev_tcpclient->read_mthd 					= COMM_CLIENT_READ_MEMBUFFER;
	ev_tcpclient->cli_proto						= COMM_CLIENTPROTO_PLAIN;
	ev_tcpclient->socket_state					= COMM_CLIENT_STATE_DISCONNECTED;

	ev_tcpclient->dnsdata.req_id						= -1;
	ev_tcpclient->ssldata.shutdown_jobid		= -1;

	/* Initialize all timers */
	ev_tcpclient->timers.reconnect_id			= -1;
	ev_tcpclient->timers.calculate_datarate_id	= -1;

//	/* Create socket and set it to non_blocking */
//	if (ev_tcpclient->cfg.flags.unix_socket)
//		socket_fd = EvKQBaseSocketUNIXNew(kq_base);
//	else
//		socket_fd = EvKQBaseSocketTCPNew(kq_base);
//
//	/* Check if created socket is OK */
//	if (socket_fd < 0)
//		return COMM_CLIENT_FAILURE_SOCKET;

	ev_tcpclient->kq_base						= kq_base;
//	ev_tcpclient->socket_fd						= socket_fd;
	ev_tcpclient->cli_id_onpool					= cli_id_onpool;

	/* Populate KQ_BASE object structure */
	ev_tcpclient->kq_obj.code					= (ev_tcpclient->cfg.flags.unix_socket) ? EV_OBJ_UNIX_CLIENT : EV_OBJ_TCP_CLIENT;
	ev_tcpclient->kq_obj.obj.ptr				= ev_tcpclient;
	ev_tcpclient->kq_obj.obj.destroy_cbh		= CommEvTCPClientObjectDestroyCBH;
	ev_tcpclient->kq_obj.obj.destroy_cbdata		= NULL;

	/* Register KQ_OBJECT */
	EvKQBaseObjectRegister(kq_base, &ev_tcpclient->kq_obj);

	/* Set it to non_blocking and save it into newly allocated client */
	EvKQBaseSocketSetNonBlock(kq_base, ev_tcpclient->socket_fd);

	/* Set description */
	EvKQBaseFDDescriptionSetByFD(kq_base, ev_tcpclient->socket_fd, "BRB_EV_COMM - TCP_CLIENT");

	/* Initialize WRITE_QUEUE */
	EvAIOReqQueueInit(ev_tcpclient->kq_base, &ev_tcpclient->iodata.write_queue, 4096,
			(ev_tcpclient->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "FD [%d] - Init at [%p]\n", ev_tcpclient->socket_fd, ev_tcpclient);

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
	CommEvTCPClientShutdown(ev_tcpclient);

	/* Free WILLY */
	free(ev_tcpclient);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPClientShutdown(CommEvTCPClient *ev_tcpclient)
{
	/* Sanity check */
	if (!ev_tcpclient)
		return;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Cleaning at [%p]\n", ev_tcpclient->socket_fd, ev_tcpclient);

	/* Unregister object */
	EvKQBaseObjectUnregister(&ev_tcpclient->kq_obj);

	/* Release SSL related objects */
	CommEvTCPClientSSLDataClean(ev_tcpclient);

	/* Cancel pending DNS request */
	CommEvDNSDataCancelAndClean(&ev_tcpclient->dnsdata);

	/* Cancel any pending SSL SHUTDOWN JOB */
	EvKQJobsCtl(ev_tcpclient->kq_base, JOB_ACTION_DELETE, ev_tcpclient->ssldata.shutdown_jobid);
	ev_tcpclient->ssldata.shutdown_jobid = -1;

	/* Cancel any possible timer or events */
	CommEvTCPClientTimersCancelAll(ev_tcpclient);
	CommEvTCPClientEventCancelAll(ev_tcpclient);

	/* Destroy read and write buffers */
	CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

	/* Will close socket and cancel any pending events of socket_fd, including the close event */
	if (ev_tcpclient->socket_fd >= 0)
	{
		EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
		ev_tcpclient->socket_fd = -1;
	}

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
	strncpy((char*)&ev_tcpclient->cfg.hostname, ev_tcpclient_conf->hostname, sizeof(ev_tcpclient->cfg.hostname) - 1);

	if (ev_tcpclient_conf->ssl.crt_path_ptr)
		strncpy((char *)&ev_tcpclient->cfg.ssl.path_crt, ev_tcpclient_conf->ssl.crt_path_ptr, sizeof(ev_tcpclient->cfg.ssl.path_crt) - 1);

	if (ev_tcpclient_conf->ssl.key_path_ptr)
		strncpy((char *)&ev_tcpclient->cfg.ssl.path_key, ev_tcpclient_conf->ssl.key_path_ptr, sizeof(ev_tcpclient->cfg.ssl.path_key) - 1);

	if (ev_tcpclient_conf->ssl.ca_path_ptr)
		strncpy((char *)&ev_tcpclient->cfg.ssl.path_ca, ev_tcpclient_conf->ssl.ca_path_ptr, sizeof(ev_tcpclient->cfg.ssl.path_ca) - 1);

	/* Save a copy of HOSTNAME for TLS Server Name Indication */
	if (ev_tcpclient_conf->sni_hostname_str)
		strncpy((char*)&ev_tcpclient->cfg.sni_hostname, ev_tcpclient_conf->sni_hostname_str, sizeof(ev_tcpclient->cfg.sni_hostname) - 1);


	char *ca_path_ptr;
	char *crt_path_ptr;

	/* Clean up flags in case we are reusing this client context */
	memset(&ev_tcpclient->flags, 0, sizeof(ev_tcpclient->flags));

	/* Load client protocol and read method information */
	ev_tcpclient->addr_mthd 	= ev_tcpclient_conf->addr_mthd;
	ev_tcpclient->cli_proto 	= ev_tcpclient_conf->cli_proto;
	ev_tcpclient->read_mthd 	= ev_tcpclient_conf->read_mthd;

	/* Copy flags and info */
	ev_tcpclient->cfg.port 									= ev_tcpclient_conf->port;

	/* Load resolver base */
	ev_tcpclient->dnsdata.resolv_base						= ev_tcpclient_conf->resolv_base;
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

	/* Check if self sync is active, then grab token to check when buffer finish */
	if (ev_tcpclient_conf->self_sync.token_str)
	{
		/* Copy token into buffer and calculate token size */
		strncpy((char*)&ev_tcpclient->cfg.self_sync.token_str_buf, ev_tcpclient_conf->self_sync.token_str, sizeof(ev_tcpclient->cfg.self_sync.token_str_buf));
		ev_tcpclient->cfg.self_sync.token_str_sz = strlen(ev_tcpclient_conf->self_sync.token_str);

		/* Mark self sync as active for this server and save max buffer limit */
		ev_tcpclient->cfg.self_sync.max_buffer_sz		= ev_tcpclient_conf->self_sync.max_buffer_sz;
		ev_tcpclient->cfg.flags.self_sync				= 1;
	}

	/* Save a copy of SRC_ADDRESS if UPPER LAYERS supplied it */
	if (ev_tcpclient_conf->src_addr.ss_family != AF_UNSPEC)
	{
		memcpy(&ev_tcpclient->src_addr, &ev_tcpclient_conf->src_addr, sizeof(struct sockaddr_storage));
	}

	/* Check now if user sent us a HOSTNAME or a IP address */
	if (!ev_tcpclient->cfg.flags.unix_socket)
	{
		ev_tcpclient->dst_addr.ss_family 					= BrbIsValidIpToSockAddr((char *)&ev_tcpclient->cfg.hostname, &ev_tcpclient->dst_addr);
		ev_tcpclient->flags.need_dns_lookup 				= (ev_tcpclient->dst_addr.ss_family != AF_INET && ev_tcpclient->dst_addr.ss_family != AF_INET6) ? 1 : 0;
	//	ev_tcpclient->flags.need_dns_lookup 				= ev_tcpclient->cfg.flags.unix_socket ? 0 : CommEvDNSCheckIfNeedDNSLookup((char *)&ev_tcpclient->cfg.hostname);

		if (ev_tcpclient->dst_addr.ss_family == AF_INET)
		{
			ev_tcpclient->addr_mthd 						= COMM_CLIENT_ADDR_TYPE_INET_4;
			satosin(&ev_tcpclient->dst_addr)->sin_port 		= htons(ev_tcpclient->cfg.port);
			ev_tcpclient->src_addr.ss_family 				= (ev_tcpclient->src_addr.ss_family != AF_INET) ? AF_UNSPEC : AF_INET;
		}
		else if (ev_tcpclient->dst_addr.ss_family == AF_INET6)
		{
			ev_tcpclient->addr_mthd 						= COMM_CLIENT_ADDR_TYPE_INET_6;
			satosin6(&ev_tcpclient->dst_addr)->sin6_port 	= htons(ev_tcpclient->cfg.port);
			ev_tcpclient->src_addr.ss_family 				= (ev_tcpclient->src_addr.ss_family != AF_INET6) ? AF_UNSPEC : AF_INET6;
		}
	}
	else
	{
		CommEvTCPClientAddrInitUnix(ev_tcpclient, ev_tcpclient_conf->hostname);
	}

	if (ev_tcpclient->src_addr.ss_family == AF_INET)
	{
		ev_tcpclient->addr_mthd 				= COMM_CLIENT_ADDR_TYPE_INET_4;
	}
	else if (ev_tcpclient->src_addr.ss_family == AF_INET6)
	{
		ev_tcpclient->addr_mthd 				= COMM_CLIENT_ADDR_TYPE_INET_6;
	}

	/* Check if host needs to be resolved - If RESOLV fails, invoke COMM_CLIENT_STATE_CONNECT_FAILED_DNS */
	if (ev_tcpclient->flags.need_dns_lookup)
	{
		/* Check now if user sent us a HOSTNAME and we have no resolver base defined - Will set flags.need_dns_lookup */
		if (!ev_tcpclient->dnsdata.resolv_base)
			return 0;

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Begin LOOKUP of HOSTNAME [%s]\n", ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname);

		ev_tcpclient->dnsdata.ipv6_query 		= (COMM_CLIENT_ADDR_TYPE_INET_64 == ev_tcpclient->addr_mthd || COMM_CLIENT_ADDR_TYPE_INET_6 == ev_tcpclient->addr_mthd) ? 1 : 0;

		/* Start ASYNC DNS lookup */
		ev_tcpclient->dnsdata.req_id 		= CommEvDNSGetHostByNameX(ev_tcpclient->dnsdata.resolv_base, ev_tcpclient->cfg.hostname, CommEvTCPClientDNSResolverCB, ev_tcpclient, ev_tcpclient->dnsdata.ipv6_query);

		if (ev_tcpclient->dnsdata.req_id	< 0)
		{
			ev_tcpclient->socket_state	= COMM_CLIENT_STATE_CONNECT_FAILED_DNS;

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed on DNS lookup\n", ev_tcpclient->socket_fd);

			return 0;
		}

		ev_tcpclient->socket_state		= COMM_CLIENT_STATE_RESOLVING_DNS;

		return 1;
	}

	/* No need to resolve, plain old IP address */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - WONT NEED DNS LOOKUP - UNIX [%d] - ADDRESS [%s]\n",
			ev_tcpclient->socket_fd, ev_tcpclient->cfg.flags.unix_socket, ev_tcpclient->cfg.hostname);

	/* If there is no socket, create one */
	CommEvTCPClientStartFD(ev_tcpclient);

	assert(ev_tcpclient->socket_fd >= 0);

	/* Set description */
	EvKQBaseFDDescriptionSetByFD(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, "BRB_EV_COMM - TCP_CLIENT - [%s:%d] - SSL [%d]",
			ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, ev_tcpclient->flags.ssl_enabled);

	/* Create a new SSL client context if there is NONE */
	op_status 						= CommEvTCPClientSSLDataInit(ev_tcpclient);

	if (op_status != COMM_CLIENT_INIT_OK)
	{
		ev_tcpclient->socket_state 	= COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SSL;

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed on Initialize SSL - RET [%d][%d]\n", ev_tcpclient->socket_fd, errno);
		return 0;
	}

	/* Issue ASYNC connect */
	op_status 						= CommEvTCPClientAsyncConnect(ev_tcpclient);

	return op_status;
}
/**************************************************************************************************************************/
static void CommEvTCPClientStartFD(CommEvTCPClient *ev_tcpclient)
{
	/* If there is no socket, create one */
	if (ev_tcpclient->socket_fd >= 0)
		return;

	/* Create socket and set it to non_blocking */
	if (ev_tcpclient->cfg.flags.unix_socket)
		ev_tcpclient->socket_fd 	= EvKQBaseSocketUNIXNew(ev_tcpclient->kq_base);
	else
		ev_tcpclient->socket_fd 	= (ev_tcpclient->dst_addr.ss_family == AF_INET6) ? EvKQBaseSocketTCPv6New(ev_tcpclient->kq_base) : EvKQBaseSocketTCPNew(ev_tcpclient->kq_base);

	if (ev_tcpclient->socket_fd < 0)
		return;

	EvKQBaseSocketSetNonBlock(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	/* Now BIND to SOURCE */
	CommEvTCPClientBindSource(ev_tcpclient);

	return;
}
/**************************************************************************************************************************/
void CommEvTCPClientResetFD(CommEvTCPClient *ev_tcpclient)
{
	EvBaseKQFileDesc *kq_fd_old;
	EvBaseKQFileDesc *kq_fd_new;
	int old_socketfd;

	/* Sanity check */
	if (!ev_tcpclient)
		return;

	if (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
		return;

	/* Save a copy of old socket_fd and create a new one */
	old_socketfd				= ev_tcpclient->socket_fd;
	ev_tcpclient->socket_fd 	= -1;

	/* Create socket and set it to non_blocking */
	CommEvTCPClientStartFD(ev_tcpclient);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Reset to NEW_FD [%d] - PROTO [%d] - UNIX [%d] AF [%d]\n",
			old_socketfd, ev_tcpclient->socket_fd, ev_tcpclient->cli_proto, ev_tcpclient->cfg.flags.unix_socket, ev_tcpclient->dst_addr.ss_family);

	/* Set context to new FD and reset negotiation count */
	if ((COMM_CLIENTPROTO_SSL == ev_tcpclient->cli_proto) && (ev_tcpclient->ssldata.ssl_handle))
	{
		SSL_set_fd(ev_tcpclient->ssldata.ssl_handle, ev_tcpclient->socket_fd);
		ev_tcpclient->ssldata.ssl_negotiatie_trycount	= 0;
	}

	/* Will close socket and cancel any pending events of old_socketfd */
	if (old_socketfd > 0)
	{
		/* Grab FD from reference table and mark listening socket flags */
		kq_fd_old 	= EvKQBaseFDGrabFromArena(ev_tcpclient->kq_base, old_socketfd);
		kq_fd_new 	= EvKQBaseFDGrabFromArena(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

		/* Clean kq_fd description area */
		if (kq_fd_old->fd.description_str)
		{
			/* copy info */
			if (kq_fd_new)
			{
				kq_fd_new->fd.description_str	= kq_fd_old->fd.description_str;
				kq_fd_new->fd.description_sz 	= kq_fd_old->fd.description_sz;
			}
			/* Release info */
			else
			{
				free(kq_fd_old->fd.description_str);
			}

			/* Reset old description info */
			kq_fd_old->fd.description_str		= NULL;
			kq_fd_old->fd.description_sz 		= 0;
		}
		else
		{
			/* Set description */
			EvKQBaseFDDescriptionSetByFD(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, "BRB_EV_COMM - TCP_CLIENT - RE");
		}

		EvKQBaseSocketClose(ev_tcpclient->kq_base, old_socketfd);
	}
	else
	{
		/* Set new description */
		EvKQBaseFDDescriptionSetByFD(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, "BRB_EV_COMM - TCP_CLIENT - RE");
	}

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
	CommEvTCPClientReconnectSchedule(ev_tcpclient, 50);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPClientReconnect(CommEvTCPClient *ev_tcpclient)
{
	int op_status = 0;

	/* Sanity check */
	if (!ev_tcpclient)
		return 0;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Will try reconnect - STATE [%d]\n",
			ev_tcpclient->socket_fd, ev_tcpclient->socket_state);

	if ((ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTING) || (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SSL))
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Already connecting\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);
		return 0;
	}

	if (ev_tcpclient->socket_state == COMM_CLIENT_STATE_CONNECTED)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Will not reconnect, already connected\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);

		return 0;
	}

	/* If we are connecting to a HOSTNAME FQDN, check if our DNS entry is expired, or if upper layers asked for new lookup on reconnect */
	if ( (ev_tcpclient->flags.need_dns_lookup) && (ev_tcpclient->flags.reconnect_new_dnslookup || (ev_tcpclient->dnsdata.expire_ts == -1) ||
			(ev_tcpclient->dnsdata.expire_ts <= ev_tcpclient->kq_base->stats.cur_invoke_ts_sec) ))
	{
		/* Has query */
		if (ev_tcpclient->dnsdata.expire_ts > 0)
		{
			/* We need to query IPv6 */
			if ((COMM_CLIENT_ADDR_TYPE_INET_46 == ev_tcpclient->addr_mthd))
			{
				ev_tcpclient->dnsdata.ipv6_query 	= ev_tcpclient->dnsdata.ipv6_query ? 0 : 1;
			}
			/* We need to query IPv4 */
			if ((COMM_CLIENT_ADDR_TYPE_INET_64 == ev_tcpclient->addr_mthd))
			{
				ev_tcpclient->dnsdata.ipv6_query 	= ev_tcpclient->dnsdata.ipv6_query ? 0 : 1;
			}
		}

		/* Begin DNS lookup */
		ev_tcpclient->dnsdata.req_id		= CommEvDNSGetHostByNameX(ev_tcpclient->dnsdata.resolv_base, ev_tcpclient->cfg.hostname, CommEvTCPClientDNSResolverCB, ev_tcpclient, ev_tcpclient->dnsdata.ipv6_query);

		/* Begin DNS lookup */
		if (ev_tcpclient->dnsdata.req_id < 0)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d]-[%s:%d] - DNS lookup failed\n",
					ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);

			goto failed;
		}

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d]-[%s:%d] - Begin DNS lookup at ID [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, ev_tcpclient->dnsdata.req_id);

		ev_tcpclient->socket_state	= COMM_CLIENT_STATE_RESOLVING_DNS;
		return 1;
	}

	/* Just connect */

	/* Client wants to rotate balance on multiple IP entries if possible, do it */
	if (ev_tcpclient->flags.reconnect_balance_on_ips && (ev_tcpclient->dnsdata.a_reply.ip_count > 1))
		CommEvTCPClientAddrInitFromAReply(ev_tcpclient);

	/* Reset this client SOCKET_FD */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Will reset SOCKET, to recycle\n",
			ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);

	CommEvTCPClientResetFD(ev_tcpclient);

	/* Failed creating SOCKET, schedule new try */
	if (ev_tcpclient->socket_fd < 0)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d]-[%s:%d] - Failed creating socket - ERRNO [%d - %s]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, errno, strerror(errno));

		goto failed;
	}

	/* Create a new SSL client context if there is NONE */
	op_status 			= CommEvTCPClientSSLDataInit(ev_tcpclient);

	if (op_status != COMM_CLIENT_INIT_OK)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed on Initialize SSL - RET [%d][%d]\n", ev_tcpclient->socket_fd, errno);
		ev_tcpclient->socket_state 	= COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SSL;
		return 0;
	}

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Begin ASYNC connect\n",
			ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);

	/* Begin ASYNC connect */
	op_status 			= CommEvTCPClientAsyncConnect(ev_tcpclient);

	return op_status;

	failed:

	/* Flag to reconnect on FAIL not set */
	if (!ev_tcpclient->flags.reconnect_on_fail)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Will not auto-reconnect, flag is disabled\n",
					ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);
		return 0;
	}

	/* Schedule RECONNECT timer */
	CommEvTCPClientReconnectSchedule(ev_tcpclient, -1);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, ev_tcpclient->timers.reconnect_id);

	return 0;
}
/**************************************************************************************************************************/
void CommEvTCPClientDisconnect(CommEvTCPClient *ev_tcpclient)
{
	/* Sanity check */
	if (!ev_tcpclient)
		return;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Immediate disconnect - LINGER [%d]\n",
			ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, ev_tcpclient->kq_base->kq_conf.onoff.close_linger);

	/* VERBOSITY - This should not happen */
	if ((ev_tcpclient->socket_state == COMM_CLIENT_STATE_DISCONNECTED) || (ev_tcpclient->socket_state > COMM_CLIENT_STATE_CONNECTED))
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d]-[%s:%d] - Seems not connected [%d]-[%s]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port,
				ev_tcpclient->socket_state, CommEvTCPClientStateCodesStr[ev_tcpclient->socket_state]);
	}

	/* Release SSL related objects */
	if (ev_tcpclient->ssldata.x509_cert)
	{
		X509_free(ev_tcpclient->ssldata.x509_cert);
		ev_tcpclient->ssldata.x509_cert = NULL;
	}

	/* Cancel pending DNS request */
	CommEvDNSDataCancelAndClean(&ev_tcpclient->dnsdata);

	/* Cancel any possible timer */
	CommEvTCPClientTimersCancelAll(ev_tcpclient);

	/* Clean up rates */
	CommEvStatisticsRateClean(&ev_tcpclient->statistics);

	/* Destroy read and write buffers */
	CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

	/* Set new state */
	ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;

	/* Will close socket and cancel any pending events socket_fd, including the close event */
	if (ev_tcpclient->socket_fd >= 0)
	{
		EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
		ev_tcpclient->socket_fd		= -1;
	}

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
/**/
/**************************************************************************************************************************/
int CommEvTCPClientReconnectSchedule(CommEvTCPClient *ev_tcpclient, int schedule_ms)
{
	if (ev_tcpclient->timers.reconnect_id >= 0)
		return -1;

	if (schedule_ms < 0)
		schedule_ms 					= ((ev_tcpclient->retry_times.reconnect_on_fail_ms > 0) ? ev_tcpclient->retry_times.reconnect_on_fail_ms : COMM_TCP_CLIENT_RECONNECT_FAIL_DEFAULT_MS);

//	reconnect_on_fail_id
	/* Schedule RECONNECT timer */
	ev_tcpclient->timers.reconnect_id 	= EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE, schedule_ms, CommEvTCPClientReconnectTimer, ev_tcpclient);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]/[%d]\n",
			ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port,
			ev_tcpclient->timers.reconnect_id, schedule_ms);

	return ev_tcpclient->timers.reconnect_id;
}
/**************************************************************************************************************************/
int CommEvTCPClientRatesCalculateSchedule(CommEvTCPClient *ev_tcpclient, int schedule_ms)
{
	/* Not enabled */
	if (!ev_tcpclient->flags.calculate_datarate)
		return -1;

	/* Has timer */
	if (ev_tcpclient->timers.calculate_datarate_id >= 0)
		return -1;

	if (schedule_ms < 0)
		schedule_ms 					= ((ev_tcpclient->retry_times.calculate_datarate_ms > 0) ? ev_tcpclient->retry_times.calculate_datarate_ms : COMM_TCP_CLIENT_CALCULATE_DATARATE_DEFAULT_MS);

	/* Schedule DATARATE CALCULATE TIMER timer */
	ev_tcpclient->timers.calculate_datarate_id = EvKQBaseTimerAdd(ev_tcpclient->kq_base, COMM_ACTION_ADD_VOLATILE, schedule_ms, CommEvTCPClientRatesCalculateTimer, ev_tcpclient);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d]-[%s:%d] - Schedule DATARATE at TIMER_ID [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port,
				ev_tcpclient->timers.calculate_datarate_id);

	return ev_tcpclient->timers.calculate_datarate_id;
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

#ifndef IS_LINUX
	ev_tcpclient->dst_addr.ss_len 		= sizeof(struct sockaddr_un);
//	unix_addr->sun_len 					= sizeof(struct sockaddr_un);
#endif

	strncpy(unix_addr->sun_path, unix_path, sizeof(unix_addr->sun_path));

	/* Save access data */
	strncpy((char*)&ev_tcpclient->cfg.hostname, unix_path, sizeof(ev_tcpclient->cfg.hostname));
	ev_tcpclient->cfg.port 				= 0;

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvTCPClientAddrInitFromAReply(CommEvTCPClient *ev_tcpclient)
{
	DNSAReply *a_reply 		= &ev_tcpclient->dnsdata.a_reply;
	DNSEntry *a_entry 		= NULL;

	/* Clean structure for later use */
	memset(&ev_tcpclient->dst_addr, 0, sizeof(ev_tcpclient->dst_addr));

	int i;

	if (ev_tcpclient->dnsdata.cur_idx >= a_reply->ip_count)
		ev_tcpclient->dnsdata.cur_idx = 0;

	for (i = ev_tcpclient->dnsdata.cur_idx; i < a_reply->ip_count && ev_tcpclient->flags.reconnect_balance_on_ips; i++)
	{
		if (COMM_CLIENT_ADDR_TYPE_INET_64 == ev_tcpclient->addr_mthd)
		{
			if (a_reply->ip_arr[i].type != RFC1035_TYPE_A && a_reply->ip_arr[i].type == RFC1035_TYPE_AAAA) continue;
			a_entry 	= &a_reply->ip_arr[i];
			break;
		}
		else if (COMM_CLIENT_ADDR_TYPE_INET_6 == ev_tcpclient->addr_mthd)
		{
			if (a_reply->ip_arr[i].type != RFC1035_TYPE_AAAA) continue;
			a_entry 	= &a_reply->ip_arr[i];
			break;
		}
		else if (COMM_CLIENT_ADDR_TYPE_INET_46 == ev_tcpclient->addr_mthd)
		{
			if (a_reply->ip_arr[i].type != RFC1035_TYPE_A && a_reply->ip_arr[i].type == RFC1035_TYPE_AAAA) continue;
			a_entry 	= &a_reply->ip_arr[i];
			break;
		}
		else
		{
			if (a_reply->ip_arr[i].type != RFC1035_TYPE_A) continue;
			a_entry 	= &a_reply->ip_arr[i];
			break;
		}

		continue;
	}

	/* Nothing found */
	if (!a_entry)
		a_entry 	= &a_reply->ip_arr[0];

	/* Count IP usage */
	a_entry->ctt++;

	if (a_entry->type == RFC1035_TYPE_A)
	{
		ev_tcpclient->dst_addr.ss_family 				= AF_INET;
		satosin(&ev_tcpclient->dst_addr)->sin_port 		= htons(ev_tcpclient->cfg.port);
		memcpy(&satosin(&ev_tcpclient->dst_addr)->sin_addr.s_addr, &a_entry->addr, sizeof(struct in_addr));
	}
	else if (a_entry->type == RFC1035_TYPE_AAAA)
	{
		ev_tcpclient->dst_addr.ss_family 				= AF_INET6;
		satosin6(&ev_tcpclient->dst_addr)->sin6_port 	= htons(ev_tcpclient->cfg.port);
		memcpy(&satosin6(&ev_tcpclient->dst_addr)->sin6_addr, &a_entry->addr6, sizeof(struct in6_addr));
	}

	/* Balance on IPs */
	if (ev_tcpclient->flags.reconnect_balance_on_ips)
	{
		/* Adjust INDEX to rotate ROUND ROBIN */
		ev_tcpclient->dnsdata.cur_idx++;
	}

	return;
}
/**************************************************************************************************************************/
void CommEvTCPClientEventDispatchInternal(CommEvTCPClient *ev_tcpclient, int data_sz, int thrd_id, int ev_type)
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
void CommEvTCPClientDestroyConnReadAndWriteBuffers(CommEvTCPClient *ev_tcpclient)
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
int CommEvTCPClientEventConnect(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPClient *ev_tcpclient	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);
	int pending_conn				= 0;

	/* Query the kernel for the current socket state */
	ev_tcpclient->socket_state = CommEvUtilsFDCheckState(ev_tcpclient->socket_fd);

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Check state returned [%d]!\n",
			ev_tcpclient->socket_fd, ev_tcpclient->socket_state);

	/* Connection not yet completed, reschedule and return */
	if (COMM_CLIENT_STATE_CONNECTING == ev_tcpclient->socket_state)
	{
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Still connecting...!\n", ev_tcpclient->socket_fd);
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
		else if (ev_tcpclient->flags.reconnect_on_fail)
		{
			/* Destroy read and write buffers */
			CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

			/* Schedule RECONNECT timer */
			CommEvTCPClientReconnectSchedule(ev_tcpclient, -1);

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_id);
		}
		else
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - NO ACTION TAKEN!\n", ev_tcpclient->socket_fd);
		}
	}

	return 1;
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
	CommEvTCPClient *ev_tcpclient 		= cb_data;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Reconnect TID [%d] - CLOSE - TOUT - FAIL [%d]\n",
			ev_tcpclient->socket_fd, timer_id, ev_tcpclient->timers.reconnect_id);

	/* Disable timer ID on CLIENT */
	ev_tcpclient->timers.reconnect_id 	= -1;

	/* Try to reconnect */
	CommEvTCPClientReconnect(ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPClientAsyncConnect(CommEvTCPClient *ev_tcpclient)
{
	int op_status;
	int socket_fd 				= ev_tcpclient->socket_fd;
	EvKQBase *ev_base			= ev_tcpclient->kq_base;

	EvBaseKQFileDesc *kq_fd		= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);
	struct sockaddr* dst_addr 	= (struct sockaddr*)&ev_tcpclient->dst_addr;

	/* Connect new_socket */
	if (dst_addr->sa_family == AF_INET6)
		op_status 				= connect(ev_tcpclient->socket_fd, dst_addr, sizeof(struct sockaddr_in6));
	else if (dst_addr->sa_family == AF_UNIX)
		op_status 				= connect(ev_tcpclient->socket_fd, dst_addr, sizeof(struct sockaddr_un));
	else
		op_status 				= connect(ev_tcpclient->socket_fd, dst_addr, sizeof(struct sockaddr_in));

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

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Connect SYSCALL fail [%d] - ERRNO [%d] - FAMILY [%d] - RoF [%d]\n",
				ev_tcpclient->socket_fd, op_status, errno, dst_addr->sa_family, ev_tcpclient->flags.reconnect_on_fail);

		/* Failed connecting */
		ev_tcpclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_CONNECT_SYSCALL;

		/* Dispatch the internal event */
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatch CONNECT - [%s]\n", ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname);
		CommEvTCPClientEventDispatchInternal(ev_tcpclient, 0, -1, COMM_CLIENT_EVENT_CONNECT);

		/* Client has been destroyed, bail out */
		if ((kq_fd) && ((kq_fd->flags.closed) || (kq_fd->flags.closing)))
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d/%d] - Will STOP, FD is CLOSED [%d]-[%d]\n",
					ev_tcpclient->socket_fd, socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->socket_fd, kq_fd->flags.closed, kq_fd->flags.closing);
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
			/* Destroy read and write buffers */
			CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

			/* Schedule RECONNECT timer */
			CommEvTCPClientReconnectSchedule(ev_tcpclient, -1);

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d] with %d ms\n",
					ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_id, ev_tcpclient->retry_times.reconnect_on_fail_ms);
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

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Connection to [%s:%d] timed out\n",
			ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port);

	/* Set client state */
	ev_tcpclient->socket_state = COMM_CLIENT_STATE_CONNECT_FAILED_TIMEOUT;

	/* Cancel and close any pending event associated with TCP client FD, to avoid double notification */
	EvKQBaseClearEvents(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
	EvKQBaseTimeoutClearAll(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);

	/* Dispatch internal event */
	CommEvTCPClientEventDispatchInternal(ev_tcpclient, timeout_type, thrd_id, COMM_CLIENT_EVENT_CONNECT);

	/* Already destroyed, bail out */
	if ((kq_fd) && ((kq_fd->flags.closed) || (kq_fd->flags.closing)))
		return 0;

	/* Disconnect any pending stuff - Will schedule reconnect if upper layers asked for it */
	CommEvTCPClientInternalDisconnect(ev_tcpclient);

	return 1;
}
/**************************************************************************************************************************/
void CommEvTCPClientInternalDisconnect(CommEvTCPClient *ev_tcpclient)
{
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Clean up - STATE [%d]\n", ev_tcpclient->socket_fd, ev_tcpclient->socket_state);

	/* Will close socket and cancel any pending events of socket_fd, including the close event */
	if (ev_tcpclient->socket_fd >= 0)
	{
		EvKQBaseSocketClose(ev_tcpclient->kq_base, ev_tcpclient->socket_fd);
		ev_tcpclient->socket_fd = -1;
	}

	/* Destroy peer X509 certificate information */
	if (ev_tcpclient->ssldata.x509_cert)
	{
		X509_free(ev_tcpclient->ssldata.x509_cert);
		ev_tcpclient->ssldata.x509_cert = NULL;
	}

	/* Cancel pending DNS request */
	CommEvDNSDataCancelAndClean(&ev_tcpclient->dnsdata);

	/* Cancel any possible timer */
	CommEvTCPClientTimersCancelAll(ev_tcpclient);

	/* Clean up rates */
	CommEvStatisticsRateClean(&ev_tcpclient->statistics);

	/* Destroy read and write buffers */
	CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

	/* Set client STATE */
	if (ev_tcpclient->socket_state <= COMM_CLIENT_STATE_CONNECTED)
		ev_tcpclient->socket_state	= COMM_CLIENT_STATE_DISCONNECTED;

	/* If client want to be reconnected automatically after a CONN_TIMEOUT, honor it */
	if ((COMM_CLIENT_STATE_CONNECT_FAILED_TIMEOUT == ev_tcpclient->socket_state) && ev_tcpclient->flags.reconnect_on_timeout)
	{
		CommEvTCPClientReconnectSchedule(ev_tcpclient, ((ev_tcpclient->retry_times.reconnect_after_timeout_ms > 0) ? ev_tcpclient->retry_times.reconnect_after_timeout_ms : COMM_TCP_CLIENT_RECONNECT_TIMEOUT_DEFAULT_MS));
		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "FD [%d] - Timed out [%s]! Reconnect in [%d] ms timer_id [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname,
				ev_tcpclient->retry_times.reconnect_after_timeout_ms, ev_tcpclient->timers.reconnect_id);
	}
	/* If client want to be reconnected automatically after an EOF, honor it */
	else if (ev_tcpclient->flags.reconnect_on_close)
	{
		CommEvTCPClientReconnectSchedule(ev_tcpclient, ((ev_tcpclient->retry_times.reconnect_after_close_ms > 0) ? ev_tcpclient->retry_times.reconnect_after_close_ms : COMM_TCP_CLIENT_RECONNECT_CLOSE_DEFAULT_MS));

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Unexpectedly closed [%s]! Reconnect in [%d] ms timer_id [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname,
				ev_tcpclient->retry_times.reconnect_after_close_ms, ev_tcpclient->timers.reconnect_id);
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvTCPClientDNSResolverCB(void *ev_dns_ptr, void *req_cb_data, void *a_reply_ptr, int code)
{
	char buff_addr[48];
	int i;
	int lower_seen_ttl				= 0;
	DNSAReply *a_reply				= a_reply_ptr;
	CommEvTCPClient *ev_tcpclient	= req_cb_data;
	EvKQBase *ev_base				= ev_tcpclient->kq_base;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, ev_tcpclient->socket_fd);
	int op_status;

	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE,
			"DNS_SLOT [%d] - Host [%s] - IP resolved - CODE [%d] - [%d] ADDRESS\n", ev_tcpclient->dnsdata.req_id, ev_tcpclient->cfg.hostname, code, a_reply->ip_count);

	/* Cancel DNS_REQ_ID */
	ev_tcpclient->dnsdata.req_id 		= -1;

	/* Reset some info */
	ev_tcpclient->dnsdata.expire_ts 	= -1;

	/* Resolve succeeded OK, begin ASYNC connection procedure */
	if (a_reply->ip_count > 0)
	{
		for (i = 0; i < a_reply->ip_count; i++)
		{
			inet_ntop(a_reply->ip_arr[i].type == RFC1035_TYPE_AAAA ? AF_INET6 : AF_INET, &a_reply->ip_arr[i].addr, buff_addr, sizeof(buff_addr) - 1);

			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Index [%d / %d] - IP_ADDR [%s] - TTL [%d] TYPE [%d]\n",
					i, a_reply->ip_count, buff_addr, a_reply->ip_arr[i].ttl, a_reply->ip_arr[i].type);

			/* First iteration, initialize values */
			if (0 == i)
				lower_seen_ttl 		= a_reply->ip_arr[i].ttl;
			/* There is a smaller TTL, keep it */
			else if (a_reply->ip_arr[i].ttl < lower_seen_ttl)
				lower_seen_ttl 		= a_reply->ip_arr[i].ttl;
		}

		/* Save DNS reply */
		memcpy(&ev_tcpclient->dnsdata.a_reply, a_reply, sizeof(DNSAReply));

		/* Set expire based on lower TTL seen on array */
		ev_tcpclient->dnsdata.expire_ts 	= ev_tcpclient->kq_base->stats.cur_invoke_ts_sec + lower_seen_ttl;
		ev_tcpclient->dnsdata.cur_idx		= 0;

		/* Load destination IP based on A reply */
		CommEvTCPClientAddrInitFromAReply(ev_tcpclient);

		CommEvTCPClientResetFD(ev_tcpclient);

		/* Failed creating SOCKET, schedule new try */
		if (ev_tcpclient->socket_fd < 0)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d]-[%s:%d] - Failed creating socket - ERRNO [%d - %s]\n",
					ev_tcpclient->socket_fd, ev_tcpclient->cfg.hostname, ev_tcpclient->cfg.port, errno, strerror(errno));

			goto failed;
		}

		/* Create a new SSL client context if there is NONE */
		op_status 		= CommEvTCPClientSSLDataInit(ev_tcpclient);

		if (op_status != COMM_CLIENT_INIT_OK)
		{
			KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Failed on Initialize SSL - RET [%d][%d]\n", ev_tcpclient->socket_fd, errno);
			ev_tcpclient->socket_state 	= COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SSL;
			goto failed;
		}

		/* Begin ASYNC connection */
		CommEvTCPClientAsyncConnect(ev_tcpclient);

		return;
	}
	else if (COMM_CLIENT_ADDR_TYPE_INET_64 == ev_tcpclient->addr_mthd)
	{
		if (ev_tcpclient->dnsdata.ipv6_query)
		{
			ev_tcpclient->dnsdata.ipv6_query 	= 0;
			ev_tcpclient->dnsdata.expire_ts 	= -1;
			goto force_reconnect;
		}

		/* Guarantee query state */
		ev_tcpclient->dnsdata.ipv6_query 		= 1;
	}
	else if (COMM_CLIENT_ADDR_TYPE_INET_46 == ev_tcpclient->addr_mthd)
	{
		if (!ev_tcpclient->dnsdata.ipv6_query)
		{
			ev_tcpclient->dnsdata.ipv6_query 	= 1;
			ev_tcpclient->dnsdata.expire_ts 	= -1;
			goto force_reconnect;
		}

		/* Guarantee query state */
		ev_tcpclient->dnsdata.ipv6_query 		= 0;
	}

	/* Resolve failed, notify upper layers */
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FAILED [%d]\n", a_reply->ip_count);

	/* Set flags */
	ev_tcpclient->socket_state				= COMM_CLIENT_STATE_CONNECT_FAILED_DNS;

	failed:

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
	if (ev_tcpclient->flags.reconnect_on_fail)
	{
		force_reconnect:

		/* Destroy read and write buffers */
		CommEvTCPClientDestroyConnReadAndWriteBuffers(ev_tcpclient);

		/* Schedule RECONNECT timer */
		CommEvTCPClientReconnectSchedule(ev_tcpclient, -1);

		KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Schedule RECONNECT_FAIL at TIMER_ID [%d]\n",
				ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_id);
	}

	return;
}
/**************************************************************************************************************************/
static int CommEvTCPClientTimersCancelAll(CommEvTCPClient *ev_tcpclient)
{
	KQBASE_LOG_PRINTF(ev_tcpclient->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Will cancel timers - RE_FAIL [%d] - CALCULATE [%d]\n",
			ev_tcpclient->socket_fd, ev_tcpclient->timers.reconnect_id, ev_tcpclient->timers.calculate_datarate_id);

	/* DELETE all pending timers */
	if (ev_tcpclient->timers.reconnect_id > -1)
		EvKQBaseTimerCtl(ev_tcpclient->kq_base, ev_tcpclient->timers.reconnect_id, COMM_ACTION_DELETE);

	if (ev_tcpclient->timers.calculate_datarate_id > -1)
		EvKQBaseTimerCtl(ev_tcpclient->kq_base, ev_tcpclient->timers.calculate_datarate_id, COMM_ACTION_DELETE);

	ev_tcpclient->timers.reconnect_id			= -1;
	ev_tcpclient->timers.calculate_datarate_id	= -1;

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
static int CommEvTCPClientBindSource(CommEvTCPClient *ev_tcpclient)
{
	/* Save a copy of SRC_ADDRESS if UPPER LAYERS supplied it */
	if (ev_tcpclient->src_addr.ss_family != AF_INET && ev_tcpclient->src_addr.ss_family != AF_INET6)
		return -1;

	if (ev_tcpclient->src_addr.ss_family != ev_tcpclient->dst_addr.ss_family)
		return -2;

	/* Now BIND to local SOURCE, LOCAL or REMOVE, as set by FLAGS */
	if (ev_tcpclient->flags.bindany_active)
		EvKQBaseSocketBindRemote(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, (struct sockaddr *)&ev_tcpclient->src_addr);
	else
		EvKQBaseSocketBindLocal(ev_tcpclient->kq_base, ev_tcpclient->socket_fd, (struct sockaddr *)&ev_tcpclient->src_addr);

	return 0;
}
/**************************************************************************************************************************/
