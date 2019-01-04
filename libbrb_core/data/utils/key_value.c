/*
 * rc_key_value.c
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

#include "../include/libbrb_core.h"

/**************************************************************************************************************************/
KEY_VALUE *__KVRowNew ()
{
	KEY_VALUE *ptr;

	BRB_CALLOC(ptr, 1,sizeof(KEY_VALUE));

	ptr->key_sz = 0;
	ptr->value_sz = 0;
	ptr->lock_status = 0;

	return ptr;

}
/**************************************************************************************************************************/
KV_ARRAY_DATA *KVArrayNew ()
{
	KV_ARRAY_DATA *ptr;

	BRB_CALLOC(ptr, 1,sizeof(KV_ARRAY_DATA));

	ptr->parsed_content_list = NULL;
	ptr->parsed_content_sz = 0;

	ptr->json_data = MemBufferNew(BRBDATA_THREAD_UNSAFE, 64);

	return ptr;
}
/**************************************************************************************************************************/
int __KVArrayParse (KV_ARRAY_DATA *arrayData)
{

	int row_sz;
	char *row_ptr;
	KEY_VALUE *kv_ptr;
	StringArray *lines_arr;

	/* Explode data */
	lines_arr = StringArrayExplodeFromMemBuffer(arrayData->file_data, "\n", NULL, "#");

	/* Sanity check */
	if (!lines_arr)
		return 0;

	/* Trim white spaces */
	StringArrayTrim(lines_arr);

	STRINGARRAY_FOREACH(lines_arr, row_ptr, row_sz)
	{
		kv_ptr 	= __KVRowParse("%s", row_ptr);
		arrayData->parsed_content_list = LinkedListInsertTail(arrayData->parsed_content_list, kv_ptr);
		arrayData->parsed_content_sz++;

		kv_ptr 	= NULL;
	}

	/* StringArray Destroy */
	StringArrayDestroy(lines_arr);

	return 1;

}
/**************************************************************************************************************************/
KEY_VALUE *__KVRowParse (char *row, ...)
{
	StringArray *key_value_arr;
	KEY_VALUE *ret_ptr;
	char buf[KV_ROW_MAX];
	char *value_ptr;
	char *key_ptr;
	va_list args;
	int msg_sz;
	int count;

	/* Sanity check */
	if (!row)
		return NULL;

	/* Create final string */
	va_start(args, row);
	msg_sz = vsnprintf((char*)&buf, (KV_ROW_MAX - 2), row, args);
	buf[msg_sz] = '\0';
	va_end(args);

	/* Explode data */
	key_value_arr 	= StringArrayExplodeStr((char*)&buf, "=", NULL, NULL);

	/* Clear white spaces */
	StringArrayStripCRLF(key_value_arr);
	StringArrayTrim(key_value_arr);

	/* Count array */
	count 			= StringArrayGetElemCount(key_value_arr);

	/* Check if we have key and value */
	if (count < 2)
	{
		StringArrayDestroy(key_value_arr);
		return NULL;
	}

	/* Initialize kv_ptr */
	ret_ptr 		= __KVRowNew();

	/* Assign KEY pointer and size */
	key_ptr			= StringArrayGetDataByPos(key_value_arr, 0);
	ret_ptr->key_sz = StringArrayGetDataSizeByPos(key_value_arr, 0);

	/* Copy Key */
	strlcpy((char*)&ret_ptr->key, key_ptr, sizeof(ret_ptr->key));

	/* Sanity check */
	if (!key_ptr)
	{
		StringArrayDestroy(key_value_arr);
		BRB_FREE(ret_ptr);
		return NULL;
	}

	/* Assign VALUE pointer and size -> Copy value to ret_ptr with removeQuotes */
	value_ptr			= StringArrayGetDataByPos(key_value_arr, 1);
	ret_ptr->value_sz	= __KVRemoveQuotes(value_ptr, ret_ptr->value);

	/* Sanity check */
	if (!value_ptr)
	{
		StringArrayDestroy(key_value_arr);
		BRB_FREE(ret_ptr);
		return NULL;
	}

	/* StringArray Destroy */
	StringArrayDestroy(key_value_arr);

	return ret_ptr;
}
/**************************************************************************************************************************/
int __KVRemoveQuotes(char *src_ptr, char *dst_ptr)
{
	int len = 0, i = 0;

	/* Peeling */
	for (i = 0; src_ptr[i] != '\0'; i++)
	{
		if (src_ptr[i] != '"')
			dst_ptr[len++] = src_ptr[i];
	}

	return len;
}
/**************************************************************************************************************************/
/* Compare function */
static int __KVCompareFunc(char *target, void *data)
{
	if ((!target) || (!data))
		return 0;

	int token_pos = 0;
	int i = 0;

	KEY_VALUE *data_ptr = (KEY_VALUE*)data;

	for (i = 0; target[i] != '\0'; i++)
	{
		if (target[i] == '*')
			token_pos = i;
	}

	if (token_pos)
	{
		/* Found it, delete */
		if (!strncmp(target, data_ptr->key, token_pos ))
		{
			BRB_FREE(data_ptr);
			return 1;
		}
		else
			return 0;
	}
	else
	{

		/* Found it, delete */
		if (!strcmp(target, data_ptr->key))
		{
			BRB_FREE(data_ptr);
			return 1;
		}
		else
			return 0;
	}
}
/**************************************************************************************************************************/
int __KVAssembleToMemBuffer(KV_ARRAY_DATA *arrayData, MemBuffer *asm_buffer, int withoutQuotes)
{

	LinkedList *content_list_head;
	KEY_VALUE *kv_ptr;

	/* Sanity check */
	if (!arrayData)
		return 0;


	/* Traverse trhu all elements */
	for (content_list_head = arrayData->parsed_content_list; content_list_head; content_list_head = LinkedListNext(content_list_head))
	{
		/* Cast it */
		kv_ptr = LinkedListGetData(content_list_head);

		/* Sanity check */
		if((!kv_ptr) || (!kv_ptr->key) || (!kv_ptr->value))
			return 0;

		/* Add string representation of key:value to StringArray */
		if (withoutQuotes)
			MemBufferPrintf(asm_buffer, "%s=%s\n", kv_ptr->key, kv_ptr->value);
		else
			MemBufferPrintf(asm_buffer, "%s=\"%s\"\n", kv_ptr->key, kv_ptr->value);

	}

	return 1;
}
/**************************************************************************************************************************/
int __KVCountTokens (char *string, char token)
{

	int i;
	int token_count;

	token_count = 0;

	if (!string)
		return 0;

	for (i = 0; string[i] != '\0'; i++)
	{
		if (string[i] == token)
			token_count++;
	}

	return token_count;

}
/**************************************************************************************************************************/
KEY_VALUE *KVRowGetByKey(KV_ARRAY_DATA *arrayData, char *key)
{

	LinkedList *content_list_head;
	int key_sz, i, token_pos = 0;
	KEY_VALUE *kv_ptr;

	/* Sanity check */
	if ( (!arrayData) || (!key) )
		return 0;

	for (i = 0; key[i] != '\0'; i++)
	{
		if (key[i] == '*')
			token_pos = i;
	}

	key_sz = (token_pos ? token_pos : strlen(key));


	/* Traverse trhu all elements */
	for (content_list_head = arrayData->parsed_content_list; content_list_head; content_list_head = LinkedListNext(content_list_head))
	{
		/* Cast it */
		kv_ptr = LinkedListGetData(content_list_head);

		if (!kv_ptr)
			continue;



		/* Check if web have token * and size is equal */
		if (token_pos || ( key_sz == kv_ptr->key_sz ) )
		{
			/* Yes! */
			if (!strncmp(key, kv_ptr->key, key_sz))
			{
				/* Return corret node */
				return kv_ptr;

				break;
			}

		}
	}

	/* Not found */
	return 0;

}
/**************************************************************************************************************************/
int KVRowAdd(KV_ARRAY_DATA *arrayData, char *key, char *value)
{
	/* Sanity check */
	if ( (!arrayData) || (!key) || (!value) )
		return 0;

	/* Search for existing key */
	if (KVRowGetByKey(arrayData, key))
		return 0;

	/* Parse key, value  */
	KEY_VALUE *kv_ptr = __KVRowParse("%s=\"%s\"", key, value);

	/* Sanity check */
	if (!kv_ptr)
		return 0;

	arrayData->parsed_content_list = LinkedListInsertTail(arrayData->parsed_content_list, kv_ptr);

	return 1;
}
/**************************************************************************************************************************/
int KVRowDeleteByKey(KV_ARRAY_DATA *arrayData, char *key)
{
	/* Sanity Check */
	if ((!arrayData) || (!key))
		return 0;

	/* Local Vars */
	LinkedList *ll_ptr = arrayData->parsed_content_list;

	/* Sanity check */
	if (!ll_ptr)
		return 0;

	arrayData->parsed_content_list = LinkedListRemove(ll_ptr, key, (equalFunction)__KVCompareFunc);

	return 1;
}

/**************************************************************************************************************************/
int KVRowUpdateByKey(KV_ARRAY_DATA *arrayData, char *key, char *new_value)
{
	/* Sanity check */
	if ( (!arrayData) || (!key) || (!new_value))
		return 0;

	/* Delete data with this key */
	KVRowDeleteByKey(arrayData, key);

	/* Insert new data */
	KVRowAdd(arrayData, key, new_value);

	/* Not fucking found */
	return 1;
}
/**************************************************************************************************************************/
int KVDestroy(KV_ARRAY_DATA *arrayData)
{

	void *content_list_head;
	KEY_VALUE *kv_ptr;

	/* Sanity check */
	if (!arrayData)
		return 0;

	/* Destroy file raw data */
	MemBufferDestroy(arrayData->file_data);

	/* Destroy json data */
	MemBufferDestroy(arrayData->json_data);

	for (content_list_head = arrayData->parsed_content_list; content_list_head; content_list_head = LinkedListNext(content_list_head))
	{
		/* Get data of list */
		kv_ptr = LinkedListGetData(content_list_head);

		if (!kv_ptr)
			continue;

		BRB_FREE(kv_ptr);

	}

	LinkedListDestroy(arrayData->parsed_content_list);

	BRB_FREE(arrayData);

	return 1;
}
/**************************************************************************************************************************/
int KVJsonExport(KV_ARRAY_DATA *arrayData)
{

	void *content_list_head;
	KEY_VALUE *kv_ptr;
	char *json_buf_ptr;

	/* Sanity check */
	if (!arrayData)
		return 0;

	for (content_list_head = arrayData->parsed_content_list; content_list_head; content_list_head = LinkedListNext(content_list_head))
	{
		/* Get data of list */
		kv_ptr = LinkedListGetData(content_list_head);

		if (!kv_ptr)
			continue;

		MemBufferPrintf(arrayData->json_data, "\"%s\":\"%s\"", kv_ptr->key, kv_ptr->value);

		if (LinkedListNext(content_list_head))
			MemBufferPrintf(arrayData->json_data, ","); /* Pair delimiter */

	}

	return 1;
}
/**************************************************************************************************************************/
int KVWriteToFile (KV_ARRAY_DATA *arrayData, char *conf_path, int encFlag, int withoutQuotes)
{

	/* Assemble data to MemBuffer */
	MemBuffer *assembled_buffer =  MemBufferNew(BRBDATA_THREAD_UNSAFE, 64);

	__KVAssembleToMemBuffer(arrayData, assembled_buffer, withoutQuotes);

	if (encFlag)
		MemBufferEncryptData(assembled_buffer, KV_SEED, 0);

	MemBufferWriteToFile(assembled_buffer, conf_path);

	MemBufferDestroy(assembled_buffer);

	return 1;
}
/**************************************************************************************************************************/
KV_ARRAY_DATA *KVReadFromFile (char *conf_path, int encFlag)
{

	/* check if conf_path is ptr*/
	if (!conf_path)
		return 0;

	MemBuffer *fileBuf = MemBufferReadFromFile(conf_path);

	/* Sanity check */
	if(!fileBuf)
	{
		return 0;
	}

	if(encFlag)
		MemBufferDecryptData(fileBuf, KV_SEED, 0);


	/* Init KV_ARRAY_DATA */
	KV_ARRAY_DATA *arrayData = KVArrayNew();

	arrayData->file_data = fileBuf;

	__KVArrayParse(arrayData);

	return arrayData;

}
/**************************************************************************************************************************/
int KVArrayCount(KV_ARRAY_DATA *arrayData)
{

	char *raw_file = MemBufferDeref(arrayData->file_data);

	/* Sanity check */
	if (!raw_file)
		return 0;

	return __KVCountTokens(raw_file, '\n');
}
/**************************************************************************************************************************/
int KVShow(FILE *fd, KEY_VALUE *kv_ptr)
{
	/* Sanity check */
	if ( (!kv_ptr) )
		return 0;

	fprintf(fd, "-----------------------------------------------------\n");
	fprintf(fd, "\tkv_ptr->key = %s\n", kv_ptr->key);
	fprintf(fd, "\tkv_ptr->key_sz = %d\n", kv_ptr->key_sz);
	fprintf(fd, "\tkv_ptr->value = %s\n", kv_ptr->value);
	fprintf(fd, "\tkv_ptr->value_sz = %d\n", kv_ptr->value_sz);
	fprintf(fd, "-----------------------------------------------------\n");

	return 1;
}
/**************************************************************************************************************************/
int KvInterpTplToMemBuffer(KV_ARRAY_DATA *kv_data, char *data_str, int data_sz, MemBuffer *data_mb)
{
	KEY_VALUE *kv_item;

	int param_sz;
	int interp_state;
	int i, j;

	/* Initialize state */
	interp_state 		= BRB_KV_TPL_STATE_READING_TEXT;

	if (data_sz < 0)
		data_sz 		= strlen(data_str);

	/* Walk on chars */
	for (i = 0, j = 0; i < data_sz; i++)
	{
		switch(interp_state)
		{
		case BRB_KV_TPL_STATE_READING_TEXT:

			/* Check if is change state to SCRIPT */
			if (data_str[i] == '$')
			{
				if (data_str[i+1] == '{')
				{
					interp_state 	= BRB_KV_TPL_STATE_READING_PARAM;

					/* Go to the script */
					i++;

					break;
				}
			}

			/* Add to buffer */
			MemBufferAdd(data_mb, &data_str[i], 1);

			break;

		case BRB_KV_TPL_STATE_READING_PARAM:
		{
			/* Lookup for finish character */
			for (j = i, param_sz = 0; data_str[j] != '}' && data_str[j] != '\0'; param_sz++, j++);

			if (data_str[j] != '}')
			{
				/* Add to buffer */
				MemBufferAdd(data_mb, "${", 2);
				MemBufferAdd(data_mb, data_str, (data_sz - (i + 1)));

				return 1;
			}

			if ((param_sz > 7) && !strncmp(&data_str[i], "number:", 7))
			{

				i 	= (i + 7);
			}

			/* Finish string to skip overflow */
			data_str[j] 		= '\0';

			/* Lookup for Key Value */
			kv_item				= KVRowGetByKey(kv_data, &data_str[i]);

			/* Go back to finish character */
			data_str[j] 		= '}';

			/* Check if we have parameters */
			if (kv_item && (kv_item->value[0] != '\0') && (kv_item->value_sz > 0))
			{
				MemBufferAdd(data_mb, kv_item->value, kv_item->value_sz);
			}
			else
			{
				MemBufferPutNULLTerminator(data_mb);
			}

			/* Change state */
			interp_state 		= BRB_KV_TPL_STATE_READING_TEXT;

			/* Go to the next function */
			i = j;
			j = 0;

			break;
		}
		}
	}

	return 1;
}
/**************************************************************************************************************************/
