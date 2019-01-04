/*
 * string_assoc_array.c
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
StringAssocArray *StringAssocArrayNew(int capacity)
{
	StringAssocArray *str_assoc_array;

	BRB_CALLOC(str_assoc_array, 1,sizeof(StringAssocArray));

	/* Initialize internal hash table */
	str_assoc_array->hash_table = HashTableNew((HashCmpFunc*)strcmp, capacity, HashTableStringHash);

	return str_assoc_array;

}
/**************************************************************************************************************************/
void StringAssocArrayAdd(StringAssocArray *string_assoc_array, char *str_key, char *str_value)
{
	BRBHashTableItem *hash_item;
	char *key_str_ptr;
	char *value_str_ptr;

	/* Check if element is already on hash table */
	hash_item = HashTableLookup(string_assoc_array->hash_table, str_key);

	/* Yes it is */
	if (hash_item)
	{
		/* Get pointers to data */
		key_str_ptr		= hash_item->key;
		value_str_ptr	= hash_item->item;

		/* Remove from hash table */
		HashTableRemoveItem(string_assoc_array->hash_table, hash_item, 1);

		/* Free both stored key and item */
		BRB_FREE(hash_item->key);
		BRB_FREE(hash_item->item);
	}

	/* Add to internal hash table */
	HashTableAddItem(string_assoc_array->hash_table, strdup(str_key), strdup(str_value));

	/* Increment elem count */
	string_assoc_array->elem_count++;

	return;


}
/**************************************************************************************************************************/
int StringAssocArrayDelete(StringAssocArray *string_assoc_array, char *str_key)
{

	BRBHashTableItem *hash_item;
	char *key_str_ptr;
	char *value_str_ptr;

	/* Check if element exists on hash table */
	hash_item = HashTableLookup(string_assoc_array->hash_table, str_key);

	/* Yes it is */
	if (hash_item)
	{
		/* Get pointers to data */
		key_str_ptr		= hash_item->key;
		value_str_ptr	= hash_item->item;

		/* Remove from hash table */
		HashTableRemoveItem(string_assoc_array->hash_table, hash_item, 1);

		/* Free item and key */
		BRB_FREE(key_str_ptr);
		BRB_FREE(value_str_ptr);

		/* Decrement elem count */
		string_assoc_array->elem_count--;

		return 1;

	}
	/* Item not found */
	else
	{
		return 0;
	}

	return 0;

}
/**************************************************************************************************************************/
char *StringAssocArrayLookup(StringAssocArray *string_assoc_array, char *str_key)
{
	BRBHashTableItem *hash_item = NULL;

	/* Lookup into internal hash_table */
	hash_item = HashTableLookup(string_assoc_array->hash_table, str_key);

	/* Item found, return it */
	if (hash_item)
	{
		return hash_item->item;
	}
	/* Item not found, return NULL */
	else
	{
		return NULL;
	}

	return NULL;
}
/**************************************************************************************************************************/
void StringAssocArrayDestroy(StringAssocArray *string_assoc_array)
{

	BRBHashTable *hash_table;
	BRBHashTableItem *hash_item = NULL;

	char *key_str_ptr;
	char *value_str_ptr;

	if (!string_assoc_array)
		return;

	/* Cast from internal assoc array hash table */
	hash_table = string_assoc_array->hash_table;

	/* Walk the hash table, freeing stored string keys and string values */
	for (hash_item = HashTableFirstItem(hash_table); hash_item; hash_item = HashTableNextItem(hash_table))
	{
		/* Get pointers to data */
		key_str_ptr		= hash_item->key;
		value_str_ptr	= hash_item->item;

		/* Remove from hash table */
		HashTableRemoveItem(string_assoc_array->hash_table, hash_item, 1);

		/* Free item and key */
		BRB_FREE(key_str_ptr);
		BRB_FREE(value_str_ptr);

	}

	/* Free internal hash table */
	HashTableFreeMemory(hash_table);

	/* Free assoc array structure */
	BRB_FREE(string_assoc_array);

	return;
}
/**************************************************************************************************************************/
void StringAssocArrayDebugShow(StringAssocArray *string_assoc_array, FILE *fd)
{
	BRBHashTable *hash_table;
	BRBHashTableItem *walker = NULL;
	int i;

	hash_table = string_assoc_array->hash_table;

	for (i = 0, (walker = HashTableFirstItem(hash_table)); walker; (walker = HashTableNextItem(hash_table)), i++)
	{
		printf("Index: [%d] - Key [%s] - Item [%s] - Item ptr [%p]\n", i, walker->key, (char *)walker->item, walker->item);

	}

	return;
}

