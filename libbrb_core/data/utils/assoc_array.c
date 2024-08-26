/*
 * assoc_array.c
 *
 *  Created on: 2012-01-08
 *      Author: Guilherme Amorim de Oliveira Alves <guilherme@brbyte.com>
 *      Author: Luiz Fernando Souza Softov <softov@brbyte.com>
 *
 *
 * Copyright (c) 2012 BrByte Software (Oliveira Alves & Amorim LTDA)
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
AssocArray *AssocArrayNew(LibDataThreadSafeType assoc_arr_type, int capacity, ASSOCITEM_DESTROYFUNC *itemdestroy_func)
{
	AssocArray *assoc_arr;

	/* Create a new assoc_array structure */
	 BRB_CALLOC(assoc_arr, 1, sizeof(AssocArray));

	/* Mark MT safety type - This should be done here because mutex macros use it to reference mt safety */
	assoc_arr->arr_type = assoc_arr_type;

	/* Get user defined destroy function, if any */
	assoc_arr->itemdestroy_func = itemdestroy_func;

	/* If object is MT safe, initialize internal mutex */
	ASSOCARR_MUTEX_INIT(assoc_arr);

	/* Set array state as free */
	ASSOCARR_BUSYSTATE_SETFREE(assoc_arr);

	/* Initialize internal hash table */
	assoc_arr->hash_table = HashTableNew((HashCmpFunc*)strcmp, capacity, HashTableStringHash);

	return assoc_arr;
}
/**************************************************************************************************************************/
void AssocArrayAddNoCheck(AssocArray *assoc_array, char *str_key, void *value)
{
	ASSOCITEM_DESTROYFUNC *itemdestroy_func = NULL;
	BRBHashTableItem *hash_item;
	char *key_str_ptr;
	void *value_ptr;

	/* CRITICAL SECTION - BEGIN */
	ASSOCARR_MUTEX_LOCK(assoc_array);

	/* Set array state as BUSY */
	ASSOCARR_BUSYSTATE_SETBUSY(assoc_array);

	/* Add to internal hash table */
	HashTableAddItem(assoc_array->hash_table, strdup(str_key), value);

	/* Incremente elem count */
	assoc_array->elem_count++;

	/* Set array state as FREE */
	ASSOCARR_BUSYSTATE_SETFREE(assoc_array);

	/* CRITICAL SECTION - FINISH */
	ASSOCARR_MUTEX_UNLOCK(assoc_array);

	return;
}
/**************************************************************************************************************************/
void AssocArrayAdd(AssocArray *assoc_array, char *str_key, void *value)
{
	ASSOCITEM_DESTROYFUNC *itemdestroy_func = NULL;
	BRBHashTableItem *hash_item;
	char *key_str_ptr;
	void *value_ptr;

	/* CRITICAL SECTION - BEGIN */
	ASSOCARR_MUTEX_LOCK(assoc_array);

	/* Set array state as BUSY */
	ASSOCARR_BUSYSTATE_SETBUSY(assoc_array);

	/* Check if element is already on hash table */
	hash_item = HashTableLookup(assoc_array->hash_table, str_key);

	/* Cast ptr to user defined destroy function */
	itemdestroy_func = assoc_array->itemdestroy_func;

	/* Yes it is */
	if (hash_item)
	{
		/* Get pointers to data */
		key_str_ptr	= hash_item->key;
		value_ptr	= hash_item->item;

		/* Remove from hash table */
		HashTableRemoveItem(assoc_array->hash_table, hash_item, 1);

		/* Free both stored key and item */
		BRB_FREE(key_str_ptr);

		/* Invoke private destroy callback */
		if (itemdestroy_func)
			itemdestroy_func(value_ptr);
	}

	/* Add to internal hash table */
	HashTableAddItem(assoc_array->hash_table, strdup(str_key), value);

	/* Incremente elem count */
	assoc_array->elem_count++;

	/* Set array state as FREE */
	ASSOCARR_BUSYSTATE_SETFREE(assoc_array);

	/* CRITICAL SECTION - FINISH */
	ASSOCARR_MUTEX_UNLOCK(assoc_array);

	return;
}
/**************************************************************************************************************************/
int AssocArrayDelete(AssocArray *assoc_array, char *str_key)
{
	ASSOCITEM_DESTROYFUNC *itemdestroy_func = NULL;
	BRBHashTableItem *hash_item;
	char *key_str_ptr;
	void *value_ptr;

	/* CRITICAL SECTION - BEGIN */
	ASSOCARR_MUTEX_LOCK(assoc_array);

	/* Set array state as BUSY */
	ASSOCARR_BUSYSTATE_SETBUSY(assoc_array);

	/* Check if element exists on hash table */
	hash_item = HashTableLookup(assoc_array->hash_table, str_key);

	/* Cast ptr to user defined destroy function */
	itemdestroy_func = assoc_array->itemdestroy_func;

	/* Yes it is */
	if (hash_item)
	{
		/* Get pointers to data */
		key_str_ptr	= hash_item->key;
		value_ptr	= hash_item->item;

		/* Remove from hash table */
		HashTableRemoveItem(assoc_array->hash_table, hash_item, 1);

		/* Free item and key */
		BRB_FREE(key_str_ptr);

		/* Invoke private destroy callback */
		if (itemdestroy_func)
			itemdestroy_func(value_ptr);

		/* Decrement elem count */
		assoc_array->elem_count--;

		/* Set array state as FREE */
		ASSOCARR_BUSYSTATE_SETFREE(assoc_array);

		/* CRITICAL SECTION - FINISH */
		ASSOCARR_MUTEX_UNLOCK(assoc_array);

		return 1;

	}
	/* Item not found */
	else
	{
		/* Set array state as FREE */
		ASSOCARR_BUSYSTATE_SETFREE(assoc_array);

		/* CRITICAL SECTION - FINISH */
		ASSOCARR_MUTEX_UNLOCK(assoc_array);

		return 0;
	}

	/* Set array state as FREE */
	ASSOCARR_BUSYSTATE_SETFREE(assoc_array);

	/* CRITICAL SECTION - FINISH */
	ASSOCARR_MUTEX_UNLOCK(assoc_array);

	return 0;
}
/**************************************************************************************************************************/
void *AssocArrayLookup(AssocArray *assoc_array, char *str_key)
{
	BRBHashTableItem *hash_item = NULL;

	/* CRITICAL SECTION - BEGIN */
	ASSOCARR_MUTEX_LOCK(assoc_array);

	/* Set array state as BUSY */
	ASSOCARR_BUSYSTATE_SETBUSY(assoc_array);

	/* Retrieve item from hash table */
	hash_item = HashTableLookup(assoc_array->hash_table, str_key);

	/* Set array state as FREE */
	ASSOCARR_BUSYSTATE_SETFREE(assoc_array);

	/* CRITICAL SECTION - FINISH */
	ASSOCARR_MUTEX_UNLOCK(assoc_array);

	if (hash_item)
	{
		return hash_item->item;
	}
	else
	{
		return NULL;
	}

	return NULL;
}
/**************************************************************************************************************************/
void AssocArrayDestroy(AssocArray *assoc_array)
{
	BRBHashTable *hash_table;
	char *key_str_ptr;
	void *value_ptr;

	ASSOCITEM_DESTROYFUNC *itemdestroy_func = NULL;
	BRBHashTableItem *hash_item				= NULL;

	/* Sanity check */
	if (!assoc_array)
		return;

	/* Cast from internal assoc array hash table */
	hash_table 			= assoc_array->hash_table;

	/* Cast ptr to user defined destroy function */
	itemdestroy_func 	= assoc_array->itemdestroy_func;

	for (hash_item = HashTableFirstItem(hash_table); hash_item; hash_item = HashTableNextItem(hash_table))
	{
		/* Get pointers to data */
		key_str_ptr	= hash_item->key;
		value_ptr	= hash_item->item;

		/* Remove from hash table */
		HashTableRemoveItem(assoc_array->hash_table, hash_item, 1);

		/* Free item and key */
		BRB_FREE(key_str_ptr);

		/* Invoke private destroy callback */
		if (itemdestroy_func)
			itemdestroy_func(value_ptr);

	}

	/* Destroy internal mutex */
	ASSOCARR_MUTEX_DESTROY(assoc_array);

	/* Free internal hash table */
	HashTableFreeMemory(hash_table);

	/* Free assoc array structure */
	BRB_FREE(assoc_array);

	return;
}
/**************************************************************************************************************************/
void AssocArrayDebugShow(AssocArray *assoc_array, FILE *fd)
{
	BRBHashTable *hash_table;
	BRBHashTableItem *walker = NULL;
	int i;

	/* CRITICAL SECTION - BEGIN */
	ASSOCARR_MUTEX_LOCK(assoc_array);

	/* Set array state as BUSY */
	ASSOCARR_BUSYSTATE_SETBUSY(assoc_array);

	hash_table = assoc_array->hash_table;

	for (i = 0, (walker = HashTableFirstItem(hash_table)); walker; (walker = HashTableNextItem(hash_table)), i++)
	{
		printf("Index: [%d] - Key [%s] - item ptr [%p]\n", i, walker->key, walker->item);
	}

	/* Set array state as FREE */
	ASSOCARR_BUSYSTATE_SETFREE(assoc_array);

	/* CRITICAL SECTION - FINISH */
	ASSOCARR_MUTEX_UNLOCK(assoc_array);

	return;
}
/**************************************************************************************************************************/
