/*
 * libbrb_ev_comm.h
 *
 *  Created on: 2012-10-28
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

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <fcntl.h>
#include <assert.h>


/* SERIAL SUPPORT */
#include <termios.h>

/* BrByte DATA framework */
#include <libbrb_data.h>

/* Local INCLUDES */
#include <libbrb_comm_proto.h>
#include <libbrb_comm_unix.h>
#include <libbrb_comm_utils.h>

#ifndef LIBBRB_COMM_H_
#define LIBBRB_COMM_H_

#define COMM_ETH_ALEN									6
#define COMM_IPV4_ALEN									4

#define COMM_TCP_SERVER_SELFSYNC_MAX_TOKEN_SZ 			16
#define COMM_TCP_SSL_READ_BUFFER_SZ						65535
#define COMM_TCP_ACCEPT_QUEUE							4096
#define COMM_TCP_SERVER_MAX_LISTERNERS					32
#define CONN_MAXSTRING_WRITESZ							65535
#define COMM_CLIENT_MAXSTRING_WRITESZ					65535

#define COMM_UNIX_CLIENT_RECONNECT_TIMEOUT_DEFAULT_MS	1000
#define COMM_UNIX_CLIENT_RECONNECT_CLOSE_DEFAULT_MS		1000
#define COMM_UNIX_CLIENT_RECONNECT_FAIL_DEFAULT_MS		1000
#define COMM_UNIX_CLIENT_CALCULATE_DATARATE_DEFAULT_MS	1000

#define COMM_TCP_CLIENT_RECONNECT_TIMEOUT_DEFAULT_MS	1000
#define COMM_TCP_CLIENT_RECONNECT_CLOSE_DEFAULT_MS		1000
#define COMM_TCP_CLIENT_RECONNECT_FAIL_DEFAULT_MS		1000
#define COMM_TCP_CLIENT_CALCULATE_DATARATE_DEFAULT_MS	1000

#define COMM_SSH_CLIENT_RECONNECT_TIMEOUT_DEFAULT_MS	1000
#define COMM_SSH_CLIENT_RECONNECT_CLOSE_DEFAULT_MS		1000
#define COMM_SSH_CLIENT_RECONNECT_FAIL_DEFAULT_MS		1000
#define COMM_SSH_CLIENT_CALCULATE_DATARATE_DEFAULT_MS	1000

#define COMM_SERIAL_PARITY_CMS							010000000000		/* Mark or space (stick) parity */


/* Some PEM string identifier */
#define PEM_STRING_X509_OLD				"X509 CERTIFICATE"
#define PEM_STRING_X509					"CERTIFICATE"
#define PEM_STRING_X509_PAIR			"CERTIFICATE PAIR"
#define PEM_STRING_X509_TRUSTED			"TRUSTED CERTIFICATE"
#define PEM_STRING_X509_REQ_OLD			"NEW CERTIFICATE REQUEST"
#define PEM_STRING_X509_REQ				"CERTIFICATE REQUEST"
#define PEM_STRING_X509_CRL				"X509 CRL"
#define PEM_STRING_EVP_PKEY				"ANY PRIVATE KEY"
#define PEM_STRING_PUBLIC				"PUBLIC KEY"
#define PEM_STRING_RSA					"RSA PRIVATE KEY"
#define PEM_STRING_RSA_PUBLIC			"RSA PUBLIC KEY"
#define PEM_STRING_DSA					"DSA PRIVATE KEY"
#define PEM_STRING_DSA_PUBLIC			"DSA PUBLIC KEY"
#define PEM_STRING_PKCS7				"PKCS7"
#define PEM_STRING_PKCS7_SIGNED			"PKCS #7 SIGNED DATA"
#define PEM_STRING_PKCS8				"ENCRYPTED PRIVATE KEY"
#define PEM_STRING_PKCS8INF				"PRIVATE KEY"
#define PEM_STRING_DHPARAMS				"DH PARAMETERS"
#define PEM_STRING_DHXPARAMS			"X9.42 DH PARAMETERS"
#define PEM_STRING_SSL_SESSION			"SSL SESSION PARAMETERS"
#define PEM_STRING_DSAPARAMS			"DSA PARAMETERS"
#define PEM_STRING_ECDSA_PUBLIC			"ECDSA PUBLIC KEY"
#define PEM_STRING_ECPARAMETERS			"EC PARAMETERS"
#define PEM_STRING_ECPRIVATEKEY			"EC PRIVATE KEY"
#define PEM_STRING_PARAMETERS			"PARAMETERS"
#define PEM_STRING_CMS					"CMS"

#define COMM_EV_STATS_READ_PACKET_RX(statistics) ((statistics.ev_base && (statistics.ev_base->stats.cur_invoke_ts_sec > (statistics.last_read_ts + 1))) ? 0 : statistics.rate.packet_rx)
#define COMM_EV_STATS_READ_BYTE_RX(statistics) ((statistics.ev_base && (statistics.ev_base->stats.cur_invoke_ts_sec > (statistics.last_read_ts + 1))) ? 0 : statistics.rate.byte_rx)
#define COMM_EV_STATS_READ_SSL_BYTE_RX(statistics) ((statistics.ev_base && (statistics.ev_base->stats.cur_invoke_ts_sec > (statistics.last_read_ts + 1))) ? 0 : statistics.rate.ssl_byte_rx)

#define COMM_EV_STATS_READ_PACKET_TX(statistics) ((statistics.ev_base && (statistics.ev_base->stats.cur_invoke_ts_sec > (statistics.last_write_ts + 1))) ? 0 : statistics.rate.packet_tx)
#define COMM_EV_STATS_READ_BYTE_TX(statistics) ((statistics.ev_base && (statistics.ev_base->stats.cur_invoke_ts_sec > (statistics.last_write_ts + 1))) ? 0 : statistics.rate.byte_tx)
#define COMM_EV_STATS_READ_SSL_BYTE_TX(statistics) ((statistics.ev_base && (statistics.ev_base->stats.cur_invoke_ts_sec > (statistics.last_write_ts + 1))) ? 0 : statistics.rate.ssl_byte_tx)

#define COMM_EV_STATS_READ_USER00(statistics) ((statistics.ev_base && (statistics.ev_base.stats.cur_invoke_ts_sec > (statistics.last_user_ts + 1))) ? 0 : statistics.rate.user00)
#define COMM_EV_STATS_READ_USER01(statistics) ((statistics.ev_base && (statistics.ev_base.stats.cur_invoke_ts_sec > (statistics.last_user_ts + 1))) ? 0 : statistics.rate.user01)
#define COMM_EV_STATS_READ_USER02(statistics) ((statistics.ev_base && (statistics.ev_base.stats.cur_invoke_ts_sec > (statistics.last_user_ts + 1))) ? 0 : statistics.rate.user02)
#define COMM_EV_STATS_READ_USER03(statistics) ((statistics.ev_base && (statistics.ev_base.stats.cur_invoke_ts_sec > (statistics.last_user_ts + 1))) ? 0 : statistics.rate.user03)


#define COMM_EV_STATS_PTR_READ_PACKET_RX(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_read_ts + 1))) ? 0 : statistics->rate.packet_rx)
#define COMM_EV_STATS_PTR_READ_BYTE_RX(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_read_ts + 1))) ? 0 : statistics->rate.byte_rx)
#define COMM_EV_STATS_PTR_READ_SSL_BYTE_RX(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_read_ts + 1))) ? 0 : statistics->rate.ssl_byte_rx)

#define COMM_EV_STATS_PTR_READ_PACKET_TX(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_write_ts + 1))) ? 0 : statistics->rate.packet_tx)
#define COMM_EV_STATS_PTR_READ_BYTE_TX(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_write_ts + 1))) ? 0 : statistics->rate.byte_tx)
#define COMM_EV_STATS_PTR_READ_SSL_BYTE_TX(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_write_ts + 1))) ? 0 : statistics->rate.ssl_byte_tx)

#define COMM_EV_STATS_PTR_READ_USER00(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_user_ts + 1))) ? 0 : statistics->rate.user00)
#define COMM_EV_STATS_PTR_READ_USER01(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_user_ts + 1))) ? 0 : statistics->rate.user01)
#define COMM_EV_STATS_PTR_READ_USER02(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_user_ts + 1))) ? 0 : statistics->rate.user02)
#define COMM_EV_STATS_PTR_READ_USER03(statistics) ((statistics->ev_base && (statistics->ev_base->stats.cur_invoke_ts_sec > (statistics->last_user_ts + 1))) ? 0 : statistics->rate.user03)

#define COMM_EV_STATS_CONN_HND_FIRE_TIMER(conn_hnd)	if ((conn_hnd->flags.calculate_datarate) && (conn_hnd->timers.calculate_datarate_id < 0))	\
		conn_hnd->timers.calculate_datarate_id	= EvKQBaseTimerAdd(conn_hnd->parent_srv->kq_base, COMM_ACTION_ADD_VOLATILE, 1000, \
				CommEvTCPServerConnRatesCalculateTimer, conn_hnd); \
				else conn_hnd->timers.calculate_datarate_id	= -1;

#define COMM_EV_TCP_CLIENT_FINISH(client) if (client->flags.destroy_after_close) CommEvTCPClientDestroy(client); else CommEvTCPClientDisconnect(client);
#define COMM_EV_TCP_CLIENT_INTERNAL_FINISH(client) if (client->flags.destroy_after_close) CommEvTCPClientDestroy(client); else CommEvTCPClientInternalDisconnect(client);
#define COMM_SERIAL_FINISH(serial) if (serial->flags.destroy_after_close) CommEvSerialPortDestroy(serial); else CommEvSerialPortClose(serial);
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
typedef enum
{
	COMM_EV_IOFUNC_READ,
	COMM_EV_IOFUNC_WRITE,
	COMM_EV_IOFUNC_LASTITEM
} CommEvIOFuncCodes;
/*******************************************************/
typedef enum
{
	COMM_CRYPTO_FUNC_NONE,
	COMM_CRYPTO_FUNC_RC4,
	COMM_CRYPTO_FUNC_RC4_MD5,
	COMM_CRYPTO_FUNC_BLOWFISH,
	COMM_CRYPTO_FUNC_LASTITEM
} CommCryptoFuncCodes;
/*******************************************************/
typedef enum
{
	COMM_RATES_READ,
	COMM_RATES_WRITE,
	COMM_RATES_USER,
	COMM_RATES_LASTITEM
} CommEvRateCodes;
/*******************************************************/
typedef int CommEvIOFunc(int, char *, int);
/*******************************************************/
typedef struct _CommEvSSLUtilsCertReq
{
	struct
	{
		char *country_str;
		char *organization_str;
		char *common_name_str;
		char *dns_name_str;
		unsigned int serial;
		int valid_days;
		int self_sign;
		int key_size;
		int exponent;
	} options;

	struct
	{
		EVP_PKEY *key_private;
		EVP_PKEY *key_public;
		X509 *x509_cert;
	} result;

} CommEvSSLUtilsCertReq;
/*******************************************************/
typedef struct _CommEvCryptoInfo
{
	int algo_code;

	struct
	{
		char *ptr;
		int sz;
	} key;

	struct
	{
		struct
		{
			BRB_RC4_State rc4;
			BRB_BLOWFISH_CTX blowfish;
		} read;

		struct
		{
			BRB_RC4_State rc4;
			BRB_BLOWFISH_CTX blowfish;
		} write;
	} state;

	struct
	{
		unsigned int enabled:1;
	} flags;

} CommEvCryptoInfo;
/*******************************************************/
typedef struct _CommEvContentTransformerInfo
{
	CommEvCryptoInfo crypto;

} CommEvContentTransformerInfo;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
/* TCP_SERVER_CONN_TABLE TABLE STUFF - BEGIN
/******************************************************************************************************/
#define COMM_SERVER_CONN_AIO_MUTEX_INIT(conn_hnd)			if ((conn_hnd->parent_srv->kq_base->flags.mt_engine) && (!conn_hnd->flags.iodata_mutex_init)) \
		{ conn_hnd->flags.iodata_mutex_init = 1; MUTEX_INIT (conn_hnd->iodata.mutex, "TCP_SERVER_CONN_IO_MUTEX") }
#define COMM_SERVER_CONN_AIO_MUTEX_DESTROY(conn_hnd)		if ((conn_hnd->parent_srv->kq_base->flags.mt_engine) && (conn_hnd->flags.iodata_mutex_init)) { \
		conn_hnd->flags.iodata_mutex_init = 0; MUTEX_DESTROY (conn_hnd->iodata.mutex, "TCP_SERVER_CONN_IO_MUTEX") }
#define COMM_SERVER_CONN_AIO_MUTEX_LOCK(conn_hnd)			if ((conn_hnd->parent_srv->kq_base->flags.mt_engine) && (conn_hnd->flags.iodata_mutex_init)) { \
		MUTEX_LOCK (conn_hnd->iodata.mutex, "TCP_SERVER_CONN_IO_MUTEX") }
#define COMM_SERVER_CONN_AIO_MUTEX_TRYLOCK(conn_hnd, state) if ((conn_hnd->parent_srv->kq_base->flags.mt_engine) && (conn_hnd->flags.iodata_mutex_init)) { \
		MUTEX_TRYLOCK (conn_hnd->iodata.mutex, "TCP_SERVER_CONN_IO_MUTEX", state) }
#define COMM_SERVER_CONN_AIO_MUTEX_UNLOCK(conn_hnd) 		if ((conn_hnd->parent_srv->kq_base->flags.mt_engine) && (conn_hnd->flags.iodata_mutex_init)) { \
		MUTEX_UNLOCK (conn_hnd->iodata.mutex, "TCP_SERVER_CONN_IO_MUTEX") }
/*******************************************************/
#define COMM_SERVER_CONN_TABLE_MUTEX_INIT(srv_ptr)				if (srv_ptr->kq_base->flags.mt_engine) MUTEX_INIT (srv_ptr->conn_table.mutex, "TCP_SERVER_CONN_TABLE_MUTEX")
#define COMM_SERVER_CONN_TABLE_MUTEX_DESTROY(srv_ptr)			if (srv_ptr->kq_base->flags.mt_engine) MUTEX_DESTROY (srv_ptr->conn_table.mutex, "TCP_SERVER_CONN_TABLE_MUTEX")
#define COMM_SERVER_CONN_TABLE_MUTEX_LOCK(srv_ptr)				if (srv_ptr->kq_base->flags.mt_engine) MUTEX_LOCK (srv_ptr->conn_table.mutex, "TCP_SERVER_CONN_TABLE_MUTEX")
#define COMM_SERVER_CONN_TABLE_MUTEX_TRYLOCK(srv_ptr, state)	if (srv_ptr->kq_base->flags.mt_engine) MUTEX_TRYLOCK (srv_ptr->conn_table.mutex, "TCP_SERVER_CONN_TABLE_MUTEX", state)
#define COMM_SERVER_CONN_TABLE_MUTEX_UNLOCK(srv_ptr) 			if (srv_ptr->kq_base->flags.mt_engine) MUTEX_UNLOCK (srv_ptr->conn_table.mutex, "TCP_SERVER_CONN_TABLE_MUTEX")

#define COMM_SERVER_CONN_CAN_WRITE(conn_hnd)					(conn_hnd->flags.conn_hnd_in_transfer || ((conn_hnd->flags.close_request) && (!conn_hnd->flags.conn_unlimited_enqueue)) ? 0 : 1)

#define COMM_SERVER_CONN_SET_DEFAULT_HANDSHAKEFAIL(conn_hnd)	\
		conn_hnd->events[CONN_EVENT_SSL_HANDSHAKE_FAIL].cb_handler_ptr	= conn_hnd->parent_srv->events[listener_id][COMM_SERVER_EVENT_ACCEPT_SSL_HANDSHAKE_FAIL].cb_handler_ptr; \
		conn_hnd->events[CONN_EVENT_SSL_HANDSHAKE_FAIL].cb_data_ptr		= conn_hnd->parent_srv->events[listener_id][COMM_SERVER_EVENT_ACCEPT_SSL_HANDSHAKE_FAIL].cb_data_ptr;

#define COMM_SERVER_CONN_CAN_ENQUEUE(conn_hnd)					(conn_hnd->flags.conn_unlimited_enqueue || (conn_hnd->parent_srv->cfg[conn_hnd->listener->slot_id].cli_queue_max <= 0) ? 1 : \
		((conn_hnd->iodata.write_queue.stats.queue_sz < conn_hnd->parent_srv->cfg[conn_hnd->listener->slot_id].cli_queue_max) ? 1 : 0))

/*******************************************************/
typedef void CommEvTCPServerCBH(int, int, int, void*, void*);
/*******************************************************/
typedef enum
{
	COMM_SERVER_BINDLOOPBACK,
	COMM_SERVER_BINDANY,
	COMM_SERVER_BIND_LAST_ITEM

} CommEvTCPServerBindMethod;
/*******************************************************/
typedef enum
{
	COMM_SERVER_TYPE_INET,
	COMM_SERVER_TYPE_INET6,
	COMM_SERVER_TYPE_UNIX,
	COMM_SERVER_TYPE_LAST_ITEM

} CommEvTCPServerType;
/*******************************************************/
typedef enum
{
	COMM_SERVERPROTO_PLAIN,
	COMM_SERVERPROTO_SSL,
	COMM_SERVERPROTO_AUTODETECT,
	COMM_SERVERPROTO_LASTITEM,
} CommEvTCPServerProtocol;
/*******************************************************/
typedef enum
{
	COMM_SERVER_READ_MEMBUFFER,
	COMM_SERVER_READ_MEMSTREAM,
} CommEvTCPServerReadMethod;
/*******************************************************/
typedef enum
{
	COMM_SERVER_FAILURE_NOTUSED00,
	COMM_SERVER_FAILURE_SOCKET,
	COMM_SERVER_FAILURE_REUSEADDR,
	COMM_SERVER_FAILURE_REUSEPORT,
	COMM_SERVER_FAILURE_BIND,
	COMM_SERVER_FAILURE_LISTEN,
	COMM_SERVER_FAILURE_SETNONBLOCKING,
	COMM_SERVER_FAILURE_SSL_CONTEXT,
	COMM_SERVER_FAILURE_SSL_CERT,
	COMM_SERVER_FAILURE_SSL_KEY,
	COMM_SERVER_FAILURE_NO_MORE_SLOTS,
	COMM_SERVER_FAILURE_UNKNOWN,
	COMM_SERVER_FAILURE_LASTITEM,
	COMM_SERVER_INIT_OK
} CommEvTCPServerInitCodes;
/*******************************************************/
typedef enum
{
	COMM_CLIENT_INIT_OK,
//	COMM_CLIENT_FAILURE_NOTUSED00,
	COMM_CLIENT_FAILURE_SOCKET,
	COMM_CLIENT_FAILURE_REUSEADDR,
//	COMM_CLIENT_FAILURE_REUSEPORT,
//	COMM_CLIENT_FAILURE_BIND,
//	COMM_CLIENT_FAILURE_LISTEN,
	COMM_CLIENT_FAILURE_SETNONBLOCKING,
	COMM_CLIENT_FAILURE_SSL_CONTEXT,
	COMM_CLIENT_FAILURE_SSL_CIPHER,
	COMM_CLIENT_FAILURE_SSL_CERT,
	COMM_CLIENT_FAILURE_SSL_KEY,
	COMM_CLIENT_FAILURE_SSL_MATCH,
	COMM_CLIENT_FAILURE_SSL_CA,
	COMM_CLIENT_FAILURE_SSL_HANDLE,
	COMM_CLIENT_FAILURE_SSL_PEER,

//	COMM_CLIENT_FAILURE_NO_MORE_SLOTS,
	COMM_CLIENT_FAILURE_UNKNOWN,
	COMM_CLIENT_FAILURE_LASTITEM,
} CommEvTCPClientInitCodes;
/*******************************************************/
typedef enum
{
	COMM_SERVER_EVENT_ACCEPT_BEFORE,
	COMM_SERVER_EVENT_ACCEPT_SNIPARSE,
	COMM_SERVER_EVENT_ACCEPT_AFTER,
	COMM_SERVER_EVENT_ACCEPT_FAIL,
	COMM_SERVER_EVENT_ACCEPT_SSL_HANDSHAKE_FAIL,
	COMM_SERVER_EVENT_DEFAULT_READ,
	COMM_SERVER_EVENT_DEFAULT_CLOSE,
	COMM_SERVER_EVENT_LASTITEM
} CommEvTCPServerEventCodes;
/*******************************************************/
typedef enum
{
	CONN_EVENT_READ,
	CONN_EVENT_CLOSE,
	CONN_EVENT_WRITE_FINISH,
	CONN_EVENT_SSL_HANDSHAKE_FAIL,
	CONN_EVENT_LASTITEM
} CommEvTCPServerConnEventCodes;
/*******************************************************/
typedef struct _CommEvTCPServerCertificate
{
	DLinkedListNode node;
	char dnsname_str[COMM_SSL_SNI_MAXSZ];
	unsigned long references;
	pthread_mutex_t mutex;

	EVP_PKEY *key_private;
	X509 *x509_cert;
	RSA *key_pair;
	STACK_OF(X509) *cert_chain;
} CommEvTCPServerCertificate;
/************************************************************/
typedef struct _CommEvTCPServerConnTransferData
{
	int write_pending_bytes;
	int read_pending_bytes;
	int read_buffer_bytes;
	int write_buffer_bytes;

	struct
	{
		unsigned int peek_on_read:1;
		unsigned int defering_read:1;
		unsigned int defering_write:1;
		unsigned int ssl_enabled:1;
		unsigned int conn_read_mb:1;
	} flags;

} CommEvTCPServerConnTransferData;
/************************************************************/
typedef struct _CommEvTCPServerConf
{
	CommEvTCPServerBindMethod bind_method;
	CommEvTCPServerReadMethod read_mthd;
	CommEvTCPServerProtocol srv_proto;
	CommEvTCPServerType srv_type;

	struct sockaddr_storage bind_addr;
	int port;

	struct
	{
		int autodetect_ms;
		int transfer_ms;
		int inactive_ms;
	} timeout;

	struct
	{
		char *token_str;
		int max_buffer_sz;
	} self_sync;

	struct
	{
		long cli_queue_max;
	} limits;

	struct
	{
		SSL_CTX *ssl_context;
		char *ca_key_path;
		char *ca_cert_path;
	} ssl;

	struct
	{
		char *path_str;
		unsigned int reuse_addr:1;
		unsigned int reuse_port:1;
		unsigned int no_brb_proto:1;
	} unix_server;

	struct
	{
		CommEvTCPServerCBH *handler;
		void *data;
	} events[COMM_SERVER_EVENT_LASTITEM];

	struct
	{
		unsigned int reuse_addr:1;
		unsigned int reuse_port:1;
	} flags;

} CommEvTCPServerConf;
/************************************************************/
typedef struct _CommEvTCPServerListener
{
	DLinkedListNode node;
	struct _CommEvTCPServer *parent_srv;

	int unix_lid;
	int slot_id;
	int socket_fd;
	int port;

	struct
	{
		SSL_CTX *ssl_context;
	} ssldata;

	struct
	{
		unsigned int active:1;
		unsigned int unix_server_active:1;
	} flags;

} CommEvTCPServerListener;
/************************************************************/
typedef struct _CommEvTCPServerConnEventPrototype
{
	CommEvTCPServerCBH *cb_handler_ptr;
	void *cb_data_ptr;
	struct timeval last_tv;
	unsigned long last_ts;

	struct
	{
		unsigned int enabled :1;
		unsigned int mutex_init :1;
	} flags;

} CommEvTCPServerConnEventPrototype;
/************************************************************/
typedef struct _CommEvTCPServer
{
	EvBaseKQObject kq_obj;
	CommEvTCPServerConnEventPrototype events[COMM_TCP_SERVER_MAX_LISTERNERS][COMM_SERVER_EVENT_LASTITEM];
	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;
	struct _CommEvUNIXServer *unix_server;
	int ref_count;

	struct
	{
		DLinkedList list;
		MemArena *arena;
	} conn;

	struct
	{
		EVP_PKEY *main_key;
		RSA *rsa_key;

		struct
		{
			AssocArray *table;
			DLinkedList list;
		} cert_cache;
	} ssldata;

	struct
	{
		DLinkedList list;
		int gc_timerid;
	} transfer;

	struct
	{
		DLinkedList list;
		SlotQueue slot;
		CommEvTCPServerListener arr[COMM_TCP_SERVER_MAX_LISTERNERS];
	} listener;

	struct
	{
		unsigned int destroyed:1;
	} flags;

	struct
	{
		CommEvTCPServerBindMethod bind_method;
		CommEvTCPServerReadMethod read_mthd;
		CommEvTCPServerProtocol srv_proto;
		CommEvTCPServerType srv_type;

		struct sockaddr_storage bind_addr;
		long cli_queue_max;
		int port;

		struct
		{
			int autodetect_ms;
			int transfer_ms;
			int inactive_ms;
		} timeout;

		struct
		{
			char token_str_buf[COMM_TCP_SERVER_SELFSYNC_MAX_TOKEN_SZ];
			int token_str_sz;
			int max_buffer_sz;
		} self_sync;

		struct
		{
			CommEvTCPServerCertificate ca_cert;
			char ca_key_path[512];
			char ca_cert_path[512];
		} ssl;

		struct
		{
			unsigned int self_sync:1;
			unsigned int reuse_addr:1;
			unsigned int reuse_port:1;
		} flags;

	} cfg [COMM_TCP_SERVER_MAX_LISTERNERS];

} CommEvTCPServer;
/************************************************************/
typedef struct _CommEvTCPServerConn
{
	DLinkedListNode conn_node;
	CommEvContentTransformerInfo transform;
	CommEvStatistics statistics;
	CommEvTCPIOData iodata;
	CommEvTCPSSLData ssldata;
	CommEvTCPServerConnEventPrototype events[CONN_EVENT_LASTITEM];

	struct _CommEvTCPServerListener *listener;
	struct _CommEvTCPServer *parent_srv;

	struct sockaddr_storage conn_addr;
	struct sockaddr_storage local_addr;

	char string_ip[48];
	char server_ip[48];

	int cli_port;
	int socket_fd;

	DLinkedList user_list;

	int thrd_id;
	int user_int;
	long user_long;
	void *user_data;

	void *webengine_data;
	void *mux_data;

	struct
	{
		DLinkedListNode node;
		struct timeval tv;
		unsigned long io_loop;
	} transfer;

	struct
	{
		int calculate_datarate_id;
	} timers;

	struct
	{
		char *token_str;
		int token_str_sz;
		int max_buffer_sz;
	} self_sync;

	struct
	{
		unsigned int ssl_enabled :1;
		unsigned int ssl_init :1;
		unsigned int ssl_cert_custom:1;
		unsigned int ssl_cert_cached:1;
		unsigned int ssl_cert_destroy_onclose:1;
		unsigned int ssl_shuting_down:1;
		unsigned int tls_has_sni:1;
		unsigned int ssl_handshake_ongoing:1;
		unsigned int ssl_handshake_defer:1;
		unsigned int ssl_handshake_abort:1;
		unsigned int ssl_handshake_unknown:1;
		unsigned int ssl_fatal_error:1;
		unsigned int iodata_mutex_init :1;
		unsigned int close_request:1;
		unsigned int socket_eof:1;
		unsigned int self_sync:1;
		unsigned int calculate_datarate:1;
		unsigned int conn_hnd_inuse:1;
		unsigned int conn_unlimited_enqueue:1;
		unsigned int conn_recvd_from_unixsrv:1;
		unsigned int conn_hnd_in_transfer:1;
		unsigned int pending_write:1;
		unsigned int conn_accept_invoked:1;
		unsigned int peek_on_read:1;
	} flags;

} CommEvTCPServerConn;
/************************************************************/
#define COMM_TCP_CLIENT_POOL_MAX	64
/************************************************************/
typedef void CommEvTCPClientCBH(int, int, int, void*, void*);
/************************************************************/
typedef enum
{
	COMM_CLIENT_EVENT_READ,
	COMM_CLIENT_EVENT_WRITE,
	COMM_CLIENT_EVENT_CLOSE,
	COMM_CLIENT_EVENT_CONNECT,
	COMM_CLIENT_EVENT_LASTITEM
} CommEvTCPClientEventCodes;
/************************************************************/
typedef enum
{
	COMM_CLIENTPROTO_PLAIN,
	COMM_CLIENTPROTO_SSL,
	COMM_CLIENTPROTO_LASTITEM,
} CommEvTCPClientProtocol;
/************************************************************/
typedef enum
{
	COMM_CLIENT_STATE_DISCONNECTED,
	COMM_CLIENT_STATE_RESOLVING_DNS,
	COMM_CLIENT_STATE_CONNECTING,
	COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SSL,
	COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SECURE_TUNNEL,
	COMM_CLIENT_STATE_CONNECTED,
	COMM_CLIENT_STATE_CONNECT_FAILED_TIMEOUT,
	COMM_CLIENT_STATE_CONNECT_FAILED_DNS,
	COMM_CLIENT_STATE_CONNECT_FAILED_REFUSED,
	COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SSL,
	COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SECURE_TUNNEL,
	COMM_CLIENT_STATE_CONNECT_FAILED_AUTHENTICATING_SECURE_TUNNEL,
	COMM_CLIENT_STATE_CONNECT_FAILED_CONNECT_SYSCALL,
	COMM_CLIENT_STATE_CONNECT_FAILED_UNKNWON,
	COMM_CLIENT_STATE_LASTITEM
} CommEvTCPClientStateCodes;
/************************************************************/
typedef enum
{
	COMM_CLIENT_READ_MEMBUFFER,
	COMM_CLIENT_READ_MEMSTREAM,
} CommEvTCPClientReadMethod;
/************************************************************/
typedef enum
{
	COMM_CLIENT_ADDR_TYPE_INET_4,
	COMM_CLIENT_ADDR_TYPE_INET_6,
	COMM_CLIENT_ADDR_TYPE_INET_46,
	COMM_CLIENT_ADDR_TYPE_INET_64

} CommEvTCPClientAddrMethod;
/************************************************************/
typedef enum
{
	COMM_POOL_SELECT_ROUND_ROBIN,
	COMM_POOL_SELECT_LEAST_LOAD,
	COMM_POOL_SELECT_CONNECTED,
	COMM_POOL_SELECT_ANY,
	COMM_POOL_SELECT_LASTITEM
} CommEvPoolSelectCode;
/************************************************************/
static const char *CommEvTCPClientStateCodesStr[] =
{
		"COMM_CLIENT_STATE_DISCONNECTED",
		"COMM_CLIENT_STATE_RESOLVING_DNS",
		"COMM_CLIENT_STATE_CONNECTING",
		"COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SSL",
		"COMM_CLIENT_STATE_CONNECTED_NEGOTIATING_SECURE_TUNNEL",
		"COMM_CLIENT_STATE_CONNECTED",
		"COMM_CLIENT_STATE_CONNECT_FAILED_TIMEOUT",
		"COMM_CLIENT_STATE_CONNECT_FAILED_DNS",
		"COMM_CLIENT_STATE_CONNECT_FAILED_REFUSED",
		"COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SSL",
		"COMM_CLIENT_STATE_CONNECT_FAILED_NEGOTIATING_SECURE_TUNNEL",
		"COMM_CLIENT_STATE_CONNECT_FAILED_AUTHENTICATING_SECURE_TUNNEL",
		"COMM_CLIENT_STATE_CONNECT_FAILED_CONNECT_SYSCALL",
		"COMM_CLIENT_STATE_CONNECT_FAILED_UNKNWON",
		"COMM_CLIENT_STATE_LASTITEM",
		NULL
};
/*******************************************************/
typedef struct _CommEvTCPClientConf
{
	EvDNSResolverBase *resolv_base;
	CommEvTCPClientReadMethod read_mthd;
	CommEvTCPClientProtocol cli_proto;
	CommEvTCPClientAddrMethod addr_mthd;

	struct
	{
		char *ca_path_ptr;
		char *crt_path_ptr;
		char *key_path_ptr;
	} ssl;

	struct _EvKQBaseLogBase *log_base;
	struct sockaddr_storage src_addr;
	char *hostname;
	char *sni_hostname_str;
	int port;

	struct
	{
		char *token_str;
		int max_buffer_sz;
	} self_sync;

	struct
	{
		int connect_ms;
		int read_ms;
		int write_ms;
	} timeout;

	struct
	{
		int reconnect_after_timeout_ms;
		int reconnect_after_close_ms;
		int reconnect_on_fail_ms;
		int calculate_datarate_ms;
	} retry_times;

	struct
	{
		unsigned int reconnect_on_timeout:1;
		unsigned int reconnect_on_close:1;
		unsigned int reconnect_on_fail:1;
		unsigned int reconnect_new_dnslookup:1;
		unsigned int reconnect_balance_on_ips:1;
		unsigned int destroy_after_connect_fail:1;
		unsigned int destroy_after_close:1;
		unsigned int calculate_datarate:1;
		unsigned int bindany_active:1;
		unsigned int ssl_null_cypher:1;
	} flags;


} CommEvTCPClientConf;
/*******************************************************/
typedef struct _CommEvTCPClientEventPrototype
{
	CommEvTCPClientCBH *cb_handler_ptr;
	EvBaseKQCBH *cb_hook_ptr;
	void *cb_data_ptr;
	struct timeval last_tv;
	unsigned long last_ts;

	struct
	{
		unsigned int enabled:1;
		unsigned int hooked:1;
		unsigned int mutex_init:1;
	} flags;

} CommEvTCPClientEventPrototype;
/*******************************************************/
typedef struct _CommEvTCPClient
{
	EvBaseKQObject kq_obj;
	CommEvTCPClientProtocol cli_proto;
	CommEvTCPClientReadMethod read_mthd;
	CommEvTCPClientAddrMethod addr_mthd;

	CommEvStatistics statistics;
	CommEvContentTransformerInfo transform;
	CommEvTCPClientEventPrototype events[COMM_CLIENT_EVENT_LASTITEM];
	CommEvTCPIOData iodata;
	CommEvTCPSSLData ssldata;
	EvDNSReplyHandler dnsdata;

	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;
	struct _CommEvTCPClientPool *parent_pool;

	/* sockaddr_storage structure defined in <sys/socket.h> is large enough to accommodate main sockaddr_xxx */
	struct sockaddr_storage dst_addr;
	struct sockaddr_storage src_addr;

	void *user_data;
	long user_long;
	int user_int;

	int socket_fd;
	int socket_state;
	int cli_id_onpool;

	struct
	{
		int connect_ms;
		int read_ms;
		int write_ms;
	} timeout;

	struct
	{
		int reconnect_id;
//		int reconnect_after_timeout_id;
//		int reconnect_after_close_id;
//		int reconnect_on_fail_id;
		int calculate_datarate_id;
	} timers;

	struct
	{
		int reconnect_after_timeout_ms;
		int reconnect_after_close_ms;
		int reconnect_on_fail_ms;
		int calculate_datarate_ms;
	} retry_times;

	struct
	{
//		char unix_path[1024];
		char hostname[1024];
		char sni_hostname[1024];
		int port;

		struct
		{
			char path_ca[1024];
			char path_crt[1024];
			char path_key[1024];
		} ssl;

		struct
		{
			char token_str_buf[COMM_TCP_SERVER_SELFSYNC_MAX_TOKEN_SZ];
			int token_str_sz;
			int max_buffer_sz;
		} self_sync;

		struct
		{
			unsigned int self_sync:1;
			unsigned int unix_socket:1;
		} flags;

	} cfg;

	struct
	{
		unsigned int reconnect_on_timeout:1;
		unsigned int reconnect_on_close:1;
		unsigned int reconnect_on_fail:1;
		unsigned int reconnect_new_dnslookup:1;
		unsigned int reconnect_balance_on_ips:1;
		unsigned int destroy_after_connect_fail:1;
		unsigned int destroy_after_close:1;
		unsigned int ssl_enabled:1;
		unsigned int ssl_shuting_down:1;
		unsigned int ssl_fatal_error:1;
		unsigned int ssl_zero_ret:1;
		unsigned int ssl_null_cypher:1;
		unsigned int need_dns_lookup:1;
		unsigned int socket_eof:1;
		unsigned int calculate_datarate:1;
		unsigned int close_request:1;
		unsigned int bindany_active:1;
		unsigned int pending_write:1;
		unsigned int socket_in_transfer:1;
		unsigned int peek_on_read:1;
	} flags;

} CommEvTCPClient;

typedef struct _CommEvTCPClientPoolConf
{
	int cli_count_init;
	int cli_count_max;

	struct
	{
		unsigned int foo:1;
	} flags;

} CommEvTCPClientPoolConf;

typedef struct _CommEvTCPClientPool
{
	CommEvTCPClientPoolConf pool_conf;
	CommEvTCPClientConf cli_conf;
	EvBaseKQObject kq_obj;

	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;

	long user_long;
	int user_int;
	void *user_data;

	struct
	{
		MemSlotBase memslot;
		int count_init;
		int count_online;
		int rr_current;
	} client;

	struct
	{
		unsigned int has_error:1;
	} flags;

} CommEvTCPClientPool;

/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
typedef enum
{
	COMM_SSHCLIENT_DIRECTION_EAGAIN,
	COMM_SSHCLIENT_DIRECTION_FAIL,
	COMM_SSHCLIENT_DIRECTION_FINISH,
	COMM_SSHCLIENT_DIRECTION_LASTITEM
} CommEvSSHClientDirectionCodes;
/*******************************************************/
typedef struct _CommEvSSHClientConf
{
	EvDNSResolverBase *resolv_base;
	struct _EvKQBaseLogBase *log_base;

	char *hostname;
	char *username;
	char *password;
	char *known_hosts_filepath;

	int port;

	struct
	{
		int connect_ms;
		int read_ms;
		int write_ms;
	} timeout;

	struct
	{
		int reconnect_after_timeout_ms;
		int reconnect_after_close_ms;
		int reconnect_on_fail_ms;
	} retry_times;

	struct
	{
		unsigned int notify_conn_progress:1;
		unsigned int reconnect_on_timeout:1;
		unsigned int reconnect_on_close:1;
		unsigned int reconnect_on_fail:1;
		unsigned int reconnect_new_dnslookup:1;
		unsigned int reconnect_balance_on_ips:1;
		unsigned int check_known_hosts:1;
	} flags;

} CommEvSSHClientConf;

typedef struct _CommEvSSHClient
{
	EvDNSResolverBase *resolv_base;
	EvBaseKQObject kq_obj;
	MemBuffer *read_buffer;
	EvAIOReqQueue write_queue;
	CommEvIOFunc *io_func[COMM_EV_IOFUNC_LASTITEM];
	CommEvStatistics statistics;

	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;


	struct sockaddr_in conn_addr;
	int socket_fd;
	int socket_state;
	int dnsreq_id;
	void *user_data;
	int user_int;
	long user_long;

	struct
	{
		LIBSSH2_SESSION *session;
		LIBSSH2_CHANNEL *channel;
		LIBSSH2_KNOWNHOSTS *known_hosts;
		const char *fingerprint;

		struct
		{
			char *str;
			int str_sz;
			int code;
		} last_err;
	} ssh;

	struct
	{
		DNSAReply a_reply;
		long expire_ts;
		int cur_idx;
	} dns;

	struct
	{
		char known_hosts_path[1024];
		char hostname[1024];
		char username[128];
		char password[1024];
		int port;
	} cfg;

	struct
	{
		CommEvTCPClientCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct
		{
			unsigned int enabled :1;
			unsigned int mutex_init :1;
		} flags;
	} events[COMM_CLIENT_EVENT_LASTITEM];

	struct
	{
		int reconnect_after_timeout_id;
		int reconnect_after_close_id;
		int reconnect_on_fail_id;
		int calculate_datarate_id;
	} timers;

	struct
	{
		int connect_ms;
		int read_ms;
		int write_ms;
	} timeout;

	struct
	{
		int reconnect_after_timeout_ms;
		int reconnect_after_close_ms;
		int reconnect_on_fail_ms;
	} retry_times;

	struct
	{
		unsigned int handshake :1;
		unsigned int authenticated :1;
		unsigned int known_host :1;
		unsigned int session_opened :1;
		unsigned int need_dns_lookup:1;

		unsigned int notify_conn_progress:1;
		unsigned int reconnect_on_timeout:1;
		unsigned int reconnect_on_close:1;
		unsigned int reconnect_on_fail:1;
		unsigned int reconnect_new_dnslookup:1;
		unsigned int reconnect_balance_on_ips:1;
		unsigned int check_known_hosts:1;
	} flags;
} CommEvSSHClient;
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
typedef enum
{
	COMM_SERIAL_EVENT_READ,
	COMM_SERIAL_EVENT_CLOSE,
	COMM_SERIAL_EVENT_CONNECT,
	COMM_SERIAL_EVENT_LASTITEM
} CommEvSerialEventCodes;

typedef enum
{
	COMM_SERIAL_PARITY_NONE,
	COMM_SERIAL_PARITY_ODD,
	COMM_SERIAL_PARITY_EVEN,
	COMM_SERIAL_PARITY_MARK,
	COMM_SERIAL_PARITY_SPACE,
	COMM_SERIAL_PARITY_LASTITEM,
} CommEvSerialParityCodes;

typedef enum
{
	COMM_SERIAL_LINESTATE_DCD	= 0x040,	/*! Data Carrier Detect (read only) */
	COMM_SERIAL_LINESTATE_CTS	= 0x020,	/*! Clear To Send (read only) */
	COMM_SERIAL_LINESTATE_DSR	= 0x100,	/*! Data Set Ready (read only) */
	COMM_SERIAL_LINESTATE_DTR	= 0x002,	/*! Data Terminal Ready (write only) */
	COMM_SERIAL_LINESTATE_RING	= 0x080,	/*! Ring Detect (read only) */
	COMM_SERIAL_LINESTATE_RTS	= 0x004,	/*! Request To Send (write only) */
	COMM_SERIAL_LINESTATE_NULL	= 0x000		/*! no active line state, use this for clear */
} CommEvSerialLineState;
/*******************************************************/
typedef struct _CommEvSerialPortConfig
{
	struct _EvKQBaseLogBase *log_base;
	char *device_name_str;

	unsigned char wordlen;
	unsigned char stopbits;
	int parity;
	int baud;

	struct
	{
		unsigned int rtscts:1;
		unsigned int xonxoff:1;
	} flow_control;

	struct
	{
		unsigned int exclusive_lock:1;
		unsigned int empty_read_buffer:1;
		unsigned int echo_ignore:1;
	} flags;

} CommEvSerialPortConfig;

typedef struct _CommEvSerialPort
{
	EvBaseKQGenericEventPrototype events[COMM_SERIAL_EVENT_LASTITEM];
	CommEvStatistics statistics;
	EvBaseKQObject kq_obj;
	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;

	char device_name_str[256];
	char device_mode_str[16];

	unsigned char wordlen;
	unsigned char stopbits;

	int err_code;
	int parity;
	int baud;
	int fd;

	struct
	{
		MemBuffer *read_buffer;
		EvAIOReqQueue write_queue;
	} iodata;

	struct
	{
		struct termios old;
		struct termios cur;
	} options;

	struct
	{
		unsigned int rtscts:1;
		unsigned int xonxoff:1;
	} flow_control;

	struct
	{
		unsigned int pending_write:1;
		unsigned int close_request:1;
		unsigned int destroy_after_close:1;
		unsigned int eof:1;
		unsigned int echo_ignore:1;
	} flags;

} CommEvSerialPort;
/******************************************************************************************************/
/* PUBLIC PROTOTYPES */
/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
/* comm/core/comm_core_utils.c */
/******************************************************************************************************/
int CommEvUtilsFDCheckState(int socket_fd);
void CommEvNetClientAddrInit(struct sockaddr_in *conn_addr, char *host, unsigned short port);

/******************************************************************************************************/
/* comm/core/tcp/comm_tcp_aio.c */
/******************************************************************************************************/
int CommEvTCPAIOWrite(struct _EvKQBase *ev_base, struct _EvKQBaseLogBase *log_base, CommEvStatistics *stats, CommEvTCPIOData *iodata, CommEvTCPIOResult *ioret, void *parent,
		int can_write_sz, int invoke_cb);
/******************************************************************************************************/
/* comm/core/serial/comm_serial.c */
/******************************************************************************************************/
CommEvSerialPort *CommEvSerialPortNew(struct _EvKQBase *ev_base);
int CommEvSerialPortDestroy(CommEvSerialPort *serial_port);
CommEvSerialPort *CommEvSerialPortOpen(struct _EvKQBase *ev_base, CommEvSerialPortConfig *serial_config);
int CommEvSerialPortClose(CommEvSerialPort *serial_port);
int CommEvSerialAIOWriteString(CommEvSerialPort *serial_port, char *data_str, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int CommEvSerialAIOWrite(CommEvSerialPort *serial_port, char *data, unsigned long data_sz, EvAIOReqCBH *finish_cb, void *finish_cbdata);
int CommEvSerialPortEventIsSet(CommEvSerialPort *serial_port, int ev_type);
void CommEvSerialPortEventSet(CommEvSerialPort *serial_port, int ev_type, EvBaseKQCBH *cb_handler, void *cb_data);
void CommEvSerialPortEventCancel(CommEvSerialPort *serial_port, int ev_type);
void CommEvSerialPortEventCancelAll(CommEvSerialPort *serial_port);


/******************************************************************************************************/
/* comm/core/tcp/comm_tcp_server.c */
/******************************************************************************************************/
void CommEvTCPServerEventSet(CommEvTCPServer *srv_ptr, int listener_id, CommEvTCPServerEventCodes ev_type, CommEvTCPServerCBH *cb_handler, void *cb_data);
int CommEvTCPServerSwitchConnProto(CommEvTCPServerConn *conn_hnd, int proto);
int CommEvTCPServerConnReadReschedule(CommEvTCPServerConn *conn_hnd);
int CommEvTCPServerKickConnReadPending(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr);
int CommEvTCPServerKickConnFromAcceptDefer(CommEvTCPServerConn *conn_hnd);
int CommEvTCPServerDestroy(CommEvTCPServer *srv_ptr);
CommEvTCPServer *CommEvTCPServerLink(CommEvTCPServer *srv_ptr);
int CommEvTCPServerUnlink(CommEvTCPServer *srv_ptr);
CommEvTCPServer *CommEvTCPServerNew(struct _EvKQBase *kq_base);
int CommEvTCPServerListenerConnShutdownAll(CommEvTCPServer *srv_ptr, int listener_id);
int CommEvTCPServerListenerDel(CommEvTCPServer *srv_ptr, int listener_id);
int CommEvTCPServerListenerAdd(CommEvTCPServer *srv_ptr, CommEvTCPServerConf *server_conf);
void CommEvTCPServerKickConnWriteQueue(CommEvTCPServerConn *conn_hnd);
void CommEvTCPServerSSLCertCacheDestroy(CommEvTCPServerCertificate *cert_info);
int CommEvTCPServerSSLCertCacheInsert(CommEvTCPServer *srv_ptr, char *dnsname_str, X509 *x509_cert);
X509 *CommEvTCPServerSSLCertCacheLookupByConnHnd(CommEvTCPServerConn *conn_hnd);
X509 *CommEvTCPServerSSLCertCacheLookup(CommEvTCPServer *srv_ptr, char *dnsname_str);

/******************************************************************************************************/
/* comm/core/tcp/comm_tcp_server_conn.c */
/******************************************************************************************************/
int CommEvTCPServerConnTransferViaUnixClientPool(CommEvTCPServerConn *conn_hnd, CommEvUNIXClientPool *unix_client_pool);
int CommEvTCPServerConnTransferViaUnixClientPoolWithMB(CommEvTCPServerConn *conn_hnd, CommEvUNIXClientPool *unix_client_pool, MemBuffer *payload_mb);
void CommEvTCPServerConnCloseRequest(CommEvTCPServerConn *conn_hnd);
void CommEvTCPServerConnClose(CommEvTCPServerConn *conn_hnd);
int CommEvTCPServerConnTimersCancelAll(CommEvTCPServerConn *conn_hnd);
int CommEvTCPServerConnAIOWriteVectored(CommEvTCPServerConn *conn_hnd, EvAIOReqIOVectorData *vector_table, int vector_table_sz, CommEvTCPServerCBH *finish_cb, void *finish_cbdata);
int CommEvTCPServerConnAIOWriteAndDestroyMemBufferOffset(CommEvTCPServerConn *conn_hnd, MemBuffer *mem_buf, int offset, CommEvTCPServerCBH *finish_cb, void *finish_cbdata);
int CommEvTCPServerConnAIOWriteAndDestroyMemBuffer(CommEvTCPServerConn *conn_hnd, MemBuffer *mem_buf, CommEvTCPServerCBH *finish_cb, void *finish_cbdata);
int CommEvTCPServerConnAIOWriteMemBuffer(CommEvTCPServerConn *conn_hnd, MemBuffer *mem_buf, CommEvTCPServerCBH *finish_cb, void *finish_cbdata);
int CommEvTCPServerConnAIOWriteStringFmt(CommEvTCPServerConn *conn_hnd, CommEvTCPServerCBH *finish_cb, void *finish_cbdata, char *string, ...);
int CommEvTCPServerConnAIOWriteString(CommEvTCPServerConn *conn_hnd, char *string, CommEvTCPServerCBH *finish_cb, void *finish_cbdata);
int CommEvTCPServerConnAIOWrite(CommEvTCPServerConn *conn_hnd, char *data, unsigned long data_sz, CommEvTCPServerCBH *finish_cb, void *finish_cbdata);
int CommEvTCPServerConnSSLShutdownBegin(CommEvTCPServerConn *conn_hnd);
int CommEvTCPServerConnInternalShutdown(CommEvTCPServerConn *conn_hnd);
void CommEvTCPServerConnCancelWriteQueue(CommEvTCPServerConn *conn_hnd);
void CommEvTCPServerConnCancelEvents(CommEvTCPServerConn *conn_hnd);
void CommEvTCPServerConnSetDefaultEvents(CommEvTCPServer *srv_ptr, CommEvTCPServerConn *conn_hnd);
void CommEvTCPServerConnSetEvent(CommEvTCPServerConn *conn_hnd, CommEvTCPServerConnEventCodes ev_type, CommEvTCPServerCBH *cb_handler, void *cb_data);
void CommEvTCPServerConnDispatchEventByFD(CommEvTCPServer *srv_ptr, int fd, int data_sz, int thrd_id, int ev_type);
void CommEvTCPServerConnDispatchEvent(CommEvTCPServerConn *conn_hnd, int data_sz, int thrd_id, int ev_type);
CommEvTCPServerConn *CommEvTCPServerConnArenaGrab(CommEvTCPServer *srv_ptr, int fd);
void CommEvTCPServerConnArenaNew(CommEvTCPServer *srv_ptr);
void CommEvTCPServerConnArenaDestroy(CommEvTCPServer *srv_ptr);
int CommEvTCPServerConnIODataLock(CommEvTCPServerConn *conn_hnd);
int CommEvTCPServerConnIODataUnlock(CommEvTCPServerConn *conn_hnd);
int CommEvTCPServerConnIODataDestroy(CommEvTCPServerConn *conn_hnd);
void CommEvTCPServerConnSSLSessionInit(CommEvTCPServerConn *ret_conn);
void CommEvTCPServerConnSSLSessionDestroy(CommEvTCPServerConn *ret_conn);
char *CommEvTCPServerConnSSLDataGetSNIStr(CommEvTCPServerConn *conn_hnd);



/******************************************************************************************************/
/* comm_tcp_client.c */
/******************************************************************************************************/
CommEvTCPClient *CommEvTCPClientNew(struct _EvKQBase *kq_base);
CommEvTCPClient *CommEvTCPClientNewUNIX(struct _EvKQBase *kq_base);
int CommEvTCPClientInit(struct _EvKQBase *kq_base, CommEvTCPClient *ev_tcpclient, int cli_id_onpool);
void CommEvTCPClientDestroy(CommEvTCPClient *ev_tcpclient);
void CommEvTCPClientShutdown(CommEvTCPClient *ev_tcpclient);
int CommEvTCPClientResetConn(CommEvTCPClient *ev_tcpclient);
int CommEvTCPClientConnect(CommEvTCPClient *ev_tcpclient, CommEvTCPClientConf *ev_tcpclient_conf);
int CommEvTCPClientReconnect(CommEvTCPClient *ev_tcpclient);
void CommEvTCPClientDisconnect(CommEvTCPClient *ev_tcpclient);
void CommEvTCPClientInternalDisconnect(CommEvTCPClient *ev_tcpclient);
int CommEvTCPClientDisconnectRequest(CommEvTCPClient *ev_tcpclient);
void CommEvTCPClientDestroyConnReadAndWriteBuffers(CommEvTCPClient *ev_tcpclient);
void CommEvTCPClientResetFD(CommEvTCPClient *ev_tcpclient);

int CommEvTCPClientAIOWriteVectored(CommEvTCPClient *ev_tcpclient, EvAIOReqIOVectorData *vector_table, int vector_table_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
int CommEvTCPClientAIOWriteAndDestroyMemBuffer(CommEvTCPClient *ev_tcpclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
int CommEvTCPClientAIOWriteMemBuffer(CommEvTCPClient *ev_tcpclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
int CommEvTCPClientAIOWriteStringFmt(CommEvTCPClient *ev_tcpclient, CommEvTCPClientCBH *finish_cb, void *finish_cbdata, char *string, ...);
int CommEvTCPClientAIOWriteString(CommEvTCPClient *ev_tcpclient, char *string, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
int CommEvTCPClientAIOWriteAndFree(CommEvTCPClient *ev_tcpclient, char *data, unsigned long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
int CommEvTCPClientAIOWrite(CommEvTCPClient *ev_tcpclient, char *data, unsigned long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
int CommEvTCPClientEventIsSet(CommEvTCPClient *ev_tcpclient, CommEvTCPClientEventCodes ev_type);
void CommEvTCPClientEventSet(CommEvTCPClient *ev_tcpclient, CommEvTCPClientEventCodes ev_type, CommEvTCPClientCBH *cb_handler, void *cb_data);
void CommEvTCPClientEventCancel(CommEvTCPClient *ev_tcpclient, CommEvTCPClientEventCodes ev_type);
void CommEvTCPClientEventCancelAll(CommEvTCPClient *ev_tcpclient);
void CommEvTCPClientAddrInit(CommEvTCPClient *ev_tcpclient, char *host, unsigned short port);
void CommEvTCPClientAddrInitUnix(CommEvTCPClient *ev_tcpclient, char *unix_path);

int CommEvTCPClientReconnectSchedule(CommEvTCPClient *ev_tcpclient, int schedule_ms);
int CommEvTCPClientRatesCalculateSchedule(CommEvTCPClient *ev_tcpclient, int schedule_ms);
void CommEvTCPClientEventDispatchInternal(CommEvTCPClient *ev_tcpclient, int data_sz, int thrd_id, int ev_type);
int CommEvTCPClientProcessBuffer(CommEvTCPClient *ev_tcpclient, int read_sz, int thrd_id, char *read_buf, int read_buf_sz);
/* AIO events */
EvBaseKQCBH CommEvTCPClientEventConnect;
EvBaseKQCBH CommEvTCPClientEventEof;
EvBaseKQCBH CommEvTCPClientEventRead;
EvBaseKQCBH CommEvTCPClientEventWrite;

/* SSL support */
EvBaseKQCBH CommEvTCPClientEventSSLNegotiate;
EvBaseKQCBH CommEvTCPClientSSLShutdown;
EvBaseKQCBH CommEvTCPClientEventSSLRead;
EvBaseKQCBH CommEvTCPClientEventSSLWrite;
int CommEvTCPClientSSLShutdownBegin(CommEvTCPClient *ev_tcpclient);
int CommEvTCPClientSSLDataInit(CommEvTCPClient *ev_tcpclient);
int CommEvTCPClientSSLDataClean(CommEvTCPClient *ev_tcpclient);
int CommEvTCPClientSSLDataReset(CommEvTCPClient *ev_tcpclient);
int CommEvTCPClientSSLPeerVerify(CommEvTCPClient *ev_tcpclient);
/******************************************************************************************************/
/* comm_tcp_client_pool.c */
/******************************************************************************************************/
CommEvTCPClientPool *CommEvTCPClientPoolNew(struct _EvKQBase *kq_base, CommEvTCPClientPoolConf *tcp_clientpool_conf);
int CommEvTCPClientPoolDestroy(CommEvTCPClientPool *tcp_clientpool);
int CommEvTCPClientPoolConnect(CommEvTCPClientPool *tcp_clientpool, CommEvTCPClientConf *ev_tcpclient_conf);
int CommEvTCPClientPoolDisconnect(CommEvTCPClientPool *tcp_clientpool);
int CommEvTCPClientPoolReconnect(CommEvTCPClientPool *tcp_clientpool);
CommEvTCPClient *CommEvTCPClientPoolClientSelect(CommEvTCPClientPool *tcp_clientpool, int select_method, int select_connected);
CommEvTCPClient *CommEvTCPClientPoolClientSelectLeastLoad(CommEvTCPClientPool *tcp_clientpool, int select_connected);
CommEvTCPClient *CommEvTCPClientPoolClientSelectRoundRobin(CommEvTCPClientPool *tcp_clientpool, int select_connected);
int CommEvTCPClientPoolHasConnected(CommEvTCPClientPool *tcp_clientpool);
int CommEvTCPClientPoolEventSet(CommEvTCPClientPool *tcp_clientpool, CommEvTCPClientEventCodes ev_type, CommEvTCPClientCBH *cb_handler, void *cb_data);
int CommEvTCPClientPoolEventCancel(CommEvTCPClientPool *tcp_clientpool, CommEvTCPClientEventCodes ev_type);
int CommEvTCPClientPoolEventCancelAll(CommEvTCPClientPool *tcp_clientpool);
int CommEvTCPClientPoolAIOWrite(CommEvTCPClientPool *tcp_clientpool, int select_method, char *data, long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);

/******************************************************************************************************/
/* comm_ssh_client.c */
/******************************************************************************************************/
void CommEvSSHClientAIOWriteAndDestroyMemBuffer(CommEvSSHClient *ev_sshclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
void CommEvSSHClientAIOWriteMemBuffer(CommEvSSHClient *ev_sshclient, MemBuffer *mem_buf, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
void CommEvSSHClientAIOWriteStringFmt(CommEvSSHClient *ev_sshclient, CommEvTCPClientCBH *finish_cb, void *finish_cbdata, char *string, ...);
void CommEvSSHClientAIOWriteString(CommEvSSHClient *ev_sshclient, char *string, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
void CommEvSSHClientAIOWrite(CommEvSSHClient *ev_sshclient, char *data, unsigned long data_sz, CommEvTCPClientCBH *finish_cb, void *finish_cbdata);
CommEvSSHClient *CommEvSSHClientNew(struct _EvKQBase *kq_base);
void CommEvSSHClientDestroy(CommEvSSHClient *ev_sshclient);
int CommEvSSHClientEventIsSet(CommEvSSHClient *ev_sshclient, CommEvTCPClientEventCodes ev_type);
void CommEvSSHClientEventSet(CommEvSSHClient *ev_sshclient, CommEvTCPClientEventCodes ev_type, CommEvTCPClientCBH *cb_handler, void *cb_data);
void CommEvSSHClientEventCancel(CommEvSSHClient *ev_sshclient, CommEvTCPClientEventCodes ev_type);
void CommEvSSHClientEventCancelAll(CommEvSSHClient *ev_sshclient);
void CommEvSSHClientTimeoutSet(CommEvSSHClient *ev_sshclient, int type, int time, EvBaseKQCBH *timeout_cb, void *cb_data);
void CommEvSSHClientResetFD(CommEvSSHClient *ev_sshclient);
int CommEvSSHClientReconnect(CommEvSSHClient *ev_sshclient);
int CommEvSSHClientConnect(CommEvSSHClient *ev_sshclient, CommEvSSHClientConf *ev_sshclient_conf);
void CommEvSSHClientDisconnect(CommEvSSHClient *ev_sshclient);

/******************************************************************************************************/
/* comm_ssl_utils.c */
/******************************************************************************************************/
X509 *CommEvSSLUtils_X509ForgeAndSignFromConnHnd(CommEvTCPServerConn *conn_hnd, int valid_sec, int wildcard);
X509 *CommEvSSLUtils_X509ForgeAndSignFromParams(X509 *ca_cert, EVP_PKEY *cakey, EVP_PKEY *key, const char *dnsname_str, long valid_sec);
StringArray *CommEvSSLUtils_X509AltNamesToStringArray(X509 *cert);
X509 *CommEvSSLUtils_X509ForgeAndSignFromOrigCert(X509 *ca_cert, EVP_PKEY *cakey, X509 *origcrt, const char *dnsname_str, EVP_PKEY *key, long valid_sec);
int CommEvSSLUtils_X509CertSign(X509 *target_cert, EVP_PKEY *cakey);

void CommEvSSLUtils_X509CertRefCountInc(X509 *crt, int thread_safe);
int CommEvSSLUtils_X509CopyRandom(X509 *dstcrt, X509 *srccrt);
int CommEvSSLUtils_X509V3ExtAdd(X509V3_CTX *ctx, X509 *crt, char *k, char *v);
int CommEvSSLUtils_X509V3ExtCopyByNID(X509 *crt, X509 *origcrt, int nid);
int CommEvSSLUtils_Random(void *p, unsigned long sz);

/* X.509 Certificate conversion functions */
X509 *CommEvSSLUtils_X509CertFromPEM(char *pem_str, int pem_strsz);
void CommEvSSLUtils_X509CertToStr(X509 *crt, char *ret_buf, int ret_buf_maxsz);
void CommEvSSLUtils_X509CertToPEM(X509 *crt, char *ret_buf, int ret_buf_maxsz);
X509 *CommEvSSLUtils_X509CertFromFile(const char *filename);
int CommEvSSLUtils_X509CertToFile(const char *filename, X509 *cert);

/* Public key conversion functions */
EVP_PKEY *CommEvSSLUtils_X509PublicKeyFromPEM(char *pem_str, int pem_strsz);
void CommEvSSLUtils_X509PublicKeyToPEM(EVP_PKEY *key, char *ret_buf, int ret_buf_maxsz);
int CommEvSSLUtils_X509PublicKeyWriteToFile(const char *filename, EVP_PKEY *key);
EVP_PKEY *CommEvSSLUtils_X509PublicKeyReadFromFile(const char *filename);

void CommEvSSLUtils_GenerateRSA(EVP_PKEY **dst_pkey, RSA **dst_rsa, const int keysize);
int CommEvSSLUtils_X509CertRootNew(CommEvSSLUtilsCertReq *cert_req);
int CommEvSSLUtils_GenerateWildCardFromDomain(char *src_domain, char *dst_tld_buf, int dst_tld_maxsz);
unsigned char *CommEvSSLUtils_RSASHA1Sign(const char *data_ptr, size_t data_len, EVP_PKEY *private_key, unsigned int *sign_len);
int CommEvSSLUtils_SNIParse(const unsigned char *buf, int *sz, char *ret_buf, int retbuf_maxsz);
/******************************************************************************************************/
/* Private key functions */
void CommEvSSLUtils_X509PrivateKeyRefCountInc(EVP_PKEY *key, int thread_safe);
int CommEvSSLUtils_X509PrivateKeyCheck(X509 *cert_x509, EVP_PKEY *cert_key);
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyFromMB(MemBuffer *file_mb);
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyFromPEM(char *pem_str, int pem_strsz);
int CommEvSSLUtils_GenerateRSAToServer(CommEvTCPServer *srv_ptr, const int keysize);
void CommEvSSLUtils_X509PrivateKeyToPEM(EVP_PKEY *key, char *ret_buf, int ret_buf_maxsz);
int CommEvSSLUtils_X509PrivateKeyWriteToFile(const char *filename, EVP_PKEY *key);
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyReadFromFile(const char *filename);


EVP_PKEY *CommEvSSLUtils_X509PrivateKeyReadCreate(const char *path_ptr, int ptype);
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyCreate(int type);
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyCreateRSA(int kbits);
EVP_PKEY *CommEvSSLUtils_X509PrivateKeyCreateEC(int knid);
/******************************************************************************************************/
/* comm_statistics.c */
/******************************************************************************************************/
void CommEvStatisticsRateCalculate(struct _EvKQBase *ev_base, CommEvStatistics *statistics, int socket_fd, int rates_type);
char *CommEvStatisticsBytesHumanize(long total_bytes, char *buf_ptr, int buf_maxsz);
char *CommEvStatisticsRateHumanize(long rate, char *buf_ptr, int buf_maxsz);
char *CommEvStatisticsRatePPSHumanize(long rate, char *buf_ptr, int buf_maxsz);
char *CommEvStatisticsPacketsHumanize(long total_packets, char *buf_ptr, int buf_maxsz);
char *CommEvStatisticsUptimeHumanize(long total_sec, char *buf_ptr, int buf_maxsz);
void CommEvStatisticsClean(CommEvStatistics *statistics);
void CommEvStatisticsRateClean(CommEvStatistics *statistics);
/******************************************************************************************************/
/* comm_desc_token.c */
/******************************************************************************************************/
char *CommEvTokenStrFromTokenArr(CommEvToken *token_arr, int token_code);

#endif /* LIBBRB_COMM_TCP_H_ */
