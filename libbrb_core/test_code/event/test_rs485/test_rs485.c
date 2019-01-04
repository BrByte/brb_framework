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

static EvBaseKQCBH mainTimerEvent;

static int glob_timerid;
static int glob_toggle = 0;

#define BRB_RS485_SPK '\02'
#define BRB_RS485_EPK '\03'
#define BRB_RS485_STX BRB_RS485_SPK
#define BRB_RS485_ETX BRB_RS485_EPK

#define BRB_ROUND_UI8_SZ(sz) ((sz / sizeof(char)) * sizeof(uint8_t))

typedef struct _BrbRS485Session
{
	CommEvSerialPort *serial_port;

	int address;
	int timer_id;

	uint8_t buffer_data[32];
	int buffer_sz;
	int buffer_max;
	uint8_t byte_cur;
	unsigned long err_cnt;

	struct
	{
		unsigned int using_pkt :1;
		unsigned int have_STX :1;
		unsigned int have_ETX :1;
		unsigned int have_MSG :1;

		unsigned int wait_first_nible :1;
	} flags;

} BrbRS485Session;

typedef enum
{
	RS485_PKT_TYPE_HANDSHAKE,

	RS485_PKT_TYPE_CMD_GET_A,
	RS485_PKT_TYPE_CMD_GET_D,

	RS485_PKT_TYPE_CMD_SET_A,
	RS485_PKT_TYPE_CMD_SET_D,
	RS485_PKT_TYPE_CMD_sET_ID,

	RS485_PKT_TYPE_REPLY,

} BrbRS485PacketType;

typedef struct _BrbRS485PacketHdr
{
	uint8_t src;
	uint8_t dst;
	uint8_t type;
	uint8_t len;
} BrbRS485PacketHdr;

typedef struct _BrbRS485Packet
{
	BrbRS485PacketHdr hdr;
	uint8_t val;
} BrbRS485Packet;

typedef struct _BrbRS485PacketHandShake
{
	BrbRS485PacketHdr hdr;
	uint8_t uuid[6];
} BrbRS485PacketHandShake;

typedef struct _BrbRS485PacketSetDigital
{
	unsigned int pin_num :4;
	unsigned int pin_mode :2;
	unsigned int pin_value :2;

} BrbRS485PacketSetDigital;

static uint8_t BrbRS485Session_CalcCRC8(const uint8_t *addr, size_t len);
static int BrbRS485Session_Reset(BrbRS485Session *rs485_sess);
static int BrbRS485Session_EncodePacket(uint8_t *pkt_ptr, int pkt_sz, char *ret_ptr, int max_sz);
int BrbRS485Session_SendMsg(BrbRS485Session *rs485_sess, uint8_t src, uint8_t dst, uint8_t *data_ptr, size_t data_sz);

int BrbRS485Session_GetPinAnalogValue(BrbRS485Session *rs485_sess, uint8_t src, uint8_t dst, unsigned short pin);
int BrbRS485Session_GetPinDigitalValue(BrbRS485Session *rs485_sess, uint8_t src, uint8_t dst, unsigned short pin);
int BrbRS485Session_SetPinDigitalValue(BrbRS485Session *rs485_sess, uint8_t src, uint8_t dst, unsigned short pin_num, unsigned short pin_mode, unsigned short pin_value);

int BrbRS485Session_SendPacket(BrbRS485Session *rs485_sess, BrbRS485Packet *rs485_pkt);

BrbRS485Session glob_rs485_sess;
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
	log_conf.flags.double_write = 1;

	/* Create event base and log base */
	glob_ev_base = EvKQBaseNew(NULL);
	glob_log_base = EvKQBaseLogBaseNew(glob_ev_base, &log_conf);

	/* Populate SERIAL configuration */
//	serial_config.log_base = glob_log_base;
	serial_config.device_name_str = "/dev/ttyu0";
	serial_config.parity = COMM_SERIAL_PARITY_NONE;
	serial_config.baud = 9600;
	serial_config.wordlen = 8;
	serial_config.stopbits = 2;
	serial_config.flags.empty_read_buffer = 1;

	/* Try to open SERIAL_PORT */
	serial_port = CommEvSerialPortOpen(glob_ev_base, &serial_config);
//	serial_port->log_base = glob_log_base;

	CommEvSerialPortEventSet(serial_port, COMM_SERIAL_EVENT_READ, SerialEventRead, NULL);

	memset(&glob_rs485_sess, 0, sizeof(EvKQBaseLogBaseConf));
	glob_rs485_sess.buffer_max = sizeof(glob_rs485_sess.buffer_data);
	glob_rs485_sess.flags.using_pkt = 1;
	glob_rs485_sess.serial_port = serial_port;

	glob_timerid = EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 500, mainTimerEvent, serial_port);

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "PID [%d] - Begin on TIMER_ID [%d]\n", getpid(), glob_timerid);

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
	CommEvSerialPort *serial_port = base_ptr;
	char *read_data = MemBufferDeref(serial_port->iodata.read_buffer);
	int data_sz = MemBufferGetSize(serial_port->iodata.read_buffer);

	/* Log data */
//	printf("SerialEventRead - FD [%d] - READ event of [%d] bytes -> [%s]\n", fd, can_read_sz, read_data);
	printf("------------------------\n");
	EvKQBaseLogHexDump(read_data, data_sz, 8, 4);

	BrbRS485Session *rs485_sess = &glob_rs485_sess;

	uint8_t *buf_ptr = (uint8_t *) read_data;
	int buff_sz = (data_sz / sizeof(char)) * sizeof(uint8_t);
	uint8_t byte_read;

//	printf("SerialEventRead - FD [%d] - READ event of [%d] [%d] [%d] bytes\n", fd, can_read_sz, data_sz, buff_sz);

	for (int i = 0; i < data_sz; i++)
	{
		byte_read = read_data[i];

		switch (byte_read)
		{
		case '\02': // start of text

//			printf("GOTA START\n");

			/* Set flags */
			rs485_sess->flags.have_STX = 1;
			rs485_sess->flags.have_ETX = 0;
			rs485_sess->flags.wait_first_nible = 1;
			rs485_sess->buffer_sz = 0;
			break;

		case '\03': // end of text
//			printf("GOTA END\n");

			/* Set flags */
			rs485_sess->flags.have_ETX = 1;

			/* show data ready */
			rs485_sess->flags.have_MSG = 1;

			/* We are not parsing a packet, so message finish here */
			if (!rs485_sess->flags.using_pkt)
			{
//				KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "GET MSG [%d] bytes\n", rs485_sess->buffer_sz);

				/* Log data */
				EvKQBaseLogHexDump(rs485_sess->buffer_data, rs485_sess->buffer_sz, 8, 4);

				/* Parse Msg Received */
//				BrbRS485Session_MsgParser(rs485_sess);
				/* Reset Message after finish */
				BrbRS485Session_Reset(rs485_sess);

			}

			break;

		default:
			/* wait until packet officially starts */
			if (!rs485_sess->flags.have_STX)
				break;

			/* Processing packet */
			if (rs485_sess->flags.using_pkt)
			{
//				printf("GOTA %d PKT %d\n", rs485_sess->flags.wait_first_nible, rs485_sess->buffer_sz);

				/* check byte is in valid form (4 bits followed by 4 bits complemented) */
				if ((byte_read >> 4) != ((byte_read & 0x0F) ^ 0x0F))
				{
					/* bad character */
					BrbRS485Session_Reset(rs485_sess);
					rs485_sess->err_cnt++;
					break;
				}

				/* convert back */
				byte_read >>= 4;

				/* high-order nibble? */
				if (rs485_sess->flags.wait_first_nible)
				{
					/* end of first nibble, just copy it */
					rs485_sess->byte_cur = byte_read;
					rs485_sess->flags.wait_first_nible = 0;
					break;
				}

				/* low-order nibble */
				rs485_sess->byte_cur <<= 4;
				rs485_sess->byte_cur |= byte_read;
				rs485_sess->flags.wait_first_nible = 1;

				/* if we have the ETX this must be the CRC */
				if (rs485_sess->flags.have_ETX)
				{
					uint8_t crc8;

					crc8 = BrbRS485Session_CalcCRC8((uint8_t *) &rs485_sess->buffer_data, rs485_sess->buffer_sz);
					if (crc8 != rs485_sess->byte_cur)
					{
						KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_RED, "BAD CRC8 0x%02x 0x%02x PKT %d\n", crc8, rs485_sess->byte_cur, rs485_sess->buffer_sz);

						/* Bad crc */
						BrbRS485Session_Reset(rs485_sess);
						rs485_sess->err_cnt++;
						break;
					}

					BrbRS485Packet *rs485_pkt = (BrbRS485Packet *) &rs485_sess->buffer_data;

					KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "GET PACKET [%d] bytes - 0x0%02x --->  0x0%02x - type %u \n",
							rs485_sess->buffer_sz, rs485_pkt->hdr.src, rs485_pkt->hdr.dst, rs485_pkt->hdr.type);

					switch (rs485_pkt->hdr.type)
					{
					case RS485_PKT_TYPE_HANDSHAKE:
					{
						BrbRS485PacketHandShake *rs485_pkt_hs = (BrbRS485PacketHandShake *) rs485_pkt;

						KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_GREEN, "GET HANDSHAKE [%02x][%02x][%02x][%02x][%02x][%02x]\n",
								rs485_pkt_hs->uuid[0], rs485_pkt_hs->uuid[1], rs485_pkt_hs->uuid[2], rs485_pkt_hs->uuid[3], rs485_pkt_hs->uuid[4], rs485_pkt_hs->uuid[5]);

						break;
					}
					default:
						/* Log data */
						EvKQBaseLogHexDump(rs485_sess->buffer_data, rs485_sess->buffer_sz, 8, 4);
						break;
					}

					rs485_sess->flags.have_MSG = 1;

					// end have ETX already
					break;
				}
			}
			else
			{
				/* We are not using packet, wait message to be processed */
				if (rs485_sess->flags.have_ETX)
					break;

				/* Just copy byte */
				rs485_sess->byte_cur = byte_read;
			}

			/* keep adding if not full */
			if (rs485_sess->buffer_sz < rs485_sess->buffer_max)
			{
				rs485_sess->buffer_data[rs485_sess->buffer_sz++] = rs485_sess->byte_cur;
				// rs485_sess->buffer_ptr[rs485_sess->buffer_sz++] = byte_read;
				rs485_sess->buffer_data[rs485_sess->buffer_sz] = '\0';
			}
			else
			{
				/* overflow, start again */
				BrbRS485Session_Reset(rs485_sess);
				rs485_sess->err_cnt++;
			}

			break;
		}
	}

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
/**/
/**/
/**************************************************************************************************************************/
static int mainTimerEvent(int timer_id, int unused, int thrd_id, void *cb_data, void *base_ptr)
{
	CommEvSerialPort *serial_port = cb_data;
	char message_str[32];
	int op_status;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_CYAN, "PID [%d] - Reschedule on TIMER_ID [%d] - OP [%d]\n", getpid(), glob_timerid, op_status);

	BrbRS485Session_SetPinDigitalValue(&glob_rs485_sess, 0x00, 0xA7, 5, 1, (((glob_toggle++) % 2) == 0));

	/* Reschedule TIMER */
	glob_timerid = EvKQBaseTimerAdd(glob_ev_base, COMM_ACTION_ADD_VOLATILE, 2000, mainTimerEvent, serial_port);

	return 1;
}
/**********************************************************************************************************************/
static int BrbRS485Session_Reset(BrbRS485Session *rs485_sess)
{
	rs485_sess->flags.have_STX = 0;
	rs485_sess->flags.have_ETX = 0;
	rs485_sess->flags.have_MSG = 0;
	rs485_sess->buffer_sz = 0;

	return 0;
}
/**********************************************************************************************************************/
static uint8_t BrbRS485Session_CalcCRC8(const uint8_t *addr, size_t len)
{
	uint8_t crc = 0;
	while (len--)
	{
		uint8_t inbyte = *addr++;
		for (uint8_t i = 8; i; i--)
		{
			uint8_t mix = (crc ^ inbyte) & 0x01;
			crc >>= 1;
			if (mix)
				crc ^= 0x8C;
			inbyte >>= 1;
			continue;
		}
	}
	return crc;
}
/**********************************************************************************************************************/
static int BrbRS485Session_EncodePacket(uint8_t *pkt_ptr, int pkt_sz, char *ret_ptr, int max_sz)
{
	int ret_sz = 0;
	int off_sz = 0;
	uint8_t crc8;
	uint8_t c;

	ret_ptr[ret_sz++] = BRB_RS485_SPK;

	while (off_sz < pkt_sz && ret_sz < (max_sz - 2))
	{
		c = pkt_ptr[off_sz] >> 4;
		ret_ptr[ret_sz++] = (c << 4) | (c ^ 0x0F);

		c = pkt_ptr[off_sz] & 0x0F;
		ret_ptr[ret_sz++] = (c << 4) | (c ^ 0x0F);

		off_sz++;
	}
	ret_ptr[ret_sz++] = BRB_RS485_EPK;

	crc8 = BrbRS485Session_CalcCRC8(pkt_ptr, pkt_sz);

	c = crc8 >> 4;
	ret_ptr[ret_sz++] = (c << 4) | (c ^ 0x0F);

	c = crc8 & 0x0F;
	ret_ptr[ret_sz++] = (c << 4) | (c ^ 0x0F);

	return ret_sz;
}
/**********************************************************************************************************************/
int BrbRS485Session_GetPinAnalogValue(BrbRS485Session *rs485_sess, uint8_t src, uint8_t dst, unsigned short pin)
{
	int op_status;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_PURPLE, "---> Send MSG: S [%d] D [%d] - pin [%u]\n", src, dst, pin);

	BrbRS485Packet *rs485_pkt = calloc(1, sizeof(BrbRS485Packet));

	rs485_pkt->hdr.src = src;
	rs485_pkt->hdr.dst = dst;
	rs485_pkt->hdr.type = RS485_PKT_TYPE_CMD_GET_A;
	rs485_pkt->hdr.len = (sizeof(BrbRS485Packet) / sizeof(uint8_t));
	rs485_pkt->val = pin;

	op_status = BrbRS485Session_SendPacket(rs485_sess, rs485_pkt);

	return op_status;
}
/**********************************************************************************************************************/
int BrbRS485Session_GetPinDigitalValue(BrbRS485Session *rs485_sess, uint8_t src, uint8_t dst, unsigned short pin)
{
	int op_status;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_PURPLE, "---> Send MSG: S [%d] D [%d] - pin [%u]\n", src, dst, pin);

	BrbRS485Packet *rs485_pkt = calloc(1, sizeof(BrbRS485Packet));

	rs485_pkt->hdr.src = src;
	rs485_pkt->hdr.dst = dst;
	rs485_pkt->hdr.type = RS485_PKT_TYPE_CMD_GET_D;
	rs485_pkt->hdr.len = (sizeof(BrbRS485Packet) / sizeof(uint8_t));
	rs485_pkt->val = pin;

	op_status = BrbRS485Session_SendPacket(rs485_sess, rs485_pkt);

	return op_status;
}
/**********************************************************************************************************************/
int BrbRS485Session_SetPinDigitalValue(BrbRS485Session *rs485_sess, uint8_t src, uint8_t dst, unsigned short pin_num, unsigned short pin_mode, unsigned short pin_value)
{
	BrbRS485PacketSetDigital *set_digital;
	int op_status;

//	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_PURPLE, "---> Send MSG: S [%d] D [%d] - pin [%u] [%u] [%u]\n", src, dst, pin_num, pin_mode, pin_value);

	BrbRS485Packet *rs485_pkt = calloc(1, sizeof(BrbRS485Packet));

	rs485_pkt->hdr.src = src;
	rs485_pkt->hdr.dst = dst;
	rs485_pkt->hdr.type = RS485_PKT_TYPE_CMD_SET_D;
	rs485_pkt->hdr.len = (sizeof(BrbRS485Packet) / sizeof(uint8_t));

	set_digital = (BrbRS485PacketSetDigital *) &rs485_pkt->val;
	set_digital->pin_num = pin_num;
	set_digital->pin_mode = pin_mode;
	set_digital->pin_value = pin_value;

	op_status = BrbRS485Session_SendPacket(rs485_sess, rs485_pkt);

	return op_status;
}
/**********************************************************************************************************************/
int BrbRS485Session_SendPacket(BrbRS485Session *rs485_sess, BrbRS485Packet *rs485_pkt)
{
	int enc_sz = 0;
	int op_status;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_PURPLE, "---> Send Packet: 0x%02x --> 0x%02x - type [%u]\n", rs485_pkt->hdr.src, rs485_pkt->hdr.dst, rs485_pkt->hdr.type);

	uint8_t enc_pkt[2048];

	enc_sz = BrbRS485Session_EncodePacket((uint8_t *) rs485_pkt, rs485_pkt->hdr.len, (char *) &enc_pkt, sizeof(enc_pkt));

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_PURPLE, "---> Send Packet: - LEN [%d] - SZ [%u] [%u]\n", rs485_pkt->hdr.len, enc_sz, sizeof(enc_pkt));

	op_status = CommEvSerialAIOWrite(rs485_sess->serial_port, enc_pkt, enc_sz, NULL, NULL);

	EvKQBaseLogHexDump(enc_pkt, enc_sz, 8, 4);

	return op_status;
}
/**********************************************************************************************************************/
int BrbRS485Session_SendMsg(BrbRS485Session *rs485_sess, uint8_t src, uint8_t dst, uint8_t *data_ptr, size_t data_sz)
{
	int bytes_write = 0;
	int op_status;

	KQBASE_LOG_PRINTF(glob_log_base, LOGTYPE_DEBUG, LOGCOLOR_PURPLE, "---> Send MSG: S [%d] D [%d] - SZ [%u] [%u]\n", src, dst, data_sz, ((sizeof(BrbRS485Packet) / sizeof(int)) * sizeof(uint8_t)));

	BrbRS485Packet *rs485_pkt = calloc(1, sizeof(BrbRS485Packet) + data_sz);
	uint8_t enc_pkt[2048];

	rs485_pkt->hdr.src = src;
	rs485_pkt->hdr.dst = dst;
	rs485_pkt->hdr.type = 0;
	rs485_pkt->hdr.len = data_sz + (sizeof(BrbRS485Packet) / sizeof(uint8_t));
	memcpy((uint8_t *) &rs485_pkt->val, data_ptr, data_sz);

	op_status = BrbRS485Session_SendPacket(rs485_sess, rs485_pkt);

	return op_status;
}
/**************************************************************************************************************************/
