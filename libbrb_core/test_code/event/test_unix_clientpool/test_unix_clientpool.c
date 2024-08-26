/*
 * test_unix_client_pool.c
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
CommEvUNIXClientPool *glob_unix_client_pool;
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
	CommEvUNIXClientPoolConf client_conf_pool;
	CommEvUNIXClientConf client_conf;
	EvKQBaseLogBaseConf log_conf;
	int op_status;

	chdir("/brb_main/coredumps");

	/* Clean stack space */
	memset(&client_conf_pool, 0, sizeof(CommEvUNIXClientPoolConf));
	memset(&client_conf, 0, sizeof(CommEvUNIXClientConf));
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));

	client_conf_pool.cli_count_init			= 1;
	client_conf_pool.cli_count_max			= 1;

	//log_conf.fileout_pathstr				= "./unix_clientpool.log";
	log_conf.flags.double_write				= 1;

	/* Create event base */
	glob_ev_base							= EvKQBaseNew(NULL);
	glob_log_base							= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	glob_unix_client_pool					= CommEvUNIXClientPoolNew(glob_ev_base, &client_conf_pool);
	//glob_unix_client_pool->log_base			= glob_log_base;

	if (glob_unix_client_pool->flags.has_error)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed creating unix client pool with [%d] init and [%d] max clients\n",
				client_conf_pool.cli_count_init, client_conf_pool.cli_count_max);
		exit(0);
	}


	/* Set flags */
	client_conf.flags.reconnect_on_timeout				= 1;
	client_conf.flags.reconnect_on_close				= 1;
	client_conf.flags.reconnect_on_fail					= 1;
	client_conf.flags.autoclose_fd_on_ack				= 1;

	client_conf.retry_times.reconnect_after_timeout_ms	= 1000;
	client_conf.retry_times.reconnect_after_close_ms	= 1000;
	client_conf.retry_times.reconnect_on_fail_ms		= 1000;

	/* Set timeout information */
	client_conf.timeout.connect_ms						= 1000;

	client_conf.server_path = "/brb_main/tmp/unix_listener00";
	//client_conf.log_base	= glob_log_base;

	/* NULL on PoolEventSet will set SELF as CBDATA */
	CommEvUNIXClientPoolEventSet(glob_unix_client_pool, COMM_UNIX_CLIENT_EVENT_CONNECT, UNIXEventsConnectEvent, NULL);
	CommEvUNIXClientPoolEventSet(glob_unix_client_pool, COMM_UNIX_CLIENT_EVENT_CLOSE, UNIXEventsCloseEvent, NULL);
	CommEvUNIXClientPoolEventSet(glob_unix_client_pool, COMM_UNIX_CLIENT_EVENT_READ, UNIXEventsReadEvent, NULL);

	op_status = CommEvUNIXClientPoolConnect(glob_unix_client_pool, &client_conf);

	if (!op_status)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Connection to [%s] failed\n", client_conf.server_path);
		exit(0);
	}

	EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2000, UNIXSendRequestTimer, glob_unix_client_pool);

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
	CommEvUNIXClientPool *unix_client_pool	= cb_data;

	char data[128];
	int write_slot_id;
	int iter_sz;
	int i;

	int fd_arr[8];

	memset(&data, 0, sizeof(data));

	for (i = 0; i < 8092; i++)
	{
		iter_sz = abs((arc4random() % (sizeof(data) - 1)));

		fd_arr[0] = EvKQBaseSocketTCPNew(glob_ev_base);

		if (fd_arr[0] < 0)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FAILED CREATING FDs at STEP [%d]\n", 1);
			break;
		}

		fd_arr[1] = EvKQBaseSocketTCPNew(glob_ev_base);

		if (fd_arr[1] < 0)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FAILED CREATING FDs at STEP [%d]\n", 2);

			close(fd_arr[0]);
			break;
		}
		fd_arr[2] = EvKQBaseSocketTCPNew(glob_ev_base);

		if (fd_arr[2] < 0)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FAILED CREATING FDs at STEP [%d]\n", 3);

			close(fd_arr[0]);
			close(fd_arr[1]);

			break;
		}

		/* Write data */
		write_slot_id = CommEvUNIXClientPoolAIOWrite(unix_client_pool, COMM_UNIX_SELECT_LEAST_LOAD, (char*)&data, iter_sz, (int*)&fd_arr, 3, NULL, UNIXEventsACKEvent, NULL, NULL);

		if (write_slot_id < 0)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FAILED WRITING [%d] FDs with DATA_SZ [%d]\n", 3, iter_sz);

			close(fd_arr[0]);
			close(fd_arr[1]);
			close(fd_arr[2]);
			break;
		}

		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "[%d] - FD [%d] - REQ_ID [%d] - WROTE [%d] - START_FD [%d]\n",
				i, fd, write_slot_id, iter_sz, /*unix_client->counters.req_sent_with_ack - unix_client->counters.reply_ack*/ fd_arr[0]);

		continue;
	}

	/* Schedule timer for next request */
	if (glob_tick_count++ < 8092)
		EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 100, UNIXSendRequestTimer, unix_client_pool);
	else
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "STOP AT [%d]\n", glob_tick_count);

	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsReadEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	int i;

	CommEvUNIXClient *unix_client = cb_data;
	CommEvUNIXIOData *io_data		= &unix_client->iodata;

	//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLI_ID [%d] - SIZE [%d] - CLOSED [%d] FDs\n", fd, unix_client->cli_id_onpool, to_read_sz, io_data->read.fd_arr.sz);

	for (i = 0; i < io_data->read.fd_arr.sz; i++)
	{
		//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLI_ID [%d] - Will close received FD [%d]\n", fd, unix_client->cli_id_onpool, io_data->read.fd_arr.data[i]);
		assert(io_data->read.fd_arr.data[i] > 0);
		close(io_data->read.fd_arr.data[i]);
		continue;
	}


	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsConnectEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *unix_client = cb_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SIZE [%d]\n", fd, to_read_sz);

	if (COMM_CLIENT_STATE_CONNECTED == unix_client->socket_state)
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - CLI_ID [%d] - Connected to [%s] sending request\n",
				fd, unix_client->cli_id_onpool, unix_client->server_path);

	}
	else
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FD [%d] -  CLI_ID [%d] - FAILED connecting to [%s] - STATE [%d]\n",
				fd,  unix_client->cli_id_onpool, unix_client->server_path, unix_client->socket_state);
	}



	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsCloseEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXClient *unix_client = cb_data;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SIZE [%d]\n", fd, to_read_sz);

	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsACKEvent(int ack_code, void *pend_req_ptr, void *cb_data)
{
	int i;
	CommEvUNIXWriteRequest *write_req	= pend_req_ptr;
	CommEvUNIXClient *unix_client		= write_req->parent_ptr;

	//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - CLIENT [%d] - PENDING [%d]\n", unix_client->socket_fd, unix_client->cli_id_onpool,
	//		(unix_client->counters.req_sent_with_ack - unix_client->counters.reply_ack));

//	for (i = 0; i < write_req->fd_arr.sz; i++)
//	{
//		assert(write_req->fd_arr.data[i] > 0);
//		close(write_req->fd_arr.data[i]);
//		continue;
//	}

	return 1;
}
/**************************************************************************************************************************/

