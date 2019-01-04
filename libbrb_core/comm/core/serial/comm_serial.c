/*
 * comm_serial.c
 *
 *  Created on: 2015-01-09
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2015 BrByte Software (Oliveira Alves & Amorim LTDA)
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

static void CommEvSerialPortEnqueueAndKickWriteQueue(CommEvSerialPort *serial_port, EvAIOReq *aio_req);
static void CommEvSerialEventInternalDispatch(CommEvSerialPort *serial_port, int data_sz, int thrd_id, int ev_type);
static void CommEvSerialDestroyConnReadAndWriteBuffers(CommEvSerialPort *serial_port);
static int CommEvSerialPortInternalConnect(CommEvSerialPort *serial_port, CommEvSerialPortConfig *serial_config);
static int CommEvSerialOptionsLoad(CommEvSerialPort *serial_port);
static int CommEvSerialBaudRateAdapt(int baud);

static EvBaseKQCBH CommEvSerialEventRead;
static EvBaseKQCBH CommEvSerialEventWrite;
static EvBaseKQCBH CommEvSerialEventEOF;

/**************************************************************************************************************************/
CommEvSerialPort *CommEvSerialPortNew(EvKQBase *ev_base)
{
	CommEvSerialPort *serial_port;

	/* Create a new SERIAL_PORT and save EV_BASE reference */
	serial_port = calloc(1, sizeof(CommEvSerialPort));
	serial_port->kq_base = ev_base;

	/* Initialize WRITE_QUEUE */
	EvAIOReqQueueInit(serial_port->kq_base, &serial_port->iodata.write_queue, 4096, (serial_port->kq_base->flags.mt_engine ? AIOREQ_QUEUE_MT_SAFE : AIOREQ_QUEUE_MT_UNSAFE),
			AIOREQ_QUEUE_SIMPLE);

	return serial_port;
}
/**************************************************************************************************************************/
int CommEvSerialPortDestroy(CommEvSerialPort *serial_port)
{
	/* Sanity check */
	if (!serial_port)
		return 0;

	/* Destroy IO buffers */
	CommEvSerialDestroyConnReadAndWriteBuffers(serial_port);

	/* Clean up serial port and release container */
	CommEvSerialPortClose(serial_port);
	free(serial_port);

	return 1;
}
/**************************************************************************************************************************/
CommEvSerialPort *CommEvSerialPortOpen(EvKQBase *ev_base, CommEvSerialPortConfig *serial_config)
{
	CommEvSerialPort *serial_port;
	int op_status;

	/* Create a new serial port */
	serial_port				= CommEvSerialPortNew(ev_base);
	serial_port->log_base	= serial_config->log_base;
	op_status				= CommEvSerialPortInternalConnect(serial_port, serial_config);

	/* Failed opening serial port */
	if (!op_status)
	{
		CommEvSerialPortDestroy(serial_port);
		return NULL;
	}

	/* Initialize SERIAL_FD */
	EvKQBaseSerialPortFDInit(ev_base, serial_port->fd);
	EvKQBaseSocketSetNonBlock(ev_base, serial_port->fd);

	/* Drain read buffer if flag is set */
	if (serial_config->flags.empty_read_buffer)
		EvKQBaseFDReadBufferDrain(ev_base, serial_port->fd);

	/* Set internal events */
	EvKQBaseSetEvent(ev_base, serial_port->fd,  COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventRead, serial_port);
	EvKQBaseSetEvent(ev_base, serial_port->fd,  COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventWrite, serial_port);
	EvKQBaseSetEvent(ev_base, serial_port->fd,  COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventEOF, serial_port);

	return serial_port;
}
/**************************************************************************************************************************/
int CommEvSerialPortClose(CommEvSerialPort *serial_port)
{
	/* Sanity check */
	if ((!serial_port) || (serial_port->fd < 0))
		return 0;

	/* Flush any pending data on serial */
	tcflush(serial_port->fd, TCOFLUSH);

	/* Close control FD */
	EvKQBaseSocketClose(serial_port->kq_base, serial_port->fd);
	serial_port->fd = -1;

	return 1;
}
/**************************************************************************************************************************/
int CommEvSerialAIOWriteString(CommEvSerialPort *serial_port, char *data_str, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	unsigned long data_sz = strlen(data_str);
	int op_status;

	/* Sanity check */
	if (!serial_port)
		return 0;

	op_status = CommEvSerialAIOWrite(serial_port, data_str, data_sz, finish_cb, finish_cbdata);

	return op_status;
}
/**************************************************************************************************************************/
int CommEvSerialAIOWrite(CommEvSerialPort *serial_port, char *data, unsigned long data_sz, EvAIOReqCBH *finish_cb, void *finish_cbdata)
{
	EvAIOReq *aio_req;

	/* Sanity check */
	if (!serial_port)
		return 0;

	/* Close request */
	if (serial_port->flags.close_request)
		return 0;

	/* Create a new aio_request */
	aio_req = EvAIOReqNew(&serial_port->iodata.write_queue, serial_port->fd, serial_port, data, data_sz, 0, NULL, finish_cb, finish_cbdata);

	/* Set flags we are WRITING to a SOCKET */
	aio_req->flags.aio_write	= 1;
	aio_req->flags.aio_serial	= 1;

	/* Enqueue and begin writing ASAP */
	CommEvSerialPortEnqueueAndKickWriteQueue(serial_port, aio_req);
	return 1;

}
/**************************************************************************************************************************/
int CommEvSerialPortEventIsSet(CommEvSerialPort *serial_port, int ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_SERIAL_EVENT_LASTITEM)
		return 0;

	if (serial_port->events[ev_type].cb_handler_ptr && serial_port->events[ev_type].flags.enabled)
		return 1;

	return 0;
}
/**************************************************************************************************************************/
void CommEvSerialPortEventSet(CommEvSerialPort *serial_port, int ev_type, EvBaseKQCBH *cb_handler, void *cb_data)
{
	/* Sanity check */
	if (ev_type >= COMM_SERIAL_EVENT_LASTITEM)
		return;

	/* Set event */
	serial_port->events[ev_type].cb_handler_ptr		= cb_handler;
	serial_port->events[ev_type].cb_data_ptr		= cb_data;

	/* Mark enabled */
	serial_port->events[ev_type].flags.enabled		= 1;

	return;
}
/**************************************************************************************************************************/
void CommEvSerialPortEventCancel(CommEvSerialPort *serial_port, int ev_type)
{
	/* Sanity check */
	if (ev_type >= COMM_SERIAL_EVENT_LASTITEM)
		return;

	/* Set event */
	serial_port->events[ev_type].cb_handler_ptr		= NULL;
	serial_port->events[ev_type].cb_data_ptr		= NULL;

	/* Mark disabled */
	serial_port->events[ev_type].flags.enabled		= 0;

	/* Update kqueue_event */
	switch (ev_type)
	{
	case COMM_SERIAL_EVENT_READ:
		EvKQBaseSetEvent(serial_port->kq_base, serial_port->fd, COMM_EV_READ, COMM_ACTION_DELETE, NULL, NULL);
		break;
	}

	return;
}
/**************************************************************************************************************************/
void CommEvSerialPortEventCancelAll(CommEvSerialPort *serial_port)
{
	int i;

	/* Cancel all possible events */
	for (i = 0; i < COMM_SERIAL_EVENT_LASTITEM; i++)
		CommEvSerialPortEventCancel(serial_port, i);

	return;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void CommEvSerialPortEnqueueAndKickWriteQueue(CommEvSerialPort *serial_port, EvAIOReq *aio_req)
{
	/* Close request */
	if (serial_port->flags.close_request)
	{
		EvAIOReqInvokeCallBacks(aio_req, 1, aio_req->fd, -1, -1, aio_req->parent_ptr);
		EvAIOReqDestroy(aio_req);
		return;
	}

	/* Enqueue it in WRITE_QUEUE */
	EvAIOReqQueueEnqueue(&serial_port->iodata.write_queue, aio_req);

	/* If there is ENQUEUED data, schedule WRITE event and LEAVE, as we need to PRESERVE WRITE ORDER */
	if (serial_port->flags.pending_write)
	{
		EvKQBaseSetEvent(serial_port->kq_base, serial_port->fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventWrite, serial_port);
		return;
	}
	/* Try to write on this very same IO LOOP */
	else
	{
		CommEvSerialEventWrite(serial_port->fd, ((aio_req->data.size < 8092) ? aio_req->data.size : 8092), -1, serial_port, serial_port->kq_base);
		return;
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvSerialEventInternalDispatch(CommEvSerialPort *serial_port, int data_sz, int thrd_id, int ev_type)
{
	EvBaseKQCBH *cb_handler	= NULL;
	void *cb_handler_data	= NULL;

	/* Grab callback_ptr */
	cb_handler	= serial_port->events[ev_type].cb_handler_ptr;

	/* Touch time stamps */
	serial_port->events[ev_type].run.ts = serial_port->kq_base->stats.cur_invoke_ts_sec;
	memcpy(&serial_port->events[ev_type].run.tv, &serial_port->kq_base->stats.cur_invoke_tv, sizeof(struct timeval));

	/* There is a handler for this event. Invoke the damn thing */
	if (cb_handler)
	{
		/* Grab data for this CBH */
		cb_handler_data = serial_port->events[ev_type].cb_data_ptr;

		/* Jump into CBH. Base for this event is CommEvUNIXServer* */
		cb_handler(serial_port->fd, data_sz, thrd_id, cb_handler_data, serial_port);
	}

	return;
}
/**************************************************************************************************************************/
static void CommEvSerialDestroyConnReadAndWriteBuffers(CommEvSerialPort *serial_port)
{
	/* Destroy any pending write event */
	EvAIOReqQueueClean(&serial_port->iodata.write_queue);

	/* Destroy any data buffered on read_buffer */
	if (serial_port->iodata.read_buffer)
		MemBufferDestroy(serial_port->iodata.read_buffer);

	serial_port->iodata.read_buffer = NULL;
	return;

}
/**************************************************************************************************************************/
static int CommEvSerialPortInternalConnect(CommEvSerialPort *serial_port, CommEvSerialPortConfig *serial_config)
{
	int tty_fd;
	int dummy;

	/* Sanity check */
	if ((!serial_port) || (!serial_config) || (!serial_config->device_name_str))
		return 0;

	/* Try to open device */
	tty_fd = open(serial_config->device_name_str, (O_RDWR | O_NOCTTY | O_NONBLOCK));

	/* Failed opening TTY, bail out */
	if (tty_fd < 0)
	{
		KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_WARNING, LOGCOLOR_YELLOW, "Failed opening TTY_FD at [%s] - ERRNO [%d]\n", serial_config->device_name_str, errno);

		/* Mark FD as inactive and leave */
		serial_port->fd = -1;
		return 0;
	}

	/* Get exclusive access to TTY device */
	if (serial_config->flags.exclusive_lock)
		ioctl(tty_fd, TIOCEXCL, &dummy);

	/* Save device configuration information */
	serial_port->baud		= CommEvSerialBaudRateAdapt(serial_config->baud);
	serial_port->parity		= serial_config->parity;
	serial_port->wordlen	= serial_config->wordlen;
	serial_port->stopbits	= serial_config->stopbits;
	serial_port->fd			= tty_fd;

	/* Save flags */
	serial_port->flags.echo_ignore = serial_config->flags.echo_ignore;

	/* Save device flow_control and name */
	memcpy(&serial_port->flow_control, &serial_config->flow_control, sizeof(serial_port->flow_control));
	strncpy((char*)&serial_port->device_name_str, serial_config->device_name_str, sizeof(serial_port->device_name_str));

	/* Load options */
	CommEvSerialOptionsLoad(serial_port);

	KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - Opened TTY device [%s]\n", tty_fd, serial_config->device_name_str);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvSerialOptionsLoad(CommEvSerialPort *serial_port)
{
	/* Load current serial port options */
	tcgetattr(serial_port->fd, &serial_port->options.cur);
	tcgetattr(serial_port->fd, &serial_port->options.old);

	/* Set defined SPEED into current TERMIOS options */
	cfsetspeed(&serial_port->options.cur, serial_port->baud);

	/* Set parity */
	switch(serial_port->parity)
	{

	case COMM_SERIAL_PARITY_NONE:
		serial_port->options.cur.c_cflag &= ~PARENB;
		break;

	case COMM_SERIAL_PARITY_ODD:
		serial_port->options.cur.c_cflag |= PARENB;
		serial_port->options.cur.c_cflag |= PARODD;
		break;

	case COMM_SERIAL_PARITY_EVEN:
		serial_port->options.cur.c_cflag |= PARENB;
		serial_port->options.cur.c_cflag &= ~PARODD;
		break;

	case COMM_SERIAL_PARITY_MARK:
		serial_port->options.cur.c_cflag |= (PARENB | COMM_SERIAL_PARITY_CMS | PARODD);
		break;

	case COMM_SERIAL_PARITY_SPACE:
		serial_port->options.cur.c_cflag |= (PARENB | COMM_SERIAL_PARITY_CMS);
		serial_port->options.cur.c_cflag &= ~PARODD;
		break;
	}

	/* STOP bits */
	if (serial_port->stopbits == 2)
		serial_port->options.cur.c_cflag |= CSTOPB;
	else
		serial_port->options.cur.c_cflag &= ~CSTOPB;

	/* WORDLEN */
	serial_port->options.cur.c_cflag &= ~CSIZE;

	if (serial_port->wordlen == 7)
		serial_port->options.cur.c_cflag |= CS7;
	else if (serial_port->wordlen == 6)
		serial_port->options.cur.c_cflag |= CS6;
	else if (serial_port->wordlen == 5)
		serial_port->options.cur.c_cflag |= CS5;

	/* This is the default */
	else serial_port->options.cur.c_cflag |= CS8;

	/* RTS/CTS */
	if (!serial_port->flow_control.rtscts)
		serial_port->options.cur.c_cflag &= ~CRTSCTS;
	else
		serial_port->options.cur.c_cflag |= CRTSCTS;

	serial_port->options.cur.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
	serial_port->options.cur.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON | IXOFF | IXANY);
	serial_port->options.cur.c_iflag |= IGNPAR;
	serial_port->options.cur.c_oflag &= ~OPOST;

	/* XON/XOFF */
	if (serial_port->flow_control.xonxoff)
		serial_port->options.cur.c_iflag |= (IXON | IXOFF);

	/* WARNING:  MIN = 1 means, in TIME (1/10 secs) defined timeout will be started AFTER receiving the first byte so we must set MIN = 0.
	 * (timeout starts immediately, abort also without READED byte */
	serial_port->options.cur.c_cc[VMIN] = 0;

	/* No timeout for non blocked transfer */
	serial_port->options.cur.c_cc[VTIME] = 0;

	/* Write options to TTY terminal */
	tcsetattr(serial_port->fd, TCSANOW, &serial_port->options.cur);

	return 1;
}
/**************************************************************************************************************************/
static int CommEvSerialBaudRateAdapt(int baud)
{
	switch(baud)
	{
	case 150:
		return B150;
	case 300:
		return B300;
	case 600:
		return B600;
	case 1200:
		return B1200;
	case 2400:
		return B2400;
	case 4800:
		return B4800;
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 921600:
		return B921600;

		/* NOTE! The speed of 38400 is required, if you want to set an non-standard BAUD_RATE. See below! */
	default:
		return B38400;
	}

	return B38400;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int CommEvSerialEventRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvSerialPort *serial_port	= cb_data;
	int data_read					= 0;
	char *data_ptr					= NULL;
	int data_sz						= 0;
	int terminator_off				= 0;

	KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - [%s] - Read event with [%d] bytes\n",
			fd, serial_port->device_name_str, can_read_sz);

	/* Reschedule read event */
	EvKQBaseSetEvent(ev_base, serial_port->fd,  COMM_EV_READ, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventRead, serial_port);

	/* Empty read */
	if (can_read_sz <= 0)
		return 0;

	/* Create a new read_buffer object */
	if (!serial_port->iodata.read_buffer)
		serial_port->iodata.read_buffer = MemBufferNew((ev_base->flags.mt_engine ? BRBDATA_THREAD_SAFE : BRBDATA_THREAD_UNSAFE), (can_read_sz + 1));

	/* Read from FD with flags set to ZERO */
	data_read	= MemBufferReadFromFD(serial_port->iodata.read_buffer, can_read_sz, serial_port->fd);
	data_ptr	= MemBufferDeref(serial_port->iodata.read_buffer);
	data_sz		= MemBufferGetSize(serial_port->iodata.read_buffer);

	/* Echo IGNORE, ignore all data that do not begin with \r\n */
	if ((serial_port->flags.echo_ignore) && ( ('\x0D' != data_ptr[0]) || ('\x0A' != data_ptr[1])) )
	{
		/* Search terminator */
		terminator_off 		= BrbStrFindSubStr(data_ptr, data_sz, "\r\n", 2);

		KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - [%s] - CLEAN - ECHO - TERMINATOR [%d] - [%d] bytes\n",
				fd, serial_port->device_name_str, terminator_off, data_sz);

		/* Log data */
		EvKQBaseLogHexDump(data_ptr, data_sz, 8, 4);

		if ((terminator_off > 0) && (terminator_off < data_sz))
		{
			MemBufferOffsetSet(serial_port->iodata.read_buffer, (terminator_off - 1));
		}
		else
		{
			MemBufferClean(serial_port->iodata.read_buffer);
			goto leave;
		}
	}

	KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - [%s] - Read [%d] bytes\n",
			fd, serial_port->device_name_str, data_read);

	/* Dispatch event - This upper layer event can make serial_port->fd get disconnected, and destroy IO buffers beneath our feet */
	CommEvSerialEventInternalDispatch(serial_port, data_read, thrd_id, COMM_SERIAL_EVENT_READ);

	/* Touch statistics and leave */
	leave:

	serial_port->statistics.total[COMM_CURRENT].byte_rx		+= data_read;
	serial_port->statistics.total[COMM_CURRENT].packet_rx	+= 1;

	return 1;
}
/**************************************************************************************************************************/
static int CommEvSerialEventWrite(int fd, int can_write_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvAIOReq *cur_aio_req;

	char *data_ptr;
	unsigned long wanted_write_sz;
	unsigned long possible_write_sz;

	int wrote_sz;
	int still_can_write_sz;

	EvKQBase *ev_base				= base_ptr;
	CommEvSerialPort *serial_port	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(ev_base, serial_port->fd);
	int total_wrote_sz				= 0;

	KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - [%s] - Write event with [%d] bytes\n",
			fd, serial_port->device_name_str, can_write_sz);

	/* Label used to write multiple aio_reqs in the same write_window, if possible */
	write_again:

	/* Grab aio_req unit */
	cur_aio_req			= EvAIOReqQueueDequeue(&serial_port->iodata.write_queue);

	/* Nothing to write, bail out */
	if (!cur_aio_req)
	{
		KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Empty WRITE_LIST.. STOP\n",	fd);

		/* Reset pending write flag */
		serial_port->flags.pending_write = 0;

		/* Upper layers requested to close after writing all */
		if (serial_port->flags.close_request)
		{
			KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Upper layer set CLOSE_REQUEST, write buffer is empty - [%s]\n",
					serial_port->fd, serial_port->flags.destroy_after_close ? "DESTROYING" : "CLOSING");

			/* Destroy or close, based on flag */
			COMM_SERIAL_FINISH(serial_port);
		}

		return total_wrote_sz;
	}

	/* Calculate current data size and offset */
	wanted_write_sz		= EvAIOReqGetMissingSize(cur_aio_req);
	data_ptr			= EvAIOReqGetDataPtr(cur_aio_req);
	possible_write_sz	= ((can_write_sz < wanted_write_sz) ? can_write_sz : wanted_write_sz);

	/* Issue write call - Either what we want to write, if possible. Otherwise, write what kernel tells us we can */
	wrote_sz			= write(fd, data_ptr, possible_write_sz);

	/* The write was interrupted by a signal or we were not able to write any data to it, reschedule and return. */
	if (wrote_sz == -1)
	{
		/* RENQUEUE AIO_REQ, either for destruction or for another write attempt */
		EvAIOReqQueueEnqueueHead(&serial_port->iodata.write_queue, cur_aio_req);
		cur_aio_req->err = errno;

		/* NON_FATAL error, re_schedule to try again on next IO loop */
		if ((!kq_fd->flags.so_write_eof) && (errno == EINTR || errno == EAGAIN))
		{
			KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - NON_FATAL WRITE ERROR - [EINTR or EAGAIN] - CAN_WRITE_SZ [%d]\n", fd, can_write_sz);

			/* SET pending write flag */
			serial_port->flags.pending_write = 1;

			/* REENQUEUE and reschedule write event */
			EvKQBaseSetEvent(ev_base, serial_port->fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventWrite, serial_port);
			return total_wrote_sz;
		}

		KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_RED, "FD [%d] - FATAL WRITE ERROR - CAN_WRITE_SZ [%d]\n", fd, can_write_sz);

		/* Has close request, invoke */
		if (serial_port->flags.close_request)
			COMM_SERIAL_FINISH(serial_port);

		return total_wrote_sz;
	}

	KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_DEBUG, LOGCOLOR_YELLOW, "FD [%d] - Wrote [%d]\n", fd, wrote_sz);
	EvKQBaseLogHexDump(data_ptr, wrote_sz, 8, 4);

	/* Write_ok, update offset and counter */
	cur_aio_req->data.offset								+= wrote_sz;
	total_wrote_sz											+= wrote_sz;

	/* TOUCH statistics */
	serial_port->statistics.total[COMM_CURRENT].byte_tx	+= wrote_sz;
	serial_port->statistics.total[COMM_CURRENT].packet_tx	+= 1;

	/* Write is complete */
	if (cur_aio_req->data.offset >= cur_aio_req->data.size)
	{
		KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - FULL write of [%d] bytes - Wanted [%lu] - Offset is now [%lu] on THREAD [%d]\n",
				fd, wrote_sz, wanted_write_sz, cur_aio_req->data.offset, thrd_id);

		/* Invoke notification CALLBACKS */
		EvAIOReqInvokeCallBacks(cur_aio_req, 1, fd, cur_aio_req->data.offset, thrd_id, serial_port);

		/* Closed flag set, we are already destroyed, just bail out */
		if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
			return total_wrote_sz;

		/* Destroy current AIO_REQ */
		EvAIOReqDestroy(cur_aio_req);

		/* Next aio_req exists, loop writing some more or reschedule write event */
		if (!EvAIOReqQueueIsEmpty(&serial_port->iodata.write_queue))
		{
			DLinkedList *aio_req_list = &serial_port->iodata.write_queue.aio_req_list;

			/* Get a reference to next element */
			cur_aio_req = (EvAIOReq*)aio_req_list->head->data;

			/* Calculate wanted data size and how many bytes we have left to write */
			wanted_write_sz		= cur_aio_req->data.size - cur_aio_req->data.offset;
			still_can_write_sz	= can_write_sz - total_wrote_sz;

			/* Keep writing as many aio_reqs the kernel allow us to */
			if (wanted_write_sz <= still_can_write_sz)
			{
				KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_GREEN, "FD [%d] - We can still write [%d] bytes qnd next aio_req wants [%d] bytes, "
						"loop to grab it\n", serial_port->fd, still_can_write_sz, wanted_write_sz);
				goto write_again;
			}
			/* No more room to write, reschedule write event */
			else
			{
				/* SET pending write flag */
				serial_port->flags.pending_write = 1;

				KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - We can still write [%d] bytes and next aio_req wants [%d] bytes, "
						"RESCHEDULE WRITE_EV\n", serial_port->fd, still_can_write_sz, wanted_write_sz);

				EvKQBaseSetEvent(ev_base, serial_port->fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventWrite, serial_port);
				return total_wrote_sz;
			}
		}
		/* Write buffer is empty */
		else
		{
			KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Finish - Wrote [%d] bytes\n", serial_port->fd, total_wrote_sz);

			/* Reset pending write flag */
			serial_port->flags.pending_write = 0;

			/* Upper layers requested to close after writing all */
			if (serial_port->flags.close_request)
			{
				KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - Upper layer set CLOSE_REQUEST, write buffer is empty - [%s]\n",
						serial_port->fd, serial_port->flags.destroy_after_close ? "DESTROYING" : "CLOSING");

				/* Destroy or close, based on flag */
				COMM_SERIAL_FINISH(serial_port);
			}

			return total_wrote_sz;
		}
	}
	/* Write has been issued PARTIAL */
	else
	{
		/* SET pending write flag */
		serial_port->flags.pending_write = 1;

		KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_YELLOW, "FD [%d] - PARTIAL write of [%d] bytes - Offset is now [%d]\n",
				serial_port->fd, wrote_sz, cur_aio_req->data.offset);

		/* REENQUEUE and reschedule write event */
		EvAIOReqQueueEnqueueHead(&serial_port->iodata.write_queue, cur_aio_req);
		EvKQBaseSetEvent(ev_base, serial_port->fd, COMM_EV_WRITE, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventWrite, serial_port);

		return total_wrote_sz;
	}

	return total_wrote_sz;
}
/**************************************************************************************************************************/
static int CommEvSerialEventEOF(int fd, int queued_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	EvKQBase *ev_base				= base_ptr;
	CommEvSerialPort *serial_port	= cb_data;
	EvBaseKQFileDesc *kq_fd			= EvKQBaseFDGrabFromArena(serial_port->kq_base, fd);

	KQBASE_LOG_PRINTF(serial_port->log_base, LOGTYPE_INFO, LOGCOLOR_CYAN, "FD [%d] - [%s] - EOF event with [%d] bytes queued\n",
			fd, serial_port->device_name_str, queued_read_sz);

	/* Mark EOFed */
	serial_port->flags.eof = 1;

	/* Do not close for now, there is data pending read */
	if (queued_read_sz > 0)
	{
		EvKQBaseSetEvent(serial_port->kq_base, fd, COMM_EV_EOF, COMM_ACTION_ADD_VOLATILE, CommEvSerialEventEOF, serial_port);
		return 0;
	}

	/* Dispatch event - This upper layer event can make serial_port->fd get disconnected, and destroy IO buffers beneath our feet */
	CommEvSerialEventInternalDispatch(serial_port, queued_read_sz, thrd_id, COMM_SERIAL_EVENT_CLOSE);

	/* Closed flag set, we are already destroyed, just bail out */
	if ((kq_fd->flags.closed) || (kq_fd->flags.closing))
		return 0;

	/* Destroy or close, based on flag */
	COMM_SERIAL_FINISH(serial_port);

	return 1;
}
/**************************************************************************************************************************/


