/*
 * test_tftp_server.c
 *
 *  Created on: 2014-03-07
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
CommEvTFTPServer *glob_tftp_server;

CommEvTFTPTServerCBH TFTPServerEventFileRead;

/**************************************************************************************************************************/
void TFTPServerEventFileRead(void *base, void *cb_data, int operation, int transfer_mode, char *filename, struct sockaddr_in *cli_addr)
{
	MemBuffer *data_mb;
	CommEvTFTPServer *ev_tftp_server = base;

	data_mb = MemBufferReadFromFile(filename);

	printf("TFTPServerEventFileRead - INFO: Client [%s:%u] - Requested filename: [%s] - Transfer mode [%d]\n", inet_ntoa(cli_addr->sin_addr), ntohs(cli_addr->sin_port), filename, transfer_mode);

	if (data_mb)
	{
		printf("TFTPServerEventFileRead - INFO: Read file [%s] - [%lu] bytes QUEUED\n", filename, MemBufferGetSize(data_mb));

		/* Enqueue transfer for this TFTP client */
		CommEvTFTPServerWriteMemBuffer(ev_tftp_server, data_mb, cli_addr);
	}
	else
	{
		printf("TFTPServerEventFileRead - INFO: File [%s] - NOT FOUND\n", filename);
		CommEvTFTPServerErrorReply(ev_tftp_server, COMM_TFTP_ERROR_FILE_NOTFOUND, cli_addr, "File [%s] NOT FOUND\n", filename);
	}

	return;
}
/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	int op_status;

	/* Create event base */
	glob_ev_base		= EvKQBaseNew(NULL);
	glob_tftp_server	= CommEvTFTPServerNew(glob_ev_base);


	op_status = CommEvTFTPServerInit(glob_tftp_server, NULL);

	CommEvTFTPServerEventSet(glob_tftp_server, COMM_TFTP_EVENT_FILE_READ, TFTPServerEventFileRead, NULL);

	if (COMM_TFTP_SERVER_INIT_OK != op_status)
	{
		printf("Failed initializing TFTP server with code [%d]\n", op_status);
		exit(0);
	}

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	exit(0);
}
/**************************************************************************************************************************/
