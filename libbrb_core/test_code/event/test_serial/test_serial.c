/*
 * test_serial.c
 *
 *  Created on: 2014-02-20
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
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE./

/* Brb Framework */
#include <libbrb_core.h>

EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;

static EvBaseKQCBH SerialEventRead;
static EvBaseKQCBH SerialEventEOF;

/**************************************************************************************************************************/
int main(int argc, char **argv)
{
	CommEvSerialPortConfig serial_config;
	EvKQBaseLogBaseConf log_conf;
	CommEvSerialPort *serial_port;
	int op_status;

	/* Clean stack */
	memset(&serial_config, 0, sizeof(CommEvSerialPortConfig));
	memset(&log_conf, 0, sizeof(EvKQBaseLogBaseConf));

	/* Populate LOG configuration */
	log_conf.flags.double_write	= 1;

	/* Create event base and log base */
	glob_ev_base	= EvKQBaseNew(NULL);
	glob_log_base	= EvKQBaseLogBaseNew(glob_ev_base, &log_conf);

	/* Populate SERIAL configuration */
	serial_config.log_base					= glob_log_base;
	serial_config.device_name_str			= argv[1];
	serial_config.parity					= COMM_SERIAL_PARITY_NONE;
	serial_config.baud						= 115200;
	serial_config.wordlen					= 8;
	serial_config.stopbits					= 1;
	serial_config.flags.empty_read_buffer	= 1;

	/* Try to open SERIAL_PORT */
	serial_port = CommEvSerialPortOpen(glob_ev_base, &serial_config);
	serial_port->log_base = glob_log_base;

	/* Set serial port events */
	op_status = CommEvSerialAIOWrite(serial_port, "AT\r\n", 5, NULL, NULL);
//	<CR><LF>
	//op_status = CommEvSerialAIOWrite(serial_port, "AT+GSN\r\n", 9, NULL, NULL);

	CommEvSerialPortEventSet(serial_port, COMM_SERIAL_EVENT_READ, SerialEventRead, NULL);

	/* Jump into event loop */
	EvKQBaseDispatch(glob_ev_base, 100);

	exit(0);

	return 1;
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static int SerialEventRead(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSerialPort *serial_port	= base_ptr;
	char *read_data					= MemBufferDeref(serial_port->iodata.read_buffer);
	int data_sz						= MemBufferGetSize(serial_port->iodata.read_buffer);

	printf("SerialEventRead - FD [%d] - READ event of [%d] bytes -> [%s]\n", fd, can_read_sz, read_data);
	printf("------------------------\n");

	/* Log data */
	EvKQBaseLogHexDump(read_data, data_sz, 8, 4);

	/* Cleanup read buffer */
	MemBufferClean(serial_port->iodata.read_buffer);
	return 1;
}
/**************************************************************************************************************************/
static int SerialEventEOF(int fd, int can_read_sz, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSerialPort *serial_port = cb_data;

	printf("SerialEventEOF - FD [%d] - EOF event [%d] bytes\n", fd, can_read_sz);
	return 1;
}
/**************************************************************************************************************************/
