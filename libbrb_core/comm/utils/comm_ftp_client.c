/*
 * comm_ftp_client.c
 *
 *  Created on: 2013-12-10
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

/* Private internal TCP events */
static CommEvTCPClientCBH CommEvFTPClientCTRLEventRead;
static CommEvTCPClientCBH CommEvFTPClientCTRLEventConnect;
static CommEvTCPClientCBH CommEvFTPClientCTRLEventClose;

static CommEvTCPClientCBH CommEvFTPClientRawDataEventConnect;
static CommEvTCPClientCBH CommEvFTPClientRawDataEventClose;
static CommEvTCPClientCBH CommEvFTPClientRawDataEventRead;
static CommEvTCPClientCBH CommEvFTPClientRawDataEventWriteMemBufferFinish;

static CommEvFTPClientServerActionCBH CommEvFTPClientAction150_FileOK;
static CommEvFTPClientServerActionCBH CommEvFTPClientAction220_Ready;

static CommEvFTPClientServerActionCBH CommEvFTPClientAction226_FileReceivedOK;
static CommEvFTPClientServerActionCBH CommEvFTPClientAction227_Passive;
static CommEvFTPClientServerActionCBH CommEvFTPClientAction230_LoggedIn;
static CommEvFTPClientServerActionCBH CommEvFTPClientAction250_CmdOk;
static CommEvFTPClientServerActionCBH CommEvFTPClientAction331_SendPass;
static CommEvFTPClientServerActionCBH CommEvFTPClientAction550_CmdFail;
static CommEvFTPClientServerActionCBH CommEvFTPClientAction553_FileFail;


static int CommEvFTPClientActionDispatch(CommEvFTPClient *ev_ftp_client, int action_code);
static int CommEvFTPClientActionCancel(CommEvFTPClient *ev_ftp_client, int action_code);
static int CommEvFTPClientActionRegister(CommEvFTPClient *ev_ftp_client, int action_code, CommEvFTPClientServerActionCBH *cb_handler, void *cb_data);
static int CommEvFTPClientLoadConfFromProto(CommEvFTPClientConf *ev_ftp_conf, CommEvFTPClientConfProto *ev_ftp_conf_proto);

static int CommEvFTPClientEventDispatch(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type);
static int CommEvFTPClientParse227_PassiveReply(CommEvFTPClient *ev_ftp_client, char *data_str_ptr, int data_str_sz);

/**************************************************************************************************************************/
int CommEvFTPClientRetrMemBuffer(CommEvFTPClient *ev_ftp_client, MemBuffer *raw_data, char *dst_path, CommEvFTPClientReplyCBH *finish_cb, void *finish_cbdata)
{
	/* Sanity check */
	if (ev_ftp_client->flags.busy)
		return 0;

	if (ev_ftp_client->current_state != EV_FTP_STATE_ONLINE)
		return 0;

	/* Grab reference to data */
	ev_ftp_client->queued_transfer.cb_handler_ptr	= finish_cb;
	ev_ftp_client->queued_transfer.cb_data_ptr 		= finish_cbdata;
	ev_ftp_client->queued_transfer.raw_data			= raw_data;
	ev_ftp_client->queued_transfer.direction		= EV_FTP_TRANSFER_RECEIVE_MEM;
	ev_ftp_client->flags.queued_transfer			= 1;

	/* Listen for file failure and success events */
	CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_FILE_OK, CommEvFTPClientAction150_FileOK, NULL);
	CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_FILE_FAIL, CommEvFTPClientAction553_FileFail, NULL);

	/* Send the STOR command */
	CommEvFTPClientCmdSend(ev_ftp_client, NULL, NULL, EV_FTPCLI_CMD_RETR, dst_path);

	/* SET FLAGS */
	ev_ftp_client->cmd.cmd_sent = EV_FTPCLI_CMD_RETR;
	ev_ftp_client->flags.busy = 1;

	return 1;
}
/**************************************************************************************************************************/
int CommEvFTPClientPutMemBuffer(CommEvFTPClient *ev_ftp_client, MemBuffer *raw_data, char *dst_path, CommEvFTPClientReplyCBH *finish_cb, void *finish_cbdata)
{
	/* Sanity check */
	if (ev_ftp_client->flags.busy)
		return 0;

	if (ev_ftp_client->current_state != EV_FTP_STATE_ONLINE)
		return 0;

	/* Grab reference to data */
	ev_ftp_client->queued_transfer.cb_handler_ptr	= finish_cb;
	ev_ftp_client->queued_transfer.cb_data_ptr 		= finish_cbdata;
	ev_ftp_client->queued_transfer.raw_data			= raw_data;
	ev_ftp_client->queued_transfer.direction		= EV_FTP_TRANSFER_SEND;
	ev_ftp_client->flags.queued_transfer			= 1;

	/* Listen for file failure and success events */
	CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_FILE_OK, CommEvFTPClientAction150_FileOK, NULL);
	CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_FILE_FAIL, CommEvFTPClientAction553_FileFail, NULL);

	/* Send the STOR command */
	CommEvFTPClientCmdSend(ev_ftp_client, NULL, NULL, EV_FTPCLI_CMD_STOR, dst_path);

	/* SET FLAGS */
	ev_ftp_client->cmd.cmd_sent = EV_FTPCLI_CMD_STOR;
	ev_ftp_client->flags.busy = 1;

	return 1;
}
/**************************************************************************************************************************/
int CommEvFTPClientCmdSend(CommEvFTPClient *ev_ftp_client, CommEvFTPClientReplyCBH *cb_handler, void *cb_data, int cmd, char *param_str)
{

	/* Sanity check */
	if (ev_ftp_client->flags.busy)
		return 0;

	if (ev_ftp_client->current_state != EV_FTP_STATE_ONLINE)
		return 0;

	if (cmd >= EV_FTPCLI_CMD_LASTITEM)
		return 0;

	if (ev_ftp_usercmd_arr[cmd].param_count > 0 && !param_str)
		return 0;

	/* Send CMD */
	if (ev_ftp_usercmd_arr[cmd].param_count > 0)
		CommEvTCPClientAIOWriteStringFmt(ev_ftp_client->ev_tcpclient_ctrl, NULL, NULL, "%s %s\r\n", ev_ftp_usercmd_arr[cmd].cmd_str, param_str);
	else
		CommEvTCPClientAIOWriteStringFmt(ev_ftp_client->ev_tcpclient_ctrl, NULL, NULL, "%s\r\n", ev_ftp_usercmd_arr[cmd].cmd_str);

	/* Copy notification call_back data from user */
	ev_ftp_client->cmd.cb_handler_ptr = cb_handler;
	ev_ftp_client->cmd.cb_data_ptr = cb_data;

	/* SET FLAGS */
	ev_ftp_client->cmd.cmd_sent = cmd;
	ev_ftp_client->flags.busy = 1;

	//printf("CommEvFTPClientCmdSend - Sent command [%s %s]\n", ev_ftp_usercmd_arr[cmd].cmd_str, param_str);

	return 1;
}

/**************************************************************************************************************************/
int CommEvFTPClientEventDisable(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_FTPCLIENT_EVENT_LASTITEM)
		return 0;

	ev_ftp_client->events[ev_type].flags.enabled = 0;

	return 1;
}
/**************************************************************************************************************************/
int CommEvFTPClientEventEnable(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_FTPCLIENT_EVENT_LASTITEM)
		return 0;

	ev_ftp_client->events[ev_type].flags.enabled = 1;

	return 1;
}
/**************************************************************************************************************************/
int CommEvFTPClientEventCancelAll(CommEvFTPClient *ev_ftp_client)
{
	int i;

	/* Cancel all events we are aware off */
	for (i = 0; i < COMM_FTPCLIENT_EVENT_LASTITEM; i++)
		CommEvFTPClientEventCancel(ev_ftp_client, i);

	return 1;
}
/**************************************************************************************************************************/
int CommEvFTPClientEventCancel(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_FTPCLIENT_EVENT_LASTITEM)
		return 0;

	/* Fill in data */
	ev_ftp_client->events[ev_type].cb_handler_ptr = NULL;
	ev_ftp_client->events[ev_type].cb_data_ptr = NULL;
	ev_ftp_client->events[ev_type].flags.enabled = 0;

	return 1;
}
/**************************************************************************************************************************/
int CommEvFTPClientEventSet(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type, CommEvFTPClientCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= COMM_FTPCLIENT_EVENT_LASTITEM)
		return 0;

	/* Fill in data */
	ev_ftp_client->events[ev_type].cb_handler_ptr = cb_handler;
	ev_ftp_client->events[ev_type].cb_data_ptr = cb_data;
	ev_ftp_client->events[ev_type].flags.enabled = 1;

	return 1;
}
/**************************************************************************************************************************/
int CommEvFTPClientConnect(CommEvFTPClient *ev_ftp_client, CommEvFTPClientConfProto *ev_ftpclient_conf)
{
	CommEvTCPClientConf tcpclient_conf;
	int op_status;

	memset(&tcpclient_conf, 0, sizeof(CommEvTCPClientConf));

	/* Load configuration for TCP client */
	tcpclient_conf.hostname = ev_ftpclient_conf->host;
	tcpclient_conf.port		= ev_ftpclient_conf->port;

	/* Load CONFIG from prototype */
	CommEvFTPClientLoadConfFromProto(&ev_ftp_client->cli_conf, ev_ftpclient_conf);

	/* Connect TCP client side of this FTP client */
	op_status 	= CommEvTCPClientConnect(ev_ftp_client->ev_tcpclient_ctrl, &tcpclient_conf);

	return op_status;
}
/**************************************************************************************************************************/
CommEvFTPClient *CommEvFTPClientNew(EvKQBase *ev_base)
{
	CommEvFTPClient *ev_ftp_client;

	/* Create a new FTP client */
	ev_ftp_client = calloc(1, sizeof(CommEvFTPClient));

	/* Save a reference to parent base */
	ev_ftp_client->parent_ev_base = ev_base;

	/* Initialize child TCP client inside FTP client instance */
	ev_ftp_client->ev_tcpclient_ctrl = CommEvTCPClientNew(ev_base);
	ev_ftp_client->ev_tcpclient_data = CommEvTCPClientNew(ev_base);

	/* Set local events for CTRL client */
	CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_ctrl, COMM_CLIENT_EVENT_CONNECT, CommEvFTPClientCTRLEventConnect, ev_ftp_client);
	CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_ctrl, COMM_CLIENT_EVENT_CLOSE, CommEvFTPClientCTRLEventClose, ev_ftp_client);
	CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_ctrl, COMM_CLIENT_EVENT_READ, CommEvFTPClientCTRLEventRead, ev_ftp_client);

	/* Set local events for DATA client */
	CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_data, COMM_CLIENT_EVENT_CONNECT, CommEvFTPClientRawDataEventConnect, ev_ftp_client);
	CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_data, COMM_CLIENT_EVENT_CLOSE, CommEvFTPClientRawDataEventClose, ev_ftp_client);
	CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_data, COMM_CLIENT_EVENT_READ, CommEvFTPClientRawDataEventRead, ev_ftp_client);

	/* Set socket description */
	EvKQBaseFDDescriptionSetByFD(ev_base, ev_ftp_client->ev_tcpclient_ctrl->socket_fd , "BRB_EV_COMM - FTP client CONTROL");
	EvKQBaseFDDescriptionSetByFD(ev_base, ev_ftp_client->ev_tcpclient_data->socket_fd , "BRB_EV_COMM - FTP client DATA");


	return ev_ftp_client;
}
/**************************************************************************************************************************/
void CommEvFTPClientDestroy(CommEvFTPClient *ev_ftp_client)
{
	/* Sanity check */
	if (!ev_ftp_client)
		return;

	/* Shutdown TCP client side */
	CommEvTCPClientDestroy(ev_ftp_client->ev_tcpclient_ctrl);
	CommEvTCPClientDestroy(ev_ftp_client->ev_tcpclient_data);

	free(ev_ftp_client);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvFTPClientRawDataEventWriteMemBufferFinish(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvFTPClient *ev_ftp_client = cb_data;
	CommEvTCPClient *ev_tcpclient_data = ev_ftp_client->ev_tcpclient_data;

	//printf("CommEvFTPClientRawDataEventWriteMemBufferFinish - FD [%d] - DATA_CONN - FINISHED WRITING [%d] btyes\n", fd, MemBufferGetSize(ev_ftp_client->queued_transfer.raw_data));

	/* Disconnect data client */
	CommEvTCPClientDisconnect(ev_tcpclient_data);

	/* Reset client data FD */
	CommEvTCPClientResetFD(ev_tcpclient_data);

	/* Mark as reconnecting, so PASV parsing code do not re_dispatch AUTHOK event */
	ev_ftp_client->flags.reconnecting = 1;

	/* Schedule event */
	CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_FILE_RECV_OK, CommEvFTPClientAction226_FileReceivedOK, NULL);

	return;
}
/**************************************************************************************************************************/
static void CommEvFTPClientRawDataEventRead(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvFTPClient *ev_ftp_client = cb_data;
	CommEvTCPClient *ev_tcpclient_data = ev_ftp_client->ev_tcpclient_data;

	char *read_buffer_ptr;
	int read_buffer_sz;

	/* Zero sized read, bail out */
	if (!ev_tcpclient_data->iodata.read_buffer)
	{
		//printf("CommEvFTPClientRawDataEventRead - ZERO SIZED READ\n");
		return;
	}

	read_buffer_ptr = MemBufferDeref(ev_tcpclient_data->iodata.read_buffer);
	read_buffer_sz = MemBufferGetSize(ev_tcpclient_data->iodata.read_buffer);

	/* Add into raw data buffer */
	if (ev_ftp_client->queued_transfer.direction == EV_FTP_TRANSFER_RECEIVE_MEM)
		MemBufferAdd(ev_ftp_client->queued_transfer.raw_data, read_buffer_ptr, read_buffer_sz);

	//printf("CommEvFTPClientRawDataEventRead - FD [%d] - DATA_CONN - DATA from host [%s] port [%u] -> [%d] - [%s]\n", fd,
	//		ev_ftp_client->passive.target_str, ev_ftp_client->passive.target_port, read_buffer_sz, read_buffer_ptr);

	/* Clean the read buffer */
	MemBufferClean(ev_tcpclient_data->iodata.read_buffer);

	return;
}
/**************************************************************************************************************************/
static void CommEvFTPClientRawDataEventConnect(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvFTPClient *ev_ftp_client = cb_data;
	CommEvTCPClient *ev_tcpclient_data = ev_ftp_client->ev_tcpclient_data;

	/* Client connect OK, start AUTH */
	if (COMM_CLIENT_STATE_CONNECTED == ev_tcpclient_data->socket_state)
	{
		//printf("CommEvFTPClientRawDataEventConnect - FD [%d] - DATA_CONN - Connected to host [%s] port [%u]\n", fd, ev_ftp_client->passive.target_str, ev_ftp_client->passive.target_port);

		/* Schedule read and close events */
		CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_data, COMM_CLIENT_EVENT_CLOSE, CommEvFTPClientRawDataEventClose, ev_ftp_client);
		CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_data, COMM_CLIENT_EVENT_READ, CommEvFTPClientRawDataEventRead, ev_ftp_client);

		/* Note state */
		ev_ftp_client->current_state = EV_FTP_STATE_ONLINE;

		/* Dispatch internal event */
		if (!ev_ftp_client->flags.reconnecting)
			CommEvFTPClientEventDispatch(ev_ftp_client, COMM_FTPCLIENT_EVENT_AUTHOK);

		return;

	}
	/* Client failed connecting */
	else
	{
		//printf("CommEvFTPClientRawDataEventConnect - FD [%d] - DATA_CONN - FAILED connecting to host [%s] port [%u]\n", fd, ev_ftp_client->passive.target_str, ev_ftp_client->passive.target_port);

		/* Dispatch internal event */
		CommEvFTPClientEventDispatch(ev_ftp_client, COMM_FTPCLIENT_EVENT_FAIL_PASSIVE_DATACONN);
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvFTPClientRawDataEventClose(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvFTPClient *ev_ftp_client = cb_data;
	CommEvTCPClient *ev_tcpclient_data = ev_ftp_client->ev_tcpclient_data;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	//printf("CommEvFTPClientRawDataEventClose - FD [%d] - DATA_CONN - DISCONNECTED from host [%s] port [%u]\n", fd, ev_ftp_client->passive.target_str, ev_ftp_client->passive.target_port);


	/* We are receiving, disconnect DATA client */
	if (ev_ftp_client->queued_transfer.direction == EV_FTP_TRANSFER_RECEIVE_MEM)
	{
		//printf("CommEvFTPClientRawDataEventClose - RECEIVED_MEM OK\n");

		/* Reset client data FD */
		CommEvTCPClientResetFD(ev_tcpclient_data);

		/* Mark as reconnecting, so PASV parsing code do not re_dispatch AUTHOK event */
		ev_ftp_client->flags.reconnecting = 1;

		/* Re_negotiate PASV */
		CommEvTCPClientAIOWriteStringFmt(ev_tcpclient_ctrl, NULL, NULL, "PASV\r\n");
		CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_PASSIVE, CommEvFTPClientAction227_Passive, NULL);
	}



	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvFTPClientCTRLEventRead(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvFTPClient *ev_ftp_client = cb_data;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	char *read_buffer_ptr;
	int read_buffer_sz;

	char ftp_code_strbuf[8];

	int ftp_code;
	int i;

	/* Zero sized read, bail out */
	if (!ev_tcpclient_ctrl->iodata.read_buffer)
		return;

	read_buffer_ptr = MemBufferDeref(ev_tcpclient_ctrl->iodata.read_buffer);
	read_buffer_sz = MemBufferGetSize(ev_tcpclient_ctrl->iodata.read_buffer);

	//printf("CommEvFTPClientCTRLEventRead - Received DATA [%d]-[%s]\n", read_buffer_sz, read_buffer_ptr);

	/* Walk on the beginning of reply checking for ASCII digits */
	for (i = 0; (i < sizeof(ftp_code_strbuf) - 1); i++)
	{
		/* Not a digit, stop */
		if (!isdigit(read_buffer_ptr[i]))
			break;

		ftp_code_strbuf[i] = read_buffer_ptr[i];
	}

	/* NULL-terminate it */
	ftp_code_strbuf[i] = '\0';

	/* Convert */
	ftp_code = atoi((char*) &ftp_code_strbuf);

	/* Dispatch FTP code */
	CommEvFTPClientActionDispatch(ev_ftp_client, ftp_code);

	/* Clean read buffer */
	MemBufferClean(ev_tcpclient_ctrl->iodata.read_buffer);

	/* Return consumed bytes */
	return;

}
/**************************************************************************************************************************/
static void CommEvFTPClientCTRLEventConnect(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvFTPClient *ev_ftp_client = cb_data;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	/* Client connect OK, start AUTH */
	if (COMM_CLIENT_STATE_CONNECTED == ev_tcpclient_ctrl->socket_state)
	{
		//		printf("CommEvFTPClientEventConnect - FD [%d] - Connected to host [%s] port [%u]\n", fd, ev_ftp_client->cli_conf.host, ev_ftp_client->cli_conf.port);

		/* Schedule internal events */
		CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_ctrl, COMM_CLIENT_EVENT_CLOSE, CommEvFTPClientCTRLEventClose, ev_ftp_client);
		CommEvTCPClientEventSet(ev_ftp_client->ev_tcpclient_ctrl, COMM_CLIENT_EVENT_READ, CommEvFTPClientCTRLEventRead, ev_ftp_client);

		/* Register the actions we want right now */
		CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_READY, CommEvFTPClientAction220_Ready, NULL);
		CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_SENDPASS, CommEvFTPClientAction331_SendPass, NULL);
		CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_LOGGED_IN, CommEvFTPClientAction230_LoggedIn, NULL);

	}
	/* Client failed connecting */
	else
	{
		//		printf("CommEvFTPClientEventConnect - FD [%d] - FAILED connecting to host [%s] port [%u]\n", fd, ev_ftp_client->cli_conf.host, ev_ftp_client->cli_conf.port);
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvFTPClientCTRLEventClose(int fd, int data_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvFTPClient *ev_ftp_client = cb_data;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	//	printf("CommEvFTPClientEventConnect - FD [%d] - Lost connection to host [%s] port [%u]\n", fd, ev_ftp_client->cli_conf.host, ev_ftp_client->cli_conf.port);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvFTPClientAction553_FileFail(void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;
	CommEvTCPClient *ev_tcpclient_data = ev_ftp_client->ev_tcpclient_data;

	/* What command the client has sent */
	switch(ev_ftp_client->cmd.cmd_sent)
	{
	case  EV_FTPCLI_CMD_STOR:
	{
		/* Adjust flags */
		ev_ftp_client->flags.queued_transfer	= 0;
		ev_ftp_client->flags.busy				= 0;

		/* If there is a finish notification, invoke it */
		if (ev_ftp_client->queued_transfer.cb_handler_ptr)
		{
			CommEvFTPClientReplyCBH *cb_handler_ptr = ev_ftp_client->queued_transfer.cb_handler_ptr;
			void *cb_data_ptr = ev_ftp_client->queued_transfer.cb_data_ptr;

			/* Destroy queued data buffer */
			MemBufferDestroy(ev_ftp_client->queued_transfer.raw_data);
			ev_ftp_client->queued_transfer.raw_data = NULL;
			ev_ftp_client->queued_transfer.cb_handler_ptr	= NULL;
			ev_ftp_client->queued_transfer.cb_data_ptr		= NULL;

			/* Invoke finish notification call_back handler */
			cb_handler_ptr(ev_ftp_client, cb_data_ptr, EV_FTP_TRANSFER_FAILED_FILE_NOT_ALLOWED);
		}

		break;
	}

	}

	/* We DONT want to hear you anymore */
	CommEvFTPClientActionCancel(ev_ftp_client, EV_FTP_SERVICE_FILE_FAIL);


}
/**************************************************************************************************************************/
static void CommEvFTPClientAction550_CmdFail(void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	//	printf("CommEvFTPClientAction550_CmdOk - FD [%d] - Received 550 from SERVER [%s]\n", ev_tcpclient_ctrl->socket_fd, ev_ftp_client->cli_conf.host);

	return;
}
/**************************************************************************************************************************/
static void CommEvFTPClientAction331_SendPass(void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	//	printf("CommEvFTPClientAction331_SendPass - FD [%d] - Received 331 from SERVER [%s]\n", ev_tcpclient_ctrl->socket_fd, ev_ftp_client->cli_conf.host);

	/* Note state */
	ev_ftp_client->current_state = EV_FTP_STATE_SENT_PASS;

	/* Send USERNAME */
	CommEvTCPClientAIOWriteStringFmt(ev_tcpclient_ctrl, NULL, NULL, "PASS %s\r\n", ev_ftp_client->cli_conf.password);

	/* We DONT want to hear you anymore */
	CommEvFTPClientActionCancel(ev_ftp_client, EV_FTP_SERVICE_SENDPASS);

	return;
}

/**************************************************************************************************************************/
static void CommEvFTPClientAction250_CmdOk(void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	//	printf("CommEvFTPClientAction250_CmdOk - FD [%d] - Received 250 from SERVER [%s]\n", ev_tcpclient_ctrl->socket_fd, ev_ftp_client->cli_conf.host);

	return;
}

/**************************************************************************************************************************/
static void CommEvFTPClientAction230_LoggedIn(void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	//	printf("CommEvFTPClientAction230_LoggedIn - FD [%d] - Received 230 from SERVER [%s] - WE ARE ONLINE\n", ev_tcpclient_ctrl->socket_fd, ev_ftp_client->cli_conf.host);

	/* We DONT want to hear you anymore */
	CommEvFTPClientActionCancel(ev_ftp_client, EV_FTP_SERVICE_LOGGED_IN);

	if (ev_ftp_client->cli_conf.flags.passive)
	{
		CommEvTCPClientAIOWriteStringFmt(ev_tcpclient_ctrl, NULL, NULL, "PASV\r\n");
		CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_PASSIVE, CommEvFTPClientAction227_Passive, NULL);

		return;
	}
	else
	{
		/* Note state */
		ev_ftp_client->current_state = EV_FTP_STATE_ONLINE;

		/* Dispatch internal event */
		CommEvFTPClientEventDispatch(ev_ftp_client, COMM_FTPCLIENT_EVENT_AUTHOK);
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvFTPClientAction226_FileReceivedOK(void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;
	CommEvTCPClient *ev_tcpclient_data = ev_ftp_client->ev_tcpclient_data;

	/* Adjust flags */
	ev_ftp_client->flags.queued_transfer	= 0;
	ev_ftp_client->flags.busy				= 0;

	/* Re_negotiate PASV */
	CommEvTCPClientAIOWriteStringFmt(ev_tcpclient_ctrl, NULL, NULL, "PASV\r\n");
	CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_PASSIVE, CommEvFTPClientAction227_Passive, NULL);

	/* If there is a finish notification, invoke it */
	if (ev_ftp_client->queued_transfer.cb_handler_ptr)
	{
		CommEvFTPClientReplyCBH *cb_handler_ptr = ev_ftp_client->queued_transfer.cb_handler_ptr;
		void *cb_data_ptr = ev_ftp_client->queued_transfer.cb_data_ptr;

		/* Clear data before invoking client_side call_back if we are sending, if we are receiving, leave it here */
		if (ev_ftp_client->queued_transfer.direction == EV_FTP_TRANSFER_SEND)
		{
			MemBufferDestroy(ev_ftp_client->queued_transfer.raw_data);
			ev_ftp_client->queued_transfer.raw_data = NULL;
		}

		/* Clear handler and cb_data */
		ev_ftp_client->queued_transfer.cb_handler_ptr	= NULL;
		ev_ftp_client->queued_transfer.cb_data_ptr		= NULL;

		/* Invoke finish notification call_back handler */
		cb_handler_ptr(ev_ftp_client, cb_data_ptr, EV_FTP_TRANSFER_OK);
	}

	/* We DONT want to hear you anymore */
	CommEvFTPClientActionCancel(ev_ftp_client, EV_FTP_SERVICE_FILE_RECV_OK);

	return;
}
/**************************************************************************************************************************/
static void CommEvFTPClientAction227_Passive(void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvTCPClientConf tcpclient_conf;
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;
	CommEvTCPClient *ev_tcpclient_data = ev_ftp_client->ev_tcpclient_data;
	int pasv_parse_status;

	memset(&tcpclient_conf, 0, sizeof(CommEvTCPClientConf));

	/* Load configuration for TCP client */
	tcpclient_conf.hostname = (char*)ev_ftp_client->passive.target_str;
	tcpclient_conf.port		= ev_ftp_client->passive.target_port;

	/* We DONT want to hear you anymore */
	CommEvFTPClientActionCancel(ev_ftp_client, EV_FTP_SERVICE_PASSIVE);

	/* Parse PASV reply into FTPClient */
	pasv_parse_status = CommEvFTPClientParse227_PassiveReply(ev_ftp_client, MemBufferDeref(ev_tcpclient_ctrl->iodata.read_buffer),
			MemBufferGetSize(ev_tcpclient_ctrl->iodata.read_buffer));

	/* Mark flags if failed parsing passive reply */
	if (!pasv_parse_status)
		ev_ftp_client->flags.passv_parse_failed;

	/* Connect DATA TCP client side of this FTP client */
	CommEvTCPClientConnect(ev_tcpclient_data, &tcpclient_conf);

	return;
}

/**************************************************************************************************************************/
static void CommEvFTPClientAction150_FileOK (void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_data = ev_ftp_client->ev_tcpclient_data;

	/* We DONT want to hear you anymore */
	CommEvFTPClientActionCancel(ev_ftp_client, EV_FTP_SERVICE_FILE_OK);

	/* What command the client has sent */
	switch(ev_ftp_client->cmd.cmd_sent)
	{
	case  EV_FTPCLI_CMD_STOR:
	{
		/* Schedule data transfer */
		CommEvTCPClientAIOWriteMemBuffer(ev_tcpclient_data, ev_ftp_client->queued_transfer.raw_data, CommEvFTPClientRawDataEventWriteMemBufferFinish, ev_ftp_client);
		break;
	}
	case  EV_FTPCLI_CMD_RETR:
	{
		/* Listen to file finish event */
		CommEvFTPClientActionRegister(ev_ftp_client, EV_FTP_SERVICE_FILE_RECV_OK, CommEvFTPClientAction226_FileReceivedOK, NULL);
		break;
	}

	}

	return;
}
/**************************************************************************************************************************/
static void CommEvFTPClientAction220_Ready(void *ev_ftpclient_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpclient_ptr;
	CommEvTCPClient *ev_tcpclient_ctrl = ev_ftp_client->ev_tcpclient_ctrl;

	//	printf("CommEvFTPClientAction220_Ready - FD [%d] - Received 220 from SERVER [%s]\n", ev_tcpclient_ctrl->socket_fd, ev_ftp_client->cli_conf.host);

	/* Note state */
	ev_ftp_client->current_state = EV_FTP_STATE_SENT_USER;

	/* Send USERNAME */
	CommEvTCPClientAIOWriteStringFmt(ev_tcpclient_ctrl, NULL, NULL, "USER %s\r\n", ev_ftp_client->cli_conf.username);

	/* We DONT want to hear you anymore */
	CommEvFTPClientActionCancel(ev_ftp_client, EV_FTP_SERVICE_READY);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvFTPClientActionDispatch(CommEvFTPClient *ev_ftp_client, int action_code)
{
	/* DONT overflow */
	if (action_code > FTP_MAX_KNOWN_CODE)
		return 0;

	/* Pre_dispatch this reply if we are in busy state */
	if ((ev_ftp_client->flags.busy) && (ev_ftp_client->cmd.cb_handler_ptr))
	{
		ev_ftp_client->cmd.cb_handler_ptr(ev_ftp_client, ev_ftp_client->cmd.cb_data_ptr, action_code);

		ev_ftp_client->flags.busy = 0;
		ev_ftp_client->cmd.cb_handler_ptr = NULL;
		ev_ftp_client->cmd.cb_data_ptr = NULL;
	}

	/* Action not registered, bail out */
	if (!ev_ftp_client->action_table[action_code].action_cbh)
		return 0;

	/* Jump into action */
	ev_ftp_client->action_table[action_code].action_cbh(ev_ftp_client, ev_ftp_client->action_table[action_code].action_cbdata);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvFTPClientActionCancel(CommEvFTPClient *ev_ftp_client, int action_code)
{
	/* DONT overflow */
	if (action_code > FTP_MAX_KNOWN_CODE)
		return 0;

	/* Action not registered, bail out */
	if (!ev_ftp_client->action_table[action_code].action_cbh)
		return 0;

	ev_ftp_client->action_table[action_code].action_cbh = NULL;
	ev_ftp_client->action_table[action_code].action_cbdata = NULL;

	return 1;

}
/**************************************************************************************************************************/
static int CommEvFTPClientActionRegister(CommEvFTPClient *ev_ftp_client, int action_code, CommEvFTPClientServerActionCBH *cb_handler, void *cb_data)
{
	/* DONT overflow */
	if (action_code > FTP_MAX_KNOWN_CODE)
		return 0;

	ev_ftp_client->action_table[action_code].action_cbh = cb_handler;
	ev_ftp_client->action_table[action_code].action_cbdata = cb_data;

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvFTPClientLoadConfFromProto(CommEvFTPClientConf *ev_ftp_conf, CommEvFTPClientConfProto *ev_ftp_conf_proto)
{

	/* Set port */
	ev_ftp_conf->port = ev_ftp_conf_proto->port;

	/* Copy data */
	strncpy((char*) &ev_ftp_conf->host, ev_ftp_conf_proto->host, sizeof(ev_ftp_conf->host));
	strncpy((char*) &ev_ftp_conf->username, ev_ftp_conf_proto->username, sizeof(ev_ftp_conf->username));
	strncpy((char*) &ev_ftp_conf->password, ev_ftp_conf_proto->password, sizeof(ev_ftp_conf->password));

	memcpy(&ev_ftp_conf->flags, &ev_ftp_conf_proto->flags, sizeof(ev_ftp_conf->flags));

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvFTPClientEventDispatch(CommEvFTPClient *ev_ftp_client, CommEvFTPClientEvents ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_FTPCLIENT_EVENT_LASTITEM)
		return 0;

	/* Jump into the event if its enabled */
	if ((ev_ftp_client->events[ev_type].cb_handler_ptr) && ev_ftp_client->events[ev_type].flags.enabled)
	{
		ev_ftp_client->events[ev_type].cb_handler_ptr(ev_ftp_client, ev_ftp_client->events[ev_type].cb_data_ptr);
		return 1;
	}

	return 0;
}
/**************************************************************************************************************************/
static int CommEvFTPClientParse227_PassiveReply(CommEvFTPClient *ev_ftp_client, char *data_str_ptr, int data_str_sz)
{
	StringArray *passv_strarr;
	int passv_strarr_sz;
	char target_ip[32];
	int i;

	/* Search for begin of data we want */
	for (i = 0; (i < data_str_sz); i++)
	{
		/* Found wanted separator */
		if ('(' == data_str_ptr[i])
		{
			i++;
			break;
		}
	}

	if (i == (data_str_sz - 1) )
		return 0;

	/* Adjust pointer */
	data_str_ptr += i;

	/* Explode data */
	passv_strarr = StringArrayExplodeStr(data_str_ptr, ",", NULL, NULL);
	passv_strarr_sz = StringArrayGetElemCount(passv_strarr);

	/* Too few parameters for PASV reply */
	if (passv_strarr_sz < 6)
	{
		//printf("CommEvFTPClientParse227_PassiveReply -  Too few parameters for PASV reply [%d]\n", passv_strarr_sz);
		return 0;
	}


	/* Create target_ip */
	snprintf((char*)&ev_ftp_client->passive.target_str, (sizeof(ev_ftp_client->passive.target_str) - 1), "%s.%s.%s.%s",
			StringArrayGetDataByPos(passv_strarr, 0),
			StringArrayGetDataByPos(passv_strarr, 1),
			StringArrayGetDataByPos(passv_strarr, 2),
			StringArrayGetDataByPos(passv_strarr, 3));

	/* Grab P1, P2 and calculate passive port */
	ev_ftp_client->passive.p1					= atoi(StringArrayGetDataByPos(passv_strarr, 4));
	ev_ftp_client->passive.p2					= atoi(StringArrayGetDataByPos(passv_strarr, 5));
	ev_ftp_client->passive.target_port			= (ev_ftp_client->passive.p1 * 256) + ev_ftp_client->passive.p2;
	ev_ftp_client->passive.target_addr.s_addr	= inet_addr((char*)&ev_ftp_client->passive.target_str);

	/* Destroy parsing string array */
	StringArrayDestroy(passv_strarr);

	//printf("CommEvFTPClientParse227_PassiveReply - TARGET_ADDR [%s] - PORT [%d] - P1 [%d] - P2 [%d]\n",
	//		ev_ftp_client->passive.target_str, ev_ftp_client->passive.target_port, ev_ftp_client->passive.p1, ev_ftp_client->passive.p2);



	return 1;
}
/**************************************************************************************************************************/














