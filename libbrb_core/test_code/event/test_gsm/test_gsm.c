/*
 * test_gsm.c
 *
 *  Created on: 17-10-2019
 *      Author: Paulo Lacerda
 */

#include <libbrb_core.h>
#include <libbrb_fs.h>


/*
 * Command AT+CSCS? will answer You what type of sms-encoding is used. Properly answer is "GSM", and if not, You should set it by command AT+CSCS="GSM".
 *
 *
 *
 * */
EvKQBase *glob_ev_base;
EvKQBaseLogBase *glob_log_base;

static EvBaseKQCBH SerialEventRead;
static EvBaseKQCBH SerialEventEOF;

#define GSM_MAX_NUM_SIZE	14
#define GSM_MAX_MSG_SIZE	160

typedef enum _MsgStateCode
{
	SMS_STATE_INITIAL,
	SMS_STATE_MODE,
	SMS_STATE_UCS2,
	SMS_STATE_NUM,
	SMS_STATE_TEXT,
	SMS_STATE_LAST_ITEM,
} MsgStateCode;

typedef struct _MsgBody
{
	DLinkedListNode node;
	char to[GSM_MAX_NUM_SIZE];
	char msg[GSM_MAX_MSG_SIZE];
} MsgBody;

static MsgBody *createNode(char *to, char *msg);
MemBuffer *glob_command_mb;

int msg_state = SMS_STATE_INITIAL;

static int BrbStrIConvUcs2(char *in_str, size_t in_sz, char *to_str, size_t out_sz);
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

	//Creating DlinkedList
	DLinkedList list;
	MsgBody *node;
	MsgBody *aux;
	glob_command_mb 	= MemBufferNew(BRBDATA_THREAD_UNSAFE, 256);

	DLinkedListInit(&list, BRBDATA_THREAD_UNSAFE);

	aux = createNode("+5567981079863", "teste");
	node = aux;
	DLinkedListAddHead(&list, &node->node, node);

	aux = createNode("+556781654434", "teste");
	node = aux;
	DLinkedListAddHead(&list, &node->node, node);

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
	msg_state = SMS_STATE_INITIAL;

	printf("START START START START START START \n");

	/* Set serial port events */
	op_status = CommEvSerialAIOWrite(serial_port, "AT\r\n", 5, NULL, NULL);

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
	char *data_ptr					= MemBufferDeref(serial_port->iodata.read_buffer);
	int data_sz						= MemBufferGetSize(serial_port->iodata.read_buffer);
	int op_status;
	int found_end;

	printf("------------------------ ------------------------ ------------------------\n");
	printf("FD [%d] - READ event of [%d] bytes -> [%s]\n", fd, can_read_sz, data_ptr);

	/* Log data */
	EvKQBaseLogHexDump(data_ptr, data_sz, 8, 4);

	for (int i = 0; i < data_sz; i++) {
		if (data_ptr[i] == '\n')
			found_end = 1;
		continue;
	}

	printf("------------------------\n");

	if (!found_end)
		return 0;

	switch (msg_state)
	{
	case SMS_STATE_INITIAL:
	{
		printf("-- -- -- SMS_STATE_INITIAL\n");

		MemBufferClean(glob_command_mb);
		MemBufferPrintf(glob_command_mb, "AT+CMGF=1\r");
		op_status = CommEvSerialAIOWrite(serial_port, MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), NULL, NULL);
		MemBufferClean(glob_command_mb);
		msg_state 	= SMS_STATE_MODE;


		break;
	}
	case SMS_STATE_MODE:
	{
		printf("-- -- -- SMS_STATE_MODE\n");

		MemBufferClean(glob_command_mb);
		MemBufferPrintf(glob_command_mb, "AT+CSCS=\"UCS2\"\r");
//		MemBufferPrintf(glob_command_mb, "AT\r\n");
		op_status = CommEvSerialAIOWrite(serial_port, MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), NULL, NULL);
		MemBufferClean(glob_command_mb);

		msg_state 	= SMS_STATE_UCS2;

		break;
	}
	case SMS_STATE_UCS2:
	{
		printf("-- -- -- SMS_STATE_CSCS\n");

		char dest[1048] 		= "+5567981234276";
		char dest2[1048] 		= {0};
//		char dest2[1048] 		= "+5567981234276";

		BrbStrIConvUcs2(dest, strlen(dest), dest2, sizeof(dest2));

		//	AT+CMGS="+5567981234276"
		//			0035003500360037003900380031003200330034003200370036
	//	AT+CMGS="0035003500360037003900380031003200330034003200370036"

		MemBufferClean(glob_command_mb);
		MemBufferPrintf(glob_command_mb, "AT+CMGS=\"%s\"\r", (char *)&dest2);
		EvKQBaseLogHexDump(MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), 8, 4);
		op_status = CommEvSerialAIOWrite(serial_port, MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), NULL, NULL);
		MemBufferClean(glob_command_mb);

		msg_state 	= SMS_STATE_NUM;

		break;
	}
	case SMS_STATE_NUM:
	{
		printf("-- -- -- SMS_STATE_NUM\n");

		found_end = 0;
		for (int i = 0; i < data_sz; i++) {
			if (data_ptr[i] == '>')
				found_end = 1;
			continue;
		}

		if (!found_end)
			break;


		char dest[1048] 		= "00112233";
		char dest2[1048] 		= {0};

//		BrbStrIConv("ISO-8859-1", "UTF-8", (char *)&dest, (char *)&dest2, sizeof(dest2));
//		BrbStrNormalizeStr((char *)&dest, strlen(dest), (char *)&dest2, sizeof(dest2));
//		memcpy((char *)&dest, (char *)&dest2, sizeof(dest2));
//		memset((char *)&dest2, 0, sizeof(dest2));
		BrbStrIConvUcs2((char *)&dest, strlen(dest), (char *)&dest2, sizeof(dest2));

		MemBufferClean(glob_command_mb);
		MemBufferPrintf(glob_command_mb, "%s%c", (char *)&dest2, 0x1A);

		EvKQBaseLogHexDump(MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), 8, 4);
		op_status = CommEvSerialAIOWrite(serial_port, MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), NULL, NULL);
		MemBufferClean(glob_command_mb);



//		MemBufferPrintf(glob_command_mb, "Teste");
////		MemBufferAdd(glob_command_mb, 0x1A, 1);
//
//		char message_str[512] = {0};
////		char message_str[512] = "Teste\r\n";
//
////		op_status = BrbStrIConv("ISO_8859-1", "UTF-8", MemBufferDeref(glob_command_mb), (char *)&message_str, sizeof(message_str));
//		op_status = BrbStrIConvUcs2(MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), (char *)&message_str, sizeof(message_str));
//
//		printf("-- -- -- MESSAGE CONVERT %d\n\n", op_status);
//		EvKQBaseLogHexDump((char *)&message_str, strlen(message_str), 8, 4);
//
////		BrbStrNormalizeStr(MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), (char *)&message_str, sizeof(message_str));
//		message_str[strlen(message_str)] = 0x1A;
//
//		MemBufferClean(glob_command_mb);
//		MemBufferAdd(glob_command_mb, (char *)&message_str, strlen(message_str));
////		MemBufferPrintf(glob_command_mb, "%c", 0x1A);
////		MemBufferAdd(glob_command_mb, (void *)0x1A, 1);
//
//		EvKQBaseLogHexDump(MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), 8, 4);

//		printf("-- -- -- SENDING MESSAGE %d\n\n", MemBufferGetSize(glob_command_mb));
//
//		op_status = CommEvSerialAIOWrite(serial_port, MemBufferDeref(glob_command_mb), MemBufferGetSize(glob_command_mb), NULL, NULL);
//		MemBufferClean(glob_command_mb);
		msg_state 	= SMS_STATE_TEXT;

		break;
	}
	case SMS_STATE_TEXT:
	{
		printf("-- -- -- SMS_STATE_TEXT\n");

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
static MsgBody *createNode(char *to, char *msg)
{
	MsgBody *node;

	node = calloc(1, sizeof(MsgBody));

	strlcpy((char *)&node->to, to, sizeof(node->to));
	strlcpy((char *)&node->msg, msg, sizeof(node->msg));

	return node;
}
/**************************************************************************************************************************/
static int BrbStrIConvUcs2(char *in_str, size_t in_sz, char *to_str, size_t out_sz)
{
	char converted[16000] 	= {0};
	char *outbuf 			= (char *)&converted;
	iconv_t iconv_format;
	int iconv_res;

	/* OPEN ICONV */
	iconv_format 	= iconv_open("UCS-2BE", "UTF-8");

	if (iconv_format == (iconv_t) -1)
		return -1;

	out_sz			= 16000;

	iconv_res 		= iconv(iconv_format, &in_str, &in_sz, &outbuf, &out_sz);

	/* Close ICONV */
	iconv_close(iconv_format);

	if (iconv_res < 0)
		return 0;

	char stringa[16];
	char stringa2[16];
	int i;
	for (i = 0; i < 16000 - (int) out_sz; i++)
	{
		memset(stringa, '\0', sizeof(stringa));
		memset(stringa2, '\0', sizeof(stringa2));
		sprintf(stringa, "%02X", converted[i]);

		printf("character is |%02X|\n", converted[i]);

		stringa2[0] = stringa[strlen(stringa) - 2];
		stringa2[1] = stringa[strlen(stringa) - 1];

		strncat(to_str, stringa2, ((out_sz - strlen(to_str)) - 1));	//add the received line to the buffer

		printf("stringa=%s, stringa2=%s, ucs2_out=%s\n", stringa, stringa2, to_str);
	}

	return 1;
}
/************************************************************************************************************************/
