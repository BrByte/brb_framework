/*
 * test_unix_server.c
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
CommEvUNIXServer *glob_unix_server;
EvKQBaseLogBase *glob_log_base;
int glob_tick_count = 0;

static EvBaseKQCBH UNIXSendRequestTimer;
static CommEvUNIXGenericCBH UNIXEventsReadEvent;
static CommEvUNIXGenericCBH UNIXEventsAcceptEvent;
static CommEvUNIXGenericCBH UNIXEventsCloseEvent;
static CommEvUNIXACKCBH UNIXEventsACKEvent;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	CommEvUNIXServerConf server_conf;
	EvKQBaseLogBaseConf log_conf;
	int listener_id;

	/* Clean up stack */
	memset(&server_conf, 0, sizeof(CommEvUNIXServerConf));
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));
//	log_conf.fileout_pathstr				= "./unix_server.log";
	log_conf.flags.double_write				= 1;

	server_conf.path_str = "/brb_main/tmp/unix_listener00";
	server_conf.flags.autoclose_fd_on_ack = 1;
	server_conf.flags.no_brb_proto = 1;

	/* Create event base */
	glob_ev_base		= EvKQBaseNew(NULL);
	glob_unix_server	= CommEvUNIXServerNew(glob_ev_base);
	glob_log_base		= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);
	listener_id			= CommEvUNIXServerListenerAdd(glob_unix_server, &server_conf);
//	glob_log_base->log_level = LOGTYPE_WARNING;

	glob_unix_server->log_base = glob_log_base;

	printf("UNIX server [%s] INIT with ID [%d] at [%s]\n", (listener_id < 0 ? "FAILED" : "SUCCESS"), listener_id, server_conf.path_str);

	if (listener_id > -1)
	{
		CommEvUNIXServerEventSet(glob_unix_server, listener_id, COMM_UNIX_SERVER_EVENT_READ, UNIXEventsReadEvent, NULL);
		CommEvUNIXServerEventSet(glob_unix_server, listener_id, COMM_UNIX_SERVER_EVENT_ACCEPT, UNIXEventsAcceptEvent, NULL);
		CommEvUNIXServerEventSet(glob_unix_server, listener_id, COMM_UNIX_SERVER_EVENT_CLOSE, UNIXEventsCloseEvent, NULL);
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
	CommEvUNIXServerConn *conn_hnd	 = cb_data;
	char data[512];
	char *data_ptr = (char *)&data;
	int write_slot_id;
	int iter_sz;
	int err_count;
	int total_sz;
	int i;

	int fd_arr[8];

	memset(&data, 0xAA, sizeof(data));

//	memset(&data, 0xBB, 196550);

	for (total_sz = 0, err_count = 0, i = 0; i < 512; i++)
	{
		iter_sz = abs((arc4random() % (sizeof(data) - 1)));
//		iter_sz = 16384;

		fd_arr[0] = EvKQBaseSocketTCPNew(glob_ev_base);

		if (fd_arr[0] < 0)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed creating FD [%d] with ERRNO [%d]\n", fd_arr[0], errno);
			break;
		}

		fd_arr[1] = EvKQBaseSocketTCPNew(glob_ev_base);

		if (fd_arr[1] < 0)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed creating FD [%d] with ERRNO [%d]\n", fd_arr[1], errno);
			close(fd_arr[0]);
			break;
		}
		fd_arr[2] = EvKQBaseSocketTCPNew(glob_ev_base);

		if (fd_arr[2] < 0)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "Failed creating FD [%d] with ERRNO [%d]\n", fd_arr[2], errno);

			close(fd_arr[0]);
			close(fd_arr[1]);
			break;
		}


		/* Write data */
		write_slot_id = CommEvUNIXServerConnAIOBrbProtoWrite(conn_hnd, (char*)&data, (iter_sz > 1 ? iter_sz : 1), (int*)&fd_arr, 3, NULL, UNIXEventsACKEvent, NULL);

		if (write_slot_id < 0)
		{
			KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_WARNING, LOGCOLOR_RED, "FAILED WRITING [%d] FDs with DATA_SZ [%d] - ERR [%d]\n", 3, iter_sz, write_slot_id);

			err_count++;

			close(fd_arr[0]);
			close(fd_arr[1]);
			close(fd_arr[2]);
		}

		//printf("UNIXSendRequestTimer - [%d] - FD [%d] - REQ_ID [%d] - WROTE [%d]\n", i, fd, write_slot_id, iter_sz);
		total_sz += iter_sz;
		continue;
	}

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "TICK [%d] - FINISHED WRITING [%d] FDs with [%d] bytes of payload - [%d] ERRORs\n",
			glob_tick_count, i, total_sz, err_count);

	if (glob_tick_count++ < 1024)
		EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 1000, UNIXSendRequestTimer, conn_hnd);
	else
	{
		KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_RED, "STOP AT [%d]\n", glob_tick_count);
	}

	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsReadEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	int i;

	CommEvUNIXServer *srv_ptr			= base_ptr;
	EvKQBase *ev_base					= srv_ptr->kq_base;
	CommEvUNIXServerConn *conn_hnd		= MemArenaGrabByID(srv_ptr->conn.arena, fd);
	CommEvUNIXIOData *io_data			= &conn_hnd->iodata;
	CommEvUNIXServerListener *listener	= conn_hnd->listener;
	char *data_str						= MemBufferDeref(conn_hnd->iodata.read.data_mb);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "FD [%d] - SIZE [%d] - CLOSED [%d] FDs -> [%s]\n",
			fd, to_read_sz, io_data->read.fd_arr.sz, data_str);


	//	for (i = 0; i < io_data->read.fd_arr.sz; i++)
//	{
//		//KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Will close received FD [%d]\n", fd, io_data->read.fd_arr.data[i]);
//		assert(io_data->read.fd_arr.data[i] > 0);
//		close(io_data->read.fd_arr.data[i]);
//		continue;
//	}

	MemBufferClean(conn_hnd->iodata.read.data_mb);
	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsAcceptEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvUNIXServer *srv_ptr			= base_ptr;
	EvKQBase *ev_base					= srv_ptr->kq_base;
	CommEvUNIXServerConn *conn_hnd		= MemArenaGrabByID(srv_ptr->conn.arena, fd);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SIZE [%d]\n", fd, to_read_sz);
	CommEvUNIXServerConnAIORawWriteStr(conn_hnd, "oiiiii\n", NULL, NULL);

	/* Schedule timer for next request */
	//EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 75, UNIXSendRequestTimer, conn_hnd);
	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsCloseEvent(int fd, int to_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - SIZE [%d]\n", fd, to_read_sz);

	return 1;
}
/**************************************************************************************************************************/
static int UNIXEventsACKEvent(int ack_code, void *pend_req_ptr, void *cb_data)
{
	int i;
	CommEvUNIXWriteRequest *write_req	= pend_req_ptr;
	CommEvUNIXServerConn *conn_hnd		= write_req->parent_ptr;

//	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_INFO, LOGCOLOR_PURPLE, "PENDING [%d] - ACK for REQ_ID [%d] - [%d] FDs\n",
//			(conn_hnd->counters.req_sent_with_ack - conn_hnd->counters.reply_ack), write_req->req_id, write_req->fd_arr.sz);
//
//	for (i = 0; i < write_req->fd_arr.sz; i++)
//	{
//		assert(write_req->fd_arr.data[i] > 0);
//		close(write_req->fd_arr.data[i]);
//		continue;
//	}

	return 1;
}
/**************************************************************************************************************************/
