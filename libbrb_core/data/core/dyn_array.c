/*
 * dyn_array.c
 *
 *  Created on: 2010-12-02
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2010 BrByte Software (Oliveira Alves & Amorim LTDA)
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

/*
 * TODO: DynArrayDeleteByPos
 */

/**************************************************************************************************************************/
/* PUBLIC FUNCTIONS
/**************************************************************************************************************************/
DynArray *DynArrayNew(LibDataThreadSafeType arr_type, int grow_rate, ITEMDESTROY_FUNC *itemdestroy_func)
{
	DynArray *dynarr_ptr;

	dynarr_ptr = calloc(1, sizeof(DynArray));

	dynarr_ptr->busy_flags	= 0;
	dynarr_ptr->elements	= 0;
	dynarr_ptr->capacity	= 0;

	/* Get a ptr to user-specific item destroy function */
	dynarr_ptr->itemdestroy_func = itemdestroy_func;

	/* Initialize it as NULL */
	dynarr_ptr->data = NULL;

	/* Set array type into structure */
	dynarr_ptr->arr_type = arr_type;

	/* If its a multithreaded safe dyn array, initialize mutex */
	if (arr_type == BRBDATA_THREAD_SAFE)
	{
		/* Initialize dyn array mutex */
		pthread_mutex_init(&dynarr_ptr->mutex, NULL);

		/* Set DynArray object status as free */
		EBIT_SET(dynarr_ptr->busy_flags, DYNARRAY_FREE);
	}

	/* grow_rate = 0 means we want default value */
	if (grow_rate == 0)
		dynarr_ptr->grow_rate = 64;
	else
		dynarr_ptr->grow_rate = grow_rate;

	return dynarr_ptr;

}
/**************************************************************************************************************************/
int DynArrayDestroy(DynArray *dyn_arr)
{
	int i;
	void *elem_ptr;

	/* Sanity checks */
	if (!dyn_arr)
		return 0;

	if (dyn_arr->arr_type == BRBDATA_THREAD_SAFE)
	{
		pthread_mutex_destroy(&dyn_arr->mutex);
	}

	/* Invoke user-specific item destroy function */
	if (dyn_arr->itemdestroy_func)
	{
		DYNARRAY_FOREACH(dyn_arr, elem_ptr)	{
			if (elem_ptr)
				dyn_arr->itemdestroy_func(elem_ptr);

		}
	}

	BRB_FREE(dyn_arr->data);
	BRB_FREE(dyn_arr);

	return 1;
}
/**************************************************************************************************************************/
int DynArrayAdd(DynArray *dyn_arr, void *new_data)
{

	/* Sanity checks */
	if (!dyn_arr)
		return 0;


	/* CRITICAL SECTION - BEGIN */
	if (dyn_arr->arr_type == BRBDATA_THREAD_SAFE)
		_DynArrayEnterCritical(dyn_arr);

	/* Check if its the first element. If true, alloc. Else realloc */
	if (dyn_arr->capacity == 0)
	{

		dyn_arr->data = (void**)calloc(dyn_arr->grow_rate, sizeof(void*));

		/* Update capacity */
		dyn_arr->capacity += dyn_arr->grow_rate;
	}
	/* Time to grow up the buffer */
	else if ( dyn_arr->capacity <= (dyn_arr->elements + 1 ))
	{
		dyn_arr->data = (void**)realloc(dyn_arr->data, ((dyn_arr->elements + dyn_arr->grow_rate) * sizeof(void*)));

		/* Clean buffer received by realloc */
		memset( &dyn_arr->data[dyn_arr->elements], 0, (dyn_arr->grow_rate * sizeof(void*)));

		/* Update capacity */
		dyn_arr->capacity += dyn_arr->grow_rate;
	}

	/* Add new ptr to array and update number of elements */
	dyn_arr->data[dyn_arr->elements] = new_data;
	dyn_arr->elements++;

	/* CRITICAL SECTION - END */
	if (dyn_arr->arr_type == BRBDATA_THREAD_SAFE)
		_DynArrayLeaveCritical(dyn_arr);

	/* Return number of elements */
	return (dyn_arr->elements - 1);
}
/**************************************************************************************************************************/
void *DynArrayGetDataByPos(DynArray *dyn_arr, int pos)
{
	/* Sanity checks */
	if (!dyn_arr)
		return 0;

	if (!dyn_arr->data)
		return 0;

	if (dyn_arr->elements < pos)
		return 0;

	if (!dyn_arr->data[pos])
		return 0;

	/* Return correct ptr */
	return dyn_arr->data[pos];

}
/**************************************************************************************************************************/
int DynArrayClean(DynArray *dyn_arr)
{
	unsigned int i;
	void *ptr;
	void *elem_ptr;

	/* Sanity check */
	if (!dyn_arr)
		return 0;

	/* Unitialized array, cant clean */
	if (dyn_arr->capacity == 0)
		return 0;

	/* CRITICAL SECTION - BEGIN */
	if (dyn_arr->arr_type == BRBDATA_THREAD_SAFE)
		_DynArrayEnterCritical(dyn_arr);

	/* Invoke user-specific item destroy function */
	if (dyn_arr->itemdestroy_func)
	{
		DYNARRAY_FOREACH(dyn_arr, elem_ptr)	{
			if (elem_ptr)
				dyn_arr->itemdestroy_func(elem_ptr);

		}
	}

	/* Zero out number of elements */
	dyn_arr->elements = 0;

	/* Shrink it back to grow rate */
	dyn_arr->data = (void**)realloc(dyn_arr->data, (dyn_arr->grow_rate * sizeof(void*)) );

	/* Update new capacity */
	dyn_arr->capacity = dyn_arr->grow_rate;

	/* CRITICAL SECTION - END */
	if (dyn_arr->arr_type == BRBDATA_THREAD_SAFE)
		_DynArrayLeaveCritical(dyn_arr);

	/* Return capacity of new array */
	return dyn_arr->capacity;

}
/**************************************************************************************************************************/
int DynArraySetNullByPos(DynArray *dyn_arr, int pos)
{
	/* Sanity checks */
	if (!dyn_arr)
		return 0;

	if (!dyn_arr->data[pos])
		return 0;

	/* CRITICAL SECTION - BEGIN */
	if (dyn_arr->arr_type == BRBDATA_THREAD_SAFE)
		_DynArrayEnterCritical(dyn_arr);

	/* Set it to NULL */
	dyn_arr->data[pos] = 0x0;

	/* CRITICAL SECTION - END */
	if (dyn_arr->arr_type == BRBDATA_THREAD_SAFE)
		_DynArrayLeaveCritical(dyn_arr);

	return 1;
}
/**************************************************************************************************************************/
/* PRIVATE LOCAL PROTOTYPES
/**************************************************************************************************************************/
int _DynArrayEnterCritical(DynArray *dyn_arr)
{
	/* Sanity Check */
	if (!dyn_arr)
		return DYNARRAY_ACQUIRE_FAIL;

	/* Lock the mutex */
	pthread_mutex_lock(&dyn_arr->mutex);

	/* Acquire dynarray */
	EBIT_CLR(dyn_arr->busy_flags, DYNARRAY_FREE);
	EBIT_SET(dyn_arr->busy_flags, DYNARRAY_BUSY);

	return DYNARRAY_ACQUIRE_SUCCESS;
}
/**************************************************************************************************************************/
int _DynArrayLeaveCritical(DynArray *dyn_arr)
{
	/* Sanity Check */
	if (!dyn_arr)
		return DYNARRAY_ACQUIRE_FAIL;

	/* Lock the mutex */
	pthread_mutex_unlock(&dyn_arr->mutex);

	/* Release dynarray */
	EBIT_CLR(dyn_arr->busy_flags, DYNARRAY_BUSY);
	EBIT_SET(dyn_arr->busy_flags, DYNARRAY_FREE);

	return DYNARRAY_ACQUIRE_SUCCESS;

}
/**************************************************************************************************************************/

