/*
 * comm_core_utils.c
 *
 *  Created on: 2021-10-05
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

#include "../include/libbrb_core.h"

/**************************************************************************************************************************/
int CommEvUtilsFDCheckState(int socket_fd)
{
	int op_status		= -1;
	int err				= 0;
	socklen_t errlen	= sizeof(err);

	struct sockaddr peer_addr;
	socklen_t sock_sz	= sizeof(struct sockaddr);

	/* Initialize stack */
	memset(&peer_addr, 0, sizeof(struct sockaddr));

	/* Get socket status */
	op_status = getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &errlen);

	if (op_status == 0)
	{
		switch (err)
		{

		case 0:
		case EISCONN:
		{
			op_status = getpeername(socket_fd, &peer_addr, &sock_sz);

			if (op_status == 0)
				return COMM_CLIENT_STATE_CONNECTED;
			else
				return COMM_CLIENT_STATE_CONNECT_FAILED_UNKNWON;

		}

		case EINPROGRESS:
			/* FALL TROUGHT */
		case EWOULDBLOCK:
			/* FALL TROUGHT */
		case EALREADY:
			/* FALL TROUGHT */
		case EINTR:
			return COMM_CLIENT_STATE_CONNECTING;

		case ECONNREFUSED:
			return COMM_CLIENT_STATE_CONNECT_FAILED_REFUSED;

		default:
			break;

		}
	}

	return COMM_CLIENT_STATE_CONNECT_FAILED_UNKNWON;
}
/**************************************************************************************************************************/
void CommEvNetClientAddrInit(struct sockaddr_in *conn_addr, char *host, unsigned short port)
{
	/* Clean conn_addr structure for later use */
	memset(conn_addr, 0, sizeof(struct sockaddr_in));

	/* Fill in the stub sockaddr_in structure */
	conn_addr->sin_family		= AF_INET;
	conn_addr->sin_addr.s_addr	= inet_addr(host);
	conn_addr->sin_port			= htons(port);

	return;
}
/**************************************************************************************************************************/
