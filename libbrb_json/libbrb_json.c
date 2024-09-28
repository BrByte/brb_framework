/*
 * libbrb_json.c
 *
 *  Created on: 2014-02-24
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

#include "libbrb_json.h"

/**********************************************************************************************************************/
int BrbJsonTryRealloc(void **ptr, long new_size)
{
	void *reallocated_ptr = BrbJsonRealloc(*ptr, new_size);

	if (!reallocated_ptr)
		return JSON_FAILURE;

	*ptr 	= reallocated_ptr;

	return JSON_SUCCESS;
}
/**********************************************************************************************************************/
char *BrbJsonStrNDup(const char *string, long n)
{
	char *output_string 	= (char*) BrbJsonMalloc(n + 1);

	/* Sanitize */
	if (!output_string)
		return NULL;

	output_string[n] 		= '\0';

	strncpy(output_string, string, n);

	return output_string;
}
/**********************************************************************************************************************/
int BrbJsonMemBufferAddKeySlashed(MemBuffer *mb, char *key_str, char *buf_str)
{
	/* Sanitize */
	if (!mb || !key_str)
		return -1;

	MemBufferPrintf(mb, "\"%s\": \"", key_str);

	BrbJsonMemBufferAddSlashed(mb, buf_str, -1, -1);

	MemBufferAdd(mb, "\"", 1);

	return 0;
}
/**********************************************************************************************************************/
int BrbJsonMemBufferAddKeySlashedSize(MemBuffer *mb, char *key_str, char *buf_str, int buf_sz)
{
	return BrbJsonMemBufferAddKeySlashedLimited(mb, key_str, buf_str, buf_sz, -1);
}
/**********************************************************************************************************************/
int BrbJsonMemBufferAddKeySlashedLimitedFmt(MemBuffer *mb, char *key_str, int lmt_sz, char *buf_str, ...)
{
	char fmt_buf[MEMBUFFER_MAX_PRINTF] = {0};
	va_list args;
	int msg_len;

	char *buf_ptr	= (char*)&fmt_buf;
	int msg_malloc	= 0;
	int alloc_sz	= MEMBUFFER_MAX_PRINTF;

	/* Probe message size */
	va_start(args, buf_str);
	msg_len 		= vsnprintf(NULL, 0, buf_str, args);
	va_end(args);

	/* Too big to fit on local stack, use heap */
	if (msg_len > (MEMBUFFER_MAX_PRINTF - 16))
	{
		/* Set new alloc size to replace default */
		alloc_sz		= (msg_len + 16);
		buf_ptr			= malloc(alloc_sz);
		msg_malloc		= 1;
	}

	/* Initialize VA ARGs */
	va_start(args, buf_str);

	/* Now actually print it and NULL terminate */
	msg_len 			= vsnprintf(buf_ptr, (alloc_sz - 1), buf_str, args);
	buf_ptr[msg_len] 	= '\0';

	/* Finish with VA ARGs list */
	va_end(args);

	BrbJsonMemBufferAddKeySlashedLimited(mb, key_str , buf_ptr, msg_len, lmt_sz);

	/* Used MALLOC< release it */
	if (msg_malloc)
		free(buf_ptr);

	return 0;
}
/**********************************************************************************************************************/
int BrbJsonMemBufferAddKeySlashedLimited(MemBuffer *mb, char *key_str, char *buf_str, int buf_sz, int lmt_sz)
{
	MemBufferPrintf(mb, "\"%s\": \"", key_str);

	BrbJsonMemBufferAddSlashed(mb, buf_str, buf_sz, lmt_sz);

	MemBufferAdd(mb, "\"", 1);

	return 0;
}
/**********************************************************************************************************************/
int BrbJsonMemBufferAddSlashed(MemBuffer *mb, char *buf_str, int buf_sz, int lmt_sz)
{
	int ret_size;
	int i;

	/* Sanitize */
	if (!mb || !buf_str)
		return -1;

	if (buf_sz == 0)
		return 0;

	if (buf_sz < 0)
		buf_sz 		= strlen(buf_str);

	if (buf_sz == 0)
		return 0;

	/* no limit, at least the double */
	if (lmt_sz <= 0)
		lmt_sz 		= (buf_sz * 2) + 1;

	ret_size 		= 0;

	/* Allocate memory */
	for (i = 0; i < buf_sz; i++)
	{
		/* Double slash */
		switch (buf_str[i])
		{
		case '\0':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\0", 2);
			ret_size 		= ret_size + 2;
			break;
		case '\a':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\a", 2);
			ret_size 		= ret_size + 2;
			break;
		case '\b':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\b", 2);
			ret_size 		= ret_size + 2;
			break;
		case '\f':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\f", 2);
			ret_size 		= ret_size + 2;
			break;
		case '\n':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\n", 2);
			ret_size 		= ret_size + 2;
			break;
		case '\r':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\r", 2);
			ret_size 		= ret_size + 2;
			break;
		case '\t':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\t", 2);
			ret_size 		= ret_size + 2;
			break;
		case '\v':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\v", 2);
			ret_size 		= ret_size + 2;
			break;
		case '\\':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\\\", 2);
			ret_size 		= ret_size + 2;
			break;
//		case '\?':
//			if ((ret_size + 2) > lmt_sz) return ret_size;
//			MemBufferAdd(mb, "\\?", 2);
//			ret_size 		= ret_size + 2;
//			break;
//		case '\'':
//			if ((ret_size + 2) > lmt_sz) return ret_size;
//			MemBufferAdd(mb, "\\'", 2);
//			ret_size 		= ret_size + 2;
//			break;
		case '\"':
			if ((ret_size + 2) > lmt_sz) return ret_size;
			MemBufferAdd(mb, "\\\"", 2);
			ret_size 		= ret_size + 2;
			break;
		default:
			/* DO NOT CHANGE THIS, if needed, create another function */
			if ((ret_size + 1) > lmt_sz) return ret_size;
//			if (buf_str[i] < 32)
//			{
//				MemBufferAdd(mb, "-", 1);
//				ret_size 		= ret_size + 1;
//			}
//			else
//			{
				MemBufferAdd(mb, buf_str + i, 1);
				ret_size 		= ret_size + 1;
//			}
			break;
		}
		continue;
	}

	/* NULL terminate it */
	MemBufferPutNULLTerminator(mb);

	/* Return changed size */
	return ret_size;
}
/**************************************************************************************************************************/
int BrbJsonDumpDLinkedList(DLinkedList *list, MemBuffer *json_mb, int off_start, int off_limit, BrbJsonDumpDLinkedNode *cb_func, void *cb_data)
{
	DLinkedListNode *node;
	int count_items 		= 0;
	int i;

	if (!cb_func)
		return 0;

	/* Walk on items to DUMP */
	for (i = 0, node = list->head; node; node = node->next, i++)
	{
		/* There is a start defined, honor it */
		if ((off_start > 0) && (i < off_start))
			continue;

		/* There is a limit, and we reached reached, break */
		if ((off_limit > 0) && (i > 0) && ((i - off_start) >= off_limit))
			break;

		/* Check for data */
		if (!node->data)
			continue;

		if (count_items > 0)
			MEMBUFFER_JSON_ADD_COMMA(json_mb);

		cb_func(json_mb, node->data, cb_data);
		count_items++;
		continue;
	}

	return count_items;
}
/**************************************************************************************************************************/
int BrbJsonDumpReplyList(DLinkedList *list, MemBuffer *json_mb, int off_start, int off_limit, BrbJsonDumpDLinkedNode *cb_func, void *cb_data)
{
	int count_items;

	/* Build the JSON reply */
	MEMBUFFER_JSON_BEGIN_OBJECT(json_mb);
	MEMBUFFER_JSON_ADD_BOOLEAN(json_mb, "success", 1);
	MEMBUFFER_JSON_ADD_COMMA(json_mb);
	MEMBUFFER_JSON_ADD_INT(json_mb, "total", list->size);
	MEMBUFFER_JSON_ADD_COMMA(json_mb);
	MEMBUFFER_JSON_BEGIN_ARRAY_KEY(json_mb, "results");
	count_items 		= BrbJsonDumpDLinkedList(list, json_mb, off_start, off_limit, cb_func, cb_data);
	MEMBUFFER_JSON_FINISH_ARRAY(json_mb);
	MEMBUFFER_JSON_FINISH_OBJECT(json_mb);

	return count_items;
}
/**********************************************************************************************************************/
