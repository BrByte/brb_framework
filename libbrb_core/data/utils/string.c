/*
 * string.c
 *
 *  Created on: 2011-10-11
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *      Author: Henrique Fernandes Silveira
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

#include "../include/libbrb_core.h"

#define STRING_MAX_SIZE 65535 * 4

/**************************************************************************************************************************/
SafeString *SafeStringNew(LibDataThreadSafeType str_type, int grow_rate)
{
	SafeString *string_ptr;

	/* Create a new String-casted MemBuffer */
	string_ptr = (SafeString*)MemBufferNew(str_type, grow_rate);

	/* Return it back */
	return string_ptr;
}
/**************************************************************************************************************************/
int SafeStringClean(SafeString *str_ptr)
{
	MemBuffer *mb_ptr;

	/* Sanity check */
	if (!str_ptr)
		return 0;

	/* Cast mem_buf */
	mb_ptr = (MemBuffer*)str_ptr;

	MemBufferClean(mb_ptr);

	return 0;
}
/**************************************************************************************************************************/
int SafeStringGetSize(SafeString *str_ptr)
{
	return MemBufferGetSize((SafeString*)str_ptr);
}
/**************************************************************************************************************************/
void SafeStringDestroy(SafeString *str_ptr)
{
	MemBuffer *mb_ptr;

	/* Sanity check */
	if (!str_ptr)
		return;

	/* Cast mem_buf */
	mb_ptr = (MemBuffer*)str_ptr;

	MemBufferDestroy(mb_ptr);

	return;
}
/**************************************************************************************************************************/
int SafeStringAdd(SafeString *str_ptr, char *data, int data_sz)
{
	MemBuffer *mb_ptr;
	int new_size = 0;

	/* Cast mem_buf */
	mb_ptr = (MemBuffer*)str_ptr;

	/* Add it to mem_buf */
	new_size = MemBufferAdd(mb_ptr, data, data_sz);

	return new_size;
}
/**************************************************************************************************************************/
SafeString *SafeStringInitFmt(LibDataThreadSafeType str_type, char *data, ...)
{
	SafeString *string;
	char fmt_buf[MEMBUFFER_MAX_PRINTF];
	va_list args;
	int msg_len;

	char *buf_ptr		= (char*)&fmt_buf;
	int msg_malloc		= 0;

	/* Sanity Check */
	if (!data)
		return NULL;

	/* Probe message size */
	va_start(args, data);
	msg_len = vsnprintf(NULL, 0, data, args);
	va_end(args);

	/* Too big to fit on local stack, use heap */
	if (msg_len > (MEMBUFFER_MAX_PRINTF - 16))
	{
		buf_ptr		= malloc((msg_len + 16));
		msg_malloc	= 1;
	}

	/* Initialize VA ARGs */
	va_start(args, data);

	/* Now actually print it and NULL terminate */
	msg_len = vsnprintf(buf_ptr, MEMBUFFER_MAX_PRINTF, data, args);
	buf_ptr[msg_len] = '\0';

	/* Finish with VA ARGs list */
	va_end(args);

	/* Create a new string object */
	string = SafeStringNew(str_type, (msg_len + 16));

	/* Add it to SAFE_STRING */
	SafeStringAdd(string, buf_ptr, msg_len);

	/* Used MALLOC< release it */
	if (msg_malloc)
		free(buf_ptr);

	return string;
}
/**************************************************************************************************************************/
SafeString *SafeStringInit(LibDataThreadSafeType str_type, char *data)
{
	SafeString *string;

	int data_sz;
	int new_size;

	/* Get string size */
	for (data_sz = 0; ( (data[data_sz] != '\0') && (data_sz < STRING_MAX_SIZE) ); data_sz++);

	/* Create a new string object */
	string = SafeStringNew(str_type, data_sz);

	/* Add it to string object */
	SafeStringAdd(string, data, data_sz);

	/* Return the newly created string size */
	return string;
}
/**************************************************************************************************************************/
char *SafeStringDeref(SafeString *str_ptr)
{
	/* Sanity check */
	if (!str_ptr)
		return NULL;

	/* Return data to buffer */
	return str_ptr->data;
}
/**************************************************************************************************************************/
int SafeStringAppend(SafeString *str_ptr, char *data)
{
	int data_sz		= 0;
	int new_size	= 0;

	/* Get string size */
	for (data_sz = 0; ( (data[data_sz] != '\0') && (data_sz < STRING_MAX_SIZE) ); data_sz++);

	/* String add will return new string size */
	new_size = SafeStringAdd(str_ptr, data, data_sz);

	return new_size;

}
/**************************************************************************************************************************/
SafeString *SafeStringDup(SafeString *str_ptr)
{
	SafeString *new_str;

	new_str = (SafeString*)MemBufferDup((SafeString*)str_ptr);

	return new_str;
}
/**************************************************************************************************************************/
int SafeStringPrintf(SafeString *str_ptr, char *message, ...)
{
	char fmt_buf[MEMBUFFER_MAX_PRINTF];
	va_list args;
	int msg_len;

	char *buf_ptr		= (char*)&fmt_buf;
	int msg_malloc		= 0;
	int alloc_sz		= MEMBUFFER_MAX_PRINTF;

	/* Sanity Check */
	if ((!str_ptr) || (!message))
		return 0;

	/* Probe message size */
	va_start(args, message);
	msg_len = vsnprintf(NULL, 0, message, args);
	va_end(args);

	/* Too big to fit on local stack, use heap */
	if (msg_len > (MEMBUFFER_MAX_PRINTF - 16))
	{
		/* Set new alloc size to replace default */
		alloc_sz	= (msg_len + 16);
		buf_ptr		= malloc(alloc_sz);
		msg_malloc	= 1;
	}

	/* Initialize VA ARGs */
	va_start(args, message);

	/* Now actually print it and NULL terminate */
	msg_len = vsnprintf(buf_ptr, (alloc_sz - 1), message, args);
	buf_ptr[msg_len] = '\0';

	/* Finish with VA ARGs list */
	va_end(args);

	/* Add it to SafeString */
	SafeStringAdd(str_ptr, buf_ptr, msg_len);

	/* Used MALLOC< release it */
	if (msg_malloc)
		free(buf_ptr);

	return msg_len;
}
/**************************************************************************************************************************/
SafeString *SafeStringReplaceSubStrDup(SafeString *str_ptr, char *sub_str, char *new_sub_str, int elem_count)
{
	SafeString *new_str		= NULL;

	char *base_ptr		= NULL;
	char *lastpos_ptr	= NULL;

	int sub_str_sz		= 0;
	int new_sub_str_sz	= 0;
	int sub_str_count	= 0;
	int remaining		= 0;
	int replace_count	= 0;

	int i;

	/* Get pointer to data */
	base_ptr = SafeStringDeref(str_ptr);

	/* Get sub_string size */
	for (sub_str_sz = 0; ( (sub_str[sub_str_sz] != '\0') && (sub_str_sz < STRING_MAX_SIZE) ); sub_str_sz++);

	/* Get new sub_string size */
	for (new_sub_str_sz = 0; ( (new_sub_str[new_sub_str_sz] != '\0') && (new_sub_str_sz < STRING_MAX_SIZE) ); new_sub_str_sz++);

	/* Create new string to return */
	new_str = SafeStringNew(str_ptr->mb_type, str_ptr->capacity);

	/* Replace all substrings if elem_count = 0, otherwise, replace elem_count substrings */
	do
	{
		/* Try to find substring inside string */
		lastpos_ptr = strstr(base_ptr, sub_str);

		/* Found */
		if (lastpos_ptr)
		{
			//	printf("token found at ptr %p\n", lastpos_ptr);

			/* Add from base to token */
			SafeStringAdd(new_str, base_ptr, (lastpos_ptr - base_ptr));

			/* Add new token */
			SafeStringAdd(new_str, new_sub_str, new_sub_str_sz);

			/* Decrement elem count */
			elem_count--;
		}
		/* Not found */
		else
		{

			/* Get remaining size */
			for (remaining = 0; ( (base_ptr[remaining] != '\0') && (remaining < STRING_MAX_SIZE) ); remaining++);

			/* Add remainig data */
			SafeStringAdd(new_str, base_ptr, remaining);

			break;
		}

		/* Update base_ptr */
		base_ptr = lastpos_ptr + sub_str_sz;

	} while (base_ptr && elem_count);


	/* If any elem has been found, add remaining bytes */
	if (lastpos_ptr)
	{
		/* Get remaining size */
		for (remaining = 0; ( (base_ptr[remaining] != '\0') && (remaining < STRING_MAX_SIZE) ); remaining++);

		/* Add remainig data */
		SafeStringAdd(new_str, base_ptr, remaining);
	}

	return new_str;
}
/**************************************************************************************************************************/
