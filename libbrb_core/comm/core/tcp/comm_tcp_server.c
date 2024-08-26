/*
 * comm_tcp_server.c
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

/*
 * SSL Certificate generation steps:
 *
 * openssl genrsa -out server.key 1024
 * openssl req -new -key server.key -out server.csr
 * openssl x509 -req -days 365 -in server.csr -signkey server.key -out server.crt
 *
 * Removing SSL PHASSPHREASE REQUEST
 * openssl rsa -in server.key -out server.key
 *
 */

#include "../include/libbrb_core.h"

/* Common event - Will call SSLAccept or destroy SSL data if in SSL mode */
static EvBaseKQCBH CommEvTCPServerEventProtocolIdentifyTimeout;
static EvBaseKQCBH CommEvTCPServerEventProtocolIdentify;
static EvBaseKQCBH CommEvTCPServerEventSSLHandshakeDeferDummyReadEvent;
static EvBaseKQCBH CommEvTCPServerEventSNIPeek;
static EvBaseKQCBH CommEvTCPServerEventListenerClose;
static EvBaseKQCBH CommEvTCPServerEventAccept;
static EvBaseKQCBH CommEvTCPServerEventClose;

static EvBaseKQCBH CommEvTCPServerConnRatesCalculateTimer;

/* PLAINTEXT IO events */
static EvBaseKQCBH CommEvTCPServerEventWrite;
static EvBaseKQCBH CommEvTCPServerEventRead;

/* SSL HANDSHAKE EVENT */
static EvBaseKQCBH CommEvTCPServerEventSSLAccept;

/* SSL IO events */
static EvBaseKQCBH CommEvTCPServerEventSSLRead;
static EvBaseKQCBH CommEvTCPServerEventSSLWrite;

static int CommEvTCPServerListenerTCPInit(CommEvTCPServer *srv_ptr, CommEvTCPServerConf *server_conf, CommEvTCPServerListener *listener);
static int CommEvTCPServerListenInet(CommEvTCPServer *srv_ptr, int slot_id);
static int CommEvTCPServerListenUnix(CommEvTCPServer *srv_ptr, int listener_id, char *path_str);
static void CommEvTCPServerDispatchEvent(CommEvTCPServer *srv_ptr, CommEvTCPServerConn *conn_hnd, int data_sz, int thrd_id, int ev_type);
static int CommEvTCPServerSelfSyncReadBuffer(CommEvTCPServerConn *conn_hnd, int orig_read_sz, int thrd_id);
static int CommEvTCPServerEventProcessBuffer(CommEvTCPServerConn *conn_hnd, int read_sz, int thrd_id, char *read_buf, int read_buf_sz);
static int CommEvTCPServerAcceptPostInit(CommEvTCPServer *srv_ptr, CommEvTCPServerListener *listener, int conn_fd, int accept_queue_sz, int thrd_id);

static EvBaseKQObjDestroyCBH CommEvTCPServerObjectDestroyCBH;

static CommEvUNIXGenericCBH CommEvTCPServerUNIXEventAccept;
static CommEvUNIXGenericCBH CommEvTCPServerUNIXEventRead;
static CommEvUNIXGenericCBH CommEvTCPServerUNIXEventBrbProtoRead;
static CommEvUNIXGenericCBH CommEvTCPServerUNIXEventClose;

static int CommEvTCPServerDoDestroy(CommEvTCPServer *srv_ptr);
static int CommEvTCPServerDoDestroy_SSLCleanup(CommEvTCPServer *srv_ptr, CommEvTCPServerListener *listener);

/**************************************************************************************************************************/
CommEvTCPServer *CommEvTCPServerNew(EvKQBase *kq_base)
{
	CommEvTCPServer *srv_ptr;

	/* Create in-memory server structure */
	srv_ptr								= calloc(1, sizeof(CommEvTCPServer));
	srv_ptr->kq_base					= kq_base;
	srv_ptr->transfer.gc_timerid		= -1;

	/* Populate KQ_BASE object structure */
	srv_ptr->kq_obj.code				= EV_OBJ_TCP_SERVER;
	srv_ptr->kq_obj.obj.ptr				= srv_ptr;
	srv_ptr->kq_obj.obj.destroy_cbh		= CommEvTCPServerObjectDestroyCBH;
	srv_ptr->kq_obj.obj.destroy_cbdata	= NULL;

	/* Register KQ_OBJECT */
	EvKQBaseObjectRegister(kq_base, &srv_ptr->kq_obj);

	/* Initialize LISTENER SLOTs */
	SlotQueueInit(&srv_ptr->listener.slot, COMM_TCP_SERVER_MAX_LISTERNERS, (kq_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE));

	/* Create CONN_FD arena */
	CommEvTCPServerConnArenaNew(srv_ptr);

	/* INIT MT_SAFE MUTEXES */
	//COMM_SERVER_CONN_TABLE_MUTEX_INIT(srv_ptr);

	return srv_ptr;
}
/**************************************************************************************************************************/
int CommEvTCPServerDestroy(CommEvTCPServer *srv_ptr)
{
	/* Sanity check */
	if (!srv_ptr)
		return 0;

	/* Set flags as destroyed */
	srv_ptr->flags.destroyed = 1;

	/* Reference count still holds, bail out */
	if (srv_ptr->ref_count-- > 0)
		return srv_ptr->ref_count;

	/* Invoke internal destroy */
	CommEvTCPServerDoDestroy(srv_ptr);

	return -1;
}
/**************************************************************************************************************************/
CommEvTCPServer *CommEvTCPServerLink(CommEvTCPServer *srv_ptr)
{
	/* Sanity check */
	if (!srv_ptr)
		return NULL;

	srv_ptr->ref_count++;
	return srv_ptr;
}
/**************************************************************************************************************************/
int CommEvTCPServerUnlink(CommEvTCPServer *srv_ptr)
{
	/* Sanity check */
	if (!srv_ptr)
		return 0;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "SRV_PTR [%p] - Will unlink with REF_COUNT [%d]\n", srv_ptr, srv_ptr->ref_count);

	/* Reference count still holds, bail out */
	if (srv_ptr->ref_count-- > 0)
		return srv_ptr->ref_count;

	/* Invoke internal destroy */
	CommEvTCPServerDoDestroy(srv_ptr);
	return -1;
}
/**************************************************************************************************************************/
int CommEvTCPServerListenerConnShutdownAll(CommEvTCPServer *srv_ptr, int listener_id)
{
	CommEvTCPServerConn *conn_hnd;

	DLinkedListNode *node	= srv_ptr->conn.list.head;
	int limit				= (srv_ptr->conn.list.size + 8);
	int count				= 0;

	/* No item */
	if (!node)
		return 0;

	/* Walk all connections searching for any belonging to this LISTENER ID */
	while (limit-- > 0)
	{
		/* END of list, bail out */
		if (!node)
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "SRV_PTR [%p] - LID [%d] - Finished\n", srv_ptr, listener_id);
			break;
		}

		/* Grab data and point to next node */
		conn_hnd	= node->data;
		node		= node->next;

		assert(conn_hnd->listener);

		/* Shutdown client CONN - If listener has been set to -1, shutdown all clients */
		if ((listener_id < 0) || (listener_id == conn_hnd->listener->slot_id))
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "SRV_PTR [%p] - LID [%d] - FD [%d] - Destroying context - COUNT/LIMIT [%d / %d]\n",
					srv_ptr, listener_id, conn_hnd->socket_fd, count, limit);

			/* Invoke and touch counter */
			CommEvTCPServerConnInternalShutdown(conn_hnd);
			count++;
		}

		continue;
	}

	return count;
}
/**************************************************************************************************************************/
int CommEvTCPServerListenerDel(CommEvTCPServer *srv_ptr, int listener_id)
{
	CommEvTCPServerListener *listener;
	EvBaseKQFileDesc *kq_fd;

	/* Sanity check */
	if (listener_id < 0)
		return 0;

	/* Point to selected LISTENER and point LISTENER back to server */
	listener = &srv_ptr->listener.arr[listener_id];
	BRB_ASSERT_FMT(srv_ptr->kq_base, (listener->flags.active), "SRV_PTR [%p] - Inactive LISTENER [%d]\n", srv_ptr, listener_id);

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "SRV_PTR [%p] - LID_FD [%d] - LID [%d / %s] - Begin shutdown\n",
			srv_ptr, listener->socket_fd, listener_id, EvKQBaseFDDescriptionGetByFD(srv_ptr->kq_base, listener->socket_fd));

	/* Grab FD from reference table and mark listening socket flags */
	kq_fd = EvKQBaseFDGrabFromArena(srv_ptr->kq_base, listener->socket_fd);
	assert(kq_fd->flags.so_listen);

	/* Remove from active listener LIST and release SLOT */
	DLinkedListDelete(&srv_ptr->listener.list, &listener->node);
	SlotQueueFree(&srv_ptr->listener.slot, listener_id);

	/* Shutdown UNIX server */
	CommEvUNIXServerListenerDel(srv_ptr->unix_server, listener->unix_lid);
	listener->unix_lid = -1;

	/* Shutdown all CONN_HNDs related to this listener - Then shutdown any SSL related information of this listener */
	CommEvTCPServerListenerConnShutdownAll(srv_ptr, listener_id);
	CommEvTCPServerDoDestroy_SSLCleanup(srv_ptr, listener);

	/* Clear description and flags */
	EvKQBaseFDDescriptionClearByFD(srv_ptr->kq_base, listener->socket_fd);
	memset(&listener->flags, 0, sizeof(listener->flags));

	/* Shutdown main socket */
	EvKQBaseSocketClose(srv_ptr->kq_base, listener->socket_fd);
	listener->socket_fd		= -1;

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPServerListenerAdd(CommEvTCPServer *srv_ptr, CommEvTCPServerConf *server_conf)
{
	CommEvTCPServerListener *listener;
	EvBaseKQFileDesc *kq_fd;
	int status;
	int slot_id;
	int i;

	int reuseaddr_on	= 1;
	int op_status		= 0;

	/* Sanity check */
	if ((!srv_ptr) || (!server_conf))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "SRV_PTR [%p] - SERVER_CONF [%p]\n", srv_ptr, server_conf);
		return (- COMM_SERVER_FAILURE_UNKNOWN);
	}

	/* Grab a free listener SLOT ID */
	slot_id = SlotQueueGrab(&srv_ptr->listener.slot);

	/* No more slots allowed, bail out */
	if (slot_id < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "SRV_PTR [%p] - No more slots\n", srv_ptr);
		return (- COMM_SERVER_FAILURE_NO_MORE_SLOTS);
	}

	/* Point to selected LISTENER and point LISTENER back to server */
	listener = &srv_ptr->listener.arr[slot_id];

	/* Make sure we are not active and cleanup SLOT */
	assert(!listener->flags.active);
	memset(listener, 0, sizeof(CommEvTCPServerListener));

	/* Populate data into listener */
	listener->parent_srv			= srv_ptr;
	listener->slot_id				= slot_id;
	listener->port					= server_conf->port;
	listener->unix_lid				= -1;
	listener->ssldata.ssl_context	= server_conf->ssl.ssl_context;

	/* Copy common configuration information */
	srv_ptr->cfg[slot_id].bind_method				= server_conf->bind_method;
	srv_ptr->cfg[slot_id].read_mthd					= server_conf->read_mthd;
	srv_ptr->cfg[slot_id].srv_proto					= server_conf->srv_proto;
	srv_ptr->cfg[slot_id].srv_type					= server_conf->srv_type;
	srv_ptr->cfg[slot_id].port						= server_conf->port;
	srv_ptr->cfg[slot_id].cli_queue_max				= server_conf->limits.cli_queue_max;
	srv_ptr->cfg[slot_id].timeout.autodetect_ms		= server_conf->timeout.autodetect_ms;
	srv_ptr->cfg[slot_id].timeout.transfer_ms		= server_conf->timeout.transfer_ms;
	srv_ptr->cfg[slot_id].timeout.inactive_ms		= server_conf->timeout.inactive_ms;
	srv_ptr->cfg[slot_id].flags.reuse_addr			= server_conf->flags.reuse_addr;
	srv_ptr->cfg[slot_id].flags.reuse_port			= server_conf->flags.reuse_port;

	/* Set default IO events */
	for (i = 0; i < COMM_SERVER_EVENT_LASTITEM; i++)
		if (server_conf->events[i].handler)
			CommEvTCPServerEventSet(srv_ptr, slot_id, i, server_conf->events[i].handler, server_conf->events[i].data);

	/* Check if self sync is active, then grab token to check when buffer finish */
	if (server_conf->self_sync.token_str)
	{
		/* Copy token into buffer and calculate token size */
		strncpy((char*)&srv_ptr->cfg[slot_id].self_sync.token_str_buf, server_conf->self_sync.token_str, sizeof(srv_ptr->cfg[slot_id].self_sync.token_str_buf));
		srv_ptr->cfg[slot_id].self_sync.token_str_sz = strlen(server_conf->self_sync.token_str);

		/* Mark self sync as active for this server and save max buffer limit */
		srv_ptr->cfg[slot_id].self_sync.max_buffer_sz	= server_conf->self_sync.max_buffer_sz;
		srv_ptr->cfg[slot_id].flags.self_sync			= 1;
	}

	/* Create socket if there is a port SET or if its a UNIX with not brb_proto */
	if (listener->port > 0 || (server_conf->unix_server.path_str && server_conf->unix_server.no_brb_proto))
	{
		/* Initialize TCP side of this server */
		CommEvTCPServerListenerTCPInit(srv_ptr, server_conf, listener);

		/* Set volatile read event for accepting new connections - Will be rescheduled by internal accept event */
		EvKQBaseSetEvent(srv_ptr->kq_base, listener->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_PERSIST, CommEvTCPServerEventAccept, listener);
		EvKQBaseSetEvent(srv_ptr->kq_base, listener->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventListenerClose, listener);
	}

	/* Fire UP UNIX_SERVER side of this TCP_SERVER if upper layers set a path */
	if (server_conf->unix_server.path_str && !server_conf->unix_server.no_brb_proto)
	{
		CommEvUNIXServerConf unix_server_conf;
		memset(&unix_server_conf, 0, sizeof(CommEvUNIXServerConf));
		unix_server_conf.path_str			= server_conf->unix_server.path_str;
		unix_server_conf.flags.reuse_addr	= server_conf->unix_server.reuse_addr;
		unix_server_conf.flags.reuse_port	= server_conf->unix_server.reuse_port;
		unix_server_conf.flags.no_brb_proto	= server_conf->unix_server.no_brb_proto;

		/* Initialize a new UNIX_SERVER instance if we had not initialized yet */
		if (!srv_ptr->unix_server)
		{
			/* New server and link log base */
			srv_ptr->unix_server			= CommEvUNIXServerNew(srv_ptr->kq_base);
			srv_ptr->unix_server->log_base	= EvKQBaseLogBaseLink(srv_ptr->log_base);
		}

		/* Add UNIX listener */
		listener->unix_lid					= CommEvUNIXServerListenerAdd(srv_ptr->unix_server, &unix_server_conf);

		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, ((listener->unix_lid > -1) ? LOGCOLOR_GREEN : LOGCOLOR_RED),
				"FD [%d] - LID [%d] - UNIX_SERVER_PATH [%s] - UNIX_LID [%d] - [%s]\n",
				listener->socket_fd, listener->slot_id, server_conf->unix_server.path_str, listener->unix_lid, ((listener->unix_lid > -1) ? "SUCCESS" : "FAILED"));

		/* Set up common events */
		if (listener->unix_lid > -1)
		{
			CommEvUNIXServerEventSet(srv_ptr->unix_server, listener->unix_lid, COMM_UNIX_SERVER_EVENT_ACCEPT, CommEvTCPServerUNIXEventAccept, listener);
			CommEvUNIXServerEventSet(srv_ptr->unix_server, listener->unix_lid, COMM_UNIX_SERVER_EVENT_CLOSE, CommEvTCPServerUNIXEventClose, listener);
			CommEvUNIXServerEventSet(srv_ptr->unix_server, listener->unix_lid, COMM_UNIX_SERVER_EVENT_READ,
					(unix_server_conf.flags.no_brb_proto ? CommEvTCPServerUNIXEventRead : CommEvTCPServerUNIXEventBrbProtoRead), listener);

		}
	}

	/* Add into active linked list and set flags to active */
	DLinkedListAdd(&srv_ptr->listener.list, &listener->node, listener);
	listener->flags.active = 1;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Added LISTENER_ID [%d]\n", listener->socket_fd, listener->slot_id);
	return slot_id;
}
/**************************************************************************************************************************/
void CommEvTCPServerEventSet(CommEvTCPServer *srv_ptr, int listener_id, CommEvTCPServerEventCodes ev_type, CommEvTCPServerCBH *cb_handler, void *cb_data)
{
	if (ev_type >= COMM_SERVER_EVENT_LASTITEM)
		return;

	/* Set event */
	srv_ptr->events[listener_id][ev_type].cb_handler_ptr	= cb_handler;
	srv_ptr->events[listener_id][ev_type].cb_data_ptr		= cb_data;

	//printf("CommEvTCPServerEventSet - LISTENER_ID [%d] - EV_TYPE [%d] CB_H [%p]\n", listener_id, ev_type, cb_handler);

	return;
}
/**************************************************************************************************************************/
int CommEvTCPServerSwitchConnProto(CommEvTCPServerConn *conn_hnd, int proto)
{
	CommEvTCPServer *parent_srv = conn_hnd->parent_srv;
	switch(proto)
	{
	case COMM_SERVERPROTO_PLAIN:
	{
		KQBASE_LOG_PRINTF(parent_srv->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - Protocol SWITCHED to PLAIN\n", conn_hnd->socket_fd);

		/* Mark SSL as disabled */
		conn_hnd->flags.ssl_enabled				= 0;

		/* Set default READ and CLOSE events to this conn_fd, if any is defined in server */
		CommEvTCPServerConnSetDefaultEvents(conn_hnd->parent_srv, conn_hnd);

		/* Set disconnect and read internal events for newly connected socket */
		EvKQBaseSetEvent(parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventClose, conn_hnd);
		EvKQBaseSetEvent(parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventRead, conn_hnd);

		break;
	}

	case COMM_SERVERPROTO_SSL:
		break;
	}

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPServerConnReadReschedule(CommEvTCPServerConn *conn_hnd)
{
	/* We are running SSL, read accordingly */
	if (conn_hnd->flags.ssl_enabled)
		EvKQBaseSetEvent(conn_hnd->parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLRead, conn_hnd);
	else
		EvKQBaseSetEvent(conn_hnd->parent_srv->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventRead, conn_hnd);

	return 1;
}
/**************************************************************************************************************************/
int CommEvTCPServerKickConnReadPending(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	int op_status;

	EvKQBase *kq_base				= base_ptr;
	CommEvTCPServerConn *conn_hnd	= cb_data;

	KQBASE_LOG_PRINTF(kq_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Dispatching read event of [%d] bytes\n", fd, read_sz);

	/* We are running SSL, read accordingly */
	if (conn_hnd->flags.ssl_enabled)
		op_status = CommEvTCPServerEventSSLRead(fd, read_sz, thrd_id, cb_data, base_ptr);
	else
		op_status = CommEvTCPServerEventRead(fd, read_sz, thrd_id, cb_data, base_ptr);

	return op_status;
}
/**************************************************************************************************************************/
int CommEvTCPServerKickConnFromAcceptDefer(CommEvTCPServerConn *conn_hnd)
{
	CommEvTCPServer *srv_ptr = conn_hnd->parent_srv;

	/* Not defer, bail out */
	if (!conn_hnd->flags.ssl_handshake_defer)
		return 0;

	conn_hnd->flags.ssl_handshake_defer = 0;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Back from SSL_DEFER_HANDSHAKE\n", conn_hnd->socket_fd);

	/* Cancel PERSISTENT read event */
	EvKQBaseSetEvent(srv_ptr->kq_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);

	/* Create a new SSL conn_client context if needed and begin SSL handshake */
	CommEvTCPServerConnSSLSessionInit(conn_hnd);
	CommEvTCPServerEventSSLAccept(conn_hnd->socket_fd, 0, -1, conn_hnd, srv_ptr->kq_base);

	return 1;
}
/**************************************************************************************************************************/
void CommEvTCPServerKickConnWriteQueue(CommEvTCPServerConn *conn_hnd)
{
	CommEvTCPServer *srv_ptr	= conn_hnd->parent_srv;
	int listener_id				= conn_hnd->listener->slot_id;


	/* If there is ENQUEUED data, schedule WRITE event and LEAVE, as we need to PRESERVE WRITE ORDER */
	if (conn_hnd->flags.pending_write)
	{
		EvKQBaseSetEvent(srv_ptr->kq_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE,
				conn_hnd->flags.ssl_enabled ? CommEvTCPServerEventSSLWrite : CommEvTCPServerEventWrite, conn_hnd);
		return;
	}
	/* Try to write on this very same IO LOOP */
	else
	{
		if (conn_hnd->flags.ssl_enabled)
			CommEvTCPServerEventSSLWrite(conn_hnd->socket_fd, 8092, -1, conn_hnd, srv_ptr->kq_base);
		else
			CommEvTCPServerEventWrite(conn_hnd->socket_fd, 8092, -1, conn_hnd, srv_ptr->kq_base);

		return;
	}

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
void CommEvTCPServerSSLCertCacheDestroy(CommEvTCPServerCertificate *cert_info)
{
	/* Sanity check */
	if (!cert_info)
		return;

	X509_free(cert_info->x509_cert);
	free(cert_info);

	return;
}
/**************************************************************************************************************************/
int CommEvTCPServerSSLCertCacheInsert(CommEvTCPServer *srv_ptr, char *dnsname_str, X509 *x509_cert)
{
	CommEvTCPServerCertificate *cert_info;
	char wildcard_str[512];
	int wildcardable;
	int dnsname_strsz;

	/* Sanity check */
	if (!dnsname_str)
		return 0;

	/* Grab DNS name string length */
	dnsname_strsz = strlen(dnsname_str);

	/* Too small to be valid */
	if (dnsname_strsz < 3)
		return 0;

	/* Cache arena not initialized, initialize now */
	if (!srv_ptr->ssldata.cert_cache.table)
	{
		srv_ptr->ssldata.cert_cache.table = AssocArrayNew(BRBDATA_THREAD_UNSAFE, 7951, (ASSOCITEM_DESTROYFUNC*)CommEvTCPServerSSLCertCacheDestroy);
		DLinkedListInit(&srv_ptr->ssldata.cert_cache.list, BRBDATA_THREAD_UNSAFE);
	}

	cert_info = (CommEvTCPServerCertificate*)AssocArrayLookup(srv_ptr->ssldata.cert_cache.table, dnsname_str);

	/* Already cached, bail out */
	if (cert_info)
		return 0;

	/* Create a new certificate node */
	cert_info				= calloc(1, sizeof(CommEvTCPServerCertificate));
	cert_info->x509_cert	= x509_cert;

	/* Generate WILDCARD for this domain */
	wildcardable = CommEvSSLUtils_GenerateWildCardFromDomain(dnsname_str, (char*)&wildcard_str, (sizeof(wildcard_str)));

	/* Add into internal cache */
	AssocArrayAdd(srv_ptr->ssldata.cert_cache.table, (wildcardable ? wildcard_str : dnsname_str), cert_info);

	return 1;
}
/**************************************************************************************************************************/
X509 *CommEvTCPServerSSLCertCacheLookupByConnHnd(CommEvTCPServerConn *conn_hnd)
{
	char wildcard_str[512];
	int wildcardable;

	/* Try to find a CACHE_CERT for this SNI */
	conn_hnd->ssldata.x509_cert			= CommEvTCPServerSSLCertCacheLookup(conn_hnd->parent_srv, CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd));
	conn_hnd->ssldata.sni_host_tldpos	= CommEvSSLUtils_GenerateWildCardFromDomain(CommEvTCPServerConnSSLDataGetSNIStr(conn_hnd), (char*)&wildcard_str, (sizeof(wildcard_str)));

	if (conn_hnd->ssldata.sni_host_tldpos > 0)
		conn_hnd->ssldata.sni_host_tldpos++;

	/* We are using a cached certificate, save flags */
	if (conn_hnd->ssldata.x509_cert)
		conn_hnd->flags.ssl_cert_cached = 1;

	/* Send back what we got */
	return conn_hnd->ssldata.x509_cert;
}
/**************************************************************************************************************************/
X509 *CommEvTCPServerSSLCertCacheLookup(CommEvTCPServer *srv_ptr, char *dnsname_str)
{
	CommEvTCPServerCertificate *cert_info;
	char wildcard_str[512];
	int wildcardable;

	/* Cache arena not initialized, bail out */
	if (!srv_ptr->ssldata.cert_cache.table)
		return NULL;

	/* Try to find directly */
	cert_info = (CommEvTCPServerCertificate*)AssocArrayLookup(srv_ptr->ssldata.cert_cache.table, dnsname_str);

	/* Found, return X509 certificate */
	if (cert_info)
	{
		//printf("CommEvTCPServerSSLCertCacheLookup - Found NON_WILDCARD_CERT [%p] for DNS [%s]\n", cert_info, dnsname_str);
		return cert_info->x509_cert;
	}

	/* Generate WILDCARD for this domain */
	wildcardable = CommEvSSLUtils_GenerateWildCardFromDomain(dnsname_str, (char*)&wildcard_str, (sizeof(wildcard_str)));

	/* Generated WILDCARD, try to lookup */
	if (wildcardable)
	{
		/* Try to find via WILDCARD */
		cert_info = (CommEvTCPServerCertificate*)AssocArrayLookup(srv_ptr->ssldata.cert_cache.table, wildcard_str);

		/* Found, return X509 certificate */
		if (cert_info)
		{
			//printf("CommEvTCPServerSSLCertCacheLookup - Found WILDCARD_CERT [%p] for DNS [%s] from DNS_NAME [%s]\n", cert_info, wildcard_str, dnsname_str);
			return cert_info->x509_cert;
		}
	}

	//printf("CommEvTCPServerSSLCertCacheLookup - NOT FOUND for DNS [%s] nor WILDCARD [%s]\n", dnsname_str, wildcard_str);

	/* Not found, NULL */
	return NULL;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* Private functions
/**************************************************************************************************************************/
static int CommEvTCPServerListenerTCPInit(CommEvTCPServer *srv_ptr, CommEvTCPServerConf *server_conf, CommEvTCPServerListener *listener)
{
	EvBaseKQFileDesc *kq_fd;
	int op_status;

	int slot_id = listener->slot_id;

	if (server_conf->unix_server.path_str && server_conf->unix_server.no_brb_proto)
	{
		/* Create socket */
		listener->socket_fd 	= EvKQBaseSocketUNIXNew(srv_ptr->kq_base);
	}
	else
	{
		listener->socket_fd 	= server_conf->srv_type == COMM_SERVER_TYPE_INET6 ? EvKQBaseSocketTCPv6New(srv_ptr->kq_base) : EvKQBaseSocketTCPNew(srv_ptr->kq_base);
	}

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Will add LISTENER_ID [%d]\n", listener->socket_fd, listener->slot_id);

	/* Check if created socket is ok */
	if (listener->socket_fd < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - LISTENER_ID [%d] - COMM_SERVER_FAILURE_SOCKET\n", listener->socket_fd, slot_id);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- COMM_SERVER_FAILURE_SOCKET);
	}

	/* Set SOCKOPT SO_REUSEADDR */
	if ((srv_ptr->cfg[slot_id].flags.reuse_addr) && (EvKQBaseSocketSetReuseAddr(srv_ptr->kq_base, listener->socket_fd) == -1))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - LISTENER_ID [%d] - COMM_SERVER_FAILURE_REUSEADDR\n", listener->socket_fd, slot_id);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- COMM_SERVER_FAILURE_REUSEADDR);
	}

	/* Set SOCKOPT SO_REUSEPORT */
	if ((srv_ptr->cfg[slot_id].flags.reuse_port) && (EvKQBaseSocketSetReusePort(srv_ptr->kq_base, listener->socket_fd) == -1))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - LISTENER_ID [%d] - COMM_SERVER_FAILURE_REUSEPORT\n", listener->socket_fd, slot_id);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- COMM_SERVER_FAILURE_REUSEPORT);
	}

	if (server_conf->unix_server.path_str && server_conf->unix_server.no_brb_proto)
	{
		op_status = CommEvTCPServerListenUnix(srv_ptr, slot_id, server_conf->unix_server.path_str);
	}
	else
	{
		/* Begin to listen */
		op_status = CommEvTCPServerListenInet(srv_ptr, slot_id);
	}

	/* Check if everything went OK with socket */
	if (op_status != COMM_SERVER_INIT_OK)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - LISTENER_ID [%d] - COMM_SERVER_FAILURE_LISTEN\n", listener->socket_fd, slot_id);
		SlotQueueFree(&srv_ptr->listener.slot, slot_id);
		return (- op_status);
	}

	/* Grab FD from reference table and mark listening socket flags */
	kq_fd 	= EvKQBaseFDGrabFromArena(srv_ptr->kq_base, listener->socket_fd);
	kq_fd->flags.so_listen = 1;

	/* Set humanized description of this socket */
	EvKQBaseFDDescriptionSet(kq_fd, "EV_TCPSERVER [%s:%d] - LID/FD [%d/%d]", (COMM_SERVERPROTO_SSL == srv_ptr->cfg[slot_id].srv_proto ? "SSL" : "PLAIN"),
			listener->port, slot_id, listener->socket_fd);

	/* Make it non-blocking and no-delay */
	EvKQBaseSocketSetNonBlock(srv_ptr->kq_base, listener->socket_fd);
	EvKQBaseSocketSetNoDelay(srv_ptr->kq_base, listener->socket_fd);

	/* We are running SSL or AUTODETECT, so start loading CONTEXT, KEY and CERT */
	if ((COMM_SERVERPROTO_SSL == srv_ptr->cfg[slot_id].srv_proto) || (COMM_SERVERPROTO_AUTODETECT == srv_ptr->cfg[slot_id].srv_proto))
	{
		/* Generate main RSA key for this server if not already generated */
		if (!srv_ptr->ssldata.main_key)
			CommEvSSLUtils_GenerateRSAToServer(srv_ptr, 1024);

		/* Load key path, if any */
		if (server_conf->ssl.ca_key_path)
			strncpy((char*)&srv_ptr->cfg[slot_id].ssl.ca_key_path, server_conf->ssl.ca_key_path, sizeof(srv_ptr->cfg[slot_id].ssl.ca_key_path));

		/* Load certificate path, if any */
		if (server_conf->ssl.ca_cert_path)
			strncpy((char*)&srv_ptr->cfg[slot_id].ssl.ca_cert_path, server_conf->ssl.ca_cert_path, sizeof(srv_ptr->cfg[slot_id].ssl.ca_cert_path));

		/* Create new context */
		if (!listener->ssldata.ssl_context)
			listener->ssldata.ssl_context	= SSL_CTX_new(SSLv23_server_method());

		/* Failed initializing SSL context, bail out */
		if (!listener->ssldata.ssl_context)
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - LISTENER_ID [%d] - COMM_SERVER_FAILURE_SSL_CONTEXT\n", listener->socket_fd, slot_id);
			SlotQueueFree(&srv_ptr->listener.slot, slot_id);
			return (- COMM_SERVER_FAILURE_SSL_CONTEXT);
		}

		/* Load the CERTIFICATE FILE into the SSL_CTX structure */
		if (srv_ptr->cfg[slot_id].ssl.ca_cert_path[0] != '\0')
		{
			op_status = SSL_CTX_use_certificate_file(listener->ssldata.ssl_context, (char*)&srv_ptr->cfg[slot_id].ssl.ca_cert_path, SSL_FILETYPE_PEM);

			if (op_status <= 0)
			{
				KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - LISTENER_ID [%d] - COMM_SERVER_FAILURE_SSL_CERT\n", listener->socket_fd, slot_id);
				SlotQueueFree(&srv_ptr->listener.slot, slot_id);
				return (- COMM_SERVER_FAILURE_SSL_CERT);
			}

			/* Load CA certificate */
			srv_ptr->cfg[slot_id].ssl.ca_cert.x509_cert = CommEvSSLUtils_X509CertFromFile((char*)&srv_ptr->cfg[slot_id].ssl.ca_cert_path);
			sk_X509_insert(srv_ptr->cfg[slot_id].ssl.ca_cert.cert_chain, srv_ptr->cfg[slot_id].ssl.ca_cert.x509_cert, 0);
		}

		/* Load the KEY FILE into the SSL_CTX structure */
		if (srv_ptr->cfg[slot_id].ssl.ca_key_path[0] != '\0')
		{
			op_status = SSL_CTX_use_PrivateKey_file(listener->ssldata.ssl_context, (char*)&srv_ptr->cfg[slot_id].ssl.ca_key_path, SSL_FILETYPE_PEM);

			if (op_status <= 0)
			{
				KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - LISTENER_ID [%d] - COMM_SERVER_FAILURE_SSL_KEY\n", listener->socket_fd, slot_id);
				SlotQueueFree(&srv_ptr->listener.slot, slot_id);
				return (- COMM_SERVER_FAILURE_SSL_KEY);
			}

			/* Load CA private key */
			srv_ptr->cfg[slot_id].ssl.ca_cert.key_private = CommEvSSLUtils_X509PrivateKeyReadFromFile((char*)&srv_ptr->cfg[slot_id].ssl.ca_key_path);
		}
	}

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerListenInet(CommEvTCPServer *srv_ptr, int listener_id)
{
	/* Point to selected LISTENER */
	CommEvTCPServerListener *listener	= &srv_ptr->listener.arr[listener_id];
	int socket_size 					= sizeof(struct sockaddr_in);
	int op_status						= -1;

	/* Initialize SERVER_ADDR STRUCT */
	memset(&srv_ptr->cfg[listener_id].bind_addr, 0, sizeof(srv_ptr->cfg[listener_id].bind_addr));

	if (srv_ptr->cfg[listener_id].srv_type == COMM_SERVER_TYPE_INET6)
	{
		/* Populate CFG STRUCT */
		satosin6(&srv_ptr->cfg[listener_id].bind_addr)->sin6_family		= AF_INET6;
		satosin6(&srv_ptr->cfg[listener_id].bind_addr)->sin6_port		= htons(listener->port);
		satosin6(&srv_ptr->cfg[listener_id].bind_addr)->sin6_addr 		= srv_ptr->cfg[listener_id].bind_method == COMM_SERVER_BINDLOOPBACK ? in6addr_loopback : in6addr_any;
		socket_size 													= sizeof(struct sockaddr_in6);
	}
	else
	{
		/* Populate CFG STRUCT */
		satosin(&srv_ptr->cfg[listener_id].bind_addr)->sin_family		= AF_INET;
		satosin(&srv_ptr->cfg[listener_id].bind_addr)->sin_port			= htons(listener->port);
		satosin(&srv_ptr->cfg[listener_id].bind_addr)->sin_addr.s_addr 	= srv_ptr->cfg[listener_id].bind_method == COMM_SERVER_BINDLOOPBACK ? htonl(INADDR_LOOPBACK) : htonl(INADDR_ANY);
		socket_size 													= sizeof(struct sockaddr_in);
	}

	/* Bind the socket to SERVER_ADDR */
	op_status 							= bind(listener->socket_fd, (struct sockaddr *)&srv_ptr->cfg[listener_id].bind_addr, socket_size);

	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed BIND on socket [%d] - ERRNO [%d]\n", listener->socket_fd, errno);
		return COMM_SERVER_FAILURE_BIND;
	}

	/* Start listening */
	op_status 							= listen(listener->socket_fd, COMM_TCP_ACCEPT_QUEUE);

	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed LISTEN on socket [%d] - ERRNO [%d]\n", listener->socket_fd, errno);
		return COMM_SERVER_FAILURE_LISTEN;
	}

	return COMM_SERVER_INIT_OK;
}
/**************************************************************************************************************************/
static int CommEvTCPServerListenUnix(CommEvTCPServer *srv_ptr, int listener_id, char *path_str)
{
	/* Point to selected LISTENER */
	CommEvTCPServerListener *listener	= &srv_ptr->listener.arr[listener_id];
	int op_status						= -1;
	int servaddr_len 					= 0;
	struct sockaddr_un  servaddr_un 	= {0};
	struct sockaddr    *servaddr;
//	struct sockaddr_un  *servaddr_un 	= &srv_ptr->cfg[listener_id].bind_addr;

	/* Populate structure */
	servaddr_un.sun_family  			= AF_UNIX;
	strcpy (servaddr_un.sun_path, path_str);
	/* Remove file if exists */
	unlink(servaddr_un.sun_path);

	/* Cast and calculate size */
	servaddr     = (struct sockaddr *) &servaddr_un;
	servaddr_len = sizeof (struct sockaddr_un) - sizeof (servaddr_un.sun_path) + strlen (path_str);

	/* Bind it */
	op_status = bind (listener->socket_fd, servaddr, servaddr_len);

	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed BIND on socket [%d] - ERRNO [%d]\n", listener->socket_fd, errno);
		return COMM_SERVER_FAILURE_BIND;
	}

	/* Start listening */
	op_status = listen(listener->socket_fd, COMM_TCP_ACCEPT_QUEUE);

	if (op_status < 0)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed LISTEN on socket [%d] - ERRNO [%d]\n", listener->socket_fd, errno);
		return COMM_SERVER_FAILURE_LISTEN;
	}

	/* CHMOD file, so others daemons can read and write */
	op_status 	= chmod(servaddr_un.sun_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);

	op_status 	= chown(servaddr_un.sun_path, 0, 0);

	return COMM_SERVER_INIT_OK;
}
/**************************************************************************************************************************/
static void CommEvTCPServerDispatchEvent(CommEvTCPServer *srv_ptr, CommEvTCPServerConn *conn_hnd, int data_sz, int thrd_id, int ev_type)
{
	CommEvTCPServerListener *listener;
	CommEvTCPServerCBH *cb_handler	= NULL;
	void *cb_handler_data			= NULL;

	/* Point to selected LISTENER */
	listener = conn_hnd ? conn_hnd->listener : 0;

	/* Grab callback_ptr */
	cb_handler = srv_ptr->events[listener->slot_id][ev_type].cb_handler_ptr;

	//printf("CommEvTCPServerDispatchEvent - FD [%d] - LISTENER ID [%d] at FD [%d] - EV_TYPE [%d] - CB_H [%p]\n", conn_hnd->socket_fd, listener->slot_id, listener->socket_fd, ev_type, cb_handler);

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Mark ACCEPT as called if we are invoking ACCEPT_AFTER event */
		if (COMM_SERVER_EVENT_ACCEPT_AFTER == ev_type)
			conn_hnd->flags.conn_accept_invoked = 1;

		/* Grab data for this CBH */
		cb_handler_data = srv_ptr->events[listener->slot_id][ev_type].cb_data_ptr;

		/* Jump into CBH. Use conn_hnd fd if it exists, otherwise, send accept fd. Base for this event is CommEvTCPServer* */
		cb_handler(conn_hnd ? conn_hnd->socket_fd : listener->socket_fd, data_sz, thrd_id, cb_handler_data, srv_ptr);
	}

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTCPServerEventProtocolIdentifyTimeout(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base					= base_ptr;
	CommEvTCPServerConn *conn_hnd		= cb_data;
	CommEvTCPServer *parent_srv			= conn_hnd->parent_srv;
	CommEvTCPServerListener *listener	= conn_hnd->listener;
	int listener_id						= conn_hnd->listener->slot_id;

	KQBASE_LOG_PRINTF(parent_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - TIMED OUT\n", fd);

	/* Mark SSL as disabled */
	conn_hnd->flags.ssl_enabled				= 0;

	/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
	CommEvTCPServerDispatchEvent(parent_srv, conn_hnd, read_sz, thrd_id, COMM_SERVER_EVENT_ACCEPT_AFTER);

	/* Set default READ and CLOSE events to this conn_fd, if any is defined in server */
	CommEvTCPServerConnSetDefaultEvents(parent_srv, conn_hnd);

	/* Set disconnect and read internal events for newly connected socket */
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventClose, conn_hnd);
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventRead, conn_hnd);

	/* Fire up data rate calculation timer if flag is set */
	COMM_EV_STATS_CONN_HND_FIRE_TIMER(conn_hnd);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerEventProtocolIdentify(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	char peek_buf[4];
	char peek_buf_sz;

	EvKQBase *ev_base					= base_ptr;
	CommEvTCPServerConn *conn_hnd		= cb_data;
	CommEvTCPServer *parent_srv			= conn_hnd->parent_srv;
	CommEvTCPServerListener *listener	= conn_hnd->listener;
	int listener_id						= conn_hnd->listener->slot_id;

	/* Clean stack area */
	memset(&peek_buf, 0, sizeof(peek_buf));

	/* We want to PEEK some data from FD to try to guess protocol */
	peek_buf_sz = recv(conn_hnd->socket_fd, (unsigned char*)&peek_buf, sizeof(peek_buf), MSG_PEEK);

	/* Failed PEEKING */
	if (peek_buf_sz < sizeof(peek_buf))
		goto dispatch_plain;

	/* Look the first bytes of our PEEKed buffer, and take a guess if its really a SSL handshake */
	if (!((peek_buf_sz >= 4) && (((peek_buf[0] == 0x16) && (peek_buf[1] == 0x03)) || ((peek_buf[0] == 0x50) && (peek_buf[2] == 0x01)))))
	{
		KQBASE_LOG_PRINTF(parent_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Not running SSL, dispatching plain\n", fd);
		goto dispatch_plain;
	}
	else
	{
		KQBASE_LOG_PRINTF(parent_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - SSL detected, jumping to SNI READ\n", fd);
		goto dispatch_ssl;
	}

	return 1;

	/* TAG to dispatch SSL connection */
	dispatch_ssl:

	/* Mark SSL as enabled */
	conn_hnd->flags.ssl_enabled				= 1;
	conn_hnd->ssldata.sni_parse_trycount	= 0;

	/* Clean certificate and set forge request to -1 */
	conn_hnd->ssldata.x509_cert				= NULL;
	conn_hnd->ssldata.x509_forge_reqid		= -1;

	/* Jump into SNI peek event */
	CommEvTCPServerEventSNIPeek(conn_hnd->socket_fd, read_sz, thrd_id, conn_hnd, ev_base);

	return 1;

	/* TAG to dispatch PLAINTEXT connection */
	dispatch_plain:

	/* Mark SSL as disabled */
	conn_hnd->flags.ssl_enabled				= 0;

	/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
	CommEvTCPServerDispatchEvent(parent_srv, conn_hnd, read_sz, thrd_id, COMM_SERVER_EVENT_ACCEPT_AFTER);

	/* Set default READ and CLOSE events to this conn_fd, if any is defined in server */
	CommEvTCPServerConnSetDefaultEvents(parent_srv, conn_hnd);

	/* Fire up data rate calculation timer if flag is set */
	COMM_EV_STATS_CONN_HND_FIRE_TIMER(conn_hnd);

	/* Set disconnect and read internal events for newly connected socket */
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventClose, conn_hnd);
	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventRead, conn_hnd);

	/* Dispatch a read event for this pending bytes on FD buffer */
	CommEvTCPServerEventRead(conn_hnd->socket_fd, read_sz, thrd_id, conn_hnd, ev_base);

	return 1;

}
/**************************************************************************************************************************/
static int CommEvTCPServerEventSSLHandshakeDeferDummyReadEvent(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base					= base_ptr;
	CommEvTCPServerConn *conn_hnd		= cb_data;
	CommEvTCPServer *parent_srv			= conn_hnd->parent_srv;

	KQBASE_LOG_PRINTF(parent_srv->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - DUMMY READ EVENT - [%d] BYTES\n", fd, read_sz);

	return read_sz;
}
/**************************************************************************************************************************/
static int CommEvTCPServerEventSNIPeek(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base					= base_ptr;
	CommEvTCPServerConn *conn_hnd		= cb_data;
	CommEvTCPServer *srv_ptr			= conn_hnd->parent_srv;
	CommEvTCPServerListener *listener	= conn_hnd->listener;
	int listener_id						= conn_hnd->listener->slot_id;

	char sni_buf[1024];
	int sni_read_sz;
	int sni_parse_sz;
	int sni_parse_status;

	/* We want to PEEK some data from FD to preview TLS/SNI info */
	sni_parse_sz = sni_read_sz = recv(conn_hnd->socket_fd, (unsigned char*)&sni_buf, sizeof(sni_buf), MSG_PEEK);

	/* Failed PEEKING */
	if (sni_read_sz < 0)
		goto reschedule;

	/* Set flags and cleanup buffer */
	conn_hnd->ssldata.sni_host_tldpos		= 0;
	conn_hnd->flags.ssl_handshake_defer		= 0;
	conn_hnd->flags.ssl_handshake_unknown	= 0;

	/* Look the first bytes of our PEEKed buffer, and take a guess if its really a SSL handshake */
	if (!((sni_parse_sz >= 4) && (((sni_buf[0] == 0x16) && (sni_buf[1] == 0x03)) || ((sni_buf[0] == 0x50) && (sni_buf[2] == 0x01)))))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - UNKNOWN SSL HANDSHAKE\n", fd);
		conn_hnd->flags.ssl_handshake_unknown	= 1;
	}

	/* Parse SNI data */
	conn_hnd->ssldata.sni_host_ptr = calloc(1, 256);
	sni_parse_status = CommEvSSLUtils_SNIParse((unsigned char*)&sni_buf, &sni_parse_sz, conn_hnd->ssldata.sni_host_ptr, 255);

	//KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - PARSE_STATUS [%d] - PARSE_SZ [%d]\n", fd, sni_parse_status, sni_parse_sz);

	/* SNI parsing failed because we need more data */
	if (sni_parse_status < 0)
	{
		//KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "RESCHEDULE - SNI_PARSE_STATUS [%d]\n", sni_parse_status);

		/* Reset SNI data */
		free(conn_hnd->ssldata.sni_host_ptr);
		conn_hnd->ssldata.sni_host_ptr = NULL;
		goto reschedule;
	}

	/* Calculate SNI string size */
	conn_hnd->ssldata.sni_host_strsz = strlen(conn_hnd->ssldata.sni_host_ptr);

	/* SNI host found, set flags */
	if (conn_hnd->ssldata.sni_host_strsz > 0)
	{
		//KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Parsed TLS/VHOST is [%s]\n", fd, conn_hnd->ssldata.sni_host_str);

		/* We have SNI information on this connection */
		conn_hnd->flags.tls_has_sni = 1;
	}
	/* Set flags to indicate there is no SNI information on this connection */
	else
	{
		conn_hnd->flags.tls_has_sni			= 0;
		conn_hnd->ssldata.sni_host_strsz	= -1;

		/* Reset SNI data */
		free(conn_hnd->ssldata.sni_host_ptr);
		conn_hnd->ssldata.sni_host_ptr = NULL;
	}

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - WILL DISPATCH SNI_PARSE EVENT\n", fd);

	/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
	CommEvTCPServerDispatchEvent(conn_hnd->parent_srv, conn_hnd, read_sz, thrd_id, COMM_SERVER_EVENT_ACCEPT_SNIPARSE);

	/* Just handshake if not asked by upper layers to defer (usually for dynamic x.509 forging) */
	if (conn_hnd->flags.ssl_handshake_defer)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - WILL DEFER SSL_HANDSHAKE\n", fd);

		/* Set a DUMMY read event so we can detect EOF events between DEFER_HANDSHAKE and its back way from CommEvTCPServerKickConnFromAcceptDefer */
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_TRANSITION, CommEvTCPServerEventSSLHandshakeDeferDummyReadEvent, conn_hnd);

	}
	else if (conn_hnd->flags.ssl_handshake_abort)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - ABORT SSL HANDSHAKE - Client must take care of what to do\n", fd);

		/* Fire up data rate calculation timer if flag is set */
		COMM_EV_STATS_CONN_HND_FIRE_TIMER(conn_hnd);
	}
	else
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - WILL NOT DEFER SSL_HANDSHAKE - Jump into SSL_ACCEPT\n", fd);

		/* Create a new SSL conn_client context if needed and begin SSL handshake */
		CommEvTCPServerConnSSLSessionInit(conn_hnd);
		CommEvTCPServerEventSSLAccept(fd, read_sz, thrd_id, conn_hnd, base_ptr);
	}

	EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventClose, conn_hnd);
	return 1;

	/* Tag to reschedule for another read */
	reschedule:

	/* Too many SNI parse tries, move on to generic TLS accept */
	if (conn_hnd->ssldata.sni_parse_trycount > 15)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - FAILED PEEK from TLS socket - RETCODE [%d] - ERRNO [%d] - Too many parses, move to SSL_ACCEPT\n",
				fd, sni_read_sz, errno);

		/* Create a new SSL conn_client context if needed and begin SSL handshake */
		CommEvTCPServerConnSSLSessionInit(conn_hnd);
		CommEvTCPServerEventSSLAccept(fd, read_sz, thrd_id, conn_hnd, base_ptr);

		return 0;
	}
	else
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - FAILED PEEK from TLS socket - RETCODE [%d] - ERRNO [%d] - WILL RESCHEDULE\n", fd, sni_read_sz, errno);
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSNIPeek, conn_hnd);
	}

	return 0;
}
/**************************************************************************************************************************/
static int CommEvTCPServerEventListenerClose(int fd, int accept_queue_sz, int thrd_id, void *cb_data, void *base_ptr)
{

	abort();

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerEventAccept(int fd, int accept_queue_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServerConn *conn_hnd;
	EvBaseKQFileDesc *kq_fd;
	struct sockaddr_in clientaddr;
	int conn_fd;

	EvKQBase *ev_base					= base_ptr;
	CommEvTCPServerListener *listener	= cb_data;
	CommEvTCPServer *srv_ptr			= listener->parent_srv;
	unsigned int sockaddr_sz			= sizeof(struct sockaddr_in);
	int listener_id						= listener->slot_id;
	int accept_count					= 0;
	int accept_max						= COMM_TCP_ACCEPT_QUEUE;

	CommEvTCPServerCBH *cb_handler		= NULL;
	void *cb_handler_data				= NULL;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "ACCEPT EVENT FD [%d] - QUEUE_SZ [%d] - THRD [%d]\n", fd, accept_queue_sz, thrd_id);

	/* Dispatch ACCEPT_BEFORE event, still with a NULL CONN_HND */
	cb_handler		= srv_ptr->events[listener_id][COMM_SERVER_EVENT_ACCEPT_BEFORE].cb_handler_ptr;
	cb_handler_data = srv_ptr->events[listener_id][COMM_SERVER_EVENT_ACCEPT_BEFORE].cb_data_ptr;

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
		cb_handler(listener->socket_fd, accept_queue_sz, thrd_id, cb_handler_data, srv_ptr);

	/* Accept as many connections as we can in the same IO loop */
	while ((accept_count < accept_queue_sz) && (accept_count < accept_max))
	{
		/* Accept the connection */
		conn_fd = accept(fd, (struct sockaddr *)&clientaddr, &sockaddr_sz);

		/* Check if succeeded accepting connection */
		if (conn_fd > 0)
		{
			/* Grab a connection handler from server internal arena */
			conn_hnd	= CommEvTCPServerConnArenaGrab(srv_ptr, conn_fd);
			kq_fd		= EvKQBaseFDGrabFromArena(ev_base, conn_fd);

			/* Not received from UNIX */
			conn_hnd->flags.conn_recvd_from_unixsrv = 0;

			/* Touch flags */
			kq_fd->flags.active		= 1;
			kq_fd->flags.closed		= 0;
			kq_fd->flags.closing	= 0;

			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Accepted NEW_FD on CONN_FD [%d]\n", fd, conn_fd);

			/* Common POST_ACCEPT initialization procedure and set a CLOSE event */
			CommEvTCPServerAcceptPostInit(srv_ptr, listener, conn_fd, accept_queue_sz, thrd_id);
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventClose, conn_hnd);

			/* Increment accept count */
			accept_count++;
		}
		else
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Accept failed on LID [%d] - ERR [%d] - ERRNO [%d]\n", fd, listener_id, conn_fd, errno);

			/* Dispatch ACCEPT_FAIL event, still with a NULL CONN_HND */
			cb_handler		= srv_ptr->events[listener_id][COMM_SERVER_EVENT_ACCEPT_FAIL].cb_handler_ptr;
			cb_handler_data = srv_ptr->events[listener_id][COMM_SERVER_EVENT_ACCEPT_FAIL].cb_data_ptr;

			/* There is a handler for this event. Invoke the damn thing */
			if (cb_handler)
				cb_handler(listener->socket_fd, accept_count, thrd_id, cb_handler_data, srv_ptr);

			break;
		}

		continue;
	}

	return accept_count;
}
/**************************************************************************************************************************/
static int CommEvTCPServerEventSSLAccept(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPServerConn *conn_hnd	= cb_data;
	CommEvTCPServer *srv_ptr		= conn_hnd->parent_srv;
	int op_status;
	int ssl_error;

	//KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - THRD [%d] - READ_SZ [%d]\n", fd, thrd_id, read_sz);

	assert(conn_hnd->ssldata.ssl_handle);

	/* Set flags as ONGOING SSL HANDSHAKE and increment count */
	conn_hnd->flags.ssl_handshake_ongoing	= 1;
	conn_hnd->ssldata.ssl_negotiatie_trycount++;

	/* Too many negotiation retries, give up */
	if (conn_hnd->ssldata.ssl_negotiatie_trycount > 50)
		goto negotiation_failed;

	/* Clear libSSL errors and invoke SSL handshake mechanism */
	ERR_clear_error();
	op_status = SSL_accept(conn_hnd->ssldata.ssl_handle);

	/* Failed to connect on this try, check what is going on */
	if (op_status <= 0)
	{
		ssl_error = SSL_get_error(conn_hnd->ssldata.ssl_handle, op_status);

		//KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - LID [%d] - SSL_ERROR [%d]\n", conn_hnd->socket_fd, conn_hnd->listener->slot_id, ssl_error);

		switch (ssl_error)
		{

		case SSL_ERROR_WANT_READ:
		{
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLAccept, conn_hnd);
			return 0;
		}

		case SSL_ERROR_WANT_WRITE:
		{
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLAccept, conn_hnd);
			return 0;
		}

		default:
		{
			goto negotiation_failed;
			return 0;
		}
		}
	}
	/* SSL connected OK */
	else
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - LID [%d] - SSL Handshake OK\n", conn_hnd->socket_fd, conn_hnd->listener->slot_id);

		/* Reset HANDHSAKE defer flag */
		conn_hnd->flags.ssl_handshake_defer		= 0;
		conn_hnd->flags.ssl_handshake_ongoing	= 0;

		/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
		CommEvTCPServerDispatchEvent(conn_hnd->parent_srv, conn_hnd, read_sz, thrd_id, COMM_SERVER_EVENT_ACCEPT_AFTER);

		/* Set default READ and CLOSE events to this conn_fd, if any is defined in server */
		CommEvTCPServerConnSetDefaultEvents(conn_hnd->parent_srv, conn_hnd);

		/* Set read internal events for newly connected socket */
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLRead, conn_hnd);

		/* Fire up data rate calculation timer if flag is set */
		COMM_EV_STATS_CONN_HND_FIRE_TIMER(conn_hnd);

		return 1;
	}

	/* Negotiation failed label */
	negotiation_failed:

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - Failed to accept client on LID [%d]\n", conn_hnd->socket_fd, conn_hnd->listener->slot_id);

	/* Dispatch ACCEPT_FAIL event followed by a CLOSE event */
	CommEvTCPServerConnDispatchEventByFD(conn_hnd->parent_srv, conn_hnd->socket_fd, ssl_error, thrd_id, CONN_EVENT_SSL_HANDSHAKE_FAIL);

	/* SSL is not enabled anymore */
	conn_hnd->flags.ssl_enabled				= 0;
	conn_hnd->flags.ssl_handshake_ongoing	= 0;
	CommEvTCPServerConnClose(conn_hnd);

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* PLAIN CONN I/O support functions
/**************************************************************************************************************************/
static int CommEvTCPServerEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPServerConn *conn_hnd	= cb_data;
	CommEvTCPServer	*srv_ptr		= conn_hnd->parent_srv;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, conn_hnd->socket_fd);
	int total_wrote_sz				= 0;

	CommEvTCPIOResult ioret;
	int op_status;

	/* Invoke IO mechanism to write data */
	op_status = CommEvTCPAIOWrite(ev_base, srv_ptr->log_base, &conn_hnd->statistics, &conn_hnd->iodata, &ioret, srv_ptr, can_write_sz,
			(!conn_hnd->flags.close_request));

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "FD [%d] - Wrote [%d] bytes - Already closing, bail out\n", kq_fd->fd, ioret.aio_total_sz);
		return ioret.aio_total_sz;
	}

	/* Jump into FSM */
	switch (op_status)
	{
	case COMM_TCP_AIO_WRITE_NEEDED:
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "FD [%d] - Reschedule WRITE_EV - Wrote [%d] bytes\n", conn_hnd->socket_fd, ioret.aio_total_sz);

		/* Reschedule write event and SET pending WRITE FLAG */
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventWrite, conn_hnd);
		conn_hnd->flags.pending_write = 1;

		return ioret.aio_total_sz;
	}

	/* All writes FINISHED */
	case COMM_TCP_AIO_WRITE_FINISHED:
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "FD [%d] - FINISHED - Wrote [%d] bytes\n", conn_hnd->socket_fd, ioret.aio_total_sz);

		/* Reset pending write flag */
		conn_hnd->flags.pending_write = 0;

		/* Upper layers requested to close after writing all */
		if (conn_hnd->flags.close_request)
		{
			KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_ORANGE, "FD [%d] - Upper layer set CLOSE_REQUEST, write buffer is empty\n", conn_hnd->socket_fd);

			/* Disconnect */
			CommEvTCPServerConnClose(conn_hnd);
		}

		return ioret.aio_total_sz;
	}
	case COMM_TCP_AIO_WRITE_ERR_FATAL:
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Fatal error after [%d] bytes\n", conn_hnd->socket_fd, ioret.aio_total_sz);

		/* Has close request, invoke */
		if (conn_hnd->flags.close_request)
			CommEvTCPServerConnClose(conn_hnd);

		return ioret.aio_total_sz;
	}
	default:
		BRB_ASSERT_FMT(ev_base, 0, "Undefined state [%d]\n", op_status);
		return ioret.aio_total_sz;
	}

	return ioret.aio_total_sz;
}
/**************************************************************************************************************************/
static int CommEvTCPServerEventRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPServerConn *conn_hnd	= cb_data;
	CommEvTCPServer *tcp_srv		= conn_hnd->parent_srv;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);
	int listener_id					= conn_hnd->listener->slot_id;
	int data_read					= 0;

	KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - CLOSED [%d] - ADDR [%s] - [%d] bytes buffered to read\n",
			fd, kq_fd->flags.closed, conn_hnd->string_ip, can_read_sz);

	/* Cancel any pending read timeout set here, as it will not pass to BASE timeout cancel function */
	EvKQBaseTimeoutClear(ev_base, fd, COMM_EV_TIMEOUT_READ);
	EvKQBaseTimeoutClear(ev_base, fd, COMM_EV_TIMEOUT_BOTH);

	/* This is a closing connection, bail out */
	if (can_read_sz <= 0)
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Returning\n", fd);
		return 0;
	}

	/* Process the READ BUFFER */
	data_read = CommEvTCPServerEventProcessBuffer(conn_hnd, can_read_sz, thrd_id, NULL, 0);

	/* Reschedule read if we have not been closed and if there is a data event for this FD */
	if ((!kq_fd->flags.closed && !kq_fd->flags.closing) && (conn_hnd->events[CONN_EVENT_READ].cb_handler_ptr))
	{
		/* Touch statistics */
		if (data_read > 0)
		{
			conn_hnd->statistics.total[COMM_CURRENT].byte_rx			+= data_read;
			conn_hnd->statistics.total[COMM_CURRENT].packet_rx			+= 1;
		}

		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventRead, conn_hnd);

		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - CLOSED [%d] - EV_PTR [%p] - Reschedule READ_EV\n",
				fd, kq_fd->flags.closed, conn_hnd->events[CONN_EVENT_READ].cb_handler_ptr);
	}
	else
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - CLOSED [%d] - READ_EV NOT rescheduled\n",
				fd, kq_fd->flags.closed);
	}

	return data_read;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
/* SSL CONN I/O support functions
/**************************************************************************************************************************/
static int CommEvTCPServerEventSSLWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOReq *cur_aio_req;
	int ssl_bytes_written;
	int ssl_error;

	EvKQBase *ev_base				= base_ptr;
	CommEvTCPServerConn *conn_hnd	= cb_data;
	CommEvTCPServer *tcp_srv		= conn_hnd->parent_srv;
	int total_ssl_bytes_written		= 0;

	/* Client is going down, bail out */
	if (can_write_sz <= 0)
		return 0;

	assert(conn_hnd->ssldata.ssl_handle);

	/* Grab aio_req unit */
	cur_aio_req	= EvAIOReqQueueDequeue(&conn_hnd->iodata.write_queue);

	/* Nothing to write, bail out */
	if (!cur_aio_req)
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Empty WRITE_LIST.. STOP\n",	fd);

		/* Reset pending write flag */
		conn_hnd->flags.pending_write = 0;
		return total_ssl_bytes_written;
	}

	/* Touch statistics */
	conn_hnd->statistics.total[COMM_CURRENT].packet_tx		+= 1;

	/* Clear libSSL errors and write it to SSL tunnel */
	ERR_clear_error();
	ssl_bytes_written = SSL_write(conn_hnd->ssldata.ssl_handle, cur_aio_req->data.ptr, cur_aio_req->data.size);

	/* Failed writing to SSL tunnel */
	if (ssl_bytes_written <= 0)
	{
		/* Grab SSL error to process */
		ssl_error = SSL_get_error(conn_hnd->ssldata.ssl_handle, ssl_bytes_written);

		/* Push AIO_REQ queue back for writing and set pending write flag */
		EvAIOReqQueueEnqueueHead(&conn_hnd->iodata.write_queue, cur_aio_req);
		conn_hnd->flags.pending_write = 1;

		/* Decide based on SSL error */
		switch (ssl_error)
		{
		case SSL_ERROR_WANT_READ:
		{
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLWrite, conn_hnd);
			return 0;
		}
		case SSL_ERROR_WANT_WRITE:
		{
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLWrite, conn_hnd);
			return 0;
		}

		/* Receive peer close notification, move to shutdown */
		case SSL_ERROR_NONE:
		case SSL_ERROR_ZERO_RETURN:
		{
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLWrite, conn_hnd);
			return 0;
		}

		case SSL_ERROR_SYSCALL:
		default:
		{
			/* Mark flags as fatal error and pending write */
			conn_hnd->flags.ssl_fatal_error = 1;

			/* Trigger the SSL shutdown mechanism (post-2008) */
			CommEvTCPServerConnSSLShutdownBegin(conn_hnd);
			return 0;
		}
		}
	}

	cur_aio_req->data.offset								+= ssl_bytes_written;
	/* Touch statistics */
	conn_hnd->statistics.total[COMM_CURRENT].byte_tx		+= cur_aio_req->data.size;
	conn_hnd->statistics.total[COMM_CURRENT].ssl_byte_tx	+= ssl_bytes_written;
	total_ssl_bytes_written									+= ssl_bytes_written;

	/* Invoke notification CALLBACKS if not CLOSE_REQUEST and then destroy AIO REQ */
	if (!conn_hnd->flags.close_request)
		EvAIOReqInvokeCallBacks(cur_aio_req, 1, fd, cur_aio_req->data.offset, thrd_id, conn_hnd->parent_srv);
	EvAIOReqDestroy(cur_aio_req);

	/* We have more items to write, reschedule write event */
	if (!EvAIOReqQueueIsEmpty(&conn_hnd->iodata.write_queue))
	{
		/* SET pending write flag */
		conn_hnd->flags.pending_write = 1;
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLWrite, conn_hnd);
	}
	/* Write list is empty */
	else
	{
		/* Reset pending write flag */
		conn_hnd->flags.pending_write = 0;

		/* Upper layers requested to close after writing all */
		if (conn_hnd->flags.close_request)
		{
			KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Upper layer set CLOSE_REQUEST, write buffer is empty, closing..\n", conn_hnd->socket_fd);
			CommEvTCPServerConnClose(conn_hnd);
		}
	}

	return ssl_bytes_written;

}
/**************************************************************************************************************************/
static int CommEvTCPServerEventSSLRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPServerConn *conn_hnd	= cb_data;
	CommEvTCPServer *tcp_srv		= conn_hnd->parent_srv;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, fd);
	int listener_id					= conn_hnd->listener->slot_id;

	char read_buf[COMM_TCP_SSL_READ_BUFFER_SZ];
	int ssl_bytes_read;
	int ssl_error;

	/* This is a closing connection, bail out */
	if (can_read_sz <= 0)
		return 0;

	assert(conn_hnd->ssldata.ssl_handle);

	/* Clear libSSL errors and read from SSL tunnel */
	ERR_clear_error();
	ssl_bytes_read = SSL_read(conn_hnd->ssldata.ssl_handle, &read_buf, sizeof(read_buf) - 1);

	//KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SIZE [%d] - READ [%d]\n", fd, read_sz, bytes_read);

	/* Account for RAW DATA bytes */
	conn_hnd->statistics.total[COMM_CURRENT].packet_rx			+= 1;

	/* Check errors */
	if (ssl_bytes_read <= 0)
	{
		ssl_error = SSL_get_error(conn_hnd->ssldata.ssl_handle, ssl_bytes_read);

		switch (ssl_error)
		{
		case SSL_ERROR_WANT_READ:
		{
			//KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - SSL_ERROR_WANT_READ [%d]\n", conn_hnd->socket_fd, ssl_error);
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLRead, conn_hnd);
			return 0;
		}
		case SSL_ERROR_WANT_WRITE:
		{
			//KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - SSL_ERROR_WANT_WRITE [%d]\n", conn_hnd->socket_fd, ssl_error);
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLRead, conn_hnd);
			return 0;
		}

		/* Receive peer close notification, move to shutdown */
		case SSL_ERROR_NONE:
		case SSL_ERROR_ZERO_RETURN:
		{
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLRead, conn_hnd);
			return 0;
		}

		case SSL_ERROR_SYSCALL:
		default:
		{
			//KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Fatal SSL_ERROR [%d]\n", conn_hnd->socket_fd, ssl_error);

			/* Mark flags as fatal error */
			conn_hnd->flags.ssl_fatal_error = 1;

			/* Trigger the SSL shutdown mechanism (post-2008) */
			CommEvTCPServerConnSSLShutdownBegin(conn_hnd);
			return 0;
		}
		}
	}
	/* Success reading */
	else
	{
		//KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "SUCCESS READING [%d] bytes - SZ [%d]\n", bytes_read, read_sz);
		CommEvTCPServerEventProcessBuffer(conn_hnd, can_read_sz, thrd_id, (char *)&read_buf, ssl_bytes_read);

		/* Touch SSL-side statistics */
		conn_hnd->statistics.total[COMM_CURRENT].ssl_byte_rx	+= ssl_bytes_read;
		conn_hnd->statistics.total[COMM_CURRENT].byte_rx		+= ssl_bytes_read;
	}

	/* Reschedule READ event */
	if ((!kq_fd->flags.closed && !kq_fd->flags.closing) && (conn_hnd->events[CONN_EVENT_READ].cb_handler_ptr))
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSSLRead, conn_hnd);

	/* SSL read must return zero so lower layers do not get tricked by missing bytes in kernel */
	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTCPServerEventClose(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvTCPServerConn *conn_hnd	= cb_data;
	CommEvTCPServer *srv_ptr		= conn_hnd->parent_srv;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, conn_hnd->socket_fd);

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE,	"FD [%d] from addr [%s] - THRD_ID [%d] - [%d] bytes buffered to read\n",
			conn_hnd->socket_fd, conn_hnd->string_ip, thrd_id, read_sz);

	/* Mark socket as EOF */
	conn_hnd->flags.socket_eof = 1;

	/* There is a close request for this client. Upper layers do not care any longer, just finish */
	if (conn_hnd->flags.close_request)
		goto finish;

	/* If we have data and are not on SSL neither DEFERING SSL_HANDSHAKE, and also not using PEEK, do not close */
	if ((read_sz > 0) && !conn_hnd->flags.ssl_enabled && !conn_hnd->flags.ssl_handshake_defer && !conn_hnd->flags.peek_on_read)
	{
		KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Not closing, %d bytes pending on READ\n", conn_hnd->socket_fd, read_sz);
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventClose, conn_hnd);
		return 0;
	}

	/* Notify upper layers handshake has failed while waiting for DEFER */
	if ((conn_hnd->flags.ssl_enabled) && (conn_hnd->flags.ssl_handshake_defer || conn_hnd->flags.ssl_handshake_ongoing))
	{
		CommEvTCPServerConnDispatchEventByFD(conn_hnd->parent_srv, conn_hnd->socket_fd, read_sz, thrd_id, CONN_EVENT_SSL_HANDSHAKE_FAIL);
	}
	/* Dispatch event before destroying IO structures to give a chance for the operator to use it */
	else if ((conn_hnd->flags.conn_accept_invoked) || (conn_hnd->flags.ssl_handshake_abort))
	{
		CommEvTCPServerConnDispatchEventByFD(conn_hnd->parent_srv, conn_hnd->socket_fd, read_sz, thrd_id, CONN_EVENT_CLOSE);
	}

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return 0;

	/* Finish connection */
	finish:
	CommEvTCPServerConnClose(conn_hnd);
	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerEventProcessBuffer(CommEvTCPServerConn *conn_hnd, int read_sz, int thrd_id, char *read_buf, int read_buf_sz)
{
	CommEvTCPServer *tcp_srv		= conn_hnd->parent_srv;
	EvKQBase *ev_base				= tcp_srv->kq_base;
	MemBuffer *transformed_mb		= NULL;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, conn_hnd->socket_fd);
	int listener_id					= conn_hnd->listener->slot_id;
	int data_read					= 0;

	char *transform_ptr;
	long data_read_cur;

	switch (tcp_srv->cfg[listener_id].read_mthd)
	{
	/*********************************************************************/
	case COMM_SERVER_READ_MEMBUFFER:
	{
		/* Create a new read_buffer object */
		if (!conn_hnd->iodata.read_buffer)
		{
			/* Check if have partial buffer, and as read is empty, just switch pointers and set partial to NULL */
			if (conn_hnd->iodata.partial_read_buffer)
			{
				conn_hnd->iodata.read_buffer			= conn_hnd->iodata.partial_read_buffer;
				conn_hnd->iodata.partial_read_buffer 	= NULL;
			}
			/* Create a new read buffer */
			else
				conn_hnd->iodata.read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (read_sz + 1));
		}

		/* There is a partial buffer left, copy it back to read buffer */
		if (conn_hnd->iodata.partial_read_buffer)
		{
			MemBufferAdd(conn_hnd->iodata.read_buffer, MemBufferDeref(conn_hnd->iodata.partial_read_buffer), MemBufferGetSize(conn_hnd->iodata.partial_read_buffer));
			MemBufferClean(conn_hnd->iodata.partial_read_buffer);
		}

		/* Absorb data from READ_BUFFER */
		if (read_buf_sz > 0)
		{
			/* Allow upper layers to perform data transformation */
			transformed_mb		= EvAIOReqTransform_ReadData(&conn_hnd->transform, read_buf, read_buf_sz);

			/* No transformed MB, append read_buf */
			if (!transformed_mb)
				MemBufferAdd(conn_hnd->iodata.read_buffer, read_buf, read_buf_sz);

			data_read 	= read_buf_sz;
		}
		/* Grab data from FD directly into read_buffer, zero-copy */
		else
		{
			/* Get current size */
			data_read_cur 	= MemBufferGetSize(conn_hnd->iodata.read_buffer);

			/* Read data from FD */
			data_read 		= MemBufferAppendFromFD(conn_hnd->iodata.read_buffer, read_sz, conn_hnd->socket_fd, (conn_hnd->flags.peek_on_read ? MSG_PEEK : 0));

			/* Read OK */
			if (data_read > 0)
			{
				transform_ptr	= MemBufferDeref(conn_hnd->iodata.read_buffer);
				transform_ptr 	= transform_ptr + data_read_cur;

				/* Allow upper layers to perform data transformation */
				transformed_mb	= EvAIOReqTransform_ReadData(&conn_hnd->transform, transform_ptr, data_read);

				/* Correct size to add transformed MB */
				if (transformed_mb)
					conn_hnd->iodata.read_buffer->size = data_read_cur;
			}
			/* Failed reading FD */
			else
			{
				/* NON_FATAL error */
				//				if ((!kq_fd->flags.so_read_eof) && (errno == EINTR || errno == EAGAIN))
				//					return -1;
				//
				//				KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - Error reading [%d] of [%d] bytes - ERRNO [%d]\n",
				//						conn_hnd->socket_fd, data_read, read_sz, errno);
				return -1;
			}

		}

		/* Data has been transformed, add to READ_BUFFER */
		if (transformed_mb)
		{
			/* Add Read Buffer into read_buffer */
			MemBufferAdd(conn_hnd->iodata.read_buffer, MemBufferDeref(transformed_mb), MemBufferGetSize(transformed_mb));
			MemBufferDestroy(transformed_mb);
		}

		break;
	}
	/*********************************************************************/
	case COMM_SERVER_READ_MEMSTREAM:
	{
		/* Create a new read_stream object */
		if (!conn_hnd->iodata.read_stream)
		{
			if (ev_base->flags.mt_engine)
				conn_hnd->iodata.read_stream = MemStreamNew( (read_sz + 1), MEMSTREAM_MT_SAFE);
			else
				conn_hnd->iodata.read_stream = MemStreamNew( (read_sz + 1), MEMSTREAM_MT_UNSAFE);
		}

		/* Absorb data from READ_BUFFER */
		if (read_buf_sz > 0)
		{
			/* Add SSL buffer into plain read_stream */
			MemStreamWrite(conn_hnd->iodata.read_stream, read_buf, read_buf_sz);
			data_read = read_buf_sz;
		}
		/* Grab data from FD directly into read_stream, zero-copy */
		else
			data_read = MemStreamGrabDataFromFD(conn_hnd->iodata.read_stream, read_sz, conn_hnd->socket_fd);

		break;
	}
	/*********************************************************************/
	}

	/* Upper layers asked for self_sync, so invoke it */
	if (conn_hnd->flags.self_sync)
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE,	"FD [%d] - IP [%s] - SIZE [%d] - Jumping to SELF_SYNC code\n",
				conn_hnd->socket_fd, conn_hnd->string_ip, read_sz);

		/* Jump into SELF_SYNC code - This will dispatch READ_EV to upper layers when needed */
		CommEvTCPServerSelfSyncReadBuffer(conn_hnd, read_sz, thrd_id);
	}
	/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
	else
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE,	"FD [%d] - IP [%s] - SIZE [%d] - READ_EV direct INVOKE\n",
				conn_hnd->socket_fd, conn_hnd->string_ip, read_sz);

		/* Dispatch READ_EV directly for this lower event */
		CommEvTCPServerConnDispatchEventByFD(tcp_srv, conn_hnd->socket_fd, read_sz, thrd_id, CONN_EVENT_READ);
	}

	return data_read;
}
/**************************************************************************************************************************/
static int CommEvTCPServerSelfSyncReadBuffer(CommEvTCPServerConn *conn_hnd, int orig_read_sz, int thrd_id)
{
	char *partial_ptr;
	char *read_buffer_ptr;
	char *request_str_ptr;
	int request_str_sz;
	int remaining_sz;
	int i, j;

	CommEvTCPServer *tcp_srv	= conn_hnd->parent_srv;
	EvKQBase *ev_base			= tcp_srv->kq_base;
	int listener_id				= conn_hnd->listener->slot_id;
	char *token_str				= conn_hnd->self_sync.token_str;
	int max_buffer_sz			= conn_hnd->self_sync.max_buffer_sz;
	int token_sz				= conn_hnd->self_sync.token_str ? strlen(conn_hnd->self_sync.token_str) : 0;
	int token_found				= 0;

	/* Flag not set, or buffer is empty, bail out */
	if ((!conn_hnd->flags.self_sync) || (!conn_hnd->iodata.read_buffer) || (!conn_hnd->self_sync.token_str))
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - IP [%s] - Invalid SELF_SYNC condition!\n", conn_hnd->socket_fd, conn_hnd->string_ip);
		return 0;
	}

	/* Make sure buffer if NULL terminated before dealing with it as a string */
	MemBufferPutNULLTerminator(conn_hnd->iodata.read_buffer);

	/* Get information about original string encoded request array */
	request_str_ptr	= MemBufferDeref(conn_hnd->iodata.read_buffer);
	request_str_sz	= MemBufferGetSize(conn_hnd->iodata.read_buffer);

	/* Sanity check */
	if (request_str_sz < token_sz)
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - IP [%s] - Request smaller than TOKEN, bail out\n", conn_hnd->socket_fd, conn_hnd->string_ip);
		return 0;
	}

	KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - IP [%s] - REQ_SZ [%d] - TOKEN_SZ [%d] - Processing request\n",
			conn_hnd->socket_fd, conn_hnd->string_ip, request_str_sz, token_sz);

	//EvKQBaseLogHexDump(request_str_ptr, request_str_sz, 8, 4);

	/* Start searching the buffer */
	for (j = 0, i = (request_str_sz); i >= 0; i--, j++)
	{
		/* Found finish of token, compare full token versus buffer */
		if ( ((j >= token_sz) && (request_str_ptr[i] == token_str[0])) && (!memcmp(&request_str_ptr[i], token_str, token_sz)) )
		{
			KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - IP [%s] - Token found at [%d] - Buffer size is [%d]\n",
					conn_hnd->socket_fd, conn_hnd->string_ip, j, request_str_sz);

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

		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - IP [%s] - Remaining [%d] bytes after TOKEN, adding that\n",
				conn_hnd->socket_fd, conn_hnd->string_ip, remaining_sz);

		/* There is data past token, save it into partial buffer */
		if (remaining_sz > 0)
		{
			/* Create a new partial_read_buffer object */
			if (!conn_hnd->iodata.partial_read_buffer)
			{
				if (ev_base->flags.mt_engine)
					conn_hnd->iodata.partial_read_buffer = MemBufferNew(BRBDATA_THREAD_SAFE, (remaining_sz + 1));
				else
					conn_hnd->iodata.partial_read_buffer = MemBufferNew(BRBDATA_THREAD_UNSAFE, (remaining_sz + 1));
			}

			/* Point to remaining data */
			read_buffer_ptr						= MemBufferOffsetDeref(conn_hnd->iodata.read_buffer, (request_str_sz - remaining_sz));
			conn_hnd->iodata.read_buffer->size -= remaining_sz;

			/* Save left over into partial buffer and NULL terminate read buffer to new size */
			MemBufferAdd(conn_hnd->iodata.partial_read_buffer, read_buffer_ptr, remaining_sz);
			MemBufferPutNULLTerminator(conn_hnd->iodata.read_buffer);
		}
	}

	/* Dispatch upper layer read event if token has been found or if we reached our maximum allowed buffer size */
	if ( (token_found) || ((max_buffer_sz > 0) && (request_str_sz >= max_buffer_sz)) )
	{
		/* Dispatch SYNCED read event to listener */
		CommEvTCPServerConnDispatchEventByFD(conn_hnd->parent_srv, conn_hnd->socket_fd, orig_read_sz, thrd_id, CONN_EVENT_READ);

		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - IP [%s] - Dispatched READ_EV of [%d] bytes\n",
				conn_hnd->socket_fd, conn_hnd->string_ip, MemBufferGetSize(conn_hnd->iodata.read_buffer));
	}
	else
	{
		KQBASE_LOG_PRINTF(tcp_srv->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - IP [%s] - READ_EV not dispatched - [%d] bytes PENDING\n",
				conn_hnd->socket_fd, conn_hnd->string_ip, request_str_sz);
	}

	return 1;

}
/**************************************************************************************************************************/
static int CommEvTCPServerAcceptPostInit(CommEvTCPServer *srv_ptr, CommEvTCPServerListener *listener, int conn_fd, int accept_queue_sz, int thrd_id)
{
	CommEvTCPServerConn *conn_hnd;
	int recv_unix;

	EvKQBase *ev_base			= srv_ptr->kq_base;
	int listener_id				= listener->slot_id;
	unsigned int sockaddr_sz	= srv_ptr->cfg[listener_id].srv_type == COMM_SERVER_TYPE_INET6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);

	/* Invalid descriptor */
	if (conn_fd < 0)
		return 0;

	/* Grab a connection handler from server internal arena */
	conn_hnd	= CommEvTCPServerConnArenaGrab(srv_ptr, conn_fd);
	recv_unix	= conn_hnd->flags.conn_recvd_from_unixsrv;

	/* Clean all flags and statistics */
	memset(&conn_hnd->flags, 0, sizeof(conn_hnd->flags));
	memset(&conn_hnd->statistics, 0, sizeof(conn_hnd->statistics));
	memset(&conn_hnd->transform, 0, sizeof(conn_hnd->transform));

	/* Populate CONN_HANDLER structure associated with this FD */
	conn_hnd->socket_fd						= conn_fd;
	conn_hnd->parent_srv					= srv_ptr;
	conn_hnd->listener						= listener;
	conn_hnd->iodata.read_stream			= NULL;
	conn_hnd->iodata.read_buffer			= NULL;
	conn_hnd->iodata.partial_read_buffer	= NULL;
	conn_hnd->iodata.partial_read_stream	= NULL;
	conn_hnd->ssldata.sni_host_ptr			= NULL;
	conn_hnd->flags.conn_hnd_inuse			= 1;
	conn_hnd->flags.conn_recvd_from_unixsrv	= recv_unix;

	/* Load self sync data */
	conn_hnd->self_sync.token_str			= (char*)&srv_ptr->cfg[listener_id].self_sync.token_str_buf;
	conn_hnd->self_sync.token_str_sz		= srv_ptr->cfg[listener_id].self_sync.token_str_sz;
	conn_hnd->self_sync.max_buffer_sz		= srv_ptr->cfg[listener_id].self_sync.max_buffer_sz;
	conn_hnd->flags.self_sync				= srv_ptr->cfg[listener_id].flags.self_sync;

	/* Initialize timer IDs and JOB_IDs */
	conn_hnd->timers.calculate_datarate_id	= -1;
	conn_hnd->ssldata.shutdown_jobid		= -1;
	conn_hnd->thrd_id						= -1;

	/* Make it non-blocking */
	EvKQBaseSocketSetNonBlock(ev_base, conn_hnd->socket_fd);
	EvKQBaseSocketSetNoDelay(ev_base, conn_hnd->socket_fd);

	/* Grab local and remote address - LOCAL Will be remote if connection is locally intercepted */
	getpeername(conn_hnd->socket_fd, (struct sockaddr*)&conn_hnd->conn_addr, &sockaddr_sz);
	getsockname(conn_hnd->socket_fd, (struct sockaddr*)&conn_hnd->local_addr, &sockaddr_sz);

	/* Generate a string representation of binary IP */
	memset(&conn_hnd->string_ip, 0, sizeof(conn_hnd->string_ip ) - 1);

	if (srv_ptr->cfg[listener_id].srv_type == COMM_SERVER_TYPE_INET6)
	{
		conn_hnd->cli_port 		= ntohs(satosin6(&conn_hnd->conn_addr)->sin6_port);
		inet_ntop(AF_INET6, &satosin6(&conn_hnd->conn_addr)->sin6_addr, (char*)&conn_hnd->string_ip, sizeof(conn_hnd->string_ip) - 1);
		inet_ntop(AF_INET6, &satosin6(&conn_hnd->local_addr)->sin6_addr, (char*)&conn_hnd->server_ip, sizeof(conn_hnd->server_ip) - 1);
	}
	else
	{
		conn_hnd->cli_port 		= ntohs(satosin(&conn_hnd->conn_addr)->sin_port);
		inet_ntop(AF_INET, &satosin(&conn_hnd->conn_addr)->sin_addr, (char*)&conn_hnd->string_ip, sizeof(conn_hnd->string_ip) - 1);
		inet_ntop(AF_INET, &satosin(&conn_hnd->local_addr)->sin_addr, (char*)&conn_hnd->server_ip, sizeof(conn_hnd->server_ip) - 1);
	}

	/* Set all TIMEOUT stuff to UNINITIALIZED, intiialize WRITE_REQ_QUEUE and set DESCRIPTION */
	EvKQBaseTimeoutInitAllByFD(ev_base, conn_hnd->socket_fd);
	EvAIOReqQueueInit(srv_ptr->kq_base, &conn_hnd->iodata.write_queue, 4096, (srv_ptr->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE), AIOREQ_QUEUE_SIMPLE);

	/* Set humanized description of this socket */
	EvKQBaseFDDescriptionSetByFD(ev_base, conn_hnd->socket_fd, "SRV FD/PORT [%d/%d] - CONN - FD/IP:PORT [%d / %s:%d]",
			listener->socket_fd, listener->port, conn_hnd->socket_fd, (char*)&conn_hnd->string_ip, conn_hnd->cli_port);

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - Client connected from addr [%s] on listener ID [%d]\n", conn_hnd->socket_fd, conn_hnd->string_ip, listener_id);

	/* Add client to active client LIST */
	DLinkedListAdd(&srv_ptr->conn.list, &conn_hnd->conn_node, conn_hnd);

	/* Set default READ and CLOSE events to this conn_fd, if any is defined in server */
	CommEvTCPServerConnSetDefaultEvents(srv_ptr, conn_hnd);

	/* Decide what to do based on listener protocol */
	switch(srv_ptr->cfg[listener_id].srv_proto)
	{
	/* Running SSL, jump to SNI peek */
	case COMM_SERVERPROTO_SSL:
	{
		/* Mark SSL as enabled */
		conn_hnd->flags.ssl_enabled				= 1;
		conn_hnd->ssldata.sni_parse_trycount	= 0;

		/* Clean certificate and set forge request to -1 */
		conn_hnd->ssldata.x509_cert				= NULL;
		conn_hnd->ssldata.x509_forge_reqid		= -1;

		/* Default SSL_HANDSHAKE_FAIL event */
		COMM_SERVER_CONN_SET_DEFAULT_HANDSHAKEFAIL(conn_hnd);

		/* Try to parse TLS SNI information from this connection prior to anything and a close event to detect EOF */
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventSNIPeek, conn_hnd);
		break;
	}
	/* Try to auto detect what protocol the client is speaking */
	case COMM_SERVERPROTO_AUTODETECT:
	{
		/* Mark SSL as enabled */
		conn_hnd->flags.ssl_enabled				= 0;
		conn_hnd->ssldata.sni_parse_trycount	= 0;

		/* Clean certificate and set forge request to -1 */
		conn_hnd->ssldata.x509_cert				= NULL;
		conn_hnd->ssldata.x509_forge_reqid		= -1;

		/* Default SSL_HANDSHAKE_FAIL event */
		COMM_SERVER_CONN_SET_DEFAULT_HANDSHAKEFAIL(conn_hnd);

		/* Schedule a read event to try to identify the protocol and a close event to detect EOF */
		EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventProtocolIdentify, conn_hnd);

		/* Schedule AUTODETECT timeout event to proceed as PLAIN for SILENT clients */
		if (srv_ptr->cfg[listener_id].timeout.autodetect_ms > 0)
			EvKQBaseTimeoutSet(ev_base, conn_hnd->socket_fd, KQ_CB_TIMEOUT_READ, srv_ptr->cfg[listener_id].timeout.autodetect_ms, CommEvTCPServerEventProtocolIdentifyTimeout, conn_hnd);

		break;
	}

	case COMM_SERVERPROTO_PLAIN:
	default:
	{
		/* Mark SSL as disabled */
		conn_hnd->flags.ssl_enabled				= 0;

		/* Dispatch event - This upper layer event can make conn_hnd->socket_fd get disconnected, and destroy IO buffers beneath our feet */
		CommEvTCPServerDispatchEvent(srv_ptr, conn_hnd, accept_queue_sz, thrd_id, COMM_SERVER_EVENT_ACCEPT_AFTER);

		/* Fire up data rate calculation timer if flag is set */
		COMM_EV_STATS_CONN_HND_FIRE_TIMER(conn_hnd);

		/* Set read internal event for newly connected socket if we have an upper layer event defined */
		if (conn_hnd->events[CONN_EVENT_READ].cb_handler_ptr)
			EvKQBaseSetEvent(ev_base, conn_hnd->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventRead, conn_hnd);

		break;
	}

	}

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTCPServerConnRatesCalculateTimer(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServerConn *conn_hnd = cb_data;

	/* Calculate read rates */
	if (conn_hnd->flags.calculate_datarate)
	{
		CommEvStatisticsRateCalculate(conn_hnd->parent_srv->kq_base, &conn_hnd->statistics, conn_hnd->socket_fd, COMM_RATES_READ);
		CommEvStatisticsRateCalculate(conn_hnd->parent_srv->kq_base, &conn_hnd->statistics, conn_hnd->socket_fd, COMM_RATES_WRITE);
		CommEvStatisticsRateCalculate(conn_hnd->parent_srv->kq_base, &conn_hnd->statistics, conn_hnd->socket_fd, COMM_RATES_USER);

		/* Reschedule DATARATE CALCULATE TIMER timer */
		conn_hnd->timers.calculate_datarate_id =
				EvKQBaseTimerAdd(conn_hnd->parent_srv->kq_base, COMM_ACTION_ADD_VOLATILE, 1000, CommEvTCPServerConnRatesCalculateTimer, conn_hnd);
	}
	else
		conn_hnd->timers.calculate_datarate_id = -1;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTCPServerUNIXEventAccept(int fd, int accept_queue_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXServer *unix_srv_ptr			= base_ptr;
	CommEvTCPServerListener *tcp_listener	= cb_data;
	CommEvTCPServer *tcp_server				= tcp_listener->parent_srv;
	EvKQBase *ev_base						= unix_srv_ptr->kq_base;
	CommEvUNIXServerConn *unix_conn_hnd		= MemArenaGrabByID(unix_srv_ptr->conn.arena, fd);
	CommEvUNIXIOData *unix_io_data			= &unix_conn_hnd->iodata;
	CommEvUNIXServerListener *unix_listener	= unix_conn_hnd->listener;
	int unix_listener_id					= unix_listener->slot_id;

	KQBASE_LOG_PRINTF(tcp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - UNIX LID [%d]- Client connected\n", unix_conn_hnd->socket_fd, unix_listener_id);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerUNIXEventRead(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXServer *unix_srv_ptr				= base_ptr;
	CommEvTCPServerListener *tcp_listener		= cb_data;
	CommEvTCPServer *tcp_server					= tcp_listener->parent_srv;
	EvKQBase *ev_base							= unix_srv_ptr->kq_base;
	CommEvUNIXServerConn *unix_conn_hnd			= MemArenaGrabByID(unix_srv_ptr->conn.arena, fd);
	CommEvUNIXServerListener *unix_listener		= unix_conn_hnd->listener;
	CommEvUNIXIOData *io_data					= &unix_conn_hnd->iodata;
	int unix_listener_id						= unix_listener->slot_id;
	char *raw_data								= MemBufferDeref(io_data->read.data_mb);
	int raw_data_sz								= MemBufferGetSize(io_data->read.data_mb);

	KQBASE_LOG_PRINTF(tcp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - UNIX LID [%d]- Read event of [%d] bytes\n",
			unix_conn_hnd->socket_fd, unix_listener_id, to_read_sz);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerUNIXEventBrbProtoRead(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTCPServerConn *tcp_conn_hnd;
	CommEvTCPServerCBH *cb_handler;
	EvBaseKQFileDesc *kq_fd;
	void *cb_handler_data;

	CommEvUNIXServer *unix_srv_ptr						= base_ptr;
	CommEvTCPServerListener *tcp_listener				= cb_data;
	CommEvTCPServer *tcp_server							= tcp_listener->parent_srv;
	EvKQBase *ev_base									= unix_srv_ptr->kq_base;
	CommEvUNIXServerConn *unix_conn_hnd					= MemArenaGrabByID(unix_srv_ptr->conn.arena, fd);
	CommEvUNIXServerListener *unix_listener				= unix_conn_hnd->listener;
	CommEvUNIXIOData *io_data							= &unix_conn_hnd->iodata;
	CommEvTCPServerConnTransferData *transfer_data		= MemBufferDeref(io_data->read.data_mb);
	void *payload_ptr									= ((char*)transfer_data + sizeof(CommEvTCPServerConnTransferData));
	int unix_listener_id								= unix_listener->slot_id;
	int tcp_listener_id									= tcp_listener->slot_id;
	int transfer_data_sz								= MemBufferGetSize(io_data->read.data_mb);
	int payload_sz										= ((transfer_data_sz - sizeof(CommEvTCPServerConnTransferData)));
	int recv_fd_count									= io_data->read.fd_arr.sz;
	int recv_fd											= io_data->read.fd_arr.data[0];

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - UNIX LID [%d] - Client read of [%d] bytes - TOTAL [%d] - CONTROL [%d] - PAYLOAD [%d]\n",
			unix_conn_hnd->socket_fd, unix_listener_id, to_read_sz, transfer_data_sz, sizeof(CommEvTCPServerConnTransferData), payload_sz);

	/* This should not happen */
	BRB_ASSERT_FMT(ev_base, (transfer_data_sz >= sizeof(CommEvTCPServerConnTransferData)), "FD [%d] - UNIX LID [%d] - Too small transfer data [%d] of [%d]\n",
			unix_conn_hnd->socket_fd, unix_listener_id, transfer_data_sz, sizeof(CommEvTCPServerConnTransferData));
	BRB_ASSERT_FMT(ev_base, (transfer_data->read_buffer_bytes == payload_sz), "FD [%d] - UNIX LID [%d] - Payload size mismatch - [%d]-[%d]\n",
			unix_conn_hnd->socket_fd, unix_listener_id, transfer_data->read_buffer_bytes, payload_sz);

	/* Check control FD_ARR */
	if (recv_fd_count != 1)
	{
		KQBASE_LOG_PRINTF(tcp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - UNIX LID [%d] - Unexpected FD count [%d] - Ignoring..\n",
				unix_conn_hnd->socket_fd, unix_listener_id, io_data->read.fd_arr.sz);

		return 0;
	}

	/* Check received FD */
	if (recv_fd <= 0)
	{
		KQBASE_LOG_PRINTF(tcp_server->log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] - UNIX LID [%d] - Invalid FD received [%d] - Ignoring..\n",
				unix_conn_hnd->socket_fd, unix_listener_id, recv_fd);

		/* Dispatch ACCEPT_FAIL event, still with a NULL CONN_HND */
		cb_handler		= tcp_server->events[tcp_listener_id][COMM_SERVER_EVENT_ACCEPT_FAIL].cb_handler_ptr;
		cb_handler_data = tcp_server->events[tcp_listener_id][COMM_SERVER_EVENT_ACCEPT_FAIL].cb_data_ptr;

		/* There is a handler for this event. Invoke the damn thing */
		if (cb_handler)
			cb_handler(tcp_listener->socket_fd, 0, thrd_id, cb_handler_data, tcp_server);

		return 0;
	}

	/* Grab TCP_CONN_HND of this received FD from arena, set flags as RECEIVED by UNIX_SERVER */
	tcp_conn_hnd			= CommEvTCPServerConnArenaGrab(tcp_server, recv_fd);
	kq_fd					= EvKQBaseFDGrabFromArena(ev_base, recv_fd);
	tcp_conn_hnd->flags.conn_recvd_from_unixsrv = 1;

	/* Invoke common POST_ACCEPT initialization procedure and set a CLOSE event handler */
	CommEvTCPServerAcceptPostInit(tcp_server, tcp_listener, recv_fd, 0, thrd_id);
	EvKQBaseSetEvent(ev_base, tcp_conn_hnd->socket_fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvTCPServerEventClose, tcp_conn_hnd);

	/* If there is PAYLOAD, add it inside read buffer */
	if (payload_sz > 0)
	{
		/* Create a new read buffer to hold PAYLOAD data */
		if (!tcp_conn_hnd->iodata.read_buffer)
			tcp_conn_hnd->iodata.read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (payload_sz + 1));

		/* Add data to READ_BUF and dispatch READ_EV directly for this lower event */
		MemBufferAdd(tcp_conn_hnd->iodata.read_buffer, payload_ptr, payload_sz);
		CommEvTCPServerConnDispatchEventByFD(tcp_server, tcp_conn_hnd->socket_fd, payload_sz, thrd_id, CONN_EVENT_READ);
	}

	/* If there is any pending read data being PEEKED, dispatch a LOW_LEVEL read event */
	if ((transfer_data->flags.peek_on_read) && (transfer_data->read_buffer_bytes > 0))
		EvKQBaseDispatchEventRead(ev_base, kq_fd, transfer_data->read_buffer_bytes);
	else if ((transfer_data->flags.defering_read) && (transfer_data->read_pending_bytes > 0))
		EvKQBaseDispatchEventRead(ev_base, kq_fd, transfer_data->read_pending_bytes);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerUNIXEventClose(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXServer *unix_srv_ptr			= base_ptr;
	CommEvTCPServerListener *tcp_listener	= cb_data;
	CommEvTCPServer *tcp_server				= tcp_listener->parent_srv;
	EvKQBase *ev_base						= unix_srv_ptr->kq_base;
	CommEvUNIXServerConn *unix_conn_hnd		= MemArenaGrabByID(unix_srv_ptr->conn.arena, fd);
	CommEvUNIXIOData *unix_io_data			= &unix_conn_hnd->iodata;
	CommEvUNIXServerListener *unix_listener	= unix_conn_hnd->listener;
	int unix_listener_id					= unix_listener->slot_id;

	KQBASE_LOG_PRINTF(tcp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - UNIX LID [%d] - Client disconnected\n", unix_conn_hnd->socket_fd, unix_listener_id);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTCPServerObjectDestroyCBH(void *kq_obj_ptr, void *cb_data)
{
	EvBaseKQObject *kq_obj		= kq_obj_ptr;
	CommEvTCPServer *srv_ptr	= kq_obj->obj.ptr;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "Invoked to destroy COMM_EV_TCP_SERVER at [%p]\n", kq_obj->obj.ptr);

	/* Destroy and clean structure */
	CommEvTCPServerDestroy(srv_ptr);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerDoDestroy(CommEvTCPServer *srv_ptr)
{
	CommEvTCPServerListener *listener;
	CommEvTCPServerConn *conn_hnd;
	DLinkedListNode *node;

	KQBASE_LOG_PRINTF(srv_ptr->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "SRV_PTR [%p] - Will destroy [%u] active clients\n", srv_ptr, srv_ptr->conn.list.size);

	/* Unregister object and shutdown all server active clients */
	EvKQBaseObjectUnregister(&srv_ptr->kq_obj);
	CommEvTCPServerListenerConnShutdownAll(srv_ptr, -1);

	/* Shutdown all server listeners */
	for (node = srv_ptr->listener.list.head; node; node = node->next)
	{
		listener = node->data;

		/* XXX TODO: - Shutdown UNIX_LID of this listener, if any */
		if (listener->unix_lid > -1)
		{

		}

		/* Clean up SSL stuff */
		CommEvTCPServerDoDestroy_SSLCleanup(srv_ptr, listener);

		/* Clear description and flags */
		EvKQBaseFDDescriptionClearByFD(srv_ptr->kq_base, listener->socket_fd);
		memset(&listener->flags, 0, sizeof(listener->flags));

		/* Close the socket and cancel any pending events */
		EvKQBaseSocketClose(srv_ptr->kq_base, listener->socket_fd);
		listener->socket_fd = -1;

		continue;
	}

	/* Free master RSA key, if any */
	if (srv_ptr->ssldata.main_key)
	{
		EVP_PKEY_free(srv_ptr->ssldata.main_key);
		srv_ptr->ssldata.main_key = NULL;
	}

	/* Destroy UNIX_SERVER, if any exists */
	CommEvUNIXServerDestroy(srv_ptr->unix_server);

	/* Destroy X.509 certificate cache table */
	AssocArrayDestroy(srv_ptr->ssldata.cert_cache.table);
	srv_ptr->ssldata.cert_cache.table = NULL;

	/* Destroy LISTENER SLOTs */
	SlotQueueDestroy(&srv_ptr->listener.slot);

	/* Destroy arena and MUTEX */
	//COMM_SERVER_CONN_TABLE_MUTEX_DESTROY(srv_ptr);
	CommEvTCPServerConnArenaDestroy(srv_ptr);

	/* Free WILLY */
	free(srv_ptr);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTCPServerDoDestroy_SSLCleanup(CommEvTCPServer *srv_ptr, CommEvTCPServerListener *listener)
{
	/* Destroy private key */
	if (srv_ptr->cfg[listener->slot_id].ssl.ca_cert.key_private)
	{
		EVP_PKEY_free(srv_ptr->cfg[listener->slot_id].ssl.ca_cert.key_private);
		srv_ptr->cfg[listener->slot_id].ssl.ca_cert.key_private = NULL;
	}

	/* Destroy any pending X509 certificate */
	if (srv_ptr->cfg[listener->slot_id].ssl.ca_cert.x509_cert)
	{
		X509_free(srv_ptr->cfg[listener->slot_id].ssl.ca_cert.x509_cert);
		srv_ptr->cfg[listener->slot_id].ssl.ca_cert.x509_cert = NULL;
	}

	if (listener->ssldata.ssl_context)
	{
		SSL_CTX_free(listener->ssldata.ssl_context);
		listener->ssldata.ssl_context = NULL;
	}

	return 1;
}
/**************************************************************************************************************************/
