/*
 * base64.c
 *
 *  Created on: 2011-01-25
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2011 BrByte Software (Oliveira Alves & Amorim LTDA)
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

#include "libbrb_data.h"

#define BRB_BASE64_VALUE_SZ			256
#define BRB_BASE64_RESULT_SZ		(65535 * 2)

int brb_base64_value[BRB_BASE64_VALUE_SZ];
static int brb_base64_initialized	= 0;
const char brb_base64_code[]		= "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void brb_base64_init(void);

/**************************************************************************************************************************/
char *brb_base64_decode(const char *p)
{
	static char result[BRB_BASE64_RESULT_SZ];
	int j;
	int c;
	long val;

	if (!p)
		return NULL;

	if (!brb_base64_initialized)
		brb_base64_init();

	val = c = 0;

	for (j = 0; *p && j + 4 < BRB_BASE64_RESULT_SZ; p++)
	{
		unsigned int k = ((unsigned char) *p) % BRB_BASE64_VALUE_SZ;

		if (brb_base64_value[k] < 0)
			continue;

		val <<= 6;
		val += brb_base64_value[k];

		if (++c < 4)
			continue;

		/* One quantum of four encoding characters/24 bit */
		result[j++] = val >> 16;			/* High 8 bits */
		result[j++] = (val >> 8) & 0xff;	/* Mid 8 bits */
		result[j++] = val & 0xff;			/* Low 8 bits */
		val = c = 0;

		continue;
	}

	result[j] = 0;
	return result;
}
/**************************************************************************************************************************/
int brb_base64_decode_bin(char *in_ptr, char *out_ptr, int out_sz)
{
	int j;
	int c;
	long val;

	if (!in_ptr || !out_ptr)
		return -1;

	if (!brb_base64_initialized)
		brb_base64_init();

	val = c = 0;

	for (j = 0; *in_ptr && j + 4 < out_sz; in_ptr++)
	{
		unsigned int k = ((unsigned char) *in_ptr) % BRB_BASE64_VALUE_SZ;

		if (brb_base64_value[k] < 0)
			continue;

		val <<= 6;
		val += brb_base64_value[k];

		if (++c < 4)
			continue;

		/* One quantum of four encoding characters/24 bit */
		out_ptr[j++] = val >> 16;			/* High 8 bits */
		out_ptr[j++] = (val >> 8) & 0xff;	/* Mid 8 bits */
		out_ptr[j++] = val & 0xff;			/* Low 8 bits */
		val = c = 0;

		continue;
	}

//	/* We are working with binary, skip out leading pad '=' */
//	while(j > 0 && out_ptr[j] == '\0')
//		j--;

	return j;
}
/**************************************************************************************************************************/
int brb_base64_decode_to_mb(char *in_ptr, MemBuffer *file_mb)
{
	char data_buf[8];
	int j;
	int c;
	long val;

	if (!in_ptr || !file_mb)
		return -1;

	if (!brb_base64_initialized)
		brb_base64_init();

	val = c = 0;

	for (j = 0; *in_ptr; in_ptr++)
	{
		unsigned int k = ((unsigned char) *in_ptr) % BRB_BASE64_VALUE_SZ;

		if (brb_base64_value[k] < 0)
			continue;

		val <<= 6;
		val += brb_base64_value[k];

		if (++c < 4)
			continue;

		/* One quantum of four encoding characters/24 bit */

		data_buf[0] = val >> 16;
		data_buf[1] = (val >> 8) & 0xff;
		data_buf[2] = val & 0xff;
		MemBufferAdd(file_mb, &data_buf, 3);

//		out_ptr[j++] = val >> 16;			/* High 8 bits */
//		out_ptr[j++] = (val >> 8) & 0xff;	/* Mid 8 bits */
//		out_ptr[j++] = val & 0xff;			/* Low 8 bits */
		val = c = 0;

		continue;
	}

//	/* We are working with binary, skip out leading pad '=' */
//	while(j > 0 && out_ptr[j] == '\0')
//		j--;

	return j;
}
/**************************************************************************************************************************/
const char *brb_base64_encode(const char *decoded_str)
{
	static char result[BRB_BASE64_RESULT_SZ];
	int bits = 0;
	int char_count = 0;
	int out_cnt = 0;
	int c;

	if (!decoded_str)
		return decoded_str;

	if (!brb_base64_initialized)
		brb_base64_init();

	while ((c = (unsigned char) *decoded_str++) && out_cnt < sizeof(result) - 5)
	{
		bits += c;
		char_count++;

		if (char_count == 3)
		{
			result[out_cnt++] = brb_base64_code[bits >> 18];
			result[out_cnt++] = brb_base64_code[(bits >> 12) & 0x3f];
			result[out_cnt++] = brb_base64_code[(bits >> 6) & 0x3f];
			result[out_cnt++] = brb_base64_code[bits & 0x3f];
			bits = 0;
			char_count = 0;
		}
		else
		{
			bits <<= 8;
		}

		continue;
	}

	if (char_count != 0)
	{
		bits <<= 16 - (8 * char_count);
		result[out_cnt++] = brb_base64_code[bits >> 18];
		result[out_cnt++] = brb_base64_code[(bits >> 12) & 0x3f];

		if (char_count == 1)
		{
			result[out_cnt++] = '=';
			result[out_cnt++] = '=';
		}
		else
		{
			result[out_cnt++] = brb_base64_code[(bits >> 6) & 0x3f];
			result[out_cnt++] = '=';
		}
	}

	result[out_cnt] = '\0';	/* terminate */
	return result;
}
/**************************************************************************************************************************/
const char *brb_base64_encode_bin(const char *data, int len)
{
	static char result[BRB_BASE64_RESULT_SZ];

//	brb_base64_encode_bin_into(data, len, &result, sizeof(result));
//
//	/* Very bad practice */
//	return &result;


	int bits = 0;
	int char_count = 0;
	int out_cnt = 0;

	if (!data)
		return data;

	if (!brb_base64_initialized)
		brb_base64_init();

	while (len-- && out_cnt < sizeof(result) - 5)
	{
		int c = (unsigned char) *data++;
		bits += c;
		char_count++;

		if (char_count == 3)
		{
			result[out_cnt++] = brb_base64_code[bits >> 18];
			result[out_cnt++] = brb_base64_code[(bits >> 12) & 0x3f];
			result[out_cnt++] = brb_base64_code[(bits >> 6) & 0x3f];
			result[out_cnt++] = brb_base64_code[bits & 0x3f];
			bits = 0;
			char_count = 0;
		}
		else
		{
			bits <<= 8;
		}

		continue;
	}

	if (char_count != 0)
	{
		bits <<= 16 - (8 * char_count);
		result[out_cnt++] = brb_base64_code[bits >> 18];
		result[out_cnt++] = brb_base64_code[(bits >> 12) & 0x3f];

		if (char_count == 1)
		{
			result[out_cnt++] = '=';
			result[out_cnt++] = '=';
		}
		else
		{
			result[out_cnt++] = brb_base64_code[(bits >> 6) & 0x3f];
			result[out_cnt++] = '=';
		}
	}

	result[out_cnt] = '\0';	/* terminate */
	return result;
}
/**************************************************************************************************************************/
const char *brb_base64_encode_to_mb(const char *data, int len, MemBuffer *out_mb)
{
	int bits = 0;
	int char_count = 0;

	if (!data)
		return data;

	if (!brb_base64_initialized)
		brb_base64_init();

	while (len--)
	{
		int c = (unsigned char) *data++;
		bits += c;
		char_count++;

		if (char_count == 3)
		{
			MemBufferAdd(out_mb, brb_base64_code + (bits >> 18), 1);
			MemBufferAdd(out_mb, brb_base64_code + ((bits >> 12) & 0x3f), 1);
			MemBufferAdd(out_mb, brb_base64_code + ((bits >> 6) & 0x3f), 1);
			MemBufferAdd(out_mb, brb_base64_code + (bits & 0x3f), 1);
			bits 		= 0;
			char_count 	= 0;
		}
		else
		{
			bits <<= 8;
		}
		continue;
	}

	if (char_count != 0)
	{
		bits <<= 16 - (8 * char_count);
		MemBufferAdd(out_mb, brb_base64_code + (bits >> 18), 1);
		MemBufferAdd(out_mb, brb_base64_code + ((bits >> 12) & 0x3f), 1);

		if (char_count == 1)
		{
			MemBufferAdd(out_mb, "=", 1);
			MemBufferAdd(out_mb, "=", 1);
		}
		else
		{
			MemBufferAdd(out_mb, brb_base64_code + ((bits >> 6) & 0x3f), 1);
			MemBufferAdd(out_mb, "=", 1);
		}
	}

	MemBufferPutNULLTerminator(out_mb);

	return MemBufferDeref(out_mb);
}
/**************************************************************************************************************************/
/**/
/**/
/**************************************************************************************************************************/
static void brb_base64_init(void)
{
	int i;

	for (i = 0; i < BRB_BASE64_VALUE_SZ; i++)
		brb_base64_value[i] = -1;

	for (i = 0; i < 64; i++)
		brb_base64_value[(int) brb_base64_code[i]] = i;

	brb_base64_value['=']	= 0;
	brb_base64_initialized	= 1;

	return;
}
/**************************************************************************************************************************/
int brb_base64_encode_bin_into(const char *data, int len, char *result, int result_sz)
{
	int bits		= 0;
	int char_count	= 0;
	int out_cnt		= 0;

	if (!data)
		return 0;

	if (!brb_base64_initialized)
		brb_base64_init();

	while ( (len-- && out_cnt) < (result_sz - 5) )
	{
		int c = (unsigned char) *data++;
		bits += c;
		char_count++;

		if (char_count == 3)
		{
			result[out_cnt++] = brb_base64_code[bits >> 18];
			result[out_cnt++] = brb_base64_code[(bits >> 12) & 0x3f];
			result[out_cnt++] = brb_base64_code[(bits >> 6) & 0x3f];
			result[out_cnt++] = brb_base64_code[bits & 0x3f];
			bits = 0;
			char_count = 0;
		}
		else
		{
			bits <<= 8;
		}

		continue;
	}

	if (char_count != 0)
	{
		bits <<= 16 - (8 * char_count);
		result[out_cnt++] = brb_base64_code[bits >> 18];
		result[out_cnt++] = brb_base64_code[(bits >> 12) & 0x3f];

		if (char_count == 1)
		{
			result[out_cnt++] = '=';
			result[out_cnt++] = '=';
		}
		else
		{
			result[out_cnt++] = brb_base64_code[(bits >> 6) & 0x3f];
			result[out_cnt++] = '=';
		}
	}

	result[out_cnt] = '\0';	/* terminate */
	return out_cnt;
}
/**************************************************************************************************************************/



