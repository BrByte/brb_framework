/*
 * test_ftp.c
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

#include "include/test_ftp.h"

static CommEvFTPClientCBH FtpEventAuthOK;
static CommEvFTPClientCBH FtpEventAuthFail;
static CommEvFTPClientCBH FtpEventPassiveFail;

static CommEvFTPClientReplyCBH FtpEventGetFinish;
static CommEvFTPClientReplyCBH FtpEventPutFinish;

/**************************************************************************************************************************/
static void FtpEventGetFinish(void *ev_ftpcli_ptr, void *cb_data, int transfer_code)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpcli_ptr;

	if (EV_FTP_TRANSFER_OK == transfer_code)
	{
		printf("FtpEventGetFinish - SUCCESS - DATA [%ld]-[%s]\n", MemBufferGetSize(ev_ftp_client->queued_transfer.raw_data), MemBufferDeref(ev_ftp_client->queued_transfer.raw_data) );
	}

	return;
}

/**************************************************************************************************************************/
static void FtpEventPutFinish(void *ev_ftpcli_ptr, void *cb_data, int transfer_code)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpcli_ptr;
	MemBuffer *recv_data_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 1024);

	if (!recv_data_mb)
	{
		printf("FtpEventAuthOK - Failed loading file\n");
		return;
	}

	if (EV_FTP_TRANSFER_OK == transfer_code)
	{
		printf("FtpEventPutFinish - FINISHED OK\n");
		CommEvFTPClientRetrMemBuffer(ev_ftp_client, recv_data_mb, "/brb/filetransfer.txt", FtpEventGetFinish, NULL);
	}
	else
	{
		printf("FtpEventPutFinish - FINISHED FAILED\n");
	}

	return;
}
/**************************************************************************************************************************/
static void FtpEventAuthOK(void *ev_ftpcli_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpcli_ptr;
	MemBuffer *raw_data_mb = MemBufferReadFromFile("/testfile.txt");

	if (!raw_data_mb)
	{
		printf("FtpEventAuthOK - Failed loading file\n");
		return;
	}

	printf("FtpEventAuthOK - Sending file\n");

	/* Begin sending mem_buffer to destination server */
	CommEvFTPClientPutMemBuffer(ev_ftp_client, raw_data_mb, "/brb/filetransfer.txt", FtpEventPutFinish, NULL);

	return;
}
/**************************************************************************************************************************/
static void FtpEventAuthFail(void *ev_ftpcli_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpcli_ptr;

	printf("FtpEventAuthFail - Sending file\n");

	return;
}
/**************************************************************************************************************************/
static void FtpEventPassiveFail(void *ev_ftpcli_ptr, void *cb_data)
{
	CommEvFTPClient *ev_ftp_client = ev_ftpcli_ptr;

	printf("FtpEventPassiveFail - Sending file\n");

	return;
}
/**************************************************************************************************************************/
void FtpInit(char *p_host_str, int p_port, char *p_user, char *p_pass, int p_passive)
{
	CommEvFTPClient *ev_ftp_client;
	CommEvFTPClientConfProto ftp_conf;
	int op_status;

	/* Fill in configuration */
	ftp_conf.host 			= p_host_str;
	ftp_conf.port 			= p_port;
	ftp_conf.username 		= p_user;
	ftp_conf.password 		= p_pass;
	ftp_conf.flags.passive 	= p_passive;

	printf("FtpInit - HOST %s:%d\n", p_host_str, p_port);

	/* Create FTP client */
	ev_ftp_client 			= CommEvFTPClientNew(glob_ev_base);

	/* Set internal event */
	CommEvFTPClientEventSet(ev_ftp_client, COMM_FTPCLIENT_EVENT_AUTHFAIL, FtpEventAuthFail, NULL);
	CommEvFTPClientEventSet(ev_ftp_client, COMM_FTPCLIENT_EVENT_FAIL_PASSIVE_DATACONN, FtpEventPassiveFail, NULL);
	CommEvFTPClientEventSet(ev_ftp_client, COMM_FTPCLIENT_EVENT_AUTHOK, FtpEventAuthOK, NULL);

	/* Connect */
	op_status 		= CommEvFTPClientConnect(ev_ftp_client, &ftp_conf);

	printf("FtpInit - STATUS [%d]\n", op_status);

	return;
}
/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	int kq_retcode;

	/* Initialize event base */
	glob_ev_base 		= EvKQBaseNew(NULL);

	if (argc <= 4)
	{
		printf("USAGE - [%s] IP port user pass passive\n", argv[0]);
		exit(0);
	}

	int is_passive 	= (argc > 4) ? 1 : 0;

	/* Initialize disks data */
	FtpInit(argv[1], atoi(argv[2]), argv[3], argv[4], is_passive);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	/* We are exiting.. destroy event base */
	EvKQBaseDestroy(glob_ev_base);

	return 1;
}
/**************************************************************************************************************************/
