/*
 * string_array.c
 *
 *  Created on: 2011-11-15
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
void StringArrayStripFromEmptyPos(StringArray *str_arr)
{
	char *str_ptr;
	int str_sz;
	int empty_pos = 0;

	/* Traverse all lines of string array */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		/* Sanity check */
		if (!str_ptr)
			continue;

		/* Found empty cell */
		if (str_sz == 0)
		{
			/* Get position */
			empty_pos = _count_;
			break;
		}
	}

	/* Set new array size */
	str_arr->data->elements = empty_pos;

	return;
}
/**************************************************************************************************************************/
int StringArrayPrintf(StringArray *str_arr, char *message, ...)
{
	char fmt_buf[MEMBUFFER_MAX_PRINTF];
	va_list args;
	int msg_len;

	char *buf_ptr		= (char*)&fmt_buf;
	int msg_malloc		= 0;
	int alloc_sz		= MEMBUFFER_MAX_PRINTF;

	/* Sanity Check */
	if ((!str_arr) || (!message))
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

	/* Add it to STRING_ARRAY */
	StringArrayAddN(str_arr, buf_ptr, msg_len);

	/* Used MALLOC< release it */
	if (msg_malloc)
		free(buf_ptr);

	return 1;
}
/**************************************************************************************************************************/
void StringArrayClean(StringArray *str_arr)
{
	int ptr_need_mem = 0;


	/* Sanity check */
	if (!str_arr)
		return;

	/* StringArray not initialized, defering to clean */
	if (!MemBufferIsInit(str_arr->data->mem_buf))
		return;

	/* Calculate needed memory for pointers */
	ptr_need_mem = (int)(str_arr->grow_rate / 2);

	/* Minimum 4 pointers in advance */
	if (ptr_need_mem < 4)
		ptr_need_mem = 4;

	/* Clean string buffer */
	MemBufferClean(str_arr->data->mem_buf);

	/* Shrink both pointer table */
	BRB_REALLOC(str_arr->data->basearr, str_arr->data->basearr, (ptr_need_mem * sizeof(int)) + 1 );
	BRB_REALLOC(str_arr->data->offsetarr, str_arr->data->offsetarr, (ptr_need_mem * sizeof(int)) + 1 );

	/* Update elements and size */
	str_arr->data->elements = 0;
	str_arr->data->capacity = ptr_need_mem;



	return;


}
/**************************************************************************************************************************/
StringArray *StringArrayNew(LibDataThreadSafeType arr_type, int grow_rate)
{
	StringArray *strarr_ptr;

	/* Here we will save a alloc call by asking for memory for both structs in the same call */
	BRB_CALLOC(strarr_ptr, 1, sizeof(StringArray));
	BRB_CALLOC(strarr_ptr->data, 1, sizeof(StringArrayData));

	strarr_ptr->busy_flags				= 0;

	/* Ptr buffer info */
	strarr_ptr->data->elements			= 0;
	strarr_ptr->data->capacity			= 0;
	strarr_ptr->data->basearr			= 0;
	strarr_ptr->data->offsetarr			= 0;

	/* grow_rate = 0 means we want default value */
	if (grow_rate == 0)
		strarr_ptr->grow_rate = 128;
	else
		strarr_ptr->grow_rate = grow_rate;

	/* Data buffer info - Calculate 32x the grow rate of StringArray ptr table */
	strarr_ptr->data->mem_buf			= MemBufferNew(arr_type, (grow_rate * 32));

	/* Set array type into structure */
	strarr_ptr->arr_type = arr_type;

	/* If its a multithreaded safe string array, initialize mutex */
	if (arr_type == BRBDATA_THREAD_SAFE)
	{
		/* Initialize string array mutex */
		pthread_mutex_init(&strarr_ptr->mutex, NULL);

		/* Set StringArray object status as free */
		EBIT_SET(strarr_ptr->busy_flags, STRINGARRAY_FREE);
	}

	return strarr_ptr;

}
/**************************************************************************************************************************/
int StringArrayDestroy(StringArray *str_arr)
{
	int i;
	char *ptr;

	/* Sanity checks */
	if (!str_arr)
		return 0;

	/* If there is a mutex, destroy it */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		pthread_mutex_destroy(&str_arr->mutex);

	/* Destroy string mem buffer */
	MemBufferDestroy(str_arr->data->mem_buf);

	/* Free reference arrays */
	BRB_FREE(str_arr->data->basearr);
	BRB_FREE(str_arr->data->offsetarr);

	/* Free structure capsule */
	BRB_FREE(str_arr->data);
	BRB_FREE(str_arr);

	return 1;
}
/**************************************************************************************************************************/
int _StringArrayCheckForGrow(StringArray *str_arr)
{
	int buf_need_mem = 0;
	int ptr_need_mem = 0;

	int need_to_grow = 0;

	/* Initialize pointer table */
	if (!str_arr->data->capacity)
	{
		//	printf("_StringArrayCheckForGrow - alloc for ptrs\n");

		ptr_need_mem = (int)(str_arr->grow_rate + 1);

		/* Minimum 4 pointers in advance */
		if (ptr_need_mem < 4)
			ptr_need_mem = 4;

		/* Alloc space in both pointer table */
		BRB_CALLOC(str_arr->data->basearr, ptr_need_mem, sizeof(int));
		BRB_CALLOC(str_arr->data->offsetarr, ptr_need_mem, sizeof(int));

		/* Update capacity */
		str_arr->data->capacity = ptr_need_mem;

		/* Mark grow */
		need_to_grow = 1;

	}

	/* Time to grow pointer table */
	else if ( str_arr->data->capacity <= (str_arr->data->elements) + 1)
	{
		//	printf("_StringArrayCheckForGrow - realloc for ptrs\n");

		ptr_need_mem = (str_arr->data->capacity + str_arr->grow_rate);

		/* Realloc arrays */
		BRB_REALLOC(str_arr->data->basearr, str_arr->data->basearr, ptr_need_mem  * sizeof(int) );
		BRB_REALLOC(str_arr->data->offsetarr, str_arr->data->offsetarr, ptr_need_mem  * sizeof(int) );

		/* Clean buffer received by realloc */
		memset( (void*)&str_arr->data->basearr[str_arr->data->elements], 0, (str_arr->grow_rate * sizeof(int)) - 1);
		memset( (void*)&str_arr->data->offsetarr[str_arr->data->elements], 0, (str_arr->grow_rate * sizeof(int)) - 1);

		/* Update capacity */
		str_arr->data->capacity += str_arr->grow_rate - 1;

		/* Mark grow */
		need_to_grow = 1;

	}

	return need_to_grow;
}
/**************************************************************************************************************************/
int _StringArrayAddToInternalPTRTable(StringArray *str_arr, int base, int offset)
{
	/* Update pointer table */
	str_arr->data->basearr[str_arr->data->elements] = base;
	str_arr->data->offsetarr[str_arr->data->elements] = offset;

	/* Update elements count */
	str_arr->data->elements++;

	return 1;
}
/**************************************************************************************************************************/
int StringArrayAdd(StringArray *str_arr, char *new_string)
{
	int position = 0;
	int offset_ptr = 0;
	int new_string_sz = 0;

	MemBuffer *mem_buf;

	/* Sanity checks */
	if ( (!str_arr) || (!new_string) )
		return 0;

	/* Get new data length */
	for (new_string_sz = 0; (new_string_sz< MAX_ARRLINE_SZ) && (new_string[new_string_sz] != '\0'); new_string_sz++);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Get a reference to StringArray internal mem_buf */
	mem_buf = str_arr->data->mem_buf;

	/* Internal function to initialize and adjust buffer sizes */
	_StringArrayCheckForGrow(str_arr);

	/* Now we need to copy new buffer and update pointer and offset table */
	position = MemBufferGetSize(mem_buf);

	/* Add it to string mem_buffer */
	MemBufferAdd(mem_buf, new_string, new_string_sz);

	/* Add a NULL char to split string */
	MemBufferAppendNULL(mem_buf);

	/* Update internal pointer table */
	_StringArrayAddToInternalPTRTable(str_arr, position, new_string_sz);

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return 1;
}
/**************************************************************************************************************************/
int StringArrayAddN(StringArray *str_arr, char *new_string, int size)
{
	int position = 0;
	int offset_ptr = 0;
	//	int new_string_sz = 0;

	MemBuffer *mem_buf;

	/* Sanity checks */
	if ( (!str_arr) || (!new_string) )
		return 0;

	//	/* Get new data length */
	//	for (new_string_sz = 0; (new_string_sz< MAX_ARRLINE_SZ) && (new_string[new_string_sz] != '\0'); new_string_sz++);
	//
	//	/* Check if size is not after \0 */
	//	if (size > new_string_sz)
	//		return 0;

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Get a reference to StringArray internal mem_buf */
	mem_buf = str_arr->data->mem_buf;

	/* Internal function to initialize and adjust buffer sizes */
	_StringArrayCheckForGrow(str_arr);

	/* Now we need to copy new buffer and update pointer and offset table */
	position = MemBufferGetSize(mem_buf);

	/* Add it to string mem_buffer */
	MemBufferAdd(mem_buf, new_string, size);

	/* Add a NULL char to split string */
	MemBufferAppendNULL(mem_buf);

	/* Update internal pointer table */
	_StringArrayAddToInternalPTRTable(str_arr, position, size);

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return 1;
}
/**************************************************************************************************************************/
char *StringArrayGetDataByPos(StringArray *str_arr, int pos)
{
	char *str_ptr;
	int position;

	/* Sanity checks */
	if ( (!str_arr) || ((!str_arr->data)) )
		return NULL;

	if (str_arr->data->elements <= pos)
	{
		//	printf("StringArrayGetDataByPos - return 0\n");
		return NULL;
	}

	/* Get position */
	position = str_arr->data->basearr[pos];

	/* Get base pointer */
	str_ptr = MemBufferDeref(str_arr->data->mem_buf);

	return (char*)(str_ptr + position);
}
/**************************************************************************************************************************/
int StringArrayGetDataSizeByPos(StringArray *str_arr, int pos)
{
	/* Sanity checks */
	if ( (!str_arr) || ((!str_arr->data)) )
		return 0;

	if (str_arr->data->elements < pos)
		return 0;

	return str_arr->data->offsetarr[pos];

}
/**************************************************************************************************************************/
/**************************************************************************************************************************/
int StringArrayDeleteByPos(StringArray *str_arr, int pos)
{
	void *base_ptr		= NULL;
	void *next_base_ptr	= NULL;

	int offset		= 0;
	int next_offset	= 0;

	int i;

	/* Sanity checks */
	if (!str_arr)
		return 0;

	if (str_arr->data->elements < pos)
		return 0;

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* This is the last element, just wipe it out */
	if (str_arr->data->elements == (pos + 1) )
	{

		//	printf ("deleting last item\n");

		base_ptr = StringArrayGetDataByPos(str_arr, pos);
		offset   = StringArrayGetDataSizeByPos(str_arr, pos);

		if (!base_ptr)
		{
			/* CRITICAL SECTION - END */
			if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
				_StringArrayLeaveCritical(str_arr);

			return 0;
		}

		/* Clean buffer */
		memset(base_ptr, 0, offset);

		str_arr->data->elements--;

		/* CRITICAL SECTION - END */
		if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
			_StringArrayLeaveCritical(str_arr);

		return 1;

	}
	/* This is an element in the middle of array, delete it and work on pointer table */
	else
	{

		base_ptr = StringArrayGetDataByPos(str_arr, pos);
		offset   = StringArrayGetDataSizeByPos(str_arr, pos);

		next_base_ptr = StringArrayGetDataByPos(str_arr, (pos + 1));
		next_offset = StringArrayGetDataSizeByPos(str_arr, (pos + 1));

		if ((!base_ptr) || (!next_base_ptr))
		{
			/* CRITICAL SECTION - END */
			if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
				_StringArrayLeaveCritical(str_arr);

			return 0;
		}


		/* Adjust all pointers to new buffer */
		for (i = pos; i <= str_arr->data->elements; i++)
		{
			str_arr->data->basearr[i] = str_arr->data->basearr[i + 1];
			str_arr->data->offsetarr[i] = str_arr->data->offsetarr[i + 1];
		}

		str_arr->data->elements--;

		/* CRITICAL SECTION - END */
		if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
			_StringArrayLeaveCritical(str_arr);

		return 1;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return 0;
}
/**************************************************************************************************************************/
void StringArrayDebugShow(StringArray *str_arr, FILE *fd)
{
	char *str_ptr;
	int data_sz;
	int i;

	if (!fd)
		return;

	if (!str_arr)
	{
		fprintf(fd, "NULL StringArray\n");
		return;
	}

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);


	fprintf(fd, "------------------------------------------\n");
	fprintf(fd, "ptr table size       -> [%d]\n", str_arr->data->elements);
	fprintf(fd, "ptr table capacity   -> [%d]\n", str_arr->data->capacity);
	//	fprintf(fd, "data buffer size     -> [%d]\n", str_arr->data->buffer_sz);
	//	fprintf(fd, "data buffer capacity -> [%d]\n", str_arr->data->buffer_capacity);
	fprintf(fd, "------------------------------------------\n");


	STRINGARRAY_FOREACH(str_arr, str_ptr, data_sz)
	{

		if (str_ptr)
		{
			fprintf(fd, "line[%d] - ptr[%p] - size[%d] -> %s\n", _count_, str_ptr, data_sz, str_ptr);
		}
		else
		{
			fprintf(fd, "line[%d] - ptr[%p] - size[%d] -> %s\n", _count_, NULL, 0, "NULL");

		}
	}

	fprintf(fd, "------------------------------------------\n");



	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return;



}
/**************************************************************************************************************************/
int _StringArrayEnterCritical(StringArray *str_arr)
{

	/* Sanity Check */
	if (!str_arr)
		return STRINGARRAY_ACQUIRE_FAIL;


	/* Lock the mutex */
	pthread_mutex_lock(&str_arr->mutex);

	/* Acquire string array */
	EBIT_CLR(str_arr->busy_flags, STRINGARRAY_FREE);
	EBIT_SET(str_arr->busy_flags, STRINGARRAY_BUSY);

	return STRINGARRAY_ACQUIRE_SUCCESS;
}
/**************************************************************************************************************************/
int _StringArrayLeaveCritical(StringArray *str_arr)
{
	/* Sanity Check */
	if (!str_arr)
		return STRINGARRAY_ACQUIRE_FAIL;

	/* Lock the mutex */
	pthread_mutex_unlock(&str_arr->mutex);

	/* Release string array */
	EBIT_CLR(str_arr->busy_flags, STRINGARRAY_BUSY);
	EBIT_SET(str_arr->busy_flags, STRINGARRAY_FREE);

	return STRINGARRAY_ACQUIRE_SUCCESS;

}
/**************************************************************************************************************************/
int StringArrayUnescapeString(char *ret_ptr, char *str_ptr, char escape_byte)
{
	int i, j = 0;

	/* Remove the escape char */
	for (i = 0; str_ptr[i] != '\0'; i++)
	{
		if (str_ptr[i] == escape_byte)
			continue;

		/* Copy byte */
		ret_ptr[j++] = str_ptr[i];

	}

	return j;
}
/**************************************************************************************************************************/
void StringArrayExplodeStrInit(StringArray *arr, char *str_ptr, char *delim, char *escape, char *comment)
{
	char *buffer_ptr;
	char delim_byte;
	char escape_byte;
	char comment_byte;

	int escape_buffer_sz;
	int delim_len;
	int i;

	int base				= 0;
	int offset				= 0;
	int escape_flag			= 0;
	int found_token_flag	= 0;
	int first_token_flag	= 0;

	/* Sanity check */
	if ( (!arr) || (!str_ptr) || (!delim) )
		return;

	/* Check escape, comment and DELIM */
	escape_byte		= (escape ? escape[0] : 1);
	comment_byte	= (comment ? comment[0] : 1);
	delim_byte		= delim[0];

	/* Create stack space for buffer */
	char buffer[MEMBUFFER_MAX_PRINTF + 1];
	char escape_buffer[MEMBUFFER_MAX_PRINTF + 1];

	/* Iterate TRHU all string */
	for(i = 0; (str_ptr[i] != '\0'); i++)
	{
		/* Safety cap */
		if (i >= MEMBUFFER_MAX_PRINTF - 1)
			break;

		/* Token is the first char */
		if (i == 0 && (str_ptr[0] == delim_byte))
		{
			StringArrayAdd(arr, "");
			first_token_flag = 1;
//			i++;

			continue;
		}

		/* Found token */
		if (str_ptr[i] == delim_byte)
		{
			/* This token is escaped, keep your ass moving */
			if ( (i) && (str_ptr[i-1] == escape_byte) )
			{
				escape_flag = 1;
				continue;
			}

			/* Skip DELIM char */
			if (base || first_token_flag)
				base++;

			/* Mark found token flag and calculate TOKEN OFFSET */
			found_token_flag	= 1;
			offset				= (i - base);

			/* Copy offset bytes from base */
			memcpy( &buffer , (str_ptr + base), offset);
			buffer[offset] = '\0';

			/* Skip commented lines */
			if ((buffer[0] == comment_byte) )
			{
				/* Update base */
				base = i;
				continue;
			}

			/* This string part has the escape flag inside it. Remove it before inserting into array */
			if (escape_flag)
			{
				/* Remove the escape from the string */
				escape_buffer_sz =	StringArrayUnescapeString((char*)&escape_buffer, (char*)&buffer, escape_byte);
				escape_buffer[escape_buffer_sz] = '\0';

				/* Add it to string array */
				StringArrayAdd(arr, (char*)&escape_buffer);

				/* Reset escape flag */
				escape_flag = 0;
			}
			/* Add it to string array */
			else
				StringArrayAdd(arr, (char*)&buffer);

			/* Update base */
			base = i;
		}
		continue;
	}

	/* Code below is to add data after the last delimiter */
	if (found_token_flag)
	{
		/* Skip DELIM char */
		if (base)
			base++;

		/* Calculate token offset */
		offset = (i - base);

		/* Copy offset bytes from base */
		memcpy( &buffer , (str_ptr + base), offset);
		buffer[offset] = '\0';

		if (escape_byte)
		{
			/* Remove the escape from the string */
			escape_buffer_sz = StringArrayUnescapeString((char*)&escape_buffer, (char*)&buffer, escape_byte);
			escape_buffer[escape_buffer_sz] = '\0';
		}

		if (!(buffer[0] == comment_byte))
		{
			/* Add it to string array - Escaped */
			if (escape_byte)
				StringArrayAdd(arr, (char*)&escape_buffer);

			/* Add it to string array - Original */
			else
				StringArrayAdd(arr, (char*)&buffer);
		}

	}
	/* No tokens found, just return it as first ELEM */
	else
		StringArrayAdd(arr, str_ptr);

	/* Shrink the buffer */
	MemBufferShrink(arr->data->mem_buf);
	return;
}
/**************************************************************************************************************************/
StringArray *StringArrayExplodeStrN(char *str_ptr, char *delim, char *escape, char *comment, int str_max_sz)
{
	StringArray *arr;
	char *buffer_ptr;
	char delim_byte;
	char escape_byte;
	char comment_byte;

	int escape_buffer_sz;
	int delim_len;
	int i;

	int base				= 0;
	int offset				= 0;
	int escape_flag			= 0;
	int found_token_flag	= 0;
	int first_token_flag	= 0;

	/* Sanity check */
	if ((!str_ptr) || (!delim))
		return 0;

	/* Check escape, comment and DELIM */
	escape_byte		= (escape ? escape[0] : 1);
	comment_byte	= (comment ? comment[0] : 1);
	delim_byte		= delim[0];

	/* Create stack space for buffer */
	char buffer[MEMBUFFER_MAX_PRINTF + 1];
	char escape_buffer[MEMBUFFER_MAX_PRINTF + 1];

	if (str_max_sz < 0)
		str_max_sz = (MEMBUFFER_MAX_PRINTF - 1);

	/* Create a new StringArray to be returned */
	arr = StringArrayNew(BRBDATA_THREAD_UNSAFE, 512);

	/* Iterate TRHU all string */
	for (i = 0; ( (str_ptr[i] != '\0') && (i < str_max_sz)); i++)
	{
		/* Safety cap */
		if (i >= MEMBUFFER_MAX_PRINTF - 1)
			break;

		/* Token is the first char */
		if (i == 0 && (str_ptr[0] == delim_byte))
		{
			StringArrayAdd(arr, "");

			/* Mark first token and found token flag */
			first_token_flag = 1;
			found_token_flag = 1;

//			i++;

			continue;
		}

		/* Found token */
		if (str_ptr[i] == delim_byte)
		{
			/* This token is escaped, keep your ass moving */
			if ( (i) && (str_ptr[i-1] == escape_byte) )
			{
				escape_flag = 1;
				continue;
			}

			/* Skip DELIM char */
			if (base || first_token_flag)
				base++;

			/* Mark found token flag and calculate TOKEN OFFSET */
			found_token_flag	= 1;
			offset				= (i - base);

			/* Copy offset bytes from base */
			memcpy( &buffer , (str_ptr + base), offset);
			buffer[offset] = '\0';

			/* Skip commented lines */
			if ((buffer[0] == comment_byte) )
			{
				/* Update base */
				base = i;
				continue;
			}

			/* This string part has the escape flag inside it. Remove it before inserting into array */
			if (escape_flag)
			{
				/* Remove the escape from the string */
				escape_buffer_sz =	StringArrayUnescapeString((char*)&escape_buffer, (char*)&buffer, escape_byte);
				escape_buffer[escape_buffer_sz] = '\0';

				/* Add it to string array */
				StringArrayAdd(arr, (char*)&escape_buffer);

				/* Reset escape flag */
				escape_flag = 0;
			}
			/* Add it to string array */
			else
				StringArrayAdd(arr, (char*)&buffer);

			/* Update base */
			base = i;
		}

		continue;
	}

	/* Code below is to add data after the last delimiter */
	if (found_token_flag)
	{
		/* Skip DELIM char */
		if (base || first_token_flag)
			base++;

		/* Calculate token offset */
		offset = (i - base);

		/* Copy offset bytes from base */
		memcpy( &buffer , (str_ptr + base), offset);
		buffer[offset] = '\0';

		if (escape_byte)
		{
			/* Remove the escape from the string */
			escape_buffer_sz = StringArrayUnescapeString((char*)&escape_buffer, (char*)&buffer, escape_byte);
			escape_buffer[escape_buffer_sz] = '\0';
		}

		if (!(buffer[0] == comment_byte))
		{
			/* Add it to string array - Escaped */
			if (escape_byte)
				StringArrayAdd(arr, (char*)&escape_buffer);
			/* Add it to string array - Original */
			else
				StringArrayAdd(arr, (char*)&buffer);
		}

	}
	/* No tokens found, just return it as first ELEM */
	else
		StringArrayAddN(arr, str_ptr, i);

	/* Shrink the buffer */
	MemBufferShrink(arr->data->mem_buf);
	return arr;
}
/**************************************************************************************************************************/
StringArray *StringArrayExplodeLargeStrN(char *str_ptr, char *delim, char *escape, char *comment, int str_max_sz)
{
	StringArray *arr;
	char *escape_buffer;
	char *buffer_ptr;
	char *buffer;
	char delim_byte;
	char escape_byte;
	char comment_byte;

	int escape_buffer_sz;
	int delim_len;
	int i;

	int base				= 0;
	int offset				= 0;
	int escape_flag			= 0;
	int found_token_flag	= 0;
	int first_token_flag	= 0;

	/* Sanity check */
	if ( (!str_ptr) || (!delim) )
		return 0;

	/* Check escape, comment and DELIM */
	escape_byte		= (escape ? escape[0] : 1);
	comment_byte	= (comment ? comment[0] : 1);
	delim_byte		= delim[0];

	/* Create stack space for buffer */
	buffer			= calloc(str_max_sz + 32, sizeof(char));
	escape_buffer	= calloc(str_max_sz + 32, sizeof(char));

	/* Create a new StringArray to be returned */
	arr = StringArrayNew(BRBDATA_THREAD_UNSAFE, 8092);

	/* Iterate TRHU all string */
	for (i = 0; ((str_ptr[i] != '\0') && i < str_max_sz); i++)
	{
		/* Safety cap */
		if (i >= str_max_sz)
			break;

		/* Token is the first char */
		if (i == 0 && (str_ptr[0] == delim_byte))
		{
			StringArrayAdd(arr, "");

			/* Mark first token flag */
			first_token_flag = 1;
			found_token_flag = 1;
//			i++;

			continue;
		}

		/* Found token */
		if (str_ptr[i] == delim_byte)
		{
			/* This token is escaped, keep your ass moving */
			if ( (i) && (str_ptr[i-1] == escape_byte) )
			{
				escape_flag = 1;
				continue;
			}

			/* Skip DELIM char */
			if (base || first_token_flag)
				base++;

			/* Mark found token flag */
			found_token_flag = 1;
			offset = (i - base);

			/* Copy offset bytes from base */
			memcpy(buffer , (str_ptr + base), offset);
			buffer[offset] = '\0';

			/* Skip commented lines */
			if ((buffer[0] == comment_byte) )
			{
				/* Update base */
				base = i;
				continue;
			}

			/* This string part has the escape flag inside it. Remove it before inserting into array */
			if (escape_flag)
			{
				/* Remove the escape from the string */
				escape_buffer_sz = StringArrayUnescapeString(escape_buffer, buffer, escape_byte);
				escape_buffer[escape_buffer_sz] = '\0';

				/* Add it to string array */
				StringArrayAddN(arr, escape_buffer, escape_buffer_sz);

				/* Reset escape flag */
				escape_flag = 0;
			}
			else
				StringArrayAddN(arr, buffer, offset);

			/* Update base */
			base = i;
		}
		continue;
	}

	/* Code below is to add data after the last delimiter */
	if (found_token_flag)
	{
		/* Skip DELIM char */
		if (base || first_token_flag)
			base++;

		/* Calculate token offset */
		offset = (i - base);

		/* Copy offset bytes from base */
		memcpy(buffer , (str_ptr + base), offset);
		buffer[offset] = '\0';

		if (escape_byte)
		{
			/* Remove the escape from the string */
			escape_buffer_sz = StringArrayUnescapeString(escape_buffer, buffer, escape_byte);
			escape_buffer[escape_buffer_sz+1] = '\0';
		}

		if (!(buffer[0] == comment_byte))
		{
			/* Add it to string array - Escaped */
			if (escape_byte)
				StringArrayAddN(arr, escape_buffer, escape_buffer_sz);
			/* Add it to string array - Original */
			else
				StringArrayAddN(arr, buffer, offset);
		}

	}
	/* No tokens found, just return it as first ELEM */
	else
		StringArrayAddN(arr, str_ptr, i);

	free(buffer);
	free(escape_buffer);

	/* Shrink the buffer */
	MemBufferShrink(arr->data->mem_buf);
	return arr;
}
/**************************************************************************************************************************/
StringArray *StringArrayExplodeStr(char *str_ptr, char *delim, char *escape, char *comment)
{
	StringArray *arr;

	arr = StringArrayExplodeStrN(str_ptr, delim, escape, comment, -1);
	return arr;
}
/**************************************************************************************************************************/
StringArray *StringArrayExplodeFromFile(char *filepath, char *delim, char *escape, char *comment)
{
	MemBuffer *mb_ptr;
	StringArray *ret_array;

	/* Read file into mem_buf */
	mb_ptr = MemBufferReadFromFile(filepath);

	/* Error reading file */
	if (!mb_ptr)
		return 0;

	ret_array = StringArrayExplodeFromMemBuffer(mb_ptr, delim, escape, comment);

	/* Destroy mem_buf */
	MemBufferDestroy(mb_ptr);

	/* Return newly create string array */
	return ret_array;

}
/**************************************************************************************************************************/
StringArray *StringArrayExplodeFromMemBuffer(MemBuffer *mb_ptr, char *delim, char *escape, char *comment)
{
	StringArray *ret_array;
	char *str_ptr;

	/* Error reading file */
	if (!mb_ptr)
		return 0;

	/* Get ptr to data */
	str_ptr = MemBufferDeref(mb_ptr);

	/* Create string array */
	ret_array = StringArrayExplodeLargeStrN(str_ptr, delim, escape, comment, MemBufferGetSize(mb_ptr));

	/* Return newly create string array */
	return ret_array;

}
/**************************************************************************************************************************/
int StringArrayEscapeString(char *ret_ptr, char *str_ptr, char delim_byte, char escape_byte)
{
	int i, j = 0;
	int escaped_items = 0;

	/* Remove the escape char */
	for (i = 0; str_ptr[i] != '\0'; i++)
	{
		/* If delim present, escape it */
		if (str_ptr[i] == delim_byte)
		{
			ret_ptr[j++] = escape_byte;
			escaped_items++;
		}

		/* Copy byte */
		ret_ptr[j++] = str_ptr[i];
	}

	return escaped_items;
}
/**************************************************************************************************************************/
MemBuffer *StringArrayImplodeToMemBuffer(StringArray *str_arr, char *delim, char *escape)
{
	MemBuffer *imploded_mb;

	char *str_ptr;
	int str_sz;
	int escaped_items = 0;

	char escape_byte;
	char delim_byte;

	/* Sanity check */
	if ((!str_arr) || (!delim))
		return 0;

	/* Check escape */
	if (escape)
		escape_byte	= escape[0];
	else
		escape_byte = 0;

	/* Get delim byte */
	delim_byte = delim[0];


	/* Create a new mem_buf to hold imploded data */
	imploded_mb = MemBufferNew(str_arr->arr_type, MemBufferGetSize(str_arr->data->mem_buf));

	/* Traverse thru all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{

		if (!str_ptr)
		{
			/* Add it to mem_buf */
			MemBufferAdd(imploded_mb, (void*)&delim_byte, sizeof(delim_byte));
			continue;
		}

		/* Recalculate size */
		str_sz 			= strlen(str_ptr);

		/* Just add if this one is not the last item */
		if (str_sz == 0)
		{
			/* Avoid adding delim to the last item */
			if ((_count_ + 1) == str_arr->data->elements)
				continue;

			/* Add it to mem_buf */
			MemBufferAdd(imploded_mb, (void*)&delim_byte, sizeof(delim_byte));
			continue;

		}

		/* Alloc stack space */
		char buffer[str_sz * 2];

		/* Clean stack */
		memset(&buffer, 0, (sizeof(buffer) - 1) );

		/* Escape string */
		escaped_items = StringArrayEscapeString((char*)&buffer, str_ptr, delim_byte, escape_byte);

		/* Nothing escaped, just plain data */
		if (!escaped_items)
		{
			/* Add string to mem_buf */
			MemBufferAdd(imploded_mb, str_ptr, str_sz);
		}
		else
		{
			/* Add string to mem_buf */
			MemBufferAdd(imploded_mb, &buffer, (str_sz + escaped_items) );
		}

		/* If not last item, add delimiter */
		if ( (_count_ + 1) != str_arr->data->elements)
			MemBufferAdd(imploded_mb, (void*)&delim_byte, sizeof(delim_byte));

	}


	/* Shrink the buffer */
	MemBufferShrink(imploded_mb);

	return imploded_mb;
}
/**************************************************************************************************************************/
char *StringArrayImplodeToStr(StringArray *str_arr, char *delim, char *escape)
{
	MemBuffer *mb_ptr;
	char *str_ptr;
	void *data_ptr;

	/* Implode to mem_buf */
	mb_ptr = StringArrayImplodeToMemBuffer(str_arr, delim, escape);

	if (mb_ptr)
	{
		/* Get ptr to mem_buf data */
		data_ptr = MemBufferDeref(mb_ptr);

		/* Create space for data */
		BRB_CALLOC(str_ptr, (mb_ptr->size + 1), sizeof(char));

		/* Copy it */
		memcpy(str_ptr, data_ptr, mb_ptr->size);

		/* Destroy mem_buf */
		MemBufferDestroy(mb_ptr);

		return str_ptr;
	}

	return NULL;

}
/**************************************************************************************************************************/
int StringArrayImplodeToFile(StringArray *str_arr, char *filepath, char *delim, char *escape)
{
	MemBuffer *mb_ptr;
	int op_status;

	/* Create the mem_buf */
	mb_ptr = StringArrayImplodeToMemBuffer(str_arr, delim, escape);

	/* mem_buf created, write it to file */
	if (mb_ptr)
		op_status = MemBufferWriteToFile(mb_ptr, filepath);

	/* Destroy mem_buf */
	MemBufferDestroy(mb_ptr);

	return op_status;

}
/**************************************************************************************************************************/
int StringArrayTrim(StringArray *str_arr)
{
	char *str_ptr;
	int str_sz;
	int again_flag = 0;

	/* Sanity check */
	if (!str_arr)
		return 0;

	/* Keep doing until again_flag is false */
	do
	{
		/* Traverse all elements */
		STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz) {

			if (!str_ptr)
				continue;

			/* Delete zero_sized string inside array */
			if (str_sz == 0)
			{
				StringArrayDeleteByPos(str_arr, _count_);
				again_flag = 1;

				/* We have deleted an item inside array, update inner macro variables to reflect this new size */
				_size_--;

				break;
			}

			again_flag = 0;

		}

		/* If exploded from blank file, to prevent infinity while */
		if (!StringArrayGetElemCount(str_arr))
			again_flag = 0;

		//		printf("%d lines\n", StringArrayGetElemCount(str_arr));

	}
	while (again_flag);

	return 1;
}
/**************************************************************************************************************************/
int StringArrayStrStr(StringArray *str_arr, char *data)
{
	char *str_ptr;

	int data_sz;
	int str_sz;
	int line_count = 0;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return 0;

	/* Get data length */
	for (data_sz = 0; data[data_sz] != '\0'; data_sz++);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (strcasestr(str_ptr, data))
			line_count++;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;

}
/**************************************************************************************************************************/
int StringArrayLineHasPrefix(StringArray *str_arr, char *data)
{
	char *str_ptr;
	int data_sz;
	int str_sz;

	int line_count = 0;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return 0;

	/* Get data length */
	data_sz = strlen(data);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (data_sz > str_sz)
			continue;

		if (!strncmp(str_ptr, data, data_sz))
			line_count++;

		continue;
	}

	return line_count;
}
/**************************************************************************************************************************/
int StringArrayHasLine(StringArray *str_arr, char *data)
{
	char *str_ptr;
	int data_sz;
	int str_sz;

	int line_count = 0;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return 0;

	/* Get data length */
	data_sz = strlen(data);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (data_sz != str_sz)
			continue;

		if (!strncmp(str_ptr, data, data_sz))
			line_count++;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;

}
/**************************************************************************************************************************/
int StringArrayHasLineFmt(StringArray *str_arr, char *data, ...)
{
	char buf[MAX_ARRLINE_SZ];
	char *str_ptr;
	int data_sz;
	int str_sz;
	va_list args;

	int line_count = 0;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return 0;

	/* Build formatted message */
	va_start(args, data);
	data_sz = vsnprintf((char*)&buf, (MAX_ARRLINE_SZ - 1), data, args);
	buf[data_sz] = '\0';
	va_end(args);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (data_sz != str_sz)
			continue;

		if (!strncmp(str_ptr, (char*)&buf, data_sz))
			line_count++;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;
}
/**************************************************************************************************************************/
int StringArrayGetPosLine(StringArray *str_arr, char *data)
{
	char *str_ptr;
	int data_sz;
	int str_sz;
	int line_count = -1;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return -1;

	/* Get data length */
	data_sz = strlen(data);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (data_sz != str_sz)
			continue;

		if (!strncmp(str_ptr, data, data_sz))
			line_count = _count_;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;

}
/**************************************************************************************************************************/
int StringArrayGetPosLinePrefix(StringArray *str_arr, char *data)
{
	char *str_ptr;
	int data_sz;
	int str_sz;
	int line_count = -1;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return -1;

	/* Get data length */
	data_sz = strlen(data);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (data_sz > str_sz)
			continue;

		if (!strncmp(str_ptr, data, data_sz))
			line_count = _count_;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;

}
/**************************************************************************************************************************/
int StringArrayGetPosLinePrefixFmt(StringArray *str_arr, char *data, ...)
{
	char *str_ptr;

	char buf[MAX_ARRLINE_SZ];
	int data_sz;
	int str_sz;
	int line_count = -1;
	va_list args;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return -1;

	/* Build formatted message */
	va_start(args, data);
	data_sz = vsnprintf((char*)&buf, (MAX_ARRLINE_SZ - 1), data, args);
	buf[data_sz] = '\0';
	va_end(args);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (data_sz > str_sz)
			continue;

		if (!strncmp(str_ptr, (char*)&buf, data_sz))
			line_count = _count_;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;
}
/**************************************************************************************************************************/
int StringArrayGetPosLineFmt(StringArray *str_arr, char *data, ...)
{
	char *str_ptr;

	char buf[MAX_ARRLINE_SZ];
	int data_sz;
	int str_sz;
	int line_count = -1;
	va_list args;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return -1;

	/* Build formatted message */
	va_start(args, data);
	data_sz = vsnprintf((char*)&buf, (MAX_ARRLINE_SZ - 1), data, args);
	buf[data_sz] = '\0';
	va_end(args);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (data_sz != str_sz)
			continue;

		if (!strncmp(str_ptr, (char*)&buf, data_sz))
			line_count = _count_;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;
}
/**************************************************************************************************************************/
int StringArrayGetPartialPosLine(StringArray *str_arr, char *data)
{
	char *str_ptr;

	int data_sz;
	int str_sz;
	int line_count = -1;

	/* Sanity check */
	if ( (!str_arr) || (!data) )
		return 0;

	/* Get data length */
	data_sz = strlen(data);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (strstr(str_ptr, data))
			line_count = _count_;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;

}
/**************************************************************************************************************************/
int StringArrayGetPartialPosLineFmt(StringArray *str_arr, char *data, ...)
{
	char buf[MAX_ARRLINE_SZ];
	char *str_ptr;
	int data_sz;
	int str_sz;
	va_list args;

	int line_count = -1;

	/* Sanity check */
	if ((!str_arr) || (!data))
		return -1;

	/* Build formatted message */
	va_start(args, data);
	data_sz = vsnprintf((char*)&buf, (MAX_ARRLINE_SZ - 1), data, args);
	buf[data_sz] = '\0';
	va_end(args);

	/* CRITICAL SECTION - BEGIN */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayEnterCritical(str_arr);

	/* Traverse all elements */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		if (strnstr(str_ptr, (char*)&buf, data_sz))
			line_count = _count_;

		continue;
	}

	/* CRITICAL SECTION - END */
	if (str_arr->arr_type == BRBDATA_THREAD_SAFE)
		_StringArrayLeaveCritical(str_arr);

	return line_count;
}
/**************************************************************************************************************************/
StringArray *StringArrayDup(StringArray *str_arr)
{
	StringArray *ret_arr;
	char *str_ptr;
	int str_sz;

	/* Sanity check */
	if (!str_arr)
		return 0;

	/* Create new array */
	ret_arr = StringArrayNew(str_arr->arr_type, str_arr->grow_rate);

	/* Traverse all elements adding to new array */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		StringArrayAdd(ret_arr, str_ptr);
	}

	/* Return duplicated array */
	return ret_arr;
}
/**************************************************************************************************************************/
int StringArrayUnique(StringArray *str_arr)
{
	char *str_ptr;
	int str_sz;
	int line_count = 0;
	int deleted_lines = 0;

	/* Sanity check */
	if (!str_arr)
		return 0;

	/* Traverse all elements checking reference array */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		/* StringArray size will change with deletes, skip NULLs */
		if (!str_ptr)
			continue;

		/* Cout how many lines with this data we have */
		line_count = StringArrayHasLine(str_arr, str_ptr);

		/* If more than one entry, delete */
		while (line_count > 1)
		{
			StringArrayDeleteByPos(str_arr, _count_);
			deleted_lines++;

			/* We have deleted an item inside array, update inner macro variables to reflect this new size */
			_size_--;

			/* Check again */
			line_count = StringArrayHasLine(str_arr, str_ptr);
		}

	}

	return deleted_lines;



}
/**************************************************************************************************************************/
int StringArrayGetElemCount(StringArray *str_arr)
{
	/* Sanity check */
	if (!str_arr)
		return 0;

	return str_arr->data->elements;
}
/**************************************************************************************************************************/
void StringArrayStripCRLF(StringArray *str_arr)
{
	char *str_ptr;
	int str_sz;
	int i;
	int delta_size;

	/* Sanity check */
	if (!str_arr)
		return;

	/* Traverse all lines of string array */
	STRINGARRAY_FOREACH(str_arr, str_ptr, str_sz)
	{
		delta_size = 0;

		/* Scan line */
		for (i = 0; ((i < str_sz) && (str_ptr[i] != '\0')); i++)
		{
			/* Clean CR and LF chars */
			if ( (str_ptr[i] == '\n') || (str_ptr[i] == '\r') )
			{
				str_ptr[i] = '\0';

				/* Position where char is located */
				delta_size = str_sz - i;
			}
		}

		/* Adjust current line size */
		if (delta_size)
		{
			str_arr->data->offsetarr[_count_] -= delta_size;
		}
	}

	return;
}
/**************************************************************************************************************************/
int StringArrayProcessBufferLines(char *buffer_str, int buffer_sz, StringArrayProcessBufferLineCBH *cb_handler_ptr, void *cb_data, int min_sz, char *delim)
{
	StringArray *line_strarr;

	char *line_str;
	int line_sz;
	int lines_c = 0;

	/* sanitize */
	if (!buffer_str || (buffer_sz < min_sz) || !cb_handler_ptr)
		return 0;

	/* Explode DATA */
	line_strarr = StringArrayExplodeStrN(buffer_str, delim, NULL, NULL, buffer_sz);

	/* Clean and trim */
	StringArrayStripCRLF(line_strarr);
	StringArrayTrim(line_strarr);

	/* Process all lines */
	STRINGARRAY_FOREACH(line_strarr, line_str, line_sz)
	{
		/* Ignore too small lines */
		if (line_sz < min_sz)
			continue;

		/* Process line and add it to global drivers list */
		lines_c += cb_handler_ptr(cb_data, line_str, line_sz);
	}

	/* Destroy parsing buffer */
	StringArrayDestroy(line_strarr);

	return lines_c;
}
/**************************************************************************************************************************/
char *StringArraySearchVarIntoStrArr(StringArray *data_strarr, char *target_key_str)
{
	char key_buf[1024];
	char *cur_key_str;
	char *cur_value_offptr;
	int cur_key_sz;
	int target_key_sz;
	int i;

	/* Generate key with terminator */
	target_key_sz = snprintf((char*)&key_buf, sizeof(key_buf), "%s:", target_key_str);

	STRINGARRAY_FOREACH(data_strarr, cur_key_str, cur_key_sz)
	{
		for (i = 0; (i < cur_key_sz); i++)
			if  ((cur_key_str[i] == ':'))
				break;

		if ((i + 1) != target_key_sz)
			continue;

		/* Keys match, calculate value offset */
		if (!memcmp(cur_key_str, &key_buf, target_key_sz))
		{
			cur_value_offptr = (cur_key_str + i + 2);
			return cur_value_offptr;
		}
	}

	/* Not found */
	return NULL;
}
/**************************************************************************************************************************/



