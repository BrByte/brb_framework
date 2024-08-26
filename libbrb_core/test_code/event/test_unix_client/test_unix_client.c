/*
 * test_unix_client.c
 *
 *  Created on: 2014-07-20
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

#include <libbrb_core.h>

EvKQBase *glob_ev_base;
CommEvUNIXClient *glob_unix_client;
EvKQBaseLogBase *glob_log_base;
int glob_tick_count = 0;

static EvBaseKQCBH UNIXSendRequestTimer;
static CommEvUNIXGenericCBH UNIXEventsReadEvent;
static CommEvUNIXGenericCBH UNIXEventsConnectEvent;
static CommEvUNIXGenericCBH UNIXEventsCloseEvent;
static CommEvUNIXACKCBH UNIXEventsACKEvent;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	CommEvUNIXClientConf client_conf;
	EvKQBaseLogBaseConf log_conf;
	int op_status;

	chdir("/brb_main/coredumps");

	/* Clean stack space */
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
	memset(&client_conf, 0, sizeof(CommEvUNIXClientConf));
	//log_conf.fileout_pathstr				= "./unix_client.log";
	log_conf.flags.double_write				= 1;

	/* Create event base */
	glob_ev_base		= EvKQBaseNew(NULL);
	glob_unix_client	= CommEvUNIXClientNew(glob_ev_base);
	glob_log_base		= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);

	glob_unix_client->log_base = glob_log_base;

	/* Set flags */
	client_conf.flags.reconnect_on_timeout	= 1;
	client_conf.flags.reconnect_on_close	= 1;
	client_conf.flags.reconnect_on_fail		= 1;
	client_conf.flags.autoclose_fd_on_ack	= 1;
	client_conf.flags.no_brb_proto			= 1;

	client_conf.retry_times.reconnect_after_timeout_ms	= 1000;
	client_conf.retry_times.reconnect_after_close_ms	= 1000;
	client_conf.retry_times.reconnect_on_fail_ms		= 1000;

	/* Set timeout information */
	client_conf.timeout.connect_ms						= 1000;
	client_conf.server_path = "/brb_main/tmp/unix_listener00";

	CommEvUNIXClientEventSet(glob_unix_client, COMM_UNIX_CLIENT_EVENT_CONNECT, UNIXEventsConnectEvent, glob_unix_client);
	CommEvUNIXClientEventSet(glob_unix_client, COMM_UNIX_CLIENT_EVENT_CLOSE, UNIXEventsCloseEvent, glob_unix_client);
	CommEvUNIXClientEventSet(glob_unix_client, COMM_UNIX_CLIENT_EVENT_READ, UNIXEventsReadEvent, glob_unix_client);

	op_status = CommEvUNIXClientConnect(glob_unix_client, &client_conf);

	if (!op_status)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Connection to [%s] failed\n", client_conf.server_path);
		exit(0);
	}

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 5);

	exit(0);
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int UNIXSendRequestTimer(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *unix_client = cb_data;
	char data[64];
	int write_slot_id;
	int iter_sz;
	int err_count;
	int total_sz;
	int i;

	int fd_arr[8];

	memset(&data, 0xAA, sizeof(data));

	printf("UNIXSendRequestTimer - FD [%d]\n", fd);

//	for (total_sz = 0, err_count = 0, i = 0; i < 512; i++)
//	{
//		iter_sz = abs((arc4random() % (sizeof(data) - 1)));
//		//iter_sz = 32;
//
//		fd_arr[0] = EvKQBaseSocketTCPNew(glob_ev_base);
//
//		if (fd_arr[0] < 0)
//		{
//			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed creating FD [%d] with ERRNO [%d]\n", fd_arr[0], errno);
//			break;
//		}
//
//		fd_arr[1] = EvKQBaseSocketTCPNew(glob_ev_base);
//
//		if (fd_arr[1] < 0)
//		{
//			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed creating FD [%d] with ERRNO [%d]\n", fd_arr[1], errno);
//			close(fd_arr[0]);
//			break;
//		}
//		fd_arr[2] = EvKQBaseSocketTCPNew(glob_ev_base);
//
//		if (fd_arr[2] < 0)
//		{
//			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed creating FD [%d] with ERRNO [%d]\n", fd_arr[2], errno);
//
//			close(fd_arr[0]);
//			close(fd_arr[1]);
//
//			break;
//		}
//
//		/* Write data */
//		write_slot_id = CommEvUNIXClientAIOWrite(unix_client, (char*)&data, (iter_sz > 1 ? iter_sz : 1), (int*)&fd_arr, 3, NULL, UNIXEventsACKEvent, NULL);
//
//		if (write_slot_id < 0)
//		{
//			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FAILED WRITING [%d] FDs with DATA_SZ [%d]\n", 3, iter_sz);
//
//			err_count++;
//
//			close(fd_arr[0]);
//			close(fd_arr[1]);
//			close(fd_arr[2]);
//			continue;
//		}
//
//		total_sz += iter_sz;
//		//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "[%d] - FD [%d] - REQ_ID [%d] - WROTE [%d] - PENDING [%d] - START_FD [%d]\n",
//		//		i, fd, write_slot_id, iter_sz, unix_client->counters.req_sent_with_ack - unix_client->counters.reply_ack, fd_arr[0]);
//
//		continue;
//	}
//
//	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FINISHED WRITING [%d] FDs with [%d] bytes of payload - [%d] ERRORs\n", i, total_sz, err_count);
//
//	/* Schedule timer for next request */
//	if (glob_tick_count++ < 8092)
		//EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 5000, UNIXSendRequestTimer, unix_client);
//	else
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "STOP AT [%d]\n", glob_tick_count);

	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsReadEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	int i;

	CommEvUNIXClient *unix_client 	= cb_data;
	CommEvUNIXIOData *io_data		= &unix_client->iodata;
	char *data_str					= MemBufferDeref(io_data->read.data_mb);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SIZE [%d] - [%s]\n", fd, to_read_sz, (data_str ? data_str : "NULL"));

//	for (i = 0; i < io_data->read.fd_arr.sz; i++)
//	{
//		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "CLIENT - FD [%d] - Will close received FD [%d]\n", fd, io_data->read.fd_arr.data[i]);
//
////		if (io_data->read.fd_arr.data[i] >= 0)
////		{
//			assert(io_data->read.fd_arr.data[i] >= 0);
//			close(io_data->read.fd_arr.data[i]);
////		}
//
//		continue;
//	}

	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsConnectEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *unix_client = cb_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SIZE [%d]\n", fd, to_read_sz);

	if (COMM_CLIENT_STATE_CONNECTED == unix_client->socket_state)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "Connected to [%s] sending request\n", unix_client->server_path);
		CommEvUNIXClientAIORawWriteStr(unix_client, "aaaaaaaaaaaaaaaaaaaa\n", NULL, NULL);

		/* Schedule timer for next request */
		//EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 50, UNIXSendRequestTimer, unix_client);
	}
	else
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] FAILED connecting to [%s] - STATE [%d]\n", fd,  unix_client->server_path, unix_client->socket_state);
	}



	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsCloseEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *unix_client = cb_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - SIZE [%d]\n", fd, to_read_sz);

	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsACKEvent(int ack_code, void *pend_req_ptr, void *cb_data)
{
	int i;
	CommEvUNIXWriteRequest *write_req	= pend_req_ptr;
	CommEvUNIXClient *unix_client		= write_req->parent_ptr;

	//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "CLIENT - PENDING [%d] -  ACK for REQ_ID [%d] - [%d] FDs\n",
	//		(unix_client->counters.req_sent_with_ack - unix_client->counters.reply_ack), write_req->req_id, write_req->fd_arr.sz);

//	for (i = 0; i < write_req->fd_arr.sz; i++)
//	{
//		assert(write_req->fd_arr.data[i] > 0);
//		close(write_req->fd_arr.data[i]);
//		continue;
//	}

	return 1;
}
/**************************************************************************************************************************/
