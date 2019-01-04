/*
 * comm_unix_aio.c
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

#include "../include/libbrb_core.h"

static void CommEvUNIXIODoDataDestroy(CommEvUNIXIOData *iodata);

/**************************************************************************************************************************/
int CommEvUNIXIODataInit(CommEvUNIXIOData *iodata, void *parent_ptr, int parent_fd, int max_sz, int mt_type)
{
	/* Sanity check */
	if (!iodata)
		return 0;

	/* Already INIT */
	if (iodata->flags.init)
		return 1;

	MemSlotBaseInit(&iodata->write.req_mem_slot, sizeof(CommEvUNIXWriteRequest), max_sz, mt_type ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE);
	MemSlotBaseInit(&iodata->write.ack_mem_slot, sizeof(CommEvUNIXACKReply), max_sz, mt_type ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE);

	/* Mark flags as INIT */
	iodata->flags.init = 1;

	return 1;
}
/**************************************************************************************************************************/
void CommEvUNIXIODataDestroy(CommEvUNIXIOData *iodata)
{
	/* If zero or below, destroy */
	if (iodata->ref_count <= 0)
		CommEvUNIXIODoDataDestroy(iodata);
	/* Decrement ref_count and leave */
	else
		iodata->ref_count--;

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXIODataLock(CommEvUNIXIOData *iodata)
{
	/* Sanity check */
	if (!iodata)
		return;

	/* Increment ref_count */
	iodata->ref_count++;

	return;
}
/**************************************************************************************************************************/
void CommEvUNIXIODataUnlock(CommEvUNIXIOData *iodata)
{
	/* Sanity check */
	if (!iodata)
		return;

	CommEvUNIXIODataDestroy(iodata);

	return;
}
/**************************************************************************************************************************/
int CommEvUNIXAutoCloseLocalDescriptors(CommEvUNIXWriteRequest *write_req)
{
	int i;

	/* Shutdown all open descriptors */
	for (i = 0; i < write_req->fd_arr.sz ; i++)
	{
		printf("CommEvUNIXAutoCloseLocalDescriptors - REQ_ID [%d] - Will close [%d] FDs - ARR_ITEM [%d] - FD [%d]\n", write_req->req_id, write_req->fd_arr.sz, i, write_req->fd_arr.data[i]);

		assert(write_req->fd_arr.data[i] > 0);
		EvKQBaseSocketClose(write_req->kq_base, write_req->fd_arr.data[i]);
		write_req->fd_arr.data[i] = -1;

		continue;
	}

	return i;
}
/**************************************************************************************************************************/
int CommEvUNIXIOControlDataProcess(EvKQBase *ev_base, CommEvUNIXIOData *iodata, CommEvUNIXControlData *control_data_head, int read_bytes)
{
	CommEvUNIXControlData *control_data_ptr;
	CommEvUNIXWriteRequest *write_req;
	int cur_ctrl_id;
	int reply_count;
	int i;

	/* Point to enqueued replies and calculate reply count - Control data head will point to the first CONTROL_REPLY - More of them can be aligned on READ_PTR */
	control_data_ptr	= MemBufferDeref(iodata->read.data_mb);
	reply_count			= ceil(read_bytes / sizeof(CommEvUNIXControlData));

	/* Get access to the write request who has originated this CONTROL_REPLY */
	write_req			= MemSlotBaseSlotGrabByID(&iodata->write.req_mem_slot, control_data_head->req_id);
	cur_ctrl_id			= MemSlotBaseSlotGetID(write_req);

	assert(control_data_head->req_id == cur_ctrl_id);
	assert(write_req->flags.in_use);

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "[0] - ACK reply for REQ_ID [%d] - AUTO_CLOSE [%d] - PEND_SZ [%d] - REPLIES [%d] - READ_BYTES [%d]\n",
			cur_ctrl_id, write_req->flags.autoclose_on_ack, iodata->write.req_mem_slot.list[COMM_UNIX_LIST_PENDING_ACK].size, reply_count, read_bytes);

	/* Invoke ACK CB */
	if (write_req->ack_cb)
		write_req->ack_cb(COMM_UNIX_ACK_OK, write_req, write_req->ack_cbdata);

	/* Upper layers want we close local FDs after ACK */
	if (write_req->flags.autoclose_on_ack)
		CommEvUNIXAutoCloseLocalDescriptors(write_req);

	/* Release write slot */
	write_req->flags.in_use = 0;
	MemSlotBaseSlotFree(&iodata->write.req_mem_slot, write_req);

	/* Stop right now if we are dealing with a single packet */
	if (reply_count <= 1)
		return reply_count;

	/* Walk all reply control packets */
	for (i = 1; i < reply_count; i++)
	{
		/* Get access to the write request who has originated this CONTROL_REPLY */
		write_req		= MemSlotBaseSlotGrabByID(&iodata->write.req_mem_slot, control_data_ptr[i - 1].req_id);
		cur_ctrl_id		= MemSlotBaseSlotGetID(write_req);

		assert(control_data_ptr[i - 1].req_id == cur_ctrl_id);
		assert(write_req->flags.in_use);

		/* Invoke ACK CB */
		if (write_req->ack_cb)
			write_req->ack_cb(COMM_UNIX_ACK_OK, write_req, write_req->ack_cbdata);

		/* Upper layers want we close local FDs after ACK */
		if (write_req->flags.autoclose_on_ack)
			CommEvUNIXAutoCloseLocalDescriptors(write_req);

		//KQBASE_LOG_PRINTF(ev_base->log_base, "[%d] - ACK reply for REQ_ID [%d] - AUTO_CLOSE [%d] - PEND_SZ [%d]\n",
		//		i, cur_ctrl_id, write_req->flags.autoclose_on_ack, iodata->write.req_mem_slot.list[COMM_UNIX_LIST_PENDING_ACK].size);

		/* Release write slot */
		write_req->flags.in_use = 0;
		MemSlotBaseSlotFree(&iodata->write.req_mem_slot, write_req);
		continue;
	}

	return reply_count;
}
/**************************************************************************************************************************/
int CommEvUNIXIOReadRaw(EvKQBase *ev_base, CommEvUNIXIOData *io_data, int socket_fd, int can_read_sz)
{
	char read_buffer[COMM_UNIX_READ_BUFFER_SIZE];
	struct cmsghdr *cmsg;
	struct msghdr	msghdr;
	struct iovec	iov [2];
	int read_bytes;

	/* Sanity check */
	if (can_read_sz <= 0)
		return 0;

	/* Load up IOV base */
	iov[0].iov_base			= &read_buffer;
	iov[0].iov_len 			= (sizeof(read_buffer) - 1);

	/* Load IOV into MSG */
	msghdr.msg_iov			= iov;
	msghdr.msg_iovlen		= 1;

	/* Load MESSAGE CONTROL area into MSG */
	msghdr.msg_control		= NULL;
	msghdr.msg_controllen	= 0;

	msghdr.msg_name			= NULL;
	msghdr.msg_namelen		= 0;
	msghdr.msg_flags		= 0;

	/* Read message PAYLOAD data */
	read_bytes 				= recvmsg (socket_fd, &msghdr, 0);

	/* Create READ_MB */
	if (!io_data->read.data_mb)
		io_data->read.data_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 4096);

	/* Append data to READ_MB */
	MemBufferAdd(io_data->read.data_mb, read_buffer, read_bytes);


	return read_bytes;
}
/**************************************************************************************************************************/
int CommEvUNIXIORead(EvKQBase *ev_base, CommEvUNIXIOData *io_data, int socket_fd, int can_read_sz)
{
	EvBaseKQFileDesc *kq_fd;
	CommEvUNIXControlData *control_data;
	char read_buffer[COMM_UNIX_READ_BUFFER_SIZE];
	struct cmsghdr *cmsg;
	struct msghdr	msghdr;
	struct iovec	iov [2];
	int *fd_arr;
	int i, j;

	int read_bytes		= 0;
	int total_read		= 0;
	int need_read		= 0;
	int can_read_bytes 	= can_read_sz;

	union
	{
		struct cmsghdr cmsg;
		char           control [CMSG_SPACE(COMM_UNIX_MAX_FDARR_SZ * sizeof(int))];
	} msg_control;

	control_data 		= &io_data->read.control_data;

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - READ_PARTIAL [%d] - SIZEOF CONTROL_DATA [%d] - CAN_READ [%d]\n",
			socket_fd, io_data->flags.read_partial, sizeof(CommEvUNIXControlData), can_read_sz);

	/* We are reading partial, go to RAW */
	if (io_data->flags.read_partial)
		goto read_raw;

	/* Clean our MSG header structure */
	memset(&msghdr, 0, sizeof(struct msghdr));
	memset(&msg_control, 0, sizeof(msg_control));

	/* Load up IOV base */
	iov[0].iov_base			= control_data;
	iov[0].iov_len 			= sizeof (CommEvUNIXControlData);

	/* Load IOV into MSG */
	msghdr.msg_name			= NULL;
	msghdr.msg_namelen		= 0;
	msghdr.msg_iov			= iov;
	msghdr.msg_iovlen		= 1;

	/* Load MESSAGE CONTROL area into MSG */
	msghdr.msg_control		= &msg_control;
	msghdr.msg_controllen	= sizeof (msg_control);
	msghdr.msg_flags		= 0;

	/* Read message CONTROL BLOCK and ANCILLIARY data */
	read_bytes 				= recvmsg (socket_fd, &msghdr, 0);
	total_read 				= read_bytes;

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - READ_BYTES A [%d] - NEED [%d] - ERRNO [%d]\n",
			socket_fd, read_bytes, need_read, ((read_bytes < 0) ? errno : 0));

	/* Error reading, bail out */
	if (read_bytes <= 0)
		return read_bytes;

	/* Log data */
	//EvKQBaseLoggerHexDump(ev_base->log_base, LOGTYPE_INFO, (char *)control_data, iov[0].iov_len, 8, 4);

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN,
			"READ_BYTES A [%d] - MAGIC [%X] - DATA_SZ [%ld] - REQ_ID [%d] - FLAGS [%d] - FD_COUNT [%d] - SEQ_ID [%d]\n",
			read_bytes, control_data->magic_code, control_data->data_sz, control_data->req_id, control_data->flags,
			control_data->fd_count, control_data->seq_id);

	/* This should not happen */
	BRB_ASSERT_FMT(ev_base, (COMM_UNIX_MAGIC_CODE00 == control_data->magic_code), "Invalid MAGIC [%X]\n", control_data->magic_code);

	/* Populate read side of IO_DATA */
	io_data->read.fd_arr.sz			= control_data->fd_count;

	/* Walk thru control FD-transfer kernel messages */
	if (io_data->read.fd_arr.sz > 0)
	{
		for (i = 0, cmsg = CMSG_FIRSTHDR (&msghdr); cmsg; cmsg = CMSG_NXTHDR (&msghdr, cmsg), i++)
		{
			KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "CMSG_LEN [%d]-[%d]\n",
					cmsg->cmsg_len, CMSG_LEN (control_data->fd_count * sizeof (int)));

			/* This is not what we want */
			if (cmsg->cmsg_len != CMSG_LEN (control_data->fd_count * sizeof (int)) || cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type  != SCM_RIGHTS)
				continue;

			/* Grab our descriptor */
			for (fd_arr = (int *)CMSG_DATA(cmsg), j = 0; j < control_data->fd_count; j++)
			{
				assert(fd_arr[j] > 0);
				io_data->read.fd_arr.data[j] = fd_arr[j];

				kq_fd	= EvKQBaseFDGrabFromArena(ev_base, io_data->read.fd_arr.data[j]);

				/* Touch flags */
				if (kq_fd)
				{
					kq_fd->flags.active		= 1;
					kq_fd->flags.closed		= 0;
					kq_fd->flags.closing	= 0;
				}

				continue;
			}

			continue;
		}
	}

	can_read_bytes 			= (can_read_bytes - read_bytes);

	/* Finish here is there is no USERDATA to read */
	if (control_data->data_sz <= 0)
		return read_bytes;

	/* Not enough data to read */
	if (can_read_bytes <= 0)
	{
		KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "NO ENOGH CAN READ [%d] - NEED [%d]\n", can_read_bytes, control_data->data_sz);

		io_data->flags.read_partial = 1;
		return read_bytes;
	}

	/* Tag to read RAW DATA */
	read_raw:

	need_read				= control_data->data_sz - (io_data->read.data_mb ? MemBufferGetSize(io_data->read.data_mb) : 0);

	if (can_read_bytes < need_read)
		need_read 			= can_read_bytes;

	/* Load up IOV base */
	iov[0].iov_base			= &read_buffer;
	iov[0].iov_len 			= ((need_read < (COMM_UNIX_READ_BUFFER_SIZE - 1)) ? need_read : (COMM_UNIX_READ_BUFFER_SIZE - 1));

	/* Load IOV into MSG */
	msghdr.msg_iov			= iov;
	msghdr.msg_iovlen		= 1;

	/* Load MESSAGE CONTROL area into MSG */
	msghdr.msg_control		= NULL;
	msghdr.msg_controllen	= 0;

	msghdr.msg_name			= NULL;
	msghdr.msg_namelen		= 0;
	msghdr.msg_flags		= 0;

	/* Read message PAYLOAD data */
	read_bytes 				= recvmsg (socket_fd, &msghdr, 0);
	total_read 				+= read_bytes;

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - READ_BYTES A [%d] - NEED [%d] - ERRNO [%d]\n",
				socket_fd, read_bytes, need_read, ((read_bytes < 0) ? errno : 0));

	/* Error reading, bail out */
	if (read_bytes <= 0)
	{
		io_data->flags.read_partial = 1;
		return total_read;
	}

	/* Create READ_MB */
	if (!io_data->read.data_mb)
		io_data->read.data_mb = MemBufferNew(BRBDATA_THREAD_UNSAFE, 4096);

	/* Append data to READ_MB and leave */
	MemBufferAdd(io_data->read.data_mb, read_buffer, read_bytes);

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "READ_BYTES B - MB_SZ [%d] - DATA_SZ [%d] - DELTA [%d]\n",
			MemBufferGetSize(io_data->read.data_mb), control_data->data_sz, (MemBufferGetSize(io_data->read.data_mb) - control_data->data_sz));

	/* Signal partial read */
	if (MemBufferGetSize(io_data->read.data_mb) < control_data->data_sz)
		io_data->flags.read_partial = 1;
	else
	{
		assert(MemBufferGetSize(io_data->read.data_mb) == control_data->data_sz);
		io_data->flags.read_partial = 0;
	}

	return total_read;
}
/**************************************************************************************************************************/
int CommEvUNIXIOWriteRaw(EvKQBase *ev_base, CommEvUNIXWriteRequest *write_req, int socket_fd)
{
	struct cmsghdr *cmsg;
	struct msghdr msghdr;
	struct iovec iov[2];
	int wrote_bytes;

	assert(write_req->flags.in_use);

	/* Clean our MSG header structure */
	memset(&msghdr, 0, sizeof(struct msghdr));

	/* Point to data */
	iov[0].iov_base 		= (write_req->data.ptr + write_req->data.offset);
	iov[0].iov_len  		= write_req->data.remain;

	/* Populate message header structure */
	msghdr.msg_iov			= iov;
	msghdr.msg_iovlen		= 1;
	msghdr.msg_name			= NULL;
	msghdr.msg_namelen		= 0;
	msghdr.msg_flags		= 0;

	/* Write to socket */
	wrote_bytes 			= sendmsg (socket_fd, &msghdr, 0);

	return wrote_bytes;
}
/**************************************************************************************************************************/
int CommEvUNIXIOWrite(EvKQBase *ev_base, CommEvUNIXWriteRequest *write_req, int socket_fd)
{
	CommEvUNIXControlData control_data;
	struct cmsghdr *cmsg;
	struct msghdr msghdr;
	struct iovec iov[2];
	int *fd_arr;
	int wrote_bytes = 0;
	int wrote_total = 0;
	int i;

	union
	{
		struct cmsghdr cmsg;
		char           control [CMSG_SPACE(COMM_UNIX_MAX_FDARR_SZ * sizeof(int))];
	} msg_control;

	/* Clean our MSG header structure */
	memset(&msghdr, 0, sizeof(struct msghdr));
	memset(&msg_control, 0, sizeof(msg_control));

	/* Clean BRB control data header */
	memset(&control_data, 0, sizeof(CommEvUNIXControlData));

	/* Fill IN first MSG_CONTROLL if we have FDs to transfer */
	if (0 == write_req->data.seq_id)
	{
		KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "A - DATA SIZE [%d] - OFFSET [%d] - REMAIN [%d] - SEQ_ID [%d] - REQ_ID [%d]\n",
				write_req->data.size, write_req->data.offset, write_req->data.remain, write_req->data.seq_id, write_req->req_id);

		/* Populate internal BRB control data */
		control_data.magic_code = COMM_UNIX_MAGIC_CODE00;
		control_data.req_id		= write_req->req_id;
		control_data.seq_id		= write_req->data.seq_id;
		control_data.data_sz	= write_req->data.size;
		control_data.fd_count	= ((0 == write_req->data.seq_id) ? write_req->fd_arr.sz : 0);
		control_data.flags		= write_req->ctrl_flags;

		/* Append control data */
		iov[0].iov_base			= &control_data;
		iov[0].iov_len  		= sizeof(CommEvUNIXControlData);

		/* Append user data */
		//		iov[1].iov_base 		= (write_req->data.ptr + write_req->data.offset);
		//		iov[1].iov_len  		= write_req->data.remain;

		/* Populate message header structure */
		msghdr.msg_iov			= iov;
		msghdr.msg_iovlen		= 1;

		msghdr.msg_name			= NULL;
		msghdr.msg_namelen		= 0;

		msghdr.msg_flags		= 0;

		//		msghdr.msg_control   	= msg_control.control;
		//		msghdr.msg_controllen 	= sizeof(msg_control.control);

		msghdr.msg_control   	= &msg_control;
		msghdr.msg_controllen	= CMSG_SPACE(write_req->fd_arr.sz * sizeof(int));
		//		msghdr.msg_controllen	= CMSG_SPACE(sizeof(msg_control));
		//		msghdr.msg_controllen	= CMSG_SPACE(COMM_UNIX_MAX_FDARR_SZ * sizeof(int));
		//		msghdr.msg_controllen	= sizeof(struct cmsghdr) + sizeof(int) * write_req->fd_arr.sz;

		cmsg 					= CMSG_FIRSTHDR(&msghdr);
		cmsg->cmsg_len			= CMSG_LEN(write_req->fd_arr.sz * sizeof(int));
		cmsg->cmsg_level 		= SOL_SOCKET;
		cmsg->cmsg_type 		= SCM_RIGHTS;

		/* Grab PTR to FD_ARR inside CONTROL MESSAGE and stuff to-transfer FDs inside it */
		fd_arr					= (int *)CMSG_DATA(cmsg);

		/* Load up FDs */
		for (i = 0; i < write_req->fd_arr.sz; i++)
			fd_arr[i] 			= write_req->fd_arr.data[i];

		/* Write to socket */
		wrote_bytes 			= sendmsg (socket_fd, &msghdr, 0);
		wrote_total				+= wrote_bytes;

		KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "WRITE A - WROTE [%d] - OFFSET [%d] - REMAIN [%d] - ERR [%d]\n",
				wrote_bytes, write_req->data.offset, write_req->data.remain, (wrote_bytes < 0) ? errno : 0);

		/* Touch sequence ID and offset */
		if (wrote_bytes > 0)
		{
			assert(wrote_bytes >= sizeof(CommEvUNIXControlData));

			write_req->data.offset += (wrote_bytes - sizeof(CommEvUNIXControlData));
			write_req->data.remain -= (wrote_bytes - sizeof(CommEvUNIXControlData));
			write_req->data.seq_id++;
		}
		else
		{
			return 0;
		}

		//		if (write_req->data.remain <= 0)
		//			return wrote_total;
	}

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "B - DATA SIZE [%d] - OFFSET [%d] - REMAIN [%d] - SEQ_ID [%d] - REQ_ID [%d]\n",
			write_req->data.size, write_req->data.offset, write_req->data.remain, write_req->data.seq_id, write_req->req_id);

	/* Append user data */
	iov[0].iov_base 		= (write_req->data.ptr + write_req->data.offset);
	iov[0].iov_len  		= write_req->data.remain;

	/* Populate message header structure */
	msghdr.msg_iov			= iov;
	msghdr.msg_iovlen		= 1;
	msghdr.msg_name			= NULL;
	msghdr.msg_namelen		= 0;
	msghdr.msg_flags		= 0;

	msghdr.msg_control   	= NULL;
	msghdr.msg_controllen	= 0;

	/* Write to socket */
	wrote_bytes 			= sendmsg (socket_fd, &msghdr, 0);
	wrote_total				+= wrote_bytes;

	/* Touch sequence ID and offset */
	if (wrote_bytes > 0)
	{
		//assert(wrote_bytes >= sizeof(CommEvUNIXControlData));

		write_req->data.offset += wrote_bytes;
		write_req->data.remain -= wrote_bytes;
		write_req->data.seq_id++;
	}

	KQBASE_LOG_PRINTF(ev_base->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "WRITE B - WROTE [%d] - OFFSET [%d] - REMAIN [%d] - ERR [%d]\n",
			wrote_bytes, write_req->data.offset, write_req->data.remain, (wrote_bytes < 0) ? errno : 0);

	return wrote_total;
}
/**************************************************************************************************************************/
int CommEvUNIXIOReplyACK(CommEvUNIXACKReply *ack_reply, int socket_fd)
{
	CommEvUNIXControlData control_data;
	struct cmsghdr *cmsg;
	struct msghdr	msghdr;
	struct iovec	iov [1];
	int wrote_bytes;

	/* Clean our MSG header structure */
	memset(&msghdr, 0, sizeof(struct msghdr));

	/* Clean and populate internal control data */
	memset(&control_data, 0, sizeof(CommEvUNIXControlData));
	control_data.magic_code = COMM_UNIX_MAGIC_CODE00;
	control_data.req_id		= ack_reply->req_id;
	control_data.data_sz	= -1;

	/* Set flags as reply */
	EBIT_SET(control_data.flags, COMM_UNIX_CONTROL_FLAGS_REPLY);

	/* Append control data */
	iov[0].iov_base			= &control_data;
	iov[0].iov_len  		= sizeof(CommEvUNIXControlData);

	/* Populate control message structure */
	msghdr.msg_name			= NULL;
	msghdr.msg_namelen		= 0;
	msghdr.msg_iov			= iov;
	msghdr.msg_iovlen		= 1;
	msghdr.msg_flags		= 0;

	msghdr.msg_control   	= NULL;
	msghdr.msg_controllen	= 0;

	/* Write to socket */
	wrote_bytes 			= sendmsg (socket_fd, &msghdr, 0);
	return wrote_bytes;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvUNIXIODoDataDestroy(CommEvUNIXIOData *iodata)
{
	CommEvUNIXWriteRequest *write_req_pending;
	CommEvUNIXWriteRequest *write_req_waiting_ack;

	/* Not initialized, bail out */
	if (!iodata->flags.init)
		return;

	/* Walk all WRITE pending slots and invoke WRITE_FINISH CALLBACKs with size -1 to give a chance to free associated structures */
	while ((write_req_pending = (CommEvUNIXWriteRequest*)MemSlotBaseSlotPopHead(&iodata->write.req_mem_slot, COMM_UNIX_LIST_PENDING_WRITE)))
	{
		/* Invoke finish CB */
		if (write_req_pending->finish_cb)
			write_req_pending->finish_cb(iodata->socket_fd, -1, -1, write_req_pending->finish_cbdata, iodata->parent);

		continue;
	}

	/* Walk all WRITE pending slots and invoke ACK CALLBACKs with size ZERO to give a chance to free associated structures */
	while ((write_req_waiting_ack = (CommEvUNIXWriteRequest*)MemSlotBaseSlotPopHead(&iodata->write.req_mem_slot, COMM_UNIX_LIST_PENDING_ACK)))
	{
		/* Invoke ACK CB */
		if (write_req_waiting_ack->ack_cb)
			write_req_waiting_ack->ack_cb(COMM_UNIX_ACK_FAILED, write_req_waiting_ack, write_req_waiting_ack->ack_cbdata);

		/* Upper layers want we close local FDs after ACK */
		if (write_req_waiting_ack->flags.autoclose_on_ack)
			CommEvUNIXAutoCloseLocalDescriptors(write_req_waiting_ack);

		continue;
	}

	/* Clean READ BUFFER */
	MemBufferDestroy(iodata->read.data_mb);
	iodata->read.data_mb = NULL;

	/* Clean WRITE and ACK slot arena */
	MemSlotBaseClean(&iodata->write.req_mem_slot);
	MemSlotBaseClean(&iodata->write.ack_mem_slot);

	/* Mark flags as NOT INIT */
	iodata->flags.init = 0;

	return;
}
/**************************************************************************************************************************/

