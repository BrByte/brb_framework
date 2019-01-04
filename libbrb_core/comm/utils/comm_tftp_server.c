/*
 * comm_tftp_server.c
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

#include "../include/libbrb_ev_kq.h"

static int CommEvTFTPACKWrite(CommEvTFTPServer *ev_tftp_server, CommEvTFTPTransfer *tftp_transfer, int seq_id);
static CommEvTFTPTransfer *CommEvTFTPTransferNew(CommEvTFTPServer *ev_tftp_server, MemBuffer *data_mb, int operation, struct sockaddr_in *cli_addr);
static void CommEvTFTPTransferDestroy(CommEvTFTPTransfer *tftp_transfer);
static int CommEvTFTPTransferBlockWrite(CommEvTFTPServer *ev_tftp_server, CommEvTFTPTransfer *tftp_transfer);

static int CommEvTFTPServerProcessControlPacket(CommEvTFTPServer *ev_tftp_server, CommEvTFTPControlPacket *tftp_ctl_packet, struct sockaddr_in *sender_addr, int total_sz);
static int CommEvTFTPServerProcessACK(CommEvTFTPServer *ev_tftp_server, CommEvTFTPPacket *tftp_packet, struct sockaddr_in *sender_addr);

static void CommEvTFTPServerEventDispatchInternal(CommEvTFTPServer *ev_tftp_server, int operation, int transfer_mode, char *filename, struct sockaddr_in *cli_addr);
static EvBaseKQCBH CommEvTFTPServerEventRead;
static EvBaseKQCBH CommEvTFTPServerEventWrite;


static unsigned short CommEvTFTPCheckSumAdd(unsigned short len_udp, int padding, const unsigned short *temp);

/**************************************************************************************************************************/
CommEvTFTPServer *CommEvTFTPServerNew(EvKQBase *kq_base)
{
	CommEvTFTPServer *ev_tftp_server;

	ev_tftp_server = calloc(1,sizeof(CommEvTFTPServer));

	ev_tftp_server->kq_base		= kq_base;
	ev_tftp_server->socket_fd	= -1;

	return ev_tftp_server;
}
/**************************************************************************************************************************/
int CommEvTFTPServerInit(CommEvTFTPServer *ev_tftp_server, CommEvTFTPServerConf *ev_tftp_server_conf)
{
	int op_status;

	/* Sanity check */
	if (!ev_tftp_server)
		return COMM_TFTP_SERVER_FAILURE_UNKNOWN;

	/* Save "listen" port in host order for future reference */
	ev_tftp_server->port					= ((ev_tftp_server_conf && ev_tftp_server_conf->port > 0) ? ev_tftp_server_conf->port : TFTP_DEFAULT_PORT);

	/* Initialize server_side.addr */
	ev_tftp_server->addr.sin_family			= AF_INET;
	ev_tftp_server->addr.sin_port			= htons(ev_tftp_server->port);
	ev_tftp_server->addr.sin_addr.s_addr	= htonl(INADDR_ANY);

	/* Create a new UDP non_blocking socket */
	ev_tftp_server->socket_fd				= EvKQBaseSocketUDPNew(ev_tftp_server->kq_base);

	if (ev_tftp_server->socket_fd < 0)
		return COMM_TFTP_SERVER_FAILURE_SOCKET;

	/* Set socket flags */
	EvKQBaseSocketSetNonBlock(ev_tftp_server->kq_base, ev_tftp_server->socket_fd);
	EvKQBaseSocketSetReuseAddr(ev_tftp_server->kq_base, ev_tftp_server->socket_fd);
	EvKQBaseSocketSetReusePort(ev_tftp_server->kq_base, ev_tftp_server->socket_fd);

	/* Bind UDP socket to port */
	op_status = bind(ev_tftp_server->socket_fd, (struct sockaddr *)&ev_tftp_server->addr, sizeof (struct sockaddr_in));

	/* Failed to bind - Close socket and leave */
	if (op_status < 0)
	{
		EvKQBaseSocketClose(ev_tftp_server->kq_base, ev_tftp_server->socket_fd);
		ev_tftp_server->socket_fd = -1;
		return COMM_TFTP_SERVER_FAILURE_BIND;
	}

	KQBASE_LOG_PRINTF(ev_tftp_server->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "New TFTP_SERVER listening on UDP_PORT [%d]\n", ev_tftp_server->port);

	/* Set read internal events for UDP server socket and FD_DESCRIPTION */
	EvKQBaseSetEvent(ev_tftp_server->kq_base, ev_tftp_server->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTFTPServerEventRead, ev_tftp_server);
	EvKQBaseFDDescriptionSetByFD(ev_tftp_server->kq_base, ev_tftp_server->socket_fd, "BRB_EV_COMM - TFTP SERVER on PORT [%u]", htons(ev_tftp_server->port));

	return COMM_TFTP_SERVER_INIT_OK;

}
/**************************************************************************************************************************/
void CommEvTFTPServerDestroy(CommEvTFTPServer *ev_tftp_server)
{
	if (!ev_tftp_server)
		return;

	if (ev_tftp_server->socket_fd > -1)
		EvKQBaseSocketClose(ev_tftp_server->kq_base, ev_tftp_server->socket_fd);

	ev_tftp_server->socket_fd = -1;
	free(ev_tftp_server);
	return;
}
/**************************************************************************************************************************/
int CommEvTFTPServerErrorReply(CommEvTFTPServer *ev_tftp_server, int error_code, struct sockaddr_in *cli_addr, char *error_msg, ...)
{
	CommEvTFTPPacket tftp_packet;
	char buf[512];
	va_list args;
	int wrote_bytes;
	int buf_sz;

	/* Clean up stack */
	memset(&tftp_packet, 0, sizeof(CommEvTFTPPacket));

	va_start( args, error_msg );
	buf_sz = vsnprintf((char*)&buf, (sizeof(buf) - 1), error_msg, args);
	buf[buf_sz] = '\0';
	va_end(args);

	/* Assembly TFTP packet on STACK */
	tftp_packet.opcode	= htons(COMM_TFTP_OPCODE_ERROR);
	tftp_packet.seq_id	= htons(error_code);

	if (error_msg)
		strlcpy((char*)&tftp_packet.data, (char*)&buf, sizeof(tftp_packet.data));

	/* Send data to client */
	wrote_bytes	= sendto(ev_tftp_server->socket_fd, &tftp_packet, (sizeof(int) + buf_sz), 0, (struct sockaddr*)cli_addr, sizeof(struct sockaddr_in));

	if (wrote_bytes >= (sizeof(int) + buf_sz))
		return 1;
	else
		return 0;
}
/**************************************************************************************************************************/
int CommEvTFTPServerWriteMemBuffer(CommEvTFTPServer *ev_tftp_server, MemBuffer *data_mb, struct sockaddr_in *cli_addr)
{
	/* Enqueue MB into READ LIST -> From US to CLIENT */
	CommEvTFTPTransferNew(ev_tftp_server, data_mb, COMM_TFTP_OPCODE_RRQ, cli_addr);

	return 1;
}
/**************************************************************************************************************************/
void CommEvTFTPServerEventSet(CommEvTFTPServer *ev_tftp_server, CommEvTFTPEventCodes ev_type, CommEvTFTPTServerCBH *cb_handler, void *cb_data)
{

	/* Sanity check */
	if (ev_type >= COMM_TFTP_EVENT_LASTITEM)
		return;

	/* Set event */
	ev_tftp_server->events[ev_type].cb_handler_ptr	= cb_handler;
	ev_tftp_server->events[ev_type].cb_data_ptr		= cb_data;

	/* Mark enabled */
	ev_tftp_server->events[ev_type].flags.enabled	= 1;


	return;
}
/**************************************************************************************************************************/
void CommEvTFTPServerEventCancel(CommEvTFTPServer *ev_tftp_server, CommEvTFTPEventCodes ev_type)
{
	/* Set event */
	ev_tftp_server->events[ev_type].cb_handler_ptr		= NULL;
	ev_tftp_server->events[ev_type].cb_data_ptr			= NULL;

	/* Mark disabled */
	ev_tftp_server->events[ev_type].flags.enabled			= 0;

}
/**************************************************************************************************************************/
void CommEvTFTPServerEventCancelAll(CommEvTFTPServer *ev_tftp_server)
{
	int i;

	/* Cancel all possible events */
	for (i = 0; i < COMM_TFTP_EVENT_LASTITEM; i++)
		CommEvTFTPServerEventCancel(ev_tftp_server, i);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTFTPACKWrite(CommEvTFTPServer *ev_tftp_server, CommEvTFTPTransfer *tftp_transfer, int seq_id)
{
	CommEvTFTPPacket tftp_packet;
	int wrote_bytes;

	memset(&tftp_packet, 0, sizeof(int));

	/* Assembly TFTP packet on STACK */
	tftp_packet.opcode	= htons(COMM_TFTP_OPCODE_ACK);
	tftp_packet.seq_id	= htons(seq_id);

	/* Send data to client */
	wrote_bytes	= sendto(ev_tftp_server->socket_fd, &tftp_packet, sizeof(int), 0, (struct sockaddr *)&tftp_transfer->cli_addr, sizeof(struct sockaddr_in));

	if (wrote_bytes >= sizeof(int))
	{
		printf("CommEvTFTPAckWrite - Wrote ACK [%d]\n", seq_id);

		tftp_transfer->flags.pending_ack	= 1;
		return 1;
	}
	else
		return 0;

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static CommEvTFTPTransfer *CommEvTFTPTransferNew(CommEvTFTPServer *ev_tftp_server, MemBuffer *data_mb, int operation, struct sockaddr_in *cli_addr)
{
	CommEvTFTPTransfer *tftp_transfer;

	tftp_transfer					= calloc(1,sizeof(CommEvTFTPTransfer));
	tftp_transfer->ev_tftp_server	= ev_tftp_server;
	tftp_transfer->operation		= operation;
	tftp_transfer->data_mb			= data_mb;

	/* Save client address */
	memcpy(&tftp_transfer->cli_addr, cli_addr, sizeof(struct sockaddr_in));

	/* Add to correct pending list */
	if (COMM_TFTP_OPCODE_RRQ == tftp_transfer->operation)
	{
		DLinkedListAddTail(&ev_tftp_server->pending.read_list, &tftp_transfer->node, tftp_transfer);
	}
	else if (COMM_TFTP_OPCODE_WRQ == tftp_transfer->operation)
	{
		DLinkedListAddTail(&ev_tftp_server->pending.write_list, &tftp_transfer->node, tftp_transfer);
	}

	/* Schedule WRITE event */
	EvKQBaseSetEvent(ev_tftp_server->kq_base, ev_tftp_server->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTFTPServerEventWrite, ev_tftp_server);

	return tftp_transfer;
}
/**************************************************************************************************************************/
static void CommEvTFTPTransferDestroy(CommEvTFTPTransfer *tftp_transfer)
{
	CommEvTFTPServer *ev_tftp_server;

	if (!tftp_transfer)
		return;

	ev_tftp_server = tftp_transfer->ev_tftp_server;

	/* Delete from correct pending list */
	if (COMM_TFTP_OPCODE_RRQ == tftp_transfer->operation)
	{
		DLinkedListDelete(&ev_tftp_server->pending.read_list, &tftp_transfer->node);
	}
	else if (COMM_TFTP_OPCODE_WRQ == tftp_transfer->operation)
	{
		DLinkedListDelete(&ev_tftp_server->pending.write_list, &tftp_transfer->node);
	}

	/* Destroy data buffer */
	MemBufferDestroy(tftp_transfer->data_mb);

	/* Free transfer node */
	free(tftp_transfer);

	return;
}
/**************************************************************************************************************************/
static int CommEvTFTPTransferBlockWrite(CommEvTFTPServer *ev_tftp_server, CommEvTFTPTransfer *tftp_transfer)
{
	CommEvTFTPPacket tftp_packet;

	char *raw_data;

	int total_size;
	int chunk_size;
	int wrote_bytes;

	int finished = 0;

	/* Clean stack buffer */
	memset(&tftp_packet, 0, sizeof(CommEvTFTPPacket));

	/* Calculate current offset */
	tftp_transfer->cur_offset	= (tftp_transfer->cur_block * TFTP_PAYLOAD_SIZE);
	total_size					= MemBufferGetSize(tftp_transfer->data_mb);

	/* Total size is smaller than single TFTP PAYLOAD - Will reply in one packet */
	if (total_size <= TFTP_PAYLOAD_SIZE)
	{
		chunk_size = total_size;
		//printf("CommEvTFTPTransferWriteBlock - Single packet reply of [%d] bytes\n", chunk_size);

		/* Mark as finished */
		finished = 1;
	}
	/* CUR_OFFSET points AFTER END OF DATA - Adjust current offset to total data size */
	else if (tftp_transfer->cur_offset > total_size)
	{
		chunk_size = (tftp_transfer->cur_offset - total_size);
		//printf("CommEvTFTPTransferWriteBlock - Last incomplete block of [%d] bytes\n", chunk_size);

		/* Mark as finished */
		finished = 1;
	}
	/* We are in the middle of data, move on */
	else
	{
		chunk_size = TFTP_PAYLOAD_SIZE;
		//printf("CommEvTFTPTransferWriteBlock - Regular block of [%d] bytes\n", chunk_size);

	}

	/* Grab data from buffer at correct offset */
	raw_data	= MemBufferOffsetDeref(tftp_transfer->data_mb, tftp_transfer->cur_offset);

	/* Assembly TFTP packet on STACK */
	tftp_packet.opcode	= htons(COMM_TFTP_OPCODE_DATA);
	tftp_packet.seq_id	= htons(tftp_transfer->cur_block + 1);
	memcpy(&tftp_packet.data, raw_data, chunk_size);

	/* Send data to client */
	wrote_bytes	= sendto(ev_tftp_server->socket_fd, &tftp_packet, (TFTP_HEADER_SIZE + chunk_size), 0, (struct sockaddr *)&tftp_transfer->cli_addr, sizeof(struct sockaddr_in));

	/* Success write */
	if (wrote_bytes >= (TFTP_HEADER_SIZE + chunk_size))
	{
		//printf("CommEvTFTPTransferWriteBlock - BLOCK [%d] - [%d - %d] - Wrote [%d] bytes to [%s:%u] - OK\n",
		//		tftp_transfer->cur_block + 1, tftp_transfer->cur_offset, tftp_transfer->cur_offset + chunk_size, wrote_bytes, inet_ntoa(tftp_transfer->cli_addr.sin_addr), ntohs(tftp_transfer->cli_addr.sin_port));

		if (finished)
		{
			//printf("CommEvTFTPTransferWriteBlock -  FINISHED\n");
			tftp_transfer->flags.finished = 1;
		}
		else
		{
			/* Walk to next block */
			tftp_transfer->cur_block++;

			/* We are waiting for ACK */
			tftp_transfer->flags.pending_ack	= 1;
			tftp_transfer->flags.pending_io		= 0;
		}

		return wrote_bytes;
	}
	else
	{
		printf("CommEvTFTPTransferWriteBlock - FAILED - Wrote [%d] bytes OK\n", wrote_bytes);
		return 0;
	}

	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvTFTPServerProcessControlPacket(CommEvTFTPServer *ev_tftp_server, CommEvTFTPControlPacket *tftp_ctl_packet, struct sockaddr_in *sender_addr, int total_sz)
{
	CommEvTFTPTransfer *tftp_transfer;
	MemBuffer *data_mb;

	char *transfermode_ptr;
	char *filename_ptr;
	int transfermode_sz;
	int filename_sz;
	int transfermode_code;

	int tftp_opcode		= ntohs(tftp_ctl_packet->opcode);

	/* Extract filename and mode from packet */
	filename_ptr		= (char*)&tftp_ctl_packet->data;
	filename_sz			= strlen(filename_ptr);
	transfermode_ptr	= filename_ptr;
	transfermode_ptr	+= (filename_sz + 1);
	transfermode_sz		= strlen(transfermode_ptr);

	/* Parse transfer MODE */
	if (transfermode_sz == 5 && !strncmp(transfermode_ptr, "octet", 5))
		transfermode_code = COMM_TFTP_TRANSFER_OCTET;
	/* Parse transfer MODE */
	else if (transfermode_sz == 8 && !strncmp(transfermode_ptr, "netascii", 8))
		transfermode_code = COMM_TFTP_TRANSFER_NETASCII;

	/* Dispatch upper layer notification */
	if ((COMM_TFTP_OPCODE_RRQ == tftp_opcode) || (COMM_TFTP_OPCODE_WRQ == tftp_opcode))
		CommEvTFTPServerEventDispatchInternal(ev_tftp_server, tftp_opcode, transfermode_code, filename_ptr, sender_addr);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTFTPServerProcessACK(CommEvTFTPServer *ev_tftp_server, CommEvTFTPPacket *tftp_packet, struct sockaddr_in *sender_addr)
{
	CommEvTFTPTransfer *tftp_transfer;
	DLinkedListNode *node;


	for (node = ev_tftp_server->pending.read_list.head; node; node = node->next)
	{
		tftp_transfer = node->data;

		/* Found our target */
		if (!memcmp(&tftp_transfer->cli_addr, sender_addr, sizeof(struct sockaddr_in)))
		{
			tftp_transfer->flags.pending_ack	= 0;
			tftp_transfer->flags.pending_io		= 1;
			return 1;
		}

		continue;
	}

	for (node = ev_tftp_server->pending.write_list.head; node; node = node->next)
	{
		tftp_transfer = node->data;

		/* Found our target */
		if (!memcmp(&tftp_transfer->cli_addr, sender_addr, sizeof(struct sockaddr_in)))
		{
			tftp_transfer->flags.pending_ack	= 0;
			tftp_transfer->flags.pending_io		= 1;
			return 1;
		}
	}


	return 0;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvTFTPServerEventDispatchInternal(CommEvTFTPServer *ev_tftp_server, int operation, int transfer_mode, char *filename, struct sockaddr_in *cli_addr)
{
	int ev_type;
	CommEvTFTPTServerCBH *cb_handler	= NULL;
	void *cb_handler_data				= NULL;

	/* Translate into correct EV_TYPE */
	if (COMM_TFTP_OPCODE_RRQ == operation)
		ev_type = COMM_TFTP_EVENT_FILE_READ;
	else if (COMM_TFTP_OPCODE_WRQ == operation)
		ev_type = COMM_TFTP_EVENT_FILE_WRITE;
	/* Event not found, bail out */
	else
		return;

	/* Grab callback_ptr */
	cb_handler = ev_tftp_server->events[ev_type].cb_handler_ptr;

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Grab data for this CBH */
		cb_handler_data = ev_tftp_server->events[ev_type].cb_data_ptr;

		/* Jump into CBH. Base for this event is CommEvTFTPServer* */
		cb_handler(ev_tftp_server, cb_handler_data, operation, transfer_mode, filename, cli_addr);
	}

	return;
}
/**************************************************************************************************************************/
static int CommEvTFTPServerEventWrite(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTFTPTransfer *tftp_transfer;
	DLinkedListNode *node;
	int op_status;

	CommEvTFTPServer *ev_tftp_server	= cb_data;

	/* Nothing more to dispatch, bail out */
	if (DLINKED_LIST_ISEMPTY(ev_tftp_server->pending.read_list))
		return 0;

	/* Walk all pending READ (client reading from us) list and WRITE a block, if possible */
	for (node = ev_tftp_server->pending.read_list.head; node; node = node->next)
	{
		loop_without_move:

		/* Sanity check */
		if ((!node) || (!node->data))
			break;

		tftp_transfer = node->data;

		/* We have a pending ACK on this transfer, move on */
		if (tftp_transfer->flags.pending_ack)
			continue;

		/* Write data to destination */
		op_status = CommEvTFTPTransferBlockWrite(ev_tftp_server, tftp_transfer);

		/* Failed writing, stop for now */
		if (!op_status)
			break;

		/* Touch statistics */
		ev_tftp_server->stats.sent_bytes += op_status;

		/* Finished transfer data, bail out */
		if (tftp_transfer->flags.finished)
			goto drop_transfer;

		continue;

		/* Label to destroy and drop transfer */
		drop_transfer:

		//printf("CommEvTFTPServerEventWrite - Transfer finished with [%d] data blocks\n", tftp_transfer->cur_block);

		node = node->next;
		CommEvTFTPTransferDestroy(tftp_transfer);
		goto loop_without_move;
	}

	/* Schedule WRITE event */
	EvKQBaseSetEvent(ev_tftp_server->kq_base, ev_tftp_server->socket_fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvTFTPServerEventWrite, ev_tftp_server);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvTFTPServerEventRead(int fd, int read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvTFTPControlPacket *tftp_ctl_packet;
	CommEvTFTPPacket *tftp_packet;
	char read_buf[TFTP_TOT_PACKET_SIZE + 1];
	struct sockaddr_in sender_addr;
	socklen_t addrlen;

	unsigned short raw_opcode;
	int read_bytes;
	int tftp_opcode;

	CommEvTFTPServer *ev_tftp_server = cb_data;

	/* Calculate ADDRLEN */
	addrlen = sizeof(struct sockaddr_in);

	/* Clean stack space */
	memset(&sender_addr, 0, sizeof(struct sockaddr_in));
	memset(&read_buf, 0, sizeof(read_buf));

	//printf("CommEvTFTPServerEventRead - Received [%d] bytes to read\n", read_sz);

	/* Receive OPCODE data */
	read_bytes	= recvfrom(fd, &read_buf, TFTP_TOT_PACKET_SIZE, 0, (struct sockaddr *) &sender_addr, &addrlen);

	if (read_bytes <= 0)
		goto error_reading;

	/* Copy first 2 bytes into RAW opcode to detect operation and convert back to HOST ORDER */
	memcpy(&raw_opcode, &read_buf, sizeof(short));
	tftp_opcode = ntohs(raw_opcode);

	//printf("CommEvTFTPServerEventRead - Read [%d] bytes - OPCODE [%u]\n", read_sz, tftp_opcode);

	/* Act based on OPCODE */
	switch (tftp_opcode)
	{
	case COMM_TFTP_OPCODE_RRQ:
	case COMM_TFTP_OPCODE_WRQ:
	{
		tftp_ctl_packet = (CommEvTFTPControlPacket*)&read_buf;

		/* Process control PACKET - This will schedule a READ or a WRITE */
		CommEvTFTPServerProcessControlPacket(ev_tftp_server, tftp_ctl_packet, &sender_addr, (read_bytes - TFTP_HEADER_SIZE));

		break;
	}
	case COMM_TFTP_OPCODE_ACK:
	{
		tftp_packet = (CommEvTFTPPacket*)&read_buf;

		//printf("CommEvTFTPServerEventRead - COMM_TFTP_OPCODE_ACK for SEQ_ID [%d]\n", ntohs(tftp_packet->seq_id));
		CommEvTFTPServerProcessACK(ev_tftp_server, tftp_packet, &sender_addr);

		break;
	}
	case COMM_TFTP_OPCODE_DATA:
	case COMM_TFTP_OPCODE_ERROR:
	{
		break;
	}
	}


	/* Set read internal events for UDP server socket */
	EvKQBaseSetEvent(ev_tftp_server->kq_base, ev_tftp_server->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTFTPServerEventRead, ev_tftp_server);

	return read_bytes;

	/* Label for error while reading */
	error_reading:

	printf("CommEvTFTPServerEventRead - WARNING: Failed reading with error [%d]\n", read_bytes);

	/* Set read internal events for UDP server socket */
	EvKQBaseSetEvent(ev_tftp_server->kq_base, ev_tftp_server->socket_fd, COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvTFTPServerEventRead, ev_tftp_server);
	return 0;


}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static unsigned short CommEvTFTPCheckSumAdd(unsigned short len_udp, int padding, const unsigned short *temp)
{
	return 0;

	unsigned short prot_udp = 17;
	unsigned short padd = 0;
	unsigned short word16;
	unsigned short sum;
	static unsigned char buff[1600];
	int i;

	memset(buff, 0, 1600);
	memcpy(buff, temp, len_udp);

	/* Take padding into account */
	if ((padding & 1) == 1)
	{
		padd = 1;
		buff[len_udp] = 0;
	}

	/* initialize sum to zero */
	sum = 0;

	/* make 16 bit words out of every two adjacent 8 bit words and calculate the sum of all 16 bit words */
	for (i = 0; i < len_udp + padd; i = i + 2)
	{
		word16	= ((buff[i] << 8) & 0xFF00) + (buff[i + 1] & 0xFF);
		sum		= sum + (unsigned long) word16;
	}

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	sum = ~sum;

	return ((unsigned short) sum);
}
/**************************************************************************************************************************/












