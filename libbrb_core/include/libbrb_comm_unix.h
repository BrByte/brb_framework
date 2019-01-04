/*
 * libbrb_comm_unix.h
 *
 *  Created on: 2016-10-05
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
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

#ifndef LIBBRB_COMM_UNIX_H_
#define LIBBRB_COMM_UNIX_H_


/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
#define COMM_UNIX_SERVER_MAX_LISTERNERS			4
#define COMM_UNIX_MAX_PATH_SZ					128
#define COMM_UNIX_MAX_TX_RETRY					8
#define COMM_UNIX_MAX_ACK_QUEUE					32
#define COMM_UNIX_MAX_FDARR_SZ					8
#define COMM_UNIX_MAX_WRITE_REQ					4096
#define COMM_UNIX_MAX_WRITE_SIZE				524280
#define COMM_UNIX_READ_BUFFER_SIZE				65535
#define COMM_UNIX_MAGIC_CODE00					0x31337
#define COMM_UNIX_MIN_DATA_SZ					(sizeof(CommEvUNIXControlData) + (COMM_UNIX_MAX_FDARR_SZ * sizeof(int)) + 64)

//#define COMM_UNIX_SERVER_CONN_CAN_ENQUEUE(conn_hnd)		((conn_hnd->parent_srv->cfg[conn_hnd->listener->slot_id].cli_queue_max <= 0) ? 1 : \
//														((conn_hnd->iodata.write_queue.stats.queue_sz < conn_hnd->parent_srv->cfg[conn_hnd->listener->slot_id].cli_queue_max) ? 1 : 0))

#define COMM_UNIX_CLIENT_POOL_MAX	64

/*******************************************************/
typedef enum
{
	COMM_UNIX_LIST_PENDING_WRITE,
	COMM_UNIX_LIST_PENDING_ACK,
	COMM_UNIX_LIST_LASTITEM,
} CommEvUNIXListCodes;

typedef enum
{
	COMM_UNIX_ACK_FAILED,
	COMM_UNIX_ACK_OK,
	COMM_UNIX_ACK_LASTITEM,
} CommEvUNIXACKCodes;

typedef enum
{
	COMM_UNIX_SELECT_ROUND_ROBIN,
	COMM_UNIX_SELECT_LEAST_LOAD,
	COMM_UNIX_SELECT_CONNECTED,
	COMM_UNIX_SELECT_ANY,
	COMM_UNIX_SELECT_LASTITEM
} CommEvUNIXSelectCode;

typedef enum
{
	COMM_UNIX_SERVER_EVENT_ACCEPT,
	COMM_UNIX_SERVER_EVENT_READ,
	COMM_UNIX_SERVER_EVENT_CLOSE,
	COMM_UNIX_SERVER_EVENT_LASTITEM
} CommEvUNIXServerEventCodes;

typedef enum
{
	COMM_UNIX_SERVER_CONN_EVENT_READ,
	COMM_UNIX_SERVER_CONN_EVENT_CLOSE,
	COMM_UNIX_SERVER_CONN_EVENT_WRITE_FINISH,
	COMM_UNIX_SERVER_CONN_EVENT_LASTITEM
} CommEvUNIXServerConnEventCodes;

typedef enum
{
	COMM_UNIX_CONTROL_FLAGS_REQUEST,
	COMM_UNIX_CONTROL_FLAGS_REPLY,
	COMM_UNIX_CONTROL_FLAGS_WANT_ACK,
	COMM_UNIX_CONTROL_FLAGS_LASTITEM
} CommEvUNIXControlFlags;

typedef enum
{
	COMM_UNIX_PARENT_UNIX_CONN_HND,
	COMM_UNIX_PARENT_UNIX_CLIENT,
	COMM_UNIX_PARENT_UNIX_LASTITEM,
} CommEvUNIXParentTypes;

/*******************************************************/
typedef int CommEvUNIXGenericCBH(int, int, int, void*, void*);
typedef int CommEvUNIXACKCBH(int, void*, void*);
/*******************************************************/
typedef struct _CommEvUNIXControlData
{
	long data_sz;
	int magic_code;
	int req_id;
	int flags;
	int fd_count;
	int seq_id;
} CommEvUNIXControlData;

typedef struct _CommEvUNIXACKReply
{
	int tx_retry_count;
	int req_id;
	short fd_count;
	short seq_id;

	struct
	{
		unsigned int in_use:1;
	} flags;

} CommEvUNIXACKReply;

typedef struct _CommEvUNIXWriteRequest
{
	DLinkedListNode node;
	CommEvUNIXGenericCBH *finish_cb;
	CommEvUNIXACKCBH *ack_cb;

	struct _EvKQBase *kq_base;
	void *finish_cbdata;
	void *ack_cbdata;
	void *parent_ptr;

	int req_id;
	int parent_type;
	int tx_retry_count;
	int ctrl_flags;

	void *user_data;
	long user_long;
	int user_int;

	struct
	{
		int data[COMM_UNIX_MAX_FDARR_SZ];
		int sz;
	} fd_arr;

	struct
	{
		char *ptr;
		long offset;
		long remain;
		long size;
		short seq_id;
	} data;

	struct
	{
		unsigned int in_use:1;
		unsigned int wrote:1;
		unsigned int autoclose_on_ack:1;
	} flags;

} CommEvUNIXWriteRequest;

typedef struct _CommEvUNIXIOData
{
	struct _EvKQBaseLogBase *log_base;
	int ref_count;
	int socket_fd;
	void *parent;

	struct
	{
		MemSlotBase req_mem_slot;
		MemSlotBase ack_mem_slot;
	} write;

	struct
	{
		CommEvUNIXControlData control_data;
		MemBuffer *data_mb;
		int seq_id;

		struct
		{
			int data[COMM_UNIX_MAX_FDARR_SZ + 1];
			int sz;
		} fd_arr;

	} read;

	struct
	{
		unsigned int init:1;
		unsigned int read_partial:1;
	} flags;

} CommEvUNIXIOData;


typedef struct _CommEvUNIXServerConf
{
	char *path_str;
	int listen_queue_sz;

	struct
	{
		long cli_queue_max;
	} limits;

	struct
	{
		CommEvUNIXGenericCBH *cb_handler_ptr;
		void *cb_data_ptr;

		struct
		{
			unsigned int enabled :1;
		} flags;
	} events[COMM_UNIX_SERVER_EVENT_LASTITEM];

	struct
	{
		unsigned int autoclose_fd_on_ack:1;
		unsigned int reuse_addr:1;
		unsigned int reuse_port:1;
		unsigned int no_brb_proto:1;
	} flags;
} CommEvUNIXServerConf;

typedef struct _CommEvUNIXServerListener
{
	DLinkedListNode node;
	struct _CommEvUNIXServer *parent_srv;
	char path_str[COMM_UNIX_MAX_PATH_SZ];
	int listen_queue_sz;
	int slot_id;
	int socket_fd;

	struct
	{
		unsigned int active:1;
		unsigned int autoclose_fd_on_ack:1;
		unsigned int reuse_addr:1;
		unsigned int reuse_port:1;
		unsigned int no_brb_proto:1;
	} flags;
} CommEvUNIXServerListener;

typedef struct _CommEvUNIXServer
{
	EvBaseKQObject kq_obj;

	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;

	struct
	{
		MemArena *arena;
		DLinkedList listener_list;
		DLinkedList global_list;
	} conn;

	struct
	{
		int req_scheduled;
		int req_sent_no_ack;
		int req_sent_with_ack;
		int reply_ack;
	} counters;

	struct
	{
		CommEvUNIXGenericCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct timeval last_tv;
		unsigned long last_ts;

		struct
		{
			unsigned int enabled :1;
		} flags;

	} events[COMM_UNIX_SERVER_MAX_LISTERNERS][COMM_UNIX_SERVER_EVENT_LASTITEM];

	struct
	{
		DLinkedList list;
		SlotQueue slot;
		CommEvUNIXServerListener arr[COMM_UNIX_SERVER_MAX_LISTERNERS];
	} listener;

	struct
	{
		int cli_queue_max;
	} cfg [COMM_UNIX_SERVER_MAX_LISTERNERS];

} CommEvUNIXServer;

typedef struct _CommEvUNIXServerConn
{
	DLinkedListNode global_node;
	DLinkedListNode listener_node;
	CommEvStatistics statistics;
	CommEvUNIXIOData iodata;

	struct _CommEvUNIXServerListener *listener;
	struct _CommEvUNIXServer *parent_srv;
	struct sockaddr_in conn_addr;

	int socket_fd;
	long user_long;
	int user_int;
	void *user_data;

	struct
	{
		CommEvUNIXGenericCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct timeval last_tv;
		unsigned long last_ts;

		struct
		{
			unsigned int enabled :1;
		} flags;

	} events[COMM_UNIX_SERVER_CONN_EVENT_LASTITEM];

	struct
	{
		int req_scheduled;
		int req_sent_no_ack;
		int req_sent_with_ack;
		int reply_ack;
	} counters;

	struct
	{
		unsigned int socket_eof:1;
		unsigned int close_request:1;
		unsigned int pending_write_request:1;
		unsigned int pending_write_ack:1;
		unsigned int no_brb_proto:1;
	} flags;


} CommEvUNIXServerConn;



/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
typedef enum
{
	COMM_UNIX_CLIENT_EVENT_READ,
	COMM_UNIX_CLIENT_EVENT_CLOSE,
	COMM_UNIX_CLIENT_EVENT_CONNECT,
	COMM_UNIX_CLIENT_EVENT_LASTITEM
} CommEvUNIXClientEventCodes;

typedef enum
{
	COMM_UNIX_CLIENT_STATE_DISCONNECTED,
	COMM_UNIX_CLIENT_STATE_CONNECTING,
	COMM_UNIX_CLIENT_STATE_CONNECTED,
	COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_TIMEOUT,
	COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_REFUSED,
	COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_CONNECT_SYSCALL,
	COMM_UNIX_CLIENT_STATE_CONNECT_FAILED_UNKNWON,
} CommEvUNIXClientStateCodes;

typedef enum
{
	COMM_UNIX_CLIENT_FAILURE_SOCKET,
	COMM_UNIX_CLIENT_FAILURE_REUSEADDR,
	COMM_UNIX_CLIENT_FAILURE_SETNONBLOCKING,
	COMM_UNIX_CLIENT_INIT_OK
} CommEvUNIXClientInitCodes;

typedef struct _CommEvUNIXClientConf
{
	struct _EvDNSResolverBase *resolv_base;
	struct _EvKQBaseLogBase *log_base;
	char *server_path;

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
		unsigned int destroy_after_connect_fail:1;
		unsigned int destroy_after_close:1;
		unsigned int calculate_datarate:1;
		unsigned int autoclose_fd_on_ack:1;
		unsigned int no_brb_proto:1;
	} flags;

} CommEvUNIXClientConf;

typedef struct _CommEvUNIXClient
{
	EvBaseKQObject kq_obj;
	CommEvUNIXIOData iodata;
	CommEvStatistics statistics;
	DLinkedListNode user_node;

	struct _EvKQBase *kq_base;
	struct _EvKQBaseLogBase *log_base;
	struct _CommEvUNIXClientPool *parent_pool;
	struct sockaddr_un servaddr_un;

	char server_path[COMM_UNIX_MAX_PATH_SZ];
	void *user_data;
	long user_long;
	int user_int;
	int socket_fd;
	int socket_state;
	int cli_id_onpool;

	struct
	{
		CommEvUNIXGenericCBH *cb_handler_ptr;
		void *cb_data_ptr;
		struct timeval last_tv;
		unsigned long last_ts;

		struct
		{
			unsigned int enabled:1;
		} flags;

	} events[COMM_UNIX_CLIENT_EVENT_LASTITEM];

	struct
	{
		int req_scheduled;
		int req_sent_no_ack;
		int req_sent_with_ack;
		int reply_ack;
	} counters;

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
		int reconnect_after_timeout_id;
		int reconnect_after_close_id;
		int reconnect_on_fail_id;
		int calculate_datarate_id;
	} timers;

	struct
	{
		unsigned int reconnect_on_timeout:1;
		unsigned int reconnect_on_close:1;
		unsigned int reconnect_on_fail:1;
		unsigned int destroy_after_connect_fail:1;
		unsigned int destroy_after_close:1;
		unsigned int calculate_datarate:1;
		unsigned int autoclose_fd_on_ack:1;
		unsigned int writequeue_init:1;
		unsigned int socket_eof:1;
		unsigned int close_request:1;
		unsigned int pending_write_request:1;
		unsigned int pending_write_ack:1;
		unsigned int no_brb_proto:1;
	} flags;

} CommEvUNIXClient;

typedef struct _CommEvUNIXClientPoolConf
{
	int cli_count_init;
	int cli_count_max;

	struct
	{
		unsigned int no_brb_proto:1;
	} flags;

} CommEvUNIXClientPoolConf;

typedef struct _CommEvUNIXClientPool
{
	CommEvUNIXClientPoolConf pool_conf;
	CommEvUNIXClientConf cli_conf;
	DLinkedListNode user_node;
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

} CommEvUNIXClientPool;

/******************************************************************************************************/
/**/
/**/
/******************************************************************************************************/
/* comm/core/unix/comm_unix_aio.c */
/******************************************************************************************************/
int CommEvUNIXIODataInit(CommEvUNIXIOData *iodata, void *parent_ptr, int parent_fd, int max_sz, int mt_type);
void CommEvUNIXIODataDestroy(CommEvUNIXIOData *iodata);
void CommEvUNIXIODataLock(CommEvUNIXIOData *iodata);
void CommEvUNIXIODataUnlock(CommEvUNIXIOData *iodata);
int CommEvUNIXAutoCloseLocalDescriptors(CommEvUNIXWriteRequest *write_req);
int CommEvUNIXIOControlDataProcess(struct _EvKQBase *ev_base, CommEvUNIXIOData *iodata, CommEvUNIXControlData *control_data_head, int read_bytes);
int CommEvUNIXIOReadRaw(struct _EvKQBase *ev_base, CommEvUNIXIOData *io_data, int socket_fd, int can_read_sz);
int CommEvUNIXIORead(struct _EvKQBase *ev_base, CommEvUNIXIOData *io_data, int socket_fd, int can_read_sz);
int CommEvUNIXIOWriteRaw(struct _EvKQBase *ev_base, CommEvUNIXWriteRequest *write_req, int socket_fd);
int CommEvUNIXIOWrite(struct _EvKQBase *ev_base, CommEvUNIXWriteRequest *write_req, int socket_fd);
int CommEvUNIXIOReplyACK(CommEvUNIXACKReply *ack_reply, int socket_fd);
/******************************************************************************************************/
/* comm/core/unix/comm_unix_server.c */
/******************************************************************************************************/
CommEvUNIXServer *CommEvUNIXServerNew(struct _EvKQBase *kq_base);
void CommEvUNIXServerDestroy(CommEvUNIXServer *srv_ptr);
int CommEvUNIXServerListenerAdd(CommEvUNIXServer *srv_ptr, CommEvUNIXServerConf *server_conf);
int CommEvUNIXServerListenerDel(CommEvUNIXServer *srv_ptr, int listener_id);
void CommEvUNIXServerEventSet(CommEvUNIXServer *srv_ptr, int listener_id, CommEvUNIXServerEventCodes ev_type, CommEvUNIXGenericCBH *cb_handler, void *cb_data);
void CommEvUNIXServerEventCancel(CommEvUNIXServer *srv_ptr, int listener_id, CommEvUNIXServerEventCodes ev_type);
void CommEvUNIXServerEventCancelAll(CommEvUNIXServer *srv_ptr);
int CommEvUNIXServerConnAIOBrbProtoACK(CommEvUNIXServerConn *conn_hnd, int req_id);
int CommEvUNIXServerConnAIORawWriteStr(CommEvUNIXServerConn *conn_hnd, char *data, CommEvUNIXGenericCBH *finish_cb, void *finish_cbdata);
int CommEvUNIXServerConnAIORawWrite(CommEvUNIXServerConn *conn_hnd, char *data, long data_sz, CommEvUNIXGenericCBH *finish_cb, void *finish_cbdata);
int CommEvUNIXServerConnAIOBrbProtoWrite(CommEvUNIXServerConn *conn_hnd, char *data, long data_sz, int *fd_arr, int fd_sz, CommEvUNIXGenericCBH *finish_cb,
		CommEvUNIXACKCBH *ack_cb, void *finish_cbdata);
void CommEvUNIXServerConnCloseRequest(CommEvUNIXServerConn *conn_hnd);
void CommEvUNIXServerConnClose(CommEvUNIXServerConn *conn_hnd);
void CommEvUNIXServerConnEventSetDefault(CommEvUNIXServer *srv_ptr, CommEvUNIXServerConn *conn_hnd);
void CommEvUNIXServerConnEventSet(CommEvUNIXServerConn *conn_hnd, CommEvUNIXServerConnEventCodes ev_type, CommEvUNIXGenericCBH *cb_handler, void *cb_data);
void CommEvUNIXServerConnEventCancel(CommEvUNIXServerConn *conn_hnd, CommEvUNIXServerConnEventCodes ev_type);
void CommEvUNIXServerConnEventCancelAll(CommEvUNIXServerConn *conn_hnd);

/******************************************************************************************************/
/* comm/core/unix/comm_unix_client.c */
/******************************************************************************************************/
CommEvUNIXClient *CommEvUNIXClientNew(struct _EvKQBase *kq_base);
int CommEvUNIXClientInit(struct _EvKQBase *kq_base, CommEvUNIXClient *ev_unixclient, int socket_fd, int cli_id_onpool);
void CommEvUNIXClientClean(CommEvUNIXClient *ev_unixclient);
void CommEvUNIXClientDestroy(CommEvUNIXClient *ev_unixclient);
int CommEvUNIXClientConnect(CommEvUNIXClient *ev_unixclient, CommEvUNIXClientConf *ev_unixclient_conf);
void CommEvUNIXClientResetFD(CommEvUNIXClient *ev_unixclient);
void CommEvUNIXClientDisconnectRequest(CommEvUNIXClient *ev_unixclient);
void CommEvUNIXClientDisconnect(CommEvUNIXClient *ev_unixclient);
void CommEvUNIXClientIODataDestroy(CommEvUNIXClient *ev_unixclient);
void CommEvUNIXClientConnCloseRequest(CommEvUNIXClient *ev_unixclient);
void CommEvUNIXClientConnClose(CommEvUNIXClient *ev_unixclient);
int CommEvUNIXClientEventIsSet(CommEvUNIXClient *ev_unixclient, CommEvUNIXClientEventCodes ev_type);
void CommEvUNIXClientEventSet(CommEvUNIXClient *ev_unixclient, CommEvUNIXClientEventCodes ev_type, CommEvUNIXGenericCBH *cb_handler, void *cb_data);
void CommEvUNIXClientEventCancel(CommEvUNIXClient *ev_unixclient, CommEvUNIXClientEventCodes ev_type);
void CommEvUNIXClientEventCancelAll(CommEvUNIXClient *ev_unixclient);
int CommEvUNIXClientAIOBrbProtoACK(CommEvUNIXClient *ev_unixclient, int req_id);
int CommEvUNIXClientAIORawWriteStr(CommEvUNIXClient *ev_unixclient, char *data, CommEvUNIXGenericCBH *finish_cb, void *finish_cbdata);
int CommEvUNIXClientAIORawWrite(CommEvUNIXClient *ev_unixclient, char *data, long data_sz, CommEvUNIXGenericCBH *finish_cb, void *finish_cbdata);
int CommEvUNIXClientAIOBrbProtoWrite(CommEvUNIXClient *ev_unixclient, char *data, long data_sz, int *fd_arr, int fd_sz, CommEvUNIXGenericCBH *finish_cb,
		CommEvUNIXACKCBH *ack_cb, void *finish_cbdata, void *ack_cbdata);

/******************************************************************************************************/
/* comm/core/unix/comm_unix_client_pool.c */
/******************************************************************************************************/
CommEvUNIXClientPool *CommEvUNIXClientPoolNew(struct _EvKQBase *kq_base, CommEvUNIXClientPoolConf *unix_clientpool_conf);
int CommEvUNIXClientPoolDestroy(CommEvUNIXClientPool *unix_clientpool);
int CommEvUNIXClientPoolConnect(CommEvUNIXClientPool *unix_clientpool, CommEvUNIXClientConf *ev_unixclient_conf);
CommEvUNIXClient *CommEvUNIXClientPoolClientSelect(CommEvUNIXClientPool *unix_clientpool, int select_method, int select_connected);
CommEvUNIXClient *CommEvUNIXClientPoolClientSelectLeastLoad(CommEvUNIXClientPool *unix_clientpool, int select_connected);
CommEvUNIXClient *CommEvUNIXClientPoolClientSelectRoundRobin(CommEvUNIXClientPool *unix_clientpool, int select_connected);
int CommEvUNIXClientPoolHasConnected(CommEvUNIXClientPool *unix_clientpool);
int CommEvUNIXClientPoolEventSet(CommEvUNIXClientPool *unix_clientpool, CommEvUNIXClientEventCodes ev_type, CommEvUNIXGenericCBH *cb_handler, void *cb_data);
int CommEvUNIXClientPoolEventCancel(CommEvUNIXClientPool *unix_clientpool, CommEvUNIXClientEventCodes ev_type);
int CommEvUNIXClientPoolEventCancelAll(CommEvUNIXClientPool *unix_clientpool);
int CommEvUNIXClientPoolAIOWrite(CommEvUNIXClientPool *unix_clientpool, int select_method, char *data, long data_sz, int *fd_arr, int fd_sz,
		CommEvUNIXGenericCBH *finish_cb, CommEvUNIXACKCBH *ack_cb, void *finish_cbdata, void *ack_cbdata);


#endif /* LIBBRB_COMM_UNIX_H_ */
